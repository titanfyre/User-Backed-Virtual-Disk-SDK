# User-Backed-Virtual-Disk-SDK PowerShell SDK

PowerShell7.4+ Windows x64. `Install-Module UserBackedVirtualDiskSDK -Scope CurrentUser`.

```powershell
$client=Connect-UserBackedVirtualDiskSDKSDK
try { Invoke-UserBackedVirtualDiskSDKSDK -Client $client -Operation status } finally { Disconnect-UserBackedVirtualDiskSDKSDK -Client $client }
```

Use -Arguments with a hashtable matching [schema](docs/API.md). Native errors retain their Windows code through the inner exception. Matching driver installed separately.
