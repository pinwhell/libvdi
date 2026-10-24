/**
 * @file libvdi.hpp
 * @brief High-level modern C++17 bindings for libvdi without exceptions.
 *
 * Provides a type-safe, RAII-managed, completely non-throwing (noexcept /
 * -fno-exceptions friendly) C++ interface using explicit Result<T> and Status types.
 *
 * ============================================================================
 * ACKNOWLEDGEMENTS & QEMU CREDITS
 * ============================================================================
 * Underpinned by libvdi, which is derived directly from QEMU's VirtualBox VDI
 * driver (qemu/block/vdi.c) by Stefan Weil and the QEMU Project contributors.
 * License: GPL-2.0-or-later
 */

#ifndef LIBVDI_HPP
#define LIBVDI_HPP

#include "libvdi.h"
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstddef>

namespace vdi {

/**
 * @brief Type of VDI image allocation.
 */
enum class ImageType : uint32_t {
    Dynamic = VDI_TYPE_DYNAMIC, /**< Sparse, grow-on-write image. */
    Static  = VDI_TYPE_STATIC   /**< Fixed, pre-allocated image. */
};

/**
 * @brief File access permissions.
 */
enum class OpenMode : int {
    ReadOnly  = VDI_OPEN_READONLY,  /**< Read-only mode. */
    ReadWrite = VDI_OPEN_READWRITE  /**< Read and write access. */
};

/**
 * @brief Non-throwing status return type.
 */
class Status {
public:
    constexpr Status(vdi_status_t code = VDI_OK) noexcept : code_(code) {}

    [[nodiscard]] constexpr bool ok() const noexcept { return code_ == VDI_OK; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return ok(); }
    [[nodiscard]] constexpr vdi_status_t code() const noexcept { return code_; }
    [[nodiscard]] const char *message() const noexcept { return vdi_strerror(code_); }

private:
    vdi_status_t code_;
};

/**
 * @brief Non-throwing outcome wrapper containing either a value of type T or a failure Status.
 */
template <typename T>
class Result {
public:
    /** Construct a successful result */
    Result(T value) noexcept : value_(std::move(value)), status_(VDI_OK) {}

    /** Construct a failed result */
    Result(vdi_status_t status) noexcept : value_{}, status_(status) {}
    Result(Status status) noexcept : value_{}, status_(status.code()) {}

    [[nodiscard]] bool ok() const noexcept { return status_ == VDI_OK; }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const T &value() const noexcept { return value_; }
    [[nodiscard]] T &value() noexcept { return value_; }

    [[nodiscard]] const T *operator->() const noexcept { return &value_; }
    [[nodiscard]] T *operator->() noexcept { return &value_; }

    [[nodiscard]] const T &operator*() const noexcept { return value_; }
    [[nodiscard]] T &operator*() noexcept { return value_; }

    [[nodiscard]] vdi_status_t status() const noexcept { return status_; }
    [[nodiscard]] const char *errorMessage() const noexcept { return vdi_strerror(status_); }

private:
    T value_;
    vdi_status_t status_;
};

/**
 * @brief Metadata information about a VDI image.
 */
struct ImageInfo {
    uint64_t    diskSize = 0;
    uint32_t    blockSize = 0;
    ImageType   imageType = ImageType::Dynamic;
    uint32_t    blocksInImage = 0;
    uint32_t    blocksAllocated = 0;
    uint32_t    offsetBmap = 0;
    uint32_t    offsetData = 0;
    std::string uuid;
};

/**
 * @brief RAII wrapper representing an open VirtualBox Disk Image (noexcept).
 */
class Image {
public:
    /**
     * @brief Constructs an uninitialized, closed Image object.
     */
    Image() noexcept : handle_(nullptr) {}

    /**
     * @brief Wraps an existing raw vdi_image_t handle, taking ownership.
     */
    explicit Image(vdi_image_t *raw_handle) noexcept : handle_(raw_handle) {}

    /**
     * @brief Non-copyable to prevent double-close of underlying handle.
     */
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    /**
     * @brief Move constructor transfers handle ownership.
     */
    Image(Image &&other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    /**
     * @brief Move assignment operator transfers handle ownership.
     */
    Image &operator=(Image &&other) noexcept {
        if (this != &other) {
            close();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    /**
     * @brief Destructor automatically flushes and closes the image (noexcept).
     */
    ~Image() noexcept {
        close();
    }

    /* --- Static Factory Methods --- */

    /**
     * @brief Probes a target file to verify if it is a valid VDI image.
     */
    [[nodiscard]] static bool probe(const std::string &path) noexcept {
        return vdi_probe_file(path.c_str()) == VDI_OK;
    }

    /**
     * @brief Probes a memory buffer for a valid VDI header.
     */
    [[nodiscard]] static bool probe(const void *buffer, size_t size) noexcept {
        return vdi_probe_buffer(buffer, size) == VDI_OK;
    }

    /**
     * @brief Creates a pure IN-MEMORY VDI image (RAM disk).
     * Zero filesystem files are touched!
     *
     * @param diskSize Virtual capacity in bytes.
     * @param type Dynamic (sparse) or Static (preallocated).
     * @param blockSize Cluster size (pass 0 for default 1 MiB).
     */
    [[nodiscard]] static Result<Image> create(uint64_t diskSize,
                                              ImageType type = ImageType::Dynamic,
                                              uint32_t blockSize = 0) noexcept {
        vdi_image_t *raw = nullptr;
        vdi_status_t rc = vdi_create(&raw, nullptr, diskSize,
                                     static_cast<uint32_t>(type), blockSize);
        if (rc != VDI_OK) {
            return Result<Image>(rc);
        }
        return Result<Image>(Image(raw));
    }

    /**
     * @brief Creates a new VDI image over ANY generic I/O transport (memory, stream, device).
     * Does NOT force writing directly to a file on disk!
     */
    [[nodiscard]] static Result<Image> create(vdi_io_t io, uint64_t diskSize,
                                              ImageType type = ImageType::Dynamic,
                                              uint32_t blockSize = 0) noexcept {
        vdi_image_t *raw = nullptr;
        vdi_status_t rc = vdi_create(&raw, &io, diskSize,
                                     static_cast<uint32_t>(type), blockSize);
        if (rc != VDI_OK) {
            return Result<Image>(rc);
        }
        return Result<Image>(Image(raw));
    }

    /**
     * @brief Opens an existing VDI image from any generic I/O transport.
     */
    [[nodiscard]] static Result<Image> open(vdi_io_t io, OpenMode mode = OpenMode::ReadWrite) noexcept {
        vdi_image_t *raw = nullptr;
        vdi_status_t rc = vdi_open(&raw, &io, static_cast<int>(mode));
        if (rc != VDI_OK) {
            return Result<Image>(rc);
        }
        return Result<Image>(Image(raw));
    }

    /**
     * @brief Creates a new VDI image using a caller-provided dynamic memory buffer.
     */
    [[nodiscard]] static Result<Image> createMem(vdi_mem_buffer_t *mem, uint64_t diskSize,
                                                 ImageType type = ImageType::Dynamic,
                                                 uint32_t blockSize = 0) noexcept {
        vdi_image_t *raw = nullptr;
        vdi_status_t rc = vdi_create_mem(&raw, mem, diskSize,
                                         static_cast<uint32_t>(type), blockSize);
        if (rc != VDI_OK) return Result<Image>(rc);
        return Result<Image>(Image(raw));
    }

    /**
     * @brief Opens an existing in-memory VDI image buffer.
     */
    [[nodiscard]] static Result<Image> openMem(vdi_mem_buffer_t *mem, OpenMode mode = OpenMode::ReadWrite) noexcept {
        vdi_image_t *raw = nullptr;
        vdi_status_t rc = vdi_open_mem(&raw, mem, static_cast<int>(mode));
        if (rc != VDI_OK) return Result<Image>(rc);
        return Result<Image>(Image(raw));
    }

    /**
     * @brief Creates a new VDI image on a filesystem path.
     */
    [[nodiscard]] static Result<Image> createFile(const std::string &path, uint64_t diskSize,
                                                  ImageType type = ImageType::Dynamic,
                                                  uint32_t blockSize = 0) noexcept {
        vdi_image_t *raw = nullptr;
        vdi_status_t rc = vdi_create_file(&raw, path.c_str(), diskSize,
                                          static_cast<uint32_t>(type), blockSize);
        if (rc != VDI_OK) {
            return Result<Image>(rc);
        }
        return Result<Image>(Image(raw));
    }

    /**
     * @brief Opens an existing VDI image from a filesystem path.
     */
    [[nodiscard]] static Result<Image> openFile(const std::string &path,
                                                OpenMode mode = OpenMode::ReadWrite) noexcept {
        vdi_image_t *raw = nullptr;
        vdi_status_t rc = vdi_open_file(&raw, path.c_str(), static_cast<int>(mode));
        if (rc != VDI_OK) {
            return Result<Image>(rc);
        }
        return Result<Image>(Image(raw));
    }

    /**
     * @brief Path overload for create().
     */
    [[nodiscard]] static Result<Image> create(const std::string &path, uint64_t diskSize,
                                              ImageType type = ImageType::Dynamic,
                                              uint32_t blockSize = 0) noexcept {
        return createFile(path, diskSize, type, blockSize);
    }

    /**
     * @brief Path overload for open().
     */
    [[nodiscard]] static Result<Image> open(const std::string &path,
                                            OpenMode mode = OpenMode::ReadWrite) noexcept {
        return openFile(path, mode);
    }

    /* --- State & Metadata --- */

    /**
     * @brief Checks if the image is currently open.
     */
    [[nodiscard]] bool isOpen() const noexcept {
        return handle_ != nullptr;
    }

    /**
     * @brief Boolean evaluation operator.
     */
    explicit operator bool() const noexcept {
        return isOpen();
    }

    /**
     * @brief Retrieves complete image information and metadata.
     */
    [[nodiscard]] Result<ImageInfo> info() const noexcept {
        if (!handle_) return Result<ImageInfo>(VDI_ERR_INVALID_ARG);

        vdi_info_t raw_info;
        vdi_status_t rc = vdi_get_info(handle_, &raw_info);
        if (rc != VDI_OK) {
            return Result<ImageInfo>(rc);
        }

        char uuid_str[37] = {0};
        vdi_uuid_to_str(&raw_info.uuid_image, uuid_str);

        ImageInfo result;
        result.diskSize        = raw_info.disk_size;
        result.blockSize       = raw_info.block_size;
        result.imageType       = static_cast<ImageType>(raw_info.image_type);
        result.blocksInImage  = raw_info.blocks_in_image;
        result.blocksAllocated = raw_info.blocks_allocated;
        result.offsetBmap      = raw_info.offset_bmap;
        result.offsetData      = raw_info.offset_data;
        result.uuid            = uuid_str;
        return Result<ImageInfo>(result);
    }

    [[nodiscard]] uint64_t diskSize() const noexcept {
        auto inf = info();
        return inf.ok() ? inf->diskSize : 0;
    }

    [[nodiscard]] uint32_t blockSize() const noexcept {
        auto inf = info();
        return inf.ok() ? inf->blockSize : 0;
    }

    [[nodiscard]] uint32_t blocksAllocated() const noexcept {
        auto inf = info();
        return inf.ok() ? inf->blocksAllocated : 0;
    }

    [[nodiscard]] std::string uuid() const noexcept {
        auto inf = info();
        return inf.ok() ? inf->uuid : "";
    }

    /* --- I/O Operations --- */

    /**
     * @brief Reads data from virtual disk into a raw destination buffer.
     *
     * @return Result<size_t> with bytes read, or failure status.
     */
    [[nodiscard]] Result<size_t> read(uint64_t offset, void *dest, size_t count) noexcept {
        if (!handle_) return Result<size_t>(VDI_ERR_INVALID_ARG);
        size_t bytes_read = 0;
        vdi_status_t rc = vdi_read(handle_, offset, dest, count, &bytes_read);
        if (rc != VDI_OK) {
            return Result<size_t>(rc);
        }
        return Result<size_t>(bytes_read);
    }

    /**
     * @brief Reads data from virtual disk into an std::vector.
     *
     * @return Result<std::vector<uint8_t>> with buffer, or failure status.
     */
    [[nodiscard]] Result<std::vector<uint8_t>> read(uint64_t offset, size_t count) noexcept {
        std::vector<uint8_t> buf(count);
        auto res = read(offset, buf.data(), count);
        if (!res.ok()) {
            return Result<std::vector<uint8_t>>(res.status());
        }
        buf.resize(res.value());
        return Result<std::vector<uint8_t>>(std::move(buf));
    }

    /**
     * @brief Writes data from a raw source buffer to virtual disk space.
     *
     * @return Result<size_t> with bytes written, or failure status.
     */
    [[nodiscard]] Result<size_t> write(uint64_t offset, const void *src, size_t count) noexcept {
        if (!handle_) return Result<size_t>(VDI_ERR_INVALID_ARG);
        size_t bytes_written = 0;
        vdi_status_t rc = vdi_write(handle_, offset, src, count, &bytes_written);
        if (rc != VDI_OK) {
            return Result<size_t>(rc);
        }
        return Result<size_t>(bytes_written);
    }

    /**
     * @brief Writes byte vector to virtual disk space.
     */
    [[nodiscard]] Result<size_t> write(uint64_t offset, const std::vector<uint8_t> &data) noexcept {
        return write(offset, data.data(), data.size());
    }

    /**
     * @brief Writes string/binary view to virtual disk space.
     */
    [[nodiscard]] Result<size_t> write(uint64_t offset, std::string_view data) noexcept {
        return write(offset, data.data(), data.size());
    }

    /**
     * @brief Flushes pending metadata and write buffers to physical storage.
     */
    [[nodiscard]] Status flush() noexcept {
        if (!handle_) return Status(VDI_ERR_INVALID_ARG);
        return Status(vdi_flush(handle_));
    }

    /**
     * @brief Closes the image handle and releases resources.
     */
    void close() noexcept {
        if (handle_) {
            vdi_close(handle_);
            handle_ = nullptr;
        }
    }

    /**
     * @brief Access the underlying C handle.
     */
    [[nodiscard]] vdi_image_t *get() const noexcept {
        return handle_;
    }

    /**
     * @brief Releases ownership of the C handle without closing it.
     */
    vdi_image_t *release() noexcept {
        vdi_image_t *tmp = handle_;
        handle_ = nullptr;
        return tmp;
    }

private:
    vdi_image_t *handle_ = nullptr;
};

} // namespace vdi

#endif /* LIBVDI_HPP */
