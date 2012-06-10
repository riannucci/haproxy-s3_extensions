require 'spec_helper'

describe "Metadata operations" do
  it "should correctly sign a GET with a query param" do
    redis_set.should_not include(test_filename)
    test_object.should_not exist

    test_object(:master).write("This is a really lame payload #{test_filename}")
    acl = nil
    expect { acl = test_object.acl }.not_to raise_error # does GET ...?acl
  end
end
