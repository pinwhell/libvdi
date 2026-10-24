/**
 * @file vdi_internal.h
 * @brief Internal declarations, endian utilities, and platform portability helpers.
 *
 * Derived from QEMU (qemu/block/vdi.c, qemu/bswap.h, qemu/osdep.h).
 * Original Author: Stefan Weil
 * License: GPL-2.0-or-later
 */

#ifndef VDI_INTERNAL_H
#define VDI_INTERNAL_H

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "libvdi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/*
 * Compile-time assertion to ensure compiler structure packing matches the
 * exact on-disk 512-byte specification from VirtualBox / QEMU.
 */
typedef char vdi_check_header_size[(sizeof(vdi_header_t) == 512) ? 1 : -1];

/** Default banner string written into VdiHeader.text by QEMU and libvdi */
#define VDI_TEXT "<<< QEMU VM Virtual Disk Image >>>\n"

/* ============================================================================
 * Endianness Conversion
 *
 * All multi-byte integer values in the VDI specification (header fields,
 * block table entries, disk size, etc.) are explicitly stored in LITTLE ENDIAN.
 * On little-endian hosts (x86, x86_64, ARM LE), conversions are no-ops.
 * On big-endian hosts, bytes are reversed.
 * ============================================================================ */

static inline uint16_t vdi_le16(uint16_t val) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return (uint16_t)((val << 8) | (val >> 8));
#else
    return val;
#endif
}

static inline uint32_t vdi_le32(uint32_t val) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return (((val & 0x000000FFU) << 24) |
            ((val & 0x0000FF00U) << 8)  |
            ((val & 0x00FF0000U) >> 8)  |
            ((val & 0xFF000000U) >> 24));
#else
    return val;
#endif
}

static inline uint64_t vdi_le64(uint64_t val) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return (((val & 0x00000000000000FFULL) << 56) |
            ((val & 0x000000000000FF00ULL) << 40) |
            ((val & 0x0000000000FF0000ULL) << 24) |
            ((val & 0x00000000FF000000ULL) << 8)  |
            ((val & 0x000000FF00000000ULL) >> 8)  |
            ((val & 0x0000FF0000000000ULL) >> 24) |
            ((val & 0x00FF000000000000ULL) >> 40) |
            ((val & 0xFF00000000000000ULL) >> 56));
#else
    return val;
#endif
}

/* ============================================================================
 * 64-bit File Positioning Wrappers
 *
 * Virtual disk images frequently exceed 2 GiB / 4 GiB boundaries.
 * Standard fseek/ftell take long (32-bit on Windows and 32-bit platforms).
 * We wrap _fseeki64/_ftelli64 (MSVC/Windows) and fseeko/ftello (POSIX).
 * ============================================================================ */

static inline int vdi_fseek64(FILE *f, int64_t offset, int origin) {
#if defined(_WIN32)
    return _fseeki64(f, offset, origin);
#elif defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE >= 200112L)
    return fseeko(f, (off_t)offset, origin);
#else
    return fseek(f, (long)offset, origin);
#endif
}

static inline int64_t vdi_ftell64(FILE *f) {
#if defined(_WIN32)
    return _ftelli64(f);
#elif defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE >= 200112L)
    return (int64_t)ftello(f);
#else
    return (int64_t)ftell(f);
#endif
}

/* ============================================================================
 * Internal VDI Context Structure
 * ============================================================================ */
struct vdi_image {
    vdi_io_t        io;              /**< Abstract I/O transport callbacks. */
    int             flags;           /**< Access flags (VDI_OPEN_READONLY / READWRITE). */
    vdi_header_t    header;          /**< Parsed header kept in host byte order. */
    uint32_t       *bmap;            /**< In-memory block map table in host byte order. */
    size_t          bmap_size_bytes; /**< Sector-aligned byte length of block map on disk. */

    /* Metadata dirty tracking */
    bool            header_dirty;    /**< True if blocks_allocated or header needs flush. */
    uint32_t        dirty_bmap_first;/**< Lowest modified block table index. */
    uint32_t        dirty_bmap_last; /**< Highest modified block table index. */
};

/* Internal helper prototypes */
void vdi_header_from_le(vdi_header_t *dest, const vdi_header_t *src);
void vdi_header_to_le_struct(vdi_header_t *dest, const vdi_header_t *src);
void vdi_generate_uuid(vdi_uuid_t *uuid);

#endif /* VDI_INTERNAL_H */
