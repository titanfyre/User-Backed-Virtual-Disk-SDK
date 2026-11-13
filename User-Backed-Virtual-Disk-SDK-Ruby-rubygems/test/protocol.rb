require 'user_backed_virtual_disk_sdk'
client=UserBackedVirtualDiskSDKSDK::Client.new(library_path:ARGV[0]);begin
 begin;client.call('check');raise 'Invalid request accepted';rescue UserBackedVirtualDiskSDKSDK::NativeError;end
 tests=JSON.parse("[{\"operation\": \"check\", \"arguments\": {\"capacity\": \"67108864\"}, \"expected_key\": \"valid\", \"expected_value\": true}]")
 tests.each do |test|
 result=client.call(test['operation'],test['arguments']);value=result[test['expected_key']];raise 'Contract mismatch' if test.key?('expected_value') && value!=test['expected_value'];raise 'Array missing' if test.key?('minimum_length') && value.size<test['minimum_length'];end if ARGV[0]
ensure;client.close;end
