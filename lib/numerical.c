/*******************************************************************************
 *   Adapted from PRIMME: Feb 7, 2008
 *
 *   PRIMME PReconditioned Iterative MultiMethod Eigensolver
 *   Copyright (C) 2005  James R. McCombs,  Andreas Stathopoulos
 *
 *   This file is part of PRIMME.
 *
 *   PRIMME is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   PRIMME is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * File: numerical.c
 *
 * Purpose - This file contains for the most part C wrapper routines for
 *    calling various BLAS and LAPACK FORTRAN routines.
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdarg.h>
#include "qdp-lapack_Complex.h"
#include "qdp-lapack_numerical_private.h"
#include "qdp-lapack_numerical.h"

// Define the following to make all gemvs and gemms sum in double precision 
#define USE_DOUBLE_PREC_SUMMATION
/******************************************************************************/
/* These wrappers perform the blas functions on a parallel distributed array
 * and apply the global sum of the resulting values through a user provided
 * global sum function, eg., MPI_Allreduce().
 *
 * The dot products do not call BLAS directly but a Fortran wrapper 
 * subroutine because Fortran functions cannot return complex to a C caller.
 *
 * These functions can be replaced with functions provided in libraries with 
 * their own dot+globalsum functions
 *
 ******************************************************************************/

/******************************************************************************/
void wrap_cgemv(char *transa, int *m, int *n, Complex_C *alpha, Complex_C *a,
   int *lda, Complex_C *x, int *incx, Complex_C *beta, Complex_C *y, int *incy,
   Complex_C *work, void *params)
{
// WARNING This function works only when transa = 'C' Otherwise globalsum
// is not needed and you should use plain BLAS_CGEMV

#ifdef USE_DOUBLE_PREC_SUMMATION

   int i, yix = 0, ONE=1;
   int count = 2*(*n) ; // 2*N floats need summation
   if (beta->r  == 0.0 &&  beta->i ==0.0 &&  /* Special case y = a'*x */
       alpha->r == 1.0 && alpha->i ==0.0 ) { 

      for (i=0;i<*n;i++)
         work[i] = zsum_cdot(m, &a[(*lda)*i], &ONE, x, incx);
      globalSumSingle(work, y, &count, params );

   }
   else { // Perform all the multiplicatons needed y = alpha*a'*x+beta*y
      for (i=0;i<*n;i++) {
	   work[0] = wrap_zsum_cdot(m, &a[(*lda)*i], &ONE, x, incx, params);
           c_mul_primme(&work[1], beta, &y[yix]);
           c_mul_primme(&work[2], alpha, work);
	   y[yix].r = work[1].r + work[2].r;
	   y[yix].i = work[1].i + work[2].i;
           yix = yix + *incy;
      }
   }
#else 
   int i, yix = 0;
   Complex_C tzero = {0.0, 0.0};
   Complex_C w1, w2;

   BLAS_CGEMV(transa, m, n, alpha, a, lda, x, incx, &tzero, work, incy);


   if (beta->r != 0.0 || beta->i !=0.0 || alpha->r != 1.0 || alpha->i != 0.0) {
      // Sum first a*x into work[n]
      globalSumDouble(work, &work[*n], n, params );
      // y = beta*y + alpha *(sum)
      for (i=0;i<*n;i++) {
         c_mul_primme(&w1, beta, &y[yix]);
         c_mul_primme(&w2, alpha, &work[*n+i]);
	 y[yix].r = w1.r + w2.r;
	 y[yix].i = w1.i + w2.i;
         yix = yix + *incy;
      }
   }
   else // just globalsum directly onto y
      globalSumDouble(work, y, n, params );

#endif

}

/******************************************************************************/
/* Perform result = x'*y where all are single precision but the summation 
 * is performed in double precision */
Complex_C wrap_zsum_cdot(int *n, Complex_C *x, int *incx, Complex_C *y, int *incy, void *params) 
{
   int i;
   Complex_C cdotc, xconj, prod;
   Complex_Z sum, gsum;

   sum.r = 0.0;sum.i = 0.0;
   for (i=0;i<*n;i++) {
       s_cnjg_primme(&xconj,&x[i]);
       c_mul_primme(&prod,&xconj,&y[i]);
       sum.r = sum.r + prod.r;
       sum.i = sum.i + prod.i;
   }
   i = 2;  /* 1 double complex = 16 bytes = 2 doubles */
   globalSumDouble(&sum, &gsum, &i, params);
   cdotc.r = gsum.r;
   cdotc.i = gsum.i;
   return(cdotc);

}
/******************************************************************************/
/* Perform result = x'*y where all are single precision but the summation 
 * is performed in double precision. THIS IS LOCAL ON THE NODE.
 * global sum if needed must be performed separately */
Complex_C zsum_cdot(int *n, Complex_C *x, int *incx, Complex_C *y, int *incy) 
{
   int i;
   Complex_C cdotc, xconj, prod;
   Complex_Z sum;

   sum.r = 0.0;sum.i = 0.0;
   for (i=0;i<*n;i++) {
       s_cnjg_primme(&xconj,&x[i]);
       c_mul_primme(&prod,&xconj,&y[i]);
       sum.r = sum.r + prod.r;
       sum.i = sum.i + prod.i;
   }
   cdotc.r = sum.r;
   cdotc.i = sum.i;
   return(cdotc);
}
/******************************************************************************/
Complex_C wrap_cdot(int *n, Complex_C *x, int *incx, Complex_C *y, int *incy,
   void *params) 
{
  //int ONE = 1;
  int TWO = 2;
  Complex_C cdotc_r, cdotc;
  CDOTCSUB(&cdotc_r, n, x, incx, y, incy);
  //cblas_cdotc_sub(*n, x, *incx, y, *incy, &cdotc_r);
  //globalSumDouble(&cdotc_r, &cdotc, &ONE, params);
  globalSumSingle(&cdotc_r, &cdotc, &TWO, params);
  return(cdotc);
}
/******************************************************************************/
void wrap_cgemm(char *transa, char *transb, int *m, int *n, int *k,
   Complex_C *alpha, Complex_C *a, int *lda, Complex_C *b, int *ldb,
   Complex_C *beta, Complex_C *c, int *ldc,
   Complex_C *work, void *params)
{
// WARNING This function works only when transa = 'C' and transb = "N"
// Otherwise globalsum is not needed and you should use plain BLAS_CGEMM
#ifdef USE_DOUBLE_PREC_SUMMATION
   int i, j, cx=0,  ONE=1;
   int count = 2*(*m);

   if (beta->r  == 0.0 &&  beta->i ==0.0 &&  /* Special case c = a'*b */
       alpha->r == 1.0 && alpha->i ==0.0 ) {
      for (i=0;i<*n;i++) { 
         for (j=0;j<*m;j++) // or c[cx++] = wrap_zsum_cdot(k, ...
            work[j] = zsum_cdot(k, &a[(*lda)*j], &ONE, &b[(*ldb)*i], &ONE );
         globalSumSingle(work, &c[(*ldc)*i], &count, params);
      }
   }
   else {  // Perform all the multiplicatons needed c = alpha*a'*b+beta*c
      for (i=0;i<*n;i++) { 
         cx = (*ldc)*i;
         for (j=0;j<*m;j++) { 
            work[0] = wrap_zsum_cdot(k, &a[(*lda)*j], &ONE, 
			        &b[(*ldb)*i], &ONE, params);
            c_mul_primme(&work[1], beta, &c[cx]);
            c_mul_primme(&work[2], alpha, work);
            c[cx].r = work[1].r + work[2].r;
            c[cx].i = work[1].i + work[2].i;
	    cx++;
         }
      }
   }
#else
// WARNING THIS DOES NOT WORK FOR beta != 0.
   int i;

   BLAS_CGEMM(transa, transb, m, n, k, alpha, a, lda, b, ldb, beta, work, m);

   if (*ldc == *m) {
      i = (*m)*(*n);
      globalSumDouble(work, c, &i, params);
   }
   else {
      /* Sum all n columns only up to m, not to ldc */
      for (i=0; i<*n; i++)
         globalSumDouble(&work[i*(*m)], &c[i*(*ldc)], m, params);
   }
#endif

}
/******************************************************************************/
/* Double precision */
/******************************************************************************/
void wrap_zgemv(char *transa, int *m, int *n, Complex_Z *alpha, Complex_Z *a,
   int *lda, Complex_Z *x, int *incx, Complex_Z *beta, Complex_Z *y, int *incy,
   Complex_C *work, void *params)
{

   BLAS_CGEMV(transa, m, n, alpha, a, lda, x, incx, beta, work, incy);

   int count = 2*(*n);// 2 N doubles need global sum
   globalSumDouble(work, y, &count, params );

}

/******************************************************************************/
Complex_Z wrap_zdot(int *n, Complex_Z *x, int *incx, Complex_Z *y, int *incy,
   void *params) 
{
   // a double comples is two doubles 
   int ONE=2;
   Complex_Z zdotc_r, zdotc;
   ZDOTCSUB(&zdotc_r, n, x, incx, y, incy);
   //cblas_zdotc_sub(*n, x, *incx, y, *incy, &zdotc_r);
   globalSumDouble(&zdotc_r, &zdotc, &ONE, params);
   return(zdotc);
}

/******************************************************************************
 * function 
 * primme_seq_globalSumDouble(void *sendBuf, double *recvBuf, int count) 
 *
 * This is the sequential default for the function globalSumDouble. 
 * If the program is parallel, the user must replace this with 
 * an Allreduce() function
 * 
 ******************************************************************************
 *        NOTE: The count and the copying refers to double datatypes
 ******************************************************************************/
#ifdef USE_QMP
void globalSumDouble(void *sendBuf, void *recvBuf, int *count, void *params) {
   int ONE = 1;
   QMP_status_t f;

   BLAS_DCOPY(count, (double *) sendBuf, &ONE, (double *) recvBuf, &ONE);
   f = QMP_sum_double_array((double* ) recvBuf, *count);
}
#else
void globalSumDouble(void *sendBuf, void *recvBuf, int *count, void *params) 
{
   int ONE = 1;

   BLAS_DCOPY(count, (double *) sendBuf, &ONE, (double *) recvBuf, &ONE);

}
#endif

#ifdef USE_QMP
void globalSumSingle(void *sendBuf, void *recvBuf, int *count, void *params) {
   int ONE = 1;
   QMP_status_t f;

   BLAS_SCOPY(count, (float *) sendBuf, &ONE, (float *) recvBuf, &ONE);
   f = QMP_sum_float_array((double* ) recvBuf, *count);
}
#else
void globalSumSingle(void *sendBuf, void *recvBuf, int *count, void *params) 
{
   int ONE = 1;

   BLAS_SCOPY(count, (float *) sendBuf, &ONE, (float *) recvBuf, &ONE);

}
#endif

