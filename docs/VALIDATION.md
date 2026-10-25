# Validation and boundaries

The public application/SDK is 0.1.0; the private driver candidate is 0.1.0.0.
Windows x64 is the target. Microsoft signing and returned-package qualification
remain pending. Source builds and offline tests are not substitutes for an
installed storage-path test.

Completed offline checks: all five user-mode executables compile with MSVC
W4/WX, CFG, ASLR, NX and static CRT. The normal and separate AddressSanitizer
builds pass two CTest cases: 100,024 backend/boundary assertions, including
100,000 independent arithmetic range cases, capacity checks, real memory and
regular-file read/write/flush, and 10 read-only installer identity/signature
refusal checks. Tests reject physical-device/UNC paths, DOS device names,
alternate streams and trailing-dot/space aliases. They use temporary application
image files. The recursion guard can enumerate PnP storage identities and query
owned-disk/volume metadata;
the offline tests create no virtual disk and read or write no raw disk data.
No driver was installed on the development host.

The private candidate's static-analysis record has zero unwaived findings and
one documented C28118 model waiver for an explicit zero-timeout process-object
query at DISPATCH_LEVEL, following the [documented wait contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kewaitforsingleobject).
That narrow documented exception does not replace
runtime verification or imply a defect-free driver.

The final-image Windows 10 x64 normal and targeted Driver Verifier campaigns
each passed 1,036 native checks:
exact owned-disk discovery, genuine signed authorization/refusal, 256 RAM-backed
read/write roundtrips, eight regular-file-backed roundtrips, explicit storage
flush, 64 concurrent submitted reads with 32 observed pending requests, bounded
provider timeout, abrupt owner exit and permission expiry. All pending requests
were released.

In each campaign, a separate new 64 MiB owned RAM fixture was initialized as GPT
with explicit 1 MiB partition alignment and quick-formatted as NTFS. Ordinary file creation,
write-through write, explicit flush, close/reopen and independent byte/hash
verification, rename/delete and flush/lock/dismount passed. An attempt to create
an image on the SDK's own virtual disk was specifically refused before any image
file was created. Selection required the exact product ancestry, size/sector and
volume extents, with boot/system disks excluded. This was private guest-only
qualification; the public app and examples still never autoformat a disk.

Both campaigns completed exact candidate package removal/unload, no new
minidump and restoration of the designated guest. These are bounded tests on
one controlled Windows 10 x64 guest, not a universal compatibility or production
certification claim.

The driver is not a boot/pagefile/storage replacement. The current limits are
one dynamically attached synthetic disk with fixed-medium semantics,
64 MiB..1 GiB, 512-byte sectors, 32 queued requests
and 64 KiB transfers. One backend operation is issued at a time, preserving the
write/flush order. Unsupported FUA/DPO and VERIFY requests are refused. File
providers use write-through access and explicit flushes; this is not a power-loss
or filesystem-recovery guarantee. Incomplete/late provider responses fail bounded operations;
they do not create a persistence or recovery guarantee. Application callbacks
must honor their own timeout, cancellation and non-reentrancy contract.

Windows 11, Secure Boot/HVCI, additional filesystems and image recovery,
representative storage/security filter coexistence, suspend/resume,
long-duration pressure and throughput/latency benchmarks remain separately pending. Neither Microsoft
signing nor a bounded VM pass means zero defects, universal compatibility or
equivalence to existing production virtual-storage products.

Private kernel sources, signing keys, runtime probes, lab certificates, PDBs and
CABs are excluded from public downloads. A final submission freezes the exact
qualified INF/SYS/CAT/PDB without rebuilding and verifies all bytes after CAB
extraction. Returned Microsoft-signed packages need their own installation and
runtime qualification before they become installable public bundles.
