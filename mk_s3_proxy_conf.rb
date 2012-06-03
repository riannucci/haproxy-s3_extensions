#!/usr/bin/ruby1.9.1
require 'rubygems'
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
# NOTE: Since we're not resigning, the master id and master key are ignored
#       for now.
#

buckets = STDIN.readlines.map(&:chomp)
master, master_id, master_key  = buckets.shift.split
puts Erubis::FastEruby.new(DATA.read).result(binding)

__END__
# vim: syn=haproxy
global
  log 127.0.0.1 local0
  ulimit-n      65536
  maxconn       16384

defaults
  log global
  mode http
  option httplog
  option http-server-close
  contimeout    5000
  clitimeout    60000
  srvtimeout    60000

frontend incoming
  bind *:80

  default_backend Production

  # Only proxy s3 stuff
  block unless { hdr_end(Host) s3.amazonaws.com }

  # No operations besides HEAD, GET, DELETE, PUT
  block unless METH_GET or { method DELETE } or { method PUT }

  # No operations on Service/Bucket
  block if     { path / }

  <% buckets.each do |bucket|  %>
  acl header-<%= bucket %>  hdr(host) <%=bucket%>.s3.amazonaws.com
  acl uri-<%= bucket %>     hdr(host) s3.amazonaws.com
  acl uri-<%= bucket %>     path_beg   /<%=bucket%>/
  acl redir-<%= bucket %>   s3_already_redirected <%= bucket %>
  use_backend s3-<%= bucket %> if header-<%= bucket %> !METH_GET
  use_backend s3-<%= bucket %> if uri-<%= bucket %>    !METH_GET
  use_backend s3-<%= bucket %> if header-<%= bucket %> redir-<%= bucket %> 
  use_backend s3-<%= bucket %> if uri-<%= bucket %>    redir-<%= bucket %> 
  <% end %>
<% buckets.each do |bucket|  %>

backend s3-<%= bucket %>
  block unless { s3_mark_redirected <%= bucket %> } # will never actually block but we need to execute ACL
  server <%= bucket %> <%= bucket %>.s3.amazonaws.com
<% end %>

backend Production
  block unless { method GET }

  # remove the bucket name from URI if request didn't have it in host
  # note the beginning '.' on the ACL. This implies the presence of a bucket name.
  reqrep   ^([^\ :]*)\ /[^/]*/(.*)	\1\ /\2	if !{ hdr_end(Host) .s3.amazonaws.com }

  # Replace the host with the master bucket host
  reqirep  ^Host:\ .*$	Host:\ <%= master %>.s3.amazonaws.com

  # For now, just remove the Authorization header. Since our app theoretically only does
  # unauthenticated GET operations, we can just nuke the sucker and pass it along.
  #   s3_resign <%= master %> <%= master_id  %> <%= master_key %>
  reqidel ^Authorization:.*

  server <%= master %> <%= master %>.s3.amazonaws.com

  # Need to fix the "Name" or "Bucket" in the response xml? This would involve rewriting
  # the payload, which is not supported by haproxy (currently).
