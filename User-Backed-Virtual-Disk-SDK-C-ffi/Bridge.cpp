#include "SDK/Disk.h"
#include "WireTypes.h"
#include <map>
struct ProductSession{

std::unique_ptr<UserDisk::Session> session;
UserDisk::Session& Get(){if(!session)session=std::make_unique<UserDisk::Session>();return *session;}

Json Invoke(const std::string& op,const Json& a){

if(op=="check"){auto capacity=Identifier(a,"capacity");return {{"valid",bool(UdValidCapacity(capacity))},{"capacity",std::to_string(capacity)}};}
if(op=="check_range")return {{"valid",bool(UdValidRange(Identifier(a,"capacity"),Identifier(a,"offset"),Number(a,"length",0,UD_TRANSFER)))}};
if(op=="authorize"){CheckMessage(Get().Authorize(a.at("key").get<std::string>()));return Json::object();}
if(op=="start_ram"){auto capacity=Identifier(a,"capacity");Require(UdValidCapacity(capacity),"Invalid RAM disk capacity");CheckMessage(Get().Start(capacity,a.value("read_only",false),UserDisk::RamBackend(capacity)));return Json::object();}
if(op=="start_file"){auto capacity=Identifier(a,"capacity");Require(UdValidCapacity(capacity),"Invalid disk capacity");auto backend=UserDisk::FileBackend(Wide(a.at("path").get<std::string>()),capacity,a.value("create",false));CheckMessage(Get().Start(capacity,a.value("read_only",false),std::move(backend)));return Json::object();}
if(op=="stop"){if(session)session->Stop();return Json::object();}
if(op=="status")return EncodeWire(Get().Status());
if(op=="last_error")return {{"message",Get().LastError()}};
if(op=="native_path")return {{"path",Utf8(Get().NativePath())}};

throw SdkError(50,"Unknown operation");}
};
static Runtime<ProductSession> runtime;
extern "C" __declspec(dllexport) unsigned user_backed_virtual_disk_sdk_json_open(std::uint64_t* id)noexcept{return runtime.Open(id);}
extern "C" __declspec(dllexport) unsigned user_backed_virtual_disk_sdk_json_close(std::uint64_t id)noexcept{return runtime.Close(id);}
extern "C" __declspec(dllexport) char* user_backed_virtual_disk_sdk_json_call(std::uint64_t id,const char* request,std::uint32_t length)noexcept{return runtime.Call(id,request,length);}
extern "C" __declspec(dllexport) void user_backed_virtual_disk_sdk_json_free(char* response)noexcept{delete[] response;}
