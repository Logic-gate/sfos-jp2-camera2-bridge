#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum pixel_color {
    PIXEL_RED,
    PIXEL_GREEN,
    PIXEL_BLUE,
};

struct conversion_config {
    int width;
    int height;
    int row_stride;
    int white_level;
    int black_level[4];
    float gains[4];
    float matrix[9];
    float exposure;
    int cfa_map;
    char cfa[8];
    char raw_path[4096];
};

static char *read_text_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || length > 1024 * 1024 ||
            fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *text = malloc((size_t)length + 1);
    if (!text) {
        fclose(file);
        return NULL;
    }
    if (fread(text, 1, (size_t)length, file) != (size_t)length) {
        free(text);
        fclose(file);
        return NULL;
    }
    text[length] = '\0';
    fclose(file);
    return text;
}

static const char *json_value(const char *json, const char *key)
{
    char pattern[128];
    if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) >=
            (int)sizeof(pattern)) {
        return NULL;
    }
    const char *value = strstr(json, pattern);
    if (!value) {
        return NULL;
    }
    value += strlen(pattern);
    while (isspace((unsigned char)*value)) {
        ++value;
    }
    if (*value++ != ':') {
        return NULL;
    }
    while (isspace((unsigned char)*value)) {
        ++value;
    }
    return value;
}

static int json_int(const char *json, const char *key, int *result)
{
    const char *value = json_value(json, key);
    char *end = NULL;
    if (!value) {
        return -1;
    }
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno || end == value || parsed < INT32_MIN || parsed > INT32_MAX) {
        return -1;
    }
    *result = (int)parsed;
    return 0;
}

static int json_string(const char *json, const char *key,
                       char *output, size_t output_size)
{
    const char *value = json_value(json, key);
    size_t used = 0;
    if (!value || *value++ != '"' || output_size == 0) {
        return -1;
    }
    while (*value && *value != '"') {
        unsigned char byte = (unsigned char)*value++;
        if (byte == '\\') {
            switch (*value++) {
            case '"': byte = '"'; break;
            case '\\': byte = '\\'; break;
            case '/': byte = '/'; break;
            case 'b': byte = '\b'; break;
            case 'f': byte = '\f'; break;
            case 'n': byte = '\n'; break;
            case 'r': byte = '\r'; break;
            case 't': byte = '\t'; break;
            default: return -1;
            }
        }
        if (used + 1 >= output_size) {
            return -1;
        }
        output[used++] = (char)byte;
    }
    if (*value != '"') {
        return -1;
    }
    output[used] = '\0';
    return 0;
}

static int json_flat_array(const char *json, const char *key,
                           float *output, int wanted)
{
    const char *value = json_value(json, key);
    if (!value || *value++ != '[') {
        return -1;
    }
    for (int index = 0; index < wanted; ++index) {
        while (isspace((unsigned char)*value)) {
            ++value;
        }
        char *end = NULL;
        output[index] = strtof(value, &end);
        if (end == value) {
            return -1;
        }
        value = end;
        while (isspace((unsigned char)*value)) {
            ++value;
        }
        if (index + 1 < wanted) {
            if (*value++ != ',') {
                return -1;
            }
        } else if (*value != ']') {
            return -1;
        }
    }
    return 0;
}

static int json_rational_matrix(const char *json, const char *key,
                                float *output)
{
    const char *value = json_value(json, key);
    if (!value || *value++ != '[') {
        return -1;
    }
    for (int index = 0; index < 9; ++index) {
        while (isspace((unsigned char)*value)) {
            ++value;
        }
        if (*value++ != '[') {
            return -1;
        }
        char *end = NULL;
        float numerator = strtof(value, &end);
        if (end == value) {
            return -1;
        }
        value = end;
        while (isspace((unsigned char)*value)) {
            ++value;
        }
        if (*value++ != ',') {
            return -1;
        }
        float denominator = strtof(value, &end);
        if (end == value || denominator == 0.0f) {
            return -1;
        }
        value = end;
        while (isspace((unsigned char)*value)) {
            ++value;
        }
        if (*value++ != ']') {
            return -1;
        }
        output[index] = numerator / denominator;
        while (isspace((unsigned char)*value)) {
            ++value;
        }
        if (index + 1 < 9) {
            if (*value++ != ',') {
                return -1;
            }
        } else if (*value != ']') {
            return -1;
        }
    }
    return 0;
}

static int load_config(const char *metadata_path,
                       struct conversion_config *config)
{
    char *json = read_text_file(metadata_path);
    if (!json) {
        fprintf(stderr, "Cannot read metadata: %s\n", metadata_path);
        return -1;
    }

    float black[4];
    int ok = json_int(json, "width", &config->width) == 0 &&
        json_int(json, "height", &config->height) == 0 &&
        json_int(json, "row_stride", &config->row_stride) == 0 &&
        json_int(json, "white_level", &config->white_level) == 0 &&
        json_string(json, "cfa", config->cfa, sizeof(config->cfa)) == 0 &&
        json_string(json, "raw_path", config->raw_path,
                    sizeof(config->raw_path)) == 0 &&
        json_flat_array(json, "black_level_pattern", black, 4) == 0 &&
        json_flat_array(json, "color_correction_gains", config->gains, 4) == 0;

    for (int index = 0; index < 4; ++index) {
        config->black_level[index] = (int)black[index];
    }
    if (json_rational_matrix(json, "capture_color_transform",
                             config->matrix) != 0) {
        memset(config->matrix, 0, sizeof(config->matrix));
        config->matrix[0] = 1.0f;
        config->matrix[4] = 1.0f;
        config->matrix[8] = 1.0f;
        fprintf(stderr, "Warning: using identity color transform\n");
    }
    free(json);

    config->cfa_map = !strcmp(config->cfa, "RGGB") ? 0 :
                      !strcmp(config->cfa, "GRBG") ? 1 :
                      !strcmp(config->cfa, "GBRG") ? 2 : 3;
    if (!ok || config->width <= 1 || config->height <= 1 ||
            config->row_stride < config->width * 2 ||
            config->white_level <= 0 ||
            config->white_level <= config->black_level[0] ||
            config->white_level <= config->black_level[1] ||
            config->white_level <= config->black_level[2] ||
            config->white_level <= config->black_level[3] ||
            (strcmp(config->cfa, "RGGB") && strcmp(config->cfa, "GRBG") &&
             strcmp(config->cfa, "GBRG") && strcmp(config->cfa, "BGGR"))) {
        fprintf(stderr, "Unsupported or incomplete metadata\n");
        return -1;
    }
    return 0;
}

static enum pixel_color pixel_color_at(const struct conversion_config *config,
                                       int x, int y)
{
    static const enum pixel_color maps[4][4] = {
        { PIXEL_RED, PIXEL_GREEN, PIXEL_GREEN, PIXEL_BLUE },
        { PIXEL_GREEN, PIXEL_RED, PIXEL_BLUE, PIXEL_GREEN },
        { PIXEL_GREEN, PIXEL_BLUE, PIXEL_RED, PIXEL_GREEN },
        { PIXEL_BLUE, PIXEL_GREEN, PIXEL_GREEN, PIXEL_RED },
    };
    return maps[config->cfa_map][(y & 1) * 2 + (x & 1)];
}

static float corrected_sample(const struct conversion_config *config,
                              const uint16_t *pixels, int x, int y)
{
    int pattern_index = (y & 1) * 2 + (x & 1);
    int black = config->black_level[pattern_index];
    int value = pixels[(size_t)y * (size_t)config->width + (size_t)x];
    float normalized = value > black ?
        (float)(value - black) / (float)(config->white_level - black) : 0.0f;
    enum pixel_color color = pixel_color_at(config, x, y);
    int gain_index = color == PIXEL_RED ? 0 :
                     color == PIXEL_BLUE ? 3 : ((y & 1) ? 2 : 1);
    return normalized * config->gains[gain_index];
}

static float demosaic_channel(const struct conversion_config *config,
                              const uint16_t *pixels, int x, int y,
                              enum pixel_color wanted)
{
    if (pixel_color_at(config, x, y) == wanted) {
        return corrected_sample(config, pixels, x, y);
    }
    float sum = 0.0f;
    int count = 0;
    for (int offset_y = -1; offset_y <= 1; ++offset_y) {
        int sample_y = y + offset_y;
        if (sample_y < 0 || sample_y >= config->height) {
            continue;
        }
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
            int sample_x = x + offset_x;
            if (sample_x < 0 || sample_x >= config->width ||
                    pixel_color_at(config, sample_x, sample_y) != wanted) {
                continue;
            }
            sum += corrected_sample(config, pixels, sample_x, sample_y);
            ++count;
        }
    }
    return count ? sum / (float)count : 0.0f;
}

static float clamp_unit(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    return value > 1.0f ? 1.0f : value;
}

static unsigned char srgb_lut[65536];

static void initialize_srgb_lut(void)
{
    for (int index = 0; index < 65536; ++index) {
        float linear = (float)index / 65535.0f;
        float encoded = linear <= 0.0031308f ?
            12.92f * linear :
            1.055f * powf(linear, 1.0f / 2.4f) - 0.055f;
        int value = (int)(encoded * 255.0f + 0.5f);
        srgb_lut[index] = (unsigned char)(value < 0 ? 0 :
                                          value > 255 ? 255 : value);
    }
}

static unsigned char srgb_byte(float linear)
{
    linear = clamp_unit(linear);
    int index = (int)(linear * 65535.0f + 0.5f);
    return srgb_lut[index];
}

static uint16_t *read_raw(const struct conversion_config *config)
{
    FILE *file = fopen(config->raw_path, "rb");
    if (!file) {
        fprintf(stderr, "Cannot read RAW16 file: %s\n", config->raw_path);
        return NULL;
    }
    uint16_t *pixels = malloc((size_t)config->width *
                              (size_t)config->height * sizeof(*pixels));
    unsigned char *row = malloc((size_t)config->row_stride);
    if (!pixels || !row) {
        fprintf(stderr, "Not enough memory for RAW16 image\n");
        free(pixels);
        free(row);
        fclose(file);
        return NULL;
    }
    for (int y = 0; y < config->height; ++y) {
        if (fread(row, 1, (size_t)config->row_stride, file) !=
                (size_t)config->row_stride) {
            fprintf(stderr, "RAW16 file ended at row %d\n", y);
            free(pixels);
            free(row);
            fclose(file);
            return NULL;
        }
        for (int x = 0; x < config->width; ++x) {
            pixels[(size_t)y * (size_t)config->width + (size_t)x] =
                (uint16_t)row[x * 2] | (uint16_t)((uint16_t)row[x * 2 + 1] << 8);
        }
    }
    free(row);
    fclose(file);
    return pixels;
}

static int write_ppm(const struct conversion_config *config,
                     const uint16_t *pixels)
{
    if (printf("P6\n%d %d\n255\n", config->width, config->height) < 0) {
        return -1;
    }
    initialize_srgb_lut();
    unsigned char *row = malloc((size_t)config->width * 3);
    if (!row) {
        return -1;
    }
    for (int y = 0; y < config->height; ++y) {
        for (int x = 0; x < config->width; ++x) {
            float sensor[3] = {
                demosaic_channel(config, pixels, x, y, PIXEL_RED),
                demosaic_channel(config, pixels, x, y, PIXEL_GREEN),
                demosaic_channel(config, pixels, x, y, PIXEL_BLUE),
            };
            float output[3];
            for (int channel = 0; channel < 3; ++channel) {
                output[channel] = config->exposure *
                    (config->matrix[channel * 3] * sensor[0] +
                     config->matrix[channel * 3 + 1] * sensor[1] +
                     config->matrix[channel * 3 + 2] * sensor[2]);
                row[x * 3 + channel] = srgb_byte(output[channel]);
            }
        }
        if (fwrite(row, 3, (size_t)config->width, stdout) !=
                (size_t)config->width) {
            free(row);
            return -1;
        }
    }
    free(row);
    return fflush(stdout) == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s METADATA.json [EXPOSURE]\n", argv[0]);
        return 2;
    }
    struct conversion_config config = { .exposure = 1.0f };
    if (argc == 3) {
        char *end = NULL;
        config.exposure = strtof(argv[2], &end);
        if (!end || *end || config.exposure <= 0.0f ||
                config.exposure > 32.0f) {
            fprintf(stderr, "EXPOSURE must be greater than 0 and at most 32\n");
            return 2;
        }
    }
    if (load_config(argv[1], &config) != 0) {
        return 3;
    }
    uint16_t *pixels = read_raw(&config);
    if (!pixels) {
        return 4;
    }
    fprintf(stderr,
            "Demosaicing %dx%d %s RAW16, exposure %.3g...\n",
            config.width, config.height, config.cfa, config.exposure);
    int result = write_ppm(&config, pixels);
    free(pixels);
    if (result != 0) {
        fprintf(stderr, "Failed while writing PPM stream\n");
        return 5;
    }
    return 0;
}
