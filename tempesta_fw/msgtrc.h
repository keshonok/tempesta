#include "http.h"

/* Reasons for removal of a request from @fwd_queue. */
enum {
	TFW_MTR_IN_FWD_QUEUE = 0,
	TFW_MTR_POPREQ,
	TFW_MTR_RESCHED,
	TFW_MTR_CONN_RELEASE,
	TFW_MTR_EVICT_TIMEOUT,
	TFW_MTR_EVICT_RETRIES,
	TFW_MTR_FWD_SEND_ERROR,
	TFW_MTR_FWD_CANT_NIP,
	TFW_MTR_RESCHED_NO_BACKEND,
};

void tfw_msgtrc_req_event(TfwHttpReq *req, int reason);

void tfw_msgtrc_req_reg_seqnum(TfwHttpReq *req);
bool tfw_msgtrc_resp_reg_seqnum(TfwHttpResp *resp);

int tfw_msgtrc_conn_add(TfwCliConn *cli_conn);
void tfw_msgtrc_conn_add_trace(TfwCliConn *cli_conn);

int tfw_msgtrc_req_add(TfwHttpReq *req);
void tfw_msgtrc_req_add_trace(TfwHttpReq *req);

void tfw_msgtrc_req_trace(TfwHttpResp *resp);

int tfw_msgtrc_init(void);
void tfw_msgtrc_exit(void);
