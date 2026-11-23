/* fileno() and fstat() are POSIX, hidden by strict -std=c17. */
#define _POSIX_C_SOURCE 200809L

#include "patcher.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Retail files used by reigw. Fingerprints cover every byte, so an already
 * patched file or a different release cannot accidentally match this layout.
 * FNV-1a is an identity guard against accidental differences, not a signature.
 */
static const profile_t profiles[] = {
    { GAME, "gamex86.dll", 930304, UINT64_C(0x58458bb427dc169d),
      0x4fbdcafd, 0x20000000, 0x2ca60, 0xb58bc, 4, 5, 6 },
    { CGAME, "cgamex86.dll", 911360, UINT64_C(0xcee1c445aa4ed8a5),
      0x4fbdcb0d, 0x30000000, 0x329e0, 0xc8b50, 3, 4, 5 },
    /* UI uses instruction patches only; its CVAR payload metadata is unset. */
    { .module = UI, .name = "uix86.dll", .size = 499712,
      .fingerprint = UINT64_C(0x921187794457deb6),
      .timestamp = 0x4fbdcaee, .image_base = 0x10000000 },
    /* Engine defaults are instruction patches without a DLL syscall payload. */
    { .module = ENGINE, .name = "igwarlord.exe", .size = 2414080,
      .fingerprint = UINT64_C(0x9637644919c69121),
      .timestamp = 0x4fbeadb4, .image_base = 0x00400000 },
    /* The dedicated server's own igwarlord.exe, linked from another tree. */
    { .module = SERVER, .name = "igwarlord-server.exe", .size = 1485312,
      .fingerprint = UINT64_C(0x3fc347b3d1f74390),
      .timestamp = 0x4f9ff56d, .image_base = 0x00400000 },
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

/* Headers map at their file offsets; sections map only file-backed bytes.
 * An RVA in .data's uninitialized tail must never be patched on disk.
 */
bool image_offset(const image_t *image, uint32_t rva, size_t size, size_t *offset)
{
    if (fits(rva, size, get32(image->data + image->optional + 60)) &&
        fits(rva, size, image->size)) {
        *offset = rva;
        return true;
    }
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
    /* Query the open stream so the mode belongs to the bytes being read. */
    struct stat info;
    if (fstat(fileno(file), &info) != 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        goto done;
    }
    /* Drop setuid/setgid/sticky bits, only plain permissions carry over. */
    image->mode = (unsigned int)(info.st_mode & 0777);
    if (fseek(file, 0, SEEK_END) == 0)
        length = ftell(file);
    /* Supported inputs are below 4 MiB. Bound allocation before parsing. */
    if (length < 0 || length > 4 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
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
        fprintf(stderr, "%s: unsupported or modified binary (%zu bytes, FNV-1a %016" PRIx64 ")\n",
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
    /* Too large for comfortable stack use; images are patched one by one. */
    static payload_t payload;
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

/* Return the header of the section whose virtual range holds rva. */
static const unsigned char *section_of(const image_t *image, uint32_t rva)
{
    for (uint16_t i = 0; i < image->section_count; ++i) {
        const unsigned char *s = image->data + image->sections + (size_t)i * 40;
        uint32_t start = get32(s + 12), size = get32(s + 8);
        if (rva >= start && rva - start < size)
            return s;
    }
    return NULL;
}

/* Append HIGHLOW base relocations for count consecutive dwords at first.
 * The blocks go directly behind the existing directory, into zero bytes
 * that the section holding it already maps, so no header but the
 * directory size changes. Retail .reloc sections have such slack.
 */
static bool add_relocations(image_t *image, uint32_t first, size_t count)
{
    unsigned char *opt = image->data + image->optional;
    uint32_t dir_rva = get32(opt + 136), dir_size = get32(opt + 140);
    /* One block per page, each at most a header, one entry per slot and
     * one padding entry.
     */
    unsigned char blocks[(8 + 2 * IMPORT_FUNCTIONS_MAX + 2) * IMPORT_FUNCTIONS_MAX];
    size_t size = 0, block = 0;
    uint32_t page = 0;
    if (!count || count > IMPORT_FUNCTIONS_MAX || dir_size % 4) {
        fprintf(stderr, "Unsupported base relocation directory\n");
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t rva = first + (uint32_t)i * 4;
        if (!i || (rva & ~0xfffu) != page) {
            /* Close the previous block, padded to a dword with an
             * IMAGE_REL_BASED_ABSOLUTE entry, which the loader skips.
             */
            if (i) {
                if ((size - block) % 4) {
                    put16(blocks + size, 0);
                    size += 2;
                }
                put32(blocks + block + 4, (uint32_t)(size - block));
            }
            page = rva & ~0xfffu;
            block = size;
            put32(blocks + size, page);
            size += 8;
        }
        put16(blocks + size, (uint16_t)(0x3000u | (rva & 0xfffu)));
        size += 2;
    }
    if ((size - block) % 4) {
        put16(blocks + size, 0);
        size += 2;
    }
    put32(blocks + block + 4, (uint32_t)(size - block));

    /* The appended range must lie in the directory's own section, inside
     * both its virtual and its file-backed size, and be unused.
     */
    const unsigned char *section = section_of(image, dir_rva);
    size_t offset;
    if (!section || dir_rva > UINT32_MAX - dir_size - size ||
        dir_rva + dir_size + size > (uint64_t)get32(section + 12) + get32(section + 8) ||
        !image_offset(image, dir_rva + dir_size, size, &offset)) {
        fprintf(stderr, "No room for %zu bytes of base relocations\n", size);
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        if (image->data[offset + i]) {
            fprintf(stderr, "Base relocation slack is not empty\n");
            return false;
        }
    }
    memcpy(image->data + offset, blocks, size);
    put32(opt + 140, dir_size + (uint32_t)size);
    return true;
}

/* Find whether any base relocation of the directory touches the dword at
 * rva. Only HIGHLOW and ABSOLUTE entries are expected in these images.
 */
static bool relocation_overlaps(const image_t *image, uint32_t rva, bool *overlaps)
{
    const unsigned char *opt = image->data + image->optional;
    uint32_t dir_rva = get32(opt + 136), dir_size = get32(opt + 140);
    size_t dir;
    *overlaps = false;
    if (!image_offset(image, dir_rva, dir_size, &dir))
        goto invalid;
    for (uint32_t at = 0; at < dir_size;) {
        if (dir_size - at < 8)
            goto invalid;
        uint32_t page = get32(image->data + dir + at), size = get32(image->data + dir + at + 4);
        if (size < 8 || size % 2 || size > dir_size - at)
            goto invalid;
        for (uint32_t i = 8; i < size; i += 2) {
            uint16_t entry = get16(image->data + dir + at + i);
            uint32_t target = page + (entry & 0xfffu);
            if (!(entry >> 12))
                continue;
            if (entry >> 12 != 3)
                goto invalid;
            /* Both are four-byte slots: they overlap within three bytes. */
            if ((uint64_t)target + 4 > rva && (uint64_t)rva + 4 > target)
                *overlaps = true;
        }
        at += size;
    }
    return true;
invalid:
    fprintf(stderr, "Unsupported base relocation directory\n");
    return false;
}

/* Write an address rule's dword and add its relocation. The address must
 * point into the image, and the slot must not be relocated yet, or a rebase
 * would add the delta twice.
 */
static bool relocate_address(image_t *image, const address_rule_t *rule)
{
    uint32_t base = image->profile->image_base;
    uint32_t size = get32(image->data + image->optional + 56);
    size_t offset;
    bool overlaps;
    if (!image_offset(image, rule->rva, 4, &offset))
        return false;
    if (get32(image->data + offset) != rule->expected) {
        fprintf(stderr, "Unexpected bytes for %s at RVA 0x%08" PRIx32 "\n", rule->name, rule->rva);
        return false;
    }
    if (rule->address < base || rule->address - base >= size) {
        fprintf(stderr, "Address 0x%08" PRIx32 " of %s is outside the image\n",
                rule->address, rule->name);
        return false;
    }
    if (!relocation_overlaps(image, rule->rva, &overlaps))
        return false;
    if (overlaps) {
        fprintf(stderr, "RVA 0x%08" PRIx32 " of %s is already relocated\n", rule->rva, rule->name);
        return false;
    }
    if (!add_relocations(image, rule->rva, 1))
        return false;
    put32(image->data + offset, rule->address);
    return true;
}

/* Compare a NUL-terminated name in the image to a rule's, ignoring ASCII
 * case like the Windows loader does for DLL names.
 */
static bool same_dll(const image_t *image, uint32_t rva, const char *dll)
{
    size_t length = strlen(dll), offset;
    if (!image_offset(image, rva, length + 1, &offset))
        return false;
    for (size_t i = 0; i <= length; ++i) {
        unsigned char a = image->data[offset + i], b = (unsigned char)dll[i];
        if (a >= 'A' && a <= 'Z')
            a = (unsigned char)(a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z')
            b = (unsigned char)(b + 'a' - 'A');
        if (a != b)
            return false;
    }
    return true;
}

/* Remove one DLL's descriptor from the import directory and bind each of
 * its IAT slots to a stub that returns 0, so the DLL need not exist.
 */
static bool drop_import(image_t *image, const import_rule_t *rule)
{
    unsigned char *opt = image->data + image->optional;
    uint32_t dir_rva = get32(opt + 104), dir_size = get32(opt + 108);
    size_t dir, count = dir_size / 20, end = count, found = count;
    if (!image_offset(image, dir_rva, dir_size, &dir) || count < 2)
        goto invalid;
    /* The loader stops at the first all-zero descriptor. */
    for (size_t i = 0; i < count; ++i) {
        const unsigned char *d = image->data + dir + i * 20;
        bool zero = true;
        for (size_t k = 0; k < 20; ++k)
            zero = zero && !d[k];
        if (zero) {
            end = i;
            break;
        }
        if (same_dll(image, get32(d + 12), rule->dll)) {
            if (found != count)
                goto invalid;
            found = i;
        }
    }
    if (end == count || found == count) {
        fprintf(stderr, "Import of %s not found\n", rule->dll);
        return false;
    }

    /* Only unbound descriptors with a separate lookup table qualify, so the
     * on-disk IAT still names every function and can be checked against it.
     */
    unsigned char *d = image->data + dir + found * 20;
    uint32_t lookup_rva = get32(d), iat_rva = get32(d + 16);
    size_t lookup, iat, functions = rule->functions;
    if (!lookup_rva || get32(d + 4) ||
        !image_offset(image, lookup_rva, (functions + 1) * 4, &lookup) ||
        !image_offset(image, iat_rva, (functions + 1) * 4, &iat))
        goto invalid;
    for (size_t i = 0; i < functions; ++i) {
        uint32_t entry = get32(image->data + lookup + i * 4);
        if (!entry || (entry & 0x80000000u) || entry != get32(image->data + iat + i * 4))
            goto invalid;
    }
    if (get32(image->data + lookup + functions * 4) || get32(image->data + iat + functions * 4)) {
        fprintf(stderr, "%s imports more than %zu functions\n", rule->dll, functions);
        return false;
    }

    /* The stub replaces int3 padding in an executable section. */
    static const unsigned char padding[IMPORT_STUB_SIZE] = { 0xcc, 0xcc, 0xcc };
    static const unsigned char stub[IMPORT_STUB_SIZE] = { 0x31, 0xc0, 0xc3 };
    const unsigned char *code = section_of(image, rule->stub_rva);
    size_t stub_offset;
    if (!code || !(get32(code + 36) & 0x20000000u) ||
        !image_offset(image, rule->stub_rva, IMPORT_STUB_SIZE, &stub_offset) ||
        memcmp(image->data + stub_offset, padding, IMPORT_STUB_SIZE)) {
        fprintf(stderr, "Unexpected bytes for the %s stub at RVA 0x%08" PRIx32 "\n",
                rule->dll, rule->stub_rva);
        return false;
    }
    if (!add_relocations(image, iat_rva, functions))
        return false;
    memcpy(image->data + stub_offset, stub, IMPORT_STUB_SIZE);
    for (size_t i = 0; i < functions; ++i)
        put32(image->data + iat + i * 4, image->profile->image_base + rule->stub_rva);

    /* Close the gap. This moves the all-zero terminator up by one
     * descriptor; its old position stays zero.
     */
    memmove(d, d + 20, (end - found) * 20);
    put32(opt + 108, dir_size - 20);
    return true;
invalid:
    fprintf(stderr, "Unexpected import directory for %s\n", rule->dll);
    return false;
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
    printf("%s: known retail binary\n", p->name);
    bool has_payload = false;
    for (size_t i = 0; i < payload_feature_count; ++i) {
        const payload_feature_t *feature = payload_features[i];
        if (feature->module != p->module)
            continue;
        printf("  payload %s\n", feature->name);
        has_payload = true;
        ++changes;
    }
    for (size_t i = 0; i < cvar_rule_count; ++i) {
        const cvar_rule_t *r = cvar_rules[i];
        if (r->module != p->module)
            continue;
        printf("  %s %s = \"%s\"\n", r->mode == CVAR_FORCE ? "force" : "default", r->name, r->value);
        has_payload = true;
        ++changes;
    }
    /* Drop imports first: the payload section would follow .reloc, whose
     * slack takes the stubs' base relocations.
     */
    for (size_t i = 0; i < import_rule_count; ++i) {
        const import_rule_t *r = import_rules[i];
        if (r->module != p->module)
            continue;
        if (!drop_import(image, r))
            return false;
        printf("  import %s dropped, %zu functions -> stub 0x%08" PRIx32 ": %s\n",
               r->dll, r->functions, r->stub_rva, r->name);
        ++changes;
    }
    if (has_payload && !add_payload(image))
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
    /* After the payload, whose section then counts as part of the image. */
    for (size_t i = 0; i < address_rule_count; ++i) {
        const address_rule_t *r = address_rules[i];
        if (r->module != p->module)
            continue;
        if (!relocate_address(image, r))
            return false;
        printf("  address 0x%08" PRIx32 " = 0x%08" PRIx32 ": %s\n", r->rva, r->address, r->name);
        ++changes;
    }
    if (changes)
        update_checksum(image);
    else
        puts("  no rules enabled; output will be an unchanged copy");
    return true;
}
