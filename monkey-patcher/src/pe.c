#include "patcher.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Retail files used by reigw. Fingerprints cover every byte, so an already
 * patched file or a different release cannot accidentally match this layout.
 * FNV-1a is an identity guard against accidental differences, not a signature.
 */
static const profile_t profiles[] = {
    { GAME, "gamex86.dll", 930304, UINT64_C(0x58458bb427dc169d),
      0x4fbdcafd, 0x20000000, 0x2ca60, 0xb58bc, 4, 5, 6 },
    { CGAME, "cgamex86.dll", 911360, UINT64_C(0xcee1c445aa4ed8a5),
      0x4fbdcb0d, 0x30000000, 0x329e0, 0xc8b50, 3, 4, 5 },
    /* UI has no patch rules, so its CVAR payload metadata remains unset. */
    { .module = UI, .name = "uix86.dll", .size = 499712,
      .fingerprint = UINT64_C(0x921187794457deb6),
      .timestamp = 0x4fbdcaee, .image_base = 0x10000000 },
};

uint16_t get16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void put16(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

void put32(unsigned char *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (unsigned char)(value >> (8 * i));
}

static bool fits(size_t offset, size_t size, size_t total)
{
    return offset <= total && size <= total - offset;
}

/* Translate only file-backed bytes. An RVA in .data's uninitialized tail
 * is not a file offset, and must never be patched directly on disk.
 */
bool image_offset(const image_t *image, uint32_t rva, size_t size, size_t *offset)
{
    for (uint16_t i = 0; i < image->section_count; ++i) {
        const unsigned char *section = image->data + image->sections + (size_t)i * 40;
        uint32_t start = get32(section + 12), raw_size = get32(section + 16);
        uint32_t raw = get32(section + 20);
        if (rva >= start && fits(rva - start, size, raw_size) &&
            fits((size_t)raw + (rva - start), size, image->size)) {
            *offset = (size_t)raw + (rva - start);
            return true;
        }
    }
    fprintf(stderr, "RVA 0x%08" PRIx32 " has no file-backed range of %zu bytes\n", rva, size);
    return false;
}

bool image_open(image_t *image, const char *path)
{
    memset(image, 0, sizeof(*image));
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return false;
    }
    long length = -1;
    bool ok = false;
    if (fseek(file, 0, SEEK_END) == 0)
        length = ftell(file);
    /* Supported inputs are below 1 MiB. Bound allocation before parsing. */
    if (length < 0 || length > 2 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s: unreadable or unsupported file size\n", path);
        goto done;
    }
    image->size = (size_t)length;
    image->data = malloc(image->size ? image->size : 1);
    if (!image->data || fread(image->data, 1, image->size, file) != image->size) {
        fprintf(stderr, "%s: could not read complete input\n", path);
        goto done;
    }
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < image->size; ++i)
        hash = (hash ^ image->data[i]) * UINT64_C(0x100000001b3);
    for (size_t i = 0; i < ARRAY_SIZE(profiles); ++i) {
        if (profiles[i].size == image->size && profiles[i].fingerprint == hash)
            image->profile = &profiles[i];
    }
    if (!image->profile) {
        fprintf(stderr, "%s: unsupported or modified DLL (%zu bytes, FNV-1a %016" PRIx64 ")\n",
                path, image->size, hash);
        goto done;
    }

    /* The fingerprint selects a known layout; still check every PE boundary
     * used below so a changed profile cannot silently bypass the parser.
     */
    if (!fits(0, 64, image->size) || memcmp(image->data, "MZ", 2))
        goto invalid;
    image->pe = get32(image->data + 0x3c);
    if (!fits(image->pe, 24, image->size) || memcmp(image->data + image->pe, "PE\0\0", 4))
        goto invalid;
    image->optional = image->pe + 24;
    uint16_t optional_size = get16(image->data + image->pe + 20);
    if (get16(image->data + image->pe + 4) != 0x14c || optional_size < 224 ||
        !fits(image->optional, optional_size, image->size))
        goto invalid;
    const unsigned char *opt = image->data + image->optional;
    if (get16(opt) != 0x10b || get32(opt + 28) != image->profile->image_base ||
        get32(image->data + image->pe + 8) != image->profile->timestamp ||
        get32(opt + 92) < 16 || get32(opt + 128) || get32(opt + 132))
        goto invalid;
    image->sections = image->optional + optional_size;
    image->section_count = get16(image->data + image->pe + 6);
    if (!image->section_count || !fits(image->sections, (size_t)image->section_count * 40, image->size))
        goto invalid;
    for (uint16_t i = 0; i < image->section_count; ++i) {
        const unsigned char *s = image->data + image->sections + (size_t)i * 40;
        if (!fits(get32(s + 20), get32(s + 16), image->size))
            goto invalid;
    }
    ok = true;
    goto done;
invalid:
    fprintf(stderr, "%s: unsupported PE32 layout\n", path);
done:
    if (fclose(file) != 0)
        ok = false;
    if (!ok) {
        free(image->data);
        image->data = NULL;
    }
    return ok;
}

/* Find the export's address-table slot rather than changing executable bytes
 * at dllEntry, whose absolute operand is covered by an existing relocation.
 */
static bool entry_export(const image_t *image, size_t *slot)
{
    size_t exports, names, ordinals, functions;
    uint32_t export_rva = get32(image->data + image->optional + 96);
    if (!image_offset(image, export_rva, 40, &exports))
        return false;
    const unsigned char *dir = image->data + exports;
    uint32_t count = get32(dir + 24), function_count = get32(dir + 20);
    if (count > 64 || function_count > 64 ||
        !image_offset(image, get32(dir + 32), (size_t)count * 4, &names) ||
        !image_offset(image, get32(dir + 36), (size_t)count * 2, &ordinals) ||
        !image_offset(image, get32(dir + 28), (size_t)function_count * 4, &functions))
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        size_t name;
        if (!image_offset(image, get32(image->data + names + i * 4), 9, &name))
            return false;
        if (memcmp(image->data + name, "dllEntry\0", 9))
            continue;
        uint16_t ordinal = get16(image->data + ordinals + i * 2);
        if (ordinal >= function_count)
            return false;
        *slot = functions + (size_t)ordinal * 4;
        return get32(image->data + *slot) == image->profile->dll_entry;
    }
    fprintf(stderr, "Expected dllEntry export not found\n");
    return false;
}

static bool align_up(uint32_t value, uint32_t alignment, uint32_t *result)
{
    if (!alignment || (alignment & (alignment - 1)) || value > UINT32_MAX - (alignment - 1))
        return false;
    *result = (value + alignment - 1) & ~(alignment - 1);
    return true;
}

static bool add_payload(image_t *image)
{
    size_t export_slot, entry_offset, syscall_offset;
    if (!entry_export(image, &export_slot) ||
        !image_offset(image, image->profile->dll_entry, 10, &entry_offset) ||
        !image_offset(image, image->profile->syscall_rva, 4, &syscall_offset))
        return false;
    unsigned char expected[] = { 0x8b, 0x44, 0x24, 4, 0xa3, 0, 0, 0, 0, 0xc3 };
    put32(expected + 5, image->profile->image_base + image->profile->syscall_rva);
    if (memcmp(image->data + entry_offset, expected, sizeof(expected))) {
        fprintf(stderr, "dllEntry does not contain the expected instructions\n");
        return false;
    }
    unsigned char *opt = image->data + image->optional;
    uint32_t file_alignment = get32(opt + 36), section_alignment = get32(opt + 32);
    uint32_t rva, raw, raw_size, image_size;
    size_t header = image->sections + (size_t)image->section_count * 40;
    uint32_t first_raw = (uint32_t)image->size;
    for (uint16_t i = 0; i < image->section_count; ++i) {
        const unsigned char *s = image->data + image->sections + (size_t)i * 40;
        if (get32(s + 16) && get32(s + 20) < first_raw)
            first_raw = get32(s + 20);
    }
    if (!fits(header, 40, get32(opt + 60)) || !fits(header, 40, first_raw) ||
        !align_up(get32(opt + 56), section_alignment, &rva) ||
        !align_up((uint32_t)image->size, file_alignment, &raw)) {
        fprintf(stderr, "No room for the payload's section header\n");
        return false;
    }
    payload_t payload;
    if (!payload_build(&payload, image->profile, rva) ||
        !align_up((uint32_t)payload.size, file_alignment, &raw_size) ||
        rva > UINT32_MAX - payload.size ||
        !align_up(rva + (uint32_t)payload.size, section_alignment, &image_size))
        return false;
    size_t new_size = (size_t)raw + raw_size;
    unsigned char *data = realloc(image->data, new_size);
    if (!data) {
        fprintf(stderr, "Could not allocate patched image\n");
        return false;
    }
    memset(data + image->size, 0, new_size - image->size);
    image->data = data;
    image->size = new_size;
    memcpy(data + raw, payload.data, payload.size);
    memset(data + header, 0, 40);
    memcpy(data + header, ".igwmonk", 8);
    put32(data + header + 8, (uint32_t)payload.size);
    put32(data + header + 12, rva);
    put32(data + header + 16, raw_size);
    put32(data + header + 20, raw);
    /* Writable handle storage shares the position independent code section. */
    put32(data + header + 36, 0xe0000060u);
    put16(data + image->pe + 6, ++image->section_count);
    opt = data + image->optional;
    put32(opt + 4, get32(opt + 4) + raw_size);
    put32(opt + 8, get32(opt + 8) + raw_size);
    put32(opt + 56, image_size);
    put32(data + export_slot, rva + payload.entry);
    return true;
}

/* PE checksum: fold 16-bit words, skip the checksum field, add file length. */
static void update_checksum(image_t *image)
{
    size_t field = image->optional + 64;
    put32(image->data + field, 0);
    uint32_t sum = 0;
    for (size_t i = 0; i < image->size; i += 2) {
        uint32_t value = image->data[i];
        if (i + 1 < image->size)
            value |= (uint32_t)image->data[i + 1] << 8;
        sum += value;
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    sum = (sum & 0xffffu) + (sum >> 16);
    put32(image->data + field, sum + (uint32_t)image->size);
}

bool image_patch(image_t *image)
{
    const profile_t *p = image->profile;
    size_t changes = 0;
    printf("%s: known retail DLL\n", p->name);
    bool has_cvars = false;
    for (size_t i = 0; i < cvar_rule_count; ++i) {
        const cvar_rule_t *r = cvar_rules[i];
        if (r->module != p->module)
            continue;
        printf("  %s %s = \"%s\"\n", r->mode == CVAR_FORCE ? "force" : "default", r->name, r->value);
        has_cvars = true;
        ++changes;
    }
    if (has_cvars && !add_payload(image))
        return false;
    for (size_t i = 0; i < code_rule_count; ++i) {
        const code_rule_t *r = code_rules[i];
        size_t offset;
        if (!r->enabled || r->module != p->module)
            continue;
        if (!image_offset(image, r->rva, r->size, &offset))
            return false;
        if (memcmp(image->data + offset, r->expected, r->size)) {
            fprintf(stderr, "Unexpected bytes for %s at RVA 0x%08" PRIx32 "\n", r->name, r->rva);
            return false;
        }
        memcpy(image->data + offset, r->replacement, r->size);
        printf("  code 0x%08" PRIx32 ": %s\n", r->rva, r->name);
        ++changes;
    }
    if (changes)
        update_checksum(image);
    else
        puts("  no rules enabled; output will be an unchanged copy");
    return true;
}
