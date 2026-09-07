#ifndef SFOS_CAMERA2_BRIDGE_H
#define SFOS_CAMERA2_BRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
#define SFOS_CAMERA2_EXPORT __attribute__((visibility("default")))
#else
#define SFOS_CAMERA2_EXPORT
#endif

/*
 * 0 success, -1 invalid arguments, -2 manager creation fails,
 * -3 enum fails, -4 if output buffer is too small.
 */
SFOS_CAMERA2_EXPORT int sfos_camera2_probe(char *out, size_t out_size);

/*
 * Captures one RAW16 frame and writes the buffer plus JSON metadata.
 * The function is synchronous; timeout_ms covers session setup and capture.
 */
SFOS_CAMERA2_EXPORT int sfos_camera2_capture_raw(
    const char *camera_id,
    int width,
    int height,
    const char *raw_path,
    const char *metadata_path,
    int timeout_ms,
    char *out,
    size_t out_size);

enum sfos_camera2_focus_mode {
    SFOS_CAMERA2_FOCUS_NONE = 0,
    SFOS_CAMERA2_FOCUS_AUTO = 1,
    SFOS_CAMERA2_FOCUS_CONTINUOUS = 2,
    SFOS_CAMERA2_FOCUS_MANUAL = 3,
    SFOS_CAMERA2_FOCUS_INFINITY = 4,
};

/*
 * AUTO and CONTINUOUS create a small YUV metering stream and wait for the
 * Camera AF state machine. MANUAL uses focus_distance_diopters, while
 * INFINITY fixes the requested distance at 0.0 diopters. If
 * capture_on_focus_failure is non-zero, an AF failure or timeout is recorded
 * in the JSON metadata but the RAW capture is still attempted.
 */
SFOS_CAMERA2_EXPORT int sfos_camera2_capture_raw_focus(
    const char *camera_id,
    int width,
    int height,
    const char *raw_path,
    const char *metadata_path,
    int timeout_ms,
    int focus_mode,
    float focus_distance_diopters,
    int focus_timeout_ms,
    int capture_on_focus_failure,
    char *out,
    size_t out_size);

SFOS_CAMERA2_EXPORT const char *sfos_camera2_bridge_version(void);

#ifdef __cplusplus
}
#endif

#endif
