require 'spec_helper'

describe "Overwrite operations" do
  it "should not modify data in the master" do
    redis_set.should_not include(test_filename)
    test_object.should_not exist

    test_data = "This is a test file!"
    expect { test_object(:which => :master, :mode => :direct, :creds => :master).write(test_data) }.not_to raise_error
    test_object.read.should == test_data

    test_data2 = test_data+test_filename
    expect { test_object.write(test_data2) }.not_to raise_error
    test_object.read.should == test_data2

    test_object(:mode => :direct).read.should == test_data2
    test_object(:which => :master, :mode => :direct).read.should == test_data
  end
end
