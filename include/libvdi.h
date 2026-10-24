/**
 * @file libvdi.h
 * @brief Public interface for libvdi - Standalone VirtualBox Disk Image (VDI) library.
 *
 * ============================================================================
 * ACKNOWLEDGEMENTS & CREDITS
 * ============================================================================
 * This library is derived directly from and heavily inspired by the VDI block
 * driver in the QEMU open source machine emulator and virtualizer:
 *
 *   Source file:  qemu/block/vdi.c
 *   Original Author: Stefan Weil (2009, 2012)
 *   Project:      QEMU (https://www.qemu.org)
 *   License:      GPL-2.0-or-later (SPDX-License-Identifier: GPL-2.0-or-later)
 *
 * In QEMU, format drivers (like vdi.c) do not talk directly to files. Instead,
 * they read and write through an abstract BlockDriver transport layer (BlockBackend).
 * libvdi embraces this same architectural principle via the `vdi_io_t` abstraction:
 * VDI images can be created and manipulated in memory buffers, custom streams,
 * network storage, or filesystem files.
 *
 * Neither `vdi_create()` nor `vdi_open()` forces writing directly to a file!
 *
 * ============================================================================
 * VDI FORMAT OVERVIEW
 * ============================================================================
 * A Virtual Disk Image (VDI) consists of three sequential logical zones:
 *
 * 1. Pre-Header & Header (512 bytes):
 *    - Offset 0x000: Banner text ("<<< QEMU VM Virtual Disk Image >>>\n", 64 B)
 *    - Offset 0x040: Signature 0xbeda107f (Little Endian: 7f 10 da be)
 *    - Offset 0x044: Version 0x00010001 (v1.1)
 *    - Offset 0x048: Header size (0x180 = 384 bytes)
 *    - Offset 0x04C: Image type (1 = Dynamic / Sparse, 2 = Static / Preallocated)
 *    - Offsets:
 *      * offset_bmap: Byte offset to Block Map (usually 0x200 / 512)
 *      * offset_data: Byte offset to Data Clusters (aligned to sector boundary)
 *    - Geometry & Sizing:
 *      * disk_size: Total virtual disk capacity in bytes (64-bit uint)
 *      * block_size: Cluster size (typically 1 MiB = 1,048,576 bytes)
 *      * blocks_in_image: ceil(disk_size / block_size)
 *      * blocks_allocated: Current count of physically allocated clusters
 *    - UUIDs (16 bytes each): image, last snap, link, parent.
 *
 * 2. Block Map (bmap) [starts at offset_bmap]:
 *    - Array of uint32_t elements of size `blocks_in_image`, rounded up to
 *      a 512-byte sector boundary.
 *    - Maps virtual block index -> physical block index in the data area.
 *    - Sentinel values:
 *      * 0xFFFFFFFF (VDI_UNALLOCATED): Block is not allocated. Reads return 0x00.
 *      * 0xFFFFFFFE (VDI_DISCARDED): Block was discarded / trimmed. Reads return 0x00.
 *      * < 0xFFFFFFFE: Zero-based index of allocated block in data area.
 *
 * 3. Data Area [starts at offset_data]:
 *    - Contiguous array of clusters (each of size `block_size`).
 *    - For physical block index P:
 *      File Offset = offset_data + (P * block_size) + offset_in_block.
 */

#ifndef LIBVDI_H
#define LIBVDI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Format Signatures and Magic Constants --- */

/** Magic signature identifying a valid VDI file (0xbeda107f). */
#define VDI_SIGNATURE           0xbeda107fU

/** Supported format version: Version 1.1 (0x00010001). */
#define VDI_VERSION_1_1         0x00010001U

/** Standard physical sector size in bytes. */
#define VDI_SECTOR_SIZE         512ULL

/** Default cluster/block allocation size (1 MiB = 1,048,576 bytes). */
#define VDI_DEFAULT_BLOCK_SIZE  (1024 * 1024)

/** Image type: Dynamic (sparse, grow-on-write). */
#define VDI_TYPE_DYNAMIC        1

/** Image type: Static (preallocated, fixed capacity). */
#define VDI_TYPE_STATIC         2

/** Block sentinel: Block has never been allocated; logical reads return zeros. */
#define VDI_UNALLOCATED         0xffffffffU

/** Block sentinel: Block was discarded/trimmed; logical reads return zeros. */
#define VDI_DISCARDED           0xfffffffeU

/** Macro evaluating to true if the bmap entry refers to an allocated physical block. */
#define VDI_IS_ALLOCATED(x)     ((x) < VDI_DISCARDED)

/** Header banner text length in bytes. */
#define VDI_HEADER_TEXT_LEN     64

/** Header description field length in bytes. */
#define VDI_DESC_LEN            256

/* --- Status and Error Codes --- */

typedef enum {
    VDI_OK                  =  0, /**< Operation completed successfully. */
    VDI_ERR_INVALID_ARG     = -1, /**< An invalid or NULL argument was passed. */
    VDI_ERR_IO              = -2, /**< Underlying transport I/O error (read, write, seek). */
    VDI_ERR_NOT_VDI         = -3, /**< Does not contain a valid VDI signature. */
    VDI_ERR_UNSUPPORTED_VER = -4, /**< Image format version is unsupported (only v1.1 supported). */
    VDI_ERR_CORRUPT         = -5, /**< Image header or block map contains inconsistent values. */
    VDI_ERR_NO_MEMORY       = -6, /**< Memory allocation failure. */
    VDI_ERR_READ_ONLY       = -7, /**< Attempted write operation on a read-only image. */
    VDI_ERR_OUT_OF_BOUNDS   = -8  /**< Requested offset lies beyond virtual disk size. */
} vdi_status_t;

/* --- UUID Structure --- */

/**
 * @brief 128-bit UUID (16 raw bytes) following RFC 4122.
 */
typedef struct {
    uint8_t data[16];
} vdi_uuid_t;

/* --- On-Disk Packed Header Structure --- */

#pragma pack(push, 1)
/**
 * @brief Exact 512-byte on-disk VDI Header structure (Little Endian).
 *
 * Directly mirrors the layout defined in QEMU's block/vdi.c (VdiHeader).
 */
typedef struct {
    char        text[VDI_HEADER_TEXT_LEN]; /**< Human-readable banner string. */
    uint32_t    signature;                 /**< VDI_SIGNATURE (0xbeda107f). */
    uint32_t    version;                   /**< VDI_VERSION_1_1 (0x00010001). */
    uint32_t    header_size;               /**< Size of header payload (0x180). */
    uint32_t    image_type;                /**< VDI_TYPE_DYNAMIC or VDI_TYPE_STATIC. */
    uint32_t    image_flags;               /**< Image flags (typically 0). */
    char        description[VDI_DESC_LEN]; /**< Image description text (null-padded). */
    uint32_t    offset_bmap;               /**< Byte offset where block map begins (0x200). */
    uint32_t    offset_data;               /**< Byte offset where data blocks begin. */
    uint32_t    cylinders;                 /**< Legacy CHS geometry: cylinders. */
    uint32_t    heads;                     /**< Legacy CHS geometry: heads. */
    uint32_t    sectors;                   /**< Legacy CHS geometry: sectors. */
    uint32_t    sector_size;               /**< Sector size (512). */
    uint32_t    unused1;                   /**< Reserved / unused. */
    uint64_t    disk_size;                 /**< Virtual disk capacity in bytes. */
    uint32_t    block_size;                /**< Cluster block size (default 1 MiB). */
    uint32_t    block_extra;               /**< Extra block bytes (unused, 0). */
    uint32_t    blocks_in_image;           /**< Total virtual blocks = ceil(disk_size/block_size). */
    uint32_t    blocks_allocated;          /**< Number of physically allocated blocks in file. */
    vdi_uuid_t  uuid_image;                /**< Unique ID of this disk image. */
    vdi_uuid_t  uuid_last_snap;            /**< Unique ID of last snapshot. */
    vdi_uuid_t  uuid_link;                 /**< Unique ID of link (for differentials). */
    vdi_uuid_t  uuid_parent;               /**< Unique ID of parent image. */
    uint64_t    unused2[7];                /**< Reserved / padding up to 512 bytes. */
} vdi_header_t;
#pragma pack(pop)

/* --- Image Information Summary --- */

/**
 * @brief Public summary of an open VDI image's geometry and allocation status.
 */
typedef struct {
    uint64_t    disk_size;        /**< Virtual capacity in bytes. */
    uint32_t    block_size;       /**< Cluster size in bytes. */
    uint32_t    image_type;       /**< VDI_TYPE_DYNAMIC or VDI_TYPE_STATIC. */
    uint32_t    blocks_in_image;  /**< Total virtual clusters. */
    uint32_t    blocks_allocated; /**< Number of clusters physically allocated. */
    uint32_t    offset_bmap;      /**< Byte offset of block map on disk. */
    uint32_t    offset_data;      /**< Byte offset of data area on disk. */
    vdi_uuid_t  uuid_image;       /**< Image UUID. */
} vdi_info_t;

/** Opaque handle representing an opened VDI image instance. */
typedef struct vdi_image vdi_image_t;

/* --- Open Flags --- */

/** Open the image in read-only mode. Writes will return VDI_ERR_READ_ONLY. */
#define VDI_OPEN_READONLY   0x01

/** Open the image in read-write mode. */
#define VDI_OPEN_READWRITE  0x02

/* ============================================================================
 * Generic I/O Transport Layer (QEMU BlockDriver Protocol Abstraction)
 * ============================================================================ */

/**
 * @brief Abstract I/O transport interface.
 *
 * Allows creating and operating on VDI images over ANY medium:
 * - Memory buffers (RAM disk)
 * - Filesystem files
 * - Custom virtual devices, pipes, or network streams
 */
typedef struct vdi_io {
    void *opaque;

    /** Read bytes into buf. Returns bytes read, 0 on EOF, or negative on error. */
    int64_t (*read)(void *opaque, void *buf, int64_t count);

    /** Write bytes from buf. Returns bytes written, or negative on error. */
    int64_t (*write)(void *opaque, const void *buf, int64_t count);

    /** Seek to offset (whence: 0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END). Returns new offset or -1. */
    int64_t (*seek)(void *opaque, int64_t offset, int whence);

    /** Query current stream offset. Returns offset or -1 on error. */
    int64_t (*tell)(void *opaque);

    /** Optional flush callback. Return 0 on success, negative on error. Can be NULL. */
    int     (*flush)(void *opaque);

    /** Optional close/cleanup callback called by vdi_close(). Can be NULL. */
    void    (*close)(void *opaque);
} vdi_io_t;

/**
 * @brief In-memory byte buffer structure for memory-backed VDI images.
 */
typedef struct {
    uint8_t *data;       /**< Pointer to memory block. */
    size_t   size;       /**< Current logical size. */
    size_t   capacity;   /**< Total allocated capacity. */
    size_t   pos;        /**< Current cursor position. */
    bool     dynamic;    /**< If true, reallocs automatically on write beyond capacity. */
} vdi_mem_buffer_t;

/**
 * @brief Initializes a vdi_io_t transport backed by an in-memory buffer.
 */
vdi_status_t vdi_io_from_mem(vdi_io_t *out_io, vdi_mem_buffer_t *mem);

/**
 * @brief Initializes a vdi_io_t transport backed by a filesystem file path.
 */
vdi_status_t vdi_io_from_file(vdi_io_t *out_io, const char *path, int flags, bool create_if_missing);

/* ============================================================================
 * Public Function API
 * ============================================================================ */

/**
 * @brief Inspects an in-memory buffer to check if it contains a valid VDI header.
 */
vdi_status_t vdi_probe_buffer(const void *buf, size_t size);

/**
 * @brief Inspects a generic I/O transport to check if it contains a valid VDI image.
 */
vdi_status_t vdi_probe_io(vdi_io_t *io);

/**
 * @brief Checks if a target file on disk is a valid VDI image.
 */
vdi_status_t vdi_probe_file(const char *path);

/**
 * @brief Creates and formats a new VDI image, returning an active open handle directly.
 *
 * DOES NOT FORCE WRITING TO A FILE!
 *
 * - If `io` is provided: formats the image over that custom I/O transport.
 * - If `io` is NULL: automatically creates a pure IN-MEMORY VDI image (RAM disk)!
 *
 * @param out_vdi Pointer receiving the active, ready-to-use image handle.
 * @param io Generic I/O transport callbacks, or NULL for an in-memory RAM image.
 * @param disk_size Virtual disk capacity in bytes (automatically rounded to sector boundary).
 * @param image_type VDI_TYPE_DYNAMIC (sparse) or VDI_TYPE_STATIC (preallocated).
 * @param block_size Cluster size in bytes (pass 0 for default 1 MiB).
 * @return VDI_OK on success; error code otherwise.
 */
vdi_status_t vdi_create(vdi_image_t **out_vdi, vdi_io_t *io, uint64_t disk_size,
                        uint32_t image_type, uint32_t block_size);

/**
 * @brief Opens an existing VDI image from any generic I/O transport.
 *
 * @param out_vdi Pointer receiving allocated image context.
 * @param io Generic I/O transport callbacks.
 * @param flags Bitwise OR of VDI_OPEN_READONLY or VDI_OPEN_READWRITE.
 * @return VDI_OK on success; error code otherwise.
 */
vdi_status_t vdi_open(vdi_image_t **out_vdi, vdi_io_t *io, int flags);

/* --- Aliases matching _io naming --- */
#define vdi_create_io vdi_create
#define vdi_open_io   vdi_open

/* --- Convenience Helpers for Filesystem Paths --- */

/**
 * @brief Convenience helper: creates a new VDI image directly on a filesystem path.
 */
vdi_status_t vdi_create_file(vdi_image_t **out_vdi, const char *path, uint64_t disk_size,
                             uint32_t image_type, uint32_t block_size);

/**
 * @brief Convenience helper: opens an existing VDI file by path.
 */
vdi_status_t vdi_open_file(vdi_image_t **out_vdi, const char *path, int flags);

/* --- Convenience Helpers for Memory Buffers --- */

/**
 * @brief Convenience helper: creates a new VDI image on a specific user memory buffer.
 */
vdi_status_t vdi_create_mem(vdi_image_t **out_vdi, vdi_mem_buffer_t *mem, uint64_t disk_size,
                            uint32_t image_type, uint32_t block_size);

/**
 * @brief Convenience helper: opens an existing in-memory VDI buffer.
 */
vdi_status_t vdi_open_mem(vdi_image_t **out_vdi, vdi_mem_buffer_t *mem, int flags);

/**
 * @brief Retrieves metadata and allocation metrics from an open image.
 */
vdi_status_t vdi_get_info(const vdi_image_t *vdi, vdi_info_t *info);

/**
 * @brief Reads data from virtual disk space.
 * Unallocated/sparse blocks return zeroes without physical I/O.
 */
vdi_status_t vdi_read(vdi_image_t *vdi, uint64_t offset,
                      void *buf, size_t count, size_t *bytes_read);

/**
 * @brief Writes data to virtual disk space.
 * Dynamically allocates physical clusters on demand if needed.
 */
vdi_status_t vdi_write(vdi_image_t *vdi, uint64_t offset,
                       const void *buf, size_t count, size_t *bytes_written);

/**
 * @brief Flushes any in-memory dirty metadata (header and block map) to transport.
 */
vdi_status_t vdi_flush(vdi_image_t *vdi);

/**
 * @brief Closes the VDI image handle, flushing pending metadata and freeing memory.
 */
void vdi_close(vdi_image_t *vdi);

/**
 * @brief Formats a 16-byte VDI UUID into standard canonical string (36 chars + '\0').
 */
void vdi_uuid_to_str(const vdi_uuid_t *uuid, char out_str[37]);

/**
 * @brief Returns a human-readable English explanation of a status code.
 */
const char *vdi_strerror(vdi_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* LIBVDI_H */
