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

#include <glib-unix.h>
#include <gst/app/gstappsrc.h>
#include <gst/pbutils/pbutils.h>
#include <gst/gst.h>
#include <stdio.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Default values */
#define DEF_DEV_IDX	0
#define DEF_NUM_FRAMES	2000
#define DEF_MAX_BITRATE 5000
#define DEF_DEC_IDX 	0
#define DEF_ENC_IDX     0
#define DEF_OUTFILE	"result.mp4"
#define DEF_LOGO_X 	0.9
#define DEF_LOGO_Y 	0

#define TYPE_MP4	1
#define TYPE_ELEM	2
#define TYPE_H264	1
#define TYPE_H265	2

#define MAX_IN_WIDTH 1920
#define MAX_IN_HEIGHT 1080
#define DEFAULT_XCLBIN_PATH "/opt/xilinx/xcdr/xclbins/transcode.xclbin"

static GMainLoop *loop = NULL;
GstElement *pipeline;
static guint signal_intr_id;
GCond cond;

typedef struct _comp_data
{
  GstElement *comp, *enc, *capsfilter, *sink, *q1, *q2, *q3, *h264_out, *qtmux;
  GstElement *src[4], *dec[4], *h26[4], *olay, *stmp, *qtdemux[4];
  GstBin *bin;
} comp_data;
typedef struct user_choice
{
  int src;
  int model;
  int sink;
} options;


static gboolean
intr_handler (gpointer user_data)
{
  printf ("Interrupt: quitting loop ...\n");
  g_main_loop_quit (loop);
  /* remove signal handler */
  signal_intr_id = 0;
  return G_SOURCE_REMOVE;
}

struct user_config
{
  char *fpath[4];
  char *lpath;
  char *outfile;
  int devidx;
  int enc_max_bit;
#ifndef ENABLE_XRM_SUPPORT
  int decidx;
  int encidx;
#endif
  float logo_x;
  float logo_y;
  int cont_t[4];
  int codec_t[4];
  int width_in;
  int height_in;
  int nbuffers;
};

int get_container_type (char *path);
void print_user_options (struct user_config *uconfig);
void exit_pipeline (void);
static void usage (void);
static gchar *create_symlink (const gchar *path);
static void get_container_and_codec_type (char *path, int *container_type,
                                          int *codec_type, struct user_config *uconfig);

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
      *res = TYPE_H264;
    } else if (strstr (str, "H.265")) {
      *res = TYPE_H265;
    } else {
      printf ("ERROR: Codec type %s not supported\n", str);
      *res = -1;
    }
  }
  g_free (str);
  g_value_unset (&val);
}

int
get_container_type (char *path)
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
  if(!GST_IS_DISCOVERER_INFO (info) || err) {
    printf("Error while discovering media file : %s\n",
            err ? err->message : "");
    return -1;
  }
  Sinfo = gst_discoverer_info_get_stream_info (info);
  caps = gst_discoverer_stream_info_get_caps (Sinfo);
  str = gst_caps_get_structure (caps, 0);
  pad_type = gst_structure_get_name (str);

  if (!strcmp ("video/quicktime", pad_type))
    val = TYPE_MP4;
  else if (!strcmp ("video/x-h265", pad_type))
    val = TYPE_ELEM;
  else if (!strcmp ("video/x-h264", pad_type))
    val = TYPE_ELEM;
  else
    val = 0;

  gst_discoverer_stream_info_unref (Sinfo);
  gst_discoverer_info_unref (info);
  gst_caps_unref (caps);
  g_object_unref (dc);
  g_free (uri);
  return val;
}

static int
get_codec_type (char *path, struct user_config *uconfig)
{

  GError *err = NULL;
  GstDiscoverer *dc;
  GstDiscovererInfo *info;
  gchar *uri;
  const GstTagList *tags;
  int val = -1, w, h;
  GList *video_streams;
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
  if(!GST_IS_DISCOVERER_INFO (info) || err) {
    printf("Error while discovering media file : %s\n",
            err ? err->message : "");
    return -1;
  }
  video_streams = gst_discoverer_info_get_video_streams (info);
  w = gst_discoverer_video_info_get_width (video_streams->data);
  h = gst_discoverer_video_info_get_height (video_streams->data);
  gst_discoverer_stream_info_list_free (video_streams);
  if (uconfig->width_in == -1 && uconfig->height_in == -1) {
    if (w > MAX_IN_WIDTH || h > MAX_IN_HEIGHT) {
      printf ("ERROR: All input streams should be <= 1080p\n");
      return val;
    }
    uconfig->width_in = w;
    uconfig->height_in = h;
  } else if (uconfig->width_in != w || uconfig->height_in != h) {
    printf ("ERROR: All input streams should be of same resolution and should be <= 1080p\n");
    gst_discoverer_info_unref (info);
    g_object_unref (dc);
    g_free (uri);
    return val;
  }

  tags = gst_discoverer_info_get_tags (info);
  if (tags) {
    gst_tag_list_foreach (tags, print_tag_foreach, &val);
  }
  gst_discoverer_info_unref (info);
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
get_container_and_codec_type (char *path, int *container_type,
                              int *codec_type, struct user_config *uconfig)
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
    *codec_type = get_codec_type (symlink ? symlink : path, uconfig);
  }
  if (symlink) {
    unlink (symlink);
    g_free (symlink);
  }
}

void
print_user_options (struct user_config *uconfig)
{
  GST_INFO ("File 1 %s", uconfig->fpath[0]);
  GST_INFO ("File 2 %s", uconfig->fpath[1]);
  GST_INFO ("File 3 %s", uconfig->fpath[2]);
  GST_INFO ("File 4 %s", uconfig->fpath[3]);
  GST_INFO ("Logo file path %s", uconfig->lpath);
  GST_INFO ("Logo x relative pos is %f", uconfig->logo_x);
  GST_INFO ("Logo y relative pos is %f", uconfig->logo_y);
  GST_INFO ("Output file name is %s", uconfig->outfile);
  GST_INFO ("Max bit rate %d", uconfig->enc_max_bit);
  GST_INFO ("Device IDx value is %d", uconfig->devidx);
#ifndef ENABLE_XRM_SUPPORT
  GST_INFO ("Decoder start index is %d", uconfig->decidx);
  GST_INFO ("Encoder start index is %d", uconfig->encidx);
#endif
  GST_INFO ("width IN = %d", uconfig->width_in);
  GST_INFO ("height IN = %d", uconfig->height_in);
}

static void
fpsdisplaysink_cb (GstElement * element, gdouble fps, gdouble droprate,
    gdouble avgfps, gpointer user_data)
{
  g_print ("Average FPS %.2f\n", avgfps);
}

static void
qtdemux_cb (GstElement * element, GstPad * pad, comp_data * elms)
{

  char name[20];
  GstPad *sinkpad;
  int idx;
  int ret;
  GstStructure *str;
  GstCaps *caps = gst_pad_get_current_caps (pad);
  const gchar *new_pad_type = NULL;
  memcpy (name, gst_element_get_name (element), 10);
  idx = name[9] - '0';

  if (strncmp (name, "qtdemux__", 9))
    return;
  str = gst_caps_get_structure (caps, 0);
  new_pad_type = gst_structure_get_name (str);
  if (!g_strrstr (gst_structure_get_name (str), "video")) {
    gst_caps_unref (caps);
    return;
  }
  GST_DEBUG ("Pad type %s for idx %d", new_pad_type, idx);
  sinkpad = gst_element_get_static_pad (elms->h26[idx], "sink");
  ret = gst_pad_link (pad, sinkpad);
  if (ret != GST_PAD_LINK_OK)
    printf ("ERROR: linking Qtdemux h26* (%d)\n", ret);

  gst_object_unref (sinkpad);
  gst_caps_unref (caps);
  return;
}

static int
dec_pipe_mp4 (struct user_config *uconfig, comp_data * elms, int idx)
{

  gboolean bret;
  char name[50];

  sprintf (name, "%s_%d", "qtdemux_", idx);
  elms->src[idx] = gst_element_factory_make ("filesrc", NULL);
  elms->qtdemux[idx] = gst_element_factory_make ("qtdemux", name);
  elms->dec[idx] = gst_element_factory_make ("vvas_xvcudec", NULL);

  if (uconfig->codec_t[idx] == TYPE_H264)
    elms->h26[idx] = gst_element_factory_make ("h264parse", NULL);
  else if (uconfig->codec_t[idx] == TYPE_H265)
    elms->h26[idx] = gst_element_factory_make ("h265parse", NULL);
  else {
    printf ("ERROR: Unsupported stream fomrat\n");
    return -1;
  }

  g_object_set (elms->src[idx], "location", uconfig->fpath[idx], "num-buffers",
      uconfig->nbuffers, NULL);
#ifndef ENABLE_XRM_SUPPORT
  g_object_set (elms->dec[idx], "num-entropy-buf", 2, "dev-idx", uconfig->devidx, "sk-cur-idx",
      uconfig->decidx + idx, NULL);
#else
  g_object_set (elms->dec[idx], "num-entropy-buf", 2, "dev-idx", uconfig->devidx, NULL);
#endif

  g_object_set (elms->dec[idx], "xclbin-location", DEFAULT_XCLBIN_PATH, NULL);

  if (!elms->src[idx] || !elms->dec[idx] || !elms->h26[idx]
      || !elms->qtdemux[idx]) {
    printf ("ERROR: failed to create decode %d pipeline bin...\n\n", idx);
    return -1;
  }
  gst_bin_add_many (elms->bin, elms->src[idx], elms->qtdemux[idx],
      elms->h26[idx], elms->dec[idx], NULL);
  bret = gst_element_link (elms->src[idx], elms->qtdemux[idx]);
  if (bret == FALSE) {
    printf ("ERROR: Failure linking %d qtdemux\n", idx);
    return -1;
  }
  bret = gst_element_link (elms->h26[idx], elms->dec[idx]);
  if (bret == FALSE) {
    printf ("ERROR: Failure linking %d qtdemux\n", idx);
    return -1;
  }
  if (!g_signal_connect (elms->qtdemux[idx], "pad-added",
          G_CALLBACK (qtdemux_cb), elms)) {
    printf ("ERROR: Error registring qtdemux callback\n");
    return -1;
  }
  return 0;
}

static int
dec_pipe (struct user_config *uconfig, comp_data * elms, int idx)
{
  gboolean bret;

  /* prepare the pipeline */
  elms->src[idx] = gst_element_factory_make ("filesrc", NULL);
  elms->dec[idx] = gst_element_factory_make ("vvas_xvcudec", NULL);
  if (uconfig->codec_t[idx] == TYPE_H264)
    elms->h26[idx] = gst_element_factory_make ("h264parse", NULL);
  else if (uconfig->codec_t[idx] == TYPE_H265)
    elms->h26[idx] = gst_element_factory_make ("h265parse", NULL);
  else {
    printf ("ERROR: Unsupported stream fomrat\n");
    return -1;
  }

  if (!elms->src[idx] || !elms->dec[idx] || !elms->h26[idx]) {
    printf ("ERROR: failed to create decode %d pipeline bin...\n\n", idx);
    return -1;
  }

  g_object_set (elms->src[idx], "location", uconfig->fpath[idx], "num-buffers",
      uconfig->nbuffers, NULL);
#ifndef ENABLE_XRM_SUPPORT
  g_object_set (elms->dec[idx], "num-entropy-buf", 2, "dev-idx", uconfig->devidx, "sk-cur-idx",
      uconfig->decidx + idx, NULL);
#else
  g_object_set (elms->dec[idx], "num-entropy-buf", 2, "dev-idx", uconfig->devidx, NULL);
#endif

  g_object_set (elms->dec[idx], "xclbin-location", DEFAULT_XCLBIN_PATH, NULL);

  gst_bin_add_many (elms->bin, elms->src[idx], elms->h26[idx], elms->dec[idx],
      NULL);
  /* dec pipe */
  bret =
      gst_element_link_many (elms->src[idx], elms->h26[idx], elms->dec[idx],
      NULL);
  if (bret == FALSE) {
    g_error ("Failure Dec %d pipeline.", idx);
    return -1;
  }

  return 0;
}

static int
comp_pipe (struct user_config *uconfig, comp_data * elms)
{
  GstCaps *caps = NULL;
  gboolean bret;
  char comp_name[10] = "mix";
  GstPad *srcpad1, *sinkpad1, *srcpad2, *sinkpad2, *srcpad3, *sinkpad3;
  int width_out = uconfig->width_in * 2, height_out = uconfig->height_in * 2;
  GstElement *base_sink;

  print_user_options (uconfig);
  /* prepare the pipeline */
  elms->capsfilter = gst_element_factory_make ("capsfilter", NULL);
  elms->enc = gst_element_factory_make ("vvas_xvcuenc", NULL);
  elms->h264_out = gst_element_factory_make ("h264parse", NULL);
  elms->q1 = gst_element_factory_make ("queue", NULL);
  elms->q2 = gst_element_factory_make ("queue", NULL);
  elms->q3 = gst_element_factory_make ("queue", NULL);
  elms->olay = gst_element_factory_make ("gdkpixbufoverlay", NULL);
  elms->stmp = gst_element_factory_make ("timeoverlay", NULL);
  elms->comp = gst_element_factory_make ("compositor", NULL);
  elms->qtmux = gst_element_factory_make ("qtmux", NULL);
  elms->sink = gst_element_factory_make ("fpsdisplaysink", NULL);
  base_sink = gst_element_factory_make ("filesink", NULL);

  if (!elms->capsfilter || !elms->stmp || !elms->olay || !elms->enc || !elms->q1
      || !elms->q2 || !elms->q3 || !elms->comp || !elms->sink || !elms->qtmux
      || !elms->h264_out || !base_sink) {
    printf ("ERROR: failed to create final base pipeline bin...\n\n");
    return -1;
  }

  if (uconfig->lpath)
    g_object_set (elms->olay, "location", uconfig->lpath, "relative-x",
        uconfig->logo_x, "relative-y", uconfig->logo_y, NULL);
  g_object_set (elms->stmp, "valignment", 1, "halignment", 1, "draw-outline", 0,
      "font-desc", "Sans, 8", "color", 0, "time-mode", 2, NULL);
  g_object_set (elms->comp, "name", comp_name, NULL);

#ifndef ENABLE_XRM_SUPPORT
  g_object_set (elms->enc, "dev-idx",
      uconfig->devidx, "sk-cur-idx", uconfig->encidx, "max-bitrate",
      uconfig->enc_max_bit, NULL);
#else
  g_object_set (elms->enc, "dev-idx",
      uconfig->devidx, "max-bitrate", uconfig->enc_max_bit, NULL);
#endif

  g_object_set (elms->enc, "xclbin-location", DEFAULT_XCLBIN_PATH, NULL);
  caps =
      gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT, width_out,
      "height", G_TYPE_INT, height_out, "format", G_TYPE_STRING, "NV12", NULL);
  g_object_set (elms->capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  g_object_set (base_sink, "location", uconfig->outfile, "sync", FALSE, NULL);
  g_object_set (elms->sink, "video-sink", base_sink, "text-overlay", FALSE,
      "sync", FALSE, "fps-update-interval", 1000, "signal-fps-measurements",
      TRUE, NULL);
  //g_object_set (elms->sink, "video-sink", base_sink, "text-overlay", FALSE, "sync", FALSE, NULL);

  gst_bin_add_many (elms->bin, elms->comp, elms->capsfilter, elms->q1,
      elms->olay, elms->q2, elms->stmp, elms->q3, elms->enc, elms->h264_out,
      elms->qtmux, elms->sink, NULL);


  /* Sink pipe */
  bret =
      gst_element_link_many (elms->comp, elms->capsfilter, elms->q1, elms->olay,
      elms->q2, elms->stmp, elms->q3, elms->enc, elms->h264_out, elms->qtmux,
      elms->sink, NULL);
  if (bret == FALSE) {
    g_error ("Failure linking Enc pipeline");
    return -1;
  }

 /*** Link request pads ***/
  /* 1st pipe */
  bret = gst_element_link (elms->dec[0], elms->comp);
  if (!bret)
    return -1;
  /* 2nd pipe */
  srcpad1 = gst_element_get_static_pad (elms->dec[1], "src");
  if (srcpad1 == NULL) {
    printf ("ERROR: failed to get VCU dec 2 src pad\n");
    return -1;
  }
  GST_DEBUG ("Obtained static pad %s for Decoder 2.",
      gst_pad_get_name (srcpad1));
  sinkpad1 = gst_element_get_request_pad (elms->comp, "sink_%u");
  if (sinkpad1 == NULL) {
    printf ("ERROR: failed to get compositor sink pad\n");
    return -1;
  }
  GST_DEBUG ("Obtained Request pad %s for compositor.",
      gst_pad_get_name (sinkpad1));
  g_object_set (sinkpad1, "xpos", uconfig->width_in, NULL);
  if (gst_pad_link (srcpad1, sinkpad1) != GST_PAD_LINK_OK) {
    printf ("ERROR: failed to link Dec 2 with Compositor pad\n");
    gst_object_unref (sinkpad1);
    gst_object_unref (srcpad1);
  }
  /* 3rd pipe */
  srcpad2 = gst_element_get_static_pad (elms->dec[2], "src");
  if (srcpad2 == NULL) {
    printf ("ERROR: failed to get VCU dec 3 src pad\n");
    return -1;
  }
  GST_DEBUG ("Obtained static pad %s for Decoder 3.",
      gst_pad_get_name (srcpad2));
  sinkpad2 = gst_element_get_request_pad (elms->comp, "sink_%u");
  if (sinkpad2 == NULL) {
    printf ("ERROR: failed to get compositor sink pad\n");
    return -1;
  }
  GST_DEBUG ("Obtained Request pad %s for compositor.",
      gst_pad_get_name (sinkpad2));
  g_object_set (sinkpad2, "ypos", uconfig->height_in, NULL);
  if (gst_pad_link (srcpad2, sinkpad2) != GST_PAD_LINK_OK) {
    printf ("ERROR: failed to link Dec 3 with Compositor pad\n");
    gst_object_unref (sinkpad2);
    gst_object_unref (srcpad2);
  }
  /* 4th pipe */
  srcpad3 = gst_element_get_static_pad (elms->dec[3], "src");
  if (srcpad3 == NULL) {
    printf ("ERROR: failed to get VCU dec 4 src pad\n");
    return -1;
  }
  GST_DEBUG ("Obtained static pad %s for Decoder 4.",
      gst_pad_get_name (srcpad3));
  sinkpad3 = gst_element_get_request_pad (elms->comp, "sink_%u");
  if (sinkpad3 == NULL) {
    printf ("ERROR: failed to get compositor sink pad\n");
    return -1;
  }
  GST_DEBUG ("Obtained Request pad %s for compositor.",
      gst_pad_get_name (sinkpad3));
  g_object_set (sinkpad3, "xpos", uconfig->width_in, "ypos", uconfig->height_in,
      NULL);
  if (gst_pad_link (srcpad3, sinkpad3) != GST_PAD_LINK_OK) {
    printf ("ERROR: failed to link Dec 4 with Compositor pad\n");
    gst_object_unref (sinkpad3);
    gst_object_unref (srcpad3);
  }

  if (!g_signal_connect (elms->sink, "fps-measurements",
          G_CALLBACK (fpsdisplaysink_cb), elms)) {
    printf ("ERROR: Error registring fpsdisplaysink callback\n");
    return -1;
  }

  if (elms->bin == NULL) {
    printf ("ERROR: bin not created...\n");
    return -1;
  }

  return 0;
}

void
exit_pipeline (void)
{
  g_main_loop_quit (loop);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  g_main_loop_unref (loop);
  gst_object_unref (pipeline);
}

static gboolean
my_bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  GError *err;
  gchar *debug;
  struct user_config *uconfig = (struct user_config *)data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (message, &err, &debug);
      g_error_free (err);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      g_main_loop_quit (loop);
      printf ("End Of Stream .. !!\n");
      printf ("Output is stored in the current directory (%s)\n", uconfig->outfile);
      break;
    default:
      break;
  }
  return TRUE;
};

static void usage (void)
{
  printf ("*********************************\n");
  printf ("Usage: \n");
  printf ("./vvas_xcompositor [options]\n");
  printf
      ("\nMandatory Options:\n-f <1st h264 file> -f <2nd h264 file> -f <3rd h264 file> -f <4th h264 file>\n");
  printf
      ("\nOptional Options: \n-l <png logo file>\n-b <encoder max bit rate>\n-i <device index>");
  printf
      ("\n-d <decoder start index>\n-e <encoder start index>\n-h <horizontal alignment for logo> | Range: 0 to 1");
  printf ("\n-v <vertical alignment for logo> | Range: 0 to 1");
  printf ("\n-o <output mp4 filename>");
  printf ("\n-n <Number of input buffers>\n");
  printf
      ("\nexample:\n./vvas_xcompositor -f ./video1.mp4 -f ./video2.mp4 -f ./video3.h264 -f ./video4.h265 -l ./logo.png\n");
  printf
      ("\nFor inputs to the application, each input can not be given beyond 1080p60 resolution \n");
  printf ("*********************************\n");
  exit (-1);
}

int
main (int argc, char **argv)
{
  GstStateChangeReturn sret;
  GstBus *bus;
  int opt, file_i = 0, len, i, ret, idx = 0;
  struct user_config *uconfig;
  comp_data *elms = (comp_data *) malloc (sizeof (comp_data));
  int val1 = -1, val2 = -1;

  signal_intr_id = g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, NULL);
  gst_init (NULL, NULL);

  uconfig = (struct user_config *) calloc (1, sizeof (struct user_config));
  uconfig->devidx = -1;
  uconfig->enc_max_bit = -1;
#ifndef ENABLE_XRM_SUPPORT
  uconfig->decidx = -1;
  uconfig->encidx = -1;
#endif
  uconfig->logo_x = -1;
  uconfig->logo_y = -1;
  uconfig->width_in = -1;
  uconfig->height_in = -1;
  uconfig->nbuffers = -1;

  while ((opt = getopt (argc, argv, ":l:f:x:i:b:o:d:e:h:v:n:")) != -1) {
    switch (opt) {
      case 'n':
        uconfig->nbuffers = atoi (optarg);
        if (uconfig->nbuffers < 20) {
          printf ("ERROR: Number of buffers should be minimum 20\n");
          usage ();
        }
        break;
#ifndef ENABLE_XRM_SUPPORT
      case 'd':
        uconfig->decidx = atoi (optarg);
        break;
      case 'e':
        uconfig->encidx = atoi (optarg);
        break;
#endif
      case 'v':
        uconfig->logo_y = strtof (optarg, NULL);
        break;
      case 'h':
        uconfig->logo_x = strtof (optarg, NULL);
        break;
      case 'i':
        uconfig->devidx = atoi (optarg);
        break;
      case 'b':
        uconfig->enc_max_bit = atoi (optarg);
        break;
      case 'o':
        len = strlen (optarg);
        uconfig->outfile = (char *) malloc ((len + 1) * sizeof (char));
        strcpy (uconfig->outfile, optarg);
        break;
      case 'l':
        if (access (optarg, F_OK) != 0) {
          printf ("ERROR: %s file doesn't exist\n", optarg);
          return -1;
        }
        len = strlen (optarg);
        uconfig->lpath = (char *) malloc ((len + 1) * sizeof (char));
        strcpy (uconfig->lpath, optarg);
        break;
      case 'f':
        if (file_i > 3)         /* ignore any further files */
          break;
        if (access (optarg, F_OK) != 0) {
          printf ("ERROR: %s file doesn't exist\n", optarg);
          return -1;
        }
        len = strlen (optarg);
        uconfig->fpath[file_i] = (char *) malloc ((len + 1) * sizeof (char));
        strcpy (uconfig->fpath[file_i], optarg);
        file_i++;
        break;
      case ':':
        printf ("ERROR: option needs a value\n");
        usage ();
      case '?':
        printf ("ERROR: unknown option: %c\n", optopt);
        usage ();
    }
  }

  /* Set default options */
  if (!uconfig->outfile) {
    len = strlen (DEF_OUTFILE) + 1;
    uconfig->outfile = (char *) malloc (len * sizeof (char));
    strcpy (uconfig->outfile, DEF_OUTFILE);
  }
  if (uconfig->logo_x < 0)
    uconfig->logo_x = DEF_LOGO_X;
  if (uconfig->logo_y < 0)
    uconfig->logo_y = DEF_LOGO_Y;
  if (uconfig->nbuffers == -1)
    uconfig->nbuffers = DEF_NUM_FRAMES;
#ifndef ENABLE_XRM_SUPPORT
  if (uconfig->decidx == -1)
    uconfig->decidx = DEF_DEC_IDX;
  if (uconfig->encidx == -1)
    uconfig->encidx = DEF_ENC_IDX;
#endif
  if (uconfig->devidx == -1)
    uconfig->devidx = DEF_DEV_IDX;
  if (uconfig->enc_max_bit == -1)
    uconfig->enc_max_bit = DEF_MAX_BITRATE;
  if (file_i != 4) {
    printf ("ERROR: Please provide exactly 4 files for compositioning\n");
    usage ();
  }

  pipeline = gst_pipeline_new ("mixing");
  elms->bin = (GstBin *) gst_bin_new ("mixing_bin");

  while (idx < 4) {
    get_container_and_codec_type (uconfig->fpath[idx], &val1, &val2, uconfig);
    if (val1 == -1 || val2 == -1)
      return -1;

    uconfig->codec_t[idx] = val2;
    uconfig->cont_t[idx] = val1;

    if (uconfig->cont_t[idx] == TYPE_MP4)
      ret = dec_pipe_mp4 (uconfig, elms, idx);
    else
      ret = dec_pipe (uconfig, elms, idx);
    if (ret == -1) {
      printf ("ERROR: decode bin %d not created...\n", idx);
      return ret;
    }
    idx++;
  }

  ret = comp_pipe (uconfig, elms);
  if (ret == -1) {
    printf ("ERROR: Final bin not created...\n");
    return ret;
  }
  gst_bin_add (GST_BIN (pipeline), (GstElement *) elms->bin);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, my_bus_callback, uconfig);
  gst_object_unref (bus);
  /* start the pipeline */
  sret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (sret == GST_STATE_CHANGE_FAILURE) {
    g_error ("ERROR: Could not change state in pipeline.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  /* run the pipeline */
  loop = g_main_loop_new (NULL, TRUE);
  g_main_loop_run (loop);
  exit_pipeline ();
  if (signal_intr_id > 0)
    g_source_remove (signal_intr_id);

  gst_deinit ();
  if (uconfig->lpath)
    free (uconfig->lpath);
  free (uconfig->outfile);
  for (i = 0; i < file_i; i++) {
    free (uconfig->fpath[i]);
  }
  free (uconfig);
  return 0;
}
