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

#include <math.h>
#include <float.h>
#include <complex>
#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <sys/stat.h>

#include "const.h"
#include "rmgtypedefs.h"
#include "typedefs.h"
#include "RmgException.h"
#include "RmgSumAll.h"
#include "transition.h"
#include "RmgParallelFft.h"


// On input performs a dft of x which is an array distributed in real space across all node
// using the plane wave structure defined in pwaves. This is then filtered in g-space with
// the coefficients of all plane waves with a cutoff greater than or less than or equal to
// depending on the type of filtering set to zero.
void FftFilter(double *x,   // IN:OUT  Input array in real space. Distributed across all nodes.
               Pw &pwaves,  // IN:     Plane wave structure that corresponds to the reciprocal space grid for x
               Pw &c_pwaves, // IN: Plane wave structure for the coarse grid.
               int filter_type)  // IN: LOW_PASS or HIGH_PASS, defined in const.h
{

  double g2cut = c_pwaves.gcut;
  size_t global_basis = pwaves.global_basis;
  size_t pbasis = pwaves.pbasis;
  size_t size = pbasis;
  std::complex<double> *crho = new std::complex<double>[size];

  for(size_t i = 0;i < pbasis;i++) crho[i] = std::complex<double>(x[i], 0.0);
  pwaves.FftForward(crho, crho);

  if(filter_type == LOW_PASS) {
      for(size_t ig=0;ig < pbasis;ig++) {
          if(pwaves.gmags[ig] >= g2cut) {
              crho[ig] = std::complex<double>(0.0, 0.0);
          }
      }
  }
  else {
      for(size_t ig=0;ig < pbasis;ig++) {
          if((pwaves.gmags[ig] <= g2cut) || (pwaves.gmags[ig] > pwaves.gmax)) {
              crho[ig] = std::complex<double>(0.0, 0.0);
          }
      }
  }

  pwaves.FftInverse(crho, crho);
  for(size_t i = 0;i < pbasis;i++) x[i] = std::real(crho[i])/(double)global_basis;

  delete [] crho;

}

void FftFilter(double *x,   // IN:OUT  Input array in real space. Distributed across all nodes.
               Pw &pwaves,  // IN:     Plane wave structure that corresponds to the reciprocal space grid for x
               Pw &c_pwaves, // IN: Plane wave structure for the coarse grid.
               double factor)  // IN: LOW_PASS or HIGH_PASS, defined in const.h
{

  double g2cut = factor*c_pwaves.gcut;
  size_t global_basis = pwaves.global_basis;
  size_t pbasis = pwaves.pbasis;
  size_t size = pbasis;
  std::complex<double> *crho = new std::complex<double>[size];

  for(size_t i = 0;i < pbasis;i++) crho[i] = std::complex<double>(x[i], 0.0);
  pwaves.FftForward(crho, crho);

  for(size_t ig=0;ig < pbasis;ig++) {
      if(pwaves.gmags[ig] >= g2cut) {
          crho[ig] = std::complex<double>(0.0, 0.0);
      }
  }

  pwaves.FftInverse(crho, crho);
  for(size_t i = 0;i < pbasis;i++) x[i] = std::real(crho[i])/(double)global_basis;

  delete [] crho;

}

