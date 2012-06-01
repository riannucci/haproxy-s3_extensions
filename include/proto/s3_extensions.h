#pragma once

#include <types/acl.h>
#include <types/buffers.h>
#include <types/proxy.h>
#include <types/session.h>

/**
 * True iff the given object path has already been redirected for the given
 * bucket.
 */
int s3_already_redirected(struct acl_test *test, struct acl_pattern *pattern);

/**
 * Mark an object path as being redirected for a given bucket.
 * Will always return ACL_PAT_PASS.
 */
int s3_mark_redirected(struct acl_test *test, struct acl_pattern *pattern);

/**
 * Saves the target bucket/key/id on the current proxy, to enable 
 * s3_apply_change_bucket to work.
 */
int s3_add_change_bucket(const char *file, int line, struct proxy *px, 
			 const char *bucket, const char *id, const char *key);

/**
 * Actually change the bucket for a request and resign headers.
 */
int s3_apply_change_bucket(struct session *s, struct buffer *req, 
			   struct proxy *px);
