require 'spec_helper'

describe "Multipart operations" do
  it "should allow multipart uploads" do
    test_data1 = "Testing 123: #{test_filename}"
    test_data2 = test_data1.reverse
    expect do
      test_object.multipart_upload do |uploader|
        uploader.add_part(test_data1)
	uploader.should_not be_aborted
        uploader.add_part(test_data2)
      end
    end.not_to raise_error
    test_object.read.should == test_data1+test_data2

    test_object(:mode => :direct).read.should == test_data1+test_data2
    test_object(:master).should_not exist
  end
end
