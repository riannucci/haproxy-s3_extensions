// vim: sw=2 sts=2 expandtab smartindent

#include <proto/s3_extensions.h>
#include <proto/s3_extensions_cpp.h>
#include <proto/proto_http.h>
#include <proto/hdr_idx.h>
#include <types/global.h>
#include <openssl/hmac.h>
#include <openssl/engine.h>
#include <hiredis/hiredis.h>

extern int http_remove_header2(struct http_msg *msg, struct buffer *buf,
			struct hdr_idx *idx, struct hdr_ctx *ctx);
extern int http_header_add_tail2(struct buffer *b, struct http_msg *msg,
			 struct hdr_idx *hdr_idx, const char *text, int len);

static redisContext *g_redis_ctxt = NULL;

redisReply *doRedisCommand(const char *fmt, ...) {
  va_list ap;
  int tried = 0;

retry:
  if(!g_redis_ctxt) {
    const char *address = "127.0.0.1";
    int port = 6379;
    if(global.redis.address) {
      address = global.redis.address;
      port    = global.redis.port;
    }
    g_redis_ctxt = redisConnect(address, port);
  }

  va_start(ap,fmt);
  redisReply *reply = redisvCommand(g_redis_ctxt, fmt, ap);
  va_end(ap);

  if(!reply) {
    if(!tried && g_redis_ctxt->err == REDIS_ERR_EOF) {
      // Connection probably timed out
      tried = 1;
      redisFree(g_redis_ctxt);
      g_redis_ctxt = NULL;
      goto retry;
    } else {
      printf("Error: %s\n", g_redis_ctxt->errstr);
    }
  }

  return reply;
}

int s3_already_redirected(struct acl_test *test, struct acl_pattern *pattern) {
  const char *bucket      = pattern->ptr.str;
  const int  object_len   = test->len;
  const char *object_str  = test->ptr;

  redisReply *reply = doRedisCommand("SISMEMBER %s %b", bucket, object_str, object_len);
  int retval = !reply || reply->integer ? ACL_PAT_PASS : ACL_PAT_FAIL;

  if(reply) freeReplyObject(reply);

  return retval;
}

int s3_mark_redirected(struct acl_test *test, struct acl_pattern *pattern) {
  const char *bucket      = pattern->ptr.str;
  const int  object_len   = test->len;
  const char *object_str  = test->ptr;

  redisReply *reply = doRedisCommand("SADD %s %b", bucket, object_str, object_len);
  if(reply) freeReplyObject(reply);

  return ACL_PAT_PASS;
}

int s3_add_resign(const char *file, int line, struct proxy *px, const char *bucket, const char *id, const char *key)
{
  // TODO: Add bucket/key/id error checking
  px->s3_bucket = strdup(bucket);

  const char *new_auth_header_fmt = "Authorization: AWS %s:%%s\r\n";
  char *new_auth_header = malloc(sizeof(char) * (strlen(id) + strlen(new_auth_header_fmt)));
    sprintf(new_auth_header, new_auth_header_fmt, id);
    px->s3_auth_header      = strdup(new_auth_header);
    // length(BASE64(HMAC-SHA1(stuff))) == 28
    px->s3_auth_header_len  = strlen(px->s3_auth_header)+28;
  free(new_auth_header);

  px->s3_key    = strdup(key);
  return 0;
}

// Totally copied from apply_filter_to_req_headers
void for_each_header(struct http_txn *txn, void *data, void (*func)(void *data, char *begin, size_t len)) {
  char *cur_ptr, *cur_end, *cur_next;
  int cur_idx = 0, last_hdr;
  struct hdr_idx_elem *cur_hdr;

  last_hdr = 0;

  cur_next = txn->req.sol + hdr_idx_first_pos(&txn->hdr_idx);

  while (!last_hdr) {
    cur_idx = txn->hdr_idx.v[cur_idx].next;
    if (!cur_idx)
      break;

    cur_hdr  = &txn->hdr_idx.v[cur_idx];
    cur_ptr  = cur_next;
    cur_end  = cur_ptr + cur_hdr->len;
    cur_next = cur_end + cur_hdr->cr + 1;

    (func)(data, cur_ptr, cur_end-cur_ptr);
  }
}

void optionally_add_header(HMAC_CTX *ctx, struct http_txn *txn, const char* header_name) {
  struct hdr_ctx hdr_ctx = {.idx = 0};
  int ret = http_find_header2(header_name, strlen(header_name), txn->req.sol, &txn->hdr_idx, &hdr_ctx);
  if(ret) {
    HMAC_Update(ctx, (unsigned char*)hdr_ctx.line+hdr_ctx.val, hdr_ctx.vlen);
  }
  HMAC_Update(ctx, (unsigned const char*)"\n", 1);
}

char *make_aws_signature(const char *key, struct http_txn *txn, struct proxy* px) {
  struct http_msg *msg = &txn->req;

  static int loaded_engines = 0;
  if(!loaded_engines) {
    ENGINE_load_builtin_engines();
    ENGINE_register_all_complete();
  }
  unsigned int raw_sig_len = 28;
  unsigned char *raw_sig = malloc(sizeof(char) * raw_sig_len);

  HMAC_CTX ctx;
  HMAC_CTX_init(&ctx);
  HMAC_Init_ex(&ctx, key, strlen(key), EVP_sha1(), NULL);

  HMAC_Update(&ctx, (unsigned char*)msg->sol + msg->som, msg->sl.rq.m_l);
  HMAC_Update(&ctx, (unsigned const char*)"\n", 1);

  optionally_add_header(&ctx, txn, "Content-MD5");
  optionally_add_header(&ctx, txn, "Content-Type");
  optionally_add_header(&ctx, txn, "Date");

  void *header_sorter = HeaderSorter_new("x-amz-");
  for_each_header(txn, header_sorter, &HeaderSorter_add);
  HeaderSorter_update(header_sorter, &ctx);
  HeaderSorter_delete(header_sorter);
  header_sorter = NULL;

  CanonicalizeResource(&ctx, px->s3_bucket, txn->req.sol+txn->req.sl.rq.u, txn->req.sl.rq.u_l);

  HMAC_Final(&ctx, raw_sig, &raw_sig_len);
  HMAC_CTX_cleanup(&ctx);

  BIO *mem_output_stream, *b64_filter;
  BUF_MEM *output_buffer;

  b64_filter = BIO_new(BIO_f_base64());
  mem_output_stream = BIO_new(BIO_s_mem());
  b64_filter = BIO_push(b64_filter, mem_output_stream);
  BIO_write(b64_filter, raw_sig, raw_sig_len);
  (void) BIO_flush(b64_filter);
  BIO_get_mem_ptr(b64_filter, &output_buffer);

  char *retval = malloc(sizeof(char) + output_buffer->length);
  memcpy(retval, output_buffer->data, output_buffer->length-1);
  retval[output_buffer->length-1] = 0;

  BIO_free_all(b64_filter);

  return retval;
}

int s3_resign(struct session *s, struct buffer *req, struct proxy *px) {
  // TODO: Enable support for query string signatures?

  if (!px->s3_bucket || !px->s3_auth_header || !px->s3_key) {
    printf("In s3_resign but have null config fields?");
    return 0;
  }

  struct http_txn *txn = &s->txn;

  struct hdr_ctx authorization_header = {.idx = 0};
  int ret = http_find_header2("Authorization", 13, txn->req.sol, &txn->hdr_idx, &authorization_header);
  if(!ret) {
    printf("No Authorization, so pass through unsigned.");
    return 0;
  }

  struct hdr_exp exp;
  memset(&exp, sizeof(exp), 0);
  exp.preg    = calloc(1, sizeof(regex_t));
  exp.replace = malloc(sizeof(char) * px->s3_auth_header_len);;
  exp.action  = ACT_REPLACE;
  regcomp((regex_t*)exp.preg, "^Authorization:.*$", REG_EXTENDED | REG_ICASE);

  char *signature = make_aws_signature(px->s3_key, txn, px);
  sprintf((char*)exp.replace, px->s3_auth_header, signature);
  free(signature);

  apply_filter_to_req_headers(s, req, &exp);

  free((void*)exp.preg);
  free((void*)exp.replace);

  return 0;
}
