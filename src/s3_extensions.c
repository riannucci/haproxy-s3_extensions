
#include <proto/s3_extensions.h>

int s3_already_redirected(struct acl_test *test, struct acl_pattern *pattern) {
	// TODO: do redis here
	return ACL_PAT_PASS;
}

void s3_mark_bucket(const char* bucket, const char* object_path) {
	if(!bucket || !object_path) {
		// TODO: log error here
		return;
	}
	// TODO: do redis here
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
