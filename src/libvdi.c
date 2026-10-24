/**
 * @file libvdi.c
 * @brief Core implementation of the standalone libvdi library with abstract I/O.
 *
 * ============================================================================
 * ACKNOWLEDGEMENTS & QEMU ATTRIBUTION
 * ============================================================================
 * This implementation is directly adapted from QEMU's VirtualBox VDI block driver:
 *   File:    qemu/block/vdi.c
 *   Author:  Stefan Weil (2009, 2012)
 *   Project: QEMU (https://www.qemu.org)
 *   License: GPL-2.0-or-later
 *
 * Neither vdi_create nor vdi_open forces writing directly to a file. All I/O
 * passes through an abstract transport callback table (vdi_io_t).
 */

#include "vdi_internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ROUND_UP(x, y)      ((((x) + ((y) - 1)) / (y)) * (y))
#define DIV_ROUND_UP(x, y)  (((x) + ((y) - 1)) / (y))
#define MIN_VAL(a, b)       (((a) < (b)) ? (a) : (b))

/* ============================================================================
 * Error Formatting & Diagnostics
 * ============================================================================ */

const char *vdi_strerror(vdi_status_t status) {
    switch (status) {
        case VDI_OK:                  return "Success";
        case VDI_ERR_INVALID_ARG:     return "Invalid argument or null pointer";
        case VDI_ERR_IO:              return "I/O read, write, or seek failure";
        case VDI_ERR_NOT_VDI:         return "Not a valid VDI image (signature mismatch)";
        case VDI_ERR_UNSUPPORTED_VER: return "Unsupported VDI format version or feature";
        case VDI_ERR_CORRUPT:         return "VDI metadata corruption detected";
        case VDI_ERR_NO_MEMORY:       return "Memory allocation failure";
        case VDI_ERR_READ_ONLY:       return "Cannot write: image opened in read-only mode";
        case VDI_ERR_OUT_OF_BOUNDS:   return "Byte offset lies beyond virtual disk size";
        default:                      return "Unknown VDI error";
    }
}

/* ============================================================================
 * UUID Utilities
 * ============================================================================ */

void vdi_uuid_to_str(const vdi_uuid_t *uuid, char out_str[37]) {
    if (!uuid || !out_str) return;
    const uint8_t *d = uuid->data;
    snprintf(out_str, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             d[0], d[1], d[2], d[3],
             d[4], d[5],
             d[6], d[7],
             d[8], d[9],
             d[10], d[11], d[12], d[13], d[14], d[15]);
}

void vdi_generate_uuid(vdi_uuid_t *uuid) {
    if (!uuid) return;
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)(time(NULL) ^ (uintptr_t)uuid));
        seeded = true;
    }
    for (int i = 0; i < 16; i++) {
        uuid->data[i] = (uint8_t)(rand() & 0xFF);
    }
    /* Enforce RFC 4122 Version 4 (random) and standard variant */
    uuid->data[6] = (uuid->data[6] & 0x0F) | 0x40; /* Version 4 */
    uuid->data[8] = (uuid->data[8] & 0x3F) | 0x80; /* Variant 1 */
}

/* ============================================================================
 * Header Endianness Marshalling
 * ============================================================================ */

void vdi_header_from_le(vdi_header_t *dest, const vdi_header_t *src) {
    memcpy(dest->text, src->text, sizeof(dest->text));
    dest->signature        = vdi_le32(src->signature);
    dest->version          = vdi_le32(src->version);
    dest->header_size      = vdi_le32(src->header_size);
    dest->image_type       = vdi_le32(src->image_type);
    dest->image_flags      = vdi_le32(src->image_flags);
    memcpy(dest->description, src->description, sizeof(dest->description));
    dest->offset_bmap      = vdi_le32(src->offset_bmap);
    dest->offset_data      = vdi_le32(src->offset_data);
    dest->cylinders        = vdi_le32(src->cylinders);
    dest->heads            = vdi_le32(src->heads);
    dest->sectors          = vdi_le32(src->sectors);
    dest->sector_size      = vdi_le32(src->sector_size);
    dest->unused1          = vdi_le32(src->unused1);
    dest->disk_size        = vdi_le64(src->disk_size);
    dest->block_size       = vdi_le32(src->block_size);
    dest->block_extra      = vdi_le32(src->block_extra);
    dest->blocks_in_image  = vdi_le32(src->blocks_in_image);
    dest->blocks_allocated = vdi_le32(src->blocks_allocated);
    dest->uuid_image       = src->uuid_image;
    dest->uuid_last_snap   = src->uuid_last_snap;
    dest->uuid_link        = src->uuid_link;
    dest->uuid_parent      = src->uuid_parent;
    for (int i = 0; i < 7; i++) {
        dest->unused2[i]   = vdi_le64(src->unused2[i]);
    }
}

void vdi_header_to_le_struct(vdi_header_t *dest, const vdi_header_t *src) {
    memcpy(dest->text, src->text, sizeof(dest->text));
    dest->signature        = vdi_le32(src->signature);
    dest->version          = vdi_le32(src->version);
    dest->header_size      = vdi_le32(src->header_size);
    dest->image_type       = vdi_le32(src->image_type);
    dest->image_flags      = vdi_le32(src->image_flags);
    memcpy(dest->description, src->description, sizeof(dest->description));
    dest->offset_bmap      = vdi_le32(src->offset_bmap);
    dest->offset_data      = vdi_le32(src->offset_data);
    dest->cylinders        = vdi_le32(src->cylinders);
    dest->heads            = vdi_le32(src->heads);
    dest->sectors          = vdi_le32(src->sectors);
    dest->sector_size      = vdi_le32(src->sector_size);
    dest->unused1          = vdi_le32(src->unused1);
    dest->disk_size        = vdi_le64(src->disk_size);
    dest->block_size       = vdi_le32(src->block_size);
    dest->block_extra      = vdi_le32(src->block_extra);
    dest->blocks_in_image  = vdi_le32(src->blocks_in_image);
    dest->blocks_allocated = vdi_le32(src->blocks_allocated);
    dest->uuid_image       = src->uuid_image;
    dest->uuid_last_snap   = src->uuid_last_snap;
    dest->uuid_link        = src->uuid_link;
    dest->uuid_parent      = src->uuid_parent;
    for (int i = 0; i < 7; i++) {
        dest->unused2[i]   = vdi_le64(src->unused2[i]);
    }
}

/* ============================================================================
 * Built-in Transport: Memory Buffer
 * ============================================================================ */

static int64_t mem_read(void *opaque, void *buf, int64_t count) {
    vdi_mem_buffer_t *m = (vdi_mem_buffer_t *)opaque;
    if (m->pos >= m->size) return 0;
    int64_t avail = (int64_t)(m->size - m->pos);
    int64_t to_read = (count < avail) ? count : avail;
    memcpy(buf, m->data + m->pos, (size_t)to_read);
    m->pos += (size_t)to_read;
    return to_read;
}

static int64_t mem_write(void *opaque, const void *buf, int64_t count) {
    vdi_mem_buffer_t *m = (vdi_mem_buffer_t *)opaque;
    size_t end_pos = m->pos + (size_t)count;
    if (end_pos > m->capacity) {
        if (!m->dynamic) {
            if (m->pos >= m->capacity) return -1;
            count = (int64_t)(m->capacity - m->pos);
            end_pos = m->capacity;
        } else {
            size_t new_cap = (m->capacity == 0) ? (64 * 1024) : m->capacity * 2;
            while (new_cap < end_pos) new_cap *= 2;
            uint8_t *new_data = (uint8_t *)realloc(m->data, new_cap);
            if (!new_data) return -1;
            if (m->pos > m->size) {
                memset(new_data + m->size, 0, m->pos - m->size);
            }
            m->data = new_data;
            m->capacity = new_cap;
        }
    }
    memcpy(m->data + m->pos, buf, (size_t)count);
    m->pos = end_pos;
    if (m->pos > m->size) m->size = m->pos;
    return count;
}

static int64_t mem_seek(void *opaque, int64_t offset, int whence) {
    vdi_mem_buffer_t *m = (vdi_mem_buffer_t *)opaque;
    int64_t new_pos = 0;
    if (whence == 0)      new_pos = offset;                 /* SEEK_SET */
    else if (whence == 1) new_pos = (int64_t)m->pos + offset; /* SEEK_CUR */
    else if (whence == 2) new_pos = (int64_t)m->size + offset;/* SEEK_END */
    else return -1;

    if (new_pos < 0) return -1;
    m->pos = (size_t)new_pos;
    return new_pos;
}

static int64_t mem_tell(void *opaque) {
    vdi_mem_buffer_t *m = (vdi_mem_buffer_t *)opaque;
    return (int64_t)m->pos;
}

static int mem_flush(void *opaque) {
    (void)opaque;
    return 0;
}

vdi_status_t vdi_io_from_mem(vdi_io_t *out_io, vdi_mem_buffer_t *mem) {
    if (!out_io || !mem) return VDI_ERR_INVALID_ARG;
    out_io->opaque = mem;
    out_io->read   = mem_read;
    out_io->write  = mem_write;
    out_io->seek   = mem_seek;
    out_io->tell   = mem_tell;
    out_io->flush  = mem_flush;
    out_io->close  = NULL;
    return VDI_OK;
}

static void auto_ram_close(void *opaque) {
    vdi_mem_buffer_t *m = (vdi_mem_buffer_t *)opaque;
    if (m) {
        if (m->data) free(m->data);
        free(m);
    }
}

/* ============================================================================
 * Built-in Transport: File Stream
 * ============================================================================ */

typedef struct {
    FILE *fp;
    bool  close_on_exit;
} vdi_file_stream_t;

static int64_t file_read(void *opaque, void *buf, int64_t count) {
    vdi_file_stream_t *fs = (vdi_file_stream_t *)opaque;
    size_t rd = fread(buf, 1, (size_t)count, fs->fp);
    return (int64_t)rd;
}

static int64_t file_write(void *opaque, const void *buf, int64_t count) {
    vdi_file_stream_t *fs = (vdi_file_stream_t *)opaque;
    size_t wr = fwrite(buf, 1, (size_t)count, fs->fp);
    return (int64_t)wr;
}

static int64_t file_seek(void *opaque, int64_t offset, int whence) {
    vdi_file_stream_t *fs = (vdi_file_stream_t *)opaque;
    if (vdi_fseek64(fs->fp, offset, whence) != 0) return -1;
    return vdi_ftell64(fs->fp);
}

static int64_t file_tell(void *opaque) {
    vdi_file_stream_t *fs = (vdi_file_stream_t *)opaque;
    return vdi_ftell64(fs->fp);
}

static int file_flush(void *opaque) {
    vdi_file_stream_t *fs = (vdi_file_stream_t *)opaque;
    return (fflush(fs->fp) == 0) ? 0 : -1;
}

static void file_close(void *opaque) {
    vdi_file_stream_t *fs = (vdi_file_stream_t *)opaque;
    if (fs) {
        if (fs->close_on_exit && fs->fp) {
            fclose(fs->fp);
            fs->fp = NULL;
        }
        free(fs);
    }
}

vdi_status_t vdi_io_from_file(vdi_io_t *out_io, const char *path, int flags, bool create_if_missing) {
    if (!out_io || !path) return VDI_ERR_INVALID_ARG;

    const char *mode = "rb";
    if (create_if_missing) {
        mode = "w+b";
    } else if (flags & VDI_OPEN_READWRITE) {
        mode = "r+b";
    }

    FILE *f = fopen(path, mode);
    if (!f) return VDI_ERR_IO;

    vdi_file_stream_t *fs = (vdi_file_stream_t *)malloc(sizeof(vdi_file_stream_t));
    if (!fs) {
        fclose(f);
        return VDI_ERR_NO_MEMORY;
    }
    fs->fp = f;
    fs->close_on_exit = true;

    out_io->opaque = fs;
    out_io->read   = file_read;
    out_io->write  = file_write;
    out_io->seek   = file_seek;
    out_io->tell   = file_tell;
    out_io->flush  = file_flush;
    out_io->close  = file_close;
    return VDI_OK;
}

/* ============================================================================
 * Probing Functions
 * ============================================================================ */

vdi_status_t vdi_probe_buffer(const void *buf, size_t size) {
    if (!buf || size < sizeof(vdi_header_t)) {
        return VDI_ERR_INVALID_ARG;
    }
    const vdi_header_t *hdr = (const vdi_header_t *)buf;
    if (vdi_le32(hdr->signature) != VDI_SIGNATURE) {
        return VDI_ERR_NOT_VDI;
    }
    return VDI_OK;
}

vdi_status_t vdi_probe_io(vdi_io_t *io) {
    if (!io || !io->read || !io->seek) return VDI_ERR_INVALID_ARG;

    vdi_header_t raw_hdr;
    if (io->seek(io->opaque, 0, 0) < 0) return VDI_ERR_IO;
    if (io->read(io->opaque, &raw_hdr, sizeof(raw_hdr)) != (int64_t)sizeof(raw_hdr)) {
        return VDI_ERR_NOT_VDI;
    }
    return vdi_probe_buffer(&raw_hdr, sizeof(raw_hdr));
}

vdi_status_t vdi_probe_file(const char *path) {
    vdi_io_t io;
    vdi_status_t rc = vdi_io_from_file(&io, path, VDI_OPEN_READONLY, false);
    if (rc != VDI_OK) return rc;

    rc = vdi_probe_io(&io);
    if (io.close) io.close(io.opaque);
    return rc;
}

/* ============================================================================
 * Core Image Creation (Transport-Agnostic / In-Memory / Generic I/O)
 * ============================================================================ */

vdi_status_t vdi_create(vdi_image_t **out_vdi, vdi_io_t *io, uint64_t disk_size,
                        uint32_t image_type, uint32_t block_size) {
    if (!out_vdi || disk_size == 0) {
        return VDI_ERR_INVALID_ARG;
    }
    *out_vdi = NULL;

    if (image_type != VDI_TYPE_DYNAMIC && image_type != VDI_TYPE_STATIC) {
        return VDI_ERR_INVALID_ARG;
    }
    if (block_size == 0) {
        block_size = VDI_DEFAULT_BLOCK_SIZE;
    }

    /* If io is NULL, automatically construct a pure in-memory RAM disk */
    vdi_io_t local_io;
    if (!io) {
        vdi_mem_buffer_t *ram = (vdi_mem_buffer_t *)calloc(1, sizeof(vdi_mem_buffer_t));
        if (!ram) return VDI_ERR_NO_MEMORY;
        ram->dynamic  = true;
        ram->capacity = 64 * 1024;
        ram->data     = (uint8_t *)malloc(ram->capacity);
        if (!ram->data) {
            free(ram);
            return VDI_ERR_NO_MEMORY;
        }

        vdi_io_from_mem(&local_io, ram);
        local_io.close = auto_ram_close;
        io = &local_io;
    }

    if (!io->write || !io->seek) {
        return VDI_ERR_INVALID_ARG;
    }

    disk_size = ROUND_UP(disk_size, VDI_SECTOR_SIZE);
    uint32_t blocks = (uint32_t)DIV_ROUND_UP(disk_size, block_size);
    size_t bmap_bytes = (size_t)blocks * sizeof(uint32_t);
    bmap_bytes = ROUND_UP(bmap_bytes, (size_t)VDI_SECTOR_SIZE);

    /* Rewind to beginning */
    if (io->seek(io->opaque, 0, 0) < 0) {
        return VDI_ERR_IO;
    }

    /* Build header */
    vdi_header_t host_hdr;
    memset(&host_hdr, 0, sizeof(host_hdr));
    strncpy(host_hdr.text, VDI_TEXT, sizeof(host_hdr.text) - 1);
    host_hdr.signature        = VDI_SIGNATURE;
    host_hdr.version          = VDI_VERSION_1_1;
    host_hdr.header_size      = 0x180;
    host_hdr.image_type       = image_type;
    host_hdr.offset_bmap      = (uint32_t)sizeof(vdi_header_t); /* 0x200 */
    host_hdr.offset_data      = host_hdr.offset_bmap + (uint32_t)bmap_bytes;
    host_hdr.sector_size      = (uint32_t)VDI_SECTOR_SIZE;
    host_hdr.disk_size        = disk_size;
    host_hdr.block_size       = block_size;
    host_hdr.blocks_in_image  = blocks;
    host_hdr.blocks_allocated = (image_type == VDI_TYPE_STATIC) ? blocks : 0;
    vdi_generate_uuid(&host_hdr.uuid_image);
    vdi_generate_uuid(&host_hdr.uuid_last_snap);

    vdi_header_t le_hdr;
    vdi_header_to_le_struct(&le_hdr, &host_hdr);

    if (io->write(io->opaque, &le_hdr, sizeof(le_hdr)) != (int64_t)sizeof(le_hdr)) {
        return VDI_ERR_IO;
    }

    /* Write block map */
    uint32_t *bmap_le = (uint32_t *)calloc(1, bmap_bytes);
    if (!bmap_le) return VDI_ERR_NO_MEMORY;

    for (uint32_t i = 0; i < blocks; i++) {
        bmap_le[i] = vdi_le32((image_type == VDI_TYPE_STATIC) ? i : VDI_UNALLOCATED);
    }

    if (io->write(io->opaque, bmap_le, (int64_t)bmap_bytes) != (int64_t)bmap_bytes) {
        free(bmap_le);
        return VDI_ERR_IO;
    }
    free(bmap_le);

    /* If static, extend transport to full payload */
    if (image_type == VDI_TYPE_STATIC) {
        uint64_t total_data_bytes = (uint64_t)blocks * block_size;
        int64_t target_size = (int64_t)(host_hdr.offset_data + total_data_bytes);
        uint8_t zero = 0;
        if (io->seek(io->opaque, target_size - 1, 0) < 0 ||
            io->write(io->opaque, &zero, 1) != 1) {
            return VDI_ERR_IO;
        }
    }

    if (io->flush) io->flush(io->opaque);

    /* Return open handle ready for read/write */
    return vdi_open(out_vdi, io, VDI_OPEN_READWRITE);
}

/* ============================================================================
 * Image Open & Close (Transport-Agnostic)
 * ============================================================================ */

vdi_status_t vdi_open(vdi_image_t **out_vdi, vdi_io_t *io, int flags) {
    if (!out_vdi || !io || !io->read || !io->seek) return VDI_ERR_INVALID_ARG;
    *out_vdi = NULL;

    if (io->seek(io->opaque, 0, 0) < 0) {
        return VDI_ERR_IO;
    }

    vdi_header_t raw_hdr;
    if (io->read(io->opaque, &raw_hdr, sizeof(raw_hdr)) != (int64_t)sizeof(raw_hdr)) {
        return VDI_ERR_NOT_VDI;
    }

    vdi_header_t hdr;
    vdi_header_from_le(&hdr, &raw_hdr);

    if (hdr.signature != VDI_SIGNATURE) {
        return VDI_ERR_NOT_VDI;
    }
    if (hdr.version != VDI_VERSION_1_1) {
        return VDI_ERR_UNSUPPORTED_VER;
    }
    if (hdr.block_size == 0 || hdr.blocks_in_image == 0) {
        return VDI_ERR_CORRUPT;
    }

    size_t bmap_bytes = (size_t)hdr.blocks_in_image * sizeof(uint32_t);
    bmap_bytes = ROUND_UP(bmap_bytes, (size_t)VDI_SECTOR_SIZE);

    uint32_t *bmap_raw = (uint32_t *)malloc(bmap_bytes);
    if (!bmap_raw) return VDI_ERR_NO_MEMORY;

    if (io->seek(io->opaque, hdr.offset_bmap, 0) < 0 ||
        io->read(io->opaque, bmap_raw, (int64_t)bmap_bytes) != (int64_t)bmap_bytes) {
        free(bmap_raw);
        return VDI_ERR_IO;
    }

    uint32_t *bmap = (uint32_t *)malloc(hdr.blocks_in_image * sizeof(uint32_t));
    if (!bmap) {
        free(bmap_raw);
        return VDI_ERR_NO_MEMORY;
    }

    for (uint32_t i = 0; i < hdr.blocks_in_image; i++) {
        bmap[i] = vdi_le32(bmap_raw[i]);
    }
    free(bmap_raw);

    vdi_image_t *img = (vdi_image_t *)calloc(1, sizeof(vdi_image_t));
    if (!img) {
        free(bmap);
        return VDI_ERR_NO_MEMORY;
    }

    img->io               = *io;
    img->flags            = flags;
    img->header           = hdr;
    img->bmap             = bmap;
    img->bmap_size_bytes  = bmap_bytes;
    img->header_dirty     = false;
    img->dirty_bmap_first = VDI_UNALLOCATED;
    img->dirty_bmap_last  = VDI_UNALLOCATED;

    *out_vdi = img;
    return VDI_OK;
}

/* ============================================================================
 * Convenience File and Memory Overloads
 * ============================================================================ */

vdi_status_t vdi_create_file(vdi_image_t **out_vdi, const char *path, uint64_t disk_size,
                             uint32_t image_type, uint32_t block_size) {
    if (!path) return VDI_ERR_INVALID_ARG;

    vdi_io_t io;
    vdi_status_t rc = vdi_io_from_file(&io, path, VDI_OPEN_READWRITE, true);
    if (rc != VDI_OK) return rc;

    vdi_image_t *img = NULL;
    rc = vdi_create(&img, &io, disk_size, image_type, block_size);
    if (rc != VDI_OK) {
        if (io.close) io.close(io.opaque);
        return rc;
    }

    if (out_vdi) {
        *out_vdi = img;
    } else {
        vdi_close(img);
    }
    return VDI_OK;
}

vdi_status_t vdi_open_file(vdi_image_t **out_vdi, const char *path, int flags) {
    if (!out_vdi || !path) return VDI_ERR_INVALID_ARG;

    vdi_io_t io;
    vdi_status_t rc = vdi_io_from_file(&io, path, flags, false);
    if (rc != VDI_OK) return rc;

    rc = vdi_open(out_vdi, &io, flags);
    if (rc != VDI_OK && io.close) {
        io.close(io.opaque);
    }
    return rc;
}

vdi_status_t vdi_create_mem(vdi_image_t **out_vdi, vdi_mem_buffer_t *mem, uint64_t disk_size,
                            uint32_t image_type, uint32_t block_size) {
    if (!mem) return VDI_ERR_INVALID_ARG;
    vdi_io_t io;
    vdi_status_t rc = vdi_io_from_mem(&io, mem);
    if (rc != VDI_OK) return rc;

    return vdi_create(out_vdi, &io, disk_size, image_type, block_size);
}

vdi_status_t vdi_open_mem(vdi_image_t **out_vdi, vdi_mem_buffer_t *mem, int flags) {
    if (!mem) return VDI_ERR_INVALID_ARG;
    vdi_io_t io;
    vdi_status_t rc = vdi_io_from_mem(&io, mem);
    if (rc != VDI_OK) return rc;

    return vdi_open(out_vdi, &io, flags);
}

/* ============================================================================
 * Metadata Query
 * ============================================================================ */

vdi_status_t vdi_get_info(const vdi_image_t *vdi, vdi_info_t *info) {
    if (!vdi || !info) return VDI_ERR_INVALID_ARG;
    info->disk_size        = vdi->header.disk_size;
    info->block_size       = vdi->header.block_size;
    info->image_type       = vdi->header.image_type;
    info->blocks_in_image  = vdi->header.blocks_in_image;
    info->blocks_allocated = vdi->header.blocks_allocated;
    info->offset_bmap      = vdi->header.offset_bmap;
    info->offset_data      = vdi->header.offset_data;
    info->uuid_image       = vdi->header.uuid_image;
    return VDI_OK;
}

/* ============================================================================
 * Virtual Read Operation
 * ============================================================================ */

vdi_status_t vdi_read(vdi_image_t *vdi, uint64_t offset,
                      void *buf, size_t count, size_t *bytes_read) {
    if (bytes_read) *bytes_read = 0;
    if (!vdi || !buf) return VDI_ERR_INVALID_ARG;
    if (count == 0) return VDI_OK;

    if (offset >= vdi->header.disk_size) {
        return VDI_ERR_OUT_OF_BOUNDS;
    }

    if (offset + count > vdi->header.disk_size) {
        count = (size_t)(vdi->header.disk_size - offset);
    }

    uint8_t *dst = (uint8_t *)buf;
    size_t total_read = 0;
    uint32_t bs = vdi->header.block_size;

    while (count > 0) {
        uint32_t block_index = (uint32_t)(offset / bs);
        uint32_t offset_in_block = (uint32_t)(offset % bs);
        size_t chunk = MIN_VAL(count, (size_t)(bs - offset_in_block));

        if (block_index >= vdi->header.blocks_in_image) {
            break;
        }

        uint32_t phys_block = vdi->bmap[block_index];
        if (!VDI_IS_ALLOCATED(phys_block)) {
            memset(dst, 0, chunk);
        } else {
            int64_t file_offset = (int64_t)vdi->header.offset_data +
                                  ((int64_t)phys_block * bs) +
                                  offset_in_block;
            if (vdi->io.seek(vdi->io.opaque, file_offset, 0) < 0 ||
                vdi->io.read(vdi->io.opaque, dst, (int64_t)chunk) != (int64_t)chunk) {
                if (bytes_read) *bytes_read = total_read;
                return VDI_ERR_IO;
            }
        }

        dst        += chunk;
        offset     += chunk;
        count      -= chunk;
        total_read += chunk;
    }

    if (bytes_read) *bytes_read = total_read;
    return VDI_OK;
}

/* ============================================================================
 * Virtual Write Operation
 * ============================================================================ */

vdi_status_t vdi_write(vdi_image_t *vdi, uint64_t offset,
                       const void *buf, size_t count, size_t *bytes_written) {
    if (bytes_written) *bytes_written = 0;
    if (!vdi || !buf) return VDI_ERR_INVALID_ARG;
    if (!(vdi->flags & VDI_OPEN_READWRITE)) return VDI_ERR_READ_ONLY;
    if (count == 0) return VDI_OK;

    if (offset >= vdi->header.disk_size) {
        return VDI_ERR_OUT_OF_BOUNDS;
    }

    if (offset + count > vdi->header.disk_size) {
        count = (size_t)(vdi->header.disk_size - offset);
    }

    const uint8_t *src = (const uint8_t *)buf;
    size_t total_written = 0;
    uint32_t bs = vdi->header.block_size;
    uint8_t *alloc_block_buf = NULL;

    while (count > 0) {
        uint32_t block_index = (uint32_t)(offset / bs);
        uint32_t offset_in_block = (uint32_t)(offset % bs);
        size_t chunk = MIN_VAL(count, (size_t)(bs - offset_in_block));

        if (block_index >= vdi->header.blocks_in_image) {
            break;
        }

        uint32_t phys_block = vdi->bmap[block_index];

        if (!VDI_IS_ALLOCATED(phys_block)) {
            phys_block = vdi->header.blocks_allocated;
            vdi->header.blocks_allocated++;
            vdi->bmap[block_index] = phys_block;
            vdi->header_dirty = true;

            if (vdi->dirty_bmap_first == VDI_UNALLOCATED || block_index < vdi->dirty_bmap_first) {
                vdi->dirty_bmap_first = block_index;
            }
            if (vdi->dirty_bmap_last == VDI_UNALLOCATED || block_index > vdi->dirty_bmap_last) {
                vdi->dirty_bmap_last = block_index;
            }

            int64_t file_offset = (int64_t)vdi->header.offset_data + ((int64_t)phys_block * bs);

            if (chunk == bs) {
                if (vdi->io.seek(vdi->io.opaque, file_offset, 0) < 0 ||
                    vdi->io.write(vdi->io.opaque, src, bs) != (int64_t)bs) {
                    free(alloc_block_buf);
                    if (bytes_written) *bytes_written = total_written;
                    return VDI_ERR_IO;
                }
            } else {
                if (!alloc_block_buf) {
                    alloc_block_buf = (uint8_t *)calloc(1, bs);
                    if (!alloc_block_buf) {
                        if (bytes_written) *bytes_written = total_written;
                        return VDI_ERR_NO_MEMORY;
                    }
                } else {
                    memset(alloc_block_buf, 0, bs);
                }
                memcpy(alloc_block_buf + offset_in_block, src, chunk);

                if (vdi->io.seek(vdi->io.opaque, file_offset, 0) < 0 ||
                    vdi->io.write(vdi->io.opaque, alloc_block_buf, bs) != (int64_t)bs) {
                    free(alloc_block_buf);
                    if (bytes_written) *bytes_written = total_written;
                    return VDI_ERR_IO;
                }
            }
        } else {
            int64_t file_offset = (int64_t)vdi->header.offset_data +
                                  ((int64_t)phys_block * bs) +
                                  offset_in_block;
            if (vdi->io.seek(vdi->io.opaque, file_offset, 0) < 0 ||
                vdi->io.write(vdi->io.opaque, src, (int64_t)chunk) != (int64_t)chunk) {
                free(alloc_block_buf);
                if (bytes_written) *bytes_written = total_written;
                return VDI_ERR_IO;
            }
        }

        src           += chunk;
        offset        += chunk;
        count         -= chunk;
        total_written += chunk;
    }

    free(alloc_block_buf);

    if (bytes_written) *bytes_written = total_written;
    return vdi_flush(vdi);
}

/* ============================================================================
 * Flush Metadata
 * ============================================================================ */

vdi_status_t vdi_flush(vdi_image_t *vdi) {
    if (!vdi || !(vdi->flags & VDI_OPEN_READWRITE)) return VDI_OK;

    if (vdi->header_dirty) {
        vdi_header_t le_hdr;
        vdi_header_to_le_struct(&le_hdr, &vdi->header);
        if (vdi->io.seek(vdi->io.opaque, 0, 0) < 0 ||
            vdi->io.write(vdi->io.opaque, &le_hdr, sizeof(le_hdr)) != (int64_t)sizeof(le_hdr)) {
            return VDI_ERR_IO;
        }
        vdi->header_dirty = false;
    }

    if (vdi->dirty_bmap_first != VDI_UNALLOCATED && vdi->dirty_bmap_last != VDI_UNALLOCATED) {
        uint32_t first_idx = vdi->dirty_bmap_first;
        uint32_t last_idx  = vdi->dirty_bmap_last;

        uint32_t entries_per_sector = (uint32_t)(VDI_SECTOR_SIZE / sizeof(uint32_t));
        uint32_t first_sector = first_idx / entries_per_sector;
        uint32_t last_sector  = last_idx  / entries_per_sector;
        uint32_t start_idx    = first_sector * entries_per_sector;
        uint32_t num_entries  = (last_sector - first_sector + 1) * entries_per_sector;

        if (start_idx + num_entries > vdi->header.blocks_in_image) {
            num_entries = vdi->header.blocks_in_image - start_idx;
        }

        uint32_t *le_buf = (uint32_t *)malloc(num_entries * sizeof(uint32_t));
        if (!le_buf) return VDI_ERR_NO_MEMORY;

        for (uint32_t i = 0; i < num_entries; i++) {
            le_buf[i] = vdi_le32(vdi->bmap[start_idx + i]);
        }

        int64_t bmap_offset = (int64_t)vdi->header.offset_bmap +
                              ((int64_t)start_idx * sizeof(uint32_t));

        if (vdi->io.seek(vdi->io.opaque, bmap_offset, 0) < 0 ||
            vdi->io.write(vdi->io.opaque, le_buf, (int64_t)(num_entries * sizeof(uint32_t))) !=
            (int64_t)(num_entries * sizeof(uint32_t))) {
            free(le_buf);
            return VDI_ERR_IO;
        }
        free(le_buf);

        vdi->dirty_bmap_first = VDI_UNALLOCATED;
        vdi->dirty_bmap_last  = VDI_UNALLOCATED;
    }

    if (vdi->io.flush) vdi->io.flush(vdi->io.opaque);
    return VDI_OK;
}

void vdi_close(vdi_image_t *vdi) {
    if (!vdi) return;
    if (vdi->flags & VDI_OPEN_READWRITE) {
        vdi_flush(vdi);
    }
    if (vdi->io.close) {
        vdi->io.close(vdi->io.opaque);
        vdi->io.opaque = NULL;
    }
    if (vdi->bmap) {
        free(vdi->bmap);
        vdi->bmap = NULL;
    }
    free(vdi);
}
