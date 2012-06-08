require 'spec_helper'
require 'excon'

describe "PUT operations" do
  it "should retain data after I PUT it" do
    test_data = "This is test data: #{test_filename} #{Time.new}"
    expect { test_object.write(test_data) }.not_to raise_error
    test_object.read.should == test_data
  end

  it "should retain data in the correct bucket after I PUT it" do
    Redis::Set.new(bucket.name).should_not include(test_filename)
    test_object(:master).should_not exist
    test_object.should_not exist

    test_data = "This is test data: #{test_filename} #{Time.new}"
    expect { test_object.write(test_data) }.not_to raise_error

    redis_set.should include(test_filename)

    test_object(:master).should_not exist
    test_object.should exist

    test_object.read.should == test_data
  end
end
