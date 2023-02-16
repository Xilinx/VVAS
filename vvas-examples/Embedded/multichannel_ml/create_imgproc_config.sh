#!/bin/bash
echo -n "Creating image_processing.cfg... "
echo "[color-formats]" > image_processing.cfg

grep "#define HA.*\s\+1" image_processing_config.h >> image_processing.cfg
sed -e 's:\s\+1::' -e 's:^#define ::' -i image_processing.cfg

sed -i image_processing.cfg \
  -e 's:HAS_Y_UV8_Y_UV8_420:VVAS_VIDEO_FORMAT_Y_UV8_420:' \
  -e 's:HAS_RGB8_YUV8:VVAS_VIDEO_FORMAT_RGB:' \
  -e 's:HAS_BGR8:VVAS_VIDEO_FORMAT_BGR:' \
  -e 's:HAS_Y_UV10_Y_UV10_420:VVAS_VIDEO_FORMAT_NV12_10LE32:' \
  -e 's:HAS_Y8:VVAS_VIDEO_FORMAT_GRAY8:' \
  -e 's:HAS_Y10:VVAS_VIDEO_FORMAT_GRAY10_LE32:' \
  -e 's:HAS_Y_U_V8_420:VVAS_VIDEO_FORMAT_I420:'

echo Done!
