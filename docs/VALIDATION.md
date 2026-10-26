# Validation and boundaries

Application/SDK **0.1.1**, Microsoft-signed driver **0.1.0.0**, Windows x64. The complete ZIP includes self-contained UI/console installers embedding the exact returned driver. Kernel implementation and signing material stay private.

## Signed package identity

The returned INF is byte-identical to the submission. Windows catalog hashing confirms that protected SYS content matches the qualified original image, apart from Authenticode exclusions. Microsoft kernel-policy catalog trust and exact INF/SYS membership were verified on the development host and again inside the guest. Both installers were inspected as PE resources without running host installer code; their three embedded files match the return exactly.

- Original qualified SYS SHA-256: `1FEC9E50421701037F54B9B7668DA9DAE3BB8350CDD54D128D0A7F2182BBD301`.
- Returned signed SYS SHA-256: `682b300bb4ed929c303465ad4d491803d80f1e3655a6d052dd1880ffefef79a1`.
- No kernel rebuild, catalog regeneration, lab certificate or replacement driver was used for this return.

## Actual signed Windows tests

On 2026-10-26, the exact returned image and complete console installer passed isolated **Windows 10 build 19045 x64** campaigns: normal: **1036 native checks**, targeted Driver Verifier: **1036 native checks**. Test-signing was explicitly disabled before installation. The guest's existing Secure Boot setting was preserved; Secure Boot/HVCI qualification is not claimed.

Each campaign exercised genuine signed authorization and refusal, exact owned-disk discovery, 256 RAM-backed read/write roundtrips, eight regular-file-backed roundtrips, explicit storage flush, 64 concurrent submitted reads with 32 observed pending requests, bounded provider timeout, abrupt owner exit and permission expiry. All pending requests were released.

Each also passed six guarded filesystem checks on a separate new 64 MiB owned RAM fixture: GPT with 1 MiB alignment, quick NTFS formatting, file write/flush/close/reopen and independent byte/hash comparison, rename/delete, and flush/lock/dismount. An image backed by the SDK's own virtual disk was refused before file creation. Selection required exact product ancestry, size, sectors and volume extents, excluding boot/system disks. This was private guest qualification; public apps and examples never automatically initialize or format a disk.

Offline checks pass two CTest cases: 100,024 backend/independent-range assertions and 12 read-only installer identity/signature-refusal checks. The same updated sources also pass a separate AddressSanitizer build. The private driver has zero unwaived static findings and one documented C28118 model waiver for an explicit zero-timeout process-object query at DISPATCH_LEVEL under the [documented wait contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kewaitforsingleobject).

The driver is not a boot/pagefile/storage replacement. It presents one dynamically attached disk with fixed-medium semantics, 64 MiB..1 GiB, 512-byte sectors, 32 queued requests and 64 KiB transfers. Backend operations are processed serially to preserve write/flush order. Unsupported FUA/DPO and VERIFY are refused. Write-through files and explicit flushes are not a power-loss/recovery guarantee. Callbacks must honor bounded timeout, cancellation and non-reentrancy. Custom/cloud/encrypted backends are integration possibilities rather than bundled implementations. The overflow refusal branch was not reached because Storport admitted at most 32 requests.

Restricted-token control access, administrator zero/read-only/write-only opens and a second owner were refused. Actual console installation and uninstallation passed, every owned package was removed and all product/fixture services unloaded. No new minidump was observed. The VM returned to its original powered-off baseline with all nine historical checkpoints retained. No driver, certificate or host boot/device policy was installed or changed on the development PC.

Standard Driver Verifier targeted only TitanUserBackedDisk.sys and TitanUSBTrace.sys in the signed campaigns. The independent Vdevice fixture and Windows drivers were not targeted. UI/console setups share an installation core; UI click automation is not claimed. User-mode builds use W4/WX, CFG, ASLR, NX and static CRT. Original InfVerif and submission evidence is preserved.

Windows 11, Secure Boot/HVCI, physical USB/controllers, broader storage backends/filesystems, third-party filters, suspend/resume, surprise removal, low-resource simulation and prolonged pressure/performance benchmarks remain separate qualification work. Microsoft signing is not WHQL certification, zero overhead or a guarantee of defect-free/universal compatibility.

Product-specific keys obtain signed per-handle permissions lasting at most one hour, explicitly renewable while enabled. Revocation refuses future grants; accepted permissions expire normally. No machine binding or automatic billing. Kernel sources, CABs, PDBs, private probes and signing credentials are excluded from public repositories/downloads.
