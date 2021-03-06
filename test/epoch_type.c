/*
 * no_conflict_epoch.c
 *  <FILE_DESC>
 *
 *  Author: Min Si
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <mpi.h>

#define NUM_OPS 5
#define CHECK
#define OUTPUT_FAIL_DETAIL

double *winbuf = NULL;
double *locbuf = NULL;
double *checkbuf = NULL;
int rank, nprocs;
MPI_Win win = MPI_WIN_NULL;
int ITER = 2;

static void change_data(int nop, int x)
{
    int dst, i;
    for (dst = 0; dst < nprocs; dst++) {
        for (i = 0; i < nop; i++) {
            locbuf[dst * nop + i] = 1.0 * (x + 1) * (i + 1) + nop * dst;
        }
    }
}

static int check_data_all(int nop, int x)
{
    int errs = 0;
    /* note that it is in an epoch */
    int dst, i;

    memset(checkbuf, 0, NUM_OPS * nprocs * sizeof(double));

    for (dst = 0; dst < nprocs; dst++) {
        MPI_Get(&checkbuf[dst * nop], nop, MPI_DOUBLE, dst, 0, nop, MPI_DOUBLE, win);
    }
    MPI_Win_flush_all(win);

    for (dst = 0; dst < nprocs; dst++) {
        for (i = 0; i < nop; i++) {
            if (checkbuf[dst * nop + i] != locbuf[dst * nop + i]) {
                fprintf(stderr, "[%d] winbuf[%d] %.1lf != %.1lf\n", dst, i,
                        checkbuf[dst * nop + i], locbuf[dst * nop + i]);
                errs++;
            }
        }
    }

#ifdef OUTPUT_FAIL_DETAIL
    if (errs > 0) {
        fprintf(stderr, "[%d] locbuf:\n", rank);
        for (i = 0; i < nop * nprocs; i++) {
            fprintf(stderr, "%.1lf ", locbuf[i]);
        }
        fprintf(stderr, "\n");

        fprintf(stderr, "[%d] winbuf:\n", rank);
        for (i = 0; i < nop * nprocs; i++) {
            fprintf(stderr, "%.1lf ", checkbuf[i]);
        }
        fprintf(stderr, "\n");
    }
#endif

    return errs;
}

int check_data(int nop, int x, int dst)
{
    int errs = 0;
    /* note that it is in an epoch */
    int i;

    memset(checkbuf, 0, NUM_OPS * nprocs * sizeof(double));

    MPI_Get(&checkbuf[dst * nop], nop, MPI_DOUBLE, dst, 0, nop, MPI_DOUBLE, win);
    MPI_Win_flush(dst, win);

    for (i = 0; i < nop; i++) {
        if (checkbuf[dst * nop + i] != locbuf[dst * nop + i]) {
            fprintf(stderr, "[%d] %d winbuf[%d] %.1lf != %.1lf\n", dst, x, i,
                    checkbuf[dst * nop + i], locbuf[dst * nop + i]);
            errs++;
        }
    }
#ifdef OUTPUT_FAIL_DETAIL
    if (errs > 0) {
        fprintf(stderr, "[%d] %d locbuf:\n", rank, x);
        for (i = 0; i < nop; i++) {
            fprintf(stderr, "%.1lf ", locbuf[dst * nop + i]);
        }
        fprintf(stderr, "\n");

        fprintf(stderr, "[%d] %d winbuf:\n", rank, x);
        for (i = 0; i < nop; i++) {
            fprintf(stderr, "%.1lf ", checkbuf[dst * nop + i]);
        }
        fprintf(stderr, "\n");
    }
#endif
    return errs;
}

static int run_test1(int nop)
{
    int i, x, errs = 0;
    int dst;
    MPI_Info win_info = MPI_INFO_NULL;

    if (rank == 0)
        fprintf(stdout, "[%d]-----check win_allocate(lockall)\n", rank);

    MPI_Info_create(&win_info);
    MPI_Info_set(win_info, (char *) "epoch_type", (char *) "lockall");

    /* size in byte */
    MPI_Win_allocate(sizeof(double) * NUM_OPS, sizeof(double), win_info,
                     MPI_COMM_WORLD, &winbuf, &win);
    for (i = 0; i < NUM_OPS; i++) {
        winbuf[i] = 0.0;
    }

    MPI_Win_lock_all(0, win);
    if (rank == 0) {
        for (x = 0; x < ITER; x++) {

            /* change date in every interation */
            change_data(nop, x);

            for (dst = 0; dst < nprocs; dst++) {
                MPI_Put(&locbuf[dst * nop], 1, MPI_DOUBLE, dst, 0, 1, MPI_DOUBLE, win);
            }
            MPI_Win_flush_all(win);

            for (dst = 0; dst < nprocs; dst++) {
                for (i = 1; i < nop; i++) {
                    MPI_Put(&locbuf[dst * nop + i], 1, MPI_DOUBLE, dst, i, 1, MPI_DOUBLE, win);
#ifdef MVA
                    MPI_Win_flush(dst, win);    /* use it to poke progress in order to finish local CQEs */
#endif
                }
            }
            MPI_Win_flush_all(win);

            /* check in every iteration */
            errs += check_data_all(nop, x);
        }
    }
    MPI_Win_unlock_all(win);

    MPI_Bcast(&errs, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (win != MPI_WIN_NULL)
        MPI_Win_free(&win);
    if (win_info != MPI_INFO_NULL)
        MPI_Info_free(&win_info);
    return errs;
}

static int run_test2(int nop)
{
    int i, x, errs = 0, errs_total = 0;
    int dst;
    MPI_Info win_info = MPI_INFO_NULL;

    if (rank == 0)
        fprintf(stdout, "[%d]-----check win_allocate(lockall|lock)\n", rank);

    MPI_Info_create(&win_info);
    MPI_Info_set(win_info, (char *) "epoch_type", (char *) "lock|lockall");
    MPI_Win_allocate(sizeof(double) * NUM_OPS, sizeof(double), win_info,
                     MPI_COMM_WORLD, &winbuf, &win);
    for (i = 0; i < NUM_OPS; i++) {
        winbuf[i] = 0.0;
    }

    /* odd ranks send to even ranks */
    if (rank % 2 == 0) {
        dst = (rank + 1) % nprocs;
        for (x = 0; x < ITER; x++) {
            change_data(nop, x);
            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, dst, 0, win);

            /* enable load balancing */
            MPI_Accumulate(&locbuf[dst * nop], 1, MPI_DOUBLE, dst, 0, 1, MPI_DOUBLE, MPI_MAX, win);
            MPI_Win_flush(dst, win);

            change_data(nop, x + ITER);

            for (i = 0; i < nop; i++) {
                MPI_Accumulate(&locbuf[dst * nop + i], 1, MPI_DOUBLE, dst, i, 1, MPI_DOUBLE,
                               MPI_MAX, win);
            }
            MPI_Win_flush(dst, win);

            errs += check_data(nop, x, dst);

            MPI_Win_unlock(dst, win);
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Allreduce(&errs, &errs_total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    if (win != MPI_WIN_NULL)
        MPI_Win_free(&win);
    if (win_info != MPI_INFO_NULL)
        MPI_Info_free(&win_info);
    return errs_total;
}

static int run_test3(int nop)
{
    int i, j, x, errs = 0, errs_total = 0;
    MPI_Status stat;
    int dst, org;
    double sum = 0.0;
    MPI_Group post_group = MPI_GROUP_NULL;
    MPI_Group start_group = MPI_GROUP_NULL;
    MPI_Group world_group = MPI_GROUP_NULL;
    int source = 0;
    double max_result = 0.0;
    double sum_result = 0.0;
    MPI_Info win_info = MPI_INFO_NULL;

    if (rank == 0)
        fprintf(stdout, "[%d]-----check win_allocate(lockall|pscw)\n", rank);

    MPI_Info_create(&win_info);
    MPI_Info_set(win_info, (char *) "epoch_type", (char *) "lockall|pscw");
    MPI_Win_allocate(sizeof(double) * 2, sizeof(double), win_info, MPI_COMM_WORLD, &winbuf, &win);
    for (i = 0; i < 2; i++) {
        winbuf[i] = 0.0;
    }

    MPI_Comm_group(MPI_COMM_WORLD, &world_group);

    if (rank % 2) {
        source = 1;
        dst = (rank + 1) % nprocs;
        MPI_Group_incl(world_group, 1, &dst, &start_group);
    }
    else {
        source = 0;
        org = (rank + nprocs - 1) % nprocs;
        MPI_Group_incl(world_group, 1, &org, &post_group);
    }

    for (x = 0; x < ITER; x++) {
        /* change date */
        for (i = 0; i < NUM_OPS * nprocs; i++) {
            locbuf[i] = x + i;
        }

        sum_result = 0.0;
        for (i = 0; i < nop; i++) {
            sum_result += locbuf[nop * rank + i];
        }
        max_result = locbuf[nop * rank + nop - 1];

        if (source) {
            MPI_Win_start(start_group, 0, win);

            for (i = 0; i < nop; i++) {
                MPI_Accumulate(&locbuf[i + dst * nop], 1, MPI_DOUBLE, dst, 0, 1,
                               MPI_DOUBLE, MPI_SUM, win);
                MPI_Accumulate(&locbuf[i + dst * nop], 1, MPI_DOUBLE, dst, 1, 1,
                               MPI_DOUBLE, MPI_MAX, win);
            }

            MPI_Win_complete(win);
            MPI_Barrier(MPI_COMM_WORLD);        /* will not start next epoch before target finish its check */
        }
        else {
            MPI_Win_post(post_group, 0, win);
            MPI_Win_wait(win);

            /* source may start next start before this lock */

            /* check result */
            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, rank, 0, win);
            if (winbuf[0] != sum_result) {
                fprintf(stderr, "[%d]winbuf[%d] %.1lf != %.1lf, iter %d\n", rank, 0,
                        winbuf[0], sum_result, x);
                errs++;
            }
            if (winbuf[1] != max_result) {
                fprintf(stderr, "[%d]winbuf[%d] %.1lf != %.1lf, iter %d\n", rank, 1, winbuf[1],
                        max_result, x);
                errs++;
            }

            if (errs > 0) {
                fprintf(stderr, "winbuf: %.1lf %.1lf\n", winbuf[0], winbuf[1]);
            }

            /* reset my window */
            for (i = 0; i < 2; i++) {
                winbuf[i] = 0.0;
            }
            MPI_Win_unlock(rank, win);

            MPI_Barrier(MPI_COMM_WORLD);
        }
    }

    MPI_Allreduce(&errs, &errs_total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    if (post_group != MPI_GROUP_NULL)
        MPI_Group_free(&post_group);
    if (start_group != MPI_GROUP_NULL)
        MPI_Group_free(&start_group);
    if (world_group != MPI_GROUP_NULL)
        MPI_Group_free(&world_group);

    if (win != MPI_WIN_NULL)
        MPI_Win_free(&win);
    if (win_info != MPI_INFO_NULL)
        MPI_Info_free(&win_info);

    return errs_total;
}

static int run_test4(int nop)
{
    int i, x, errs = 0, errs_total = 0;
    int dst;
    MPI_Info win_info = MPI_INFO_NULL;

    if (rank == 0)
        fprintf(stdout, "[%d]-----check win_allocate(fence)\n", rank);

    MPI_Info_create(&win_info);
    MPI_Info_set(win_info, (char *) "epoch_type", (char *) "fence");
    MPI_Win_allocate(sizeof(double) * NUM_OPS, sizeof(double), win_info, MPI_COMM_WORLD, &winbuf,
                     &win);
    for (i = 0; i < NUM_OPS; i++) {
        winbuf[i] = 0.0;
    }

    for (x = 0; x < ITER; x++) {
        change_data(nop, x);

        MPI_Win_fence(0, win);
        for (dst = 0; dst < nprocs; dst++) {
            for (i = 0; i < nop; i++) {
                MPI_Accumulate(&locbuf[i + dst * nop], 1, MPI_DOUBLE, dst, i, 1,
                               MPI_DOUBLE, MPI_SUM, win);
            }
        }
        MPI_Win_fence(0, win);

        /* check in every iteration */
        for (i = 0; i < nop; i++) {
            if (winbuf[i] != locbuf[i + rank * nop] * nprocs) {
                fprintf(stderr, "[%d] winbuf[%d] %.1lf != %.1lf (%.1f*%d)\n",
                        rank, i, winbuf[i], locbuf[i + rank * nop] * nprocs,
                        locbuf[i + rank * nop], nprocs);
                errs++;
            }
        }

        for (i = 0; i < NUM_OPS; i++) {
            winbuf[i] = 0.0;
        }
        MPI_Win_fence(0, win);
    }

    if (errs > 0) {
        fprintf(stderr, "[%d] checking failed\n", rank);
#ifdef OUTPUT_FAIL_DETAIL
        fprintf(stderr, "[%d] locbuf:\n");
        for (i = 0; i < nop * nprocs; i++) {
            fprintf(stderr, "%.1lf ", locbuf[i]);
        }
        fprintf(stderr, "\n");
#endif
    }

    MPI_Allreduce(&errs, &errs_total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    if (win != MPI_WIN_NULL)
        MPI_Win_free(&win);
    if (win_info != MPI_INFO_NULL)
        MPI_Info_free(&win_info);

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

    locbuf = calloc(NUM_OPS * nprocs, sizeof(double));
    checkbuf = calloc(NUM_OPS * nprocs, sizeof(double));
    for (i = 0; i < NUM_OPS * nprocs; i++) {
        locbuf[i] = 1.0 * i;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test1(size);
    if (errs)
        goto exit;

    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test2(size);
    if (errs)
        goto exit;

    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test3(size);
    if (errs)
        goto exit;

    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test4(size);
    if (errs)
        goto exit;

  exit:

    if (rank == 0) {
        fprintf(stdout, "%d errors\n", errs);
    }
    if (locbuf)
        free(locbuf);
    if (checkbuf)
        free(checkbuf);

    MPI_Finalize();

    return 0;
}
