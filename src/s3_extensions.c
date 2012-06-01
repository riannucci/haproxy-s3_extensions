// vim: sw=2 sts=2 expandtab smartindent

#include <proto/s3_extensions.h>
#include <openssl/hmac.h>
#include <hiredis/hiredis.h>

static redisContext *g_redis_ctxt = NULL;

redisReply *doRedisCommand(const char *fmt, ...) {
  va_list ap;
  int tried = 0;

retry:
  if(!g_redis_ctxt) {
    // TODO: Allow setting server/port
    g_redis_ctxt = redisConnect("127.0.0.1", 6379);
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
  px->s3_bucket = strdup(bucket);
  px->s3_id     = strdup(id);
  px->s3_key    = strdup(key);
  return 0;
}

int s3_apply_change_bucket(struct session *s, struct buffer *req, struct proxy *px) {
  return 0;
}
