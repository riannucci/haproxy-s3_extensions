// vim: sw=2 sts=2 expandtab smartindent

#include <proto/s3_extensions.h>
#include <proto/proto_http.h>
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

int s3_add_change_bucket(const char *file, int line, struct proxy *px, const char *bucket, const char *id, const char *key)
{
  // TODO: Add bucket/key/id error checking
  const char *new_header_fmt = "Host: %s.s3.amazonaws.com\r\n";
  char *new_header = malloc(sizeof(char) * (strlen(bucket) + strlen(new_header_fmt)));
    sprintf(new_header, new_header_fmt, bucket);
    px->s3_host_header     = strdup(new_header);
    px->s3_host_header_len = strlen(px->s3_host_header);
  free(new_header);

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

void add_CanonicalizedAmzHeaders(struct http_txn *txn, HMAC_CTX *ctx) {
  // Collect all headers which begin with "x-amz-". Lowercase comparison.
  // Sort (lcased) headers.
  // For each header:
  //   Collect all values. Sort values.
  //   Emit:
  //     lcase(header):<value0>[,<value1>]*"\n"
}

void add_CanonicalizedResource(struct http_txn *txn, const char* bucket, HMAC_CTX *ctx) {
  // Bucket can either be in Host, or it can be the beginning of URI (if Host is s3.amazonaws.com).

  // "/" Bucket URI< up to query string. excludes bucket, if present >

  // Query params sorted by param name and interleaved with &, prepend with ?.
  // Values must be urldecoded (but haproxy has inplace url_decode. #(@$ YES!):
  //   acl, lifecycle, location, logging, notification, partNumber, policy, requestPayment, torrent, uploadId, uploads, versionId, versioning, versions, website
  //   response-content-type, response-content-language, response-expires, response-cache-control, response-content-disposition, response-content-encoding
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

  struct hdr_ctx hdr_ctx = {.idx = 0};
  int ret = http_find_header2("Content-MD5", 11, msg->sol, &txn->hdr_idx, &hdr_ctx);
  if(ret) {
    HMAC_Update(&ctx, (unsigned char*)hdr_ctx.line+hdr_ctx.val, hdr_ctx.vlen);
  }
  HMAC_Update(&ctx, (unsigned const char*)"\n", 1);

  hdr_ctx.idx = 0;
  ret = http_find_header2("Content-Type", 12, msg->sol, &txn->hdr_idx, &hdr_ctx);
  if(ret) {
    HMAC_Update(&ctx, (unsigned char*)hdr_ctx.line+hdr_ctx.val, hdr_ctx.vlen);
  }
  HMAC_Update(&ctx, (unsigned const char*)"\n", 1);

  hdr_ctx.idx = 0;
  ret = http_find_header2("Date", 4, msg->sol, &txn->hdr_idx, &hdr_ctx);
  if(ret) {
    HMAC_Update(&ctx, (unsigned char*)hdr_ctx.line+hdr_ctx.val, hdr_ctx.vlen);
  }
  HMAC_Update(&ctx, (unsigned const char*)"\n", 1);

  add_CanonicalizedAmzHeaders(txn, &ctx);
  add_CanonicalizedResource(txn, px->s3_bucket, &ctx);

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

int s3_apply_change_bucket(struct session *s, struct buffer *req, struct proxy *px) {
  // TODO: Enable support for query string signatures?
  // TODO: Enable support for anonymous GETs? (if no Authorization header, then pass through)

  if (!px->s3_bucket || !px->s3_host_header || !px->s3_auth_header || !px->s3_key) {
    printf("In s3_apply_change_bucket but have null config fields?");
    return 0;
  }

  struct http_txn *txn = &s->txn;
  struct http_msg *msg = &txn->req;

  int ret = 0;

  // First nuke Host and then add the new one back.
  struct hdr_ctx ctx = {.idx = 0};
  ret = http_find_header2("Host", 4, txn->req.sol, &txn->hdr_idx, &ctx);
  if(!ret) {
    printf("That's impossible! No Host???");
    // TODO: This is actually wrongish. S3 allows you to do:
    //  GET /bucket/object_name
    // with
    //  Host: s3.amazonaws.com
    // This should be fixed
    return 0;
  } else {
    http_remove_header2(msg, req, &txn->hdr_idx, &ctx);
  }
  ret = http_header_add_tail2(req, msg, &txn->hdr_idx, px->s3_host_header, px->s3_host_header_len);
  if(ret < 0) {
    printf("Couldn't add Host header?");
    return 0;
  }

  // Now, generate a new signature for AWS
  char *signature = make_aws_signature(px->s3_key, txn, px);

  // Finally, replace the Authorization header.
  ctx.idx = 0;
  ret = http_find_header2("Authorization", 4, txn->req.sol, &txn->hdr_idx, &ctx);
  if(!ret) {
    printf("That's impossible! No Authorization???");
    return 0;
  } else {
    http_remove_header2(msg, req, &txn->hdr_idx, &ctx);
  }

  char *new_auth_header = malloc(sizeof(char) * px->s3_auth_header_len);
  sprintf(new_auth_header, px->s3_auth_header, signature);
  free(signature);
  ret = http_header_add_tail2(req, msg, &txn->hdr_idx, new_auth_header, strlen(new_auth_header));
  free(new_auth_header);
  if(ret < 0) {
    printf("Couldn't add Authorization header?");
    return 0;
  }

  return 0;
}
