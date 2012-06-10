require 'spec_helper'

describe "Multipart operations", :slow => true do
  it "should allow multipart uploads" do
    # Minimum part size is 5MB, otherwise we'll get 400's when the multipart
    # uploader tries to POST the final assembly manifest
    test_data1 = "Testing 123: #{test_filename}"*150_000
    test_data2 = test_data1.reverse
    expect do
      test_object.multipart_upload do |uploader|
        redis_set.should include(test_filename)
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
