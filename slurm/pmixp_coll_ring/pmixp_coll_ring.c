
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include <mpi.h>
#include <src/plugins/mpi/pmix/pmixp_coll.h>
#include <src/plugins/mpi/pmix/pmixp_server.h>

#include "src/common/xmalloc.h"

#define DEBUG_MAGIC 0xC011CAFE

typedef enum {
	PMIXP_COLL_RING_SYNC,
	PMIXP_COLL_RING_COLLECT,
	PMIXP_COLL_RING_DONE,
} pmixp_coll_ring_state_t;

typedef struct {
	void *super;
	uint32_t id;
	uint32_t seq;
	bool contrib_local;
	uint32_t contrib_prev;
	uint32_t contrib_next;
	pmixp_coll_ring_state_t state;
	Buf ring_buf;
	List send_list;
	pthread_mutex_t lock;
	void *cbfunc;
	void *cbdata;
} pmixp_coll_ring_ctx_t;

typedef struct {
	int magic;

	int my_peerid;
	int peers_cnt;
	void *cbfunc;
	void *cbdata;
	pmixp_coll_ring_ctx_t *ctx;
	pmixp_coll_ring_ctx_t ctx_array[2];
	uint32_t ctx_cur;
} pmixp_coll_ring_t;

#define _RING_CTX_NUM 2

typedef struct {
	uint32_t type;
	uint32_t contrib_id;
	uint32_t seq;
	uint32_t ring_seq;
	uint32_t nodeid;
	uint32_t msgsize;
} msg_hdr_t;

typedef struct {
	char *ptr;
	size_t size;
	uint32_t contrib_id;
} ring_data_t;

static int _rank, _size;

pthread_t progress_thread[_RING_CTX_NUM];

typedef void (*ring_cbfunc_t)(pmixp_coll_ring_t *coll);

#define LOG(format, args...) {				\
	printf("[%-30s:%4d] %d: " format "\n",		\
	      __func__, __LINE__, _rank, ## args);	\
}

void hexDump(char *desc, void *addr, int len);
static void _progress_ring(pmixp_coll_ring_ctx_t *coll_ctx);


static int _prev_id(pmixp_coll_ring_t *coll) {
	return (coll->my_peerid + coll->peers_cnt - 1) % coll->peers_cnt;
}

static int _next_id(pmixp_coll_ring_t *coll) {
	return (coll->my_peerid + 1) % coll->peers_cnt;
}

static inline pmixp_coll_ring_ctx_t * _get_coll_ctx_shift(pmixp_coll_ring_t *coll) {
	uint32_t id = (coll->ctx_cur + 1) % _RING_CTX_NUM;
	return &coll->ctx_array[id];
}

static inline pmixp_coll_ring_t *ctx_get_coll(pmixp_coll_ring_ctx_t *coll_ctx) {
	return (pmixp_coll_ring_t*)(coll_ctx->super);
}

static void _msg_send_nb(pmixp_coll_ring_ctx_t *coll_ctx, uint32_t sender, char *data, size_t size) {
	msg_hdr_t hdr;
	pmixp_coll_ring_t *coll = ctx_get_coll(coll_ctx);
	hdr.nodeid = _rank;
	hdr.msgsize = size;
	hdr.seq = coll_ctx->seq;
	hdr.ring_seq = coll_ctx->contrib_next;
	hdr.contrib_id = sender;
	MPI_Request request;
	int nodeid = _next_id(coll);
	assert(DEBUG_MAGIC == coll->magic);

	LOG("seq:%d/%d %d ---[%d]--> %d (size %d)", hdr.seq, hdr.ring_seq, coll->my_peerid, hdr.contrib_id, nodeid, hdr.msgsize);
	MPI_Isend((void*) &hdr, sizeof(msg_hdr_t), MPI_BYTE, nodeid, coll_ctx->seq, MPI_COMM_WORLD, &request);
	if (size) {
		MPI_Isend((void*) data, size, MPI_BYTE, nodeid, coll_ctx->seq, MPI_COMM_WORLD, &request);
	}
}

static void _coll_send_all(pmixp_coll_ring_ctx_t *coll_ctx) {
	ring_data_t *msg;
	assert(coll_ctx);

	//LOG("send count %d", list_count(coll->ctx->send_list));
	while (!list_is_empty(coll_ctx->send_list)) {
		msg = list_dequeue(coll_ctx->send_list);
		_msg_send_nb(coll_ctx, msg->contrib_id, msg->ptr, msg->size);
		/* test */
		//if (_rank == 1)
		//	_msg_send_nb(coll_ctx, msg->contrib_id, msg->ptr, msg->size);
		/* */
		coll_ctx->contrib_next++;
		free(msg);
	}
}

int coll_ring_contrib_local(pmixp_coll_ring_t *coll, char *data, size_t size,
			    void *cbfunc, void *cbdata) {
	int ret = SLURM_SUCCESS;
	ring_data_t *msg;
	assert(coll->ctx);
	pmixp_coll_ring_ctx_t *coll_ctx = coll->ctx;

	slurm_mutex_lock(&coll->ctx->lock);

	if (coll->ctx->contrib_local) {
		/* Double contribution - reject */
		ret = SLURM_ERROR;
		slurm_mutex_unlock(&coll->ctx->lock);
		goto exit;
	}

	/* save & mark local contribution */
	if (!size_buf(coll_ctx->ring_buf)) {
		pmixp_server_buf_reserve(coll_ctx->ring_buf, size * coll->peers_cnt);
	} else if(remaining_buf(coll_ctx->ring_buf) < size) {
		/* grow sbuf size to 15% */
		size_t new_size = size_buf(coll_ctx->ring_buf) * 0.15 + size_buf(coll_ctx->ring_buf);
		pmixp_server_buf_reserve(coll_ctx->ring_buf, new_size);
	}

	memcpy(get_buf_data(coll_ctx->ring_buf) + get_buf_offset(coll_ctx->ring_buf),
	       data, size);

	msg = malloc(sizeof(ring_data_t));
	msg->ptr = get_buf_data(coll_ctx->ring_buf) + get_buf_offset(coll_ctx->ring_buf);
	msg->size = size;
	msg->contrib_id = coll->my_peerid;
	//LOG("send next add %d", size);

	list_enqueue(coll_ctx->send_list, msg);

	set_buf_offset(coll_ctx->ring_buf, get_buf_offset(coll_ctx->ring_buf) + size);

	coll_ctx->contrib_local = true;

	/* setup callback info */
	coll->cbfunc = cbfunc;
	coll->cbdata = cbdata;

	slurm_mutex_unlock(&coll_ctx->lock);

	_progress_ring(coll_ctx);
exit:
	return ret;
}

int coll_ring_contrib_prev(pmixp_coll_ring_t *coll, msg_hdr_t *hdr, Buf buf) {
	int ret = SLURM_SUCCESS;
	uint32_t size = 0;
	ring_data_t *msg;
	char *data_src = NULL, *data_dst = NULL;
	pmixp_coll_ring_ctx_t *coll_ctx = coll->ctx;

	assert(DEBUG_MAGIC == coll->magic);
	assert(coll->ctx);

	if (hdr->nodeid != _prev_id(coll)) {
		LOG("unexpected peerid %d, expect %d", hdr->nodeid, _prev_id(coll));
		goto exit;
	}

	slurm_mutex_lock(&coll->ctx->lock);

	/* shift the coll context if that contrib belongs to the next coll  */
	if ((coll_ctx->seq +1) == hdr->seq) {
		pmixp_debug_hang(0);
		coll_ctx = _get_coll_ctx_shift(coll);
	}

	//if (hdr->ring_seq != coll_ctx->contrib_prev) {
	//	LOG("error: unexpected msg seq number %d, expect %d, coll seq %d/%d", hdr->ring_seq, coll->ctx->contrib_prev, coll->ctx->seq, coll_ctx->seq);
	//	goto exit;
	//}

	/* save & mark contribution */
	if (!size_buf(coll_ctx->ring_buf)) {
		pmixp_server_buf_reserve(coll_ctx->ring_buf, size * coll->peers_cnt);
	} else if(remaining_buf(coll_ctx->ring_buf) < size) {
		/* grow sbuf size to 15% */
		size_t new_size = size_buf(coll_ctx->ring_buf) * 0.15 + size_buf(coll_ctx->ring_buf);
		pmixp_server_buf_reserve(coll_ctx->ring_buf, new_size);
	}
	data_src = get_buf_data(buf) + get_buf_offset(buf);
	size = remaining_buf(buf);
	pmixp_server_buf_reserve(coll_ctx->ring_buf, size);
	data_dst = get_buf_data(coll_ctx->ring_buf) +
			get_buf_offset(coll_ctx->ring_buf);
	memcpy(data_dst, data_src, size);
	set_buf_offset(coll_ctx->ring_buf, get_buf_offset(coll_ctx->ring_buf) + size);

	if (hdr->contrib_id != _next_id(coll)) {
		//LOG("msg add to send, contribs %d", coll->contrib_prev);
		msg = malloc(sizeof(ring_data_t));
		msg->size = size;
		msg->ptr = data_dst;
		msg->contrib_id = hdr->contrib_id;
		list_enqueue(coll->ctx->send_list, msg);
	}

	coll_ctx->contrib_prev++;
	slurm_mutex_unlock(&coll_ctx->lock);

	/* ring coll progress */
	_progress_ring(coll_ctx);
exit:
	return ret;
}

pmixp_coll_ring_t *coll_ring_get() {
	pmixp_coll_ring_t *coll = xmalloc(sizeof(*coll));
	memset(coll, 0, sizeof(*coll));
	int i;
	coll->ctx_cur = 0;

	coll->magic = DEBUG_MAGIC;

	for (i = 0; i < _RING_CTX_NUM; i++) {
		coll->ctx = &coll->ctx_array[i];
		coll->ctx->id = i;
		coll->ctx->seq = i;
		coll->ctx->contrib_local = false;
		coll->ctx->contrib_prev = 0;
		coll->ctx->contrib_next = 0;
		coll->ctx->ring_buf = create_buf(NULL, 0);
		coll->ctx->state = PMIXP_COLL_RING_SYNC;
		coll->ctx->send_list =  list_create(NULL);
		coll->ctx->super = (void*)coll;
		slurm_mutex_init(&coll->ctx->lock);
	}
	coll->ctx = &coll->ctx_array[coll->ctx_cur];
	coll->ctx->state = PMIXP_COLL_RING_SYNC;
	coll->peers_cnt = _size;
	coll->my_peerid = _rank;

	//coll->ring_buf = pmixp_server_buf_new();

	return coll;
}

static void _reset_coll_ring(pmixp_coll_ring_ctx_t *coll_ctx) {
	pmixp_coll_ring_t *coll = ctx_get_coll(coll_ctx);
	assert(DEBUG_MAGIC == coll->magic);
	ring_cbfunc_t cbfunc;

	cbfunc = coll->cbfunc;
	if (cbfunc) {
		cbfunc(coll);
	}

	coll_ctx->state = PMIXP_COLL_RING_SYNC;
	coll_ctx->contrib_local = false;
	coll_ctx->contrib_prev = 0;
	coll_ctx->contrib_next = 0;
	coll_ctx->seq += _RING_CTX_NUM;
	set_buf_offset(coll_ctx->ring_buf, 0);
}

static void _progress_ring(pmixp_coll_ring_ctx_t *coll_ctx) {
	int ret = 0;
	pmixp_coll_ring_t *coll = ctx_get_coll(coll_ctx);
	//static int count = 0;

	assert(DEBUG_MAGIC == coll->magic);

	slurm_mutex_lock(&coll_ctx->lock);
	do {
		switch(coll_ctx->state) {
			case PMIXP_COLL_RING_SYNC:
				//LOG("PMIXP_COLL_RING_SYNC");
				if (coll_ctx->contrib_local || coll_ctx->contrib_prev) {
					coll_ctx->state = PMIXP_COLL_RING_COLLECT;
					ret = true;
				} else {
					ret = false;
				}
				break;
			case PMIXP_COLL_RING_COLLECT:
				ret = false;
				//LOG("PMIXP_COLL_RING_COLLECT");
				_coll_send_all(coll_ctx);
				if (!coll_ctx->contrib_local) {
					ret = false;
				} else if ((coll->peers_cnt - 1) == coll->ctx->contrib_prev) {
					coll_ctx->state = PMIXP_COLL_RING_DONE;
					ret = true;
					pmixp_debug_hang(0);
				}
				break;
			case PMIXP_COLL_RING_DONE:
				ret = false;
				_coll_send_all(coll->ctx);
				_reset_coll_ring(coll_ctx);
				/* shift to new coll */
				coll->ctx = _get_coll_ctx_shift(coll);
				coll->ctx_cur = coll->ctx->id;
				coll->ctx->state = PMIXP_COLL_RING_SYNC;
				/* send the all collected ring contribs for the new collective */
				break;
		}
	} while(ret);
	slurm_mutex_unlock(&coll_ctx->lock);
}

void hexDump(char *desc, void *addr, int len) {
    int i;
    unsigned char buff[17];
    unsigned char *pc = (unsigned char*)addr;

    if (desc != NULL)
	printf ("%s:\n", desc);
    for (i = 0; i < len; i++) {
	if ((i % 16) == 0) {
	    if (i != 0)
		printf("  %s\n", buff);
	    printf("  %04x ", i);
	}
	printf(" %02x", pc[i]);
	if ((pc[i] < 0x20) || (pc[i] > 0x7e)) {
	    buff[i % 16] = '.';
	} else {
	    buff[i % 16] = pc[i];
	}
	buff[(i % 16) + 1] = '\0';
    }
    while ((i % 16) != 0) {
	printf("   ");
	i++;
    }
    printf("  %s\n", buff);
}

void cbfunc(pmixp_coll_ring_t *coll) {
	assert(DEBUG_MAGIC == coll->magic);
	LOG("coll seq %d: result buf size %d, used %d", coll->ctx->seq, size_buf(coll->ctx->ring_buf), get_buf_offset(coll->ctx->ring_buf));
	if (1 == coll->my_peerid) {
		hexDump("ring fini", get_buf_data(coll->ctx->ring_buf), get_buf_offset(coll->ctx->ring_buf));
	}
	else LOG("cbfunc coll %d", coll->ctx->seq);
}

void ring_coll(pmixp_coll_ring_t *coll, char *data, size_t size) {
	MPI_Status status;
	msg_hdr_t hdr = {0};
	Buf buf;
	int recv_flag;
	pmixp_coll_ring_ctx_t *coll_ctx;
	pmixp_coll_ring_state_t state = PMIXP_COLL_RING_COLLECT;
	int wait = 0;
	int seq, i;
	int delay_usec = 100000;
	int wait_usec = 0;
	int local_contrib_delay = 0;

	LOG("start %d", seq = coll->ctx->seq);

	assert(DEBUG_MAGIC == coll->magic);

#if 0
	coll_ring_contrib_local(coll, data, size, cbfunc, NULL);
	state = coll->ctx->state;
#else
	if (0 != coll->my_peerid) {
		coll_ring_contrib_local(coll, data, size, cbfunc, NULL);
		state = coll->ctx->state;
	} else {
		pmixp_debug_hang(0);
	}
#endif
	/* */
	if ((seq == 0) && (0 == coll->my_peerid)) {
		wait = 1;
		wait_usec = 3 * 1000000;
		local_contrib_delay = 3 * 1000000;
	}
	/* save orig ctx */
	coll_ctx = coll->ctx;
	do {
		MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &recv_flag, &status);
		if (recv_flag) {
			//LOG("income hdr: src %d, tag %d, size %d", status.MPI_SOURCE, status.MPI_TAG, status._ucount);
			if (status._ucount != sizeof(msg_hdr_t)) {
				LOG("error: unexpected message size %lu from %d, expected %lu",  status._ucount, status.MPI_SOURCE, sizeof(msg_hdr_t));
				abort();
			}
			MPI_Recv(&hdr, sizeof(msg_hdr_t), MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

			MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			//LOG("income msg: src %d, tag %d, size %d", status.MPI_SOURCE, status.MPI_TAG, status._ucount);
			if (hdr.msgsize) {
				char *data_buf = xmalloc(hdr.msgsize);
				MPI_Recv(data_buf, hdr.msgsize, MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
				LOG("seq:%d/%d %d <--[%d]--- %d (size %d)",
					hdr.seq, hdr.ring_seq, coll->my_peerid, hdr.contrib_id, hdr.nodeid, hdr.msgsize);
				buf = create_buf(data_buf, hdr.msgsize);
			} else {
				buf = create_buf(NULL, 0);//pmixp_server_buf_new();
			}

			coll_ring_contrib_prev(coll, &hdr, buf);
			state = coll_ctx->state;
		}
		if ((0 == coll->my_peerid) && (state == PMIXP_COLL_RING_SYNC)) {
			wait_usec -= delay_usec;
			if (wait_usec < 0) {
				wait = 0;
			}

		}
		if ((local_contrib_delay < 0)) {
			//LOG("seq %d, coll_ctx->contrib_local %d", coll_ctx->seq, coll_ctx->contrib_local);
			if (seq == coll_ctx->seq && !coll_ctx->contrib_local){
				coll_ring_contrib_local(coll, data, size, cbfunc, NULL);
				state = coll_ctx->state;
			}
		}
		local_contrib_delay -= delay_usec;
		usleep(delay_usec);
		//LOG("coll %d", seq);
	} while ((state != PMIXP_COLL_RING_SYNC) || wait);
	LOG("exit %d", seq);
	for (i = 0; i < _RING_CTX_NUM; i++) {
		LOG("\t coll %d buf size %d", i, size_buf(coll->ctx_array[i].ring_buf));
	}
}

int main(int argc, char *argv[]) {
	pmixp_coll_ring_t *coll = NULL;
	char data[64];
	size_t size = sizeof(data);
	int status;

	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &status);
	if (status != MPI_THREAD_MULTIPLE)
	{
	    printf("Sorry, this MPI implementation does not support multiple threads\n");
	    MPI_Abort(MPI_COMM_WORLD, 1);
	}
	MPI_Comm_rank(MPI_COMM_WORLD, &_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &_size);

	coll = coll_ring_get();

	if (_rank == 0) { size /=4; }
	if (_rank == 1) { size /=2; }

	memset((void*)data, (uint8_t)_rank, size);

	pmixp_debug_hang(0);

	ring_coll(coll, data, size);

	/* go to the next coll */
	memset((void*)data, ((uint8_t)_rank << 4) +  (uint8_t)_rank , size);
	ring_coll(coll, data, size);

	//memset((void*)data, (uint8_t)_rank, size);
	//ring_coll(coll, data, size);

	MPI_Barrier(MPI_COMM_WORLD);
	xfree(coll);
	MPI_Finalize();

	return 0;
}
