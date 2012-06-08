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

int s3_already_redirected(struct acl_test *test, struct acl_pattern * ignored) {
  const char *bucket      = test->ptr+1;
  const char *bucket_end  = memchr(bucket, '/', test->len-1);
  const int  bucket_len   = bucket_end - bucket;
  const char *object_str  = bucket_end+1;
  const int  object_len   = test->len - bucket_len - 2;

  if(bucket_len <= 0 || object_len <= 0) return ACL_PAT_FAIL;

  redisReply *reply = doRedisCommand("SISMEMBER %b %b", bucket, bucket_len, object_str, object_len);
  int retval = !reply || reply->integer ? ACL_PAT_PASS : ACL_PAT_FAIL;
  if(reply) freeReplyObject(reply);

  return retval;
}

int s3_mark_redirected(struct acl_test *test, struct acl_pattern * ignored) {
  const char *bucket      = test->ptr+1;
  const char *bucket_end  = memchr(bucket, '/', test->len-1);
  const int  bucket_len   = bucket_end - bucket;
  const char *object_str  = bucket_end+1;
  const int  object_len   = test->len - bucket_len - 2;

  if(bucket_len <= 0 || object_len <= 0) return ACL_PAT_FAIL;

  redisReply *reply = doRedisCommand("SADD %b %b", bucket, bucket_len, object_str, object_len);
  if(reply) freeReplyObject(reply);

  return ACL_PAT_PASS;
}

// Includes +1 for null byte
#define SIG_SIZE 28

int s3_add_resign(const char *file, int line, struct proxy *px, const char *id, const char *key)
{
  // TODO: Add bucket/key/id error checking
  const char *new_auth_header_fmt = "Authorization: AWS %s:%*s";
  px->s3_auth_header = calloc(strlen(id) + strlen(new_auth_header_fmt) + SIG_SIZE + 1, sizeof(char));
  sprintf(px->s3_auth_header, new_auth_header_fmt, id, SIG_SIZE, "");
  px->s3_auth_header_len    = strlen(px->s3_auth_header);
  px->s3_auth_header_colon  = strrchr(px->s3_auth_header, ':')+1 - px->s3_auth_header;

  px->s3_key     = strdup(key);
  px->s3_key_len = strlen(px->s3_key);
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

    (*func)(data, cur_ptr, cur_end-cur_ptr);
  }
}

#define ADD_HEADER(ctx, txn, name) optionally_add_header(ctx, txn, name, sizeof(name)-1)
void optionally_add_header(HMAC_CTX *ctx, struct http_txn *txn, const char* header_name, int header_len) {
  struct hdr_ctx hdr_ctx = {.idx = 0};
  char *begin = NULL;
  char *end   = NULL;
  if(http_find_header2(header_name, header_len, txn->req.sol, &txn->hdr_idx, &hdr_ctx)) {
    begin = hdr_ctx.line + hdr_ctx.val;
    end   = begin + hdr_ctx.vlen;
  }
  // For Date, haproxy does a silly thing where it considers the ',' to be a field separator
  while(http_find_header2(header_name, header_len, txn->req.sol, &txn->hdr_idx, &hdr_ctx)) {
    end = hdr_ctx.line + hdr_ctx.val + hdr_ctx.vlen;
  }
  if(begin) {
    HMAC_Update(ctx, (unsigned char*)begin, end - begin);
  }
  HMAC_Update(ctx, (unsigned const char*)"\n", 1);
}

void make_aws_signature(char *retval, const char *key, int key_len, struct http_txn *txn) {
  struct http_msg *msg = &txn->req;

  static int loaded_engines = 0;
  if(!loaded_engines) {
    ENGINE_load_builtin_engines();
    ENGINE_register_all_complete();
    loaded_engines = 1;
  }
  static unsigned char raw_sig[SIG_SIZE];

  HMAC_CTX ctx;
  HMAC_CTX_init(&ctx);
  HMAC_Init_ex(&ctx, key, key_len, EVP_sha1(), NULL);

  HMAC_Update(&ctx, (unsigned char*)msg->sol + msg->som, msg->sl.rq.m_l);
  HMAC_Update(&ctx, (unsigned const char*)"\n", 1);

  ADD_HEADER(&ctx, txn, "Content-MD5");
  ADD_HEADER(&ctx, txn, "Content-Type");
  ADD_HEADER(&ctx, txn, "Date");

  void *header_sorter = HeaderSorter_new("x-amz-");
  for_each_header(txn, header_sorter, &HeaderSorter_add);
  HeaderSorter_update(header_sorter, &ctx);
  HeaderSorter_delete(header_sorter);
  header_sorter = NULL;

  char *rel_uri = http_get_path(txn);
  char *uri_end = msg->sol + msg->sl.rq.u + txn->req.sl.rq.u_l;
  CanonicalizeResource(&ctx, rel_uri, uri_end - rel_uri);

  unsigned int sig_len = sizeof(raw_sig);
  HMAC_Final(&ctx, raw_sig, &sig_len);
  HMAC_CTX_cleanup(&ctx);

  BIO *mem_output_stream, *b64_filter;
  BUF_MEM *output_buffer;

  b64_filter = BIO_new(BIO_f_base64());
  mem_output_stream = BIO_new(BIO_s_mem());
  b64_filter = BIO_push(b64_filter, mem_output_stream);
  BIO_write(b64_filter, raw_sig, sig_len);
  (void) BIO_flush(b64_filter);
  BIO_get_mem_ptr(b64_filter, &output_buffer);

  memcpy(retval, output_buffer->data, output_buffer->length-1);

  BIO_free_all(b64_filter);
}

int s3_resign(struct session *s, struct buffer *req, struct proxy *px) {
  // TODO: Enable support for query string signatures?

  static struct hdr_exp *exp = NULL;
  if(!exp) {
    exp = calloc(1, sizeof(struct hdr_exp));
    exp->preg    = calloc(1, sizeof(regex_t));
    exp->replace = strdup(px->s3_auth_header);
    exp->action  = ACT_REPLACE;
    regcomp((regex_t*)exp->preg, "^Authorization:.*$", REG_EXTENDED | REG_ICASE);
  }

  if(!px->s3_auth_header || !px->s3_key) {
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

  // Using exp->replace is only OK because haproxy is single-threaded, so the buffer 
  // will only serve 1 request at a time.
  make_aws_signature((char*)exp->replace + px->s3_auth_header_colon,
    px->s3_key, px->s3_key_len, txn);

  apply_filter_to_req_headers(s, req, exp);

  return 0;
}
