/*
 * acc.c
 *  <FILE_DESC>
 * 	
 *  Author: Min Si
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mpi.h>

#define NUM_OPS 5
#define TOTAL_NUM_OPS (NUM_OPS * 2)
#define CHECK
#define OUTPUT_FAIL_DETAIL

double *winbuf = NULL;
double *locbuf = NULL;
double *result = NULL;
int rank, nprocs;
MPI_Win win = MPI_WIN_NULL;
int ITER = 5;

static void reset_win()
{
    int i;
    for (i = 0; i < nprocs; i++) {
        winbuf[i] = 0.0;
    }
}

static void change_data(int nop, int x)
{
    int i;
    for (i = 0; i < nop; i++) {
        locbuf[i] = 1.0 * (x + 1) * (i + 1);
    }
}

static void print_buffers(int nop)
{
    int i;
    fprintf(stderr, "[%d] locbuf:\n");
    for (i = 0; i < nop; i++) {
        fprintf(stderr, "%.1lf ", locbuf[i]);
    }
    fprintf(stderr, "\n");
    for (i = 0; i < nprocs; i++) {
        fprintf(stderr, "winbuf[%d]: %.1lf\n", i, winbuf[i]);
    }
}

/* Test self communication */
static int run_test1(int nop)
{
    int i, x, errs = 0, errs_total = 0;
    MPI_Status stat;
    int dst;
    double sum = 0.0;

    dst = 0;
    fprintf(stdout, "[%d]-----check self: lock(%d)/ %d * [fetch_and_op(sum) + flush]/unlock \n",
            rank, dst, nop);

    for (x = 0; x < ITER; x++) {
        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, dst, 0, win);
        change_data(nop, x);

        for (i = 0; i < nop; i++) {
            MPI_Fetch_and_op(&locbuf[i], result, MPI_DOUBLE, dst, rank, MPI_SUM, win);
            MPI_Win_flush(dst, win);

            if (result[0] != sum) {
                fprintf(stderr, "[%d] iter %d, op %d, result %.1lf != sum %.1lf \n", rank,
                        x, i, result[0], sum);
                errs++;
            }

            sum += locbuf[i];
        }
        MPI_Win_unlock(dst, win);
    }

    /* need barrier before checking local window buffer */
    MPI_Barrier(MPI_COMM_WORLD);

    if (errs > 0) {
        fprintf(stderr, "[%d] checking failed\n", rank);
#ifdef OUTPUT_FAIL_DETAIL
        print_buffers(nop);
#endif
    }

    MPI_Allreduce(&errs, &errs_total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    return errs_total;
}

/* Test neighbor communication */
static int run_test2(int nop)
{
    int i, x, errs = 0, errs_total = 0;
    MPI_Status stat;
    int dst;
    double sum = 0.0;

    dst = (rank + 1) % nprocs;
    fprintf(stdout, "[%d]-----check neighbor: lock(%d)/%d * [fetch_and_op(sum) + flush]/unlock \n",
            rank, dst, nop);

    for (x = 0; x < ITER; x++) {
        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, dst, 0, win);
        change_data(nop, x);

        for (i = 0; i < nop; i++) {
            MPI_Fetch_and_op(&locbuf[i], result, MPI_DOUBLE, dst, rank, MPI_SUM, win);
            MPI_Win_flush(dst, win);

            if (result[0] != sum) {
                fprintf(stderr, "[%d] iter %d, op %d, result %.1lf != sum %.1lf \n", rank,
                        x, i, result[0], sum);
                errs++;
            }

            sum += locbuf[i];
        }
        MPI_Win_unlock(dst, win);
    }

    /* need barrier before checking local window buffer */
    MPI_Barrier(MPI_COMM_WORLD);

    if (errs > 0) {
        fprintf(stderr, "[%d] checking failed\n", rank);
#ifdef OUTPUT_FAIL_DETAIL
        print_buffers(nop);
#endif
    }

    MPI_Allreduce(&errs, &errs_total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    return errs_total;
}

/* Test all-to-all communication */
static int run_test3(int nop)
{
    int i, x, errs = 0, errs_total = 0;
    MPI_Status stat;
    int dst;
    double sum = 0.0;

    fprintf(stdout, "[%d]-----check all2all: lockall/%d * [fetch_and_op(sum) + "
            "flushall]/unlockall \n", rank, nop);

    for (x = 0; x < ITER; x++) {
        MPI_Win_lock_all(0, win);
        change_data(nop, x);

        for (i = 0; i < nop; i++) {
            for (dst = 0; dst < nprocs; dst++) {
                MPI_Fetch_and_op(&locbuf[i], &result[dst], MPI_DOUBLE, dst, rank, MPI_SUM, win);
            }
            MPI_Win_flush_all(win);

            for (dst = 0; dst < nprocs; dst++) {
                if (result[dst] != sum) {
                    fprintf(stderr, "[%d] iter %d, op %d, dst %d, result %.1lf != sum %.1lf \n",
                            rank, x, i, dst, result[dst], sum);
                    errs++;
                }
            }
            sum += locbuf[i];
        }
        MPI_Win_unlock_all(win);
    }

    /* need barrier before checking local window buffer */
    MPI_Barrier(MPI_COMM_WORLD);

    if (errs > 0) {
        fprintf(stderr, "[%d] checking failed\n", rank);
#ifdef OUTPUT_FAIL_DETAIL
        print_buffers(nop);
#endif
    }

    MPI_Allreduce(&errs, &errs_total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    return errs_total;
}

/* Test all-to-all communication with multiple operations per flush */
static int run_test4(int nop)
{
    int i, x, errs = 0, errs_total = 0;
    MPI_Status stat;
    int dst;
    double sum = 0.0, max = 0.0;

    fprintf(stdout, "[%d]-----check all2all: lockall/%d * acc(max) + "
            "fetch_and_op(sum) + flushall + get/unlockall \n", rank, nop);

    for (x = 0; x < ITER; x++) {
        MPI_Win_lock_all(0, win);
        change_data(nop, x);

        for (dst = 0; dst < nprocs; dst++) {
            for (i = 0; i < nop; i++) {
                MPI_Accumulate(&locbuf[i], 1, MPI_DOUBLE, dst, rank, 1, MPI_DOUBLE, MPI_MAX, win);
            }

            MPI_Fetch_and_op(&locbuf[nop - 1], &result[dst], MPI_DOUBLE, dst, rank, MPI_SUM, win);
        }
        MPI_Win_flush_all(win);

        max = (sum > locbuf[nop - 1]) ? sum : locbuf[nop - 1];
        sum = max + locbuf[nop - 1];

        for (dst = 0; dst < nprocs; dst++) {
            if (result[dst] != max) {
                fprintf(stderr, "[%d] iter %d, dst %d, result %.1lf != max %.1lf \n",
                        rank, x, dst, result[dst], max);
                errs++;
            }
        }

        for (dst = 0; dst < nprocs; dst++) {
            MPI_Get(&result[dst], 1, MPI_DOUBLE, dst, rank, 1, MPI_DOUBLE, win);
        }
        MPI_Win_unlock_all(win);

        for (dst = 0; dst < nprocs; dst++) {
            if (result[dst] != sum) {
                fprintf(stderr, "[%d] iter %d, dst %d, result %.1lf != sum %.1lf \n",
                        rank, x, dst, result[dst], sum);
                errs++;
            }
        }

        if (errs > 0) {
#ifdef OUTPUT_FAIL_DETAIL
            print_buffers(nop);
#endif
        }
    }

    /* need barrier before checking local window buffer */
    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Allreduce(&errs, &errs_total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    return errs_total;
}

int main(int argc, char *argv[])
{
    int size = NUM_OPS;
    int i, errs = 0;
    MPI_Init(&argc, &argv);

    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (nprocs < 2) {
        fprintf(stderr, "Please run using at least 2 processes\n");
        goto exit;
    }

    locbuf = calloc(NUM_OPS, sizeof(double));
    result = calloc(nprocs, sizeof(double));
    for (i = 0; i < NUM_OPS; i++) {
        locbuf[i] = 0;
    }

    /* size in byte */
    MPI_Win_allocate(sizeof(double) * nprocs, sizeof(double), MPI_INFO_NULL,
                     MPI_COMM_WORLD, &winbuf, &win);

    reset_win();
    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test1(size);
    if (errs)
        goto exit;

    reset_win();
    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test2(size);
    if (errs)
        goto exit;

    reset_win();
    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test3(size);
    if (errs)
        goto exit;

    reset_win();
    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test4(size);
    if (errs)
        goto exit;

  exit:

    if (rank == 0) {
        fprintf(stdout, "%d errors\n", errs);
    }

    if (win != MPI_WIN_NULL)
        MPI_Win_free(&win);
    if (locbuf)
        free(locbuf);

    MPI_Finalize();

    return 0;
}
