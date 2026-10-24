#include "libvdi.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    std::cout << "Running libvdi test suite...\n";

    // 1. In-memory RAM disk test
    {
        auto ram = vdi::Image::create(16ULL * 1024 * 1024);
        assert(ram.ok());
        vdi::Image img = std::move(*ram);
        assert(img.isOpen());

        auto wr = img.write(0, "Test payload");
        assert(wr.ok() && *wr == 12);

        auto rd = img.read(0, 12);
        assert(rd.ok() && std::string(rd->begin(), rd->end()) == "Test payload");

        auto zeros = img.read(1024 * 1024, 512);
        assert(zeros.ok());
        for (uint8_t b : *zeros) assert(b == 0);
    }

    std::cout << "All tests passed!\n";
    return 0;
}
