#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#include "src/common/slurm_xlator.h"

/* general return codes */
//#define SLURM_SUCCESS   0
//#define SLURM_ERROR    -1
//#define SLURM_FAILURE  -1

#define PMIXP_COLL_RING_CTX_NUM 3

typedef void pmixp_proc_t;

#define PMIXP_COLL_DEBUG 1
#define PMIXP_COLL_RING_CTX_NUM 3

typedef enum {
    PMIXP_MSG_NONE,
    PMIXP_MSG_FAN_IN,
    PMIXP_MSG_FAN_OUT,
    PMIXP_MSG_DMDX,
    PMIXP_MSG_INIT_DIRECT,
#ifndef NDEBUG
    PMIXP_MSG_PINGPONG,
#endif
    PMIXP_MSG_RING
} pmixp_srv_cmd_t;

typedef enum {
    PMIXP_COLL_TYPE_FENCE_TREE = 0,
    PMIXP_COLL_TYPE_FENCE_RING,
    /* reserve coll fence ids up to 15 */
    PMIXP_COLL_TYPE_FENCE_MAX = 15,

    PMIXP_COLL_TYPE_CONNECT,
    PMIXP_COLL_TYPE_DISCONNECT
} pmixp_coll_type_t;

/* PMIx Ring collective */
typedef enum {
    PMIXP_COLL_RING_SYNC,
    PMIXP_COLL_RING_PROGRESS,
    PMIXP_COLL_RING_FINALIZE,
} pmixp_ring_state_t;

struct pmixp_coll_s;

typedef struct {
    /* ptr to coll data */
    struct pmixp_coll_s *coll;

    /* context data */
    bool in_use;
    uint32_t seq;
    bool contrib_local;
    uint32_t contrib_prev;
    uint32_t forward_cnt;
    bool *contrib_map;
    pmixp_ring_state_t state;
    Buf ring_buf;
} pmixp_coll_ring_ctx_t;

/* coll ring struct */
typedef struct {
    /* next node id */
    int next_peerid;
    /* coll contexts data */
    pmixp_coll_ring_ctx_t ctx_array[PMIXP_COLL_RING_CTX_NUM];
    /* buffer pool to ensure parallel sends of ring data */
    List fwrd_buf_pool;
    List ring_buf_pool;
} pmixp_coll_ring_t;

/* General coll struct */
typedef struct pmixp_coll_s {
//#ifndef NDEBUG
//#define PMIXP_COLL_STATE_MAGIC 0xC011CAFE
//    int magic;
//#endif
    /* element-wise lock */
//    pthread_mutex_t lock;

    /* collective state */
    uint32_t seq;

    /* general information */
    pmixp_coll_type_t type;

    /* PMIx collective id */
    /*struct {
        pmixp_proc_t *procs;
        size_t nprocs;
    } pset;*/
    int my_peerid;
    int peers_cnt;
//#ifdef PMIXP_COLL_DEBUG
//    hostlist_t peers_hl;
//#endif

    /* libpmix callback data */
    void *cbfunc;
    void *cbdata;

    /* timestamp for stale collectives detection */
    time_t ts, ts_next;

    /* coll states */
    union {
        //pmixp_coll_tree_t tree;
        pmixp_coll_ring_t ring;
    } state;
} pmixp_coll_t;

typedef void* pmixp_p2p_ctx_t;
typedef void (*pmixp_server_sent_cb_t)(int rc, pmixp_p2p_ctx_t ctx,
                       void *cb_data);

typedef struct {
    uint32_t type;
    uint32_t contrib_id;
    uint32_t seq;
    uint32_t hop_seq;
    uint32_t nodeid;
    size_t msgsize;
} pmixp_coll_ring_msg_hdr_t;

typedef enum {
    /* use non as to check non-init case */
    PMIXP_EP_NONE = 0,
    PMIXP_EP_HLIST,
    PMIXP_EP_NOIDEID
} pmixp_ep_type_t;

typedef struct {
    pmixp_ep_type_t type;
    union {
        char *hostlist;
        int nodeid;
    } ep;
} pmixp_ep_t;

typedef struct {
    pmixp_coll_t *coll;
    pmixp_coll_ring_ctx_t *coll_ctx;
    Buf buf;
    uint32_t seq;
} pmixp_coll_ring_cbdata_t;

typedef struct {
    Buf buf;
    pmixp_server_sent_cb_t complete_cb;
    void *cb_data;
} msg_t;

