/* $Id: DGNLilupackfactor.c 796 2015-08-06 20:27:07Z bolle $ */
/* ========================================================================== */
/* === AMGfactor mexFunction ================================================ */
/* ========================================================================== */

/*
    Usage:

    Return the structure 'options' and preconditioner 'PREC' for ILUPACK V2.3

    Example:

    % for initializing parameters
    [PREC, options] = DGNLilupackfactor(A,options);



    Authors:

        Matthias Bollhoefer, TU Braunschweig

    Date:

        January 23, 2009. ILUPACK V2.3.

    Acknowledgements:

        This work was supported from 2002 to 2007 by the DFG research center
        MATHEON "Mathematics for key technologies"

    Notice:

        Copyright (c) 2009 by TU Braunschweig.  All Rights Reserved.

        THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY
        EXPRESSED OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.

    Availability:

        This file is located at

        http://ilupack.tu-bs.de/
*/

/* ========================================================================== */
/* === Include files and prototypes ========================================= */
/* ========================================================================== */

#include "matrix.h"
#include "mex.h"
#include <ilupack.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FIELDS 100

/* ========================================================================== */
/* === mexFunction ========================================================== */
/* ========================================================================== */

void mexFunction(
    /* === Parameters ======================================================= */

    int nlhs,             /* number of left-hand sides */
    mxArray *plhs[],      /* left-hand side matrices */
    int nrhs,             /* number of right--hand sides */
    const mxArray *prhs[] /* right-hand side matrices */
) {
  Dmat A;
  DAMGlevelmat *PRE, *current;
  SAMGlevelmat *SPRE, *scurrent;
  DILUPACKparam *param;
  integer n, nnzU;
  int tv_exists, tv_field;

  const char **fnames;
  const char *pnames[] = {
      "n",   "nB",      "L",          "D",           "U",           "E",
      "F",   "rowscal", "colscal",    "p",           "invq",        "param",
      "ptr", "isreal",  "isdefinite", "issymmetric", "ishermitian", "issingle",
      "A_H", "errorL",  "errorU",     "errorS"};

  const mwSize *dims;
  const mwSize mydims[] = {1, 1};
  mxClassID *classIDflags;
  mxArray *tmp, *fout, *PRE_input, *tv_input, *A_input, *options_input,
      *PRE_output, *options_output, *S_output, *tv_output;
  char *pdata, *input_buf, *output_buf;
  mwSize nnz, ndim, buflen;
  mwIndex jstruct, *irs, *jcs;
  int ifield, status, nfields, ierr, i, j, k, l, m;
  integer *ibuff, *istack, *iconvert, jj, cnt;
  size_t mrows, ncols, sizebuf;
  double dbuf, *A_valuesR, *convert, *sr, *pr;
  float *spr;
  mwIndex *A_ja, /* row indices of input matrix A */
      *A_ia;     /* column pointers of input matrix A */

  if (nrhs != 2)
    mexErrMsgTxt("Two input arguments required.");
  else if (nlhs != 2)
    mexErrMsgTxt("Too many output arguments.");
  else if (!mxIsStruct(prhs[1]))
    mexErrMsgTxt("Second input must be a structure.");
  else if (!mxIsNumeric(prhs[0]))
    mexErrMsgTxt("First input must be a matrix.");

  /* The first input must be a square matrix.*/
  A_input = (mxArray *)prhs[0];
  /* get size of input matrix A */
  mrows = mxGetM(A_input);
  ncols = mxGetN(A_input);
  nnz = mxGetNzmax(A_input);
  if (mrows != ncols) {
    mexErrMsgTxt("First input must be a square matrix.");
  }
  if (!mxIsSparse(A_input)) {
    mexErrMsgTxt("ILUPACK: input matrix must be in sparse format.");
  }

  /* copy input matrix to sparse row format */
  A.nc = A.nr = mrows;
  A.ia = (integer *)MAlloc((size_t)(A.nc + 1) * sizeof(integer),
                           "DGNLilupackfactor");
  A.ja = (integer *)MAlloc((size_t)nnz * sizeof(integer), "DGNLilupackfactor");
  A.a = (double *)MAlloc((size_t)nnz * sizeof(double), "DGNLilupackfactor");

  A_ja = (mwIndex *)mxGetIr(A_input);
  A_ia = (mwIndex *)mxGetJc(A_input);
  A_valuesR = (double *)mxGetPr(A_input);

  /* -------------------------------------------------------------------- */
  /* ..  Convert matrix from 0-based C-notation to Fortran 1-based        */
  /*     notation.                                                        */
  /* -------------------------------------------------------------------- */

  /*
  for (i = 0 ; i < ncols ; i++)
    for (j = A_ia[i] ; j < A_ia[i+1] ; j++)
       printf("i=%d j=%d  A.real=%e\n", i+1,  A_ja[j]+1, A_valuesR[j]);
  */

  istack = (integer *)MAlloc((size_t)A.nr * sizeof(integer),
                             "DGNLilupackfactor:istack");

  ibuff = (integer *)MAlloc((size_t)(A.nr + 1) * sizeof(integer),
                            "DGNLilupackfactor:ibuff");
  for (i = 0; i <= A.nr; i++)
    ibuff[i] = 0;
  /* remember that MATLAB uses storage by columns and NOT by rows! */
  for (i = 0; i < A.nr; i++) {
    for (j = A_ia[i]; j < A_ia[i + 1]; j++) {
      k = A_ja[j];
      ibuff[k + 1]++;
    }
  }
  /* now we know how many entries are located in every row */

  /* switch to pointer structure */
  for (i = 0; i < A.nr; i++)
    ibuff[i + 1] += ibuff[i];

  for (i = 0; i < ncols; i++) {
    for (j = A_ia[i]; j < A_ia[i + 1]; j++) {
      /* row index l in C-notation */
      l = A_ja[j];
      /* where does row l currently start */
      k = ibuff[l];
      /* column index will be i in FORTRAN notation */
      A.ja[k] = i + 1;
      A.a[k++] = A_valuesR[j];
      ibuff[l] = k;
    }
  }
  /* switch to FORTRAN style */
  for (i = A.nr; i > 0; i--)
    A.ia[i] = ibuff[i - 1] + 1;
  A.ia[0] = 1;

  /*
  printf("\n");
  for (i = 0 ; i < A.nr ; i++)
    for (j = A.ia[i]-1 ; j < A.ia[i+1]-1 ; j++)
        printf("i=%d j=%d  A.real=%e\n", i+1,  A.ja[j], A.a[j]);
  */

  param = (DILUPACKparam *)MAlloc((size_t)sizeof(DILUPACKparam),
                                  "DGNLilupackfactor:param");
  DGNLAMGinit(&A, param);

  /* Get second input arguments */
  options_input = (mxArray *)prhs[1];
  nfields = mxGetNumberOfFields(options_input);

  /* Allocate memory  for storing classIDflags */
  classIDflags =
      (mxClassID *)mxCalloc((size_t)nfields, (size_t)sizeof(mxClassID));

  /* allocate memory  for storing pointers */
  fnames = mxCalloc((size_t)nfields, (size_t)sizeof(*fnames));

  /* Get field name pointers */
  for (ifield = 0; ifield < nfields; ifield++) {
    fnames[ifield] = mxGetFieldNameByNumber(options_input, ifield);
  }

  /* import data */
  tv_exists = 0;
  tv_field = -1;
  for (ifield = 0; ifield < nfields; ifield++) {
    tmp = mxGetFieldByNumber(options_input, 0, ifield);
    classIDflags[ifield] = mxGetClassID(tmp);

    ndim = mxGetNumberOfDimensions(tmp);
    dims = mxGetDimensions(tmp);

    /* Create string/numeric array */
    if (classIDflags[ifield] == mxCHAR_CLASS) {
      /* Get the length of the input string. */
      buflen = (mxGetM(tmp) * mxGetN(tmp)) + 1;

      /* Allocate memory for input and output strings. */
      input_buf = (char *)mxCalloc((size_t)buflen, (size_t)sizeof(char));

      /* Copy the string data from tmp into a C string
         input_buf. */
      status = mxGetString(tmp, input_buf, buflen);

      if (!strcmp("amg", fnames[ifield])) {
        if (strcmp(param->amg, input_buf)) {
          param->amg =
              (char *)MAlloc((size_t)buflen * sizeof(char), "ilupackfactor");
          strcpy(param->amg, input_buf);
        }
      } else if (!strcmp("presmoother", fnames[ifield])) {
        if (strcmp(param->presmoother, input_buf)) {
          param->presmoother =
              (char *)MAlloc((size_t)buflen * sizeof(char), "ilupackfactor");
          strcpy(param->presmoother, input_buf);
        }
      } else if (!strcmp("postsmoother", fnames[ifield])) {
        if (strcmp(param->postsmoother, input_buf)) {
          param->postsmoother =
              (char *)MAlloc((size_t)buflen * sizeof(char), "ilupackfactor");
          strcpy(param->postsmoother, input_buf);
        }
      } else if (!strcmp("typecoarse", fnames[ifield])) {
        if (strcmp(param->typecoarse, input_buf)) {
          param->typecoarse =
              (char *)MAlloc((size_t)buflen * sizeof(char), "ilupackfactor");
          strcpy(param->typecoarse, input_buf);
        }
      } else if (!strcmp("typetv", fnames[ifield])) {
        if (strcmp(param->typetv, input_buf)) {
          param->typetv =
              (char *)MAlloc((size_t)buflen * sizeof(char), "ilupackfactor");
          strcpy(param->typetv, input_buf);
        }
        /* 'static' or 'dynamic' test vector */
        if (strcmp("none", input_buf))
          tv_exists = -1;
      } else if (!strcmp("FCpart", fnames[ifield])) {
        if (strcmp(param->FCpart, input_buf)) {
          param->FCpart =
              (char *)MAlloc((size_t)buflen * sizeof(char), "ilupackfactor");
          strcpy(param->FCpart, input_buf);
        }
      } else if (!strcmp("solver", fnames[ifield])) {
        if (strcmp(param->solver, input_buf)) {
          param->solver =
              (char *)MAlloc((size_t)buflen * sizeof(char), "ilupackfactor");
          strcpy(param->solver, input_buf);
        }
      } else if (!strcmp("ordering", fnames[ifield])) {
        if (strcmp(param->ordering, input_buf)) {
          param->ordering =
              (char *)MAlloc((size_t)buflen * sizeof(char), "ilupackfactor");
          strcpy(param->ordering, input_buf);
        }
      } else {
        /* mexPrintf("%s ignored\n",fnames[ifield]);fflush(stdout); */
      }
    } else {
      if (!strcmp("elbow", fnames[ifield])) {
        param->elbow = *mxGetPr(tmp);
      } else if (!strcmp("lfilS", fnames[ifield])) {
        param->lfilS = *mxGetPr(tmp);
      } else if (!strcmp("lfil", fnames[ifield])) {
        param->lfil = *mxGetPr(tmp);
      } else if (!strcmp("maxit", fnames[ifield])) {
        param->maxit = *mxGetPr(tmp);
      } else if (!strcmp("droptolS", fnames[ifield])) {
        param->droptolS = *mxGetPr(tmp);
      } else if (!strcmp("droptolc", fnames[ifield])) {
        param->droptolc = *mxGetPr(tmp);
      } else if (!strcmp("droptol", fnames[ifield])) {
        param->droptol = *mxGetPr(tmp);
      } else if (!strcmp("condest", fnames[ifield])) {
        param->condest = *mxGetPr(tmp);
      } else if (!strcmp("restol", fnames[ifield])) {
        param->restol = *mxGetPr(tmp);
      } else if (!strcmp("npresmoothing", fnames[ifield])) {
        param->npresmoothing = *mxGetPr(tmp);
      } else if (!strcmp("npostmoothing", fnames[ifield])) {
        param->npostsmoothing = *mxGetPr(tmp);
      } else if (!strcmp("ncoarse", fnames[ifield])) {
        param->ncoarse = *mxGetPr(tmp);
      } else if (!strcmp("matching", fnames[ifield])) {
        param->matching = *mxGetPr(tmp);
      } else if (!strcmp("nrestart", fnames[ifield])) {
        param->nrestart = *mxGetPr(tmp);
      } else if (!strcmp("damping", fnames[ifield])) {
        param->damping = *mxGetPr(tmp);
      } else if (!strcmp("contraction", fnames[ifield])) {
        param->contraction = *mxGetPr(tmp);
      } else if (!strcmp("tv", fnames[ifield])) {
        tv_field = ifield;
      } else if (!strcmp("mixedprecision", fnames[ifield])) {
        param->mixedprecision = *mxGetPr(tmp);
      } else if (!strcmp("coarsereduce", fnames[ifield])) {
        if (*mxGetPr(tmp) != 0.0)
          param->flags |= COARSE_REDUCE;
        else
          param->flags &= ~COARSE_REDUCE;
      } else if (!strcmp("decoupleconstraints", fnames[ifield])) {
        if (*mxGetPr(tmp) > 0.0)
          param->flags |= DECOUPLE_CONSTRAINTSHH;
        else if (*mxGetPr(tmp) < 0.0)
          param->flags |= DECOUPLE_CONSTRAINTS;
        else
          param->flags &= ~(DECOUPLE_CONSTRAINTS | DECOUPLE_CONSTRAINTSHH);
      } else {
        /* mexPrintf("%s ignored\n",fnames[ifield]);fflush(stdout); */
      }
    }
  }
  if (param->droptolS > 0.125 * param->droptol) {
    mexPrintf("!!! ILUPACK Warning !!!\n");
    mexPrintf("`param.droptolS' is recommended to be one order of magnitude "
              "less than `param.droptol'\n");
  }

  if (tv_exists && tv_field >= 0) {
    tmp = mxGetFieldByNumber(options_input, 0, tv_field);
    param->tv = mxGetPr(tmp);
  }

  /* mexPrintf("start factorization\n"); fflush(stdout); */
  PRE =
      (DAMGlevelmat *)MAlloc((size_t)sizeof(DAMGlevelmat), "DGNLilupackfactor");
  ierr = DGNLAMGfactor(&A, PRE, param);
  /* mexPrintf("factorization completed\n"); fflush(stdout); */

  if (ierr) {
    nlhs = 0;
    /* finally release memory of the preconditioner */
    DGNLAMGdelete(&A, PRE, param);
    free(A.ia);
    free(A.ja);
    free(A.a);

    free(PRE);
    free(param);
  }

  switch (ierr) {
  case 0: /* perfect! */
    break;
  case -1: /* Error. input matrix may be wrong.
              (The elimination process has generated a
              row in L or U whose length is .gt.  n.) */
    mexErrMsgTxt("ILUPACK error, data may be wrong.");
    break;
  case -2: /* The matrix L overflows the array alu */
    mexErrMsgTxt("memory overflow, please increase `options.elbow' and retry");
    break;
  case -3: /* The matrix U overflows the array alu */
    mexErrMsgTxt("memory overflow, please increase `options.elbow' and retry");
    break;
  case -4: /* Illegal value for lfil */
    mexErrMsgTxt("Illegal value for `options.lfil'\n");
    break;
  case -5: /* zero row encountered */
    mexErrMsgTxt("zero row encountered, please reduce `options.droptol'\n");
    break;
  case -6: /* zero column encountered */
    mexErrMsgTxt("zero column encountered, please reduce `options.droptol'\n");
    break;
  case -7: /* buffers too small */
    mexErrMsgTxt("memory overflow, please increase `options.elbow' and retry");
    break;
  default: /* zero pivot encountered at step number ierr */
    mexErrMsgTxt("zero pivot encountered, please reduce `options.droptol'\n");
    break;
  } /* end switch */
  if (ierr) {
    plhs[0] = NULL;
    return;
  }

  /* read a struct matrices for output */
  nlhs = 2;
  plhs[1] = mxCreateStructMatrix((mwSize)1, (mwSize)1, nfields, fnames);
  if (plhs[1] == NULL)
    mexErrMsgTxt("Could not create structure mxArray");
  options_output = plhs[1];

  /* export data */
  for (ifield = 0; ifield < nfields; ifield++) {
    tmp = mxGetFieldByNumber(options_input, 0, ifield);
    classIDflags[ifield] = mxGetClassID(tmp);

    ndim = mxGetNumberOfDimensions(tmp);
    dims = mxGetDimensions(tmp);

    /* Create string/numeric array */
    if (classIDflags[ifield] == mxCHAR_CLASS) {
      if (!strcmp("amg", fnames[ifield])) {
        output_buf = (char *)mxCalloc((size_t)strlen(param->amg) + 1,
                                      (size_t)sizeof(char));
        strcpy(output_buf, param->amg);
        fout = mxCreateString(output_buf);
      } else if (!strcmp("presmoother", fnames[ifield])) {
        output_buf = (char *)mxCalloc((size_t)strlen(param->presmoother) + 1,
                                      (size_t)sizeof(char));
        strcpy(output_buf, param->presmoother);
        fout = mxCreateString(output_buf);
      } else if (!strcmp("postsmoother", fnames[ifield])) {
        output_buf = (char *)mxCalloc((size_t)strlen(param->postsmoother) + 1,
                                      (size_t)sizeof(char));
        strcpy(output_buf, param->postsmoother);
        fout = mxCreateString(output_buf);
      } else if (!strcmp("typecoarse", fnames[ifield])) {
        output_buf = (char *)mxCalloc((size_t)strlen(param->typecoarse) + 1,
                                      (size_t)sizeof(char));
        strcpy(output_buf, param->typecoarse);
        fout = mxCreateString(output_buf);
      } else if (!strcmp("typetv", fnames[ifield])) {
        output_buf = (char *)mxCalloc((size_t)strlen(param->typetv) + 1,
                                      (size_t)sizeof(char));
        strcpy(output_buf, param->typetv);
        fout = mxCreateString(output_buf);
      } else if (!strcmp("FCpart", fnames[ifield])) {
        output_buf = (char *)mxCalloc((size_t)strlen(param->FCpart) + 1,
                                      (size_t)sizeof(char));
        strcpy(output_buf, param->FCpart);
        fout = mxCreateString(output_buf);
      } else if (!strcmp("solver", fnames[ifield])) {
        output_buf = (char *)mxCalloc((size_t)strlen(param->solver) + 1,
                                      (size_t)sizeof(char));
        strcpy(output_buf, param->solver);
        fout = mxCreateString(output_buf);
      } else if (!strcmp("ordering", fnames[ifield])) {
        output_buf = (char *)mxCalloc((size_t)strlen(param->ordering) + 1,
                                      (size_t)sizeof(char));
        strcpy(output_buf, param->ordering);
        fout = mxCreateString(output_buf);
      } else {
        /* Get the length of the input string. */
        buflen = (mxGetM(tmp) * mxGetN(tmp)) + 1;

        /* Allocate memory for input and output strings. */
        input_buf = (char *)mxCalloc((size_t)buflen, (size_t)sizeof(char));
        output_buf = (char *)mxCalloc((size_t)buflen, (size_t)sizeof(char));

        /* Copy the string data from tmp into a C string
           input_buf. */
        status = mxGetString(tmp, input_buf, buflen);

        sizebuf = buflen * sizeof(char);
        memcpy(output_buf, input_buf, sizebuf);
        fout = mxCreateString(output_buf);
      }
    } else {
      fout = mxCreateNumericArray(ndim, dims, classIDflags[ifield], mxREAL);
      pdata = mxGetData(fout);

      sizebuf = mxGetElementSize(tmp);
      if (!strcmp("elbow", fnames[ifield])) {
        dbuf = param->elbow;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("lfilS", fnames[ifield])) {
        dbuf = param->lfilS;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("lfil", fnames[ifield])) {
        dbuf = param->lfil;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("maxit", fnames[ifield])) {
        dbuf = param->maxit;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("droptolS", fnames[ifield])) {
        dbuf = param->droptolS;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("droptolc", fnames[ifield])) {
        dbuf = param->droptolc;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("droptol", fnames[ifield])) {
        dbuf = param->droptol;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("condest", fnames[ifield])) {
        dbuf = param->condest;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("restol", fnames[ifield])) {
        dbuf = param->restol;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("npresmoothing", fnames[ifield])) {
        dbuf = param->npresmoothing;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("npostmoothing", fnames[ifield])) {
        dbuf = param->npostsmoothing;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("ncoarse", fnames[ifield])) {
        dbuf = param->ncoarse;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("matching", fnames[ifield])) {
        dbuf = param->matching;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("nrestart", fnames[ifield])) {
        dbuf = param->nrestart;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("damping", fnames[ifield])) {
        dbuf = param->damping;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("contraction", fnames[ifield])) {
        dbuf = param->contraction;
        memcpy(pdata, &dbuf, sizebuf);
      } else if (!strcmp("mixedprecision", fnames[ifield])) {
        dbuf = param->mixedprecision;
        memcpy(pdata, &dbuf, (size_t)sizebuf);
      } else {
        memcpy(pdata, mxGetData(tmp), sizebuf);
      }
    }

    /* Set each field in output structure */
    mxSetFieldByNumber(options_output, (mwSize)0, ifield, fout);
  }

  mxFree(fnames);
  mxFree(classIDflags);
  /* mexPrintf("params eported\n"); fflush(stdout); */

  plhs[0] = mxCreateStructMatrix((mwSize)1, (mwSize)PRE->nlev, 22, pnames);
  if (plhs[0] == NULL)
    mexErrMsgTxt("Could not create structure mxArray\n");
  PRE_output = plhs[0];

  current = PRE;
  if (PRE->issingle) {
    SPRE = (SAMGlevelmat *)PRE;
    scurrent = SPRE;
  }
  n = A.nr;
  convert =
      (double *)MAlloc((size_t)n * sizeof(double), "DGNLilupackfactor:convert");
  iconvert = (integer *)MAlloc((size_t)n * sizeof(integer),
                               "DGNLilupackfactor:iconvert");
  for (jstruct = 0; jstruct < PRE->nlev; jstruct++) {

    /* mexPrintf("jstruct=%d\n", jstruct); fflush(stdout); */

    /* 1. field `n' */
    ifield = 0;

    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)1, mxREAL);
    pr = mxGetPr(fout);

    if (PRE->issingle)
      *pr = scurrent->n;
    else
      *pr = current->n;

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* 2. field `nB' */
    ++ifield;

    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)1, mxREAL);
    pr = mxGetPr(fout);

    if (PRE->issingle)
      *pr = scurrent->nB;
    else
      *pr = current->nB;

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* 3. field `L' */
    ++ifield;
    /* switched to full-matrix processing */
    if (jstruct == PRE->nlev - 1 &&
        ((PRE->issingle) ? scurrent->LU.ja : current->LU.ja) == NULL) {

      if (PRE->issingle) {
        fout = mxCreateDoubleMatrix((mwSize)scurrent->nB, (mwSize)scurrent->nB,
                                    mxREAL);
        spr = scurrent->LU.a;
      } else {
        fout = mxCreateDoubleMatrix((mwSize)current->nB, (mwSize)current->nB,
                                    mxREAL);
        pr = current->LU.a;
      }
      sr = mxGetPr(fout);

      for (i = 0; i < ((PRE->issingle) ? scurrent->nB : current->nB); i++) {
        /* init strict upper triangular part with zeros */
        for (j = 0; j < i; j++) {
          *sr++ = 0;
        }
        /* diagonal entry set to 1.0 */
        *sr++ = 1.0;

        /* extract diagonal entry from LU decomposition */
        m = i * ((PRE->issingle) ? scurrent->nB : current->nB) + i;
        dbuf = (PRE->issingle) ? (double)spr[m] : pr[m];

        /* extract strict lower triangular part */
        for (j = i + 1; j < ((PRE->issingle) ? scurrent->nB : current->nB);
             j++) {
          m = j * ((PRE->issingle) ? scurrent->nB : current->nB) + i;
          if (PRE->issingle)
            *sr++ = (double)spr[m] / dbuf;
          else
            *sr++ = pr[m] / dbuf;
        } /* end for j */
      }   /* end for i */

      /* set each field in output structure */
      mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);
    } else {
      /* mexPrintf("before=%d\n",
       * (PRE->issingle)?scurrent->LU.ja[scurrent->nB]:current->LU.ja[current->nB]);
       * fflush(stdout); */

      if (PRE->issingle) {
        nnzU = scurrent->LU.ja[scurrent->nB];
        scurrent->LU.ja[scurrent->nB] = scurrent->LU.nnz + 1;
      } else {
        nnzU = current->LU.ja[current->nB];
        current->LU.ja[current->nB] = current->LU.nnz + 1;
      }

      /* mexPrintf("intermediate=%d\n",
       * (PRE->issingle)?scurrent->LU.ja[scurrent->nB]:current->LU.ja[current->nB]);
       * fflush(stdout); */

      if (PRE->issingle) {
        nnz = scurrent->nB;
        for (i = 0; i < scurrent->nB; i++)
          nnz += scurrent->LU.ia[i] - scurrent->LU.ja[i];
      } else {
        nnz = current->nB;
        for (i = 0; i < current->nB; i++)
          nnz += current->LU.ia[i] - current->LU.ja[i];
      }

      if (param->flags & COARSE_REDUCE) {
        if (PRE->issingle)
          fout = mxCreateSparse((mwSize)scurrent->nB, (mwSize)scurrent->nB, nnz,
                                mxREAL);
        else
          fout = mxCreateSparse((mwSize)current->nB, (mwSize)current->nB, nnz,
                                mxREAL);
      } else {
        if (PRE->issingle)
          fout = mxCreateSparse((mwSize)scurrent->n, (mwSize)scurrent->nB, nnz,
                                mxREAL);
        else
          fout = mxCreateSparse((mwSize)current->n, (mwSize)current->nB, nnz,
                                mxREAL);
      }
      /* mexPrintf("number of space requested=%d\n", nnz); fflush(stdout); */

      sr = (double *)mxGetPr(fout);
      irs = (mwIndex *)mxGetIr(fout);
      jcs = (mwIndex *)mxGetJc(fout);

      k = 0;
      cnt = 0;
      if (PRE->issingle) {
        for (i = 0; i < scurrent->nB; i++) {
          /* extract diagonal entry */
          jcs[i] = k;
          irs[k] = i;
          sr[k++] = 1.0 / scurrent->LU.a[i];
          cnt++;

          j = scurrent->LU.ja[i] - 1;
          jj = scurrent->LU.ia[i] - scurrent->LU.ja[i];
          Sqsort(scurrent->LU.a + j, scurrent->LU.ja + j, istack, &jj);

          /* extract strict lower triangular part */
          for (j = scurrent->LU.ja[i] - 1; j < scurrent->LU.ia[i] - 1; j++) {
            irs[k] = scurrent->LU.ja[j] - 1;
            sr[k++] = scurrent->LU.a[j];
            cnt++;
          }
        }
      } else { /* !PRE->issingle */
        for (i = 0; i < current->nB; i++) {
          /* extract diagonal entry */
          jcs[i] = k;
          irs[k] = i;
          sr[k++] = 1.0 / current->LU.a[i];
          cnt++;

          j = current->LU.ja[i] - 1;
          jj = current->LU.ia[i] - current->LU.ja[i];
          Dqsort(current->LU.a + j, current->LU.ja + j, istack, &jj);

          /* extract strict lower triangular part */
          for (j = current->LU.ja[i] - 1; j < current->LU.ia[i] - 1; j++) {
            irs[k] = current->LU.ja[j] - 1;
            sr[k++] = current->LU.a[j];
            cnt++;
          }
        }
      } /* end if-else PRE->issingle */
      jcs[i] = k;

      if (PRE->issingle)
        scurrent->LU.ja[scurrent->nB] = nnzU;
      else
        current->LU.ja[current->nB] = nnzU;

      /* mexPrintf("number of spaces used=%d\n", cnt); fflush(stdout); */
      /* mexPrintf("after=%d\n",
       * (PRE->issingle)?scurrent->LU.ja[scurrent->nB]:current->LU.ja[current->nB]);
       * fflush(stdout); */

      /* set each field in output structure */
      mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);
    }

    /* 4. field `D' */
    ++ifield;
    /* mexPrintf("4. field `D'\n"); fflush(stdout); */
    if (PRE->issingle) {
      fout = mxCreateSparse((mwSize)scurrent->nB, (mwSize)scurrent->nB,
                            (mwSize)scurrent->nB, mxREAL);
      spr = scurrent->LU.a;
    } else {
      fout = mxCreateSparse((mwSize)current->nB, (mwSize)current->nB,
                            (mwSize)current->nB, mxREAL);
      pr = current->LU.a;
    }
    sr = (double *)mxGetPr(fout);
    irs = (mwIndex *)mxGetIr(fout);
    jcs = (mwIndex *)mxGetJc(fout);

    for (i = 0; i < ((PRE->issingle) ? scurrent->nB : current->nB); i++) {
      jcs[i] = i;
      irs[i] = i;
    }
    jcs[i] = i;

    /* switched to full-matrix processing */
    if (jstruct == PRE->nlev - 1 &&
        ((PRE->issingle) ? scurrent->LU.ja : current->LU.ja) == NULL) {

      for (i = 0; i < ((PRE->issingle) ? scurrent->nB : current->nB); i++) {
        /* diagonal entry U(i,i) */
        m = i * ((PRE->issingle) ? scurrent->nB : current->nB) + i;
        dbuf = (PRE->issingle) ? (double)spr[m] : pr[m];
        sr[i] = dbuf;
      } /* end for i */

    } else {
      if (PRE->issingle) {
        for (i = 0; i < scurrent->nB; i++)
          sr[i] = 1.0 / spr[i];
      } else {
        for (i = 0; i < current->nB; i++)
          sr[i] = 1.0 / pr[i];
      }
    }
    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* 5. field `U' */
    ++ifield;
    /* mexPrintf("5. field `U'\n"); fflush(stdout); */
    /* switched to full-matrix processing */
    if (jstruct == PRE->nlev - 1 &&
        ((PRE->issingle) ? scurrent->LU.ja : current->LU.ja) == NULL) {

      if (PRE->issingle) {
        fout = mxCreateDoubleMatrix((mwSize)scurrent->nB, (mwSize)scurrent->nB,
                                    mxREAL);
        spr = scurrent->LU.a;
      } else {
        fout = mxCreateDoubleMatrix((mwSize)current->nB, (mwSize)current->nB,
                                    mxREAL);
        pr = current->LU.a;
      }
      sr = mxGetPr(fout);

      for (i = 0; i < ((PRE->issingle) ? scurrent->nB : current->nB); i++) {
        /* extract strict upper triangular part */
        for (j = 0; j < i; j++) {
          /* U(j,i) */
          m = j * ((PRE->issingle) ? scurrent->nB : current->nB) + i;
          if (PRE->issingle)
            *sr++ = (double)spr[m];
          else
            *sr++ = pr[m];
        } /* end for j */

        /* diagonal entry */
        *sr++ = 1.0;

        /* init strict lower triangular part with zeros */
        for (j = i + 1; j < ((PRE->issingle) ? scurrent->nB : current->nB);
             j++) {
          *sr++ = 0;
        }
      } /* end for i */

      /* set each field in output structure */
      mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);
    } else {
      /* fill-in upper triangular part */
      /* mexPrintf("before=%d\n",
       * (PRE->issingle)?scurrent->LU.ja[scurrent->nB]:current->LU.ja[current->nB]);
       * fflush(stdout); */
      if (PRE->issingle) {
        nnzU = scurrent->LU.ja[scurrent->nB];
        scurrent->LU.ja[scurrent->nB] = scurrent->LU.nnz + 1;

        nnz = scurrent->nB;
        for (i = 0; i < scurrent->nB; i++)
          nnz += scurrent->LU.ja[i + 1] - scurrent->LU.ia[i];

        if (param->flags & COARSE_REDUCE) {
          fout = mxCreateSparse((mwSize)scurrent->nB, (mwSize)scurrent->nB, nnz,
                                mxREAL);
        } else {
          fout = mxCreateSparse((mwSize)scurrent->nB, (mwSize)scurrent->n, nnz,
                                mxREAL);
        }
      } else { /* !PRE->issingle */
        nnzU = current->LU.ja[current->nB];
        current->LU.ja[current->nB] = current->LU.nnz + 1;

        nnz = current->nB;
        for (i = 0; i < current->nB; i++)
          nnz += current->LU.ja[i + 1] - current->LU.ia[i];

        if (param->flags & COARSE_REDUCE) {
          fout = mxCreateSparse((mwSize)current->nB, (mwSize)current->nB, nnz,
                                mxREAL);
        } else {
          fout = mxCreateSparse((mwSize)current->nB, (mwSize)current->n, nnz,
                                mxREAL);
        }
      } /* end if-else PRE->issingle */

      /* mexPrintf("intermediate=%d\n",
       * (PRE->issingle)?scurrent->LU.ja[scurrent->nB]:current->LU.ja[current->nB]);
       * fflush(stdout); */
      /* mexPrintf("number of spaces requested: %d\n",nnz);fflush(stdout); */

      sr = (double *)mxGetPr(fout);
      irs = (mwIndex *)mxGetIr(fout);
      jcs = (mwIndex *)mxGetJc(fout);

      /* each column does have a diagonal entry, shifted by one space */
      jcs[0] = 0;
      for (i = 1; i <= ((PRE->issingle) ? scurrent->nB : current->nB); i++)
        jcs[i] = 1;

      /* number of entries per column, shifted by one space */
      cnt = 0;
      if (PRE->issingle) {
        for (i = 0; i < scurrent->nB; i++) {

          j = scurrent->LU.ia[i] - 1;
          jj = scurrent->LU.ja[i + 1] - scurrent->LU.ia[i];
          Sqsort(scurrent->LU.a + j, scurrent->LU.ja + j, istack, &jj);

          for (j = scurrent->LU.ia[i] - 1; j < scurrent->LU.ja[i + 1] - 1;
               j++) {
            k = scurrent->LU.ja[j];
            jcs[k]++;
          }
        }
      } else { /* !PRE->issingle */
        for (i = 0; i < current->nB; i++) {

          j = current->LU.ia[i] - 1;
          jj = current->LU.ja[i + 1] - current->LU.ia[i];
          Dqsort(current->LU.a + j, current->LU.ja + j, istack, &jj);

          for (j = current->LU.ia[i] - 1; j < current->LU.ja[i + 1] - 1; j++) {
            k = current->LU.ja[j];
            jcs[k]++;
          }
        }
      }

      /* switch to pointer structure */
      if (param->flags & COARSE_REDUCE) {
        for (i = 0; i < ((PRE->issingle) ? scurrent->nB : current->nB); i++)
          jcs[i + 1] += jcs[i];
      } else {
        for (i = 0; i < ((PRE->issingle) ? scurrent->n : current->n); i++)
          jcs[i + 1] += jcs[i];
      }

      for (i = 0; i < ((PRE->issingle) ? scurrent->nB : current->nB); i++) {
        /* extract diagonal entry */
        k = jcs[i];
        irs[k] = i;
        sr[k++] =
            1.0 / ((PRE->issingle) ? scurrent->LU.a[i] : current->LU.a[i]);
        jcs[i] = k;
        cnt++;

        /* extract upper triangular part */
        if (PRE->issingle) {
          for (j = scurrent->LU.ia[i] - 1; j < scurrent->LU.ja[i + 1] - 1;
               j++) {
            l = scurrent->LU.ja[j] - 1;
            k = jcs[l];
            irs[k] = i;
            sr[k++] = scurrent->LU.a[j];
            jcs[l] = k;
            cnt++;
          }
        } else { /* !PRE->issingle */
          for (j = current->LU.ia[i] - 1; j < current->LU.ja[i + 1] - 1; j++) {
            l = current->LU.ja[j] - 1;
            k = jcs[l];
            irs[k] = i;
            sr[k++] = current->LU.a[j];
            jcs[l] = k;
            cnt++;
          }
        } /* end if-else PRE->issingle */
      }
      /* shift pointers by one to the right */
      if (param->flags & COARSE_REDUCE) {
        for (i = ((PRE->issingle) ? scurrent->nB : current->nB); i > 0; i--)
          jcs[i] = jcs[i - 1];
      } else {
        for (i = ((PRE->issingle) ? scurrent->n : current->n); i > 0; i--)
          jcs[i] = jcs[i - 1];
      }
      jcs[0] = 0;

      if (PRE->issingle)
        scurrent->LU.ja[scurrent->nB] = nnzU;
      else
        current->LU.ja[current->nB] = nnzU;

      /* mexPrintf("number of spaces used: %d\n",cnt);fflush(stdout); */
      /* mexPrintf("after=%d\n",
       * (PRE->issingle)?scurrent->LU.ja[scurrent->nB]:current->LU.ja[current->nB]);
       * fflush(stdout); */

      /* set each field in output structure */
      mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);
    }

    /* 6. field `E' */
    ++ifield;
    /* mexPrintf("6. field `E'\n"); fflush(stdout); */
    if (jstruct < PRE->nlev - 1) {

      if (param->flags & COARSE_REDUCE) {
        if (PRE->issingle) {
          nnz = scurrent->E.ia[scurrent->E.nr] - 1;
          fout = mxCreateSparse((mwSize)scurrent->E.nr, (mwSize)scurrent->E.nc,
                                nnz, mxREAL);
        } else {
          nnz = current->E.ia[current->E.nr] - 1;
          fout = mxCreateSparse((mwSize)current->E.nr, (mwSize)current->E.nc,
                                nnz, mxREAL);
        }

        sr = (double *)mxGetPr(fout);
        irs = (mwIndex *)mxGetIr(fout);
        jcs = (mwIndex *)mxGetJc(fout);

        if (PRE->issingle) {
          for (i = 0; i <= scurrent->E.nc; i++)
            jcs[i] = 0;
          /* number of entries per column, shifted by one space */
          for (i = 0; i < scurrent->E.nr; i++) {

            j = scurrent->E.ia[i] - 1;
            jj = scurrent->E.ia[i + 1] - scurrent->E.ia[i];
            Sqsort(scurrent->E.a + j, scurrent->E.ja + j, istack, &jj);

            for (j = scurrent->E.ia[i] - 1; j < scurrent->E.ia[i + 1] - 1;
                 j++) {
              k = scurrent->E.ja[j];
              jcs[k]++;
            }
          }
          /* switch to pointer structure */
          for (i = 0; i < scurrent->E.nc; i++)
            jcs[i + 1] += jcs[i];

          for (i = 0; i < scurrent->E.nr; i++) {
            for (j = scurrent->E.ia[i] - 1; j < scurrent->E.ia[i + 1] - 1;
                 j++) {
              l = scurrent->E.ja[j] - 1;
              k = jcs[l];
              irs[k] = i;
              sr[k++] = scurrent->E.a[j];
              jcs[l] = k;
            }
          }
          /* shift pointers by one to the right */
          for (i = scurrent->E.nc; i > 0; i--)
            jcs[i] = jcs[i - 1];
          jcs[0] = 0;
        } else { /* !PRE->issingle */
          for (i = 0; i <= current->E.nc; i++)
            jcs[i] = 0;
          /* number of entries per column, shifted by one space */
          for (i = 0; i < current->E.nr; i++) {

            j = current->E.ia[i] - 1;
            jj = current->E.ia[i + 1] - current->E.ia[i];
            Dqsort(current->E.a + j, current->E.ja + j, istack, &jj);

            for (j = current->E.ia[i] - 1; j < current->E.ia[i + 1] - 1; j++) {
              k = current->E.ja[j];
              jcs[k]++;
            }
          }
          /* switch to pointer structure */
          for (i = 0; i < current->E.nc; i++)
            jcs[i + 1] += jcs[i];

          for (i = 0; i < current->E.nr; i++) {
            for (j = current->E.ia[i] - 1; j < current->E.ia[i + 1] - 1; j++) {
              l = current->E.ja[j] - 1;
              k = jcs[l];
              irs[k] = i;
              sr[k++] = current->E.a[j];
              jcs[l] = k;
            }
          }
          /* shift pointers by one to the right */
          for (i = current->E.nc; i > 0; i--)
            jcs[i] = jcs[i - 1];
          jcs[0] = 0;
        } /* end if-else PRE->issingle */
      } else {
        fout = mxCreateDoubleMatrix((mwSize)0, (mwSize)0, mxREAL);
      }

      /* set each field in output structure */
      mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);
    }

    /* 7. field `F' */
    ++ifield;
    /* mexPrintf("7. field `F'\n"); fflush(stdout); */
    if (jstruct < PRE->nlev - 1) {

      if (param->flags & COARSE_REDUCE) {
        if (PRE->issingle) {
          nnz = scurrent->F.ia[scurrent->F.nr] - 1;
          fout = mxCreateSparse((mwSize)scurrent->F.nr, (mwSize)scurrent->F.nc,
                                nnz, mxREAL);
        } else {
          nnz = current->F.ia[current->F.nr] - 1;
          fout = mxCreateSparse((mwSize)current->F.nr, (mwSize)current->F.nc,
                                nnz, mxREAL);
        }

        sr = (double *)mxGetPr(fout);
        irs = (mwIndex *)mxGetIr(fout);
        jcs = (mwIndex *)mxGetJc(fout);

        if (PRE->issingle) {
          for (i = 0; i <= scurrent->F.nc; i++)
            jcs[i] = 0;
          /* number of entries per column, shifted by one space */
          for (i = 0; i < scurrent->F.nr; i++) {

            j = scurrent->F.ia[i] - 1;
            jj = scurrent->F.ia[i + 1] - scurrent->F.ia[i];
            Sqsort(scurrent->F.a + j, scurrent->F.ja + j, istack, &jj);

            for (j = scurrent->F.ia[i] - 1; j < scurrent->F.ia[i + 1] - 1;
                 j++) {
              k = scurrent->F.ja[j];
              jcs[k]++;
            }
          }
          /* switch to pointer structure */
          for (i = 0; i < scurrent->F.nc; i++)
            jcs[i + 1] += jcs[i];

          for (i = 0; i < scurrent->F.nr; i++) {
            for (j = scurrent->F.ia[i] - 1; j < scurrent->F.ia[i + 1] - 1;
                 j++) {
              l = scurrent->F.ja[j] - 1;
              k = jcs[l];
              irs[k] = i;
              sr[k++] = scurrent->F.a[j];
              jcs[l] = k;
            }
          }
          /* shift pointers by one to the right */
          for (i = scurrent->F.nc; i > 0; i--)
            jcs[i] = jcs[i - 1];
          jcs[0] = 0;
        } else { /* !PRE->issingle */
          for (i = 0; i <= current->F.nc; i++)
            jcs[i] = 0;
          /* number of entries per column, shifted by one space */
          for (i = 0; i < current->F.nr; i++) {

            j = current->F.ia[i] - 1;
            jj = current->F.ia[i + 1] - current->F.ia[i];
            Dqsort(current->F.a + j, current->F.ja + j, istack, &jj);

            for (j = current->F.ia[i] - 1; j < current->F.ia[i + 1] - 1; j++) {
              k = current->F.ja[j];
              jcs[k]++;
            }
          }
          /* switch to pointer structure */
          for (i = 0; i < current->F.nc; i++)
            jcs[i + 1] += jcs[i];

          for (i = 0; i < current->F.nr; i++) {
            for (j = current->F.ia[i] - 1; j < current->F.ia[i + 1] - 1; j++) {
              l = current->F.ja[j] - 1;
              k = jcs[l];
              irs[k] = i;
              sr[k++] = current->F.a[j];
              jcs[l] = k;
            }
          }
          /* shift pointers by one to the right */
          for (i = current->F.nc; i > 0; i--)
            jcs[i] = jcs[i - 1];
          jcs[0] = 0;
        } /* end if-else PRE->issingle */
      } else {
        fout = mxCreateDoubleMatrix((mwSize)0, (mwSize)0, mxREAL);
      }
      /* set each field in output structure */
      mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);
    }

    /* 8. field `rowscal' */
    ++ifield;
    /* mexPrintf("8. field `rowscal'\n"); fflush(stdout); */

    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)n, mxREAL);

    if (PRE->issingle) {
      pr = mxGetPr(fout);
      for (i = 0; i < n; i++)
        pr[i] = (double)scurrent->rowscal[i];
    } else {
      pdata = mxGetData(fout);
      memcpy(pdata, current->rowscal, (size_t)n * sizeof(double));
    }

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* 9. field `colscal' */
    ++ifield;
    /* mexPrintf("9. field `colscal'\n"); fflush(stdout); */

    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)n, mxREAL);

    if (PRE->issingle) {
      pr = mxGetPr(fout);
      for (i = 0; i < n; i++)
        pr[i] = (double)scurrent->colscal[i];
    } else {
      pdata = mxGetData(fout);
      memcpy(pdata, current->colscal, (size_t)n * sizeof(double));
    }

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* 10. field `p' */
    ++ifield;
    /* mexPrintf("10. field `p'\n"); fflush(stdout); */

    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)n, mxREAL);
    pdata = mxGetData(fout);

    if (PRE->issingle) {
      for (i = 0; i < n; i++)
        convert[i] = scurrent->p[i];
    } else {
      for (i = 0; i < n; i++)
        convert[i] = current->p[i];
    }

    memcpy(pdata, convert, (size_t)n * sizeof(double));

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* 11. field `invq' */
    ++ifield;
    /* mexPrintf("11. field `invq'\n"); fflush(stdout); */

    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)n, mxREAL);
    pdata = mxGetData(fout);

    /* switched to full-matrix processing, adapt permutation */
    if (jstruct == PRE->nlev - 1 &&
        ((PRE->issingle) ? scurrent->LU.ja : current->LU.ja) == NULL) {

      if (PRE->issingle) {
        for (i = 0; i < n; i++)
          iconvert[i] = scurrent->invq[i];
      } else {
        for (i = 0; i < n; i++)
          iconvert[i] = current->invq[i];
      }

      i = 0;
      while (i < n) {
        j = ((PRE->issingle) ? scurrent->LU.ia[i] : current->LU.ia[i]);
        if (j != i + 1) {
          k = iconvert[i];
          iconvert[i] = iconvert[j - 1];
          iconvert[j - 1] = k;
        }
        i++;
      }
      for (i = 0; i < n; i++)
        convert[iconvert[i] - 1] = i + 1;
    } else {
      if (PRE->issingle) {
        for (i = 0; i < n; i++)
          convert[i] = scurrent->invq[i];
      } else {
        for (i = 0; i < n; i++)
          convert[i] = current->invq[i];
      }
    }

    memcpy(pdata, convert, (size_t)n * sizeof(double));

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* 12. field `param' */
    ++ifield;
    /* mexPrintf("12. field `param'\n"); fflush(stdout); */

    fout = mxCreateNumericArray((mwSize)1, mydims, mxUINT64_CLASS, mxREAL);
    pdata = mxGetData(fout);

    memcpy(pdata, &param, (size_t)sizeof(size_t));

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* 13. field `ptr' */
    ++ifield;
    /* mexPrintf("13. field `ptr'\n"); fflush(stdout); */

    fout = mxCreateNumericArray((size_t)1, mydims, mxUINT64_CLASS, mxREAL);
    pdata = mxGetData(fout);

    memcpy(pdata, &PRE, (size_t)sizeof(size_t));

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* save real property to field `isreal' */
    ++ifield;

    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)1, mxREAL);
    pr = mxGetPr(fout);

    *pr = 1;

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* save non-positive definite property to field `isdefinite' */
    ++ifield;

    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)1, mxREAL);
    pr = mxGetPr(fout);

    *pr = 0;

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* save non-symmetry property to field `issymmetric' */
    ++ifield;

    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)1, mxREAL);
    pr = mxGetPr(fout);

    *pr = 0;

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* save non-Hermitian property to field `ishermitian' */
    ++ifield;

    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)1, mxREAL);
    pr = mxGetPr(fout);

    *pr = 0;

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* 18. save precision property to field `issingle' */
    ++ifield;
    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)1, mxREAL);
    pr = mxGetPr(fout);

    if (PRE->issingle)
      *pr = scurrent->issingle;
    else
      *pr = current->issingle;

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, (mwIndex)jstruct, ifield, fout);

    /* 19. save coarse grid system `A_H' */
    ++ifield;
    if (jstruct >= PRE->nlev - 1) {
      fout = mxCreateSparse((mwSize)0, (mwSize)0, (mwSize)0, mxREAL);
    } else if (param->ipar[16] & DISCARD_MATRIX) {
      if (PRE->issingle)
        fout = mxCreateSparse((mwSize)n - scurrent->nB,
                              (mwSize)n - scurrent->nB, (mwSize)0, mxREAL);
      else
        fout = mxCreateSparse((mwSize)n - current->nB, (mwSize)n - current->nB,
                              (mwSize)0, mxREAL);
    } else {
      /* switched to full-matrix processing */
      if (jstruct == PRE->nlev - 2 &&
          ((PRE->issingle) ? scurrent->next->LU.ja : current->next->LU.ja) ==
              NULL)
        fout = mxCreateSparse((mwSize)0, (mwSize)0, (mwSize)0, mxREAL);
      else {
        if (PRE->issingle) {
          nnz = scurrent->next->A.ia[scurrent->next->A.nr] - 1;
          fout = mxCreateSparse((mwSize)scurrent->next->A.nr,
                                (mwSize)scurrent->next->A.nc, nnz, mxREAL);
        } else {
          nnz = current->next->A.ia[current->next->A.nr] - 1;
          fout = mxCreateSparse((mwSize)current->next->A.nr,
                                (mwSize)current->next->A.nc, nnz, mxREAL);
        }

        sr = (double *)mxGetPr(fout);
        irs = (mwIndex *)mxGetIr(fout);
        jcs = (mwIndex *)mxGetJc(fout);

        k = 0;
        if (PRE->issingle) {
          for (i = 0; i < scurrent->next->A.nr; i++) {
            jcs[i] = k;

            j = scurrent->next->A.ia[i] - 1;
            jj = scurrent->next->A.ia[i + 1] - scurrent->next->A.ia[i];
            Sqsort(scurrent->next->A.a + j, scurrent->next->A.ja + j, istack,
                   &jj);

            for (j = scurrent->next->A.ia[i] - 1;
                 j < scurrent->next->A.ia[i + 1] - 1; j++) {
              irs[k] = scurrent->next->A.ja[j] - 1;
              sr[k++] = scurrent->next->A.a[j];
            }
          }
        } else { /* !PRE->issingle */
          for (i = 0; i < current->next->A.nr; i++) {
            jcs[i] = k;

            j = current->next->A.ia[i] - 1;
            jj = current->next->A.ia[i + 1] - current->next->A.ia[i];
            Dqsort(current->next->A.a + j, current->next->A.ja + j, istack,
                   &jj);

            for (j = current->next->A.ia[i] - 1;
                 j < current->next->A.ia[i + 1] - 1; j++) {
              irs[k] = current->next->A.ja[j] - 1;
              sr[k++] = current->next->A.a[j];
            }
          }
        } /* end if-else PRE->issingle */
        jcs[i] = k;
      }
    }
    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, jstruct, ifield, fout);

    /* 20. save error in L to field `errorL' */
    ++ifield;
    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)1, mxREAL);
    pr = mxGetPr(fout);
    if (PRE->issingle)
      *pr = scurrent->errorL;
    else
      *pr = current->errorL;
    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, (mwIndex)jstruct, ifield, fout);

    /* 21. save error in U to field `errorU' */
    ++ifield;
    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)1, mxREAL);
    pr = mxGetPr(fout);
    if (PRE->issingle)
      *pr = scurrent->errorU;
    else
      *pr = current->errorU;

    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, (mwIndex)jstruct, ifield, fout);

    /* 22. save error in S to field `errorS' */
    ++ifield;
    fout = mxCreateDoubleMatrix((mwSize)1, (mwSize)1, mxREAL);
    pr = mxGetPr(fout);
    if (PRE->issingle)
      *pr = scurrent->errorS;
    else
      *pr = current->errorS;
    /* set each field in output structure */
    mxSetFieldByNumber(PRE_output, (mwIndex)jstruct, ifield, fout);

    if (PRE->issingle) {
      n -= scurrent->nB;
      scurrent = scurrent->next;
    } else {
      n -= current->nB;
      current = current->next;
    }
  }

  free(A.ia);
  free(A.ja);
  free(A.a);

  free(ibuff);
  free(istack);
  free(convert);
  free(iconvert);

  return;
}
