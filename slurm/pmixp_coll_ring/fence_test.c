#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pmix.h>
//#include "test/test_common.h"

static void release_cb(pmix_status_t status, void *cbdata)
{
    int *ptr = (int*)cbdata;
    *ptr = 0;
}

#define FENCE(blocking, data_ex, pcs, nprocs) do {                              \
    if( blocking ){                                                             \
        pmix_info_t *info = NULL;                                               \
        size_t ninfo = 0;                                                       \
        if (data_ex) {                                                          \
            bool value = 1;                                                     \
            PMIX_INFO_CREATE(info, 1);                                          \
            (void)strncpy(info->key, PMIX_COLLECT_DATA, PMIX_MAX_KEYLEN);       \
            pmix_value_load(&info->value, &value, PMIX_BOOL);                   \
            ninfo = 1;                                                          \
        }                                                                       \
        rc = PMIx_Fence(pcs, nprocs, info, ninfo);                              \
        PMIX_INFO_FREE(info, ninfo);                                            \
    } else {                                                                    \
        int in_progress = 1, count;                                             \
        rc = PMIx_Fence_nb(pcs, nprocs, NULL, 0, release_cb, &in_progress);     \
        if ( PMIX_SUCCESS == rc ) {                                             \
            count = 0;                                                          \
            while( in_progress ){                                               \
                struct timespec ts;                                             \
                ts.tv_sec = 0;                                                  \
                ts.tv_nsec = 100;                                               \
                nanosleep(&ts,NULL);                                            \
                count++;                                                        \
            }                                                                   \
            fprintf(stderr, "PMIx_Fence_nb(barrier,collect): free time: %lfs",  \
                    count*100*1E-9);                                            \
        }                                                                       \
    }                                                                           \
    if (PMIX_SUCCESS == rc) {                                                   \
        fprintf(stderr, "%s:%d: Fence successfully completed",                  \
            myproc.nspace, myproc.rank);                                        \
    }                                                                           \
} while (0)

int main() {
    pmix_proc_t myproc, proc;
    int rc;
    pmix_value_t value;
    pmix_value_t *val = &value;
    size_t nprocs;
    pmix_proc_t *procs;
    int i;

    if (PMIX_SUCCESS != (rc = PMIx_Init(&myproc, NULL, 0))) {
        fprintf(stderr, "PMIx_Init failed: %d\n", rc);
        exit(1);
    }
    fprintf(stderr, "PMIx_Init  %s:%d\n", myproc.nspace, myproc.rank);

    (void)strncpy(proc.nspace, myproc.nspace, PMIX_MAX_NSLEN);
    proc.rank = PMIX_RANK_WILDCARD;

    if (PMIX_SUCCESS != (rc = PMIx_Get(&proc, PMIX_UNIV_SIZE, NULL, 0, &val))) {
        fprintf(stderr, "rank %d: PMIx_Get universe size failed: %d", myproc.rank, rc);
        exit(1);
    }
    nprocs = val->data.uint32;

    PMIX_PROC_CREATE(procs, nprocs);
    for (i = 0; i < nprocs; i++) {
        procs[i].rank = i;
        strncpy(procs[i].nspace, myproc.nspace, PMIX_MAX_NSLEN);
    }

    FENCE(0, 1, procs, nprocs);
    if (PMIX_SUCCESS != rc) {
        fprintf(stderr, "%s:%d: PMIx_Fence failed: %d", myproc.nspace, myproc.rank, rc);
        exit(1);
    }
    return 0;
}
