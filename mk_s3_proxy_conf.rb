#!/usr/bin/ruby1.9.1
require 'erubis'

# Use like:
#  mk_s3_proxy_conf.rb << EOF
#  product_bucket_name production_aws_id production_key
#  staging_bucket_name
#  dev1_bucket_name
#  EOF
#
# You may have as many staging/dev/test buckets as you like.
#
# Queries must be issued to the proxy with a host in the form of:
#   staging.s3.amazonaws.com
#   dev.s3.amazonaws.com
#
# etc. This means that PUT operations will always pass through without any
# resigning necessary.
#
# For goodness, you should issue a read-only key for production. This is
# because you don't want the proxy to ever 'write' to the production bucket.
#
#

buckets = STDIN.readlines.map(&:chomp)
master, master_id, master_key  = buckets.shift.split
puts Erubis::FastEruby.new(DATA.read).result(binding)

__END__
# vim: syn=haproxy
global

defaults
mode http
contimeout    5000
clitimeout    60000
srvtimeout    60000

frontend incoming
bind *:8081

default_backend Production

# No operations besides HEAD, GET, DELETE, PUT
block unless METH_GET or { method DELETE } or { method PUT }

# No operations on Service/Bucket
block if     HTTP_URL_SLASH

# No multipart upload support
block if { url_sub ?uploads } or { url_sub uploadId= }

<% buckets.each do |bucket|  %>
use_backend s3-<%= bucket %> if { hdr_beg(host) <%= bucket %>. } !METH_GET || { hdr_beg(host) <%= bucket %>. } METH_GET { s3_already_redirected <%= bucket %> }
<% end %>
<% buckets.each do |bucket|  %>

backend s3-<%= bucket %>
s3_mark_redirected <%= bucket %>
server <%= bucket %> <%= bucket %>.s3.amazonaws.com
<% end %>

backend Production
s3_change_bucket <%= master %> <%= master_id  %> <%= master_key %>
server <%= master %> <%= master %>.s3.amazonaws.com
# Need to fix the "Name" or "Bucket" in the response xml? This would involve rewriting
# the payload, which is not supported by haproxy.
