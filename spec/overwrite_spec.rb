require 'spec_helper'

describe "Overwrite operations" do
  it "should handle overwritten data" do
    redis_set.should_not include(test_filename)
    test_object.should_not exist

    test_data = "This is a test file!"
    expect { test_object(:master).write(test_data) }.not_to raise_error
    test_object.read.should == test_data

    test_data2 = test_data+test_filename
    expect { test_object.write(test_data2) }.not_to raise_error
    test_object.read.should == test_data2

    test_object(:mode => :direct).read.should == test_data2
    test_object(:master, :mode => :direct).read.should == test_data
  end

  it "should handle deletes correctly" do
    redis_set.should_not include(test_filename)
    test_object.should_not exist

    test_data = "This is a test file!"
    expect { test_object(:master).write(test_data) }.not_to raise_error
    test_object.read.should == test_data

    test_object.delete
    test_object.should_not exist

    test_object(:mode => :direct).should_not exist
    test_object(:master, :mode => :direct).read.should == test_data
    redis_set.should include(test_filename)
  end

  it "should handle write+delete correctly" do
    redis_set.should_not include(test_filename)
    test_object.should_not exist

    test_data = "This is a test file!"
    expect { test_object(:master).write(test_data) }.not_to raise_error
    test_object.read.should == test_data

    test_data2 = test_data+test_filename
    expect { test_object.write(test_data2) }.not_to raise_error
    test_object.read.should == test_data2

    test_object(:mode => :direct).read.should == test_data2
    test_object(:master, :mode => :direct).read.should == test_data

    test_object.delete
    test_object.should_not exist

    test_object(:mode => :direct).should_not exist
    test_object(:master, :mode => :direct).read.should == test_data
  end
end
