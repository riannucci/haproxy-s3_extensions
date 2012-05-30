#!/usr/bin/ruby1.9.1
require 'erubis'

# Use like:
#  mk_s3_proxy_conf.rb << EOF
#  product_bucket_name production_aws_id production_key
#  staging_bucket_name staging_aws_id staging_aws_key
#  dev1_bucket_name dev1_aws_id dev1_aws_key
#  EOF
#
# You may have as many staging/dev/test buckets as you like.
#
# Queries must be issued to the proxy with a host in the form of:
#   staging.s3.amazonaws.com
#

buckets = STDIN.readlines.map(&:chomp)
master, master_id, master_key  = buckets.shift.split
puts Erubis::FastEruby.new(DATA.read).result(binding)

__END__
# vim: syn=haproxy
global

defaults 
mode http

frontend incoming
default_backend Production
<% buckets.each do |bucket|  %>
use_backend s3-<%= bucket %> if { should_pass_through_to <%= bucket %> }
<% end %>
<% buckets.each do |bucket|  %>

backend s3-<%= bucket %>
s3_mark
server <%= bucket %> <%= bucket %>.s3.amazonaws.com
<% end %>

backend Production 
s3_alter_resign_headers <%= master %> <%= master_id  %> <%= master_key %>
server <%= master %> <%= master %>.s3.amazonaws.com
