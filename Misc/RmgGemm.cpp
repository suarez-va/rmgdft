#if HIP_ENABLED
#include "tiled_mm.hpp"
#endif
#include <complex>
#include <typeinfo>
#include <string.h>

#include "const.h"
#include "rmgtypedefs.h"
#include "typedefs.h"
#include "RmgGemm.h"
#include "GpuAlloc.h"
#include "ErrorFuncs.h"
#include "RmgTimer.h"
#include "transition.h"
#include "rmg_error.h"


#if CUDA_ENABLED
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <cublas_v2.h>
#endif


#define         dgemm           RMG_FC_GLOBAL(dgemm, DGEMM)
#define         zgemm           RMG_FC_GLOBAL(zgemm, ZGEMM)
#define         cgemm           RMG_FC_GLOBAL(cgemm, CGEMM)
#define         sgemm           RMG_FC_GLOBAL(sgemm, SGEMM)


#if SYCL_ENABLED
    #include <CL/sycl.hpp>
    #include "oneapi/mkl/blas.hpp"
    #include "mkl.h"
#else
extern "C" {
void dgemm(const char *, const char *, int *, int *, int *, double *, double *, int *, double *, int *, double *, double *, int *);
void sgemm(const char *, const char *, int *, int *, int *, float *, float *, int *, float *, int *, float *, float *, int *);
void zgemm(const char *, const char *, int *, int *, int *, std::complex<double> *, std::complex<double> *, int *, std::complex<double> *, int *, std::complex<double> *, std::complex<double> *, int *);
void cgemm(const char *, const char *, int *, int *, int *, std::complex<float> *, std::complex<float> *, int *, std::complex<float> *, int *, std::complex<float> *, std::complex<float> *, int *);
}
#endif


/*
  These functions are used to hide the details of the matrix multiplication data types and GPU 
  utilization from the higher level routines.

  The first 13 arguments are the same as the standard dgemm args but with scalar quantities passed
  by value instead of by reference.

*/


template void RmgGemm<double>(char *, char *, int, int, int, double, double *, int, double *, int, 
                                  double, double *, int);

template void RmgGemm<float>(char *, char *, int, int, int, float, float *, int, float *, int, 
                                  float, float *, int);

template void RmgGemm<std::complex<double> >(char *, char *, int, int, int, std::complex<double>, 
                      std::complex<double> *, int, std::complex<double> *, int, 
                      std::complex<double>, std::complex<double> *, int);

template void RmgGemm<std::complex<float> >(char *, char *, int, int, int, std::complex<float>, 
                      std::complex<float> *, int, std::complex<float> *, int, 
                      std::complex<float>, std::complex<float> *, int);


template <typename DataType> void RmgGemm(char *transa, char *transb, int m, int n, int k, 
                             DataType alpha, DataType *A, int lda, DataType *B, int ldb, DataType beta, 
                             DataType *C, int ldc)
{

#if BLAS_PROFILE
    if(typeid(DataType) == typeid(std::complex<double>))
    {
        if(pct.gridpe==0) printf("ZGEMM CALL m=%d n=%d k=%d\n",m,n,k);
    }
    else
    {
        if(pct.gridpe==0) printf("DGEMM CALL m=%d n=%d k=%d\n",m,n,k);
    }
#endif

#if CUDA_ENABLED

    cublasStatus_t custat;
    cublasOperation_t cu_transA = CUBLAS_OP_N, cu_transB = CUBLAS_OP_N;

    if(!strcmp(transa, "t")) cu_transA = CUBLAS_OP_T;
    if(!strcmp(transa, "T")) cu_transA = CUBLAS_OP_T;
    if(!strcmp(transa, "c")) cu_transA = CUBLAS_OP_C;
    if(!strcmp(transa, "C")) cu_transA = CUBLAS_OP_C;

    if(!strcmp(transb, "t")) cu_transB = CUBLAS_OP_T;
    if(!strcmp(transb, "T")) cu_transB = CUBLAS_OP_T;
    if(!strcmp(transb, "c")) cu_transB = CUBLAS_OP_C;
    if(!strcmp(transb, "C")) cu_transB = CUBLAS_OP_C;

    int ka = m;
    if(!strcmp("n", transa)) ka = k;
    if(!strcmp("N", transa)) ka = k;

    int kb = k;
    if(!strcmp("n", transb)) kb = n;
    if(!strcmp("N", transb)) kb = n;

    if(ct.use_cublasxt && (typeid(DataType) == typeid(std::complex<double>)))
    {
        custat = cublasXtZgemm(ct.cublasxt_handle, cu_transA, cu_transB, m, n, k,
                            (cuDoubleComplex *)&alpha,
                            (cuDoubleComplex*)A, lda,
                            (cuDoubleComplex*)B, ldb,
                            (cuDoubleComplex*)&beta, (cuDoubleComplex*)C, ldc );
        ProcessGpublasError(custat);
        RmgGpuError(__FILE__, __LINE__, custat, "Problem executing cublasXtZgemm");
        return;
    }
    if(ct.use_cublasxt && (typeid(DataType) == typeid(std::complex<float>)))
    {
        custat = cublasXtCgemm(ct.cublasxt_handle, cu_transA, cu_transB, m, n, k,
                            (cuComplex *)&alpha,
                            (cuComplex*)A, lda,
                            (cuComplex*)B, ldb,
                            (cuComplex*)&beta, (cuComplex*)C, ldc );
        ProcessGpublasError(custat);
        RmgGpuError(__FILE__, __LINE__, custat, "Problem executing cublasXtCgemm");
        return;
    }
    if(ct.use_cublasxt && (typeid(DataType) == typeid(float)))
    {
        custat = cublasXtSgemm(ct.cublasxt_handle, cu_transA, cu_transB, m, n, k,
                            (float*)&alpha,
                            (float*)A, lda,
                            (float*)B, ldb,
                            (float*)&beta, (float*)C, ldc );
        ProcessGpublasError(custat);
        RmgGpuError(__FILE__, __LINE__, custat, "Problem executing cublasXtSgemm");
        return;
    }
    if(ct.use_cublasxt && (typeid(DataType) == typeid(double)))
    {
        custat = cublasXtDgemm(ct.cublasxt_handle, cu_transA, cu_transB, m, n, k,
                            (double*)&alpha,
                            (double*)A, lda,
                            (double*)B, ldb,
                            (double*)&beta, (double*)C, ldc );
        ProcessGpublasError(custat);
        RmgGpuError(__FILE__, __LINE__, custat, "Problem executing cublasXtDgemm");
        return;
    }

    cudaPointerAttributes attr;
    cudaError_t cudaerr;
    cudaerr = cudaPointerGetAttributes(&attr, A);
    bool a_dev = false;
#if (CUDA_VERSION_MAJOR > 10)
    if(cudaerr == cudaSuccess && attr.type == cudaMemoryTypeDevice) a_dev = true;
    cudaerr = cudaPointerGetAttributes(&attr, B);
    bool b_dev = false;
    if(cudaerr == cudaSuccess && attr.type == cudaMemoryTypeDevice) b_dev = true;
    cudaerr = cudaPointerGetAttributes(&attr, C);
    bool c_dev = false;
    if(cudaerr == cudaSuccess && attr.type == cudaMemoryTypeDevice) c_dev = true;
#else
    if(cudaerr == cudaSuccess && attr.type == cudaMemoryTypeDevice) a_dev = true;
    cudaerr = cudaPointerGetAttributes(&attr, B);
    bool b_dev = false;
    if(cudaerr == cudaSuccess && attr.type == cudaMemoryTypeDevice) b_dev = true;
    cudaerr = cudaPointerGetAttributes(&attr, C);
    bool c_dev = false;
    if(cudaerr == cudaSuccess && attr.type == cudaMemoryTypeDevice) c_dev = true;
#endif

    size_t a_size = (size_t)lda * (size_t)ka;
    size_t b_size = (size_t)ldb * (size_t)kb;
    size_t c_size = (size_t)ldc * (size_t)n;

    DeviceSynchronize();
    if(typeid(DataType) == typeid(std::complex<double>)) {
        std::complex<double> *dA=(std::complex<double> *)A, *dB=(std::complex<double> *)B, *dC=(std::complex<double> *)C;
        if(!a_dev) gpuMalloc((void **)&dA, a_size * sizeof(std::complex<double>));
        if(!b_dev) gpuMalloc((void **)&dB, b_size * sizeof(std::complex<double>));
        if(!c_dev) gpuMalloc((void **)&dC, c_size * sizeof(std::complex<double>));
        if(!a_dev) cudaMemcpy(dA, A, a_size * sizeof(std::complex<double>), cudaMemcpyDefault);
        if(!b_dev) cudaMemcpy(dB, B, b_size * sizeof(std::complex<double>), cudaMemcpyDefault);
        if(!c_dev && std::abs(beta) != 0.0) cudaMemcpy(dC, C, c_size * sizeof(std::complex<double>), cudaMemcpyDefault);
        custat = cublasZgemm(ct.cublas_handle, cu_transA, cu_transB, m, n, k,
                            (cuDoubleComplex *)&alpha,
                            (cuDoubleComplex*)dA, lda,
                            (cuDoubleComplex*)dB, ldb,
                            (cuDoubleComplex*)&beta, (cuDoubleComplex*)dC, ldc );
        ProcessGpublasError(custat);
        RmgGpuError(__FILE__, __LINE__, custat, "Problem executing cublasZgemm");
        if(!c_dev) cudaMemcpy(C, dC, c_size * sizeof(std::complex<double>), cudaMemcpyDefault);
        if(!c_dev) gpuFree(dC);
        if(!b_dev) gpuFree(dB);
        if(!a_dev) gpuFree(dA);
    }
    else if(typeid(DataType) == typeid(std::complex<float>)) {
        std::complex<float> *dA=(std::complex<float> *)A, *dB=(std::complex<float> *)B, *dC=(std::complex<float> *)C;
        if(!a_dev) gpuMalloc((void **)&dA, a_size * sizeof(std::complex<float>));
        if(!b_dev) gpuMalloc((void **)&dB, b_size * sizeof(std::complex<float>));
        if(!c_dev) gpuMalloc((void **)&dC, c_size * sizeof(std::complex<float>));
        if(!a_dev) cudaMemcpy(dA, A, a_size * sizeof(std::complex<float>), cudaMemcpyDefault);
        if(!b_dev) cudaMemcpy(dB, B, b_size * sizeof(std::complex<float>), cudaMemcpyDefault);
        if(!c_dev && std::abs(beta) != 0.0) cudaMemcpy(dC, C, c_size * sizeof(std::complex<float>), cudaMemcpyDefault);
        custat = cublasCgemm(ct.cublas_handle, cu_transA, cu_transB, m, n, k,
                            (cuComplex *)&alpha,
                            (cuComplex*)dA, lda,
                            (cuComplex*)dB, ldb,
                            (cuComplex*)&beta, (cuComplex*)dC, ldc );
        ProcessGpublasError(custat);
        RmgGpuError(__FILE__, __LINE__, custat, "Problem executing cublasCgemm");
        if(!c_dev) cudaMemcpy(C, dC, c_size * sizeof(std::complex<float>), cudaMemcpyDefault);
        if(!c_dev) gpuFree(dC);
        if(!b_dev) gpuFree(dB);
        if(!a_dev) gpuFree(dA);
    }
    else if(typeid(DataType) == typeid(float)) {
        float *dA=(float *)A, *dB=(float *)B, *dC=(float *)C;
        if(!a_dev) gpuMalloc((void **)&dA, a_size * sizeof(float));
        if(!b_dev) gpuMalloc((void **)&dB, b_size * sizeof(float));
        if(!c_dev) gpuMalloc((void **)&dC, c_size * sizeof(float));
        if(!a_dev) cudaMemcpy(dA, A, a_size * sizeof(float), cudaMemcpyDefault);
        if(!b_dev) cudaMemcpy(dB, B, b_size * sizeof(float), cudaMemcpyDefault);
        if(!c_dev && std::abs(beta) != 0.0) cudaMemcpy(dC, C, c_size * sizeof(float), cudaMemcpyDefault);
        custat = cublasSgemm(ct.cublas_handle, cu_transA, cu_transB, m, n, k,
                            (float*)&alpha,
                            (float*)dA, lda,
                            (float*)dB, ldb,
                            (float*)&beta, (float*)dC, ldc );
        ProcessGpublasError(custat);
        RmgGpuError(__FILE__, __LINE__, custat, "Problem executing cublasDgemm");
        if(!c_dev) cudaMemcpy(C, dC, c_size * sizeof(float), cudaMemcpyDefault);
        if(!c_dev) gpuFree(dC);
        if(!b_dev) gpuFree(dB);
        if(!a_dev) gpuFree(dA);
    }
    else {
        double *dA=(double *)A, *dB=(double *)B, *dC=(double *)C;
        if(!a_dev) gpuMalloc((void **)&dA, a_size * sizeof(double));
        if(!b_dev) gpuMalloc((void **)&dB, b_size * sizeof(double));
        if(!c_dev) gpuMalloc((void **)&dC, c_size * sizeof(double));
        if(!a_dev) cudaMemcpy(dA, A, a_size * sizeof(double), cudaMemcpyDefault);
        if(!b_dev) cudaMemcpy(dB, B, b_size * sizeof(double), cudaMemcpyDefault);
        if(!c_dev && std::abs(beta) != 0.0) cudaMemcpy(dC, C, c_size * sizeof(double), cudaMemcpyDefault);
        custat = cublasDgemm(ct.cublas_handle, cu_transA, cu_transB, m, n, k,
                            (double*)&alpha,
                            (double*)dA, lda,
                            (double*)dB, ldb,
                            (double*)&beta, (double*)dC, ldc );
        ProcessGpublasError(custat);
        RmgGpuError(__FILE__, __LINE__, custat, "Problem executing cublasDgemm");
        if(!c_dev) cudaMemcpy(C, dC, c_size * sizeof(double), cudaMemcpyDefault);
        if(!c_dev) gpuFree(dC);
        if(!b_dev) gpuFree(dB);
        if(!a_dev) gpuFree(dA);
    }
    DeviceSynchronize();
    return;

#elif HIP_ENABLED
    // For Tiled-MM
    if(ct.use_cublasxt)
    {
       static std::unique_ptr<gpu::mm_handle<DataType>> ctx;

       char trans_a = 'N', trans_b = 'N';
       if(!strcmp(transa, "n")) trans_a = 'N';
       if(!strcmp(transb, "n")) trans_b = 'N';
       if(!strcmp(transa, "t")) trans_a = 'T';
       if(!strcmp(transb, "t")) trans_b = 'T';
       if(!strcmp(transa, "c")) trans_a = 'C';
       if(!strcmp(transb, "c")) trans_b = 'C';
       if(!strcmp(transa, "C")) trans_a = 'C';
       if(!strcmp(transb, "C")) trans_b = 'C';

       // can probably tune these
       if(!ctx) ctx = gpu::make_context<DataType>(2, 5000, 5000, 5000);
       gpu::gemm<DataType>(*ctx,
          trans_a, trans_b,
          m, n, k,
          alpha,
          A, lda, 
          B, ldb, 
          beta,
          C, ldc, false, true);
          return;
    }

    //hipDeviceSynchronize();
    hipPointerAttribute_t attr;
    hipError_t hiperr;
    hiperr = hipPointerGetAttributes(&attr, A);
    bool a_dev = false;
    if(hiperr == hipSuccess && attr.type == hipMemoryTypeDevice) a_dev = true;

    hiperr = hipPointerGetAttributes(&attr, B);
    bool b_dev = false;
    if(hiperr == hipSuccess && attr.type == hipMemoryTypeDevice) b_dev = true;

    hiperr = hipPointerGetAttributes(&attr, C);
    bool c_dev = false;
    if(hiperr == hipSuccess && attr.type == hipMemoryTypeDevice) c_dev = true;

    hipblasStatus_t hipstat;
    hipblasOperation_t hip_transA = HIPBLAS_OP_N, hip_transB = HIPBLAS_OP_N;

    if(!strcmp(transa, "t")) hip_transA = HIPBLAS_OP_T;
    if(!strcmp(transa, "T")) hip_transA = HIPBLAS_OP_T;
    if(!strcmp(transa, "c")) hip_transA = HIPBLAS_OP_C;
    if(!strcmp(transa, "C")) hip_transA = HIPBLAS_OP_C;

    if(!strcmp(transb, "t")) hip_transB = HIPBLAS_OP_T;
    if(!strcmp(transb, "T")) hip_transB = HIPBLAS_OP_T;
    if(!strcmp(transb, "c")) hip_transB = HIPBLAS_OP_C;
    if(!strcmp(transb, "C")) hip_transB = HIPBLAS_OP_C;

    int ka = m;
    if(!strcmp("n", transa)) ka = k;
    if(!strcmp("N", transa)) ka = k;

    int kb = k;
    if(!strcmp("n", transb)) kb = n;
    if(!strcmp("N", transb)) kb = n;

    size_t a_size = (size_t)lda * (size_t)ka;
    size_t b_size = (size_t)ldb * (size_t)kb;
    size_t c_size = (size_t)ldc * (size_t)n;

    if(typeid(DataType) == typeid(std::complex<double>)) {
        std::complex<double> *dA=(std::complex<double> *)A, *dB=(std::complex<double> *)B, *dC=(std::complex<double> *)C;
        if(!a_dev) gpuMalloc((void **)&dA, a_size * sizeof(std::complex<double>));
        if(!b_dev) gpuMalloc((void **)&dB, b_size * sizeof(std::complex<double>));
        if(!c_dev) gpuMalloc((void **)&dC, c_size * sizeof(std::complex<double>));
        if(!a_dev) hipMemcpyHtoD(dA, A, a_size * sizeof(std::complex<double>));
        if(!b_dev) hipMemcpyHtoD(dB, B, b_size * sizeof(std::complex<double>));
        if(!c_dev && std::abs(beta) != 0.0) hipMemcpyHtoD(dC, C, c_size * sizeof(std::complex<double>));
        hipstat = hipblasZgemm(ct.hipblas_handle, hip_transA, hip_transB, m, n, k,
                            (hipblasDoubleComplex *)&alpha,
                            (hipblasDoubleComplex*)dA, lda,
                            (hipblasDoubleComplex*)dB, ldb,
                            (hipblasDoubleComplex*)&beta, (hipblasDoubleComplex*)dC, ldc );
        if(!c_dev) hipMemcpyDtoH(C, dC, c_size * sizeof(std::complex<double>));
        if(!c_dev) gpuFree(dC);
        if(!b_dev) gpuFree(dB);
        if(!a_dev) gpuFree(dA);
        ProcessGpublasError(hipstat);
        RmgGpuError(__FILE__, __LINE__, hipstat, "Problem executing hipblasZgemm");
    }
    else if(typeid(DataType) == typeid(std::complex<float>)) {
        std::complex<float> *dA=(std::complex<float> *)A, *dB=(std::complex<float> *)B, *dC=(std::complex<float> *)C;
        if(!a_dev) gpuMalloc((void **)&dA, a_size * sizeof(std::complex<float>));
        if(!b_dev) gpuMalloc((void **)&dB, b_size * sizeof(std::complex<float>));
        if(!c_dev) gpuMalloc((void **)&dC, c_size * sizeof(std::complex<float>));
        if(!a_dev) hipMemcpyHtoD(dA, A, a_size * sizeof(std::complex<float>));
        if(!b_dev) hipMemcpyHtoD(dB, B, b_size * sizeof(std::complex<float>));
        if(!c_dev && std::abs(beta) != 0.0) hipMemcpyHtoD(dC, C, c_size * sizeof(std::complex<float>));
        hipstat = hipblasCgemm(ct.hipblas_handle, hip_transA, hip_transB, m, n, k,
                            (hipblasComplex *)&alpha,
                            (hipblasComplex*)dA, lda,
                            (hipblasComplex*)dB, ldb,
                            (hipblasComplex*)&beta, (hipblasComplex*)dC, ldc );
        if(!c_dev) hipMemcpyDtoH(C, dC, c_size * sizeof(std::complex<float>));
        if(!c_dev) gpuFree(dC);
        if(!b_dev) gpuFree(dB);
        if(!a_dev) gpuFree(dA);
        ProcessGpublasError(hipstat);
        RmgGpuError(__FILE__, __LINE__, hipstat, "Problem executing hipblasCgemm");
    }
    else if(typeid(DataType) == typeid(float)) {
        float *dA=(float *)A, *dB=(float *)B, *dC=(float *)C;
        if(!a_dev) gpuMalloc((void **)&dA, a_size * sizeof(float));
        if(!b_dev) gpuMalloc((void **)&dB, b_size * sizeof(float));
        if(!c_dev) gpuMalloc((void **)&dC, c_size * sizeof(float));
        if(!a_dev) hipMemcpyHtoD(dA, A, a_size * sizeof(float));
        if(!b_dev) hipMemcpyHtoD(dB, B, b_size * sizeof(float));
        if(!c_dev && std::abs(beta) != 0.0) hipMemcpyHtoD(dC, C, c_size * sizeof(float));
        hipstat = hipblasSgemm(ct.hipblas_handle, hip_transA, hip_transB, m, n, k,
                            (float*)&alpha,
                            (float*)dA, lda,
                            (float*)dB, ldb,
                            (float*)&beta, (float*)dC, ldc );
        if(!c_dev) hipMemcpyDtoH(C, dC, c_size * sizeof(float));
        if(!c_dev) gpuFree(dC);
        if(!b_dev) gpuFree(dB);
        if(!a_dev) gpuFree(dA);
        ProcessGpublasError(hipstat);
        RmgGpuError(__FILE__, __LINE__, hipstat, "Problem executing hipblasSgemm");
    }
    else {
        double *dA=(double *)A, *dB=(double *)B, *dC=(double *)C;
        if(!a_dev) gpuMalloc((void **)&dA, a_size * sizeof(double));
        if(!b_dev) gpuMalloc((void **)&dB, b_size * sizeof(double));
        if(!c_dev) gpuMalloc((void **)&dC, c_size * sizeof(double));
        if(!a_dev) hipMemcpyHtoD(dA, A, a_size * sizeof(double));
        if(!b_dev) hipMemcpyHtoD(dB, B, b_size * sizeof(double));
        if(!c_dev && std::abs(beta) != 0.0) hipMemcpyHtoD(dC, C, c_size * sizeof(double));
        hipstat = hipblasDgemm(ct.hipblas_handle, hip_transA, hip_transB, m, n, k,
                            (double*)&alpha,
                            (double*)dA, lda,
                            (double*)dB, ldb,
                            (double*)&beta, (double*)dC, ldc );
        if(!c_dev) hipMemcpyDtoH(C, dC, c_size * sizeof(double));
        if(!c_dev) gpuFree(dC);
        if(!b_dev) gpuFree(dB);
        if(!a_dev) gpuFree(dA);
        ProcessGpublasError(hipstat);
        RmgGpuError(__FILE__, __LINE__, hipstat, "Problem executing hiplasDgemm");
    }

    //hipDeviceSynchronize();
#elif SYCL_ENABLED

    oneapi::mkl::transpose sycl_transA = oneapi::mkl::transpose::nontrans;
    oneapi::mkl::transpose sycl_transB = oneapi::mkl::transpose::nontrans;

    if(!strcmp(transa, "t")) sycl_transA = oneapi::mkl::transpose::trans;
    if(!strcmp(transa, "T")) sycl_transA = oneapi::mkl::transpose::trans;
    if(!strcmp(transa, "c")) sycl_transA = oneapi::mkl::transpose::conjtrans;
    if(!strcmp(transa, "C")) sycl_transA = oneapi::mkl::transpose::conjtrans;

    if(!strcmp(transb, "t")) sycl_transB = oneapi::mkl::transpose::trans;
    if(!strcmp(transb, "T")) sycl_transB = oneapi::mkl::transpose::trans;
    if(!strcmp(transb, "c")) sycl_transB = oneapi::mkl::transpose::conjtrans;
    if(!strcmp(transb, "C")) sycl_transB = oneapi::mkl::transpose::conjtrans;

    int ka = m;
    if(!strcmp("n", transa)) ka = k;
    if(!strcmp("N", transa)) ka = k;

    int kb = k;
    if(!strcmp("n", transb)) kb = n;
    if(!strcmp("N", transb)) kb = n;

    size_t a_size = (size_t)lda * (size_t)ka;
    size_t b_size = (size_t)ldb * (size_t)kb;
    size_t c_size = (size_t)ldc * (size_t)n;

    cl::sycl::buffer<DataType, 1> bufA((DataType *)A, a_size, {cl::sycl::property::buffer::use_host_ptr()});
    bufA.set_final_data(nullptr);
    cl::sycl::buffer<DataType, 1> bufB((DataType *)B, b_size, {cl::sycl::property::buffer::use_host_ptr()});
    bufB.set_final_data(nullptr);
    cl::sycl::buffer<DataType, 1> bufC((DataType *)C, c_size, {cl::sycl::property::buffer::use_host_ptr()});
    try {
        oneapi::mkl::blas::gemm(ct.sycl_Q, sycl_transA, sycl_transB, m, n, k, alpha, 
                                bufA, lda, bufB, ldb, beta, bufC, ldc);
    }
    catch(cl::sycl::exception const& e) {
        std::cout << "\t\tCaught synchronous SYCL exception during GEMM:\n"
        << e.what() << std::endl << std::endl;
        rmg_error_handler (__FILE__, __LINE__, "Terminating");
    }

#else

    
//    RmgTimer *RT = new RmgTimer("gemmmmm ");
    if(typeid(DataType) == typeid(std::complex<double>))
    {
        if(ct.use_alt_zgemm)
            MyZgemm(transa, transb, m, n, k, (std::complex<double> *)(&alpha), (std::complex<double> *)A, lda, 
             (std::complex<double> *)B, ldb, (std::complex<double> *)(&beta), (std::complex<double> *)C, ldc);
        else
            zgemm(transa, transb, &m, &n, &k, (std::complex<double> *)&alpha, (std::complex<double> *)A, &lda,
            (std::complex<double> *)B, &ldb, (std::complex<double> *)&beta, (std::complex<double> *)C, &ldc);
    }
    else if(typeid(DataType) == typeid(std::complex<float>)) {
            cgemm(transa, transb, &m, &n, &k, (std::complex<float> *)&alpha, (std::complex<float> *)A, &lda,
             (std::complex<float> *)B, &ldb, (std::complex<float> *)(&beta), (std::complex<float> *)C, &ldc);
    }
    else if(typeid(DataType) == typeid(float)) {
        sgemm(transa, transb, &m, &n, &k, (float *)&alpha, (float *)A, &lda, 
        (float *)B, &ldb, (float *)&beta, (float *)C, &ldc);
    }
    else {
        dgemm(transa, transb, &m, &n, &k, (double *)&alpha, (double *)A, &lda, 
        (double *)B, &ldb, (double *)&beta, (double *)C, &ldc);
    }
//    delete RT;

#endif
}
