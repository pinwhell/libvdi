#include "libvdi.hpp"
#include <iostream>

int main() {
    std::cout << "=== libvdi Example: Modern C++ Image ===\n";

    // Create 64 MiB dynamic image
    auto disk_res = vdi::Image::create("example.vdi", 64ULL * 1024 * 1024);
    if (!disk_res) {
        std::cerr << "Error: " << disk_res.errorMessage() << "\n";
        return 1;
    }
    vdi::Image disk = std::move(*disk_res);

    std::cout << "UUID:     " << disk.uuid() << "\n";
    std::cout << "Capacity: " << (disk.diskSize() / (1024 * 1024)) << " MiB\n";

    auto wr_res = disk.write(0, "Modern C++ payload in VirtualBox VDI format");
    if (!wr_res) return 1;
    auto read_back = disk.read(0, 43);
    if (read_back) {
        std::cout << "Read:     " << std::string(read_back->begin(), read_back->end()) << "\n";
    }

    // Auto-closed on scope exit
    return 0;
}
