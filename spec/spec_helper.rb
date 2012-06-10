require 'rubygems'
begin_time = Time.now

require 'active_support/core_ext'
require 'aws-sdk'
require 'redis'
require 'redis/objects'
require 'redis/set'
require 'ruby-debug'
require 'socket'
require 'tempfile'
require 'timeout'

##### Definitions of helper methods
def parallel_iter(*arrays)
  arrays.map(&:size).max.times do
    yield arrays.map(&:shift)
  end
end

def named_positional(options, defaults)
  parallel_iter(defaults.to_a, options) do |parm, opt|
    break if !opt
    if opt.is_a?(Hash)
      defaults = defaults.merge(opt)
    else
      defaults[parm[0]] = opt
    end
  end
  defaults
end

def test_filename
  example.description.titlecase.delete(' ')
end

def s3_connection(parms={})
  parms[:creds] ||= parms[:which] || :test
  parms[:mode]  ||= parms[:which] == :master ? :direct : :proxy
  config = {provider: :aws, use_ssl: false}.merge(CONFIG["#{parms[:creds]}_creds"])
  config = config.merge(proxy_uri: CONFIG['proxy']) if parms[:mode] == :proxy

  AWS::S3.new(config)
end

def bucket_name(*options)
  parms = named_positional(options, which: :test)
  CONFIG["#{parms[:which]}_bucket"]
end

def bucket(*options)
  parms = named_positional(options, which: :test)
  s3_connection(parms).buckets[bucket_name(parms)]
end

def test_object(*options)
  parms = named_positional(options, which: :test, obj: test_filename)
  bucket(parms).objects[parms[:obj]]
end

def redis_set(which = :test)
  Redis::Set.new(bucket_name(which))
end

##### Initialization of resources
# Parse config
CONFIG = YAML.load_file("#{File.dirname(__FILE__)}/config.yml")
CONFIG['debug'] = (ENV['DEBUG']=='1') || CONFIG['debug']

# Generate haproxy config and start
if CONFIG["start_haproxy"]
  # Config and Start up haproxy
  cfg_file = Tempfile.new('haproxy_test')
  system(
    "#{File.dirname(__FILE__)}/../mk_s3_proxy_conf.rb",
    bucket_name(:master),
    CONFIG["master_creds"][:access_key_id],
    CONFIG["master_creds"][:secret_access_key],
    :out => cfg_file.path
  )

  child = fork do
    exec *["#{File.dirname(__FILE__)}/../haproxy", ("-d" if CONFIG['debug']), "-f", cfg_file.path].compact
  end
  at_exit { Process.kill(:SIGINT, child) }
end

# Connect redis
Redis.current = Redis.new(:host => '127.0.0.1', :port => 6379)

# Initialize the buckets
[:test, :master].each do |which|
  bucket(which, mode: :direct, creds: :master).tap do |b|
    b.objects.with_prefix(CONFIG["obj_prefix"]).delete_all if b.exists?
  end
  s3_connection(:mode => :direct, :creds => :master).buckets.create(bucket_name(which))
end

# Initialize Redis
Redis.current.del bucket_name

if CONFIG["fast_tests_only"]
  RSpec.configure do |c|
    c.filter_run_excluding :slow => true
  end
end

# Make sure that haproxy is ready to rock
if CONFIG["start_haproxy"]
  Timeout.timeout(5) do
    sleep 0.1
    TCPSocket.new('localhost', 8888).close rescue retry
  end
end

puts "Initialization took #{Time.now - begin_time} seconds"
