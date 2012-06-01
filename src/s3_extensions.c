// vim: sw=2 sts=2 expandtab smartindent

#include <proto/s3_extensions.h>
#include <openssl/hmac.h>
#include <hiredis/hiredis.h>

int s3_already_redirected(struct acl_test *test, struct acl_pattern *pattern) {
	// TODO: do redis here
	return ACL_PAT_PASS;
}

int s3_mark_redirected(struct acl_test *test, struct acl_pattern *pattern) {
  const char *bucket      = pattern->ptr.str;
  const int  object_len   = test->len;
  const char *object_str  = test->ptr;

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
