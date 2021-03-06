#include "MILUsolve.h"
#include "m2c.h"

static void crs_Axpy_kernel(const emxArray_int32_T *row_ptr, const
  emxArray_int32_T *col_ind, const emxArray_real_T *val, const emxArray_real_T
  *x, emxArray_real_T *y, int nrows);
static void m2c_error(void);
static void solve_milu(const emxArray_struct0_T *M, int lvl, emxArray_real_T *b,
  int offset, emxArray_real_T *b_y1, emxArray_real_T *y2);
static void crs_Axpy_kernel(const emxArray_int32_T *row_ptr, const
  emxArray_int32_T *col_ind, const emxArray_real_T *val, const emxArray_real_T
  *x, emxArray_real_T *y, int nrows)
{
  int i;
  double t;
  int j;
  for (i = 0; i + 1 <= nrows; i++) {
    t = y->data[i];
    for (j = row_ptr->data[i]; j < row_ptr->data[i + 1]; j++) {
      t += val->data[j - 1] * x->data[col_ind->data[j - 1] - 1];
    }

    y->data[i] = t;
  }
}

static void m2c_error(void)
{
  const char * msgid;
  const char * fmt;
  msgid = "crs_Axpy:BufferTooSmal";
  fmt = "Buffer space for output y is too small.";
  M2C_error(msgid, fmt);
}

static void solve_milu(const emxArray_struct0_T *M, int lvl, emxArray_real_T *b,
  int offset, emxArray_real_T *b_y1, emxArray_real_T *y2)
{
  int nB;
  int n;
  int i;
  int b_n;
  int k;
  int j;
  nB = M->data[lvl - 1].L.nrows;
  n = M->data[lvl - 1].L.nrows + M->data[lvl - 1].negE.nrows;
  for (i = 0; i + 1 <= nB; i++) {
    b_y1->data[i] = M->data[lvl - 1].rowscal->data[M->data[lvl - 1].p->data[i] -
      1] * b->data[(M->data[lvl - 1].p->data[i] + offset) - 1];
  }

  for (i = M->data[lvl - 1].L.nrows; i + 1 <= n; i++) {
    y2->data[i - nB] = M->data[lvl - 1].rowscal->data[M->data[lvl - 1].p->data[i]
      - 1] * b->data[(M->data[lvl - 1].p->data[i] + offset) - 1];
  }

  if (n > M->data[lvl - 1].L.nrows) {
    for (i = 0; i + 1 <= nB; i++) {
      b->data[offset + i] = b_y1->data[i];
    }
  }

  if ((M->data[lvl - 1].L.val->size[0] == 0) && (M->data[lvl - 1].U.val->size[0]
       == n * n)) {
    k = 0;
    for (j = 1; j <= nB; j++) {
      k += j;
      for (i = j; i + 1 <= nB; i++) {
        b_y1->data[i] -= M->data[lvl - 1].U.val->data[k] * b_y1->data[j - 1];
        k++;
      }
    }

    k = M->data[lvl - 1].L.nrows * M->data[lvl - 1].L.nrows - 1;
    for (j = M->data[lvl - 1].L.nrows - 1; j + 1 > 0; j--) {
      b_y1->data[j] /= M->data[lvl - 1].U.val->data[k];
      for (i = j; i > 0; i--) {
        k--;
        b_y1->data[i - 1] -= M->data[lvl - 1].U.val->data[k] * b_y1->data[j];
      }

      k = ((k - nB) + j) - 1;
    }
  } else {
    b_n = M->data[lvl - 1].L.col_ptr->size[0] - 1;
    for (j = 1; j <= b_n; j++) {
      for (k = M->data[lvl - 1].L.col_ptr->data[j - 1] - 1; k + 1 < M->data[lvl
           - 1].L.col_ptr->data[j]; k++) {
        b_y1->data[M->data[lvl - 1].L.row_ind->data[k] - 1] -= M->data[lvl - 1].
          L.val->data[k] * b_y1->data[j - 1];
      }
    }

    for (i = 0; i + 1 <= nB; i++) {
      b_y1->data[i] /= M->data[lvl - 1].d->data[i];
    }

    for (j = M->data[lvl - 1].U.col_ptr->size[0] - 1; j > 0; j--) {
      for (k = M->data[lvl - 1].U.col_ptr->data[j - 1] - 1; k + 1 < M->data[lvl
           - 1].U.col_ptr->data[j]; k++) {
        b_y1->data[M->data[lvl - 1].U.row_ind->data[k] - 1] -= M->data[lvl - 1].
          U.val->data[k] * b_y1->data[j - 1];
      }
    }
  }

  if (n > M->data[lvl - 1].L.nrows) {
    if (y2->size[0] < M->data[lvl - 1].negE.nrows) {
      m2c_error();
    }

    crs_Axpy_kernel(M->data[lvl - 1].negE.row_ptr, M->data[lvl - 1].negE.col_ind,
                    M->data[lvl - 1].negE.val, b_y1, y2, M->data[lvl - 1].
                    negE.nrows);
    b_n = n - M->data[lvl - 1].L.nrows;
    for (i = 0; i + 1 <= b_n; i++) {
      b->data[(offset + nB) + i] = y2->data[i];
    }

    solve_milu(M, lvl + 1, b, offset + M->data[lvl - 1].L.nrows, b_y1, y2);
    for (i = 0; i + 1 <= nB; i++) {
      b_y1->data[i] = b->data[offset + i];
    }

    b_n = n - M->data[lvl - 1].L.nrows;
    for (i = 0; i + 1 <= b_n; i++) {
      y2->data[i] = b->data[(offset + nB) + i];
    }

    if (b_y1->size[0] < M->data[lvl - 1].negF.nrows) {
      m2c_error();
    }

    crs_Axpy_kernel(M->data[lvl - 1].negF.row_ptr, M->data[lvl - 1].negF.col_ind,
                    M->data[lvl - 1].negF.val, y2, b_y1, M->data[lvl - 1].
                    negF.nrows);
    b_n = M->data[lvl - 1].L.col_ptr->size[0] - 1;
    for (j = 1; j <= b_n; j++) {
      for (k = M->data[lvl - 1].L.col_ptr->data[j - 1] - 1; k + 1 < M->data[lvl
           - 1].L.col_ptr->data[j]; k++) {
        b_y1->data[M->data[lvl - 1].L.row_ind->data[k] - 1] -= M->data[lvl - 1].
          L.val->data[k] * b_y1->data[j - 1];
      }
    }

    for (i = 0; i + 1 <= nB; i++) {
      b_y1->data[i] /= M->data[lvl - 1].d->data[i];
    }

    for (j = M->data[lvl - 1].U.col_ptr->size[0] - 1; j > 0; j--) {
      for (k = M->data[lvl - 1].U.col_ptr->data[j - 1] - 1; k + 1 < M->data[lvl
           - 1].U.col_ptr->data[j]; k++) {
        b_y1->data[M->data[lvl - 1].U.row_ind->data[k] - 1] -= M->data[lvl - 1].
          U.val->data[k] * b_y1->data[j - 1];
      }
    }
  }

  for (i = 0; i + 1 <= nB; i++) {
    b->data[(M->data[lvl - 1].q->data[i] + offset) - 1] = b_y1->data[i] *
      M->data[lvl - 1].colscal->data[M->data[lvl - 1].q->data[i] - 1];
  }

  for (i = M->data[lvl - 1].L.nrows; i + 1 <= n; i++) {
    b->data[(M->data[lvl - 1].q->data[i] + offset) - 1] = y2->data[i - nB] *
      M->data[lvl - 1].colscal->data[M->data[lvl - 1].q->data[i] - 1];
  }
}

void MILUsolve(const emxArray_struct0_T *M, emxArray_real_T *b, emxArray_real_T *
               b_y1, emxArray_real_T *y2)
{
  solve_milu(M, 1, b, 0, b_y1, y2);
}

void MILUsolve_2args(const emxArray_struct0_T *M, emxArray_real_T *b)
{
  int u0;
  int u1;
  emxArray_real_T *b_y1;
  int i0;
  emxArray_real_T *y2;
  u0 = M->data[0].L.nrows;
  u1 = M->data[0].negE.nrows;
  if (u0 > u1) {
    u1 = u0;
  }

  emxInit_real_T(&b_y1, 1);
  i0 = b_y1->size[0];
  b_y1->size[0] = u1;
  emxEnsureCapacity((emxArray__common *)b_y1, i0, sizeof(double));
  for (i0 = 0; i0 < u1; i0++) {
    b_y1->data[i0] = 0.0;
  }

  emxInit_real_T(&y2, 1);
  i0 = y2->size[0];
  y2->size[0] = M->data[0].negE.nrows;
  emxEnsureCapacity((emxArray__common *)y2, i0, sizeof(double));
  u0 = M->data[0].negE.nrows;
  for (i0 = 0; i0 < u0; i0++) {
    y2->data[i0] = 0.0;
  }

  solve_milu(M, 1, b, 0, b_y1, y2);
  emxFree_real_T(&y2);
  emxFree_real_T(&b_y1);
}

void MILUsolve_initialize(void)
{
}

void MILUsolve_terminate(void)
{
}
