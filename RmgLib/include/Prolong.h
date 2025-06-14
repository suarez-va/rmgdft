/*
 *
 * Copyright (c) 2019, Emil Briggs
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
*/

// We have to do this wierdness since the clang compiler for gpu code
// has issues on some platforms with the other include files.
#ifdef RMG_Prolong_const_only
#define MAX_PROLONG_RATIO 4
#define MAX_PROLONG_ORDER 12

#else

#define MAX_PROLONG_RATIO 4
#define MAX_PROLONG_ORDER 12

#ifndef RMG_Prolong_H
#define RMG_Prolong_H 1

#include <vector>
#include <stdint.h>
#include "TradeImages.h"
#include "Lattice.h"
#include "BaseGrid.h"
#include "rmg_error.h"



class coef_idx {

public:
    double coeff;
    int ix, iy, iz;
};

class Prolong {

public:
    Prolong(int ratio, int order, double cmix, TradeImages &TI, Lattice &L, BaseGrid &BG);
    ~Prolong(void);

    template<typename T>
    void prolong (T *full, T *half, int dimx, int dimy, int dimz, int half_dimx, int half_dimy, int half_dimz);

    template<typename T>
    void prolong_bcc (T *full, T *half, int dimx, int dimy, int dimz, int half_dimx, int half_dimy, int half_dimz);
    template<typename T>
    void prolong_fcc (T *full, T *half, int dimx, int dimy, int dimz, int half_dimx, int half_dimy, int half_dimz);
    template<typename T>
    void prolong_bcc_other (T *full, T *half, int dimx, int dimy, int dimz, int half_dimx, int half_dimy, int half_dimz);
    template<typename T>
    void prolong_any (T *full, T *half, int dimx, int dimy, int dimz, int half_dimx, int half_dimy, int half_dimz);

    template <typename T, int ord>
    void prolong (T *full, T *half, int half_dimx, int half_dimy, int half_dimz);

    template <typename T, int ord>
    void prolong_internal (T *full, T *half, int half_dimx, int half_dimy, int half_dimz);

    template<typename T, int ord>
    void prolong_hex2 (T *full, T *half, int half_dimx, int half_dimy, int half_dimz);

    template<typename T, int ord>
    void prolong_hex2a (T *full, T *half, int half_dimx, int half_dimy, int half_dimz);

    template <typename T, int ord, int htype>
    void prolong_hex_internal (T *full, T *half, int half_dimx, int half_dimy, int half_dimz);

#if HIP_ENABLED
    static inline std::vector<void *> abufs;
    static inline std::vector<void *> hbufs;
    static inline std::vector<double *> rbufs;

    template <typename T, int ord>
    void prolong_ortho_gpu(double *full,
                   T *half,
                   const int dimx,
                   const int dimy,
                   const int dimz,
                   double scale);

    template <typename T, int ord>
    void prolong_hex_gpu(double *full,
                   T *half,
                   const int dimx,
                   const int dimy,
                   const int dimz,
                   const int type,
                   double scale);
#endif

    double a[MAX_PROLONG_RATIO][MAX_PROLONG_ORDER];
    float af[MAX_PROLONG_RATIO][MAX_PROLONG_ORDER];
    int ratio;
    int order;

private:
    void cgen_prolong (double *coef, double fraction);
    void cgen_dist_inverse(std::vector<coef_idx> &coef_index, std::vector<double> &fraction);

    double cmix;
    TradeImages &TR;
    Lattice &L;
    BaseGrid &BG;
    int ibrav;
    std::vector<coef_idx> c000, c100, c010, c001, c110, c101, c011, c111;

};

#endif
#endif
