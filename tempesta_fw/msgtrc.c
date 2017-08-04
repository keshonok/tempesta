#include "http.h"
#include "msgtrc.h"

typedef struct {
	struct list_head	conn_list;
	struct list_head	msg_queue;
	spinlock_t		msg_qlock;
	unsigned long		seqnum_req;
	unsigned long		seqnum;
} TfwMsgTrcConn;

typedef struct {
	struct list_head	msg_list;
	unsigned long		seqnum;
	unsigned int		flags;
} TfwMsgTrcReq;

static atomic64_t tfw_msgtrc_seqnum_conn;
static struct list_head tfw_msgtrc_conn_queue;
static spinlock_t tfw_msgtrc_conn_queue_lock;

static struct kmem_cache *tfw_msgtrc_conn_cache;
static struct kmem_cache *tfw_msgtrc_req_cache;

static TfwMsgTrcReq *
tfw_msgtrc_req_lookup(TfwHttpResp *resp)
{
	TfwMsgTrcConn *mtr_conn;
	TfwMsgTrcReq *mtr_req = NULL;

	spin_lock(&tfw_msgtrc_conn_queue_lock);
	
	list_for_each_entry(mtr_conn, &tfw_msgtrc_conn_queue, conn_list) {
		if (mtr_conn->seqnum != resp->seqnum_conn)
			continue;

		spin_lock(&mtr_conn->msg_qlock);
		list_for_each_entry(mtr_req, &mtr_conn->msg_queue, msg_list)
			if (mtr_req->seqnum == resp->seqnum_req)
				break;
		spin_unlock(&mtr_conn->msg_qlock);

		if (&mtr_req->msg_list == &mtr_conn->msg_queue)
			mtr_req = NULL;
		break;
	}

	spin_unlock(&tfw_msgtrc_conn_queue_lock);

	return mtr_req;
}

void
tfw_msgtrc_req_trace(TfwHttpResp *resp)
{
	TfwMsgTrcReq *mtr_req;
	typeof(mtr_req->flags) flags;

	/* Request has never been in @fwd_queue. */
	if (!(mtr_req->flags & TFW_MTR_IN_FWD_QUEUE))
		return;

	if (!(mtr_req = tfw_msgtrc_req_lookup(resp))) {
		printk(KERN_ERR "Request with seqnum conn=[%lu] req=[%lu]"
				" NOT FOUND in the trace! Huh???\n",
				resp->seqnum_conn, resp->seqnum_req);
		return;
	}

	if (!mtr_req->flags) {
		printk(KERN_ERR "Request with seqnum conn=[%lu] req=[%lu]: "
				"MISSING EVENTS!\n",
				resp->seqnum_conn, resp->seqnum_req);
		return;
	}
	flags = mtr_req->flags;

	if (flags & (1 << TFW_MTR_POPREQ)) {
		printk(KERN_ERR "Request with seqnum conn=[%lu] req=[%lu]: "
				"paired with another response (IMPOSSIBLE!)\n",
				resp->seqnum_conn, resp->seqnum_req);
		flags &= ~(1 << TFW_MTR_POPREQ);
	}
	if (flags & (1 << TFW_MTR_RESCHED)) {
		printk(KERN_ERR "Request with seqnum conn=[%lu] req=[%lu]: "
				"rescheduled to another backend\n",
				resp->seqnum_conn, resp->seqnum_req);
		flags &= ~(1 << TFW_MTR_RESCHED);
	}
	if (flags & (1 << TFW_MTR_CONN_RELEASE)) {
		printk(KERN_ERR "Request with seqnum conn=[%lu] req=[%lu]: "
				"released with the connection\n",
				resp->seqnum_conn, resp->seqnum_req);
		flags &= ~(1 << TFW_MTR_CONN_RELEASE);
	}
	if (flags & (1 << TFW_MTR_EVICT_TIMEOUT)) {
		printk(KERN_ERR "Request with seqnum conn=[%lu] req=[%lu]: "
				"evicted due to timeout expiration\n",
				resp->seqnum_conn, resp->seqnum_req);
		flags &= ~(1 << TFW_MTR_EVICT_TIMEOUT);
	}
	if (flags & (1 << TFW_MTR_EVICT_RETRIES)) {
		printk(KERN_ERR "Request with seqnum conn=[%lu] req=[%lu]: "
				"evicted due to retries exhaustion\n",
				resp->seqnum_conn, resp->seqnum_req);
		flags &= ~(1 << TFW_MTR_EVICT_RETRIES);
	}
	if (flags & (1 << TFW_MTR_FWD_SEND_ERROR)) {
		printk(KERN_ERR "Request with seqnum conn=[%lu] req=[%lu]: "
				"error forwarding to a backend\n",
				resp->seqnum_conn, resp->seqnum_req);
		flags &= ~(1 << TFW_MTR_FWD_SEND_ERROR);
	}
	if (flags & (1 << TFW_MTR_FWD_CANT_NIP)) {
		printk(KERN_ERR "Request with seqnum conn=[%lu] req=[%lu]: "
				"re-forwarding of NIP requests not allowed\n",
				resp->seqnum_conn, resp->seqnum_req);
		flags &= ~(1 << TFW_MTR_FWD_CANT_NIP);
	}
	if (flags & (1 << TFW_MTR_RESCHED_NO_BACKEND)) {
		printk(KERN_ERR "Request with seqnum conn=[%lu] req=[%lu]: "
				"no backends available for rescheduling\n",
				resp->seqnum_conn, resp->seqnum_req);
		flags &= ~(1 << TFW_MTR_RESCHED_NO_BACKEND);
	}

	if (flags) {
		printk(KERN_ERR "Request with seqnum conn=[%lu] req=[%lu]: "
				"UNKNOWN EVENTS: [0x%x]\n",
				resp->seqnum_conn, resp->seqnum_req, flags);
		return;
	}
}

void
tfw_msgtrc_req_event(TfwHttpReq *req, int event)
{
	TfwMsgTrcReq *mtr_req = req->msgtrc;

	mtr_req->flags |= (1 << event);
}

void
tfw_msgtrc_req_reg_seqnum(TfwHttpReq *req)
{
	TfwMsgTrcConn *mtr_conn;
	TfwMsgTrcReq *mtr_req = req->msgtrc;

	mtr_conn = (TfwMsgTrcConn *)((TfwCliConn *)req->conn)->msgtrc;

	req->seqnum_conn = mtr_conn->seqnum;
	req->seqnum_req = mtr_req->seqnum;
}

bool
tfw_msgtrc_resp_reg_seqnum(TfwHttpResp *resp)
{
	if (resp->flags & TFW_HTTP_SEQNUM)
		return true;
	return false;
}

int
tfw_msgtrc_conn_add(TfwCliConn *cli_conn)
{
	TfwMsgTrcConn *mtr_conn;

	if (!(mtr_conn = kmem_cache_alloc(tfw_msgtrc_conn_cache, GFP_ATOMIC)))
		return -ENOMEM;

	cli_conn->msgtrc = mtr_conn;

	return 0;
}

void
tfw_msgtrc_conn_add_trace(TfwCliConn *cli_conn)
{
	TfwMsgTrcConn *mtr_conn = (TfwMsgTrcConn *)cli_conn->msgtrc;

	INIT_LIST_HEAD(&mtr_conn->msg_queue);
	spin_lock_init(&mtr_conn->msg_qlock);
	mtr_conn->seqnum = atomic64_inc_return(&tfw_msgtrc_seqnum_conn);
	mtr_conn->seqnum_req = 0;

	spin_lock(&tfw_msgtrc_conn_queue_lock);
	list_add_tail(&mtr_conn->conn_list, &tfw_msgtrc_conn_queue);
	spin_unlock(&tfw_msgtrc_conn_queue_lock);
}

int
tfw_msgtrc_req_add(TfwHttpReq *req)
{
	TfwMsgTrcReq *mtr_req;
	TfwMsgTrcConn *mtr_conn;

	mtr_conn = (TfwMsgTrcConn *)((TfwCliConn *)req->conn)->msgtrc;

	if (!(mtr_req = kmem_cache_alloc(tfw_msgtrc_req_cache, GFP_ATOMIC)))
		return -ENOMEM;

	req->msgtrc = mtr_req;

	return 0;
}

/*
 * Note: ignore the contents of a possible 'Z-Tfw-SeqNum' header from
 * a client. The contents of the header may be used in functional tests.
 */
void
tfw_msgtrc_req_add_trace(TfwHttpReq *req)
{
	TfwMsgTrcReq *mtr_req = req->msgtrc;
	TfwMsgTrcConn *mtr_conn;

	mtr_conn = (TfwMsgTrcConn *)((TfwCliConn *)req->conn)->msgtrc;

	mtr_req->flags = 0;

	spin_lock(&mtr_conn->msg_qlock);
	mtr_req->seqnum = ++mtr_conn->seqnum_req;
	list_add_tail(&mtr_req->msg_list, &mtr_conn->msg_queue);
	spin_unlock(&mtr_conn->msg_qlock);
}

int
tfw_msgtrc_init(void)
{
	tfw_msgtrc_req_cache = kmem_cache_create("tfw_msgtrc_req_cache",
						  sizeof(TfwMsgTrcReq),
						  0, 0, NULL);
	tfw_msgtrc_conn_cache = kmem_cache_create("tfw_msgtrc_conn_cache",
						  sizeof(TfwMsgTrcConn),
						  0, 0, NULL);
	if (!(tfw_msgtrc_req_cache && tfw_msgtrc_conn_cache)) {
		if (tfw_msgtrc_req_cache)
			kmem_cache_destroy(tfw_msgtrc_req_cache);
		if (tfw_msgtrc_conn_cache)
			kmem_cache_destroy(tfw_msgtrc_conn_cache);
		return -ENOMEM;
	}

	atomic64_set(&tfw_msgtrc_seqnum_conn, 0);
	INIT_LIST_HEAD(&tfw_msgtrc_conn_queue);
	spin_lock_init(&tfw_msgtrc_conn_queue_lock);

	return 0;
}

void
tfw_msgtrc_exit(void)
{
	TfwMsgTrcReq *mtr_req, *tmtr_req;
	TfwMsgTrcConn *mtr_conn, *tmtr_conn;

	spin_lock(&tfw_msgtrc_conn_queue_lock);
	
	list_for_each_entry_safe(mtr_conn, tmtr_conn,
				 &tfw_msgtrc_conn_queue, conn_list)
	{
		spin_lock(&mtr_conn->msg_qlock);
		list_for_each_entry_safe(mtr_req, tmtr_req,
					 &mtr_conn->msg_queue, msg_list)
			kmem_cache_free(tfw_msgtrc_req_cache, mtr_req);
		spin_unlock(&mtr_conn->msg_qlock);

		kmem_cache_free(tfw_msgtrc_conn_cache, mtr_conn);
	}

	spin_unlock(&tfw_msgtrc_conn_queue_lock);

	kmem_cache_destroy(tfw_msgtrc_req_cache);
	kmem_cache_destroy(tfw_msgtrc_conn_cache);
}
