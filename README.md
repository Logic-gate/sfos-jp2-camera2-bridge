# Sailfish Jolla Phone 2 Camera2 Brdige

Disclaimer: Portions of this work, particularly the Bayer Color Filter Array (CFA) conversion(which I couldn't wrap my head around) and debugging is AI assisted.

This is an experiment and not intened for use by any app. My aim is to capture raw frames directly from the sensor and then convert them to jpeg or png to test the actual quality of the cameras.
Although I started this when I had Xperia 10 iii, I had another go at it after [ric9k](https://forum.sailfishos.org/t/jolla-phone-camera-too-many-details-are-lost/32717)'s post. 

Example:
    Camera2 API
    ![Camera2 API](https://i.snipboard.io/fUdqWE.jpg)
    This is even compressed further via snipboard and converted to jpg. The original is a 12mb png. 
    
    ***
        
    Stock App
    
    ![Camera2 API](https://i.snipboard.io/So5AQG.jpg)


The project has two pieces:

* `libsfoscamera2.so` Bionic shared library built with the public Android NDK Camera2 API.
* `sfos-camera2-probe` Loads the bridge through libhybris, prints Camera2 and lens-focus capabilities, and can autofocus before capturing one RAW16 frame.

It does not replace `libdroidmedia.so` and it does not use an Android application or AppSupport.

## Prerequisites

* [Android NDK](https://developer.android.com/ndk/downloads)
* Sailfish SDK
* ImageMagick from Chum

## Build the Android bridge

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk ./scripts/build-android.sh
```

This will create `build/android/libsfoscamera2.so`. It targets theCamera2 NDK ABI introduced in API 24.

To remove it, as I suspect Jolla might use a similar name:
    
```sh
devel-su rm /usr/libexec/droid-hybris/system/lib64/libsfoscamera2.so
```

## Build the Sailfish loader

First check your aarch64 target(tested on 5.1.0.11)

```sh
sfdk tools list
sfdk config target=TARGET FROM LIST
./scripts/build-sailfish.sh
```

Please note, in `build-sailfish.sh`, `sfdk` is using an absolute path `~/SailfishOS/bin/sfdk`. You might want to change it.

## Deploy and run

```sh
scp build/android/libsfoscamera2.so sailfish/sfos-camera2-probe sailfish/sfos-raw16-to-ppm sailfish/test-camera2-raw.sh sailfish/raw16-to-image.sh \
    sailfish/capture-camera2-image.sh defaultuser@JollaPhone2026:/home/defaultuser/
ssh defaultuser@JollaPhone2026
```

On the JP2:

```sh
chmod 755 /home/defaultuser/sfos-camera2-probe
chmod 755 /home/defaultuser/sfos-raw16-to-ppm
chmod 755 /home/defaultuser/test-camera2-raw.sh /home/defaultuser/raw16-to-image.sh /home/defaultuser/capture-camera2-image.sh
devel-su cp /home/defaultuser/libsfoscamera2.so /usr/libexec/droid-hybris/system/lib64/libsfoscamera2.so
devel-su chmod 755 /usr/libexec/droid-hybris/system/lib64/libsfoscamera2.so

/home/defaultuser/sfos-camera2-probe
```

Example probe:
    
```json
    {
  "status": "ok",
  "bridge_version": "0.3.0",
  "camera_count": 3,
  "cameras": [
    {
      "id": "0",
      "status": "ok",
      "raw_capability": true,
      "hardware_level": "level_3",
      "hardware_level_value": 3,
      "focus": {
        "af_modes": [
          {
            "name": "off",
            "value": 0
          },
          {
            "name": "auto",
            "value": 1
          },
          {
            "name": "macro",
            "value": 2
          },
          {
            "name": "continuous_video",
            "value": 3
          },
          {
            "name": "continuous_picture",
            "value": 4
          }
        ],
        "minimum_focus_distance_diopters": 20,
        "fixed_focus": false,
        "focus_distance_calibration": "uncalibrated",
        "focus_distance_calibration_value": 0
      },
      "raw_outputs": [
        {
          "format": "RAW16",
          "format_value": 32,
          "width": 4096,
          "height": 3072
        },
        {
          "format": "RAW16",
          "format_value": 32,
          "width": 3264,
          "height": 2448
        },
        {
          "format": "RAW16",
          "format_value": 32,
          "width": 3072,
          "height": 1728
        },
        {
          "format": "RAW16",
          "format_value": 32,
          "width": 2560,
          "height": 1920
        },
        {
          "format": "RAW16",
          "format_value": 32,
          "width": 1920,
          "height": 1080
        },
        {
          "format": "RAW16",
          "format_value": 32,
          "width": 1600,
          "height": 1200
        }
      ]
    },
    {
      "id": "1",
      "status": "ok",
      "raw_capability": false,
      "hardware_level": "full",
      "hardware_level_value": 1,
      "focus": {
        "af_modes": [
          {
            "name": "off",
            "value": 0
          }
        ],
        "minimum_focus_distance_diopters": 0,
        "fixed_focus": true,
        "focus_distance_calibration": "uncalibrated",
        "focus_distance_calibration_value": 0
      },
      "raw_outputs": []
    },
    {
      "id": "2",
      "status": "ok",
      "raw_capability": false,
      "hardware_level": "limited",
      "hardware_level_value": 0,
      "focus": {
        "af_modes": [
          {
            "name": "off",
            "value": 0
          },
          {
            "name": "auto",
            "value": 1
          },
          {
            "name": "macro",
            "value": 2
          },
          {
            "name": "continuous_video",
            "value": 3
          },
          {
            "name": "continuous_picture",
            "value": 4
          }
        ],
        "minimum_focus_distance_diopters": 20,
        "fixed_focus": false,
        "focus_distance_calibration": "uncalibrated",
        "focus_distance_calibration_value": 0
      },
      "raw_outputs": []
    }
  ]
}
```

## Capture one RAW16 test frame

Close every camera application first, then run:

```sh
/home/defaultuser/test-camera2-raw.sh
```

It creates two timestamped files under `/home/defaultuser/Pictures`:

* `camera2-raw-*.raw16` contains the single-plane RAW16 buffer exactly as returned by `AImageReader`, including any row padding.
* `camera2-raw-*.json` records the dimensions, strides, timestamps, exposure, ISO, CFA layout, black/white levels, and available colour matrices.

Use `4096x3072` for the first capture on the tested Jolla Phone 2.


Examples 

```sh
/home/defaultuser/sfos-camera2-probe --capture --camera 0 --size 4096x3072 --timeout 30 --focus auto --focus-timeout 3 --focus-failure capture --output /home/defaultuser/Pictures/camera2-test
```

Use `capture-camera2-image.sh` rather than `sfos-camera2-probe --capture` for more control and options.

```sh
/home/defaultuser/capture-camera2-image.sh \
    --format png \
     --focus auto \
    --focus-timeout 5 \
    --focus-failure capture \
    --exposure 1.00 \
    --png-compression 6 \
    --output-prefix "$TEST_DIR/direct-png"
```

Full list of options:
    
```sh
    [defaultuser@JollaPhone2026 ~]$ ./capture-camera2-image.sh --help
Usage: ./capture-camera2-image.sh [OPTIONS]

Capture one Camera2 RAW16 frame and convert it to PNG, JPEG, or both.

Capture options:
  -f, --format FORMAT       png, jpeg, jpg, or both (default: png)
  -c, --camera ID           Camera ID (default: 0)
  -s, --size WIDTHxHEIGHT   RAW16 size (default: 4096x3072)
  -t, --timeout SECONDS     Capture timeout (default: 30)
      --focus MODE          auto, continuous, manual, infinity, or none
                            (default: auto)
      --focus-distance D    Manual focus distance in diopters (default: 0)
      --focus-timeout SEC   Autofocus timeout (default: 3)
      --focus-failure MODE  capture or abort (default: capture)
  -d, --output-dir DIR      Output directory
  -o, --output-prefix PATH  Exact output prefix without an extension
      --force               Replace files with the selected prefix

Rendering options:
  -e, --exposure FACTOR     Linear exposure multiplier, >0 to 32
  -q, --quality N           JPEG quality, 1 to 100 (default: 92)
      --sampling MODE       JPEG sampling: 4:4:4, 4:2:2, or 4:2:0
      --png-compression N   PNG compression level, 0 to 9 (default: 6)
      --progressive         Write a progressive JPEG

Cleanup and output:
      --delete-raw          Delete RAW16 only after conversion succeeds
      --delete-metadata     Delete JSON only after conversion succeeds
      --print-metadata      Print the complete JSON metadata
      --probe FILE          Path to sfos-camera2-probe
      --converter FILE      Path to raw16-to-image.sh
  -h, --help                Show this help
      --version             Show the script version

Environment defaults:
  CAMERA_ID, RAW_SIZE, CAPTURE_TIMEOUT, CAMERA_FOCUS,
  CAMERA_FOCUS_DISTANCE, CAMERA_FOCUS_TIMEOUT, CAMERA_FOCUS_FAILURE,
  OUTPUT_FORMAT, RAW_EXPOSURE, JPEG_QUALITY, JPEG_SAMPLING,
  PNG_COMPRESSION, SFOS_CAMERA2_PROBE, and SFOS_RAW16_IMAGE_CONVERTER
  ```






