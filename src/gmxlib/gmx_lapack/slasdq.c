#include <ctype.h>
#include "gmx_blas.h"
#include "gmx_lapack.h"

void
F77_FUNC(slasdq,SLASDQ)(char *uplo, 
	int *sqre, 
	int *n, 
	int *ncvt,
	int *nru, 
	int *ncc,
	float *d, 
	float *e, 
	float *vt, 
	int *ldvt, 
	float *u, 
	int *ldu, 
	float *c, 
	int *ldc, 
	float *work, 
	int *info)
{
  char xuplo=toupper(*uplo);

  int i,j,i1;
  float r, cs, sn;
  int np1, isub;
  float smin;
  int sqre1 = *sqre;
  int n1 = *n;
  int rotate = (*ncvt>0 || *nru>0 || *ncc>0);

  np1 = n1 + 1;
  *info = 0;

  if (*n == 0) 
    return;

    if (xuplo == 'U' && sqre1 == 1) {
	for (i=0; i<n1-1; i++) {
	    F77_FUNC(slartg,SLARTG)(d+i,e+i,&cs,&sn,&r);
	    d[i] = r;
	    e[i] = sn * d[i + 1];
	    d[i+1] = cs * d[i + 1];
	    if (rotate) {
		work[i] = cs;
		work[n1 + i] = sn;
	    }
	}
	F77_FUNC(slartg,SLARTG)(d+n1-1,e+n1-1,&cs,&sn,&r);
	d[n1-1] = r;
	e[n1-1] = 0.0;
	if (rotate) {
	    work[n1-1] = cs;
	    work[2*n1-1] = sn;
	}
	xuplo='L';
	sqre1 = 0;

	if (*ncvt > 0) {
	    F77_FUNC(slasr,SLASR)("L", "V", "F", &np1, ncvt, work, work+np1-1,vt,ldvt);
	}
    }

    if (xuplo=='L') {
	for (i=0;i<(n1-1);i++) {
	    F77_FUNC(slartg,SLARTG)(d+i, e+i, &cs, &sn, &r);
	    d[i] = r;
	    e[i] = sn * d[i + 1];
	    d[i + 1] = cs * d[i + 1];
	    if (rotate) {
		work[i] = cs;
		work[n1 + i] = sn;
	    }
	}

	if (sqre1 == 1) {
	    F77_FUNC(slartg,SLARTG)(d+n1-1, e+n1-1, &cs, &sn, &r);
	    d[n1-1] = r;
	    if (rotate) {
		work[n1 - 1] = cs;
		work[2*n1 - 1] = sn;
	    }
	}

	if (*nru > 0) {
	  if (sqre1 == 0) 
	    F77_FUNC(slasr,SLASR)("R","V","F",nru,n,work,work+np1-1,u,ldu);
	  else 
	    F77_FUNC(slasr,SLASR)("R","V","F",nru,&np1,work,work+np1-1,u,ldu);
      	}
	if (*ncc > 0) {
	    if (sqre1 == 0) 
	      F77_FUNC(slasr,SLASR)("L","V","F",n, ncc, work, work+np1-1,c,ldc);
	    else 
		F77_FUNC(slasr,SLASR)("L","V","F",&np1, ncc, work, work+np1-1,c,ldc);
	}
    }

    F77_FUNC(sbdsqr,SBDSQR)("U", n, ncvt, nru, ncc, d,e,vt,ldvt,u,ldu,c,ldc,work,info);

    for (i=0;i<n1;i++) {

	isub = i;
	smin = d[i];
	for (j=i+1;j<n1;j++) {
	    if (d[j] < smin) {
		isub = j;
		smin = d[j];
	    }
	}
	if (isub != i) {
	    d[isub] = d[i];
	    d[i] = smin;
	    if (*ncvt > 0) 
	      F77_FUNC(sswap,SSWAP)(ncvt, vt+isub,ldvt,vt+i,ldvt);
	    if (*nru > 0) {
	      i1 = 1;
	      F77_FUNC(sswap,SSWAP)(nru, u+(isub*(*ldu)),&i1,u+i*(*ldu),&i1);
	    } 
	    if (*ncc > 0) 
	      F77_FUNC(sswap,SSWAP)(ncc,c+isub,ldc,c+i,ldc);
	    
	}
    }
    return ;
}

