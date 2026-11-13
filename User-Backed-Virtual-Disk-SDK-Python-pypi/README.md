# User-Backed-Virtual-Disk-SDK Python SDK

`pip install user-backed-virtual-disk-sdk`

Windows x64 Python3.10+. No driver is installed by this package.

```python
from user_backed_virtual_disk_sdk import Client
with Client() as client:
    info = client.call("status")
```

[Operation schema and API](docs/API.md). Check errors and the product licensing state before device creation.
