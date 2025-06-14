/*
 *
 * Copyright 2014 The RMG Project Developers. See the COPYRIGHT file 
 * at the top-level directory of this distribution or in the current
 * directory.
 * 
 * This file is part of RMG. 
 * RMG is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * any later version.
 *
 * RMG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
*/


#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transition.h"
#include "const.h"
#include "RmgTimer.h"
#include "rmgtypedefs.h"
#include "params.h"
#include "typedefs.h"
#include "common_prototypes.h"
#include "common_prototypes1.h"
#include "rmg_error.h"
#include "Kpoint.h"
#include <complex>
#include "RmgParallelFft.h"
#include "GlobalSums.h"
#include "Prolong.h"
#include "rmgthreads.h"
#include "RmgThread.h"
#include "Symmetry.h"
#include "Voronoi.h"



template void GetNewRho<double>(Kpoint<double> **, double *);
template void GetNewRho<std::complex<double> >(Kpoint<std::complex<double>> **, double *);

template void GetNewRhoPre<double>(Kpoint<double> **, double *);
template void GetNewRhoPre<std::complex<double> >(Kpoint<std::complex<double>> **, double *);

template void GetNewRhoPost<double>(Kpoint<double> **, double *);
template void GetNewRhoPost<std::complex<double> >(Kpoint<std::complex<double>> **, double *);

template void GetNewRhoOne<double>(State<double> *, Prolong *, double *, double);
template void GetNewRhoOne<std::complex<double>>(State<std::complex<double>> *, Prolong *, double *, double);




template <typename OrbitalType> void GetNewRho(Kpoint<OrbitalType> **Kpts, double *rho)
{
    int factor = ct.noncoll_factor * ct.noncoll_factor;
    int ratio = Rmg_G->default_FG_RATIO;
    int FP0_BASIS = Rmg_G->get_P0_BASIS(ratio);

    if(ct.fast_density)
    {
        GetNewRhoPost(Kpts, rho);
    }
    else
    {
#if HIP_ENABLED
        int ibrav = Rmg_L.get_ibrav_type();
        if(ct.prolong_order == 0)
        {
            GetNewRhoPre(Kpts, rho);
        }
        else if( (ibrav == ORTHORHOMBIC_PRIMITIVE || ibrav == CUBIC_PRIMITIVE) && ratio == 2)
        {
            GetNewRhoGpu(Kpts, rho);
        }
        else
        {
            GetNewRhoPre(Kpts, rho);
        }
#else
        GetNewRhoPre(Kpts, rho);
#endif
    }
        
    if(!ct.norm_conserving_pp) {
        double *augrho = new double[FP0_BASIS*factor]();
        GetAugRho(Kpts, augrho);
        for(int idx = 0;idx < FP0_BASIS*factor;idx++) rho[idx] += augrho[idx];
        delete [] augrho;
    }

    if(ct.is_use_symmetry)
    {
        if(Rmg_Symm) Rmg_Symm->symmetrize_grid_object(rho);
        if(ct.noncoll && Rmg_Symm)
            Rmg_Symm->symmetrize_grid_vector(&rho[FP0_BASIS]);
    }
    if (ct.nspin == 2)
        get_rho_oppo (rho,  &rho[FP0_BASIS]);


    /* Check total charge. */
    ct.tcharge = ZERO;
    for (int idx = 0; idx < FP0_BASIS; idx++)
        ct.tcharge += rho[idx];

    if(ct.AFM) ct.tcharge *=2.0;
    /* ct.tcharge = real_sum_all (ct.tcharge); */
    ct.tcharge = real_sum_all (ct.tcharge, pct.grid_comm);
    ct.tcharge = real_sum_all (ct.tcharge, pct.spin_comm);
    ct.tcharge = ct.tcharge * get_vel_f();

    /* Renormalize charge, there could be some discrepancy because of interpolation */
    double t1 = ct.nel / ct.tcharge;
    for(int i = 0;i < FP0_BASIS * factor;i++) rho[i] *= t1;

    /*Write out normalization constant if needed*/
    double difference = fabs(t1 - 1.0);
    if ((ct.verbose == 1) || (difference > 0.01))
        rmg_printf ("Charge normalization constant -1.0: %e\n", t1-1.0);

}

// Generates the new density by interpolating each orbital to the fine grid and then squaring
// and summing them. 
template <typename OrbitalType> void GetNewRhoPre(Kpoint<OrbitalType> **Kpts, double *rho)
{
    BaseThread *T = BaseThread::getBaseThread(0);
    if(Verify ("freeze_occupied", true, Kpts[0]->ControlMap)) return;
    int nstates = Kpts[0]->nstates;
    int ratio = Rmg_G->default_FG_RATIO;
    int FP0_BASIS = Rmg_G->get_P0_BASIS(ratio);
    int P0_BASIS = Rmg_G->get_P0_BASIS(1);
    static Prolong P(ratio, ct.prolong_order, ct.cmix, *Rmg_T,  Rmg_L, *Rmg_G);

    int factor = ct.noncoll_factor * ct.noncoll_factor;
    double *work = new double[FP0_BASIS * factor]();

    int active_threads = ct.MG_THREADS_PER_NODE;
    if(ct.mpi_queue_mode && (active_threads > 1)) active_threads--; 

    int istop = nstates / active_threads;
    istop = istop * active_threads;

    for (int kpt = 0; kpt < ct.num_kpts_pe; kpt++)
    {

        /* Loop over states and accumulate charge */
        for(int st1=0;st1 < istop;st1+=active_threads)
        {

            SCF_THREAD_CONTROL thread_control;

            for(int ist = 0;ist < active_threads;ist++)
            {
                thread_control.job = HYBRID_GET_RHO;
                double scale = Kpts[kpt]->Kstates[st1+ist].occupation[0] * Kpts[kpt]->kp.kweight;
                thread_control.p1 = (void *)&Kpts[kpt]->Kstates[st1+ist];
                thread_control.p2 = (void *)&P;
                thread_control.p3 = (void *)work;
                thread_control.fd_diag = scale;
                thread_control.basetag = st1 + ist;
                QueueThreadTask(ist, thread_control);
            }
            // Thread tasks are set up so wake them
            if(!ct.mpi_queue_mode) T->run_thread_tasks(active_threads);

        } 
        if(ct.mpi_queue_mode) T->run_thread_tasks(active_threads, Rmg_Q);

        for(int st1=istop;st1 < nstates;st1++)
        {
            double scale = Kpts[kpt]->Kstates[st1].occupation[0] * Kpts[kpt]->kp.kweight;
            GetNewRhoOne(&Kpts[kpt]->Kstates[st1], &P, work, scale);
        }
        MPI_Barrier(pct.grid_comm);
    }                           /*end for kpt */

    MPI_Allreduce(MPI_IN_PLACE, (double *)work, FP0_BASIS * factor, MPI_DOUBLE, MPI_SUM, pct.kpsub_comm);
    if(ct.noncoll)
    {
        double rho_up, rho_down;
        for(int idx = 0; idx < FP0_BASIS; idx++)
        {
            rho_up = work[idx];
            rho_down = work[idx+3 * FP0_BASIS];
            work[idx] = rho_up + rho_down;
            work[idx+3*FP0_BASIS] = rho_up - rho_down;
        }
    }

    for(int idx = 0; idx < FP0_BASIS * factor; idx++) rho[idx] = work[idx];
    delete [] work;
}

std::mutex rhomutex;

template <typename OrbitalType> void GetNewRhoOne(State<OrbitalType> *sp, Prolong *P, double *work, double scale)
{

    BaseThread *T = BaseThread::getBaseThread(0);
    T->thread_barrier_wait(false);
    if(scale < 1.0e-10) return;              // No need to include unoccupied orbitals

    int ratio = Rmg_G->default_FG_RATIO;
    int FP0_BASIS = Rmg_G->get_P0_BASIS(ratio);
    int P0_BASIS = Rmg_G->get_P0_BASIS(1);
    int dimx = Rmg_G->get_PX0_GRID(ratio);
    int dimy = Rmg_G->get_PY0_GRID(ratio);
    int dimz = Rmg_G->get_PZ0_GRID(ratio);
    int half_dimx = Rmg_G->get_PX0_GRID(1);
    int half_dimy = Rmg_G->get_PY0_GRID(1);
    int half_dimz = Rmg_G->get_PZ0_GRID(1);


    OrbitalType *psi = sp->psi;
    std::complex<double> psiud;
    OrbitalType *psi_f = new OrbitalType[ct.noncoll_factor * FP0_BASIS]();

    if(ct.prolong_order == 0)
        FftInterpolation(*Rmg_G, psi, psi_f, ratio, false);
    else
        P->prolong(psi_f, psi, dimx, dimy, dimz, half_dimx, half_dimy, half_dimz);

    if(ct.noncoll)
        P->prolong(&psi_f[FP0_BASIS], &psi[half_dimx*half_dimy*half_dimz], dimx, dimy, dimz, half_dimx, half_dimy, half_dimz);


    double sum1 = 1.0;
    if(ct.norm_conserving_pp)
    {
        sum1 = 0.0;
        for (int idx = 0; idx < FP0_BASIS*ct.noncoll_factor; idx++) sum1 += std::norm(psi_f[idx]);
        GlobalSums(&sum1, 1, pct.grid_comm);
        sum1 = 1.0 / sum1 / get_vel_f();
    }

    rhomutex.lock();
    for (int idx = 0; idx < FP0_BASIS; idx++)
    {
        work[idx] += sum1 * scale * std::norm(psi_f[idx]);
        if(ct.noncoll)
        {
            psiud = 2.0 * psi_f[idx] * std::conj(psi_f[idx + FP0_BASIS]);
            work[idx + 1 * FP0_BASIS] += sum1 * scale * std::real(psiud);
            work[idx + 2 * FP0_BASIS] += sum1 * scale * std::imag(psiud);
            work[idx + 3 * FP0_BASIS] += sum1 * scale * std::norm(psi_f[idx + FP0_BASIS]);
        }
    }                   /* end for */
    rhomutex.unlock();

    delete [] psi_f;

}


// Computes the sum of the squares of the orbitals on the coarse grid and then does a single
// interpolation of the resulting charge density to the fine grid. Faster but less accurate.

template <typename OrbitalType> void GetNewRhoPost(Kpoint<OrbitalType> **Kpts, double *rho)
{

    int pbasis = Kpts[0]->pbasis;
    int nstates = Kpts[0]->nstates;

    if(Verify ("freeze_occupied", true, Kpts[0]->ControlMap)) return;

    int factor = ct.noncoll_factor * ct.noncoll_factor;
    double *work = new double[pbasis * factor];
    // This is for a specific kpoint and spin channel
    double tcharge = 0.0;

    for(int idx = 0;idx < pbasis * factor;idx++)
        work[idx] = 0.0;

    for (int kpt = 0; kpt < ct.num_kpts_pe; kpt++)
    {
#pragma omp parallel
        {
            double *tarr = new double[pbasis * factor]();
            double tcharge1 = 0.0;
            std::complex<double> psiud;
#pragma omp barrier

            /* Loop over states and accumulate charge */
#pragma omp for schedule(static, 1) nowait
            for (int istate = 0; istate < nstates; istate++)
            {

                double scale = Kpts[kpt]->Kstates[istate].occupation[0] * Kpts[kpt]->kp.kweight;
                tcharge1 += scale;

                OrbitalType *psi = Kpts[kpt]->Kstates[istate].psi;

                for (int idx = 0; idx < pbasis; idx++)
                {
                    tarr[idx] += scale * std::norm(psi[idx]);
                    if(ct.noncoll)
                    {
                        psiud = 2.0 * psi[idx] * std::conj(psi[idx + pbasis]);
                        tarr[idx + 1 * pbasis] += scale * std::real(psiud);
                        tarr[idx + 2 * pbasis] += scale * std::imag(psiud);
                        tarr[idx + 3 * pbasis] += scale * std::norm(psi[idx + pbasis]);
                    }
                }                   /* end for */

            }                       /*end for istate */
#pragma omp critical
            for(int idx = 0; idx < pbasis * factor; idx++) work[idx] += tarr[idx];
#pragma omp critical
            tcharge += tcharge1;
            delete [] tarr;
        }
    }                           /*end for kpt */

    MPI_Allreduce(MPI_IN_PLACE, (double *)work, pbasis * factor, MPI_DOUBLE, MPI_SUM, pct.kpsub_comm);
    if(ct.noncoll)
    {
        double rho_up, rho_down;
        for(int idx = 0; idx < pbasis; idx++)
        {
            rho_up = work[idx];
            rho_down = work[idx+3 * pbasis];
            work[idx] = rho_up + rho_down;
            work[idx+3*pbasis] = rho_up - rho_down;
        }
    }

    int FP0_BASIS = Rmg_G->get_P0_BASIS(Rmg_G->default_FG_RATIO);
    /* Interpolate onto fine grid, result will be stored in rho*/
    for(int is = 0; is < factor; is++)
    {
        switch (ct.interp_flag)
        {
            case CUBIC_POLYNOMIAL_INTERPOLATION:
                pack_rho_ctof (&work[is*pbasis], &rho[is*FP0_BASIS]);
                break;
            case PROLONG_INTERPOLATION:
                mg_prolong_MAX10 (&rho[is*FP0_BASIS], &work[is*pbasis], get_FPX0_GRID(), get_FPY0_GRID(), get_FPZ0_GRID(), get_PX0_GRID(), get_PY0_GRID(), get_PZ0_GRID(), get_FG_RATIO(), 4);
                break;
            case FFT_INTERPOLATION:
                FftInterpolation (*Kpts[0]->G, &work[is*pbasis], &rho[is*FP0_BASIS], Rmg_G->default_FG_RATIO, ct.sqrt_interpolation);
                break;
            default:

                //Dprintf ("charge interpolation is set to %d", ct.interp_flag);
                rmg_error_handler (__FILE__, __LINE__, "ct.interp_flag is set to an invalid value.");


        }
    }


    if(ct.is_use_symmetry)
    {
        if(Rmg_Symm) Rmg_Symm->symmetrize_grid_object(rho);
        if(ct.noncoll && Rmg_Symm)
            Rmg_Symm->symmetrize_grid_vector(&rho[FP0_BASIS]);
    }

    delete [] work;
}

#if HIP_ENABLED

#include "Gpufuncs.h"

template void GetNewRhoGpu<double>(Kpoint<double> **, double *);
template void GetNewRhoGpu<std::complex<double> >(Kpoint<std::complex<double>> **, double *);
template void GetNewRhoGpuOne<double>(State<double> *, Prolong *, double);
template void GetNewRhoGpuOne<std::complex<double>>(State<std::complex<double>> *, Prolong *, double);



void init_gpu_prolong(int dimx, int dimy, int dimz, Prolong &P)
{
    int factor = 1;
    if(!ct.is_gamma) factor = 2;
    int order = MAX_PROLONG_ORDER;
    size_t rbufsize = 8*dimx*dimy*dimz*sizeof(double);
    size_t bufsize = (dimx + order)*(dimy + order)*(dimz + order)*sizeof(double)*factor;
    int max_threads = ct.MG_THREADS_PER_NODE;

    // Check if just clearing the accumulators
    if(P.rbufs.size() > 0)
    {
        for(int i=0;i < max_threads;i++)
        {
            GpuFill(P.rbufs[i], 8*dimx*dimy*dimz, 0.0);
        }
        return;
    }

    P.abufs.resize(max_threads);
    P.rbufs.resize(max_threads);
    P.hbufs.resize(max_threads);
    for(int i=0;i < max_threads;i++)
    {
        hipMalloc((void **)&P.abufs[i], bufsize);
        hipMallocHost((void **)&P.hbufs[i], rbufsize);
        hipMalloc((void **)&P.rbufs[i], rbufsize);
    }
    for(int i=0;i < max_threads;i++)
    {
        GpuFill(P.rbufs[i], 8*dimx*dimy*dimz, 0.0);
    }
    DeviceSynchronize();
}

// Generates the new density by interpolating each orbital to the fine grid and then squaring
// and summing them. Uses GPUs to accelerate the process
template <typename OrbitalType> void GetNewRhoGpu(Kpoint<OrbitalType> **Kpts, double *rho)
{

    if(ct.verbose) {
        printf("PE: %d  start GetnewRhoGpu \n", pct.gridpe);
        fflush(NULL);
    }

    if(Verify ("freeze_occupied", true, Kpts[0]->ControlMap)) return;
    BaseThread *T = BaseThread::getBaseThread(0);
    spinobj<double> &rhop = *(Kpts[0]->rho);

    int half_dimx = rhop.dimx / 2;
    int half_dimy = rhop.dimy / 2;
    int half_dimz = rhop.dimz / 2;

    int nstates = Kpts[0]->nstates;
    Prolong P(2, ct.prolong_order, ct.cmix, *Rmg_T,  Rmg_L, *Rmg_G);
    init_gpu_prolong(half_dimx, half_dimy, half_dimz, P);

    int active_threads = ct.MG_THREADS_PER_NODE;
    if(ct.mpi_queue_mode && (active_threads > 1)) active_threads--;

    for (int kpt = 0; kpt < ct.num_kpts_pe; kpt++)
    {
        /* Loop over states and accumulate charge */
        for(int st1=0;st1 < nstates;st1+=active_threads)
        {

            SCF_THREAD_CONTROL thread_control;

            for(int ist = 0;ist < active_threads;ist++)
            {
                thread_control.job = GPU_GET_RHO;
                if(st1+ist >= nstates){
                    thread_control.p1 = NULL;
                    thread_control.p2 = (void *)&P;
                    thread_control.fd_diag = 0.0;
                    thread_control.basetag = st1 + ist;
                }
                else
                {
                    double scale = Kpts[kpt]->Kstates[st1+ist].occupation[0] * Kpts[kpt]->kp.kweight;
                    thread_control.p1 = (void *)&Kpts[kpt]->Kstates[st1+ist];
                    thread_control.p2 = (void *)&P;
                    thread_control.fd_diag = scale;
                    thread_control.basetag = st1 + ist;
                }
                QueueThreadTask(ist, thread_control);
            }
            // Thread tasks are set up so wake them
            if(!ct.mpi_queue_mode) T->run_thread_tasks(active_threads);

        }
        if(ct.mpi_queue_mode) T->run_thread_tasks(active_threads, Rmg_Q);

        MPI_Barrier(pct.grid_comm);
    }                           /*end for kpt */

    // Now we have to reduce rbufs on the GPU
    int ione = 1;
    const double rone = 1.0;
    DeviceSynchronize();
    for(size_t i=1;i < P.rbufs.size();i++)
    {
        gpublasDaxpy (ct.gpublas_handle, rhop.pbasis, &rone, P.rbufs[i], ione, P.rbufs[0], ione);
    }
    hipMemcpy(P.hbufs[0], P.rbufs[0], rhop.pbasis*sizeof(double), hipMemcpyDeviceToHost);
    double *hptr = (double *)P.hbufs[0];

    // Sum over kpoints and spin
    MPI_Allreduce(MPI_IN_PLACE, hptr, rhop.pbasis, MPI_DOUBLE, MPI_SUM, pct.kpsub_comm);

    std::copy(hptr, hptr+rhop.pbasis, rho);

    if(ct.verbose) {
        printf("PE: %d  done GetnewRhoGpu \n", pct.gridpe);
        fflush(NULL);
    }
}

template <typename OrbitalType> void GetNewRhoGpuOne(
        State<OrbitalType> *sp, 
        Prolong *P, 
        double scale)
{
    RmgTimer RT("2-Prolong gpu");

    BaseThread *T = BaseThread::getBaseThread(0);
    T->thread_barrier_wait(false);
    if(scale < 1.0e-10) return;              // No need to include unoccupied orbitals
    int tid = T->get_thread_tid();
    int ord = P->order;
    gpuStream_t stream = getGpuStream();

    int half_dimx = Rmg_G->get_PX0_GRID(1);
    int half_dimy = Rmg_G->get_PY0_GRID(1);
    int half_dimz = Rmg_G->get_PZ0_GRID(1);
    size_t hbasis = half_dimx * half_dimy * half_dimz;
    size_t sg_hbasis = (half_dimx + ord) * (half_dimy + ord) * (half_dimz + ord);

    if constexpr (std::is_same_v<OrbitalType, double>)
    {
        std::vector<float> sg_half(sg_hbasis), thalf(hbasis);
        for(size_t idx =0;idx < hbasis;idx++) thalf[idx] = (float)sp->psi[idx];
        Rmg_T->trade_imagesx (thalf.data(), sg_half.data(), half_dimx, half_dimy, half_dimz, ord/2, FULL_TRADE);
        float *hptr = (float *)P->hbufs[tid];
        float *gptr = (float *)P->abufs[tid];
        std::copy(sg_half.data(), sg_half.data()+sg_hbasis, hptr);
        hipMemcpyAsync(gptr, hptr, sg_hbasis*sizeof(T), hipMemcpyHostToDevice, stream);

        if(ord == 6)
            P->prolong_ortho_gpu<float, 6>(P->rbufs[tid], gptr, half_dimx, half_dimy, half_dimz, scale);
        if(ord == 8)
            P->prolong_ortho_gpu<float, 8>(P->rbufs[tid], gptr, half_dimx, half_dimy, half_dimz, scale);
        if(ord == 10)
            P->prolong_ortho_gpu<float, 10>(P->rbufs[tid], gptr, half_dimx, half_dimy, half_dimz, scale);
        if(ord == 12)
            P->prolong_ortho_gpu<float, 12>(P->rbufs[tid], gptr, half_dimx, half_dimy, half_dimz, scale);
    }
    if constexpr (std::is_same_v<OrbitalType, std::complex<double>>)
    {
        std::vector<std::complex<float>> sg_half(sg_hbasis), thalf(hbasis);
        for(int idx =0;idx < hbasis;idx++) 
            thalf[idx] = std::complex<float>(std::real(sp->psi[idx]), std::imag(sp->psi[idx]));
        Rmg_T->trade_imagesx (thalf.data(), sg_half.data(), half_dimx, half_dimy, half_dimz, ord/2, FULL_TRADE);
        std::complex<float> *hptr = (std::complex<float> *)P->hbufs[tid];
        std::complex<float> *gptr = (std::complex<float> *)P->abufs[tid];
        std::copy(sg_half.data(), sg_half.data()+sg_hbasis, hptr);
        hipMemcpyAsync(gptr, hptr, sg_hbasis*sizeof(T), hipMemcpyHostToDevice, stream);
        if(ord == 6)
            P->prolong_ortho_gpu<std::complex<float>, 6>(P->rbufs[tid], gptr, half_dimx, half_dimy, half_dimz, scale);
        if(ord == 8)
            P->prolong_ortho_gpu<std::complex<float>, 8>(P->rbufs[tid], gptr, half_dimx, half_dimy, half_dimz, scale);
        if(ord == 10)
            P->prolong_ortho_gpu<std::complex<float>, 10>(P->rbufs[tid], gptr, half_dimx, half_dimy, half_dimz, scale);
        if(ord == 12)
            P->prolong_ortho_gpu<std::complex<float>, 12>(P->rbufs[tid], gptr, half_dimx, half_dimy, half_dimz, scale);
    }
    hipStreamSynchronize(stream);
}

#endif
