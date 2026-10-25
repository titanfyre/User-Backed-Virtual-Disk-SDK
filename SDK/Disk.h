#pragma once
#include <windows.h>
#include "DiskProtocol.h"
#include "AuthorizationProtocol.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace UserDisk {
struct Request {unsigned operation=0;uint64_t id=0,offset=0;unsigned length=0;std::vector<uint8_t> data;};
// Return ERROR_SUCCESS on success, otherwise an English-readable Windows code.
// READ: fill output with exactly length bytes. WRITE/FLUSH: leave output empty.
using Backend=std::function<DWORD(const Request&,std::vector<uint8_t>& output)>;
Backend RamBackend(uint64_t capacity);
// A regular, non-reparse image file only; create=true uses CREATE_NEW and never
// overwrites an existing file. capacity must match an existing file exactly.
Backend FileBackend(const std::wstring& path,uint64_t capacity,bool create=false);
std::string Error(DWORD code);
class Session {
 struct State;std::unique_ptr<State> state_;
public:
 Session();~Session();Session(const Session&)=delete;Session& operator=(const Session&)=delete;
 std::string Authorize(std::string_view accessKey);
 std::string Start(uint64_t capacity,bool readOnly,Backend backend);
 void Stop() noexcept;
 UD_STATISTICS Status();
 std::string LastError();
 // Advanced/private diagnostics. Opening is read/write on the exact adapter.
 HANDLE NativeHandle();std::wstring NativePath();
};
}
