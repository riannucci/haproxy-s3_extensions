require 'spec_helper'
require 'excon'

describe "PUT operations" do
  it "should retain data after I PUT it" do
    bucket.objects[test_filename].should_not exist
    # put data to random object
    # assert object exist && has correct data
    # assert object exists in redis
  end

  it "should retain data in the correct bucket after I PUT it" do
  end
end
