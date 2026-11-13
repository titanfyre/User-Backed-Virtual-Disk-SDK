# User-Backed-Virtual-Disk-SDK Rust SDK

`cargo add user-backed-virtual-disk-sdk`

Windows x64 MSVC Rust1.82+. Bundled DLL/import library; copy native DLL beside deployed executable.

```rust,no_run
use user_backed_virtual_disk_sdk::Client;
let mut client=Client::new()?;
let result=client.call("status",serde_json::json!({}))?;
# Ok::<(),Box<dyn std::error::Error>>(())
```

[API](docs/API.md). Drop closes owned session; original driver and licensing requirements unchanged.
