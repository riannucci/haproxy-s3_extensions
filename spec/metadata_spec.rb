require 'spec_helper'

describe "Metadata operations" do
  it "should correctly sign a GET with a query param" do
    redis_set.should_not include(test_filename)
    test_object.should_not exist

    test_object(:master).write("This is a really lame payload #{test_filename}")
    expect { test_object.acl }.not_to raise_error # does GET ...?acl
  end

  it "should correctly sign a GET with multiple query params and values" do
    redis_set.should_not include(test_filename)
    test_object.should_not exist

    test_object(:master).write("This is a really lame payload #{test_filename}")

    # Specifically non-ordered. These should be sorted by the signer.
    query_parms = {
      "response-expires" => "Thu, 01 Dec 2012 16:00:00 GMT",
      "response-content-disposition" => "attachment; filename=#{test_filename}"
    }
    enable_monkeypatch(AWS::S3::Request, :path, :unescaped_path) do
      expect { test_object(:query => query_parms).read }.not_to raise_error
    end
  end
end

module AWS
  class S3
    class Request
      def unescaped_path  # Allows arbitrary query params
        parts = []
        parts << bucket if bucket and Client.path_style_bucket_name?(bucket)
        parts << key if key
        "/#{parts.join('/')}"
      end
    end
  end
end
