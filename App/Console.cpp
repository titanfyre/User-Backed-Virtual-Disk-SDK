#include "../SDK/Disk.h"
#include <iostream>
#include <string>
#include <stdexcept>
int wmain(int argc,wchar_t** argv) {
    try {
        std::wcout << L"User-Backed Virtual Disk SDK\nA bounded virtual disk served by your application. No physical disk is opened.\n";
        if(argc==2 && std::wstring(argv[1])==L"--preview") { std::wcout << L"OFFLINE PREVIEW: no driver or backend opened. Try the GUI with --preview.\n"; return 0; }
        if(argc>1 && std::wstring(argv[1])==L"status") {
            UserDisk::Session disk;auto s=disk.Status();
            std::cout << "Active "<<s.active<<", pending "<<s.pending<<", reads "<<s.reads<<", writes "<<s.writes<<", timeouts "<<s.timeouts<<'\n';return 0;
        }
        if(argc!=1 && !(argc==3||argc==4)) {
            std::wcout << L"Usage: UserBackedDiskCLI.exe\n  status\n  --preview\n  memory <MiB>\n  file <MiB> <existing-image>\n  create <MiB> <new-image>\nThe image must be a regular non-reparse file of exactly that capacity.\n";return 2;
        }
        std::wstring mode=argc==1?L"memory":argv[1];unsigned long long mib=64;
        if(argc>1){std::wstring number=argv[2];if(number.empty()||number.size()>4||number.find_first_not_of(L"0123456789")!=std::wstring::npos)throw std::runtime_error("Capacity must be an integer from 64 to 1024 MiB.");mib=std::stoull(number);}
        const uint64_t capacity=mib*1024*1024;
        if(!UdValidCapacity(capacity)||mib>1024)throw std::runtime_error("Capacity must be 64..1024 MiB.");
        if(mode!=L"memory"&&mode!=L"file"&&mode!=L"create")throw std::runtime_error("Choose memory, file or create.");
        if((mode==L"memory"&&argc==4)||(mode!=L"memory"&&argc!=4))throw std::runtime_error("File mode requires an image path; memory mode does not.");
        std::cout << "SDK access key: ";std::string key;std::getline(std::cin,key);
        UserDisk::Session disk;auto error=disk.Authorize(key);SecureZeroMemory(key.data(),key.size());
        if(!error.empty())throw std::runtime_error(error);
        auto backend=mode==L"memory"?UserDisk::RamBackend(capacity):UserDisk::FileBackend(argv[3],capacity,mode==L"create");
        error=disk.Start(capacity,false,std::move(backend));if(!error.empty())throw std::runtime_error(error);
        std::cout << "Backend active. Use only the exact Titan virtual disk for your authorized tests.\nNo disk initialization, filesystem format or drive letter was automated.\nPress Enter to stop.\n";
        std::cin.get();disk.Stop();error=disk.LastError();if(!error.empty())std::cerr<<error<<'\n';return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
