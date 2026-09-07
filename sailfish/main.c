#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef void *(*android_dlopen_fn)(const char *name, int flags);
typedef void *(*android_dlsym_fn)(void *handle, const char *name);
typedef int (*probe_fn)(char *out, size_t out_size);
typedef int (*capture_fn)(const char *camera_id, int width, int height,
                          const char *raw_path, const char *metadata_path,
                          int timeout_ms, char *out, size_t out_size);
typedef int (*capture_focus_fn)(
    const char *camera_id, int width, int height,
    const char *raw_path, const char *metadata_path, int timeout_ms,
    int focus_mode, float focus_distance_diopters, int focus_timeout_ms,
    int capture_on_focus_failure, char *out, size_t out_size);
typedef const char *(*version_fn)(void);
typedef void (*droid_media_init_fn)(void);

enum focus_mode {
    FOCUS_NONE = 0,
    FOCUS_AUTO = 1,
    FOCUS_CONTINUOUS = 2,
    FOCUS_MANUAL = 3,
    FOCUS_INFINITY = 4,
};

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage:\n"
            "  %s\n"
            "  %s --capture --output PREFIX [--camera ID] [--size WIDTHxHEIGHT]\n"
            "     [--timeout SECONDS] [--focus MODE] [--focus-distance D]\n"
            "     [--focus-timeout SECONDS] [--focus-failure capture|abort]\n"
            "     [--force]\n\n"
            "Without arguments, print Camera2 capabilities. Capture mode creates\n"
            "PREFIX.raw16 and PREFIX.json. Focus modes are none (default), auto,\n"
            "continuous, manual, and infinity. Manual distance is in diopters.\n",
            program, program);
}

static int parse_positive_int(const char *text, int *value)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno || !end || *end || parsed <= 0 || parsed > INT_MAX) {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

static int parse_size(const char *text, int *width, int *height)
{
    char tail = '\0';
    if (sscanf(text, "%dx%d%c", width, height, &tail) != 2 ||
            *width <= 0 || *height <= 0) {
        return -1;
    }
    return 0;
}

static int parse_nonnegative_float(const char *text, float *value)
{
    char *end = NULL;
    errno = 0;
    float parsed = strtof(text, &end);
    if (errno || !end || *end || parsed < 0.0f || parsed > 10000.0f ||
            parsed != parsed) {
        return -1;
    }
    *value = parsed;
    return 0;
}

static int parse_focus_mode(const char *text, int *mode)
{
    if (!strcmp(text, "none")) {
        *mode = FOCUS_NONE;
    } else if (!strcmp(text, "auto")) {
        *mode = FOCUS_AUTO;
    } else if (!strcmp(text, "continuous")) {
        *mode = FOCUS_CONTINUOUS;
    } else if (!strcmp(text, "manual")) {
        *mode = FOCUS_MANUAL;
    } else if (!strcmp(text, "infinity")) {
        *mode = FOCUS_INFINITY;
    } else {
        return -1;
    }
    return 0;
}

static void *open_libhybris(const char **loaded_name)
{
    const char *override = getenv("SFOS_LIBHYBRIS");
    const char *candidates[] = {
        override,
        "libhybris-common.so.1",
        "libhybris-common.so",
        NULL,
    };

    for (size_t index = 0; candidates[index] || index == 0; ++index) {
        if (!candidates[index]) {
            continue;
        }
        void *handle = dlopen(candidates[index], RTLD_NOW | RTLD_LOCAL);
        if (handle) {
            *loaded_name = candidates[index];
            return handle;
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc > 1 && (!strcmp(argv[1], "--help") ||
                     !strcmp(argv[1], "-h"))) {
        usage(stdout, argv[0]);
        return 0;
    }

    const char *hybris_name = NULL;
    void *hybris = open_libhybris(&hybris_name);
    if (!hybris) {
        fprintf(stderr, "Unable to load libhybris-common: %s\n", dlerror());
        return 10;
    }

    android_dlopen_fn android_dlopen =
        (android_dlopen_fn)dlsym(hybris, "android_dlopen");
    android_dlsym_fn android_dlsym =
        (android_dlsym_fn)dlsym(hybris, "android_dlsym");
    if (!android_dlopen || !android_dlsym) {
        fprintf(stderr, "%s does not export android_dlopen/android_dlsym\n",
                hybris_name);
        return 11;
    }

    /* Match the initialization used by normal gst-droid clients. */
    void *droidmedia = android_dlopen("libdroidmedia.so", RTLD_NOW);
    if (droidmedia) {
        droid_media_init_fn initialize =
            (droid_media_init_fn)android_dlsym(droidmedia, "_droid_media_init");
        if (initialize) {
            initialize();
        } else {
            fprintf(stderr,
                    "Warning: libdroidmedia has no _droid_media_init symbol\n");
        }
    } else {
        fprintf(stderr,
                "Warning: could not preload libdroidmedia; continuing probe\n");
    }

    const char *bridge_name = getenv("SFOS_CAMERA2_BRIDGE");
    if (!bridge_name || !*bridge_name) {
        bridge_name = "libsfoscamera2.so";
    }

    void *bridge = android_dlopen(bridge_name, RTLD_NOW);
    if (!bridge) {
        fprintf(stderr,
                "Android linker could not load %s. Install it under "
                "/usr/libexec/droid-hybris/system/lib64/.\n",
                bridge_name);
        return 12;
    }

    probe_fn probe = (probe_fn)android_dlsym(bridge, "sfos_camera2_probe");
    capture_fn capture =
        (capture_fn)android_dlsym(bridge, "sfos_camera2_capture_raw");
    capture_focus_fn capture_focus = (capture_focus_fn)android_dlsym(
        bridge, "sfos_camera2_capture_raw_focus");
    version_fn version =
        (version_fn)android_dlsym(bridge, "sfos_camera2_bridge_version");
    if (!probe || !capture || !version) {
        fprintf(stderr, "%s does not export the expected bridge API\n",
                bridge_name);
        return 13;
    }

    char json[32768] = {0};
    int result;

    if (argc == 1) {
        result = probe(json, sizeof(json));
    } else if (!strcmp(argv[1], "--capture")) {
        const char *camera_id = "0";
        const char *prefix = NULL;
        int width = 4096;
        int height = 3072;
        int timeout_seconds = 30;
        int focus_mode = FOCUS_NONE;
        float focus_distance = 0.0f;
        int focus_timeout_seconds = 3;
        int capture_on_focus_failure = 1;
        int force = 0;

        for (int index = 2; index < argc; ++index) {
            if (!strcmp(argv[index], "--camera") && index + 1 < argc) {
                camera_id = argv[++index];
            } else if (!strcmp(argv[index], "--size") && index + 1 < argc) {
                if (parse_size(argv[++index], &width, &height) != 0) {
                    fprintf(stderr, "Invalid --size value\n");
                    return 2;
                }
            } else if (!strcmp(argv[index], "--output") && index + 1 < argc) {
                prefix = argv[++index];
            } else if (!strcmp(argv[index], "--timeout") && index + 1 < argc) {
                if (parse_positive_int(argv[++index], &timeout_seconds) != 0 ||
                        timeout_seconds > INT_MAX / 1000) {
                    fprintf(stderr, "Invalid --timeout value\n");
                    return 2;
                }
            } else if (!strcmp(argv[index], "--focus") && index + 1 < argc) {
                if (parse_focus_mode(argv[++index], &focus_mode) != 0) {
                    fprintf(stderr, "Invalid --focus mode\n");
                    return 2;
                }
            } else if (!strcmp(argv[index], "--focus-distance") &&
                       index + 1 < argc) {
                if (parse_nonnegative_float(argv[++index],
                                            &focus_distance) != 0) {
                    fprintf(stderr, "Invalid --focus-distance value\n");
                    return 2;
                }
            } else if (!strcmp(argv[index], "--focus-timeout") &&
                       index + 1 < argc) {
                if (parse_positive_int(argv[++index],
                                       &focus_timeout_seconds) != 0 ||
                        focus_timeout_seconds > INT_MAX / 1000) {
                    fprintf(stderr, "Invalid --focus-timeout value\n");
                    return 2;
                }
            } else if (!strcmp(argv[index], "--focus-failure") &&
                       index + 1 < argc) {
                const char *policy = argv[++index];
                if (!strcmp(policy, "capture")) {
                    capture_on_focus_failure = 1;
                } else if (!strcmp(policy, "abort")) {
                    capture_on_focus_failure = 0;
                } else {
                    fprintf(stderr,
                            "--focus-failure must be capture or abort\n");
                    return 2;
                }
            } else if (!strcmp(argv[index], "--force")) {
                force = 1;
            } else {
                fprintf(stderr, "Unknown or incomplete option: %s\n",
                        argv[index]);
                usage(stderr, argv[0]);
                return 2;
            }
        }
        if (!prefix || !*prefix) {
            fprintf(stderr, "--output PREFIX is required for capture\n");
            return 2;
        }

        char raw_path[PATH_MAX];
        char metadata_path[PATH_MAX];
        if (snprintf(raw_path, sizeof(raw_path), "%s.raw16", prefix) >=
                    (int)sizeof(raw_path) ||
                snprintf(metadata_path, sizeof(metadata_path), "%s.json", prefix) >=
                    (int)sizeof(metadata_path)) {
            fprintf(stderr, "Output prefix is too long\n");
            return 2;
        }
        if (!force && (access(raw_path, F_OK) == 0 ||
                       access(metadata_path, F_OK) == 0)) {
            fprintf(stderr,
                    "Output exists; choose another prefix or pass --force\n");
            return 3;
        }

        if (capture_focus) {
            result = capture_focus(
                camera_id, width, height, raw_path, metadata_path,
                timeout_seconds * 1000, focus_mode, focus_distance,
                focus_timeout_seconds * 1000, capture_on_focus_failure,
                json, sizeof(json));
        } else if (focus_mode == FOCUS_NONE) {
            result = capture(camera_id, width, height, raw_path, metadata_path,
                             timeout_seconds * 1000, json, sizeof(json));
        } else {
            fprintf(stderr,
                    "The installed bridge does not support focus control. "
                    "Install libsfoscamera2.so 0.3.0 or newer.\n");
            return 14;
        }
    } else {
        usage(stderr, argv[0]);
        return 2;
    }

    if (json[0]) {
        puts(json);
    }
    if (result != 0) {
        fprintf(stderr, "Camera2 bridge %s failed with code %d\n",
                version(), result);
        return 20 - result;
    }

    return 0;
}
