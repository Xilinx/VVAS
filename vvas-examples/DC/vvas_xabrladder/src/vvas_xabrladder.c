/*
* Copyright (C) 2020 - 2021 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software
* is furnished to do so, subject to the following conditions:
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
* KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
* EVENT SHALL XILINX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
* OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE. Except as contained in this notice, the name of the Xilinx shall
* not be used in advertising or otherwise to promote the sale, use or other
* dealings in this Software without prior written authorization from Xilinx.
*/

/**
 * file: vvas_xabrladder.c
 *
 * Reference https://gstreamer.freedesktop.org/documentation/application-development/basics/helloworld.html?gi-language=c
 * https://gstreamer.freedesktop.org/documentation/tutorials/basic/short-cutting-the-pipeline.html?gi-language=c
 *
 *
 * Application generate outputs at $DEFAULT_OUTPUT_PATH using below pipeline.
 * There are some supporting components which are not shown below for simplicity
 * For ease of understanding the below diagrams are without lookahead.

 **** EXAMPLE_1_USECASE
										 +---------+  +---------+ +----------+ +----------+
									       |>|videorate|--| vvas    |-|H264/H265 |-| filesrc/ |
							 +--------+   +------+ | |  60fps  |  | XVCUENC | |Parse     | | filesink |
						      |->|1920x720|---> tee  |-| +---------+  +---------+ +----------+ +----------+
						      |  |        |   |      | | +---------+  +---------+ +----------+ +----------+
						      |  +--------+   +------+ |>|videorate|--| vvas    |-|H264/H265 |-| filesrc/ |
						      |                          |  30fps  |  | XVCUENC | |Parse     | | filesink |
						      |                          +---------+  +---------+ +----------+ +----------+
						      |
						      |
						      |  +--------+              +---------+  +---------+ +----------+ +----------+
						      |->|848x480 |------------->|videorate|--| vvas    |-|H264/H265 |-| filesrc  |
	+-------+ +---------+ +--------+ +----------+ |  |        |              |  30fps  |  | XVCUENC | |Parse     | | filesink |
	|       | |DEMUX/   | |vvas    | |vvas      | |  +--------+              +---------+  +---------+ +----------+ +----------+
	|filesrc|-|H264/H265|-|XVCUDEC |-|XAbrScaler|-|
	|       | |Parse    | |        | |          | |  +--------+              +---------+  +---------+ +----------+ +----------+
	+-------+ +---------+ +--------+ +----------+ |->|640x360 |------------->|videorate|--| vvas    |-|H264/H265 |-| filesrc/ |
						      |  |        |              |  30fps  |  | XVCUENC | |Parse     | | filesink |
						      |  +--------+              +---------+  +---------+ +----------+ +----------+
						      |
						      |  +--------+              +---------+  +---------+ +----------+ +----------+
						      |->|288x160 |------------->|videorate|--| vvas    |-|H264/H265 |-| filesrc/ |
							 |        |              |  30fps  |  | XVCUENC | |Parse     | | silesink |
							 +--------+              +---------+  +---------+ +----------+ +----------+



 ----------------------------------------------------------------------------------------------------------------------------------

 **** Normal USECASE

							 +--------+              +---------+  +---------+ +----------+ +----------+
							 |        | ------------>|videorate|--| vvas    |-|H264/H265 |-| filesrc/ |
						      |->|        |              |         |  | XVCUENC | |Parse     | | filesink |
						      |  +--------+              +---------+  +---------+ +----------+ +----------+
						      |
						      |
						      |  +--------+              +---------+  +---------+ +----------+ +----------+
						      |->|        |------------->|videorate|--| vvas    |-|H264/H265 |-| filesrc  |
	+-------+ +---------+ +--------+ +----------+ |  |        |              |         |  | XVCUENC | |Parse     | | filesink |
	|       | |DEMUX/   | |vvas    | |vvas      | |  +--------+              +---------+  +---------+ +----------+ +----------+
	|filesrc|-|H264/H265|-|XVCUDEC |-|XAbrScaler|-|
	|       | |Parse    | |        | |          | |  +--------+              +---------+  +---------+ +----------+ +----------+
	+-------+ +---------+ +--------+ +----------+ |->|        |------------->|videorate|--| vvas    |-|H264/H265 |-| filesrc/ |
						      |  |        |              |         |  | XVCUENC | |Parse     | | filesink |
						      |  +--------+              +---------+  +---------+ +----------+ +----------+
						      |
						      |  +--------+              +---------+  +---------+ +----------+ +----------+
						      |->|        |------------->|videorate|--| vvas    |-|H264/H265 |-| filesrc/ |
							 |        |              |         |  | XVCUENC | |Parse     | | silesink |
							 +--------+              +---------+  +---------+ +----------+ +----------+

 * Used https://textik.com/#52ab1bcb358b4260 for graph
 *
 */

#include <gst/gst.h>
#include <jansson.h>
#include <stdlib.h>
#include <getopt.h>
#include <gst/pbutils/pbutils.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

static gboolean caught_intr = FALSE;

#define EXAMPLE_1_USECASE 1
//#define GET_DOT_FILE
/* To get the graph:
 * export GST_DEBUG_DUMP_DOT_DIR=/tmp/
 * run the vvas_xabrladder command
 * dot -Tpng /tmp/pipeline.dot > pipeline.png
 */

#define DEFAULT_FPS_DELAY 1000000
#define DEFAULT_SOURCE "filesrc"
#define DEFAULT_JSON "/opt/xilinx/vvas/share/vvas-examples/abrladder.json"
#define DEFAULT_TEST_ITERATTION_NUM 400
#define MAX_FILE_IN_DIR 400
#define DEFAULT_OUTPUT_PATH "/tmp/ladder_outputs"
#define DEFAULT_LOOKAHEAD_KERNEL "lookahead:{lookahead_1}"

#define IDR_FRAME_RAND_LOW_LIMIT  (10)
#define IDR_FRAME_RAND_HIGH_LIMIT  (60)

#define IDR_FRAME_NUM_RAND \
         ((rand() % (IDR_FRAME_RAND_HIGH_LIMIT - IDR_FRAME_RAND_LOW_LIMIT + 1)) + IDR_FRAME_RAND_LOW_LIMIT)

#define DEFAULT_XCLBIN_PATH "/opt/xilinx/xcdr/xclbins/transcode.xclbin"

static gboolean is_lookahead_enabled = TRUE;

enum
{
  TESTCASE_DEFAULT = 0,
  TESTCASE_1,
  TESTCASE_2,

  TESTCASE_END,
};

enum
{
  CODEC_H264,
  CODEC_H265,
};

enum
{
  TYPE_MP4,
  TYPE_ELEM,
};

enum
{
  LOG_LEVEL_ERROR,
  LOG_LEVEL_WARNING,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG
};

typedef struct dyn_bframe
{
  int frame_num;
  int b_frames;
  gboolean is_valid;
} DynamicBFrame;

typedef struct dyn_bitrate
{
  int frame_num;
  int bitrate;
  gboolean is_valid;
} DynamicBitrate;

typedef struct dyn_spatial_aq
{
  int frame_num;
  gboolean spatial_aq;
  gboolean is_valid;
} DynamicSpatialAq;

typedef struct dyn_temporal_aq
{
  int frame_num;
  gboolean temporal_aq;
  gboolean is_valid;
} DynamicTemporalAq;
typedef struct dyn_spatial_aq_gain
{
  int frame_num;
  int spatial_aq_gain;
  gboolean is_valid;
} DynamicSpatialAqGain;

typedef struct dyn_params
{
  int num_dyn_params;
  DynamicBFrame *dyn_bframe_conf;
  DynamicBitrate *dyn_bitrate_conf;
  DynamicSpatialAq *dyn_spatial_aq_conf;
  DynamicTemporalAq *dyn_temporal_aq_conf;
  DynamicSpatialAqGain *dyn_spatial_aq_gain_conf;
} DynamicParams;

typedef struct output_config
{
  int height;
  int width;
  int framerate;
  int b_frames;
  int target_bitrate;
  int gop_length;
  int lookahead_depth;
  gboolean rc_mode;
  gboolean spatial_aq;
  gboolean temporal_aq;
  int spatial_aq_gain;
  int max_bitrate;
  char h264_profile[16];
  char h264_level[16];
  char h265_profile[16];
  char h265_level[16];
  
  DynamicParams dparams; /* Dynamic params configuration in each ladder */
} OutputConfig;
/*Height Width FR BFrm BR   GOP LADepth RC_MODE SpatialAQ TemporalAQ SpatialAQ-Gain MaxBR Profile Level  Profile Level*/
const OutputConfig def_cfg[8] = {
  {720, 1280, 60, 2, 4000, 120, 8, TRUE, TRUE, TRUE, 50, 4000, "high", "4.2",
      "main", "5.2"},
  {720, 1280, 30, 2, 3000, 120, 8, TRUE, TRUE, TRUE, 50, 3000, "high", "4.2",
      "main", "5.2"},
  {480, 848, 30, 2, 2500, 120, 8, TRUE, TRUE, TRUE, 50, 2500, "high", "4.2",
      "main", "5.2"},
  {360, 640, 30, 2, 1250, 120, 8, TRUE, TRUE, TRUE, 50, 1250, "high", "4.2",
      "main", "5.2"},
  {160, 288, 30, 2, 625, 120, 8, TRUE, TRUE, TRUE, 50, 625, "high", "4.2",
      "main", "5.2"},
  {0, 0, 0, 0, 0, 0, 0, FALSE, FALSE, FALSE, 0, 0, "", "", "", ""},
  {0, 0, 0, 0, 0, 0, 0, FALSE, FALSE, FALSE, 0, 0, "", "", "", ""},
  {0, 0, 0, 0, 0, 0, 0, FALSE, FALSE, FALSE, 0, 0, "", "", "", ""}
};

typedef struct head
{
  GstElement *source;
  GstElement *input_queue;
  GstElement *h26xparse;
  GstElement *parse_queue;
  GstElement *decoder;
  GstElement *qtdemux;
  GstElement *scaler_queue;
  GstElement *scaler;
} LadderHead;

typedef struct tail
{
  GstElement *enc_queue;
  GstElement *capsfilter;
  GstElement *videorate;
  GstElement *videoratecaps;
  GstElement *lookahead;
  GstElement *encoder;
  GstElement *encparse;
  GstElement *enccaps;
  GstElement *sink;

  GstElement *caps_queue;
  GstElement *tee_caps;
  GstElement *tee_queue;
  GstElement *tee;

  GstPad *sc_output_pad;
  GstPad *enc_queue_pad;

  GstPad *tee_output_pad;
  GstPad *caps_queue_pad;
  
  gulong enc_pad_probe_id;
  gulong la_pad_probe_id;
} LadderTail;

typedef struct args
{
  const gchar *source;
  int num_buffers;
  gboolean loop;
  const gchar *sink;
  gboolean fps_display;

  int dev_idx;
  int num_output;
#ifndef ENABLE_XRM_SUPPORT
  int dec_sk_cur_idx;
  int enc_sk_cur_idx;
#endif
  int codec_type;
  int input_codec;
  int container;
  gboolean force_keyframe_valid;
  int force_keyframe_freq;
  const gchar *lookahead_kernel;
  const gchar *lib_path;
  gchar *input_file;
  gchar *file_array[MAX_FILE_IN_DIR];

  OutputConfig outconf[8];      /* Scaler support max 8 output */
  gboolean is_dyn_params_present;
} LadderArgs;

typedef struct custom_data
{
  int log_level;
  GstElement *pipeline;
  LadderHead lhead;
  LadderTail ltail[16];
  LadderArgs largs;
} CustomData;

typedef struct enc_pad_probe_cb_info
{
  int index;
  gboolean is_lookahead_enabled;
  guint64 frame_count;
  int next_bitrate_index;
  int next_bframe_index;
  CustomData *data_priv;
}EncPadProbeCbInfo;

typedef struct la_pad_probe_cb_info
{
  int index;
  guint64 frame_count;
  int next_bframe_index;
  int next_spatial_aq_index;
  int next_temporal_aq_index;
  int next_spatial_aq_gain_index;
  CustomData *data_priv;
}LaPadProbeCbInfo;

int vvas_read_json (json_t *root, CustomData * priv);
static gboolean fileexists (const gchar * name);
int get_files (const gchar * input_dir, CustomData * priv);
GstElement *create_source (CustomData * priv);
GstElement *create_decoder (CustomData * priv);
GstElement *create_scaler (CustomData * priv);
GstElement *create_caps (CustomData * priv, int num);
GstElement *create_videoratecaps (CustomData * priv, int num);
GstElement *create_lookahead (CustomData * priv, int num);
GstElement *create_encoder (CustomData * priv, int num);
GstElement *create_enccaps (CustomData * priv, int num);
GstElement *create_encparse (CustomData * priv, int num);
GstElement *create_sink (CustomData * priv, int num);
int get_container_type (const char *path);
static int get_codec_type (const char *path, CustomData * priv);
static void print_usage (void);
static gchar *create_symlink (const gchar *path);
static void get_container_and_codec_type (const char *path, int *container_type,
                                          int *codec_type, CustomData * priv);

#define GST_MSG(level, set_level, ...) {\
  do {\
    if (level <= set_level) {\
      if (level == LOG_LEVEL_ERROR)\
	 g_printerr (__VA_ARGS__);\
      else\
	g_print (__VA_ARGS__);\
      g_print ("\n");\
    }\
  } while (0);\
}

static gboolean
fileexists (const gchar * name)
{
  struct stat s;
  return (stat (name, &s) == 0 && !(S_ISDIR (s.st_mode)));
}

static gboolean
direxists (const gchar * name)
{
  struct stat s;
  return (stat (name, &s) == 0 && S_ISDIR (s.st_mode));
}

static gboolean
isDirectory (const gchar * name)
{
  struct stat s;
  return (stat (name, &s) == 0 && S_ISDIR (s.st_mode));
}


int
vvas_read_json (json_t *root, CustomData * priv)
{
  json_t *ladder, *outarray, *output, *value;
  json_t *dyn_params, *dyn_params_array;
  int j, k, frame_num = 0;
  int dyn_bframe_index = 0, dyn_bitrate_index = 0;
  int dyn_spatial_aq_index = 0, dyn_temporal_aq_index = 0, dyn_spatial_aq_gain_index = 0;

  /* get log_level */
  value = json_object_get (root, "log-level");
  if (!value || !json_is_integer (value)) {
    priv->log_level = LOG_LEVEL_WARNING;
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "log level is not set");
  } else {
    priv->log_level = json_integer_value (value);
  }

  ladder = json_object_get (root, "ladder");
  if (!ladder) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "ladder is not of object type");
    goto error;
  }

  value = json_object_get (ladder, "source");
  if (!value || !json_is_string (value)) {
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "source is not of string type");
    priv->largs.source = DEFAULT_SOURCE;
    priv->largs.loop = FALSE;
    value = json_object_get (ladder, "num-buffers");
    if (!value || !json_is_integer (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "num-buffers is not set, take default -1");
      priv->largs.num_buffers = -1;
    } else {
      priv->largs.num_buffers = json_integer_value (value);
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "num-buffers = %d", priv->largs.num_buffers);
    }

  } else {
    priv->largs.source = json_string_value (value);
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "source %s", priv->largs.source);

    value = json_object_get (ladder, "num-buffers");
    if (!value || !json_is_integer (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "num-buffers is not set, take default -1");
      priv->largs.num_buffers = -1;
    } else {
      priv->largs.num_buffers = json_integer_value (value);
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "num-buffers = %d", priv->largs.num_buffers);
    }

    value = json_object_get (ladder, "loop");
    if (!value || !json_is_boolean (value))
      priv->largs.loop = FALSE;
    else
      priv->largs.loop = json_boolean_value (value);
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "loop = %d", priv->largs.loop);
    if (!strcmp ("filesrc", priv->largs.source))
      priv->largs.loop = FALSE;
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "new loop = %d",
        priv->largs.loop);
  }

  value = json_object_get (ladder, "sink");
  if (!value || !json_is_string (value)) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "sink is not of string type");
    goto error;
  } else {
    priv->largs.sink = json_string_value (value);
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "sink %s", priv->largs.sink);
  }

  value = json_object_get (ladder, "fps-display");
  if (!value || !json_is_boolean (value)) {
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "fps-display not set");
    priv->largs.fps_display = TRUE;
  } else {
    priv->largs.fps_display = json_boolean_value (value);
  }
  value = json_object_get (ladder, "lookahead-kernel");
  if (!value || !json_is_string (value)) {
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
        "lookahead_kernel name is not set");
    priv->largs.lookahead_kernel = DEFAULT_LOOKAHEAD_KERNEL;
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
        "setting  lookahead kernel to %s", priv->largs.lookahead_kernel);

  } else {
    priv->largs.lookahead_kernel = json_string_value (value);
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
        "setting  lookahead kernel to %s", priv->largs.lookahead_kernel);
  }

  /* get output array */
  outarray = json_object_get (ladder, "outputs");
  if (!outarray) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "failed to find key outputs");
    goto error;
  }

  if (!json_is_array (outarray)) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
        "outputs key is not of array type");
    goto error;
  }

  priv->largs.num_output = json_array_size (outarray);
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "number of output = %d",
      priv->largs.num_output);

  for (j = 0; j < priv->largs.num_output; j++) {
    output = json_array_get (outarray, j);
    if (!output) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "failed to get output object");
      goto error;
    }

    value = json_object_get (output, "height");
    if (!value || !json_is_integer (value)) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "height is not int type in %d output", j);
      goto error;
    } else {
      priv->largs.outconf[j].height = json_integer_value (value);
    }
    value = json_object_get (output, "width");
    if (!value || !json_is_integer (value)) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "width is not int type in %d output", j);
      goto error;
    } else {
      priv->largs.outconf[j].width = json_integer_value (value);
    }

    value = json_object_get (output, "framerate");
    if (!value || !json_is_integer (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "framerate taking default in %d output", j);
      priv->largs.outconf[j].framerate = def_cfg[j].framerate;
    } else {
      priv->largs.outconf[j].framerate = json_integer_value (value);
    }

    value = json_object_get (output, "b-frames");
    if (!value || !json_is_integer (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "b-frames taking default in %d output", j);
      priv->largs.outconf[j].b_frames = def_cfg[j].b_frames;
    } else {
      priv->largs.outconf[j].b_frames = json_integer_value (value);
    }

    value = json_object_get (output, "target-bitrate");
    if (!value || !json_is_integer (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "target-bitrate taking default in %d output", j);
      priv->largs.outconf[j].target_bitrate = def_cfg[j].target_bitrate;
    } else {
      priv->largs.outconf[j].target_bitrate = json_integer_value (value);
    }

    value = json_object_get (output, "gop-length");
    if (!value || !json_is_integer (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "gop-length taking default in %d output", j);
      priv->largs.outconf[j].gop_length = def_cfg[j].gop_length;
    } else {
      priv->largs.outconf[j].gop_length = json_integer_value (value);
    }

    value = json_object_get (output, "lookahead-depth");
    if (!value || !json_is_integer (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "lookahead-depth taking default in %d output", j);
      priv->largs.outconf[j].lookahead_depth = def_cfg[j].lookahead_depth;
    } else {
      priv->largs.outconf[j].lookahead_depth = json_integer_value (value);
    }

    value = json_object_get (output, "rc-mode");

    if (!value || !json_is_boolean (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "rc-mode taking default in %d output", j);
      priv->largs.outconf[j].rc_mode = def_cfg[j].rc_mode;
    } else {
      priv->largs.outconf[j].rc_mode = json_boolean_value (value);
    }

    value = json_object_get (output, "spatial-aq");

    if (!value || !json_is_boolean (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "spatial-aq taking default in %d output", j);
      priv->largs.outconf[j].spatial_aq = def_cfg[j].spatial_aq;
    } else {
      priv->largs.outconf[j].spatial_aq = json_boolean_value (value);

    }

    value = json_object_get (output, "temporal-aq");

    if (!value || !json_is_boolean (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "temporal-aq taking default in %d output", j);
      priv->largs.outconf[j].temporal_aq = def_cfg[j].temporal_aq;
    } else {
      priv->largs.outconf[j].temporal_aq = json_boolean_value (value);

    }

    value = json_object_get (output, "spatial-aq-gain");

    if (!value || !json_is_integer (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "spatial-aq-gain taking default in %d output", j);
      priv->largs.outconf[j].spatial_aq_gain = def_cfg[j].spatial_aq_gain;
    } else {
      priv->largs.outconf[j].spatial_aq_gain = json_integer_value (value);

    }
       
    value = json_object_get (output, "max-bitrate");
    if (!value || !json_is_integer (value)) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
          "max-bitrate taking default in %d output", j);
      priv->largs.outconf[j].max_bitrate = def_cfg[j].max_bitrate;
    } else {
      priv->largs.outconf[j].max_bitrate = json_integer_value (value);
    }

    value = json_object_get (output, "h264-profile");
    if (value && json_is_string (value)) {
      strcpy (priv->largs.outconf[j].h264_profile, json_string_value (value));
    }

    value = json_object_get (output, "h264-level");
    if (value && json_is_string (value)) {
      strcpy (priv->largs.outconf[j].h264_level, json_string_value (value));
    }

    if (strlen (priv->largs.outconf[j].h264_profile) && 
        !strlen (priv->largs.outconf[j].h264_level)) {
      printf("Please provide input level also in the JSON config !!!\n");
      goto error;
    } else if (!strlen (priv->largs.outconf[j].h264_profile) &&
               strlen (priv->largs.outconf[j].h264_level)) {
      printf("Please provide input profile also in the JSON config !!!\n");
      goto error;
    }

    value = json_object_get (output, "h265-profile");
    if (value && json_is_string (value)) {
      strcpy (priv->largs.outconf[j].h265_profile, json_string_value (value));
    }

    value = json_object_get (output, "h265-level");
    if (value && json_is_string (value)) {
      strcpy (priv->largs.outconf[j].h265_level, json_string_value (value));
    }

    if (strlen (priv->largs.outconf[j].h265_profile) && 
        !strlen (priv->largs.outconf[j].h265_level)) {
      printf("Please provide input level also in the JSON config !!!\n");
      goto error;
    } else if (!strlen (priv->largs.outconf[j].h265_profile) &&
               strlen (priv->largs.outconf[j].h265_level)) {
      printf("Please provide input profile also in the JSON config !!!\n");
      goto error;
    }

    /* Dynamic Parameters configuration for each ladder */
    priv->largs.outconf[j].dparams.num_dyn_params = 0;
    dyn_bframe_index = 0;
    dyn_bitrate_index = 0;
    dyn_spatial_aq_index = 0;
    dyn_temporal_aq_index = 0;
    dyn_spatial_aq_gain_index = 0;

    dyn_params_array = json_object_get (output, "dynamic_params");
    if (dyn_params_array) {
      if (!json_is_array (dyn_params_array)) {
        GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
            "dynamic_params key is not of array type");
        goto error;
      }

      priv->largs.outconf[j].dparams.num_dyn_params = json_array_size (dyn_params_array);
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "number of dyn_params = %d",
          priv->largs.outconf[j].dparams.num_dyn_params);

      if (priv->largs.outconf[j].dparams.num_dyn_params) {
        priv->largs.is_dyn_params_present = TRUE;
        priv->largs.outconf[j].dparams.dyn_bframe_conf = 
            (DynamicBFrame *)calloc(1, priv->largs.outconf[j].dparams.num_dyn_params * sizeof (DynamicBFrame));
        priv->largs.outconf[j].dparams.dyn_bitrate_conf = 
            (DynamicBitrate *)calloc(1, priv->largs.outconf[j].dparams.num_dyn_params * sizeof (DynamicBitrate));
        priv->largs.outconf[j].dparams.dyn_spatial_aq_conf = 
            (DynamicSpatialAq *)calloc(1, priv->largs.outconf[j].dparams.num_dyn_params * sizeof (DynamicSpatialAq));
        priv->largs.outconf[j].dparams.dyn_temporal_aq_conf = 
            (DynamicTemporalAq *)calloc(1, priv->largs.outconf[j].dparams.num_dyn_params * sizeof (DynamicTemporalAq));
        priv->largs.outconf[j].dparams.dyn_spatial_aq_gain_conf = 
            (DynamicSpatialAqGain *)calloc(1, priv->largs.outconf[j].dparams.num_dyn_params * sizeof (DynamicSpatialAqGain));

        for (k = 0; k < priv->largs.outconf[j].dparams.num_dyn_params; k++) {
          dyn_params = json_array_get (dyn_params_array, k);
          if (!dyn_params) {
            GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "failed to get dyn_params object");
            goto error;
          }

          value = json_object_get (dyn_params, "frame");
          if (!value || !json_is_integer (value)) {
            GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
                "frame key not found in %d dyn_params", k);
            goto error;
          } else {
            frame_num = json_integer_value (value);
          }

          value = json_object_get (dyn_params, "b-frames");
          if (!value || !json_is_integer (value)) {
            GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
                "b-frames not found in %d dyn_params", k);
          } else {
            priv->largs.outconf[j].dparams.dyn_bframe_conf[dyn_bframe_index].b_frames = json_integer_value (value);
            priv->largs.outconf[j].dparams.dyn_bframe_conf[dyn_bframe_index].frame_num = frame_num;
            priv->largs.outconf[j].dparams.dyn_bframe_conf[dyn_bframe_index].is_valid = TRUE;
            dyn_bframe_index++;
          }

          value = json_object_get (dyn_params, "bitrate");
          if (!value || !json_is_integer (value)) {
            GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
                "bitrate not found in %d dyn_params", k);
          } else {
            priv->largs.outconf[j].dparams.dyn_bitrate_conf[dyn_bitrate_index].bitrate = json_integer_value (value);
            priv->largs.outconf[j].dparams.dyn_bitrate_conf[dyn_bitrate_index].frame_num = frame_num;
            priv->largs.outconf[j].dparams.dyn_bitrate_conf[dyn_bitrate_index].is_valid = TRUE;
            dyn_bitrate_index++;
          }

          value = json_object_get (dyn_params, "spatial-aq");
          if (!value || !json_is_boolean (value)) {
            GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
                "spatial-aq not found in %d dyn_params", k);
          } else {
            priv->largs.outconf[j].dparams.dyn_spatial_aq_conf[dyn_spatial_aq_index].spatial_aq = json_boolean_value (value);
            priv->largs.outconf[j].dparams.dyn_spatial_aq_conf[dyn_spatial_aq_index].frame_num = frame_num;
            priv->largs.outconf[j].dparams.dyn_spatial_aq_conf[dyn_spatial_aq_index].is_valid = TRUE;
            dyn_spatial_aq_index++;
          }

          value = json_object_get (dyn_params, "temporal-aq");
          if (!value || !json_is_boolean (value)) {
            GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
                "temporal-aq not found in %d dyn_params", k);
          } else {
            priv->largs.outconf[j].dparams.dyn_temporal_aq_conf[dyn_temporal_aq_index].temporal_aq = json_boolean_value (value);
            priv->largs.outconf[j].dparams.dyn_temporal_aq_conf[dyn_temporal_aq_index].frame_num = frame_num;
            priv->largs.outconf[j].dparams.dyn_temporal_aq_conf[dyn_temporal_aq_index].is_valid = TRUE;
            dyn_temporal_aq_index++;
          }

          value = json_object_get (dyn_params, "spatial-aq-gain");
          if (!value || !json_is_integer (value)) {
            GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
                "spatial-aq-gain not found in %d dyn_params", k);
          } else {
            priv->largs.outconf[j].dparams.dyn_spatial_aq_gain_conf[dyn_spatial_aq_gain_index].spatial_aq_gain = json_integer_value (value);
            priv->largs.outconf[j].dparams.dyn_spatial_aq_gain_conf[dyn_spatial_aq_gain_index].frame_num = frame_num;
            priv->largs.outconf[j].dparams.dyn_spatial_aq_gain_conf[dyn_spatial_aq_gain_index].is_valid = TRUE;
            dyn_spatial_aq_gain_index++;
          }
        }
      }
    }
  }

  return 0;
error:
  return -1;
}

GstElement *
create_sink (CustomData * priv, int num)
{
  GstElement *sink;
  GstElement *actual_sink;
  gchar sink_name[50];
  gchar location[100];
  const gchar *codec_type;
  int check;
  const char *dirname = DEFAULT_OUTPUT_PATH;
  time_t T = time(NULL);
  struct tm tm = *localtime(&T);

  if (priv->largs.codec_type == CODEC_H264)
    codec_type = "h264";
  else if (priv->largs.codec_type == CODEC_H265)
    codec_type = "h265";
  else
    return NULL;                /* Should not come here */

  sprintf (sink_name, "sink%d", num);
#ifndef ENABLE_XRM_SUPPORT
  sprintf (location, "%s/encoded_dev%d_ladder%d_%dX%d_%d.%s",
      dirname,
      priv->largs.dev_idx,
      priv->largs.dec_sk_cur_idx,
      priv->largs.outconf[num].width, priv->largs.outconf[num].height,
      priv->largs.outconf[num].framerate, codec_type);
#else
  sprintf (location, "%s/encoded_dev%d_ladder_%ld_%02d-%02d-%04d_%02d:%02d:%02d_%dX%d_%d.%s",
      dirname,
      priv->largs.dev_idx,
      (long)getpid(),
      tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec,
      priv->largs.outconf[num].width, priv->largs.outconf[num].height,
      priv->largs.outconf[num].framerate, codec_type);
#endif
  sink = gst_element_factory_make ("fpsdisplaysink", sink_name);

  if (!strcmp ("fakesink", priv->largs.sink)
      || !strcmp ("filesink", priv->largs.sink)) {
    actual_sink = gst_element_factory_make (priv->largs.sink, "actualsink");
    if (!strcmp ("filesink", priv->largs.sink)) {
      if (!direxists (dirname)) {

        check = mkdir (dirname, 0777);

        // check if directory is created or not 
        if (!check) {
          GST_MSG (LOG_LEVEL_INFO, priv->log_level, "%s Directory created\n",
              dirname);
        } else {
          GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
              "%s Unable to create directory", dirname);
          return NULL;
        }
      }

      g_object_set (actual_sink, "location", location, NULL);
    }
  } else {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "%s sink not supported",
        priv->largs.sink);
    return NULL;
  }
  g_object_set (actual_sink, "async", FALSE, NULL);
  g_object_set (sink, "video-sink", actual_sink, "text-overlay", FALSE, "sync",
      FALSE, NULL);
  return sink;
}

GstElement *
create_videoratecaps (CustomData * priv, int num)
{
  GstElement *capsfilter;
  GstCaps *caps;
  gchar caps_name[50];
  sprintf (caps_name, "videoratecaps%d", num);
  caps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, priv->largs.outconf[num].width,
      "height", G_TYPE_INT, priv->largs.outconf[num].height,
      "framerate", GST_TYPE_FRACTION, priv->largs.outconf[num].framerate, 1,
      NULL);
  capsfilter = gst_element_factory_make ("capsfilter", caps_name);
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);
  return capsfilter;

}

GstElement *
create_caps (CustomData * priv, int num)
{
  GstElement *capsfilter;
  GstCaps *caps;
  gchar caps_name[50];
  sprintf (caps_name, "caps%d", num);
  caps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, priv->largs.outconf[num].width,
      "height", G_TYPE_INT, priv->largs.outconf[num].height, NULL);
  capsfilter = gst_element_factory_make ("capsfilter", caps_name);
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);
  return capsfilter;

}

GstElement *
create_encparse (CustomData * priv, int num)
{
  gchar name[50];
  GstElement *encparse;

  if (priv->largs.codec_type == CODEC_H264) {
    sprintf (name, "ench264parse%d", num);
    encparse = gst_element_factory_make ("h264parse", name);
  } else {
    sprintf (name, "ench265parse%d", num);
    encparse = gst_element_factory_make ("h265parse", name);
  }

  return encparse;
}

GstElement *
create_enccaps (CustomData * priv, int num)
{
  GstElement *capsfilter;
  GstCaps *caps;
  gchar caps_name[50];
  sprintf (caps_name, "enccaps%d", num);
  capsfilter = gst_element_factory_make ("capsfilter", caps_name);

  if (priv->largs.codec_type == CODEC_H264) {
    if (strlen (priv->largs.outconf[num].h264_profile) &&
        strlen (priv->largs.outconf[num].h264_level)) {
      caps = gst_caps_new_simple ("video/x-h264",
        "profile", G_TYPE_STRING, priv->largs.outconf[num].h264_profile,
        "level", G_TYPE_STRING, priv->largs.outconf[num].h264_level, NULL);
    } else {
      /* Adding "parsed" field here to avoid sentinel warning, as "parsed" 
       * output caps will always be true and doesn't affect */
      caps = gst_caps_new_simple ("video/x-h264", "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
    }
  } else {
    if (strlen (priv->largs.outconf[num].h264_profile) && 
        strlen (priv->largs.outconf[num].h264_level)) {
      caps = gst_caps_new_simple ("video/x-h265",
        "profile", G_TYPE_STRING, priv->largs.outconf[num].h265_profile,
        "level", G_TYPE_STRING, priv->largs.outconf[num].h265_level, NULL);
    } else {
      /* Adding "parsed" field here to avoid sentinel warning, as "parsed" 
       * output caps will always be true and doesn't affect */
      caps = gst_caps_new_simple ("video/x-h265", "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
    }
  }

  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);
  return capsfilter;

}

GstElement *
create_lookahead (CustomData * priv, int num)
{
  GstElement *lookahead;
  gchar la_name[50];
  sprintf (la_name, "lookahead%d", num);


  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "dev-idx = %d",
      priv->largs.dev_idx);
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "b-frames = %d",
      priv->largs.outconf[num].b_frames);
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "codec-type = %d",
      priv->largs.codec_type);

  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "spatial-aq = %d",
      priv->largs.outconf[num].spatial_aq);
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "spatial-aq-gain = %d",
      priv->largs.outconf[num].spatial_aq_gain);

  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "temporal-aq = %d",
      priv->largs.outconf[num].temporal_aq);

  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "lookahead-depth = %d",
      priv->largs.outconf[num].lookahead_depth);
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "kernel-name = %s",
      priv->largs.lookahead_kernel);
  lookahead = gst_element_factory_make ("vvas_xlookahead", la_name);
  g_object_set (lookahead, "dev-idx", priv->largs.dev_idx,
      "b-frames", priv->largs.outconf[num].b_frames,
      "codec-type", priv->largs.codec_type,
      "spatial-aq", priv->largs.outconf[num].spatial_aq,
      "spatial-aq-gain", priv->largs.outconf[num].spatial_aq_gain,
      "temporal-aq", priv->largs.outconf[num].temporal_aq,
      "lookahead-depth", priv->largs.outconf[num].lookahead_depth,
      "kernel-name", priv->largs.lookahead_kernel, NULL);

  g_object_set (lookahead, "xclbin-location", DEFAULT_XCLBIN_PATH, NULL);
  return lookahead;
}

GstElement *
create_encoder (CustomData * priv, int num)
{
  GstElement *encoder;
  gchar enc_name[50];
  sprintf (enc_name, "encoder%d", num);


  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "dev-idx = %d",
      priv->largs.dev_idx);
#ifndef ENABLE_XRM_SUPPORT
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "sk-start-idx = %d",
      priv->largs.enc_sk_cur_idx);
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "sk-cur-idx = %d",
      priv->largs.enc_sk_cur_idx + num);
#endif
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "codec-type = %d",
      priv->largs.codec_type);
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "rc-mode = %d",
      priv->largs.outconf[num].rc_mode);

  encoder = gst_element_factory_make ("vvas_xvcuenc", enc_name);

#ifndef ENABLE_XRM_SUPPORT
  if (is_lookahead_enabled) {
    g_object_set (encoder, "dev-idx", priv->largs.dev_idx,
        "sk-cur-idx", priv->largs.enc_sk_cur_idx + num,
        "b-frames", priv->largs.outconf[num].b_frames,
        "target-bitrate", priv->largs.outconf[num].target_bitrate,
        "gop-length", priv->largs.outconf[num].gop_length,
        "max-bitrate", priv->largs.outconf[num].max_bitrate,
        "rc-mode", priv->largs.outconf[num].rc_mode, NULL);
  } else {
    g_object_set (encoder, "dev-idx", priv->largs.dev_idx,
        "sk-cur-idx", priv->largs.enc_sk_cur_idx + num,
        "b-frames", priv->largs.outconf[num].b_frames,
        "target-bitrate", priv->largs.outconf[num].target_bitrate,
        "gop-length", priv->largs.outconf[num].gop_length,
        "max-bitrate", priv->largs.outconf[num].max_bitrate, NULL);
  }
#else
  if (is_lookahead_enabled) {
    g_object_set (encoder, "dev-idx", priv->largs.dev_idx,
        "b-frames", priv->largs.outconf[num].b_frames,
        "target-bitrate", priv->largs.outconf[num].target_bitrate,
        "gop-length", priv->largs.outconf[num].gop_length,
        "max-bitrate", priv->largs.outconf[num].max_bitrate,
        "rc-mode", priv->largs.outconf[num].rc_mode, NULL);
  } else {
    g_object_set (encoder, "dev-idx", priv->largs.dev_idx,
        "b-frames", priv->largs.outconf[num].b_frames,
        "target-bitrate", priv->largs.outconf[num].target_bitrate,
        "gop-length", priv->largs.outconf[num].gop_length,
        "max-bitrate", priv->largs.outconf[num].max_bitrate, NULL);
  }
#endif

  g_object_set (encoder, "xclbin-location", DEFAULT_XCLBIN_PATH, NULL);
  return encoder;
}

GstElement *
create_decoder (CustomData * priv)
{

  GstElement *decoder;


  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "dev-idx = %d",
      priv->largs.dev_idx);
#ifndef ENABLE_XRM_SUPPORT
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "sk-start-idx = %d",
      priv->largs.dec_sk_cur_idx);
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "sk-cur-idx = %d",
      priv->largs.dec_sk_cur_idx);
#endif

  decoder = gst_element_factory_make ("vvas_xvcudec", "decoder");
#ifndef ENABLE_XRM_SUPPORT
  g_object_set (decoder, "num-entropy-buf", 2,
      "dev-idx", priv->largs.dev_idx,"sk-cur-idx", priv->largs.dec_sk_cur_idx, NULL);
#else
  g_object_set (decoder, "num-entropy-buf", 2,
      "dev-idx", priv->largs.dev_idx, NULL);
#endif
  g_object_set (decoder, "xclbin-location", DEFAULT_XCLBIN_PATH, NULL);

  return decoder;
}

GstElement *
create_scaler (CustomData * priv)
{
  GstElement *scaler;

  scaler = gst_element_factory_make ("vvas_xabrscaler", "scaler");
  if(EXAMPLE_1_USECASE)
    g_object_set (scaler, "dev-idx", priv->largs.dev_idx, "kernel-name", "scaler:{scaler_1}",
      "ppc", 4, "scale-mode", 2, "avoid-output-copy", TRUE, NULL);
  else
    g_object_set (scaler, "dev-idx", priv->largs.dev_idx, "kernel-name", "scaler:{scaler_1}",
      "ppc", 4, "scale-mode", 2, NULL);

  g_object_set (scaler, "xclbin-location", DEFAULT_XCLBIN_PATH, NULL);
  return scaler;
}

GstElement *
create_source (CustomData * priv)
{
  GstElement *source;
  if (!strcmp ("multifilesrc", priv->largs.source)) {
    source = gst_element_factory_make ("multifilesrc", "source");
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
        "source = %s, loop = %d, num_buffers = %d, file = %s\n",
        priv->largs.source, priv->largs.loop, priv->largs.num_buffers,
        priv->largs.input_file);

    g_object_set (source, "location", priv->largs.input_file, "num-buffers",
        priv->largs.num_buffers, "loop", priv->largs.loop, NULL);
  } else if (!strcmp ("filesrc", priv->largs.source)) {
    source = gst_element_factory_make ("filesrc", "source");
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
        "source = %s, loop = %d, num_buffers = %d, file = %s\n",
        priv->largs.source, priv->largs.loop, priv->largs.num_buffers,
        priv->largs.input_file);

    g_object_set (source, "location", priv->largs.input_file, "num-buffers",
        priv->largs.num_buffers, NULL);
  } else {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "%s source not supported",
        priv->largs.source);
    return NULL;
  }
  return source;
}

static void
sigint_handler_sighandler (int signum)
{
  printf ("Caught interrupt -- \n");

  /* we set a flag that is checked by the mainloop, we cannot do much in the
   * interrupt handler (no mutex or other blocking stuff) */
  caught_intr = TRUE;
}

static void
sigint_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = sigint_handler_sighandler;

  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);
}

static void
print_tag_foreach (const GstTagList * tags, const gchar * tag,
    gpointer user_data)
{
  GValue val = { 0, };
  gchar *str;
  int *res = user_data;

  gst_tag_list_copy_value (&val, tags, tag);

  if (G_VALUE_HOLDS_STRING (&val))
    str = g_value_dup_string (&val);
  else
    str = gst_value_serialize (&val);

  if (!strcmp ("video-codec", tag)) {
    if (strstr (str, "H.264")) {
      printf ("input file codec is H.264\n");
      *res = CODEC_H264;
    } else if (strstr (str, "H.265")) {
      printf ("input file codec is H.265\n");
      *res = CODEC_H265;
    } else {
      printf ("ERROR: Codec type %s not supported\n", str);
      *res = -1;
    }
  }
  g_free (str);
  g_value_unset (&val);
}

static int
get_codec_type (const char *path, CustomData * priv)
{

  GError *err = NULL;
  GstDiscoverer *dc;
  GstDiscovererInfo *info;
  gchar *uri;
  const GstTagList *tags;
  int val = -1, w, h;
  GList *video_streams;
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "Get Codec Type");
  dc = gst_discoverer_new (5 * GST_SECOND, &err);

  if (!dc)
    return val;
  if (err)
    return val;

  uri = gst_filename_to_uri (path, &err);
  if (err) {
    g_object_unref (dc);
    return val;
  }

  info = gst_discoverer_discover_uri (dc, uri, &err);
  if (info == NULL)
    return val;
  video_streams = gst_discoverer_info_get_video_streams (info);
  if (video_streams == NULL)
    return val;
  w = gst_discoverer_video_info_get_width (video_streams->data);
  h = gst_discoverer_video_info_get_height (video_streams->data);

  tags = gst_discoverer_info_get_tags (info);
  if (tags) {
    gst_tag_list_foreach (tags, print_tag_foreach, &val);
  }
  printf ("Input file WidthXHeight = %dX%d\n", w, h);
  gst_discoverer_stream_info_list_free (video_streams);
  gst_discoverer_info_unref (info);
  g_object_unref (dc);
  g_free (uri);
  return val;
}

int
get_container_type (const char *path)
{

  GError *err = NULL;
  GstCaps *caps;
  GstDiscoverer *dc;
  GstDiscovererInfo *info;
  GstDiscovererStreamInfo *Sinfo;
  GstStructure *str;
  gchar *uri;
  const gchar *pad_type = NULL;
  int val = -1;

  dc = gst_discoverer_new (5 * GST_SECOND, &err);

  if (!dc)
    return val;
  if (err)
    return val;
  uri = gst_filename_to_uri (path, &err);
  if (err)
    return val;

  info = gst_discoverer_discover_uri (dc, uri, &err);
  Sinfo = gst_discoverer_info_get_stream_info (info);
  caps = gst_discoverer_stream_info_get_caps (Sinfo);
  str = gst_caps_get_structure (caps, 0);
  pad_type = gst_structure_get_name (str);
  if (info == NULL || Sinfo == NULL || caps == NULL
      || str == NULL || pad_type == NULL)
    return val;

  if (!strcmp ("video/quicktime", pad_type)) {
    printf ("input file container is video/quicktime\n");
    val = TYPE_MP4;
  } else if (!strcmp ("video/x-h265", pad_type)) {
    printf ("input file container is video/x-h265\n");
    val = TYPE_ELEM;
  } else if (!strcmp ("video/x-h264", pad_type)) {
    printf ("input file container is video/x-h264\n");
    val = TYPE_ELEM;
  } else
    val = -1;

  gst_discoverer_stream_info_unref (Sinfo);
  gst_discoverer_info_unref (info);
  gst_caps_unref (caps);
  g_object_unref (dc);
  g_free (uri);
  return val;
}

static gchar *
create_symlink (const gchar *path)
{
  gchar *full_path, *symlink_name = NULL;

  full_path = realpath (path, NULL);
  if (full_path) {
    GRand *grand = NULL;
    symlink_name = (gchar *) g_malloc0 (30);
    grand = g_rand_new ();
    if (symlink_name && grand) {
      sprintf(symlink_name, "/tmp/input_%u.h265", g_rand_int(grand));
      if (symlink (full_path, symlink_name)) {
          perror ("Error while creating symlink: ");
          g_free (symlink_name);
          symlink_name = NULL;
      }
      g_rand_free (grand);
    }
    free (full_path);
  }
  return symlink_name;
}

static void
get_container_and_codec_type (const char *path, int *container_type,
                              int *codec_type, CustomData * priv)
{
  gchar *symlink = NULL;
  if (g_str_has_suffix (path, ".hevc")) {
   /*
    * For few hevc streams gst-discover is detecting stream type
    * as H264 and not returning proper values, but the same file
    * is working when we rename or make a symboling link to the
    * same file with .h265 extension.
    * Hence creating a symbolic link with .h265 extension in /tmp/
    * directory, reason for creating it in /tmp is that the directory
    * provided by user may not be writable if we create symbolic link
    * in the same directory.
    */
    symlink = create_symlink (path);
  }
  if (container_type) {
    *container_type = get_container_type (symlink ? symlink : path);
  }
  if (codec_type) {
    *codec_type = get_codec_type (symlink ? symlink : path, priv);
  }
  if (symlink) {
    unlink (symlink);
    g_free (symlink);
  }
}

static void
qtdemux_cb (GstElement * element, GstPad * pad, CustomData * priv)
{

  char name[20];
  GstPad *sinkpad;
  int ret;
  GstStructure *str;
  GstCaps *caps = gst_pad_get_current_caps (pad);
  const gchar *new_pad_type = NULL;
  memcpy (name, gst_element_get_name (element), 10);


  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "got qtdemux_cb");
  str = gst_caps_get_structure (caps, 0);
  new_pad_type = gst_structure_get_name (str);
  if (!g_strrstr (gst_structure_get_name (str), "video")) {
    gst_caps_unref (caps);
    return;
  }
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
      "Pad type %s with qtdemux %s", new_pad_type, name);
  sinkpad = gst_element_get_static_pad (priv->lhead.h26xparse, "sink");
  ret = gst_pad_link (pad, sinkpad);
  if (ret != GST_PAD_LINK_OK)
    printf ("ERROR: linking Qtdemux h26* (%d)\n", ret);

  gst_object_unref (sinkpad);
  gst_caps_unref (caps);
  return;
}

static void
print_usage (void)
{
  printf ("*********************************\n");
  printf ("Usage: \n");
  printf ("vvas_xabrladder [options]\n");
  printf ("\nMandatory Options:\n");
  printf ("\t--devidx <dev-idx>\n");
#ifndef ENABLE_XRM_SUPPORT
  printf ("\t--decstartidx <dec sk-cur-idx>, 0 to 31\n");
  printf ("\t--encstartidx <enc sk-cur-idx>, 0 to 31\n");
#endif
  printf ("\t--codectype <output codec type>, 0=>H264, 1=>H265\n");
  printf ("\t--file <input file>, mp4, h264, h265\n");
  printf ("\nOptional Options:\n");
  printf ("\t--forcekeyframe <keyframe frequency in number of frames>\n");
  printf ("\t--lookahead_enable <0 or 1>\n");
  printf ("\t--json <path to json file>\n");

#ifndef ENABLE_XRM_SUPPORT
  printf
      ("\nexample:\n\tvvas_xabrladder  --devidx 0 --decstartidx 0 --encstartidx 0 --codectype 0 --file <path to file>\n\n");
  printf
      ("\nYou can also use:\n\tvvas_xabrladder -i 0 -d 0 -e 0 -c 0 -f <path to file>\n");
  printf
      ("\nNOTE:\n\tTo run multiple ladder, change value of devidx, decstartidx and encstartidx as per number of output in json file.\n");
#else
  printf
      ("\nexample:\n\tvvas_xabrladder  --devidx 0 --codectype 0 --file <path to file>\n\n");
  printf
      ("\nYou can also use:\n\tvvas_xabrladder -i 0 -c 0 -f <path to file>\n");
  printf
      ("\nNOTE:\n\tTo run multiple ladder, change value of devidx as per number of output in json file.\n");

#endif
  printf ("*********************************\n\n");
}

int
get_files (const gchar * input_dir, CustomData * priv)
{
  DIR *d;
  struct dirent *dir;
  int i = 0, container_type = -1;
  gchar input_file[1000];
  d = opendir (input_dir);
  if (d) {
    while ((dir = readdir (d)) != NULL) {
      if (!strcmp (".", dir->d_name) || !strcmp ("..", dir->d_name)
          || isDirectory (dir->d_name)) {
        continue;
      }

      memset (input_file, '\0', sizeof (input_file[0]) * 1000);
      strcpy (input_file, input_dir);
      strcat (input_file, "/");
      strcat (input_file, dir->d_name);
      get_container_and_codec_type (input_file, &container_type, NULL, NULL);

      if (container_type == -1)
        continue;

      priv->largs.file_array[i] = calloc (1, strlen (input_file) + 1);
      strcpy (priv->largs.file_array[i], input_file);
      i++;

    }
    closedir (d);
  } else
    return -1;
  printf ("number of valid files in %s is %d\n", input_dir, i);
  if (i == 0)
    return i;
  for (i = 0; priv->largs.file_array[i] != NULL; i++)
    printf ("%s\n", priv->largs.file_array[i]);

  return (i);
}

static GstPadProbeReturn
probe_la_buffer (GstPad          *pad,
                  GstPadProbeInfo *info,
                  gpointer         user_data)
{
  GstBuffer *buffer;
  LaPadProbeCbInfo *pad_info = (LaPadProbeCbInfo *)user_data;

  buffer = GST_PAD_PROBE_INFO_BUFFER (info);

  if (pad_info->data_priv->largs.outconf[pad_info->index].dparams.num_dyn_params) {
    if ((pad_info->next_bframe_index < pad_info->data_priv->largs.outconf[pad_info->index].dparams.num_dyn_params) &&
        (pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_bframe_conf[pad_info->next_bframe_index].is_valid) && 
        (pad_info->frame_count == pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_bframe_conf[pad_info->next_bframe_index].frame_num)) {
      g_object_set(G_OBJECT(pad_info->data_priv->ltail[pad_info->index].lookahead), "b-frames", 
        (guint32)(pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_bframe_conf[pad_info->next_bframe_index].b_frames), NULL);

      pad_info->next_bframe_index ++;
    }

    if ((pad_info->next_spatial_aq_index < pad_info->data_priv->largs.outconf[pad_info->index].dparams.num_dyn_params) &&
        (pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_spatial_aq_conf[pad_info->next_spatial_aq_index].is_valid) &&
        (pad_info->frame_count == pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_spatial_aq_conf[pad_info->next_spatial_aq_index].frame_num)) {
      g_object_set(G_OBJECT(pad_info->data_priv->ltail[pad_info->index].lookahead), "spatial-aq", 
        (guint32)(pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_spatial_aq_conf[pad_info->next_spatial_aq_index].spatial_aq), NULL);

      pad_info->next_spatial_aq_index ++;
    }

    if ((pad_info->next_temporal_aq_index < pad_info->data_priv->largs.outconf[pad_info->index].dparams.num_dyn_params) &&
        (pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_temporal_aq_conf[pad_info->next_temporal_aq_index].is_valid) &&
        (pad_info->frame_count == pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_temporal_aq_conf[pad_info->next_temporal_aq_index].frame_num)) {
      g_object_set(G_OBJECT(pad_info->data_priv->ltail[pad_info->index].lookahead), "temporal-aq", 
        (guint32)(pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_temporal_aq_conf[pad_info->next_temporal_aq_index].temporal_aq), NULL);

      pad_info->next_temporal_aq_index ++;
    }

    if ((pad_info->next_spatial_aq_gain_index < pad_info->data_priv->largs.outconf[pad_info->index].dparams.num_dyn_params) &&
        (pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_spatial_aq_gain_conf[pad_info->next_spatial_aq_gain_index].is_valid) &&
        (pad_info->frame_count == pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_spatial_aq_gain_conf[pad_info->next_spatial_aq_gain_index].frame_num)) {
      g_object_set(G_OBJECT(pad_info->data_priv->ltail[pad_info->index].lookahead), "spatial-aq-gain", 
        (guint32)(pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_spatial_aq_gain_conf[pad_info->next_spatial_aq_gain_index].spatial_aq_gain), NULL);

      pad_info->next_spatial_aq_gain_index ++;
    }
  }

  /* If Lookahead is enabled, then insert the event at Lookahead sink pad instead of Encoder sink pad */ 
  if (pad_info->data_priv->largs.force_keyframe_valid == TRUE) {
    /* Force IDR if frame count has reached IDR insertion frequency */
    if (pad_info->frame_count && ((pad_info->frame_count % pad_info->data_priv->largs.force_keyframe_freq) == 0)) {
      gst_pad_send_event(pad, gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,
          gst_structure_new("GstForceKeyUnit", "all-headers", G_TYPE_BOOLEAN, TRUE, NULL)));
    }
  }

  GST_PAD_PROBE_INFO_DATA (info) = buffer;
  pad_info->frame_count ++;

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
probe_enc_buffer (GstPad          *pad,
                  GstPadProbeInfo *info,
                  gpointer         user_data)
{
  GstBuffer *buffer;
  EncPadProbeCbInfo *pad_info = (EncPadProbeCbInfo *)user_data;

  buffer = GST_PAD_PROBE_INFO_BUFFER (info);

  if (pad_info->data_priv->largs.outconf[pad_info->index].dparams.num_dyn_params) {
    /* Update b-frame on encoder only if lookahead if not present in pipeline
     * else bframe is set on lookahead plugin 
     */
    if (pad_info->is_lookahead_enabled == FALSE) {
      if ((pad_info->next_bframe_index < pad_info->data_priv->largs.outconf[pad_info->index].dparams.num_dyn_params) &&
          (pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_bframe_conf[pad_info->next_bframe_index].is_valid) && 
          (pad_info->frame_count == pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_bframe_conf[pad_info->next_bframe_index].frame_num)) {
        g_object_set(G_OBJECT(pad_info->data_priv->ltail[pad_info->index].encoder), "b-frames", 
          (guint32)(pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_bframe_conf[pad_info->next_bframe_index].b_frames), NULL);

        pad_info->next_bframe_index ++;
      }
    }

    if ((pad_info->next_bitrate_index < pad_info->data_priv->largs.outconf[pad_info->index].dparams.num_dyn_params) &&
        (pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_bitrate_conf[pad_info->next_bitrate_index].is_valid) && 
        (pad_info->frame_count == pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_bitrate_conf[pad_info->next_bitrate_index].frame_num)) {
      g_object_set(G_OBJECT(pad_info->data_priv->ltail[pad_info->index].encoder), "target-bitrate", 
        (guint32)(pad_info->data_priv->largs.outconf[pad_info->index].dparams.dyn_bitrate_conf[pad_info->next_bitrate_index].bitrate), NULL);

      pad_info->next_bitrate_index ++;
    }
  }

  /* If Lookahead is enabled, then insert the event at Lookahead sink pad instead of Encoder sink pad */ 
  if (pad_info->is_lookahead_enabled == FALSE && pad_info->data_priv->largs.force_keyframe_valid == TRUE) {
    /* Force IDR if frame count has reached IDR insertion frequency */
    if (pad_info->frame_count && ((pad_info->frame_count % pad_info->data_priv->largs.force_keyframe_freq) == 0)) {
      gst_pad_send_event(pad, gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,
          gst_structure_new("GstForceKeyUnit", "all-headers", G_TYPE_BOOLEAN, TRUE, NULL)));
    }
  }

  GST_PAD_PROBE_INFO_DATA (info) = buffer;
  pad_info->frame_count ++;

  return GST_PAD_PROBE_OK;
}

int
main (int argc, char *argv[])
{
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  CustomData *priv;
  gchar *fps_msg;
  guint delay_show_FPS = 0;
  const gchar *json_file;
  gint int_lookahead_enabled;
  gboolean devidx, decstartidx, encstartidx, codectype, file,
      test, test_itr_nu, force_keyframe;
  int c, total_files = -1, file_nu = -1;
  int i, j;
  EncPadProbeCbInfo enc_probe_info[16];
  LaPadProbeCbInfo la_probe_info[16];
  json_t *root = NULL;
  json_error_t error;

  json_file = DEFAULT_JSON;
  test = TESTCASE_DEFAULT;
  test_itr_nu = DEFAULT_TEST_ITERATTION_NUM;
  devidx = decstartidx = encstartidx = file = codectype = force_keyframe = FALSE;
  priv = (CustomData *) calloc (1, sizeof (CustomData));
  if (priv == NULL) {
    g_printerr ("unable to allocate memory\n");
    return -1;
  }

  while (1) {
    static struct option long_options[] = {
      {"json", required_argument, 0, 'j'},
      {"devidx", required_argument, 0, 'i'},
#ifndef ENABLE_XRM_SUPPORT
      {"decstartidx", required_argument, 0, 'd'},
      {"encstartidx", required_argument, 0, 'e'},
#endif
      {"lookahead_enable", required_argument, 0, 'l'},
      {"codectype", required_argument, 0, 'c'},
      {"file", required_argument, 0, 'f'},
      {"forcekeyframe", required_argument, 0, 'k'},
      {"test", required_argument, 0, 't'},
      {"testitrnu", required_argument, 0, 'n'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    /* getopt_long stores the option index here. */
    int option_index = 0;

    c = getopt_long (argc, argv, "j:i:d:l:e:c:f:k:t:n:h", long_options,
        &option_index);

    /* Detect the end of the options. */
    if (c == -1)
      break;

    switch (c) {
      case 0:
        /* If this option set a flag, do nothing else now. */
        if (long_options[option_index].flag != 0)
          break;
        printf ("option %s", long_options[option_index].name);
        if (optarg)
          break;

      case 'j':
        json_file = optarg;
        break;

      case 'i':
        devidx = TRUE;
        priv->largs.dev_idx = atoi (optarg);
        break;

      case 'c':
        codectype = TRUE;
        priv->largs.codec_type = atoi (optarg);
        if (!(priv->largs.codec_type == 0 || priv->largs.codec_type == 1)) {
          printf ("wrong codec type %d\n", priv->largs.codec_type);
          print_usage ();
          exit (-1);
        }
        break;
#ifndef ENABLE_XRM_SUPPORT
      case 'd':
        decstartidx = TRUE;
        priv->largs.dec_sk_cur_idx = atoi (optarg);
        if (priv->largs.dec_sk_cur_idx < 0 || priv->largs.dec_sk_cur_idx > 31) {
          printf ("Wrong value of dec_sk_cur_idx %d\n",
              priv->largs.dec_sk_cur_idx);
          print_usage ();
          exit (-1);
        }
        break;
      case 'e':
        encstartidx = TRUE;
        priv->largs.enc_sk_cur_idx = atoi (optarg);
        if (priv->largs.enc_sk_cur_idx < 0 || priv->largs.enc_sk_cur_idx > 31) {
          printf ("Wrong value of enc_sk_cur_idx %d\n",
              priv->largs.enc_sk_cur_idx);
          print_usage ();
          exit (-1);
        }
        break;
#endif
      case 'l':
        int_lookahead_enabled = atoi (optarg);
        if (!(int_lookahead_enabled == 0 || int_lookahead_enabled == 1)) {
          printf ("wrong value of lookahead_enabled %d\n",
              int_lookahead_enabled);
          print_usage ();
          exit (-1);
        }
        is_lookahead_enabled = int_lookahead_enabled == 1 ? TRUE : FALSE;
        break;
      case 'f':
        file = TRUE;
        priv->largs.input_file = optarg;

        break;

      case 'k':
        force_keyframe = TRUE;
        priv->largs.force_keyframe_freq = atoi(optarg);
        if (priv->largs.force_keyframe_freq == 0)
          force_keyframe = FALSE;

        break;

      case 'h':
        print_usage ();
        exit (0);
        break;

      case 't':
        test = atoi (optarg);
        break;

      case 'n':
        test_itr_nu = atoi (optarg);
        break;

      case '?':
        /* getopt_long already printed an error message. */
        break;

      default:
        break;
    }
  }
  priv->largs.force_keyframe_valid = force_keyframe;
  
  /* Print any remaining command line arguments (not options). */
  if (optind < argc) {
    printf ("non-option ARGV-elements: ");
    while (optind < argc)
      printf ("%s ", argv[optind++]);
    putchar ('\n');
    print_usage ();
    free (priv);
    exit (-1);
  }

#ifndef ENABLE_XRM_SUPPORT
  if (devidx != TRUE || decstartidx != TRUE
      || encstartidx != TRUE || codectype != TRUE
      || file != TRUE) {
#else
  if (devidx != TRUE || codectype != TRUE
      || file != TRUE) {
#endif
    printf ("ERROR : Wrong input parameters\n");
    print_usage ();
    free (priv);
    exit (-1);
  }

  printf ("input args:\n");
  printf ("json file %s\n", json_file);
  printf ("dev_idx %d\n", priv->largs.dev_idx);
  printf ("lookahead_enabled %s\n",
      is_lookahead_enabled == TRUE ? "TRUE" : "FALSE");
#ifndef ENABLE_XRM_SUPPORT
  printf ("dec_sk_cur_idx %d\n", priv->largs.dec_sk_cur_idx);
  printf ("enc_sk_cur_idx %d\n", priv->largs.enc_sk_cur_idx);
#endif
  printf ("input file %s\n", priv->largs.input_file);
  printf ("force keyframe = %d, keyframe frequency = %d frames\n", 
      force_keyframe, priv->largs.force_keyframe_freq);
  
  if (test != TESTCASE_DEFAULT)
    printf ("test case = %d\n", test);

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  if (test == TESTCASE_2) {
    total_files = get_files (priv->largs.input_file, priv);
    if (total_files == -1) {
      printf ("%s directory not exit\n", priv->largs.input_file);
      goto error;
    } else if (total_files == 0) {
      printf ("%s does not contain a valid file\n", priv->largs.input_file);
      goto error;
    }
    file_nu = 0;

    if (test_itr_nu > DEFAULT_TEST_ITERATTION_NUM) {
      test_itr_nu = DEFAULT_TEST_ITERATTION_NUM;
      printf ("\n*******************************************************\n");
      printf ("Runing pipe without gst_deinit has fd leak problem and \n");
      printf ("gst_deinit do not allow pipe to run again in one process\n");
      printf ("so limit the max iteration to %d\n",
          DEFAULT_TEST_ITERATTION_NUM);
      printf ("\n********************************************************\n");
    }
  } else if (!fileexists (priv->largs.input_file)) {
    g_print ("Input file missing\n");
    free (priv);
    exit (-1);
  }
  if (!fileexists (json_file)) {
    g_print ("json file missing\n");
    free (priv);
    exit (-1);
  }

  sigint_setup ();

  /* get root json object */
  GST_MSG (LOG_LEVEL_DEBUG, LOG_LEVEL_WARNING, "json file %s", json_file);
  root = json_load_file (json_file, JSON_DECODE_ANY, &error);
  if (!root) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
        "failed to load json file. reason %s", error.text);
    goto error;
  }

  if (vvas_read_json (root, priv))
    goto error;

testcase2:
  if (test == TESTCASE_2) {
    strcpy (priv->largs.input_file, priv->largs.file_array[file_nu]);
    printf ("run on %s\n", priv->largs.input_file);
    file_nu++;
    if (file_nu >= total_files)
      file_nu = 0;
  }

  get_container_and_codec_type (priv->largs.input_file, &priv->largs.container,
                                &priv->largs.input_codec, priv);
  if (priv->largs.container == -1) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "unsupported container");
    if (test == TESTCASE_2)
      goto testcase2;
    goto error;
  }
  if (priv->largs.input_codec == -1) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "unsupported codec");
    if (test == TESTCASE_2)
      goto testcase2;
    goto error;
  }

  GST_MSG (LOG_LEVEL_INFO, priv->log_level,
      "Requested Outputs on device (%d) are", priv->largs.dev_idx);

  if (LOG_LEVEL_INFO <= priv->log_level) {
    GST_MSG (LOG_LEVEL_INFO, priv->log_level,
        "Height\tWidth\tFR\tBFrm\tBR\tGOP\tMaxBR\n");

    for (j = 0; j < priv->largs.num_output; j++) {
      GST_MSG (LOG_LEVEL_INFO, priv->log_level,
          "%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
          priv->largs.outconf[j].height, priv->largs.outconf[j].width,
          priv->largs.outconf[j].framerate, priv->largs.outconf[j].b_frames,
          priv->largs.outconf[j].target_bitrate,
          priv->largs.outconf[j].gop_length, priv->largs.outconf[j].max_bitrate);
    }
  }

  /* head of ladder */
  priv->lhead.source = create_source (priv);
  if (!priv->lhead.source) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "source not allocated");
    goto error;
  }
  if (priv->largs.container == TYPE_MP4) {
    priv->lhead.qtdemux = gst_element_factory_make ("qtdemux", "headqtdemux");
    if (!priv->lhead.qtdemux) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "qtdemux not allocated");
      goto error;
    }
  } else
    priv->lhead.qtdemux = NULL;

  if (priv->largs.input_codec == CODEC_H264) {
    priv->lhead.h26xparse = gst_element_factory_make ("h264parse", "h264parse");
  } else if (priv->largs.input_codec == CODEC_H265) {
    priv->lhead.h26xparse = gst_element_factory_make ("h265parse", "h265parse");
  }
  if (!priv->lhead.h26xparse) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "h26xparse not allocated");
    goto error;
  }

  priv->lhead.decoder = create_decoder (priv);
  if (!priv->lhead.decoder) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "decoder not allocated");
    goto error;
  }

  priv->lhead.parse_queue = gst_element_factory_make ("queue", "parse_queue");
  if (!priv->lhead.parse_queue) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "parse queue not allocated");
    goto error;
  }

  priv->lhead.input_queue = gst_element_factory_make ("queue", "input_queue");
  if (!priv->lhead.input_queue) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "input queue not allocated");
    goto error;
  }

  priv->lhead.scaler_queue = gst_element_factory_make ("queue", "sc_queue");
  if (!priv->lhead.scaler_queue) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "scaler queue not allocated");
    goto error;
  }
  priv->lhead.scaler = create_scaler (priv);
  if (!priv->lhead.scaler) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "scaler not allocated");
    goto error;
  }

  /* tail of ladder */
  if (is_lookahead_enabled) {

    for (i = 0; i < priv->largs.num_output; i++) {
      gchar *name;

      name = g_strdup_printf ("enc_queue%u", i);
      priv->ltail[i].enc_queue = gst_element_factory_make ("queue", name);
      g_free (name);

      priv->ltail[i].capsfilter = create_caps (priv, i);

      name = g_strdup_printf ("videorate%u", i);
      priv->ltail[i].videorate = gst_element_factory_make ("videorate", name);
      g_free (name);

      priv->ltail[i].videoratecaps = create_videoratecaps (priv, i);
      priv->ltail[i].lookahead = create_lookahead (priv, i);
      priv->ltail[i].encoder = create_encoder (priv, i);
      priv->ltail[i].encparse = create_encparse (priv, i);
      priv->ltail[i].enccaps = create_enccaps (priv, i);

      priv->ltail[i].sink = create_sink (priv, i);

      if (!priv->ltail[i].videorate ||
          !priv->ltail[i].capsfilter ||
          !priv->ltail[i].videoratecaps ||
          !priv->ltail[i].lookahead ||
          !priv->ltail[i].encoder ||
          !priv->ltail[i].encparse ||
          !priv->ltail[i].enc_queue ||
          !priv->ltail[i].enccaps || !priv->ltail[i].sink) {
        GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
            "Not all tail elements could be created.");
        goto error;
      }
    }
  }


  else {

    for (i = 0; i < priv->largs.num_output; i++) {
      gchar *name;

      name = g_strdup_printf ("enc_queue%u", i);
      priv->ltail[i].enc_queue = gst_element_factory_make ("queue", name);
      g_free (name);

      priv->ltail[i].capsfilter = create_caps (priv, i);

      name = g_strdup_printf ("videorate%u", i);
      priv->ltail[i].videorate = gst_element_factory_make ("videorate", name);
      g_free (name);

      priv->ltail[i].videoratecaps = create_videoratecaps (priv, i);
      priv->ltail[i].encoder = create_encoder (priv, i);
      priv->ltail[i].encparse = create_encparse (priv, i);
      priv->ltail[i].enccaps = create_enccaps (priv, i);

      priv->ltail[i].sink = create_sink (priv, i);

      if (!priv->ltail[i].videorate ||
          !priv->ltail[i].capsfilter ||
          !priv->ltail[i].videoratecaps ||
          !priv->ltail[i].encoder ||
          !priv->ltail[i].encparse ||
          !priv->ltail[i].enc_queue ||
          !priv->ltail[i].enccaps || !priv->ltail[i].sink) {
        GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
            "Not all tail elements could be created.");
        goto error;
      }
    }
  }

  /* Create the empty pipeline */
  priv->pipeline = gst_pipeline_new ("vvas-pipeline");

  if (!priv->pipeline) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level, "empty pipeline not created");
    goto error;
  }
  if (priv->lhead.qtdemux) {
    /* Build the pipeline */
    gst_bin_add_many (GST_BIN (priv->pipeline), priv->lhead.source,
        priv->lhead.input_queue,
        priv->lhead.qtdemux, priv->lhead.h26xparse, priv->lhead.parse_queue,
        priv->lhead.decoder, priv->lhead.scaler_queue, priv->lhead.scaler,
        NULL);
  } else {

    gst_bin_add_many (GST_BIN (priv->pipeline), priv->lhead.source,
        priv->lhead.input_queue,
        priv->lhead.h26xparse, priv->lhead.parse_queue, priv->lhead.decoder,
        priv->lhead.scaler_queue, priv->lhead.scaler, NULL);
  }

  if (EXAMPLE_1_USECASE && priv->largs.num_output != 1) {
    GstCaps *caps;

    priv->ltail[0].caps_queue =
        gst_element_factory_make ("queue", "caps_queue");

    priv->ltail[0].tee_caps =
        gst_element_factory_make ("capsfilter", "tee_caps");
    caps = gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT,
        priv->largs.outconf[0].width, "height", G_TYPE_INT,
        priv->largs.outconf[0].height, NULL);
    g_object_set (priv->ltail[0].tee_caps, "caps", caps, NULL);
    gst_caps_unref (caps);

    priv->ltail[0].tee_queue = gst_element_factory_make ("queue", "tee_queue");
    priv->ltail[0].tee = gst_element_factory_make ("tee", "tee");

    gst_bin_add_many (GST_BIN (priv->pipeline),
        priv->ltail[0].caps_queue,
        priv->ltail[0].tee_caps,
        priv->ltail[0].tee_queue, priv->ltail[0].tee, NULL);
  }

  if (is_lookahead_enabled) {
    for (i = 0; i < priv->largs.num_output; i++) {
      gst_bin_add_many (GST_BIN (priv->pipeline),
          priv->ltail[i].enc_queue,
          priv->ltail[i].capsfilter,
          priv->ltail[i].videorate,
          priv->ltail[i].videoratecaps,
          priv->ltail[i].lookahead,
          priv->ltail[i].encoder,
          priv->ltail[i].encparse,
          priv->ltail[i].enccaps, priv->ltail[i].sink, NULL);
    }
  } else {
    for (i = 0; i < priv->largs.num_output; i++) {
      gst_bin_add_many (GST_BIN (priv->pipeline),
          priv->ltail[i].enc_queue,
          priv->ltail[i].capsfilter,
          priv->ltail[i].videorate,
          priv->ltail[i].videoratecaps,
          priv->ltail[i].encoder,
          priv->ltail[i].encparse,
          priv->ltail[i].enccaps, priv->ltail[i].sink, NULL);
    }
  }


  if (priv->lhead.qtdemux) {

    if (gst_element_link_many (priv->lhead.source, priv->lhead.input_queue,
            priv->lhead.qtdemux, NULL) != TRUE) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "Head elements source with qtdemux could not be linked");
      gst_object_unref (priv->pipeline);
      goto error;
    }

    if (gst_element_link_many (priv->lhead.h26xparse, priv->lhead.parse_queue,
            priv->lhead.decoder, priv->lhead.scaler_queue, priv->lhead.scaler,
            NULL) != TRUE) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "Head elements h26xparse decoder and scaler could not be linked");
      gst_object_unref (priv->pipeline);
      goto error;
    }


    if (!g_signal_connect (priv->lhead.qtdemux, "pad-added",
            G_CALLBACK (qtdemux_cb), priv)) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "Error registring qtdemux callback");
      gst_object_unref (priv->pipeline);
      goto error;
    }

  } else {
    if (gst_element_link_many (priv->lhead.source, priv->lhead.input_queue,
            priv->lhead.h26xparse, priv->lhead.parse_queue, priv->lhead.decoder,
            priv->lhead.scaler_queue, priv->lhead.scaler, NULL) != TRUE) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "Head elements could not be linked");
      gst_object_unref (priv->pipeline);
      goto error;
    }
  }

  if (EXAMPLE_1_USECASE && priv->largs.num_output != 1) {

    if (gst_element_link_many (priv->ltail[0].caps_queue,
            priv->ltail[0].tee_caps,
            priv->ltail[0].tee_queue, priv->ltail[0].tee, NULL) != TRUE) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "Tail %d elements before tee could not be linked", 0);
      gst_object_unref (priv->pipeline);
      goto error;
    }
  }
  if (is_lookahead_enabled) {
    for (i = 0; i < priv->largs.num_output; i++) {
      if (gst_element_link_many (priv->ltail[i].enc_queue,
              priv->ltail[i].capsfilter,
              priv->ltail[i].videorate,
              priv->ltail[i].videoratecaps,
              priv->ltail[i].lookahead,
              priv->ltail[i].encoder,
              priv->ltail[i].encparse,
              priv->ltail[i].enccaps, priv->ltail[i].sink, NULL) != TRUE) {
        GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
            "Tail %d elements could not be linked", i);
        gst_object_unref (priv->pipeline);
        goto error;
      }
    }
  } else {
    for (i = 0; i < priv->largs.num_output; i++) {
      if (gst_element_link_many (priv->ltail[i].enc_queue,
              priv->ltail[i].capsfilter,
              priv->ltail[i].videorate,
              priv->ltail[i].videoratecaps,
              priv->ltail[i].encoder,
              priv->ltail[i].encparse,
              priv->ltail[i].enccaps, priv->ltail[i].sink, NULL) != TRUE) {
        GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
            "Tail %d elements could not be linked", i);
        gst_object_unref (priv->pipeline);
        goto error;
      }
    }
  }


  /* Manually link the Scaler, which has "Request" pads */
  if (EXAMPLE_1_USECASE && priv->largs.num_output != 1) {
    priv->ltail[0].sc_output_pad =
        gst_element_get_request_pad (priv->lhead.scaler, "src_0");
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
        "Obtained request pad %s for sc output0 branch.",
        gst_pad_get_name (priv->ltail[0].sc_output_pad));
    priv->ltail[0].caps_queue_pad =
        gst_element_get_static_pad (priv->ltail[0].caps_queue, "sink");

    if (gst_pad_link (priv->ltail[0].sc_output_pad,
            priv->ltail[0].caps_queue_pad) != GST_PAD_LINK_OK) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "Scaler pad %d could not be linked", 0);
      gst_object_unref (priv->pipeline);
      goto error;
    }
    gst_object_unref (priv->ltail[0].caps_queue_pad);

    priv->ltail[0].tee_output_pad =
        gst_element_get_request_pad (priv->ltail[0].tee, "src_0");
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
        "Obtained request pad %s for tee output0 branch.",
        gst_pad_get_name (priv->ltail[0].tee_output_pad));
    priv->ltail[0].enc_queue_pad =
        gst_element_get_static_pad (priv->ltail[0].enc_queue, "sink");

    if (gst_pad_link (priv->ltail[0].tee_output_pad,
            priv->ltail[0].enc_queue_pad) != GST_PAD_LINK_OK) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "Tee pad %d could not be linked", 0);
      gst_object_unref (priv->pipeline);
      goto error;
    }
    gst_object_unref (priv->ltail[0].enc_queue_pad);

    priv->ltail[1].tee_output_pad =
        gst_element_get_request_pad (priv->ltail[0].tee, "src_1");
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
        "Obtained request pad %s for tee output0 branch.",
        gst_pad_get_name (priv->ltail[0].tee_output_pad));
    priv->ltail[1].enc_queue_pad =
        gst_element_get_static_pad (priv->ltail[1].enc_queue, "sink");

    if (gst_pad_link (priv->ltail[1].tee_output_pad,
            priv->ltail[1].enc_queue_pad) != GST_PAD_LINK_OK) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "Tee pad %d could not be linked", 1);
      gst_object_unref (priv->pipeline);
      goto error;
    }
    gst_object_unref (priv->ltail[1].enc_queue_pad);

    i = 2;
  } else
    i = 0;
  for (; i < priv->largs.num_output; i++) {
    gchar *name;
    name = g_strdup_printf ("src_%u", i);
    priv->ltail[i].sc_output_pad =
        gst_element_get_request_pad (priv->lhead.scaler, name);
    GST_MSG (LOG_LEVEL_DEBUG, priv->log_level,
        "Obtained request pad %s for sc output0 branch.",
        gst_pad_get_name (priv->ltail[i].sc_output_pad));
    priv->ltail[i].enc_queue_pad =
        gst_element_get_static_pad (priv->ltail[i].enc_queue, "sink");
    g_free (name);

    if (gst_pad_link (priv->ltail[i].sc_output_pad,
            priv->ltail[i].enc_queue_pad) != GST_PAD_LINK_OK) {
      GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
          "Scaler pad %d could not be linked", i);
      gst_object_unref (priv->pipeline);
      goto error;
    }
    gst_object_unref (priv->ltail[i].enc_queue_pad);
  }

  if (force_keyframe == TRUE || priv->largs.is_dyn_params_present == TRUE) {
    memset(enc_probe_info, 0, 16 * sizeof(EncPadProbeCbInfo));
    srand(time(0));
    for (i = 0; i < priv->largs.num_output; i++) {
      GstPad *enc_sink_pad = 
          gst_element_get_static_pad (priv->ltail[i].encoder, "sink");
      enc_probe_info[i].index = i;
      enc_probe_info[i].data_priv = priv;
      enc_probe_info[i].is_lookahead_enabled = is_lookahead_enabled;
      enc_probe_info[i].frame_count = 0;
      
      priv->ltail[i].enc_pad_probe_id = gst_pad_add_probe (enc_sink_pad, 
        GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) probe_enc_buffer, &enc_probe_info[i], NULL);
      gst_object_unref (enc_sink_pad);
    }
  }
  
  if (is_lookahead_enabled && (priv->largs.is_dyn_params_present == TRUE || force_keyframe == TRUE)) {
    memset(la_probe_info, 0, 16 * sizeof(LaPadProbeCbInfo));
    for (i = 0; i < priv->largs.num_output; i++) {
      GstPad *la_sink_pad = 
          gst_element_get_static_pad (priv->ltail[i].lookahead, "sink");
      la_probe_info[i].index = i;
      la_probe_info[i].data_priv = priv;
      la_probe_info[i].frame_count = 0;
      
      priv->ltail[i].la_pad_probe_id = gst_pad_add_probe (la_sink_pad, 
        GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) probe_la_buffer, &la_probe_info[i], NULL);
      gst_object_unref (la_sink_pad);
    }
  }

  if (root)
    json_decref (root);

#ifdef GET_DOT_FILE
  GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN (priv->pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
      "pipeline");
#endif
testcase1:
  GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "going to play pipe");
  /* Start playing */
  ret = gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
        "Unable to set the pipeline to the playing state.");
    gst_object_unref (priv->pipeline);
    goto error;
  }
  GST_MSG (LOG_LEVEL_INFO, priv->log_level, "now playing.....");

  /* Wait until error or EOS */
  bus = gst_element_get_bus (priv->pipeline);
  while (1) {
    msg = gst_bus_pop (bus);

    /* Note that because input timeout is GST_CLOCK_TIME_NONE,
       the gst_bus_timed_pop_filtered() function will block forever until a
       matching message was posted on the bus (GST_MESSAGE_ERROR or
       GST_MESSAGE_EOS). */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;
      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
              "Error received from element %s: %s", GST_OBJECT_NAME (msg->src),
              err->message);
          GST_MSG (LOG_LEVEL_ERROR, priv->log_level,
              "Debugging information: %s", debug_info ? debug_info : "none");
          g_clear_error (&err);
          g_free (debug_info);
          gst_message_unref (msg);
          goto stop_pipeline;
        case GST_MESSAGE_EOS:
          GST_MSG (LOG_LEVEL_INFO, priv->log_level, "End-Of-Stream reached.\n");
          if (!strcmp ("filesink", priv->largs.sink)) {
#ifndef ENABLE_XRM_SUPPORT
            GST_MSG (LOG_LEVEL_INFO, priv->log_level,
                "Output is stored in %s directory\n", DEFAULT_OUTPUT_PATH);
#else
            GST_MSG (LOG_LEVEL_INFO, priv->log_level,
                "Output is stored in %s directory with date and timestamps\n", DEFAULT_OUTPUT_PATH);
#endif
          }
          gst_message_unref (msg);
          goto stop_pipeline;
        default:
          ;
      }
      gst_message_unref (msg);
    }

    if (caught_intr == TRUE) {
      GST_MSG (LOG_LEVEL_DEBUG, priv->log_level, "caught_intr == TRUE");
      goto stop_pipeline;
    }

    if (priv->largs.fps_display == TRUE)
      for (i = 0; i < priv->largs.num_output; i++) {
        /* Display information FPS to console */
        g_object_get (G_OBJECT (priv->ltail[i].sink), "last-message", &fps_msg,
            NULL);
        if (fps_msg != NULL) {
          if ((delay_show_FPS % DEFAULT_FPS_DELAY) == 0) {
#ifndef ENABLE_XRM_SUPPORT
            GST_MSG (LOG_LEVEL_INFO, priv->log_level,
                "Frame info devid/ladder[%d/%d] output_%dx%dp%d: \tlast-message = %s",
                priv->largs.dev_idx,
                priv->largs.dec_sk_cur_idx,
                priv->largs.outconf[i].width,
                priv->largs.outconf[i].height,
                priv->largs.outconf[i].framerate, fps_msg);
#else
            GST_MSG (LOG_LEVEL_INFO, priv->log_level,
                "Frame info devid/pid[%d/%ld] output_%dx%dp%d: \tlast-message = %s",
                priv->largs.dev_idx,
		(long)getpid(),
                priv->largs.outconf[i].width,
                priv->largs.outconf[i].height,
                priv->largs.outconf[i].framerate, fps_msg);
#endif
            delay_show_FPS = 0;
          }
        }
        g_free (fps_msg);
      }
    delay_show_FPS++;
  }

stop_pipeline:

  if (force_keyframe == TRUE || priv->largs.is_dyn_params_present == TRUE) {
    for (i = 0; i < priv->largs.num_output; i++) {
      GstPad *enc_sink_pad = 
          gst_element_get_static_pad (priv->ltail[i].encoder, "sink");
      
      gst_pad_remove_probe(enc_sink_pad, priv->ltail[i].enc_pad_probe_id);
      gst_object_unref (enc_sink_pad);
    }
  }
  
  if (is_lookahead_enabled && priv->largs.is_dyn_params_present == TRUE) {
    for (i = 0; i < priv->largs.num_output; i++) {
      GstPad *la_sink_pad = 
          gst_element_get_static_pad (priv->ltail[i].lookahead, "sink");
      
      gst_pad_remove_probe(la_sink_pad, priv->ltail[i].la_pad_probe_id);
      gst_object_unref (la_sink_pad);
    }
  }
  
  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (priv->pipeline, GST_STATE_NULL);
  if (test == TESTCASE_1 && !caught_intr && --test_itr_nu) {
    printf ("Will run %d more time \n", test_itr_nu);
    printf ("going to start again \n");
    goto testcase1;
  }

  for (i = 0; i < priv->largs.num_output; i++) {
    if (EXAMPLE_1_USECASE && i == 1) {
      gst_element_release_request_pad (priv->ltail[0].tee,
          priv->ltail[0].tee_output_pad);
      gst_object_unref (priv->ltail[0].tee_output_pad);
      gst_element_release_request_pad (priv->ltail[0].tee,
          priv->ltail[1].tee_output_pad);
      gst_object_unref (priv->ltail[1].tee_output_pad);
      continue;
    }
    gst_element_release_request_pad (priv->lhead.scaler,
        priv->ltail[i].sc_output_pad);
    gst_object_unref (priv->ltail[i].sc_output_pad);
  }

  gst_object_unref (priv->pipeline);

  if (test == TESTCASE_2 && !caught_intr && --test_itr_nu) {
    printf ("Will run %d more time \n", test_itr_nu);
    printf ("going to start again \n");
    goto testcase2;
  }

  gst_deinit ();
  for (i = 0; i < priv->largs.num_output; i++) {
    if(priv->largs.outconf[i].dparams.num_dyn_params) {
      free(priv->largs.outconf[i].dparams.dyn_bframe_conf);
      free(priv->largs.outconf[i].dparams.dyn_bitrate_conf);
      free(priv->largs.outconf[i].dparams.dyn_spatial_aq_conf);
      free(priv->largs.outconf[i].dparams.dyn_temporal_aq_conf);
      free(priv->largs.outconf[i].dparams.dyn_spatial_aq_gain_conf);
    }
  }
  free (priv);
  return 0;
error:
  gst_deinit ();
  free (priv);
  return -1;
}
