require 'rubygems'
require 'active_support/core_ext'
require 'aws-sdk'
require 'ruby-debug'
require 'tempfile'

CONFIG = YAML.load_file("#{File.dirname(__FILE__)}/config.yml")

def test_filename
  example.description.titlecase.delete(' ')
end

def s3_connection(parms = {})
  parms = {:mode => :proxy, :creds => :test}.merge(parms)
  config = {:provider => :aws, :use_ssl => false}.merge(CONFIG["#{parms[:creds]}_creds"])
  config = config.merge({:proxy_uri => CONFIG['proxy']}) if parms[:mode] == :proxy

  AWS::S3.new(config)
end

def bucket_name(parms = {})
  parms = {:which => :test}.merge(parms)
  CONFIG["#{parms[:which]}_bucket"]
end

def bucket(parms = {})
  s3_connection(parms).buckets[bucket_name(parms)]
end

# Initialize the buckets
[:test, :real].each do |which|
  bucket(:which => which, :mode => :direct, :creds => :master).tap do |b|
    b.objects.with_prefix(CONFIG["obj_prefix"]).delete_all if b.exists?
  end
  s3_connection(:mode => :direct, :creds => :master).buckets.create(bucket_name(:which => which))
end

if CONFIG["start_haproxy"]
  # Config and Start up haproxy
  cfg_file = Tempfile.new('haproxy_test')
  system(
    "#{File.dirname(__FILE__)}/../mk_s3_proxy_conf.rb",
    bucket_name(:which => :real),
    CONFIG["master_creds"][:access_key_id],
    CONFIG["master_creds"][:secret_access_key],
    :out => cfg_file.path
  )

  child = fork do
    exec *["#{File.dirname(__FILE__)}/../haproxy", ("-d" if CONFIG['debug']), "-f", cfg_file.path].compact
  end
  at_exit { Process.kill(:SIGINT, child) }
end

# TODO: Set up redis
