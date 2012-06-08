#!/usr/bin/ruby1.9.1
require 'rubygems'
require 'erubis'

# Use like:
#  mk_s3_proxy_conf.rb product_bucket_name production_aws_id production_key
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

ROOT = "s3.amazonaws.com"
master, master_id, master_key  = ARGV
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
  bind *:8888

  default_backend s3-rewrite

  # Only proxy s3 stuff
  block unless { hdr_end(Host) <%= ROOT %> }

  # No operations besides HEAD, GET, DELETE, PUT
  block unless METH_GET or { method DELETE } or { method PUT }

  # No operations on Service/Bucket
  block if     { path / }

  # Replace absolute URI with relative
  reqirep   ^([^\ :]*\ )\w+://[^/]*(/.*)   \1\2

  # Canonicalize bucket into URI if it's not there already
  acl canonicalized  hdr(Host) <%= ROOT %>
  reqikeep  ^Host:\s+(.*).<%= ROOT %>		unless canonicalized
  reqirep   ^([^\ :]*\ )(.*)  \1/\k1\2		unless canonicalized
  reqirep   ^(Host:\ ).*      \1<%= ROOT %>	unless canonicalized

  use_backend s3-passthrough if !METH_GET || { s3_already_redirected junk }

backend s3-passthrough
  block unless { s3_mark_redirected junk } # will never actually block but we need to execute ACL
  server pass_through <%= ROOT %>:80

backend s3-rewrite
  block unless METH_GET

  # set the bucket name in the URI to be the master
  reqrep   ^([^\ :]*\ /)[^/]*(/.*)      \1<%= master %>\2

  # Will fix Authorization header iff one exists.
  s3_resign <%= master_id  %> <%= master_key %>

  # An alternative to resigning is to just remove the Authorization header. Since
  # our app theoretically only does unauthenticated GET operations, we can just nuke
  # the sucker and pass it along.
  #reqidel ^Authorization:.*

  server redirected <%= ROOT %>:80

  # Need to fix the "Name" or "Bucket" in the response xml? This would involve rewriting
  # the payload, which is not supported by haproxy (currently).
