#include "patcher.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *input;
    char *output;
    image_t image;
    bool created;
} job_t;

static void usage(FILE *out)
{
    fputs("Usage: igw-monkey-patcher [--dry-run] [--output-dir DIR] DLL...\n"
          "       igw-monkey-patcher --list\n"
          "\n"
          "Writes NAME.patched.dll beside each input, or its original basename\n"
          "inside an existing output directory. Never overwrites a file.\n"
          "Edit the feature files in patches/ and rebuild to change patches.\n", out);
}

static const char *basename_of(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\')
            name = p + 1;
    }
    return name;
}

static char *output_path(const char *input, const char *directory)
{
    size_t length = strlen(input);
    if (directory) {
        const char *name = basename_of(input);
        size_t size = strlen(directory) + strlen(name) + 2;
        char *result = malloc(size);
        if (result)
            snprintf(result, size, "%s/%s", directory, name);
        return result;
    }
    /* Keep arbitrary filenames usable; only strip a .dll suffix. */
    if (length >= 4 && input[length - 4] == '.' &&
        (input[length - 3] == 'd' || input[length - 3] == 'D') &&
        (input[length - 2] == 'l' || input[length - 2] == 'L') &&
        (input[length - 1] == 'l' || input[length - 1] == 'L'))
        length -= 4;
    const char suffix[] = ".patched.dll";
    char *result = malloc(length + sizeof(suffix));
    if (result) {
        memcpy(result, input, length);
        memcpy(result + length, suffix, sizeof(suffix));
    }
    return result;
}

static const char *module_name(module_t module)
{
    switch (module) {
    case GAME: return "gamex86";
    case CGAME: return "cgamex86";
    case UI: return "uix86";
    }
    return "unknown";
}

static void list_rules(void)
{
    for (size_t i = 0; i < cvar_rule_count; ++i) {
        const cvar_rule_t *r = cvar_rules[i];
        printf("%s: %s %s = \"%s\"\n", module_name(r->module),
               r->mode == CVAR_FORCE ? "force" : "default", r->name, r->value);
    }
    for (size_t i = 0; i < code_rule_count; ++i) {
        const code_rule_t *r = code_rules[i];
        printf("%s: %s code %s\n", module_name(r->module),
               r->enabled ? "enabled" : "disabled", r->name);
    }
}

int main(int argc, char **argv)
{
    const char *directory = NULL;
    bool dry_run = false, list = false, options = true;
    size_t count = 0;
    int status = EXIT_FAILURE;
    job_t *jobs = calloc((size_t)argc, sizeof(*jobs));
    if (!jobs) {
        fputs("Could not allocate jobs\n", stderr);
        return status;
    }
    for (int i = 1; i < argc; ++i) {
        if (options && !strcmp(argv[i], "--")) {
            options = false;
        } else if (options && !strcmp(argv[i], "--dry-run")) {
            dry_run = true;
        } else if (options && !strcmp(argv[i], "--list")) {
            list = true;
        } else if (options && !strcmp(argv[i], "--output-dir") && i + 1 < argc) {
            directory = argv[++i];
        } else if (options && (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))) {
            usage(stdout);
            status = EXIT_SUCCESS;
            goto done;
        } else if (options && argv[i][0] == '-') {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            goto done;
        } else {
            jobs[count++].input = argv[i];
        }
    }
    if (!rules_validate())
        goto done;
    if (list) {
        list_rules();
        if (!count) {
            status = EXIT_SUCCESS;
            goto done;
        }
    }
    if (!count) {
        usage(stderr);
        goto done;
    }

    /* Prepare every image before creating any outputs. A bad second input
     * therefore cannot leave a successfully patched first DLL behind.
     */
    for (size_t i = 0; i < count; ++i) {
        jobs[i].output = output_path(jobs[i].input, directory);
        if (!jobs[i].output || !image_open(&jobs[i].image, jobs[i].input) ||
            !image_patch(&jobs[i].image))
            goto done;
        printf("  %s -> %s%s\n", jobs[i].input, jobs[i].output, dry_run ? " (dry run)" : "");
    }
    if (!dry_run) {
        /* Create each name exclusively, removing newly created outputs if a
         * later one fails. This also protects originals reached through
         * aliases, hard links or symbolic links.
         */
        for (size_t i = 0; i < count; ++i) {
            FILE *out = fopen(jobs[i].output, "wbx");
            if (!out) {
                fprintf(stderr, "%s: %s\n", jobs[i].output, strerror(errno));
                goto done;
            }
            jobs[i].created = true;
            bool ok = fwrite(jobs[i].image.data, 1, jobs[i].image.size, out) == jobs[i].image.size;
            if (fclose(out) != 0)
                ok = false;
            if (!ok) {
                fprintf(stderr, "%s: could not write complete output\n", jobs[i].output);
                goto done;
            }
        }
    }
    status = EXIT_SUCCESS;
done:
    for (size_t i = 0; i < count; ++i) {
        if (status != EXIT_SUCCESS && jobs[i].created && remove(jobs[i].output) != 0)
            fprintf(stderr, "Could not remove incomplete output %s: %s\n", jobs[i].output, strerror(errno));
        free(jobs[i].output);
        free(jobs[i].image.data);
    }
    free(jobs);
    return status;
}
