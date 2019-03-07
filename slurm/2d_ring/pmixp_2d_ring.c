#include <stdio.h>
#include "pmixp_2d_ring.h"

#define PMIXP_DEBUG(format, args...)                                    \
{                                                                       \
    fprintf(stderr, "<%d>:%s:%d: " format"\n",                            \
        coll->my_peerid, __func__, __LINE__, ## args);                  \
}

#define PMIXP_ERROR(format, args...)                                    \
{                                                                       \
    fprintf(stderr, "[-%d-]:%s:%d: ERROR:" format"\n",                      \
        coll->my_peerid, __func__, __LINE__, ## args);                  \
}

typedef struct {
    uint32_t id;
    pmixp_coll_t coll;
    List in_buf;
} client_info_t;

client_info_t *clients;

int pmixp_coll_ring_init(pmixp_coll_t *coll, hostlist_t *hl);
//void pmixp_coll_ring_free(pmixp_coll_ring_t *coll_ring);
//int pmixp_coll_ring_check(pmixp_coll_t  *coll, pmixp_coll_ring_msg_hdr_t *hdr);
int pmixp_coll_ring_local(pmixp_coll_t  *coll, char *data, size_t size,
              void *cbfunc, void *cbdata);
//int pmixp_coll_ring_neighbor(pmixp_coll_t *coll, pmixp_coll_ring_msg_hdr_t *hdr,
//                 Buf buf);

static void pmixp_free_buf(void *x)
{
    Buf buf = (Buf)x;
    FREE_NULL_BUFFER(buf);
}

int pmixp_coll_ring_init(pmixp_coll_t *coll, hostlist_t *hl)
{
#ifdef PMIXP_COLL_DEBUG
    PMIXP_DEBUG("called");
#endif
    int i;
    pmixp_coll_ring_ctx_t *coll_ctx = NULL;
    pmixp_coll_ring_t *ring = &coll->state.ring;
    char *p;
    int rel_id = coll->my_peerid;// = hostlist_find(*hl, pmixp_info_hostname());

    /* compute the next absolute id of the neighbor */
    //p = hostlist_nth(*hl, (rel_id + 1) % coll->peers_cnt);
    ring->next_peerid = (rel_id + 1) % coll->peers_cnt;// = pmixp_info_job_hostid(p);
    //free(p);

    ring->fwrd_buf_pool = list_create(pmixp_free_buf);
    ring->ring_buf_pool = list_create(pmixp_free_buf);

    for (i = 0; i < PMIXP_COLL_RING_CTX_NUM; i++) {
        coll_ctx = &ring->ctx_array[i];
        coll_ctx->coll = coll;
        coll_ctx->in_use = false;
        coll_ctx->seq = coll->seq;
        coll_ctx->contrib_local = false;
        coll_ctx->contrib_prev = 0;
        coll_ctx->state = PMIXP_COLL_RING_SYNC;
        // TODO bit vector
        coll_ctx->contrib_map = malloc(sizeof(bool) * coll->peers_cnt);
    }

    return SLURM_SUCCESS;
}

static inline int _ring_prev_id(pmixp_coll_t *coll)
{
    return (coll->my_peerid + coll->peers_cnt - 1) % coll->peers_cnt;
}

static inline int _ring_next_id(pmixp_coll_t *coll)
{
    return (coll->my_peerid + 1) % coll->peers_cnt;
}

static inline pmixp_coll_t *_ctx_get_coll(pmixp_coll_ring_ctx_t *coll_ctx)
{
    return coll_ctx->coll;
}

static inline pmixp_coll_ring_t *_ctx_get_coll_ring(
    pmixp_coll_ring_ctx_t *coll_ctx)
{
    return &coll_ctx->coll->state.ring;
}

static inline uint32_t _ring_remain_contrib(pmixp_coll_ring_ctx_t *coll_ctx)
{
    return coll_ctx->coll->peers_cnt -
        (coll_ctx->contrib_prev + coll_ctx->contrib_local);
}

static inline uint32_t _ring_fwd_done(pmixp_coll_ring_ctx_t *coll_ctx)
{
    return !(coll_ctx->coll->peers_cnt - coll_ctx->forward_cnt - 1);
}

static Buf _get_contrib_buf(pmixp_coll_ring_ctx_t *coll_ctx)
{
    pmixp_coll_ring_t *ring = _ctx_get_coll_ring(coll_ctx);
    Buf ring_buf = list_pop(ring->ring_buf_pool);
    if (!ring_buf) {
        ring_buf = create_buf(NULL, 0);
    }
    return ring_buf;
}

static Buf _get_fwd_buf(pmixp_coll_ring_ctx_t *coll_ctx)
{
    pmixp_coll_ring_t *ring = _ctx_get_coll_ring(coll_ctx);
    Buf buf = list_pop(ring->fwrd_buf_pool);
    if (!buf) {
        buf = create_buf(NULL, 0);
    }
    return buf;
}

static void _reset_coll_ring(pmixp_coll_ring_ctx_t *coll_ctx)
{
    pmixp_coll_t *coll = _ctx_get_coll(coll_ctx);
#ifdef PMIXP_COLL_DEBUG
    PMIXP_DEBUG("%p: called", coll_ctx);
#endif
    //pmixp_coll_ring_ctx_sanity_check(coll_ctx);
    coll_ctx->in_use = false;
    coll_ctx->state = PMIXP_COLL_RING_SYNC;
    coll_ctx->contrib_local = false;
    coll_ctx->contrib_prev = 0;
    coll_ctx->forward_cnt = 0;
    coll->ts = time(NULL);
    memset(coll_ctx->contrib_map, 0, sizeof(bool) * coll->peers_cnt);
    coll_ctx->ring_buf = NULL;
}

static void _progress_coll_ring(pmixp_coll_ring_ctx_t *coll_ctx)
{
    int ret = 0;
    pmixp_coll_t *coll = _ctx_get_coll(coll_ctx);

    //pmixp_coll_ring_ctx_sanity_check(coll_ctx);

    PMIXP_DEBUG("called");

    do {
        ret = false;
        switch(coll_ctx->state) {
        case PMIXP_COLL_RING_SYNC:
            if (coll_ctx->contrib_local || coll_ctx->contrib_prev) {
                coll_ctx->state = PMIXP_COLL_RING_PROGRESS;
                ret = true;
            }
            break;
        case PMIXP_COLL_RING_PROGRESS:
            /* check for all data is collected and forwarded */
            if (!_ring_remain_contrib(coll_ctx) ) {
                coll_ctx->state = PMIXP_COLL_RING_FINALIZE;
                //_invoke_callback(coll_ctx);
                PMIXP_DEBUG("lib_modex_invoke, buf size %lu", get_buf_offset(coll_ctx->ring_buf));
                {
                    char *ptr = get_buf_data(coll_ctx->ring_buf);
                    printf("%s",ptr);
                }
                ret = true;
            }
            break;
        case PMIXP_COLL_RING_FINALIZE:
            if(_ring_fwd_done(coll_ctx)) {
#ifdef PMIXP_COLL_DEBUG
                PMIXP_DEBUG("%p: %s seq=%d is DONE", coll,
                        "RING",
                        coll_ctx->seq);
#endif
                /* increase coll sequence */
                coll->seq++;
                _reset_coll_ring(coll_ctx);
                ret = true;

            }
            break;
        default:
            PMIXP_ERROR("%p: unknown state = %d",
                    coll_ctx, (int)coll_ctx->state);
        }
    } while(ret);
}

int pmixp_server_send_nb(pmixp_ep_t *ep, pmixp_srv_cmd_t type,
             uint32_t seq, Buf buf,
             pmixp_server_sent_cb_t complete_cb,
             void *cb_data)
{
    pmixp_coll_t *coll = &clients[ep->ep.nodeid].coll;
    Buf send_buf = create_buf(xmalloc(sizeof(msg_t)), sizeof(msg_t));
    msg_t *msg = (msg_t*)get_buf_data(send_buf);

    msg->buf = create_buf(NULL, 0);
    grow_buf(msg->buf, get_buf_offset(buf));
    memcpy(get_buf_data(msg->buf), get_buf_data(buf), get_buf_offset(buf));
    msg->cb_data = cb_data;
    msg->complete_cb = complete_cb;
    PMIXP_DEBUG("send msg to %d size %u", ep->ep.nodeid, size_buf(msg->buf));
    list_enqueue(clients[ep->ep.nodeid].in_buf, send_buf);

    return SLURM_SUCCESS;
}

static int _pack_coll_ring_info(pmixp_coll_t *coll,
                pmixp_coll_ring_msg_hdr_t *ring_hdr,
                Buf buf)
{
    //pmixp_proc_t *procs = coll->pset.procs;
    //size_t nprocs = coll->pset.nprocs;
    uint32_t type = PMIXP_COLL_TYPE_FENCE_RING;
    int i;

    /* 1. store the type of collective */
    pack32(type, buf);
#if 0
    /* 2. Put the number of ranges */
    pack32(nprocs, buf);
    for (i = 0; i < (int)nprocs; i++) {
        /* Pack namespace */
        packmem(procs->nspace, strlen(procs->nspace) + 1, buf);
        pack32(procs->rank, buf);
    }
#endif

    /* 3. pack the ring header info */
    packmem((char*)ring_hdr, sizeof(pmixp_coll_ring_msg_hdr_t), buf);

    return SLURM_SUCCESS;
}

int pmixp_coll_ring_unpack(Buf buf, pmixp_coll_type_t *type,
               pmixp_coll_ring_msg_hdr_t *ring_hdr,
               pmixp_proc_t **r, size_t *nr)
{
    //pmixp_proc_t *procs = NULL;
    //uint32_t nprocs = 0;
    uint32_t tmp;
    int rc, i;

    /* 1. extract the type of collective */
    if (SLURM_SUCCESS != (rc = unpack32(&tmp, buf))) {
        //PMIXP_ERROR("Cannot unpack collective type");
        fprintf(stderr, "Cannot unpack collective type\n");
        return rc;
    }
    *type = tmp;
#if 0
    /* 2. get the number of ranges */
    if (SLURM_SUCCESS != (rc = unpack32(&nprocs, buf))) {
        PMIXP_ERROR("Cannot unpack collective type");
        return rc;
    }
    *nr = nprocs;

    procs = xmalloc(sizeof(pmixp_proc_t) * nprocs);
    *r = procs;

    /* 3. get namespace/rank of particular process */
    for (i = 0; i < (int)nprocs; i++) {
        rc = unpackmem(procs[i].nspace, &tmp, buf);
        if (SLURM_SUCCESS != rc) {
            PMIXP_ERROR("Cannot unpack namespace for process #%d",
                    i);
            return rc;
        }
        procs[i].nspace[tmp] = '\0';

        rc = unpack32(&tmp, buf);
        procs[i].rank = tmp;
        if (SLURM_SUCCESS != rc) {
            PMIXP_ERROR("Cannot unpack ranks for process #%d, nsp=%s",
                    i, procs[i].nspace);
            return rc;
        }
    }
#endif
    /* 4. extract the ring info */
    if (SLURM_SUCCESS != (rc = unpackmem((char *)ring_hdr, &tmp, buf))) {
        //PMIXP_ERROR("Cannot unpack ring info");
        fprintf(stderr, "Cannot unpack ring info\n");
        return rc;
    }

    return SLURM_SUCCESS;
}

static inline void pmixp_server_buf_reserve(Buf buf, uint32_t size)
{
    if (remaining_buf(buf) < size) {
        uint32_t to_reserve = size - remaining_buf(buf);
        grow_buf(buf, to_reserve);
    }
}

static void _ring_sent_cb(int rc, pmixp_p2p_ctx_t ctx, void *_cbdata)
{
    pmixp_coll_ring_cbdata_t *cbdata = (pmixp_coll_ring_cbdata_t*)_cbdata;
    pmixp_coll_ring_ctx_t *coll_ctx = cbdata->coll_ctx;
    pmixp_coll_t *coll = cbdata->coll;
    Buf buf = cbdata->buf;

    //pmixp_coll_sanity_check(coll);

    //if (PMIXP_P2P_REGULAR == ctx) {
        /* lock the collective */
    //    slurm_mutex_lock(&coll->lock);
    //}
#ifdef PMIXP_COLL_DEBUG
    PMIXP_DEBUG("%p: called %d", coll_ctx, coll_ctx->seq);
#endif
    if (cbdata->seq != coll_ctx->seq) {
        /* it seems like this collective was reset since the time
         * we initiated this send.
         * Just exit to avoid data corruption.
         */
        PMIXP_DEBUG("%p: collective was reset!", coll_ctx);
        goto exit;
    }
    coll_ctx->forward_cnt++;
    _progress_coll_ring(coll_ctx);

exit:
    //pmixp_server_buf_reset(buf);
    set_buf_offset(buf, 0);
    list_push(coll->state.ring.fwrd_buf_pool, buf);

    //if (PMIXP_P2P_REGULAR == ctx) {
        /* unlock the collective */
    //    slurm_mutex_unlock(&coll->lock);
    //}
    xfree(cbdata);
}

static int _ring_forward_data(pmixp_coll_ring_ctx_t *coll_ctx, uint32_t contrib_id,
                  uint32_t hop_seq, void *data, size_t size)
{
    pmixp_coll_ring_msg_hdr_t hdr;
    pmixp_coll_t *coll = _ctx_get_coll(coll_ctx);
    pmixp_coll_ring_t *ring = &coll->state.ring;
    hdr.nodeid = coll->my_peerid;
    hdr.msgsize = size;
    hdr.seq = coll_ctx->seq;
    hdr.hop_seq = hop_seq;
    hdr.contrib_id = contrib_id;
    pmixp_ep_t *ep = (pmixp_ep_t*)xmalloc(sizeof(*ep));
    pmixp_coll_ring_cbdata_t *cbdata = NULL;
    uint32_t offset = 0;
    Buf buf = _get_fwd_buf(coll_ctx);
    int rc = SLURM_SUCCESS;

    //pmixp_coll_ring_ctx_sanity_check(coll_ctx);

#ifdef PMIXP_COLL_DEBUG
    PMIXP_DEBUG("%p: transit data to nodeid=%d, seq=%d, hop=%d, size=%lu, contrib=%d",
            coll_ctx, _ring_next_id(coll), hdr.seq,
            hdr.hop_seq, hdr.msgsize, hdr.contrib_id);
#endif
    if (!buf) {
        rc = SLURM_ERROR;
        goto exit;
    }
    ep->type = PMIXP_EP_NOIDEID;
    ep->ep.nodeid = ring->next_peerid;

    /* pack ring info */
    _pack_coll_ring_info(coll, &hdr, buf);

    /* insert payload to buf */
    offset = get_buf_offset(buf);
    pmixp_server_buf_reserve(buf, size);
    memcpy(get_buf_data(buf) + offset, data, size);
    set_buf_offset(buf, offset + size);

    cbdata = xmalloc(sizeof(pmixp_coll_ring_cbdata_t));
    cbdata->buf = buf;
    cbdata->coll = coll;
    cbdata->coll_ctx = coll_ctx;
    cbdata->seq = coll_ctx->seq;
    rc = pmixp_server_send_nb(ep, PMIXP_MSG_RING, coll_ctx->seq, buf,
                  _ring_sent_cb, cbdata);
exit:
    return rc;
}

inline static int _pmixp_coll_contrib(pmixp_coll_ring_ctx_t *coll_ctx,
                                      int contrib_id,
                                      uint32_t hop, char *data, size_t size)
{
    pmixp_coll_t *coll = _ctx_get_coll(coll_ctx);
    char *data_ptr = NULL;
    int ret;

    /* change the state */
    coll->ts = time(NULL);

    /* save contribution */
    if (!size_buf(coll_ctx->ring_buf)) {
        grow_buf(coll_ctx->ring_buf, size * coll->peers_cnt);
    } else if(remaining_buf(coll_ctx->ring_buf) < size) {
        uint32_t new_size = size_buf(coll_ctx->ring_buf) + size *
            _ring_remain_contrib(coll_ctx);
        grow_buf(coll_ctx->ring_buf, new_size);
    }
    grow_buf(coll_ctx->ring_buf, size);
    data_ptr = get_buf_data(coll_ctx->ring_buf) +
        get_buf_offset(coll_ctx->ring_buf);
    memcpy(data_ptr, data, size);
    set_buf_offset(coll_ctx->ring_buf,
               get_buf_offset(coll_ctx->ring_buf) + size);

    /* check for ring is complete */
    if (contrib_id != _ring_next_id(coll)) {
        /* forward data to the next node */
        ret = _ring_forward_data(coll_ctx, contrib_id, hop,
                     data_ptr, size);
        if (ret) {
            PMIXP_ERROR("Cannot forward ring data");
            return SLURM_ERROR;
        }
    }

    return SLURM_SUCCESS;
}

pmixp_coll_ring_ctx_t *pmixp_coll_ring_ctx_new(pmixp_coll_t *coll)
{
    int i;
    pmixp_coll_ring_ctx_t *coll_ctx = NULL, *ret_ctx = NULL,
        *free_ctx = NULL;
    pmixp_coll_ring_t *ring = &coll->state.ring;
    uint32_t seq = coll->seq;

    for (i = 0; i < PMIXP_COLL_RING_CTX_NUM; i++) {
        coll_ctx = &ring->ctx_array[i];
        /*
         * check that no active context exists to exclude the double
         * context
         */
        if (coll_ctx->in_use) {
            switch(coll_ctx->state) {
            case PMIXP_COLL_RING_FINALIZE:
                seq++;
                break;
            case PMIXP_COLL_RING_SYNC:
            case PMIXP_COLL_RING_PROGRESS:
                if (!ret_ctx && !coll_ctx->contrib_local) {
                    ret_ctx = coll_ctx;
                }
                break;
            }
        } else {
            free_ctx = coll_ctx;
            xassert(!free_ctx->in_use);
        }
    }
    /* add this context to use */
    if (!ret_ctx && free_ctx) {
        ret_ctx = free_ctx;
        ret_ctx->in_use = true;
        ret_ctx->seq = seq;
        ret_ctx->ring_buf = _get_contrib_buf(ret_ctx);
    }
    return ret_ctx;
}

/* contrib local */
int pmixp_coll_ring_local(pmixp_coll_t *coll, char *data, size_t size,
              void *cbfunc, void *cbdata)
{
    int ret = SLURM_SUCCESS;
    pmixp_coll_ring_ctx_t *coll_ctx = NULL;

    /* lock the structure */
    //slurm_mutex_lock(&coll->lock);

    /* sanity check */
    //pmixp_coll_sanity_check(coll);

    /* setup callback info */
    coll->cbfunc = cbfunc;
    coll->cbdata = cbdata;

    coll_ctx = pmixp_coll_ring_ctx_new(coll);
    if (!coll_ctx) {
        PMIXP_ERROR("Can not get new ring collective context, seq=%u",
                coll->seq);
        ret = SLURM_ERROR;
        goto exit;
    }

#ifdef PMIXP_COLL_DEBUG
    PMIXP_DEBUG("%p: contrib/loc: seqnum=%u, state=%d, size=%lu",
            coll_ctx, coll_ctx->seq, coll_ctx->state, size);
#endif

    if (_pmixp_coll_contrib(coll_ctx, coll->my_peerid, 0, data, size)) {
        goto exit;
    }

    /* mark local contribution */
    coll_ctx->contrib_local = true;
    _progress_coll_ring(coll_ctx);

exit:
    /* unlock the structure */
    //slurm_mutex_unlock(&coll->lock);

    return ret;
}

pmixp_coll_ring_ctx_t *pmixp_coll_ring_ctx_select(pmixp_coll_t *coll,
                          const uint32_t seq)
{
    int i;
    pmixp_coll_ring_ctx_t *coll_ctx = NULL, *ret = NULL;
    pmixp_coll_ring_t *ring = &coll->state.ring;

    /* finding the appropriate ring context */
    for (i = 0; i < PMIXP_COLL_RING_CTX_NUM; i++) {
        coll_ctx = &ring->ctx_array[i];
        if (coll_ctx->in_use && coll_ctx->seq == seq) {
            return coll_ctx;
        } else if (!coll_ctx->in_use) {
            ret = coll_ctx;
            continue;
        }
    }
    /* add this context to use */
    if (ret && !ret->in_use) {
        ret->in_use = true;
        ret->seq = seq;
        ret->ring_buf = _get_contrib_buf(ret);
    }
    return ret;
}

int pmixp_coll_ring_neighbor(pmixp_coll_t *coll, pmixp_coll_ring_msg_hdr_t *hdr,
                 Buf buf)
{
    int ret = SLURM_SUCCESS;
    char *data_ptr = NULL;
    pmixp_coll_ring_ctx_t *coll_ctx = NULL;
    uint32_t hop_seq;

    /* lock the structure */
    //slurm_mutex_lock(&coll->lock);

    coll_ctx = pmixp_coll_ring_ctx_select(coll, hdr->seq);
    if (!coll_ctx) {
        PMIXP_ERROR("Can not get ring collective context, seq=%u",
                hdr->seq);
        ret = SLURM_ERROR;
        goto exit;
    }
#ifdef PMIXP_COLL_DEBUG
    PMIXP_DEBUG("%p: contrib/nbr: seqnum=%u, state=%d, nodeid=%d, contrib=%d, seq=%d, size=%lu",
            coll_ctx, coll_ctx->seq, coll_ctx->state, hdr->nodeid,
            hdr->contrib_id, hdr->hop_seq, hdr->msgsize);
#endif


    /* verify msg size */
    if (hdr->msgsize != remaining_buf(buf)) {
#ifdef PMIXP_COLL_DEBUG
        PMIXP_DEBUG("%p: unexpected message size=%d, expect=%zu",
                coll, remaining_buf(buf), hdr->msgsize);
        int i;
        for(i = 0; i < PMIXP_COLL_RING_CTX_NUM; i++) {
            coll_ctx = &coll->state.ring.ctx_array[i];
            if (coll_ctx->in_use) {
                fprintf(stderr, "local   %d\n", coll_ctx->contrib_local);
                fprintf(stderr, "prev    %d\n", coll_ctx->contrib_prev);
                fprintf(stderr, "fwd cnt %d\n", coll_ctx->forward_cnt);
                fprintf(stderr, "state   %d\n", coll_ctx->state);
            }
        }
#endif
        goto exit;
    }

    /* compute the actual hops of ring: (src - dst + size) % size */
    hop_seq = (coll->my_peerid + coll->peers_cnt - hdr->contrib_id) %
        coll->peers_cnt - 1;
    if (hdr->hop_seq != hop_seq) {
#ifdef PMIXP_COLL_DEBUG
        PMIXP_DEBUG("%p: unexpected ring seq number=%d, expect=%d, coll seq=%d",
                coll, hdr->hop_seq, hop_seq, coll->seq);
#endif
        goto exit;
    }

    if (hdr->contrib_id >= coll->peers_cnt) {
        goto exit;
    }

    if (coll_ctx->contrib_map[hdr->contrib_id]) {
#ifdef PMIXP_COLL_DEBUG
        PMIXP_DEBUG("%p: double receiving was detected from %d, "
                "local seq=%d, seq=%d, rejected",
                coll, hdr->contrib_id, coll->seq, hdr->seq);
#endif
        goto exit;
    }

    /* mark number of individual contributions */
    coll_ctx->contrib_map[hdr->contrib_id] = true;

    data_ptr = get_buf_data(buf) + get_buf_offset(buf);
    if (_pmixp_coll_contrib(coll_ctx, hdr->contrib_id, hdr->hop_seq + 1,
                data_ptr, remaining_buf(buf))) {
        goto exit;
    }

    /* increase number of ring contributions */
    coll_ctx->contrib_prev++;

    /* ring coll progress */
    _progress_coll_ring(coll_ctx);
exit:
    /* unlock the structure */
    //slurm_mutex_unlock(&coll->lock);
    return ret;
}

int ring_progress(int nprocs)
{
    int i, n;
    Buf recv_buf;
    pmixp_coll_type_t type;
    pmixp_coll_ring_msg_hdr_t ring_hdr;
    msg_t *msg;
    int ret = 0;

    do {
        ret = 0;
        for (i = 0; i < nprocs; i++) {
            pmixp_coll_t *coll = &clients[i].coll;
            if (!ret) {
                for (n = 0; n < PMIXP_COLL_RING_CTX_NUM; n++) {
                    pmixp_coll_ring_ctx_t *ctx = &coll->state.ring.ctx_array[n];
                    ret += ctx->in_use;
                    if (ret) {
                        break;
                    }
                }
            }
            if (list_count(clients[i].in_buf) == 0) {
                continue;
            }
            recv_buf = list_dequeue(clients[i].in_buf);
            msg = (msg_t*)get_buf_data(recv_buf);
            msg->complete_cb(0, 0, msg->cb_data);
            pmixp_coll_ring_unpack(msg->buf, &type, &ring_hdr, NULL, NULL);
            PMIXP_DEBUG("recv msg from %d size %u", ring_hdr.contrib_id, size_buf(msg->buf));
            pmixp_coll_ring_neighbor(coll, &ring_hdr, msg->buf);
        }
    } while (ret);
}

static int coll_init(pmixp_coll_t *coll, int my_peerid, int peers_cnt)
{
    coll->seq = 0;

    coll->peers_cnt = peers_cnt;
    coll->my_peerid = my_peerid;
    pmixp_coll_ring_init(coll, NULL);
}

int main( int argc , char *argv[]) {
    int i;
    uint32_t nclients = atoi(argv[1]);//sizeof(clients)/sizeof(*clients);
    clients = malloc(sizeof(*clients) * nclients);

    for (i = 0; i < nclients; i++) {
        clients[i].id = i;
        clients[i].in_buf = list_create(pmixp_free_buf);
        coll_init(&clients[i].coll, i, nclients);
    }


    for (i = 0; i < nclients; i++) {
        char *data;
        asprintf(&data, "data_from_node_%d\n", i);
        size_t size = strlen(data);
        pmixp_coll_ring_local(&clients[i].coll, data, size, NULL, NULL);
    }

    ring_progress(nclients);

    free(clients);

    return 0;
}

