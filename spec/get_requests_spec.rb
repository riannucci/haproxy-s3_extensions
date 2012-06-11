require 'spec_helper'

describe "GET operations" do
  it "should return data from master if it hasn't been written to" do
    redis_set.should_not include(test_filename)
    test_object.should_not exist

    test_data = "This is a test file!"
    expect { test_object(:master).write(test_data) }.not_to raise_error
    test_object.read.should == test_data
  end

  it "should let you have keys with spaces, etc." do
    filename = "This is a test/thing with spaces"
    redis_set.should_not include(filename)
    test_object(:obj => filename).should_not exist

    test_data = "This is a test file!"
    expect { test_object(:master, :obj=>filename).write(test_data) }.not_to raise_error

    test_object(:obj => filename).read.should == test_data 
    test_object(:obj => filename.split('/')[0]).should_not exist
  end
end
