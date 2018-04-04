#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include <mpi.h>
#include <src/plugins/mpi/pmix/pmixp_coll.h>
#include <src/plugins/mpi/pmix/pmixp_server.h>

#include "src/common/xmalloc.h"

typedef enum {
	PMIXP_COLL_RING_NONE,
	PMIXP_COLL_RING_SYNC,
	PMIXP_COLL_RING_COLLECT,
	PMIXP_COLL_RING_DONE,
} pmixp_coll_ring_state_t;

typedef struct {
	uint32_t id;
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
	uint32_t seq;
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

pthread_t progress_thread[2];

typedef void (*ring_cbfunc_t)(pmixp_coll_ring_t *coll);

#define LOG(format, args...) {				\
	printf("%d:[%-30s:%4d] " format "\n",		\
	      _rank, __func__, __LINE__, ## args);	\
}

void hexDump(char *desc, void *addr, int len);
static void _progress_ring(pmixp_coll_ring_t *coll);


static int _prev_id(pmixp_coll_ring_t *coll) {
	return (coll->my_peerid + coll->peers_cnt - 1) % coll->peers_cnt;
}

static int _next_id(pmixp_coll_ring_t *coll) {
	return (coll->my_peerid + 1) % coll->peers_cnt;
}

void _coll_ctx_shift(pmixp_coll_ring_t *coll) {
	LOG("shift coll ctx");
	assert(coll->ctx);
	coll->ctx_cur = (coll->ctx_cur + 1) % _RING_CTX_NUM;
	coll->ctx = &coll->ctx_array[coll->ctx_cur];
}

static void _msg_send_nb(pmixp_coll_ring_t *coll, uint32_t sender, char *data, size_t size) {
	msg_hdr_t hdr;
	hdr.nodeid = _rank;
	hdr.msgsize = size;
	hdr.seq = coll->seq;
	hdr.ring_seq = coll->ctx->contrib_next;
	hdr.contrib_id = sender;
	MPI_Request request;
	int nodeid = _next_id(coll);

	LOG("seq:%d/%d %d ---[%d]--> %d (size %d)", hdr.seq, hdr.ring_seq, coll->my_peerid, hdr.contrib_id, nodeid, hdr.msgsize);
	MPI_Isend((void*) &hdr, sizeof(msg_hdr_t), MPI_BYTE, nodeid, 0, MPI_COMM_WORLD, &request);
	if (size) {
		MPI_Isend((void*) data, size, MPI_BYTE, nodeid, 0, MPI_COMM_WORLD, &request);
	}
}

static void _coll_send_all(pmixp_coll_ring_t *coll) {
	ring_data_t *msg;
	assert(coll->ctx);

	//LOG("send count %d", list_count(coll->ctx->send_list));
	while (!list_is_empty(coll->ctx->send_list)) {
		msg = list_dequeue(coll->ctx->send_list);
		_msg_send_nb(coll, msg->contrib_id, msg->ptr, msg->size);
		/* test */
		if (_rank == 1)
			_msg_send_nb(coll, msg->contrib_id, msg->ptr, msg->size);
		/* */
		coll->ctx->contrib_next++;
		free(msg);
	}
}

void * _coll_progress_thread(void *args) {
	pmixp_coll_ring_t *coll = (pmixp_coll_ring_t*)args;
	_progress_ring(coll);
	return NULL;
}

void _threadshift_progress(pmixp_coll_ring_t *coll) {
	assert(coll->ctx_cur < _RING_CTX_NUM);
	assert(coll->ctx);

	int rc = pthread_create(&progress_thread[coll->ctx_cur], NULL, _coll_progress_thread, (void*)coll);
	if (rc != 0) {
	    printf("error: can't create progress thread, rc = %d\n", rc);
	    abort();
	}
}


int coll_ring_contrib_local(pmixp_coll_ring_t *coll, char *data, size_t size,
			    void *cbfunc, void *cbdata) {
	int ret = SLURM_SUCCESS;
	ring_data_t *msg;
	assert(coll->ctx);

	slurm_mutex_lock(&coll->ctx->lock);

	if (coll->ctx->contrib_local) {
		/* Double contribution - reject */
		ret = SLURM_ERROR;
		slurm_mutex_unlock(&coll->ctx->lock);
		goto exit;
	}

	/* save & mark local contribution */
	if (!size_buf(coll->ctx->ring_buf)) {
		pmixp_server_buf_reserve(coll->ctx->ring_buf, size * coll->peers_cnt);
	} else if(remaining_buf(coll->ctx->ring_buf) < size) {
		/* grow sbuf size to 15% */
		size_t new_size = size_buf(coll->ctx->ring_buf) * 0.15 + size_buf(coll->ctx->ring_buf);
		pmixp_server_buf_reserve(coll->ctx->ring_buf, new_size);
	}

	memcpy(get_buf_data(coll->ctx->ring_buf) + get_buf_offset(coll->ctx->ring_buf),
	       data, size);

	msg = malloc(sizeof(ring_data_t));
	msg->ptr = get_buf_data(coll->ctx->ring_buf) + get_buf_offset(coll->ctx->ring_buf);
	msg->size = size;
	msg->contrib_id = coll->my_peerid;
	//LOG("send next add %d", size);

	list_enqueue(coll->ctx->send_list, msg);

	set_buf_offset(coll->ctx->ring_buf, get_buf_offset(coll->ctx->ring_buf) + size);

	coll->ctx->contrib_local = true;

	/* setup callback info */
	coll->cbfunc = cbfunc;
	coll->cbdata = cbdata;

	slurm_mutex_unlock(&coll->ctx->lock);
	_threadshift_progress(coll);
exit:
	return ret;
}

int coll_ring_contrib_prev(pmixp_coll_ring_t *coll, msg_hdr_t *hdr, Buf buf) {
	int ret = SLURM_SUCCESS;
	uint32_t size = 0;
	ring_data_t *msg;
	char *data_src = NULL, *data_dst = NULL;
	bool coll_shifted = false;

	assert(coll->ctx);

	if (hdr->nodeid != _prev_id(coll)) {
		LOG("unexpected peerid %d, expect %d", hdr->nodeid, _prev_id(coll));
		goto exit;
	}

	if (hdr->ring_seq != coll->ctx->contrib_prev) {
		LOG("error: unexpected msg seq number %d, expect %d", hdr->ring_seq, coll->ctx->contrib_prev);
		goto exit;
	}

	slurm_mutex_lock(&coll->ctx->lock);

	/* shift the coll context if that the next coll contrib */
	if ((coll->seq +1) == hdr->seq) {
		_coll_ctx_shift(coll);
		coll_shifted = true;
	}

	/* save & mark contribution */
	if (!size_buf(coll->ctx->ring_buf)) {
		pmixp_server_buf_reserve(coll->ctx->ring_buf, size * coll->peers_cnt);
	} else if(remaining_buf(coll->ctx->ring_buf) < size) {
		/* grow sbuf size to 15% */
		size_t new_size = size_buf(coll->ctx->ring_buf) * 0.15 + size_buf(coll->ctx->ring_buf);
		pmixp_server_buf_reserve(coll->ctx->ring_buf, new_size);
	}
	data_src = get_buf_data(buf) + get_buf_offset(buf);
	size = remaining_buf(buf);
	pmixp_server_buf_reserve(coll->ctx->ring_buf, size);
	data_dst = get_buf_data(coll->ctx->ring_buf) +
			get_buf_offset(coll->ctx->ring_buf);
	memcpy(data_dst, data_src, size);
	set_buf_offset(coll->ctx->ring_buf, get_buf_offset(coll->ctx->ring_buf) + size);

	if (hdr->contrib_id != _next_id(coll)) {
		//LOG("msg add to send, contribs %d", coll->contrib_prev);
		msg = malloc(sizeof(ring_data_t));
		msg->size = size;
		msg->ptr = data_dst;
		msg->contrib_id = hdr->contrib_id;
		list_enqueue(coll->ctx->send_list, msg);
	}

	coll->ctx->contrib_prev++;
	slurm_mutex_unlock(&coll->ctx->lock);

	/* ring coll progress */
	_threadshift_progress(coll);

	// TODO cond wait
	if (coll_shifted) {
		slurm_mutex_lock(&coll->ctx->lock);
		_coll_ctx_shift(coll);
		slurm_mutex_unlock(&coll->ctx->lock);
	}
exit:
	return ret;
}

pmixp_coll_ring_t *coll_ring_get() {
	pmixp_coll_ring_t *coll = xmalloc(sizeof(*coll));
	memset(coll, 0, sizeof(*coll));
	int i;
	coll->ctx_cur = 0;

	for (i = 0; i < _RING_CTX_NUM; i++) {
		coll->ctx = &coll->ctx_array[i];
		coll->ctx->id = i;
		coll->ctx->contrib_local = false;
		coll->ctx->contrib_prev = 0;
		coll->ctx->contrib_next = 0;
		coll->ctx->ring_buf = create_buf(NULL, 0);
		coll->ctx->state = PMIXP_COLL_RING_NONE;
		coll->ctx->send_list =  list_create(NULL);
		slurm_mutex_init(&coll->ctx->lock);
	}
	coll->ctx = &coll->ctx_array[coll->ctx_cur];
	coll->ctx->state = PMIXP_COLL_RING_SYNC;
	coll->peers_cnt = _size;
	coll->my_peerid = _rank;

	//coll->ring_buf = pmixp_server_buf_new();

	return coll;
}

static void _reset_coll_ring(pmixp_coll_ring_t *coll) {
	ring_cbfunc_t cbfunc;
	//LOG("seq %d", coll->seq);
	coll->ctx->state = PMIXP_COLL_RING_NONE;
	coll->ctx->contrib_local = false;
	coll->ctx->contrib_prev = 0;
	coll->ctx->contrib_next = 0;
	coll->seq++;
	cbfunc = coll->cbfunc;
	cbfunc(coll);
	set_buf_offset(coll->ctx->ring_buf, 0);
}

static void _progress_ring(pmixp_coll_ring_t *coll) {
	int ret = 0;

	slurm_mutex_lock(&coll->ctx->lock);
	do {
		switch(coll->ctx->state) {
			case PMIXP_COLL_RING_NONE:
				/* nothing to do, this collective is not started yet */
				break;
			case PMIXP_COLL_RING_SYNC:
				//LOG("PMIXP_COLL_RING_SYNC");
				if (coll->ctx->contrib_local || coll->ctx->contrib_prev) {
					coll->ctx->state = PMIXP_COLL_RING_COLLECT;
					ret = true;
					//LOG("PMIXP_COLL_RING_SYNC -> PMIXP_COLL_RING_COLLECT");
				} else {
					ret = false;
				}
				break;
			case PMIXP_COLL_RING_COLLECT:
				ret = false;
				//LOG("PMIXP_COLL_RING_COLLECT");
				//if (0 != _rank)
				_coll_send_all(coll);
				//else (coll->ctx->contrib_prev )
				if (!coll->ctx->contrib_local) {
					//LOG("PMIXP_COLL_RING_COLLECT: wait for local contrib")
					ret = false;
				} else if ((coll->peers_cnt - 1) == coll->ctx->contrib_prev) {
					_coll_send_all(coll);
					coll->ctx->state = PMIXP_COLL_RING_DONE;
					//LOG("PMIXP_COLL_RING_COLLECT -> PMIXP_COLL_RING_DONE");
					ret = true;
					pmixp_debug_hang(0);
				}
				break;
			case PMIXP_COLL_RING_DONE:
				LOG("PMIXP_COLL_RING_DONE");
				ret = false;
				/* test */
				//if (0 == coll->my_peerid) {
				//	sleep(5);
				//}
				/* */
				_reset_coll_ring(coll);
				/* disable the old coll */
				coll->ctx->state = PMIXP_COLL_RING_NONE;
				/* shift to new coll */
				_coll_ctx_shift(coll);
				coll->ctx->state = PMIXP_COLL_RING_SYNC;
				/* send the all collected ring contribs for the new collective */
				_coll_send_all(coll);
				break;
		}
	} while(ret);
	slurm_mutex_unlock(&coll->ctx->lock);
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
	LOG("result buf size %d, used %d", size_buf(coll->ctx->ring_buf), get_buf_offset(coll->ctx->ring_buf));
	if (1 == coll->my_peerid) {
		hexDump("ring fini", get_buf_data(coll->ctx->ring_buf), get_buf_offset(coll->ctx->ring_buf));
	}
}

void ring_coll(pmixp_coll_ring_t *coll, char *data, size_t size) {
	MPI_Status status;
	msg_hdr_t hdr = {0};
	Buf buf;

#if 0
	coll_ring_contrib_local(coll, data, size, cbfunc, NULL);
#else
	if (0 != coll->my_peerid) {
		coll_ring_contrib_local(coll, data, size, cbfunc, NULL);
	}
	else pmixp_debug_hang(0);
#endif
	if ( _rank == 0) {
		pmixp_debug_hang(0);
	}
	int save_seq = coll->seq;
	do {

		if (coll->my_peerid == 0) {
			sleep(0);
		}
		MPI_Recv(&hdr, sizeof(msg_hdr_t), MPI_BYTE, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
		if (hdr.msgsize) {
			char *data_buf = xmalloc(hdr.msgsize);
			MPI_Recv(data_buf, hdr.msgsize, MPI_BYTE, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
			LOG("seq:%d/%d %d <--[%d]--- %d (size %d)",
				hdr.seq, hdr.ring_seq, coll->my_peerid, hdr.contrib_id, hdr.nodeid, hdr.msgsize);
			buf = create_buf(data_buf, hdr.msgsize);
		} else {
			buf = create_buf(NULL, 0);//pmixp_server_buf_new();
		}

		coll_ring_contrib_prev(coll, &hdr, buf);

		if ((save_seq == coll->seq) && !coll->ctx->contrib_local){
			//sleep(2);
			coll_ring_contrib_local(coll, data, size, cbfunc, NULL);
		}
		//LOG("state %d", coll->state);
		usleep(200);
	} while (save_seq == coll->seq);
}

int main(int argc, char *argv[]) {
	pmixp_coll_ring_t *coll = NULL;
	char data[64];
	size_t size = sizeof(data);

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &_size);

	coll = coll_ring_get();

	if (_rank == 0) { size /=4; }
	if (_rank == 1) { size /=2; }

	memset((void*)data, (uint8_t)_rank, size);

	pmixp_debug_hang(0);

	ring_coll(coll, data, size);

	memset((void*)data, ((uint8_t)_rank << 4) +  (uint8_t)_rank , size);

	//LOG("\n\nSecond coll");
	//ring_coll(coll, data, size);


	xfree(coll);
	MPI_Finalize();

	return 0;
}
