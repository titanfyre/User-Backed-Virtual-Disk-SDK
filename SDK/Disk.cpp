#include "Disk.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <array>
#include <cstring>
#include <utility>
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"crypt32.lib")

namespace {
struct Http {
    HINTERNET value = nullptr;
    explicit Http(HINTERNET handle) : value(handle) {}
    ~Http() { if (value) WinHttpCloseHandle(value); }
    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;
};
std::string Failure(const char* stage) {
    return std::string(stage) + " failed (Windows error " +
        std::to_string(GetLastError()) + ").";
}
bool ValidKey(std::string_view key) {
    if (key.size() != 81 || key.substr(0, 5) != "app1_" || key[37] != '_')
        return false;
    for (size_t i = 5; i != 37; ++i)
        if (!((key[i] >= '0' && key[i] <= '9') ||
              (key[i] >= 'a' && key[i] <= 'f'))) return false;
    for (size_t i = 38; i != key.size(); ++i)
        if (!((key[i] >= 'a' && key[i] <= 'z') ||
              (key[i] >= 'A' && key[i] <= 'Z') ||
              (key[i] >= '0' && key[i] <= '9') ||
               key[i] == '_' || key[i] == '-')) return false;
    return true;
}
}

namespace {
std::string RequestDriverAuthorization(std::string_view key,
    const TITAN_AUTHORIZATION_CHALLENGE& challenge,
    TITAN_AUTHORIZATION_TOKEN& token) {
    token = {};
    if (!ValidKey(key)) return "Enter a valid application access key (app1_...).";
    if (challenge.magic != TITAN_AUTHORIZATION_MAGIC ||
        challenge.version != TITAN_AUTHORIZATION_VERSION ||
        challenge.duration_seconds != 0 ||
        (challenge.product != TITAN_AUTHORIZATION_USER_BACKED_DISK))
        return "The driver returned an invalid authorization challenge.";
    const char* sku = "user-backed-virtual-disk-sdk";
    std::string nonce;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char value : challenge.nonce) {
        nonce += hex[value >> 4]; nonce += hex[value & 15];
    }
    std::string body = "{\"key\":\"" + std::string(key) +
        "\",\"sku\":\"" + sku + "\",\"nonce\":\"" + nonce + "\"}";
    Http session(WinHttpOpen(L"Titan Driver SDK/0.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value) { SecureZeroMemory(body.data(), body.size()); return Failure("HTTPS initialization"); }
    WinHttpSetTimeouts(session.value, 5000, 5000, 10000, 10000);
    Http connection(WinHttpConnect(session.value, L"licences.configcsa.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.value) { SecureZeroMemory(body.data(), body.size()); return Failure("HTTPS connection"); }
    Http request(WinHttpOpenRequest(connection.value, L"POST",
        L"/portfolio/api/v1/driver-authorize", nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value) { SecureZeroMemory(body.data(), body.size()); return Failure("Authorization request"); }
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE,
        &disabled, sizeof(disabled))) {
        SecureZeroMemory(body.data(), body.size()); return Failure("HTTPS policy");
    }
    const BOOL sent = WinHttpSendRequest(request.value,
        L"Content-Type: application/json\r\n", static_cast<DWORD>(-1),
        body.data(), static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0);
    const DWORD sendError = sent ? ERROR_SUCCESS : GetLastError();
    SecureZeroMemory(body.data(), body.size());
    if (!sent) return "Authorization request failed (Windows error " + std::to_string(sendError) + ").";
    if (!WinHttpReceiveResponse(request.value, nullptr)) return Failure("Authorization response");
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE |
        WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
        &length, WINHTTP_NO_HEADER_INDEX)) return Failure("HTTP status");
    std::string response;
    std::array<char, 1024> chunk{};
    for (;;) {
        DWORD count = 0;
        if (!WinHttpReadData(request.value, chunk.data(),
            static_cast<DWORD>(chunk.size()), &count)) return Failure("Response read");
        if (!count) break;
        if (response.size() + count > 8192) return "Authorization response is too large.";
        response.append(chunk.data(), count);
    }
    if (status != 200) {
        for (char& c : response) if (static_cast<unsigned char>(c) < 32) c = ' ';
        return "License server refused authorization (HTTP " +
            std::to_string(status) + "): " + response;
    }
    // Only extract a bounded base64 field. The kernel verifies every byte of
    // the decoded permission and its signature before enabling any producer.
    const auto field = response.find("\"grant_base64\"");
    if (field == std::string::npos) return "Missing authorization grant in server response.";
    const auto colon = response.find(':', field + 14);
    const auto start = colon == std::string::npos ? std::string::npos : response.find('"', colon + 1);
    const auto end = start == std::string::npos ? std::string::npos : response.find('"', start + 1);
    if (end == std::string::npos || end - start - 1 != 152)
        return "Invalid authorization grant encoding.";
    const std::string grant = response.substr(start + 1, end - start - 1);
    DWORD bytes = sizeof(token);
    if (!CryptStringToBinaryA(grant.c_str(), static_cast<DWORD>(grant.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
        reinterpret_cast<BYTE*>(&token), &bytes, nullptr, nullptr) ||
        bytes != sizeof(token)) {
        token = {}; return "Invalid authorization grant length or encoding.";
    }
    if (token.permission.magic != challenge.magic ||
        token.permission.version != challenge.version ||
        token.permission.product != challenge.product ||
        token.permission.duration_seconds == 0 ||
        token.permission.duration_seconds > TITAN_AUTHORIZATION_MAX_SECONDS ||
        std::memcmp(token.permission.nonce, challenge.nonce, sizeof(challenge.nonce))) {
        token = {}; return "Authorization grant does not match this driver session.";
    }
    return {};
}

} // private HTTPS implementation

#include <setupapi.h>
#include <ntddscsi.h>
#include <winioctl.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include <cfgmgr32.h>
#include <set>
#pragma comment(lib,"cfgmgr32.lib")
namespace UserDisk {
namespace {
constexpr GUID Port={0x2accfe60,0xc130,0x11d2,{0xb0,0x82,0,0xa0,0xc9,0x1e,0xfb,0x8b}};
bool Property(HDEVINFO set,SP_DEVINFO_DATA& device,DWORD property,std::vector<wchar_t>& value,DWORD kind){DWORD bytes=0,type=0;SetupDiGetDeviceRegistryPropertyW(set,&device,property,&type,nullptr,0,&bytes);if(type!=kind||bytes<2||bytes>65536||bytes%2)return false;value.assign(bytes/2+2,0);return SetupDiGetDeviceRegistryPropertyW(set,&device,property,&type,reinterpret_cast<BYTE*>(value.data()),bytes,nullptr)&&type==kind;}
bool Owned(HDEVINFO set,SP_DEVINFO_DATA& device){std::vector<wchar_t> p;if(!Property(set,device,SPDRP_HARDWAREID,p,REG_MULTI_SZ))return false;bool found=false;for(const wchar_t* s=p.data();*s;s+=wcslen(s)+1)if(!_wcsicmp(s,L"ROOT\\TitanUserBackedDisk"))found=true;return found&&Property(set,device,SPDRP_SERVICE,p,REG_SZ)&&!_wcsicmp(p.data(),L"TitanUserBackedDisk");}
std::wstring Locate(){HDEVINFO set=SetupDiGetClassDevsW(&Port,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);if(set==INVALID_HANDLE_VALUE)throw std::runtime_error("Enumerate storage adapter: "+Error(GetLastError()));struct Cleanup{HDEVINFO h;~Cleanup(){SetupDiDestroyDeviceInfoList(h);}} close{set};std::wstring path;for(DWORD n=0;;++n){SP_DEVICE_INTERFACE_DATA item{};item.cbSize=sizeof(item);if(!SetupDiEnumDeviceInterfaces(set,nullptr,&Port,n,&item)){if(GetLastError()==ERROR_NO_MORE_ITEMS)break;throw std::runtime_error("Enumerate storage interface: "+Error(GetLastError()));}DWORD bytes=0;SetupDiGetDeviceInterfaceDetailW(set,&item,nullptr,0,&bytes,nullptr);if(bytes<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||bytes>65536)throw std::runtime_error("Invalid storage interface length.");std::vector<uint8_t> buffer(bytes+2,0);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(*detail);SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);if(!SetupDiGetDeviceInterfaceDetailW(set,&item,detail,bytes,nullptr,&device))continue;if(!Owned(set,device))continue;if(!path.empty())throw std::runtime_error("More than one owned user-backed adapter is present.");path=detail->DevicePath;}if(path.empty())throw std::runtime_error("The signed User-Backed Disk driver is not installed or running. Install it and run as administrator.");return path;}
bool NodeProperty(DEVINST node,ULONG property,ULONG wanted,std::vector<wchar_t>& value){ULONG bytes=0,kind=0;auto result=CM_Get_DevNode_Registry_PropertyW(node,property,&kind,nullptr,&bytes,0);if(result==CR_NO_SUCH_VALUE)return false;if(result!=CR_BUFFER_SMALL||kind!=wanted||bytes<2||bytes>65536||bytes%2)throw std::runtime_error("Cannot establish backing-recursion identity: missing/malformed/changing device metadata (Configuration Manager "+std::to_string(result)+").");value.assign(bytes/2+2,0);result=CM_Get_DevNode_Registry_PropertyW(node,property,&kind,value.data(),&bytes,0);if(result!=CR_SUCCESS||kind!=wanted||bytes<2||bytes>65536||bytes%2)throw std::runtime_error("Device metadata changed while checking backing-recursion safety (Configuration Manager "+std::to_string(result)+").");return true;}
bool OwnAncestor(DEVINST node){DEVINST root=0;auto result=CM_Locate_DevNodeW(&root,nullptr,CM_LOCATE_DEVNODE_NORMAL);if(result!=CR_SUCCESS)throw std::runtime_error("Cannot locate the device-tree root while checking backing-recursion safety.");for(unsigned n=0;n<24;++n){if(node==root)return false;std::vector<wchar_t> service,ids;if(NodeProperty(node,CM_DRP_SERVICE,REG_SZ,service)&&!_wcsicmp(service.data(),L"TitanUserBackedDisk")&&NodeProperty(node,CM_DRP_HARDWAREID,REG_MULTI_SZ,ids)){for(const wchar_t* p=ids.data();*p;p+=wcslen(p)+1)if(!_wcsicmp(p,L"ROOT\\TitanUserBackedDisk"))return true;}DEVINST parent=0;result=CM_Get_Parent(&parent,node,0);if(result!=CR_SUCCESS)throw std::runtime_error("Device ancestry changed while checking backing-recursion safety (Configuration Manager "+std::to_string(result)+").");node=parent;}throw std::runtime_error("Device ancestry exceeds the supported backing-recursion safety bound.");}
void OrdinaryPath(const std::filesystem::path& path){for(const auto& component:path.relative_path()){const auto name=component.native();if(name.empty()||name.back()==L'.'||name.back()==L' '||name.find_first_of(L"<>\"|?*:")!=std::wstring::npos||std::any_of(name.begin(),name.end(),[](wchar_t c){return c<32;}))throw std::invalid_argument("Use ordinary local file names without streams, wildcards or trailing dots/spaces.");auto stem=name.substr(0,name.find(L'.'));while(!stem.empty()&&stem.back()==L' ')stem.pop_back();bool reserved=!_wcsicmp(stem.c_str(),L"CON")||!_wcsicmp(stem.c_str(),L"PRN")||!_wcsicmp(stem.c_str(),L"AUX")||!_wcsicmp(stem.c_str(),L"NUL")||!_wcsicmp(stem.c_str(),L"CONIN$")||!_wcsicmp(stem.c_str(),L"CONOUT$")||!_wcsicmp(stem.c_str(),L"CLOCK$");if(stem.size()==4&&(!_wcsnicmp(stem.c_str(),L"COM",3)||!_wcsnicmp(stem.c_str(),L"LPT",3))){auto c=stem[3];reserved=reserved||(c>=L'1'&&c<=L'9')||c==0xb9||c==0xb2||c==0xb3;}if(reserved)throw std::invalid_argument("DOS device aliases cannot be used as an image file.");}}
std::set<DWORD> OwnDiskNumbers(){constexpr GUID disk={0x53f56307,0xb6bf,0x11d0,{0x94,0xf2,0,0xa0,0xc9,0x1e,0xfb,0x8b}};HDEVINFO set=SetupDiGetClassDevsW(&disk,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);if(set==INVALID_HANDLE_VALUE)throw std::runtime_error("Enumerate backing-recursion boundary: "+Error(GetLastError()));struct Cleanup{HDEVINFO h;~Cleanup(){SetupDiDestroyDeviceInfoList(h);}} cleanup{set};std::set<DWORD> numbers;for(DWORD i=0;;++i){SP_DEVICE_INTERFACE_DATA item{};item.cbSize=sizeof(item);if(!SetupDiEnumDeviceInterfaces(set,nullptr,&disk,i,&item)){if(GetLastError()==ERROR_NO_MORE_ITEMS)break;throw std::runtime_error("Read backing-recursion inventory: "+Error(GetLastError()));}DWORD bytes=0;SetupDiGetDeviceInterfaceDetailW(set,&item,nullptr,0,&bytes,nullptr);if(bytes<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||bytes>65536)throw std::runtime_error("Invalid backing-recursion inventory length.");std::vector<uint8_t> buffer(bytes+2,0);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(*detail);SP_DEVINFO_DATA node{};node.cbSize=sizeof(node);if(!SetupDiGetDeviceInterfaceDetailW(set,&item,detail,bytes,nullptr,&node))throw std::runtime_error("Read virtual disk identity: "+Error(GetLastError()));if(!OwnAncestor(node.DevInst))continue;HANDLE h=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Open exact owned disk metadata: "+Error(GetLastError()));STORAGE_DEVICE_NUMBER number{};DWORD got=0;BOOL ok=DeviceIoControl(h,IOCTL_STORAGE_GET_DEVICE_NUMBER,nullptr,0,&number,sizeof(number),&got,nullptr);DWORD error=ok?0:GetLastError();CloseHandle(h);if(!ok||got<sizeof(number)||number.DeviceType!=FILE_DEVICE_DISK)throw std::runtime_error("Validate owned disk number: "+Error(error?error:ERROR_INVALID_DATA));numbers.insert(number.DeviceNumber);}return numbers;}
void CheckBackingVolume(const std::wstring& path,HANDLE source=INVALID_HANDLE_VALUE){auto own=OwnDiskNumbers();if(own.empty())return;std::vector<wchar_t> mount(32768,0),name(32768,0);std::wstring volume;if(source!=INVALID_HANDLE_VALUE){DWORD size=GetFinalPathNameByHandleW(source,name.data(),static_cast<DWORD>(name.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_GUID);if(!size||size>=name.size())throw std::runtime_error("Resolve opened image volume: "+Error(GetLastError()));std::wstring full=name.data();auto end=full.find(L"}\\");if(end==std::wstring::npos)throw std::invalid_argument("The opened image has no canonical local volume identity.");volume=full.substr(0,end+2);}else{if(!GetVolumePathNameW(path.c_str(),mount.data(),static_cast<DWORD>(mount.size()))||!GetVolumeNameForVolumeMountPointW(mount.data(),name.data(),static_cast<DWORD>(name.size())))throw std::runtime_error("Resolve backing volume identity: "+Error(GetLastError()));volume=name.data();}if(volume.rfind(L"\\\\?\\Volume{",0)!=0||volume.empty()||volume.back()!=L'\\')throw std::invalid_argument("The backing volume does not have a canonical local volume identity.");volume.pop_back();HANDLE h=CreateFileW(volume.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Open backing volume metadata: "+Error(GetLastError()));struct File{HANDLE h;~File(){CloseHandle(h);}} close{h};std::vector<uint8_t> bytes(1024*1024,0);DWORD got=0;if(!DeviceIoControl(h,IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,nullptr,0,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)||got<offsetof(VOLUME_DISK_EXTENTS,Extents))throw std::runtime_error("Query backing volume extents: "+Error(GetLastError()));auto extents=reinterpret_cast<const VOLUME_DISK_EXTENTS*>(bytes.data());if(!extents->NumberOfDiskExtents||extents->NumberOfDiskExtents>(got-offsetof(VOLUME_DISK_EXTENTS,Extents))/sizeof(DISK_EXTENT))throw std::invalid_argument("Invalid backing volume extent list.");for(DWORD i=0;i<extents->NumberOfDiskExtents;++i)if(own.count(extents->Extents[i].DiskNumber))throw std::invalid_argument("An image cannot be stored on this SDK's own virtual disk; that would create recursive block I/O.");}
std::unique_ptr<UD_MESSAGE> Message(unsigned command){auto m=std::make_unique<UD_MESSAGE>();m->magic=UD_MAGIC;m->version=UD_VERSION;m->command=command;return m;}
DWORD Call(HANDLE device,UD_MESSAGE& m,DWORD timeout=10000,HANDLE stop=nullptr){HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!event)return GetLastError();struct Event{HANDLE h;~Event(){CloseHandle(h);}} close{event};OVERLAPPED ov{};ov.hEvent=event;DWORD bytes=0;BOOL ok=DeviceIoControl(device,IOCTL_MINIPORT_PROCESS_SERVICE_IRP,&m,sizeof(m),&m,sizeof(m),&bytes,&ov);if(!ok){DWORD error=GetLastError();if(error!=ERROR_IO_PENDING)return error;HANDLE events[2]={event,stop};DWORD waited=WaitForMultipleObjects(stop?2u:1u,events,FALSE,timeout);if(waited!=WAIT_OBJECT_0){DWORD failure=waited==WAIT_TIMEOUT?ERROR_TIMEOUT:waited==WAIT_OBJECT_0+1?ERROR_OPERATION_ABORTED:GetLastError();CancelIoEx(device,&ov);GetOverlappedResult(device,&ov,&bytes,TRUE);return failure;}if(!GetOverlappedResult(device,&ov,&bytes,FALSE))return GetLastError();}if(bytes!=sizeof(m)||m.magic!=UD_MAGIC||m.version!=UD_VERSION||m.length>UD_TRANSFER)return ERROR_INVALID_DATA;return ERROR_SUCCESS;}
}
std::string Error(DWORD code){char* value=nullptr;FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),reinterpret_cast<char*>(&value),0,nullptr);std::string text=value?value:"Windows operation failed";if(value)LocalFree(value);while(!text.empty()&&(text.back()=='\n'||text.back()=='\r'))text.pop_back();return text+" (Windows error "+std::to_string(code)+")";}
struct Session::State {HANDLE device=INVALID_HANDLE_VALUE,stop=nullptr;std::wstring path;std::mutex mutex,errorMutex;std::thread worker;std::atomic<bool> stopping{false};std::string failure;};
Session::Session():state_(std::make_unique<State>()){state_->stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!state_->stop)throw std::runtime_error("Create provider stop event: "+Error(GetLastError()));}
Session::~Session(){Stop();if(state_->device!=INVALID_HANDLE_VALUE)CloseHandle(state_->device);CloseHandle(state_->stop);}
HANDLE Session::NativeHandle(){if(state_->device==INVALID_HANDLE_VALUE){state_->path=Locate();state_->device=CreateFileW(state_->path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);if(state_->device==INVALID_HANDLE_VALUE)throw std::runtime_error("Open owned adapter: "+Error(GetLastError()));}return state_->device;}
std::wstring Session::NativePath(){NativeHandle();return state_->path;}
std::string Session::Authorize(std::string_view key){std::lock_guard<std::mutex> lock(state_->mutex);try{auto m=Message(UD_CHALLENGE);DWORD error=Call(NativeHandle(),*m);if(error)return "Driver challenge: "+Error(error);if(m->length!=sizeof(TITAN_AUTHORIZATION_CHALLENGE))return "Invalid driver challenge size.";TITAN_AUTHORIZATION_CHALLENGE challenge{};std::memcpy(&challenge,m->data,sizeof(challenge));TITAN_AUTHORIZATION_TOKEN token{};auto message=RequestDriverAuthorization(key,challenge,token);if(!message.empty())return message;m=Message(UD_AUTHORIZE);m->length=sizeof(token);std::memcpy(m->data,&token,sizeof(token));error=Call(state_->device,*m);SecureZeroMemory(&token,sizeof(token));SecureZeroMemory(m->data,sizeof(token));return error?"Driver permission: "+Error(error):std::string{};}catch(const std::exception& e){return e.what();}}
std::string Session::Start(uint64_t capacity,bool readOnly,Backend backend){std::lock_guard<std::mutex> lock(state_->mutex);if(!UdValidCapacity(capacity)||!backend)return "Use a 512-byte-aligned capacity from 64 MiB to 1 GiB and a valid backend.";if(state_->worker.joinable())return "Stop the previous provider before starting another disk.";try{auto m=Message(UD_START);m->capacity=capacity;m->readonly=readOnly;DWORD error=Call(NativeHandle(),*m);if(error)return "Start owned virtual disk: "+Error(error);state_->stopping=false;ResetEvent(state_->stop);{std::lock_guard<std::mutex> e(state_->errorMutex);state_->failure.clear();}state_->worker=std::thread([this,capacity,backend=std::move(backend)]{
  try{while(!state_->stopping){auto request=Message(UD_POLL);DWORD e=Call(state_->device,*request,INFINITE,state_->stop);if(e==ERROR_RETRY)continue;if(e){if(!state_->stopping){std::lock_guard<std::mutex> lockError(state_->errorMutex);state_->failure="Receive backend request: "+Error(e);}break;}if(state_->stopping)break;
   if(request->operation<UD_READ||request->operation>UD_FLUSH||(request->operation!=UD_FLUSH&&!UdValidRange(capacity,request->offset,request->length))){std::lock_guard<std::mutex> lockError(state_->errorMutex);state_->failure="Driver returned an invalid backend request.";break;}
   Request input;input.operation=request->operation;input.id=request->id;input.offset=request->offset;input.length=request->length;if(input.operation==UD_WRITE)input.data.assign(request->data,request->data+request->length);std::vector<uint8_t> output;DWORD result=ERROR_GEN_FAILURE;std::string exceptionDetail;try{result=backend(input,output);}catch(const std::exception& ex){exceptionDetail=std::string(" Backend exception: ")+ex.what();}catch(...){exceptionDetail=" Backend raised an unknown exception.";}
   if(!result&&output.size()!=(input.operation==UD_READ?input.length:0))result=ERROR_INVALID_DATA;
   if(result){std::lock_guard<std::mutex> errorLock(state_->errorMutex);state_->failure="Backend operation "+std::to_string(input.operation)+" at byte "+std::to_string(input.offset)+": "+Error(result)+exceptionDetail;}
   auto response=Message(UD_COMPLETE);response->generation=request->generation;response->id=request->id;response->result=result;if(!result&&input.operation==UD_READ){response->length=static_cast<unsigned>(output.size());std::memcpy(response->data,output.data(),output.size());}e=Call(state_->device,*response);if(e){if(!state_->stopping){std::lock_guard<std::mutex> lockError(state_->errorMutex);state_->failure="Complete backend request: "+Error(e);}break;}
  }}catch(const std::exception& exception){std::lock_guard<std::mutex> errorLock(state_->errorMutex);state_->failure=std::string("Provider worker: ")+exception.what();}catch(...){std::lock_guard<std::mutex> errorLock(state_->errorMutex);state_->failure="Provider worker raised an unknown exception.";}
  if(!state_->stopping){try{auto stop=Message(UD_STOP);(void)Call(state_->device,*stop);}catch(...){}}
 });return {};}catch(const std::exception& e){auto m=Message(UD_STOP);if(state_->device!=INVALID_HANDLE_VALUE)(void)Call(state_->device,*m);return e.what();}}
void Session::Stop() noexcept{std::lock_guard<std::mutex> lock(state_->mutex);state_->stopping=true;SetEvent(state_->stop);if(state_->device!=INVALID_HANDLE_VALUE){try{auto m=Message(UD_STOP);(void)Call(state_->device,*m);}catch(...){}}if(state_->worker.joinable())state_->worker.join();}
UD_STATISTICS Session::Status(){std::lock_guard<std::mutex> lock(state_->mutex);auto m=Message(UD_STATUS);DWORD e=Call(NativeHandle(),*m);if(e)throw std::runtime_error("Read disk state: "+Error(e));UD_STATISTICS stats{};if(m->length!=sizeof(stats))throw std::runtime_error("Invalid disk status size.");std::memcpy(&stats,m->data,sizeof(stats));if(stats.version!=UD_VERSION||stats.size!=sizeof(stats)||stats.pending>UD_PENDING||stats.active>1)throw std::runtime_error("Invalid disk status fields.");return stats;}
std::string Session::LastError(){std::lock_guard<std::mutex> lock(state_->errorMutex);return state_->failure;}
Backend RamBackend(uint64_t capacity){if(!UdValidCapacity(capacity))throw std::invalid_argument("Invalid RAM disk capacity.");auto image=std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(capacity),uint8_t{0});return [image,capacity](const Request& r,std::vector<uint8_t>& out)->DWORD{out.clear();if(r.operation==UD_FLUSH)return ERROR_SUCCESS;if(!UdValidRange(capacity,r.offset,r.length))return ERROR_INVALID_PARAMETER;if(r.operation==UD_READ)out.assign(image->data()+r.offset,image->data()+r.offset+r.length);else if(r.operation==UD_WRITE&&r.data.size()==r.length)std::memcpy(image->data()+r.offset,r.data.data(),r.length);else return ERROR_INVALID_PARAMETER;return ERROR_SUCCESS;};}
Backend FileBackend(const std::wstring& path,uint64_t capacity,bool create){if(!UdValidCapacity(capacity)||path.empty()||path.find(L"\\\\")!=std::wstring::npos||path.find(L"GLOBALROOT")!=std::wstring::npos||path.find(L"..")!=std::wstring::npos)throw std::invalid_argument("Choose a local regular image file; device/UNC/parent paths are not supported.");if(path.size()>=2&&path[1]==L':'&&(path.size()<3||(path[2]!=L'\\'&&path[2]!=L'/')))throw std::invalid_argument("Drive-relative image paths are not supported; choose an absolute or ordinary relative file path.");OrdinaryPath(std::filesystem::path(path));std::error_code error;auto absolute=std::filesystem::absolute(path,error);if(error||!absolute.has_filename())throw std::invalid_argument("Invalid image file path.");absolute=absolute.lexically_normal();absolute.make_preferred();OrdinaryPath(absolute);if(absolute.native().size()<3||absolute.native()[1]!=L':'||absolute.native().find(L':',2)!=std::wstring::npos)throw std::invalid_argument("Only ordinary local drive paths are accepted; alternate streams are not supported.");if(absolute.wstring().size()>32760||path.find(L'\0')!=std::wstring::npos)throw std::invalid_argument("Invalid image file path length or embedded terminator.");wchar_t volume[MAX_PATH]{};if(!GetVolumePathNameW(absolute.c_str(),volume,MAX_PATH)||GetDriveTypeW(volume)!=DRIVE_FIXED)throw std::invalid_argument("Back the image only on a local fixed volume.");auto parent=absolute.parent_path();for(auto p=parent;!p.empty()&&p!=p.root_path();p=p.parent_path()){DWORD attrs=GetFileAttributesW(p.c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT))throw std::invalid_argument("Image parent directories must be existing local non-reparse directories.");}DWORD attrs=GetFileAttributesW(absolute.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))throw std::invalid_argument("An image must be a regular non-reparse file.");CheckBackingVolume(absolute.wstring());HANDLE h=CreateFileW(absolute.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,create?CREATE_NEW:OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH|FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Open image: "+Error(GetLastError()));struct File{HANDLE h=INVALID_HANDLE_VALUE;~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};File opened{h};auto file=std::make_shared<File>();file->h=opened.h;opened.h=INVALID_HANDLE_VALUE;BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};if(GetFileType(h)!=FILE_TYPE_DISK||!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||!GetFileSizeEx(h,&size))throw std::runtime_error("The image handle is not a regular disk file.");std::vector<wchar_t> resolved(32768,0);DWORD finalBytes=GetFinalPathNameByHandleW(h,resolved.data(),static_cast<DWORD>(resolved.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);if(!finalBytes||finalBytes>=resolved.size())throw std::runtime_error("Resolve final image identity: "+Error(GetLastError()));std::wstring final=resolved.data();if(final.rfind(L"\\\\?\\",0)==0)final.erase(0,4);if(_wcsicmp(final.c_str(),absolute.c_str()))throw std::invalid_argument("The resolved image path differs from the selected path; aliases and redirected ancestors are not accepted.");CheckBackingVolume(final,h);if(create){LARGE_INTEGER end{};end.QuadPart=static_cast<LONGLONG>(capacity);if(!SetFilePointerEx(h,end,nullptr,FILE_BEGIN)||!SetEndOfFile(h)||!FlushFileBuffers(h))throw std::runtime_error("Initialize image: "+Error(GetLastError()));}else if(static_cast<uint64_t>(size.QuadPart)!=capacity)throw std::invalid_argument("Existing image size must exactly match the requested disk capacity.");
 return [file,capacity](const Request& r,std::vector<uint8_t>& out)->DWORD{out.clear();if(r.operation==UD_FLUSH)return FlushFileBuffers(file->h)?ERROR_SUCCESS:GetLastError();if(!UdValidRange(capacity,r.offset,r.length))return ERROR_INVALID_PARAMETER;LARGE_INTEGER offset{};offset.QuadPart=static_cast<LONGLONG>(r.offset);if(!SetFilePointerEx(file->h,offset,nullptr,FILE_BEGIN))return GetLastError();DWORD bytes=0;if(r.operation==UD_READ){out.resize(r.length);if(!ReadFile(file->h,out.data(),r.length,&bytes,nullptr))return GetLastError();}else if(r.operation==UD_WRITE&&r.data.size()==r.length){if(!WriteFile(file->h,r.data.data(),r.length,&bytes,nullptr))return GetLastError();}else return ERROR_INVALID_PARAMETER;return bytes==r.length?ERROR_SUCCESS:ERROR_HANDLE_EOF;};
}
}

