#include <stdio.h>
#include <stdlib.h>
#include "mpiasp.h"

int MPI_Finalize(void) {
    static const char FCNAME[] = "MPI_Finalize";
    int mpi_errno = MPI_SUCCESS;
    int rank, nprocs, local_rank, local_nprocs;

    MPIASP_DBG_PRINT_FCNAME();

    // ASP does not need user process information because it is a global call
    MPIASP_Func_start(MPIASP_FUNC_FINALIZE, 0, 0, MPIASP_COMM_USER_LOCAL);

    if (MPIASP_COMM_USER_WORLD) {
        MPIASP_DBG_PRINT(" free MPIASP_COMM_USER_WORLD\n");
        PMPI_Comm_free(&MPIASP_COMM_USER_WORLD);
    }

    if (MPIASP_COMM_LOCAL) {
        MPIASP_DBG_PRINT(" free MPIASP_COMM_LOCAL\n");
        PMPI_Comm_free(&MPIASP_COMM_LOCAL);
    }

    if (MPIASP_COMM_USER_LOCAL) {
        MPIASP_DBG_PRINT(" free MPIASP_COMM_USER_LOCAL\n");
        PMPI_Comm_free(&MPIASP_COMM_USER_LOCAL);
    }

    if (MPIASP_COMM_USER_ROOTS) {
        MPIASP_DBG_PRINT(" free MPIASP_COMM_USER_ROOTS\n");
        PMPI_Comm_free(&MPIASP_COMM_USER_ROOTS);
    }

    if (MPIASP_ALL_NODE_IDS)
        free(MPIASP_ALL_NODE_IDS);

    if (MPIASP_ALL_ASP_IN_COMM_WORLD)
        free(MPIASP_ALL_ASP_IN_COMM_WORLD);

    mpi_errno = PMPI_Finalize();
    if (mpi_errno != MPI_SUCCESS)
        goto fn_fail;

    mpi_errno = table_remove_all(0, NULL);
    if (mpi_errno != 0)
        goto fn_fail;
    table_destroy();

    fn_exit:
    return mpi_errno;

    fn_fail:
    goto fn_exit;
}