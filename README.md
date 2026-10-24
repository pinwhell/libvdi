# libvdi

A lightweight, standalone C/C++ library for creating, inspecting, reading, and writing **VirtualBox Disk Images (`.vdi`)**.

---

## Acknowledgements & QEMU Credits

**libvdi** is derived directly from and heavily inspired by the VDI block driver in the **QEMU** project:

* **Original Source**: `qemu/block/vdi.c`
* **Original Author**: Stefan Weil (2009, 2012)
* **Project**: QEMU Machine Emulator and Virtualizer ([qemu.org](https://www.qemu.org))
* **License**: GNU General Public License v2 or later (`GPL-2.0-or-later`)

### How libvdi relates to QEMU

In QEMU, `block/vdi.c` is deeply integrated with QEMU's asynchronous block layer, coroutines (`qemu_co_rwlock_*`), I/O vectors (`QEMUIOVector`), QAPI visitor schema, and GLib runtime.

`libvdi` extracts the core algorithms, file format specifications, and allocation logic into a **clean, synchronous, standalone C library** with **zero external dependencies** beyond the standard C library runtime.

---

## VDI Format Specification

A Virtual Disk Image consists of three contiguous logical regions:

```
+-------------------------------------------------------------+  Offset 0
| Header (vdi_header_t: 512 bytes)                           |
+-------------------------------------------------------------+  Offset offset_bmap (0x200 / 512 B)
| Block Map (bmap)                                            |
| uint32_t bmap[blocks_in_image]                             |
| (padded to 512-byte sector boundary)                        |
+-------------------------------------------------------------+  Offset offset_data (sector aligned)
| Data Area                                                   |
| Physical Block 0 (1 MiB default)                           |
| Physical Block 1 (1 MiB default)                           |
| ...                                                         |
| Physical Block N                                            |
+-------------------------------------------------------------+
```

### 1. Header (512 bytes)
* **Signature**: `0xbeda107f` (little-endian: `7F 10 DA BE`).
* **Version**: `0x00010001` (Version 1.1).
* **Image Types**:
  * `1` = `VDI_TYPE_DYNAMIC`: Sparse image. Blocks are allocated on-demand as writes occur.
  * `2` = `VDI_TYPE_STATIC`: Pre-allocated image. Entire virtual capacity is allocated upon creation.
* **Block size**: Typically `1,048,576` bytes (1 MiB).
* **Blocks in image**: $\lceil \frac{\text{disk\_size}}{\text{block\_size}} \rceil$.
* **UUIDs**: 16-byte UUIDs for image identity, snapshot chaining, and parent links.

### 2. Block Map (`bmap`)
The block map translates logical virtual block indices to physical block indices in the data area:
$$\text{virtual\_block} = \lfloor \frac{\text{logical\_offset}}{\text{block\_size}} \rfloor$$
$$\text{offset\_in\_block} = \text{logical\_offset} \pmod{\text{block\_size}}$$

Each 32-bit entry in `bmap[virtual_block]` has three possible states:
1. `VDI_UNALLOCATED` (`0xFFFFFFFF`): Block has never been written. Reads return zeroes without accessing disk.
2. `VDI_DISCARDED` (`0xFFFFFFFE`): Block was trimmed or discarded. Reads return zeroes.
3. `< 0xFFFFFFFE`: Valid index of physical block $P$ in data region.
   $$\text{file\_offset} = \text{offset\_data} + (P \times \text{block\_size}) + \text{offset\_in\_block}$$

### 3. Data Area
Contains contiguous physical blocks appended dynamically as unallocated clusters are written.

---

## Public API Reference

The entire public interface is defined in [`include/libvdi.h`](include/libvdi.h):

### Core Functions (Primary, File-Free)
| Function | Description |
|---|---|
| `vdi_create(&out, io, size, type, blk)` | Creates and formats a VDI on **any** transport or pure RAM (pass `io=NULL`). |
| `vdi_open(&out, io, flags)` | Opens a VDI from any generic I/O transport. |
| `vdi_probe_io(io)` | Checks if a generic stream contains a valid VDI header. |
| `vdi_io_from_mem(&io, &mem_buf)` | Prepares an I/O transport for in-memory buffers (RAM disk). |
| `vdi_io_from_file(&io, path, flags, create)` | Prepares an I/O transport for filesystem files. |

### Filesystem & Memory Convenience Helpers
| Function | Description |
|---|---|
| `vdi_create_file(&out, path, size, type, blk)` | Formats a VDI directly to a file on disk. |
| `vdi_open_file(&out, path, flags)` | Opens a VDI file by filesystem path. |
| `vdi_create_mem(&out, mem, size, type, blk)` | Formats a VDI on a specific memory buffer. |
| `vdi_open_mem(&out, mem, flags)` | Opens a VDI from an in-memory buffer. |
| `vdi_probe_file(path)` | Probes a file by path. |
| `vdi_probe_buffer(buf, size)` | Probes a raw memory buffer. |

### Operations & I/O
| Function | Description |
|---|---|
| `vdi_get_info(vdi, &info)` | Retrieves disk capacity, block size, block counts, and UUID. |
| `vdi_read(vdi, offset, buf, count, &read)` | Reads data from virtual disk (sparse-aware). |
| `vdi_write(vdi, offset, buf, count, &written)`| Writes data to virtual disk (allocates on demand). |
| `vdi_flush(vdi)` | Flushes in-memory metadata (header & block map) to transport. |
| `vdi_close(vdi)` | Flushes metadata and closes handle. |
| `vdi_uuid_to_str(uuid, str)` | Formats UUID into standard canonical string. |
| `vdi_strerror(status)` | Returns human-readable error description. |

---

## Code Example

```c
#include "libvdi.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    // 1. Create a 10 GiB dynamic sparse image directly in RAM (or pass &io)
    vdi_image_t *img = NULL;
    uint64_t disk_size = 10ULL * 1024 * 1024 * 1024;
    vdi_status_t rc = vdi_create(&img, NULL, disk_size, VDI_TYPE_DYNAMIC, 0);
    if (rc != VDI_OK) {
        fprintf(stderr, "Create error: %s\n", vdi_strerror(rc));
        return 1;
    }

    // 2. Write data at 2 MiB offset (allocates cluster 2 dynamically)
    const char *msg = "Hello, Virtual Disk!";
    size_t written = 0;
    vdi_write(img, 2 * 1024 * 1024, msg, strlen(msg), &written);

    // 3. Read data back
    char buf[64] = {0};
    size_t read_bytes = 0;
    vdi_read(img, 2 * 1024 * 1024, buf, strlen(msg), &read_bytes);
    printf("Read back: %s\n", buf);

    // 4. Close image
    vdi_close(img);
    return 0;
}
```

---

## High-Level Modern C++17 Bindings (`libvdi.hpp`, Exception-Free)

For modern C++ applications, `libvdi` provides a type-safe, RAII-managed, **exception-free** (`noexcept` / `-fno-exceptions` friendly) C++ interface in namespace `vdi::`:

* **No Exceptions**: Zero use of `throw` or `try/catch`. All fallible operations return `vdi::Result<T>` or `vdi::Status`.
* **RAII Lifecycle**: `vdi::Image` automatically flushes and closes upon leaving scope.
* **Modern I/O**: Direct overloads for `std::string_view` and `std::vector<uint8_t>`.
* **Move Semantics**: Safe transfer of disk resource handles.

### C++ Example

```cpp
#include "libvdi.hpp"
#include <iostream>

int main() {
    // 1. Create a 20 GiB dynamic virtual disk (returns Result<Image>)
    auto disk_res = vdi::Image::create("my_vm.vdi", 20ULL * 1024 * 1024 * 1024);
    if (!disk_res) {
        std::cerr << "Failed to create: " << disk_res.errorMessage() << "\n";
        return 1;
    }
    vdi::Image disk = std::move(*disk_res);

    std::cout << "Image UUID: " << disk.uuid() << "\n";

    // 2. Write string view into cluster 0
    auto write_res = disk.write(0, "Operating System Bootloader Payload");
    if (!write_res) {
        std::cerr << "Write failed: " << write_res.errorMessage() << "\n";
        return 1;
    }

    // 3. Read back data as std::vector<uint8_t>
    auto read_res = disk.read(0, 512);
    if (read_res) {
        std::cout << "Read " << read_res->size() << " bytes.\n";
    }

    // 4. Sparse reading: unallocated blocks return zeroes instantly
    auto zeros = disk.read(1024 * 1024 * 1024, 4096);

    // 5. RAII automatically flushes and closes on scope exit
    return 0;
}
```

---

## Building with CMake

Add `libvdi` to your project using CMake:

```cmake
add_subdirectory(libvdi)
target_link_libraries(my_app PRIVATE vdi)
```
