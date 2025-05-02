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
#include <complex>
#include "transition.h"
#include "const.h"
#include "RmgTimer.h"
#include "rmgtypedefs.h"
#include "params.h"
#include "typedefs.h"
#include "common_prototypes.h"
#include "common_prototypes1.h"
#include "rmg_error.h"
#include "State.h"
#include "Kpoint.h"
#include "RmgSumAll.h"
#include <complex>


void betapsi_so(std::complex<double> *sint, std::complex<double> *sint_tem, SPECIES &sp);
template State<double>::State(void);
template State<std::complex<double> >::State(void);

template void State<double>::normalize(double *, int );
template void State<std::complex <double> >::normalize(std::complex <double> *, int );
template void State<double>::set_storage(double *storage);
template void State<std::complex <double> >::set_storage(std::complex <double> *tpsi);
template bool State<double>::is_occupied(void);
template bool State<std::complex <double> >::is_occupied(void);

template <class StateType> State<StateType>::State(void)
{
    this->occupation[0] = 0.0;
    this->occupation[1] = 0.0;

}

template <class StateType> void State<StateType>::set_storage(StateType *storage)
{
    this->psi = storage;
}

template <class StateType> bool State<StateType>::is_occupied(void)
{
    if(!ct.spin_flag) return (this->occupation[0] > 0.0);
    return ((this->occupation[0] > 0.0) || (this->occupation[1] > 0.0));
}

template <class StateType> void State<StateType>::normalize(StateType *tpsi, int istate)
{
    double vel = (double) ((size_t)Rmg_G->get_NX_GRID(1) * (size_t)Rmg_G->get_NY_GRID(1) * (size_t)Rmg_G->get_NZ_GRID(1));
    vel = Rmg_L.get_omega() / vel;

    int num_nonloc_ions = this->Kptr->BetaProjector->get_num_nonloc_ions();
    int *owned_ions_list = this->Kptr->BetaProjector->get_owned_ions_list();
    int num_owned_ions = this->Kptr->BetaProjector->get_num_owned_ions();
    int *nonloc_ions_list = this->Kptr->BetaProjector->get_nonloc_ions_list();




    if(ct.norm_conserving_pp) {

        double sum1 = 0.0, sum2;
        for(int idx = 0;idx < this->Kptr->pbasis * ct.noncoll_factor;idx++) {
            sum1 = sum1 + std::norm(tpsi[idx]);
        }

        sum2 = vel * RmgSumAll<double>(sum1, this->Kptr->grid_comm);
        sum2 = sqrt(1.0 / sum2);

        StateType tscale(sum2);
        for(int idx = 0;idx < this->Kptr->pbasis * ct.noncoll_factor;idx++) {
            tpsi[idx] = tpsi[idx] * tscale;
        }

    }
    else {

        int ion, nh, i, j, inh, sidx_local, nidx, oion;
        double sumbeta, sumpsi, sum, t1;
        double *qqq;
        StateType *sint = new StateType[2 * ct.max_nl];
        std::complex<double> *sint_tem = new std::complex<double>[2*ct.max_nl];
        ION *iptr;


        sumpsi = 0.0;
        sumbeta = 0.0;

        nidx = -1;
        for (ion = 0; ion < num_owned_ions; ion++)
        {

            oion = owned_ions_list[ion];
            
            iptr = &Atoms[oion];
           
            nh = Species[iptr->species].nh;
            
            /* Figure out index of owned ion in nonloc_ions_list array*/
            do {
                
                nidx++;
                if (nidx >= num_nonloc_ions)
                    rmg_error_handler(__FILE__, __LINE__, "Could not find matching entry in nonloc_ions_list");
            
            } while (nonloc_ions_list[nidx] != oion);

            qqq = Atoms[oion].qqq;

            /*nidx adds offset due to current ion*/
            for(int is = 0; is < ct.noncoll_factor; is++)
            {
                for (int i = 0; i < ct.max_nl; i++)
                {
                    sint[i + is * ct.max_nl] = this->Kptr->newsint_local[(ct.noncoll_factor * istate + is)*num_nonloc_ions*ct.max_nl + ion * ct.max_nl + i];
                }               /*end for i */
            }

           // if(ct.spinorbit)
           //     betapsi_so((std::complex<double> *)sint, sint_tem, sp);

            for (i = 0; i < nh; i++)
            {
                inh = i * nh;
                for (j = 0; j < nh; j++)
                {
                    if (qqq[inh + j] != 0.0)
                    {

                        sumbeta += qqq[inh + j] * std::real( sint[i] * std::conj(sint[j]) );
                        if(ct.noncoll)
                            sumbeta += qqq[inh + j] * std::real( sint[i+ct.max_nl] * std::conj(sint[j+ct.max_nl]) );

                    }
                }
            }
        }


        for (int idx = 0; idx < this->Kptr->pbasis * ct.noncoll_factor; idx++)
        {
            sumpsi += std::norm(tpsi[idx]);
        }

        sum = RmgSumAll<double>(vel * sumpsi + sumbeta, this->Kptr->grid_comm);
        sum = 1.0 / sum;

        if (sum < 0.0)
        {
            rmg_printf ("the %dth state is wrong\n", istate);
            rmg_error_handler (__FILE__, __LINE__, "<psi|S|psi> cann't be negative");
        }

        t1 = sqrt (sum);
        for(int idx = 0;idx < this->Kptr->pbasis * ct.noncoll_factor;idx++) {
            tpsi[idx] = tpsi[idx] * t1;
        }

        /* update <beta|psi> - Local version*/

        sidx_local = istate * num_nonloc_ions * ct.max_nl * ct.noncoll_factor;
        for (ion = 0; ion < num_nonloc_ions; ion++)
        {

            StateType *tsint_ptr = &this->Kptr->newsint_local[sidx_local + ion * ct.max_nl];
            for(int jdx = 0;jdx < ct.max_nl;jdx++) {
                tsint_ptr[jdx] = tsint_ptr[jdx] * t1;
                if(ct.noncoll)
                    tsint_ptr[jdx + num_nonloc_ions * ct.max_nl] = tsint_ptr[jdx + num_nonloc_ions * ct.max_nl] * t1;

            }

        }

        delete []sint;
        delete []sint_tem;

    }
}

