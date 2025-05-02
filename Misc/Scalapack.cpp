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

#include <vector>
#include <algorithm>
#include <math.h>
#include <complex>
#include <iostream>
#include <fstream>
#include <sstream>


#include "RmgException.h"
#include "GlobalSums.h"
#include "Scalapack.h"
#include "blacs.h"
#include "transition.h"
#include "rmg_error.h"

#if USE_ELPA
#include <elpa/elpa.h>
static int once;
#endif


Scalapack::Scalapack(int ngroups, int thisimg, int images_per_node, int N, int NB, int last, MPI_Comm rootcomm)
{
    this->ngroups = ngroups;
    this->N = N;
    this->root_comm = rootcomm;

    MPI_Comm_size(rootcomm, &this->npes);
    MPI_Comm_rank(rootcomm, &this->root_rank);
    MPI_Group grp_world, grp_this;

    if(this->npes < images_per_node)
        throw RmgFatalException() << "NPES " <<  this->npes << " < images_per_node " << images_per_node << " in " << __FILE__ << " at line " << __LINE__ << ".\n";

    // Make sure we have enough pes for the number of groups requested. We'll reset group_pes later
    this->group_pes = this->npes / this->ngroups / images_per_node;
    if(this->group_pes < 1) 
        throw RmgFatalException() << "Too many Scalapack groups requested in " << __FILE__ << " at line " << __LINE__ << ".\n";



    // Get 2-d grid dimensions for this group
    int sqrtnpe = (int) (sqrt (this->group_pes)) + 1;
    for (int i = 1; i <= sqrtnpe; i++) {

        if (this->group_pes % i == 0) this->group_rows = i;

    }
    this->group_cols = this->group_pes / this->group_rows;

    if(this->group_cols * this->group_rows > this->group_pes)
        throw RmgFatalException() << "Problem with processor distribution in " << __FILE__ << " at line " << __LINE__ << ".\n";

    this->NB = NB;
    int num_blocks = N / NB;
    if(N % NB) num_blocks++;

    while (num_blocks < this->group_cols )
    {
        this->NB /=2;
        num_blocks = (N + this->NB -1) / this->NB;
    }

    if(this->NB < 2)
    {
        rmg_printf("WARNING:  scalapack npcol %d is too large for matrix size of %d \n", this->group_cols, this->N);
    }
    /*Number of processor in any given direction cannot be more than number of blocks*/
//    if(num_blocks < this->group_rows) this->group_rows = num_blocks;
//    if(num_blocks < this->group_cols) this->group_cols = num_blocks;
    // Reset this->group_pes and this->group_index 
    this->group_pes = this->group_rows * this->group_cols;
    this->group_index = this->root_rank / this->group_pes;
    //printf("this->group_pes=%d  this->group_cols=%d  this->group_rows=%d  num_blocks=%d\n",this->group_pes,this->group_cols,this->group_rows,num_blocks);

    // Get system context
    int sys_context;
    Cblacs_get (0, 0, &sys_context);


    // Split PEs into two groups. Color=1 is used for scalapack
    // ops and color=2 is not
    int scalapack_pes = this->ngroups * this->group_pes;
    int color = 1;
    if(this->root_rank < scalapack_pes) color = 2;
    MPI_Comm_split(this->root_comm, color, 1, &this->used_comm);
    //std::cout << "COLOR = " << color << "  " << scalapack_pes << std::endl;   
    // For unused comms this->comm = this->root_comm 
    if(color == 1) {
        this->comm = this->root_comm;
    }
    else {
        // Now split color=2 into ngroups comms 
        color = this->group_index + 1;
        //std::cout << "COLOR1 = " << color << "  " << this->group_index << std::endl;   
        MPI_Comm_split(this->used_comm, color, this->root_rank, &this->comm);
        MPI_Comm_rank(this->comm, &this->comm_rank);
    }

    // Allocate space on the assumption that the group size is smaller than npes
    int *pmap, *tgmap;
    pmap = new int[this->npes];
    tgmap = new int[this->npes];

    // Group rank array
    for (int i = 0; i < this->npes;i++) tgmap[i] = i;

    // Get the world rank mapping of this groups processes since blacs uses world group ranking
    MPI_Comm_group (MPI_COMM_WORLD, &grp_world);
    MPI_Comm_group (this->comm, &grp_this);
    MPI_Comm_size (this->comm, &this->scalapack_npes);
    MPI_Group_translate_ranks (grp_this, this->scalapack_npes, tgmap, grp_world, pmap);

    // Now set up the blacs with nprow*npcol
    //std::cout << "scalapack_npes=  " << this->scalapack_npes << std::endl;
    this->context = sys_context;
    Cblacs_gridmap (&this->context, pmap, this->group_rows, this->group_rows, this->group_cols);

    // Broadcast comm
    color = 1;
    if((this->root_rank >= scalapack_pes) || this->root_rank == 0) {
        color = 2;
    }
    MPI_Comm_split(this->root_comm, color, 1, &this->broadcast_comm);

    this->participates = true;
    if(this->group_index >= this->ngroups) this->participates = false;


    if(this->participates) {
        Cblacs_gridinfo (this->context, &this->group_rows, &this->group_cols, &this->my_row, &this->my_col);
        if(this->my_row < 0) this->participates = false;
        //std::cout << "PP " << this->participates <<  " " << this->group_index << "  " << this->context << std::endl;

        // Set up descriptors for a local matrix (one block on (0,0)
        int izero = 0, info = 0;
        int lld = std::max( numroc( &this->N, &this->N, &this->my_row, &izero, &this->group_rows ), 1 );
        this->local_desca = new int[this->npes*DLEN]();
        descinit( this->local_desca, &this->N, &this->N, &this->N, &this->N, &izero, &izero, &this->context, &lld, &info );


        // Get dimensions of the local part of the distributed matrix
        this->m_dist = numroc( &this->N, &this->NB, &this->my_row, &izero, &this->group_rows );
        this->n_dist = numroc( &this->N, &this->NB, &this->my_col, &izero, &this->group_cols );

        // descriptors for the distributed matrix
        int lld_distr = std::max( this->m_dist, 1 );
        this->dist_desca = new int[this->npes*DLEN]();
        descinit( this->dist_desca, &this->N, &this->N, &this->NB, &this->NB, &izero, &izero, &this->context, &lld_distr, &info );

        //printf("dist_desca = %d  %d  %d  %d  %d  %d  %d  %d  %d  root_rank=%d\n",
        //      this->dist_desca[0], this->dist_desca[1], this->dist_desca[2],
        //      this->dist_desca[3], this->dist_desca[4], this->dist_desca[5],
        //      this->dist_desca[6], this->dist_desca[7], this->dist_desca[8], this->root_rank);

    }

    delete [] tgmap;
    delete [] pmap;

    // Do we need to create any groups under this level
  //  if(!last) {
  //      this->ngroups_next = 12;
  //      this->next = new Scalapack(this->ngroups_next, thisimg, images_per_node, N, NB, true, this->root_comm);
  //
  //   }

#if USE_ELPA
    if(ct.subdiag_driver != SUBDIAG_ELPA) return;
    int error;
    int api_version = 20221109;
    if(!once)
    {
        elpa_init(api_version);
        once = 1;
    }
    this->elpa_handle = (elpa_t *)elpa_allocate(&error);
    if(error != ELPA_OK)
    {
        throw RmgFatalException() << "Error initializeing elpa in "  << __FILE__ << " at line " << __LINE__ << ".\n";

    }

    elpa_set((elpa_t)this->elpa_handle, "na", this->N, &error);
    elpa_set((elpa_t)this->elpa_handle, "nev", this->N, &error);
    elpa_set((elpa_t)this->elpa_handle, "local_nrows", this->m_dist, &error);
    elpa_set((elpa_t)this->elpa_handle, "local_ncols", this->n_dist, &error);
    elpa_set((elpa_t)this->elpa_handle, "nblk", this->NB, &error);
    elpa_set((elpa_t)this->elpa_handle, "mpi_comm_parent", MPI_Comm_c2f(this->comm), &error);
    elpa_set((elpa_t)this->elpa_handle, "process_row", this->my_row, &error);
    elpa_set((elpa_t)this->elpa_handle, "process_col", this->my_col, &error);
    elpa_set((elpa_t)this->elpa_handle, "num_process_rows", this->group_rows, &error);
    elpa_set((elpa_t)this->elpa_handle, "num_process_cols", this->group_cols, &error);
    elpa_set((elpa_t)this->elpa_handle, "blacs_context", this->context, &error);
    elpa_setup((elpa_t)this->elpa_handle);

#endif

}

// Used to gather the distributed eigenvectors into a local array
// using conversion to float.
void Scalapack::GatherEigvectors(double *A, double *distA)
{
    if(!this->participates) return;
    if(N >= 16000)
    {
        CopyDistArrayToSquareMatrix(A, distA, N, dist_desca);
        ScalapackBlockAllreduce((double *)A, (size_t)N * (size_t)N);
        return;
    }

    // Call pdgeadd_ to gather matrix (i.e. copy distA into A)
    int ione = 1;
    double rone = 1.0, rzero = 0.0;
    pdgeadd( "N", &this->N, &this->N, &rone, distA, &ione, &ione, this->dist_desca, &rzero, A, &ione, &ione, this->local_desca );
}

void Scalapack::GatherEigvectors(std::complex<double> *A, std::complex<double> *distA)
{
    if(!this->participates) return;
    if(N >= 16000 || 1)
    {
        CopyDistArrayToSquareMatrix(A, distA, N, dist_desca);
        ScalapackBlockAllreduce((double *)A, (size_t)N * (size_t)(2*N));
        return;
    }

    // Call pzgeadd_ to gather matrix (i.e. copy distA into A)
    int ione = 1;
    double rone[2], rzero[2];
    rone[0] = 1.0;rone[1] = 0.0;
    rzero[0] = 0.0;rzero[1] = 0.0;
    
    pzgeadd( "N", &this->N, &this->N, rone, (double *)distA, &ione, &ione, this->dist_desca, rzero, (double *)A, &ione, &ione, this->local_desca );
}

MPI_Comm Scalapack::GetRootComm(void)
{
    return this->root_comm;
}

int Scalapack::GetNumGroups(void)
{
    return this->ngroups;
}

int Scalapack::GetNumGroupsNext(void)
{
    return this->ngroups_next;
}

// Returns dist_length or < 0 if an error occurred
int Scalapack::ComputeDesca(int m, int n, int *desca)
{
    if(!this->participates) return 0;
    int izero = 0, info = 0;
    int m_dist = numroc( &m, &this->NB, &this->my_row, &izero, &this->group_rows );
    int n_dist = numroc( &n, &this->NB, &this->my_col, &izero, &this->group_cols );
    int lld_distr = std::max( m_dist, 1 );
    descinit( desca, &m, &n, &this->NB, &this->NB, &izero, &izero, &this->context, &lld_distr, &info );
    if(info < 0) return info;
    return m_dist * n_dist;
}

int Scalapack::ComputeMdim(int m)
{
    int izero = 0;
    return numroc( &m, &this->NB, &this->my_row, &izero, &this->group_rows );
}

int Scalapack::ComputeNdim(int n)
{
    int izero = 0;
    return numroc( &n, &this->NB, &this->my_col, &izero, &this->group_cols );
}

int Scalapack::GetRootRank(void)
{
    return this->root_rank;
}

int Scalapack::GetCommRank(void)
{
    return this->comm_rank;
}

int Scalapack::GetContext(void)
{
    return this->context;
}

int Scalapack::GetDistMdim(void)
{
    return this->m_dist;
}

int Scalapack::GetDistNdim(void)
{
    return this->n_dist;
}

int *Scalapack::GetDistDesca(void)
{
    return this->dist_desca;
}

bool Scalapack::Participates(void)
{
    return this->participates;
}

MPI_Comm Scalapack::GetComm(void)
{
    return this->comm;
}

int Scalapack::GetN(void)
{
    return this->N;
}

int Scalapack::GetNB(void)
{
    return this->NB;
}

int Scalapack::GetRows(void)
{
    return this->group_rows;
}

int Scalapack::GetCols(void)
{
    return this->group_cols;
}

int Scalapack::GetRow(void)
{
    return this->my_row;
}

int Scalapack::GetCol(void)
{
    return this->my_col;
}

int Scalapack::GetGroupIndex(void)
{
    return this->group_index;
}

int Scalapack::GetRootNpes(void)
{
    return this->npes;
}

int Scalapack::GetScalapackNpes(void)
{
    return this->scalapack_npes;
}

Scalapack *Scalapack::GetNextScalapack(void)
{
    return this->next;
}

void Scalapack::FillUpper(double *A, int n)
{
    int blocksize = 1;
#pragma omp parallel for
    for (int i = 0; i < n; i += blocksize) {
        for (int j = i; j < n; j += blocksize) {
            for (int row = i; row < i + blocksize && row < n; row++) {
                for (int col = j; col < j + blocksize && col < n; col++) {
                    A[col*n + row] = A[row*n + col];
                }
            }
        }
    }
}

void Scalapack::FillUpper(std::complex<double> *A, int n)
{
    int blocksize = 1;
#pragma omp parallel for
    for (int i = 0; i < n; i += blocksize) {
        for (int j = i; j < n; j += blocksize) {
            for (int row = i; row < i + blocksize && row < n; row++) {
                for (int col = j; col < j + blocksize && col < n; col++) {
                    A[col*n + row] = std::conj(A[row*n + col]);
                }
            }
        }
    }
}


// Returns ipiv size required by PDGESV
int Scalapack::GetIpivSize(void)
{
    if(!this->participates) return 0;
    return numroc(&this->dist_desca[2], &this->dist_desca[4], &this->my_row, &this->dist_desca[6],
                                &this->group_rows) + this->dist_desca[4];

}

void Scalapack::DistributeMatrix(double *A, double *A_dist)
{
    if(!this->participates) return;

    // Call pdgeadd_ to distribute matrix (i.e. copy A into A_dist)
    int ione = 1;
    double rone = 1.0, rzero = 0.0;
    pdgeadd( "N", &this->N, &this->N, &rone, A, &ione, &ione, this->local_desca, &rzero, A_dist, &ione, &ione, this->dist_desca );
}


void Scalapack::GatherMatrix(double *A, double *A_dist)
{

    if(!this->participates) return;

    // Call pdgeadd_ to gather matrix (i.e. copy A_dist into A)
    int ione = 1;
    double rone = 1.0, rzero = 0.0;
    pdgeadd( "N", &this->N, &this->N, &rone, A_dist, &ione, &ione, this->dist_desca, &rzero, A, &ione, &ione, this->local_desca );

}


void Scalapack::DistributeMatrix(std::complex<double> *A, std::complex<double> *A_dist)
{
    if(!this->participates) return;

    // Call pdgeadd_ to distribute matrix (i.e. copy A into A_dist)
    int ione = 1;
    double rone = 1.0, rzero = 0.0;
    pzgeadd( "N", &this->N, &this->N, &rone, (double *)A, &ione, &ione, this->local_desca, &rzero, (double *)A_dist, &ione, &ione, this->dist_desca );
}


void Scalapack::GatherMatrix(std::complex<double> *A, std::complex<double> *A_dist)
{

    if(!this->participates) return;

    // Call pdgeadd_ to gather matrix (i.e. copy A_dist into A)
    int ione = 1;
    double rone = 1.0, rzero = 0.0;
    pzgeadd( "N", &this->N, &this->N, &rone, (double *)A_dist, &ione, &ione, this->dist_desca, &rzero, (double *)A, &ione, &ione, this->local_desca );

}

void Scalapack::generalized_eigenvectors(double *a, double *b, double *ev, double *q)
{
#if USE_ELPA
    if(ct.subdiag_driver == SUBDIAG_ELPA)
    {
        this->generalized_eigenvectors_elpa(a, b, ev, q);
        return;
    }
#endif    

    this->generalized_eigenvectors_scalapack(a, b, ev, q);
}

void Scalapack::generalized_eigenvectors(std::complex<double> *a, std::complex<double> *b,
             double *ev, std::complex<double> *q)
{
#if USE_ELPA
    if(ct.subdiag_driver == SUBDIAG_ELPA)
    {
        this->generalized_eigenvectors_elpa(a, b, ev, q);
        return;
    }
#endif    

    this->generalized_eigenvectors_scalapack(a, b, ev, q);
}

void Scalapack::generalized_eigenvectors_scalapack(double *a, double *b, double *ev, double *q)
{
    /****************** Find Matrix of Eigenvectors *****************************/
    /* Using lwork=-1, PDSYGVX should return minimum required size for the work array */
    char *uplo = "l", *jobz = "v";
    int info, ione = 1, ibtype = 1;
    double scale=1.0, rone = 1.0;
    int N = this->N;
    int *desca = this->GetDistDesca();

    pdpotrf(uplo, &N, b,  &ione, &ione, desca,  &info);
    if (info)
    {
       rmg_printf ("\n pdpotrf failed, info is %d", info);
       rmg_error_handler (__FILE__, __LINE__, "pdpotrf failed");
    }

    // Get workspace required
    int NN = std::max( N, this->NB);
    int izero = 0;
    int nprow = this->GetRows();
    int npcol = this->GetCols();
    int NQ = numroc( &NN, &this->NB, &izero, &izero, &npcol );
    int NP = numroc( &NN, &this->NB, &izero, &izero, &nprow );
    int lrwork = 1 + 9*N + 3*NP*NQ;
    int liwork = 7*N + 8* npcol + 2;
    int lwork = 2*NP*this->NB + NQ*this->NB + this->NB*this->NB;
    double *work2 = new double[lwork];
    
    pdsyngst(&ibtype, uplo, &N, q, &ione, &ione, desca,
            b, &ione, &ione, desca, &scale, work2, &lwork, &info);

    if (info)
    {
       rmg_printf ("\n pdsyngst failed, info is %d", info);
       rmg_error_handler (__FILE__, __LINE__, "pdsyngst failed");
    }


    double *nwork = new double[lrwork];
    int *iwork = new int[liwork];

    // and now solve it 
    pdsyevd(jobz, uplo, &N, q, &ione, &ione, desca,
            ev, a, &ione, &ione, desca, nwork, &lrwork, iwork, &liwork, &info);

    if (info)
    {
       rmg_printf ("\n pdsyevd failed, info is %d", info);
       rmg_error_handler (__FILE__, __LINE__, "psyevd failed");
    }

    pdtrsm("Left", uplo, "T", "N", &N, &N, &rone, b, &ione, &ione, desca,
            a, &ione, &ione, desca);

    if (info)
    {
       rmg_printf ("\n pdtrms failed, info is %d", info);
       rmg_error_handler (__FILE__, __LINE__, "pdtrms failed");
    }

    delete [] iwork;
    delete [] nwork;
    delete [] work2;
}

void Scalapack::generalized_eigenvectors_scalapack(std::complex<double> *a, std::complex<double> *b,
             double *ev, std::complex<double> *q)
{
    int ibtype = 1, ione=1, info;
    char *uplo = "l", *jobz = "v";
    double scale=1.0, rone[2] = {1.0, 0.0};
    int N = this->N;
    int *desca = this->GetDistDesca();

    pzpotrf(uplo, &N, (double *)b,  &ione, &ione, desca,  &info);

    if (info)
    {
       rmg_printf ("\n pzpotrf failed, info is %d", info);
       rmg_error_handler (__FILE__, __LINE__, "pzpotrf failed");
    }

    pzhegst(&ibtype, uplo, &N, (double *)q, &ione, &ione, desca,
                (double *)b, &ione, &ione, desca, &scale, &info);

    if (info)
    {
       rmg_printf ("\n pzhegst failed, info is %d", info);
       rmg_error_handler (__FILE__, __LINE__, "pzhegst failed");
    }


    // Get workspace required
     int NN = std::max( N, this->NB);

    int izero = 0;
    int nprow = this->GetRows();
    int npcol = this->GetCols();
    int NQ = numroc( &NN, &this->NB, &izero, &izero, &npcol );
    int NP = numroc( &NN, &this->NB, &izero, &izero, &nprow );
    int lwork = N + ( NP+NQ+this->NB )*this->NB;
    int lrwork = 1 + 9*N + 3*NP*NQ;
    int liwork = 7*N + 8* npcol + 2;

    double *rwork = new double[lrwork];
    double *nwork = new double[lwork*2];
    int *iwork = new int[liwork];


    // and now solve it
    pzheevd(jobz, uplo, &N, (double *)q, &ione, &ione, desca,
            ev, (double *)a, &ione, &ione, desca, nwork, &lwork, (double *)rwork, &lrwork, iwork, &liwork, &info);

    if (info)
    {
        rmg_printf ("\n pzheevd failed, info is %d", info);
        rmg_error_handler (__FILE__, __LINE__, "pzheevd failed");
    }

    pztrsm("Left", uplo, "C", "N", &N, &N, rone, (double *)b, &ione, &ione, desca,
            (double *)a, &ione, &ione, desca);

    if (info)
    {
        rmg_printf ("\n pztrsm failed, info is %d", info);
        rmg_error_handler (__FILE__, __LINE__, "pztrms failed");
    }

    delete [] iwork;
    delete [] nwork;
    delete [] rwork;

}

#if USE_ELPA
void Scalapack::generalized_eigenvectors_elpa(double *a, double *b, double *ev, double *q)
{
    int error;
    //    elpa_set((elpa_t)this->handle, "solver", ELPA_SOLVER_2STAGE, error);
    //    elpa_set((elpa_t)this->handle, "real_kernel", ELPA_2STAGE_REAL_AMD_GPU, error);
    elpa_generalized_eigenvectors((elpa_t)this->elpa_handle, a, b, ev, q, false, &error);
    size_t dist_length = this->GetDistMdim() * this->GetDistNdim();
    for(size_t i=0;i < dist_length;i++) a[i] = q[i];
}


void Scalapack::generalized_eigenvectors_elpa(std::complex<double> *a, std::complex<double> *b, double *ev, std::complex<double> *q)
{
    int error;
    elpa_generalized_eigenvectors((elpa_t)this->elpa_handle, a, b, ev, q, false, &error);
    size_t dist_length = this->GetDistMdim() * this->GetDistNdim();
    for(size_t i=0;i < dist_length;i++) a[i] = q[i];
}
#endif

void Scalapack::Pgemm (char *transa, char *transb, int *M, int *N, int *K, double *alpha, 
        double *A, int *IA, int *JA, int *desca, 
        double *B, int *IB, int *JB, int *descb, 
        double *beta, double *C, int *IC, int *JC, int *descc)
{
    if(!this->participates) return;
    pdgemm(transa, transb, M, N, K, alpha, A, IA, JA, desca, B, IB, JB, descb, beta, C, IC, JC, descc);
}

void Scalapack::Pgemm (char *transa, char *transb, int *M, int *N, int *K, std::complex<double> *alpha, 
        std::complex<double> *A, int *IA, int *JA, int*desca,
        std::complex<double> *B, int *IB, int *JB, int *descb,
        std::complex<double> *beta, std::complex<double> *C, int *IC, int *JC, int *descc)
{
    if(!this->participates) return;
    pzgemm(transa, transb, M, N, K, alpha, A, IA, JA, desca, B, IB, JB, descb, beta, C, IC, JC, descc);
}

void Scalapack::Pgesv (int *N, int *NRHS, double *A, int *IA, int *JA, int *desca, int *ipiv, double *B, int *IB,
        int *JB, int *descb, int *info)
{
    if(!this->participates) return;
    pdgesv(N, NRHS, A, IA, JA, desca, ipiv, B, IB, JB, descb, info);
}

void Scalapack::Pgesv (int *N, int *NRHS, std::complex<double> *A, int *IA, int *JA, int *desca, int *ipiv, std::complex<double> *B, int *IB,
        int *JB, int *descb, int *info)
{
    if(!this->participates) return;
    pzgesv(N, NRHS, A, IA, JA, desca, ipiv, B, IB, JB, descb, info);
}

// Reduces within the group only
void Scalapack::Allreduce(void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op) {
    if(!this->participates) return;
    MPI_Allreduce(sendbuf, recvbuf, count, datatype, op, this->comm);
}

// Block inplace double reduction within the group only
void Scalapack::ScalapackBlockAllreduce(double *buf, size_t count)
{
    BlockAllreduce(buf, count, this->comm);
}

// Block inplace float reduction within the group only
void Scalapack::ScalapackBlockAllreduce(float *buf, size_t count)
{
    BlockAllreduce(buf, count, this->comm);
}

// Broadcast to everyone in the root
void Scalapack::BcastRoot(void *buffer, int count, MPI_Datatype datatype)
{
    //if((this->group_index > this->ngroups) || this->root_rank == 0)
    //    MPI_Bcast(buffer, count, datatype, 0, this->broadcast_comm);
    MPI_Bcast(buffer, count, datatype, 0, this->root_comm);
}

// Broadcast to everyone in the comm
void Scalapack::BcastComm(void *buffer, int count, MPI_Datatype datatype)
{
    //if((this->group_index > this->ngroups) || this->root_rank == 0)
    MPI_Bcast(buffer, count, datatype, 0, this->comm);
}

void Scalapack::CopySquareMatrixToDistArray(double *A, double *A_dist, int n, int *desca)
{
    if(this->participates) {
        matscatter(A, A_dist, n, desca, true);
    }
}

void Scalapack::CopySquareMatrixToDistArray(std::complex<double> *A, std::complex<double> *A_dist, int n, int *desca)
{
    if(this->participates) {
        matscatter((double *)A, (double *)A_dist, n, desca, false);
    }
}

void Scalapack::CopyDistArrayToSquareMatrix(double *A, double *A_dist, int n, int *desca)
{
    if(this->participates) {
        matgather(A, A_dist, n, desca, true);
    }
}

void Scalapack::CopyDistArrayToSquareMatrix(std::complex<double> *A, std::complex<double> *A_dist, int n, int *desca)
{
    if(this->participates) {
        matgather((double *)A, (double *)A_dist, n, desca, false);
    }
}


// Currently only for use on square matrices with desca set up
// during the constructor. Copies global matrix which is duplicated
// on all nodes into distributed matrix.
void Scalapack::matscatter (double *globmat, double *dismat, int size, int *desca, bool isreal)
{
    if(!this->participates) return;
    int i, j, ii, jj, iii, jjj, li, lj, maxrow, maxcol;
    int iistart, jjstart, limb, ljnb, izero = 0;
    int mycol, myrow, nprow, npcol;
    int mb = desca[4], nb = desca[5], mxllda = desca[8];
    int mxlloc = numroc(&size, &nb, &this->my_col, &izero, &this->group_cols);

    mycol = this->my_col;
    myrow = this->my_row;
    nprow = this->group_rows;
    npcol = this->group_cols;


    maxrow = (size / (nprow * mb)) + 1;
    maxcol = (size / (npcol * mb)) + 1;


    for (li = 0; li < maxrow; li++)
    {

        iistart = (li * nprow + myrow) * mb;
        limb = li * mb;

        for (lj = 0; lj < maxcol; lj++)
        {

            jjstart = (lj * npcol + mycol) * nb;
            ljnb = lj * nb;

            for (i = 0; i < mb; i++)
            {

                ii = iistart + i;
                iii = i + limb;

                if (iii < mxllda && ii < size)
                {

                    for (j = 0; j < nb; j++)
                    {

                        jj = jjstart + j;
                        jjj = j + ljnb;

                        if (jjj < mxlloc && jj < size)
                        {
                            if(isreal) {
                                dismat[iii + jjj * mxllda] = globmat[ii + jj * size];
                            }
                            else {
                                dismat[2 * (iii + jjj * mxllda)] = globmat[2 * (ii + jj * size)];
                                dismat[2 * (iii + jjj * mxllda) + 1] =
                                    globmat[2 * (ii + jj * size) + 1];
                            }
                        }
                    }
                }

            }
        }
    }

}


void Scalapack::matgather (double *globmat, double *dismat, int size, int *desca, bool isreal)
{
    if(!this->participates) return;
    int i, j, ii, jj, iii, jjj, li, lj, maxcol, maxrow, jjstart, iistart;
    int limb, ljnb, izero = 0;
    int mycol, myrow, nprow, npcol;
    int mb = desca[4], nb = desca[5], mxllda = desca[8];
    int mxlloc = numroc(&size, &nb, &this->my_col, &izero, &this->group_cols);


    mycol = this->my_col;
    myrow = this->my_row;
    nprow = this->group_rows;
    npcol = this->group_cols;


    if(isreal) {
        for (i = 0; i < size * size; i++)
            globmat[i] = 0.;
    }
    else {
        for (i = 0; i < 2 * size * size; i++)
            globmat[i] = 0.;
    }


    maxrow = (size / (nprow * mb)) + 1;
    maxcol = (size / (npcol * mb)) + 1;

    /* loop on the blocks of size mb*nb */
    for (li = 0; li < maxrow; li++)
    {

        iistart = (li * nprow + myrow) * mb;
        limb = li * mb;

        for (lj = 0; lj < maxcol; lj++)
        {

            jjstart = (lj * npcol + mycol) * nb;
            ljnb = lj * nb;

            /* loop in the block */
            for (i = 0; i < mb; i++)
            {

                ii = iistart + i;
                iii = limb + i;

                if (iii < mxllda && ii < size)
                {

                    for (j = 0; j < nb; j++)
                    {

                        jj = jjstart + j;
                        jjj = j + ljnb;

                        //if (jjj < mxllda && jj < size)
                        if (jjj < mxlloc && jj < size)
                        {
                            if(isreal) {
                                globmat[ii + jj * size] = dismat[iii + jjj * mxllda];
                            }
                            else {
                                globmat[2 * (ii + jj * size)] = dismat[2 * (iii + jjj * mxllda)];
                                globmat[2 * (ii + jj * size) + 1] =
                                    dismat[2 * (iii + jjj * mxllda) + 1];
                            }
                        }
                    }
                }

            }
        }
    }
}

template void Scalapack::matgather_t<double, double>(double *, double *, int, int, int, int *, bool);
template void Scalapack::matgather_t<double, float>(double *, float *, int, int, int, int *, bool);

template<typename T1, typename T2> void Scalapack::matgather_t(T1 *globmat, T2 *dismat, int size,
        int myrow, int mycol, int *desca, bool isreal)
{
    if(!this->participates) return;
    int i, j, ii, jj, iii, jjj, li, lj, maxcol, maxrow, jjstart, iistart;
    int limb, ljnb, izero = 0;
    int nprow, npcol;
    int mb = desca[4], nb = desca[5], mxllda = desca[8];
    //printf("SSSS  %d  %d  %d  %d\n",size,nb,mycol,this->group_cols);fflush(NULL);
    int mxlloc = numroc(&size, &nb, &mycol, &izero, &this->group_cols);

    nprow = this->group_rows;
    npcol = this->group_cols;


    if(isreal) {
        for (i = 0; i < size * size; i++)
            globmat[i] = 0.;
    }
    else {
        for (i = 0; i < 2 * size * size; i++)
            globmat[i] = 0.;
    }


    maxrow = (size / (nprow * mb)) + 1;
    maxcol = (size / (npcol * mb)) + 1;

    /* loop on the blocks of size mb*nb */
    for (li = 0; li < maxrow; li++)
    {

        iistart = (li * nprow + myrow) * mb;
        limb = li * mb;

        for (lj = 0; lj < maxcol; lj++)
        {

            jjstart = (lj * npcol + mycol) * nb;
            ljnb = lj * nb;

            /* loop in the block */
            for (i = 0; i < mb; i++)
            {

                ii = iistart + i;
                iii = limb + i;

                if (iii < mxllda && ii < size)
                {

                    for (j = 0; j < nb; j++)
                    {

                        jj = jjstart + j;
                        jjj = j + ljnb;

                        //if (jjj < mxllda && jj < size)
                        if (jjj < mxlloc && jj < size)
                        {
                            if(isreal) {
                                globmat[ii + jj * size] = (T1)dismat[iii + jjj * mxllda];
                            }
                            else {
                                globmat[2 * (ii + jj * size)] = (T1)dismat[2 * (iii + jjj * mxllda)];
                                globmat[2 * (ii + jj * size) + 1] =
                                    (T1)dismat[2 * (iii + jjj * mxllda) + 1];
                            }
                        }
                    }
                }

            }
        }
    }
}

// Clean up
Scalapack::~Scalapack(void)
{
    Cblacs_gridexit(this->context);
    MPI_Comm_free(&this->comm);
    delete [] this->dist_desca;
    delete [] this->local_desca;
#if USE_ELPA
    if(ct.subdiag_driver != SUBDIAG_ELPA) return;
    int error;
    elpa_deallocate((elpa_t)this->elpa_handle, &error);
#endif

}
