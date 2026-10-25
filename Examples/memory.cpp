#include "../SDK/Disk.h"
#include <iostream>
int main() { try {
    std::cout << "Serve a temporary 64 MiB virtual disk from memory. Closing this program removes its backend.\n";
    std::cout << "SDK access key: "; std::string key; std::getline(std::cin,key);
    UserDisk::Session disk;
    auto error=disk.Authorize(key); SecureZeroMemory(key.data(),key.size());
    if(error.empty()) error=disk.Start(64ull*1024*1024,false,UserDisk::RamBackend(64ull*1024*1024));
    if(!error.empty()) { std::cerr << error << '\n'; return 1; }
    std::cout << "Backend active. No formatting or drive letter is automated. Press Enter to stop.\n";
    std::cin.get();
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
