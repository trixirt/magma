/*
    -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2011

       @precisions normal z -> s d c

*/

// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <cuda_runtime_api.h>
#include <cublas.h>

// includes, project
#include "flops.h"
#include "magma.h"
#include "magma_lapack.h"
#include "testings.h"
#include "common_magma.h"

extern "C" magma_int_t
magma_zgeqr2x_gpu(magma_int_t *m, magma_int_t *n, cuDoubleComplex *dA,
                  magma_int_t *ldda, cuDoubleComplex *dtau,
                  cuDoubleComplex *dT, cuDoubleComplex *ddA,
                  double *dwork, magma_int_t *info);

extern "C" magma_int_t
magma_zgeqr2x2_gpu(magma_int_t *m, magma_int_t *n, cuDoubleComplex *dA,
                  magma_int_t *ldda, cuDoubleComplex *dtau,
                  cuDoubleComplex *dT, cuDoubleComplex *ddA,
                  double *dwork, magma_int_t *info);

extern "C" magma_int_t
magma_zgeqr2x3_gpu(magma_int_t *m, magma_int_t *n, cuDoubleComplex *dA,
                  magma_int_t *ldda, cuDoubleComplex *dtau,
                  cuDoubleComplex *dT, cuDoubleComplex *ddA,
                  double *dwork, magma_int_t *info);

/* ////////////////////////////////////////////////////////////////////////////
   -- Testing zgeqrf
*/
int main( int argc, char** argv)
{
    TESTING_INIT();

    real_Double_t    gflops, gpu_perf, gpu_time, cpu_perf, cpu_time;
    double           error, work[1];
    cuDoubleComplex  c_neg_one = MAGMA_Z_NEG_ONE;
    cuDoubleComplex *h_A, *h_T, *h_R, *tau, *dtau, *h_work, tmp[1];
    cuDoubleComplex *d_A, *d_T, *ddA;
    double *dwork;

    /* Matrix size */
    magma_int_t M = 0, N = 0, n2, lda, ldda, lwork;
    const int MAXTESTS = 10;
    magma_int_t msize[MAXTESTS] = { 1024, 2048, 3072, 4032, 5184, 6016, 7040, 8064, 9088, 10112 };
    magma_int_t nsize[MAXTESTS] = { 1024, 2048, 3072, 4032, 5184, 6016, 7040, 8064, 9088, 10112 };

    magma_int_t i, info, min_mn;
    magma_int_t ione     = 1;
    magma_int_t ISEED[4] = {0,0,0,1};
    magma_int_t checkres, version = 3;

    checkres = getenv("MAGMA_TESTINGS_CHECK") != NULL;

    // process command line arguments
    printf( "\nUsage: %s -N <m,n> -c -v <version 1..3>\n", argv[0] );
    printf( "  -N can be repeated up to %d times. If only m is given, then m=n.\n", MAXTESTS );
    printf( "  -c or setting $MAGMA_TESTINGS_CHECK runs LAPACK and checks result.\n\n" );
    int ntest = 0;
    for( int i = 1; i < argc; ++i ) {
        if ( strcmp("-N", argv[i]) == 0 && i+1 < argc ) {
            magma_assert( ntest < MAXTESTS, "error: -N repeated more than maximum %d tests\n", MAXTESTS );
            int m, n;
            info = sscanf( argv[++i], "%d,%d", &m, &n );
            if ( info == 2 && m > 0 && n > 0 ) {
                msize[ ntest ] = m;
                nsize[ ntest ] = n;
            }
            else if ( info == 1 && m > 0 ) {
                msize[ ntest ] = m;
                nsize[ ntest ] = m;  // implicitly
            }
            else {
                printf( "error: -N %s is invalid; ensure m > 0, n > 0.\n", argv[i] );
                exit(1);
            }
            M = max( M, msize[ ntest ] );
            N = max( N, nsize[ ntest ] );
            ntest++;
        }
        else if ( strcmp("-M", argv[i]) == 0 ) {
            printf( "-M has been replaced in favor of -N m,n to allow -N to be repeated.\n\n" );
            exit(1);
        }
        else if ( strcmp("-c", argv[i]) == 0 ) {
            checkres = true;
        }
        else if ( strcmp("-v", argv[i]) == 0 ) {
            sscanf( argv[++i], "%d", &version );
        }
        else {
            printf( "invalid argument: %s\n", argv[i] );
            exit(1);
        }
    }
    if ( ntest == 0 ) {
        ntest = MAXTESTS;
        M = msize[ntest-1];
        N = nsize[ntest-1];
    }

    ldda   = ((M+31)/32)*32;
    n2     = M * N;
    min_mn = min(M, N);

    /* Allocate memory for the matrix */
    TESTING_MALLOC(    tau, cuDoubleComplex, min_mn );
    TESTING_MALLOC(    h_A, cuDoubleComplex, n2     );
    TESTING_MALLOC(    h_T, cuDoubleComplex,    N*N );

    TESTING_HOSTALLOC( h_R, cuDoubleComplex, n2     );

    TESTING_DEVALLOC(  d_A, cuDoubleComplex, ldda*N );
    TESTING_DEVALLOC(  d_T, cuDoubleComplex,    N*N );
    TESTING_DEVALLOC(  ddA, cuDoubleComplex,    N*N );
    TESTING_DEVALLOC( dtau, cuDoubleComplex, min_mn );

    TESTING_DEVALLOC(dwork, double, max(5*min_mn, (32*2+2)*min_mn) );

    cudaMemset(ddA, 0, N*N*sizeof(cuDoubleComplex));
    cudaMemset(d_T, 0, N*N*sizeof(cuDoubleComplex));

    lwork = -1;
    lapackf77_zgeqrf(&M, &N, h_A, &M, tau, tmp, &lwork, &info);
    lwork = (magma_int_t)MAGMA_Z_REAL( tmp[0] );
    lwork = max(lwork, N*N);

    TESTING_MALLOC( h_work, cuDoubleComplex, lwork );

    printf("  M     N     CPU GFlop/s (ms)    GPU GFlop/s (ms)   ||R||_F/||A||_F  ||R_T||\n");
    printf("=============================================================================\n");
    for( i = 0; i < ntest; ++i ) {
        M = msize[i];
        N = nsize[i];
        min_mn= min(M, N);
        lda   = M;
        n2    = lda*N;
        ldda  = ((M+31)/32)*32;
        gflops = (FLOPS_ZGEQRF( M, N ) + FLOPS_ZGEQRT( M, N)) / 1e9;

        /* Initialize the matrix */
        lapackf77_zlarnv( &ione, ISEED, &n2, h_A );
        lapackf77_zlacpy( MagmaUpperLowerStr, &M, &N, h_A, &lda, h_R, &lda );
        magma_zsetmatrix( M, N, h_R, lda, d_A, ldda );

        /* ====================================================================
           Performs operation using MAGMA
           =================================================================== */

        cudaDeviceSynchronize();
        gpu_time = magma_wtime();

        if (version == 1)
            magma_zgeqr2x_gpu(&M, &N, d_A, &ldda, dtau, d_T, ddA, dwork, &info);
        else if (version == 2)
            magma_zgeqr2x2_gpu(&M, &N, d_A, &ldda, dtau, d_T, ddA, dwork, &info);
        else
            magma_zgeqr2x3_gpu(&M, &N, d_A, &ldda, dtau, d_T, ddA, dwork, &info);

        cudaDeviceSynchronize();
        gpu_time = magma_wtime() - gpu_time;
        gpu_perf = gflops / gpu_time;
        if (info != 0)
            printf("magma_zgeqrf returned error %d.\n", (int) info);
        
        if ( checkres ) {
            /* =====================================================================
               Performs operation using LAPACK
               =================================================================== */
            cpu_time = magma_wtime();
            lapackf77_zgeqrf(&M, &N, h_A, &lda, tau, h_work, &lwork, &info);
            lapackf77_zlarft( MagmaForwardStr, MagmaColumnwiseStr,
                              &M, &N, h_A, &lda, tau, h_work, &N);
            //magma_zgeqr2(&M, &N, h_A, &lda, tau, h_work, &info);
            cpu_time = magma_wtime() - cpu_time;
            cpu_perf = gflops / cpu_time;
            if (info != 0)
                printf("lapackf77_zgeqrf returned error %d.\n", (int) info);
            
            /* =====================================================================
               Check the result compared to LAPACK
               =================================================================== */
            magma_zgetmatrix( M, N, d_A, ldda, h_R, M );
            magma_zgetmatrix( N, N, ddA, N,    h_T, N );

            // Restore the upper triangular part of A before the check
            for(int col=0; col<N; col++){
                for(int row=0; row<=col; row++)
                    h_R[row + col*M] = h_T[row + col*N];
            }
            
            error = lapackf77_zlange("M", &M, &N, h_A, &lda, work);
            blasf77_zaxpy(&n2, &c_neg_one, h_A, &ione, h_R, &ione);
            error = lapackf77_zlange("M", &M, &N, h_R, &lda, work) / error;

            // Check if T is the same
            double terr = 0.;
            magma_zgetmatrix( N, N, d_T, N, h_T, N );

            for(int col=0; col<N; col++)
                for(int row=0; row<=col; row++)
                    terr += (  MAGMA_Z_ABS(h_work[row + col*N] - h_T[row + col*N])*
                               MAGMA_Z_ABS(h_work[row + col*N] - h_T[row + col*N])  );
            terr = magma_dsqrt(terr);

            printf("%5d %5d   %7.2f (%7.2f)   %7.2f (%7.2f)     %8.2e     %8.2e\n",
                   (int) M, (int) N, cpu_perf, 1000.*cpu_time, gpu_perf, 1000.*gpu_time,
                   error, terr);
        }
        else {
            printf("%5d %5d     ---   (  ---  )   %7.2f (%7.2f)     ---  \n",
                   (int) M, (int) N, gpu_perf, 1000.*gpu_time);
        }
    }
    
    /* Memory clean up */
    TESTING_FREE( tau );
    TESTING_FREE( h_A );
    TESTING_FREE( h_T );
    TESTING_FREE( h_work );
    TESTING_HOSTFREE( h_R );
    TESTING_DEVFREE( d_A  );
    TESTING_DEVFREE( d_T  );
    TESTING_DEVFREE( ddA  );
    TESTING_DEVFREE( dtau );
    TESTING_DEVFREE( dwork );

    TESTING_FINALIZE();
    return 0;
}
