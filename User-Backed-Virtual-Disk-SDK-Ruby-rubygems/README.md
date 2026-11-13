# User-Backed-Virtual-Disk-SDK Ruby SDK

`gem install user-backed-virtual-disk-sdk`

Windows x64 Ruby3.1+.

```ruby
require "user_backed_virtual_disk_sdk"
UserBackedVirtualDiskSDKSDK::Client.open { |client| p client.call("status") }
```

[API and operation schema](docs/API.md). Matching driver installed separately.
