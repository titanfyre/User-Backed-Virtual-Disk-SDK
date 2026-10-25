# Backend contract

The disk is a bounded block device. Windows supplies filesystem behavior above
it; the application implements storage below it. The driver queues up to32
requests and issues one backend operation at a time. The provider worker executes
callbacks in that order, keeping flushes behind earlier writes. Do not call back into that same session from a
callback, and do not destroy the session while another caller still uses it.

| Request | Callback behavior on success |
| --- | --- |
| `UD_READ` | Read `length` bytes at `offset`; fill the output vector with exactly those bytes. |
| `UD_WRITE` | Store the `data` bytes at `offset`; return an empty output vector. |
| `UD_FLUSH` | Apply your backend's documented flush semantics; return an empty vector. |

The callback returns `ERROR_SUCCESS` or an appropriate Windows error code.
Offsets and lengths are validated for capacity, 512-byte alignment and a
64 KiB maximum. Requests carry opaque IDs so the driver can reject stale or
incorrect completions. The protocol transports bytes rather than user pointers.

The included memory backend is volatile. It owns an ordinary bounded C++ byte
vector; closing its session loses its contents. The file backend exclusively
opens a regular file on an existing local fixed volume, validates its exact
size, or explicitly creates a new file with `CREATE_NEW`. File access is
write-through; successful write callbacks finish their file operation before
acknowledging it, and flush callbacks use `FlushFileBuffers`. It refuses device,
UNC, parent-navigation, alternate-stream, DOS reserved and reparse paths. It
checks the opened file's actual volume extents and refuses an image on this
SDK's own virtual disk, preventing recursive block I/O. Do not create circular
dependencies in custom backends either.

Backend callbacks must finish promptly. Kernel deadlines limit pending disk
requests; they cannot terminate arbitrary application code or guarantee remote
server/file operations finish. `Stop()` signals the provider and offlines the
disk, then waits for its worker/callback to leave. If your callback blocks
indefinitely, your application's stop can also block. Add your own bounded
timeouts/cancellation to a custom network or external-storage backend.

Stopping a provider makes its virtual disk unavailable; it is not a filesystem-level
flush or dismount. For a formatted disk, close its clients and flush and
dismount that exact volume before stopping the provider. RAM contents are
discarded even after a successful dismount. An image file retains the bytes
successfully stored in it, but abrupt provider exit, expiry or interrupted
filesystem writes can still leave its filesystem inconsistent. No backend can
promise filesystem recovery merely because it implements the block callbacks.

For durable storage, specify what a successful write/flush actually means and
test loss of power/process/network independently. A callback returning success
does not make a database, encrypted store, cache or network service durable.
The supplied examples do not implement transactions, journaling, recovery,
encryption, compression, snapshots, cloud connectivity or a filesystem.

Only the explicitly owned synthetic disk is managed. The public SDK has no
physical-disk selector, generic kernel memory access or arbitrary driver-code
execution interface. `NativePath()` and `NativeHandle()` are advanced diagnostics
for the exact adapter, not required for ordinary backend integration.
