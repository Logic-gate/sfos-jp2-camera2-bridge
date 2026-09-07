/*
 * Camera2 capability probe for the Sailfish bridge experiment 
 *
 * This code runs on the Android side of the bridge. It enumerates the
 * available cameras and reads Camera2 characteristics: 
 * returns a small JSON description of their hardware level and advertised RAW output sizes.
 */
#include "camera2_bridge.h"

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraMetadataTags.h>
#include <media/NdkImage.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct json_writer {
    char *data;
    size_t capacity;
    size_t length;
    bool truncated;
};

static void json_appendf(struct json_writer *writer, const char *format, ...)
{
    if (writer->truncated || writer->length >= writer->capacity) {
        writer->truncated = true;
        return;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(writer->data + writer->length,
                            writer->capacity - writer->length,
                            format, args);
    va_end(args);

    if (written < 0 || (size_t)written >= writer->capacity - writer->length) {
        writer->length = writer->capacity - 1;
        writer->data[writer->length] = '\0';
        writer->truncated = true;
        return;
    }

    writer->length += (size_t)written;
}

static void json_string(struct json_writer *writer, const char *value)
{
    const unsigned char *cursor = (const unsigned char *)(value ? value : "");
    json_appendf(writer, "\"");
    while (*cursor && !writer->truncated) {
        switch (*cursor) {
        case '\"': json_appendf(writer, "\\\""); break;
        case '\\': json_appendf(writer, "\\\\"); break;
        case '\b': json_appendf(writer, "\\b"); break;
        case '\f': json_appendf(writer, "\\f"); break;
        case '\n': json_appendf(writer, "\\n"); break;
        case '\r': json_appendf(writer, "\\r"); break;
        case '\t': json_appendf(writer, "\\t"); break;
        default:
            if (*cursor < 0x20) {
                json_appendf(writer, "\\u%04x", (unsigned int)*cursor);
            } else {
                json_appendf(writer, "%c", *cursor);
            }
        }
        ++cursor;
    }
    json_appendf(writer, "\"");
}

static bool metadata_has_u8(const ACameraMetadata *metadata,
                            uint32_t tag, uint8_t wanted)
{
    ACameraMetadata_const_entry entry;
    if (ACameraMetadata_getConstEntry(metadata, tag, &entry) != ACAMERA_OK) {
        return false;
    }

    for (uint32_t index = 0; index < entry.count; ++index) {
        if (entry.data.u8[index] == wanted) {
            return true;
        }
    }
    return false;
}

static int32_t metadata_first_u8(const ACameraMetadata *metadata,
                                 uint32_t tag, int32_t fallback)
{
    ACameraMetadata_const_entry entry;
    if (ACameraMetadata_getConstEntry(metadata, tag, &entry) != ACAMERA_OK ||
            entry.count == 0) {
        return fallback;
    }
    return entry.data.u8[0];
}

static float metadata_first_float(const ACameraMetadata *metadata,
                                  uint32_t tag, float fallback)
{
    ACameraMetadata_const_entry entry;
    if (ACameraMetadata_getConstEntry(metadata, tag, &entry) != ACAMERA_OK ||
            entry.count == 0) {
        return fallback;
    }
    return entry.data.f[0];
}

/* https://developer.android.com/ndk/reference/group/camera#acamera_metadata_enum_acamera_info_supported_hardware_level */

static const char *hardware_level_name(int32_t level)
{
    switch (level) {
    case ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY: return "legacy";
    case ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED: return "limited";
    case ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_FULL: return "full";
    case ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_3: return "level_3";
#ifdef ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL
    case ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL: return "external";
#endif
    default: return "unknown";
    }
}

static const char *raw_format_name(int32_t format)
{
    switch (format) {
    case AIMAGE_FORMAT_RAW16: return "RAW16";
    case AIMAGE_FORMAT_RAW10: return "RAW10";
    case AIMAGE_FORMAT_RAW12: return "RAW12";
    case AIMAGE_FORMAT_RAW_PRIVATE: return "RAW_PRIVATE";
    default: return NULL;
    }
}

static const char *af_mode_name(uint8_t mode)
{
    switch (mode) {
    case ACAMERA_CONTROL_AF_MODE_OFF: return "off";
    case ACAMERA_CONTROL_AF_MODE_AUTO: return "auto";
    case ACAMERA_CONTROL_AF_MODE_MACRO: return "macro";
    case ACAMERA_CONTROL_AF_MODE_CONTINUOUS_VIDEO: return "continuous_video";
    case ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE:
        return "continuous_picture";
    case ACAMERA_CONTROL_AF_MODE_EDOF: return "edof";
    default: return "unknown";
    }
}

static const char *focus_calibration_name(int32_t calibration)
{
    switch (calibration) {
        //  acamera_metadata_enum_acamera_lens_info_focus_distance_calibration
        // Setting the lens to the same focus distance on separate occasions may
        // result in a different real focus distance, depending on factors such as the orientation
        // of the device, the age of the focusing mechanism, and the device temperature.
        
    case ACAMERA_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_UNCALIBRATED:
        return "uncalibrated";
    case ACAMERA_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_APPROXIMATE:
        return "approximate";
    case ACAMERA_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_CALIBRATED:
        return "calibrated";
    default: return "unknown";
    }
}

static void append_focus_capabilities(struct json_writer *writer,
                                      const ACameraMetadata *metadata)
{
    ACameraMetadata_const_entry entry;
    bool first = true;
    float minimum_distance = metadata_first_float(
        metadata, ACAMERA_LENS_INFO_MINIMUM_FOCUS_DISTANCE, 0.0f);
    int32_t calibration = metadata_first_u8(
        metadata, ACAMERA_LENS_INFO_FOCUS_DISTANCE_CALIBRATION, -1);

    json_appendf(writer, "{\"af_modes\":[");
    if (ACameraMetadata_getConstEntry(
            metadata, ACAMERA_CONTROL_AF_AVAILABLE_MODES,
            &entry) == ACAMERA_OK) {
        for (uint32_t index = 0; index < entry.count; ++index) {
            uint8_t mode = entry.data.u8[index];
            json_appendf(writer, "%s{\"name\":", first ? "" : ",");
            json_string(writer, af_mode_name(mode));
            json_appendf(writer, ",\"value\":%u}", (unsigned int)mode);
            first = false;
        }
    }
    json_appendf(writer,
                 "],\"minimum_focus_distance_diopters\":%.9g,"
                 "\"fixed_focus\":%s,\"focus_distance_calibration\":",
                 minimum_distance, minimum_distance > 0.0f ? "false" : "true");
    json_string(writer, focus_calibration_name(calibration));
    json_appendf(writer, ",\"focus_distance_calibration_value\":%d}",
                 calibration);
}

static void append_raw_outputs(struct json_writer *writer,
                               const ACameraMetadata *metadata)
{
    ACameraMetadata_const_entry entry;
    bool first = true;

    json_appendf(writer, "[");
    if (ACameraMetadata_getConstEntry(
            metadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
            &entry) == ACAMERA_OK) {
        for (uint32_t index = 0; index + 3 < entry.count; index += 4) {
            int32_t format = entry.data.i32[index];
            int32_t width = entry.data.i32[index + 1];
            int32_t height = entry.data.i32[index + 2];
            int32_t direction = entry.data.i32[index + 3];
            const char *name = raw_format_name(format);

            if (!name || direction !=
                    ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) {
                continue;
            }

            json_appendf(writer, "%s{\"format\":", first ? "" : ",");
            json_string(writer, name);
            json_appendf(writer,
                         ",\"format_value\":%d,\"width\":%d,\"height\":%d}",
                         format, width, height);
            first = false;
        }
    }
    json_appendf(writer, "]");
}

const char *sfos_camera2_bridge_version(void)
{
    return "0.3.0";
}

int sfos_camera2_probe(char *out, size_t out_size)
{
    if (!out || out_size < 2) {
        return -1;
    }

    struct json_writer writer = {
        .data = out,
        .capacity = out_size,
        .length = 0,
        .truncated = false,
    };
    out[0] = '\0';

    ACameraManager *manager = ACameraManager_create();
    if (!manager) {
        snprintf(out, out_size,
                 "{\"status\":\"error\",\"stage\":\"manager_create\"}");
        return -2;
    }

    ACameraIdList *camera_ids = NULL;
    camera_status_t status =
        ACameraManager_getCameraIdList(manager, &camera_ids);
    if (status != ACAMERA_OK || !camera_ids) {
        snprintf(out, out_size,
                 "{\"status\":\"error\",\"stage\":\"camera_list\","
                 "\"camera_status\":%d}", status);
        ACameraManager_delete(manager);
        return -3;
    }

    json_appendf(&writer,
                 "{\"status\":\"ok\",\"bridge_version\":\"%s\","
                 "\"camera_count\":%d,\"cameras\":[",
                 sfos_camera2_bridge_version(), camera_ids->numCameras);

    for (int camera_index = 0;
         camera_index < camera_ids->numCameras;
         ++camera_index) {
        const char *camera_id = camera_ids->cameraIds[camera_index];
        ACameraMetadata *metadata = NULL;

        if (camera_index != 0) {
            json_appendf(&writer, ",");
        }
        json_appendf(&writer, "{\"id\":");
        json_string(&writer, camera_id);

        status = ACameraManager_getCameraCharacteristics(
            manager, camera_id, &metadata);
        if (status != ACAMERA_OK || !metadata) {
            json_appendf(&writer,
                         ",\"status\":\"error\",\"camera_status\":%d}",
                         status);
            continue;
        }

        bool raw_capability = metadata_has_u8(
            metadata, ACAMERA_REQUEST_AVAILABLE_CAPABILITIES,
            ACAMERA_REQUEST_AVAILABLE_CAPABILITIES_RAW);
        int32_t hardware_level = metadata_first_u8(
            metadata, ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL, -1);

        json_appendf(&writer,
                     ",\"status\":\"ok\",\"raw_capability\":%s,"
                     "\"hardware_level\":",
                     raw_capability ? "true" : "false");
        json_string(&writer, hardware_level_name(hardware_level));
        json_appendf(&writer, ",\"hardware_level_value\":%d,\"focus\":",
                     hardware_level);
        append_focus_capabilities(&writer, metadata);
        json_appendf(&writer, ",\"raw_outputs\":");
        append_raw_outputs(&writer, metadata);
        json_appendf(&writer, "}");

        ACameraMetadata_free(metadata);
    }

    json_appendf(&writer, "]}");
    ACameraManager_deleteCameraIdList(camera_ids);
    ACameraManager_delete(manager);

    return writer.truncated ? -4 : 0;
}
