#include "../InstallerUI/Setup.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <cstring>
int main(){
 wchar_t temp[MAX_PATH]{};if(!GetTempPathW(MAX_PATH,temp))return 1;GUID guid{};if(FAILED(CoCreateGuid(&guid)))return 1;wchar_t name[40]{};StringFromGUID2(guid,name,40);auto folder=std::filesystem::path(temp)/(std::wstring(L"DiskIdentity-")+name);std::filesystem::create_directory(folder);auto path=folder/L"identity.inf";
 struct Cleanup{std::filesystem::path file,folder;~Cleanup(){std::error_code e;std::filesystem::remove(file,e);std::filesystem::remove(folder,e);}} cleanup{path,folder};
 const std::string valid=R"INF([Version]
Signature="$Windows NT$"
Class=SCSIAdapter
ClassGuid={4D36E97B-E325-11CE-BFC1-08002BE10318}
Provider=%Provider%
CatalogFile=TitanUserBackedDisk.cat
DriverVer=10/25/2026,0.1.0.0
PnpLockdown=1
[Manufacturer]
%Provider%=Models,NTamd64.10.0...19041
[Models.NTamd64.10.0...19041]
%Device%=Install,ROOT\TitanUserBackedDisk
[Install.NT]
CopyFiles=DriverFiles
[Install.NT.Services]
AddService=TitanUserBackedDisk,0x00000002,Service
[Service]
DisplayName=%Device%
ServiceType=1
StartType=3
ErrorControl=1
ServiceBinary=%13%\TitanUserBackedDisk.sys
[DestinationDirs]
DriverFiles=13
[SourceDisksNames]
1=%Device%
[SourceDisksFiles]
TitanUserBackedDisk.sys=1
[DriverFiles]
TitanUserBackedDisk.sys
[Strings]
Provider="Titanfyre"
Device="Titan User-Backed Virtual Disk adapter"
)INF";
 auto write=[&](const std::string& text){std::ofstream f(path,std::ios::binary);f<<text;f.close();return !f.fail();};
 if(!write(valid)||!Setup::PackageMatches(path.wstring())){std::cerr<<"Positive disk identity failed Windows="<<GetLastError()<<"\n";return 1;}unsigned checks=1;
 for(const auto& change:{std::pair{"4D36E97B","4D36E97D"},std::pair{"Provider=\"Titanfyre\"","Provider=\"Different\""},std::pair{"CatalogFile=TitanUserBackedDisk.cat","CatalogFile=Other.cat"},std::pair{"Install,ROOT\\TitanUserBackedDisk","Install,ROOT\\OtherDisk"},std::pair{"AddService=TitanUserBackedDisk","AddService=OtherDisk"},std::pair{"ServiceBinary=%13%\\TitanUserBackedDisk.sys","ServiceBinary=%13%\\OtherDisk.sys"},std::pair{"ServiceBinary=%13%\\TitanUserBackedDisk.sys","ServiceBinary=C:\\OtherFolder\\TitanUserBackedDisk.sys"},std::pair{"ServiceBinary=%13%\\TitanUserBackedDisk.sys","ServiceBinary=%13%\\other\\TitanUserBackedDisk.sys"},std::pair{"TitanUserBackedDisk.sys=1","OtherDisk.sys=1"}}){auto text=valid;auto offset=text.find(change.first);if(offset==text.npos)return 1;text.replace(offset,std::strlen(change.first),change.second);if(!write(text)||Setup::PackageMatches(path.wstring())){std::cerr<<"Mutation incorrectly accepted: "<<change.first<<"\n";return 1;}++checks;}
 if(Setup::CatalogTrusted(path.wstring())||Setup::CatalogTrusted((folder/L"missing.cat").wstring()))return 1;checks+=2;
 std::cout<<checks<<" read-only installer identity/signature refusal checks passed; no installation call made.\n";
}
