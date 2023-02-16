/*
 * Copyright (C) 2020 - 2022 Xilinx, Inc.  All rights reserved.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
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

/* TODO:
 * 1. XRM support
 *    a. for PPE XRM is required
 *    b. for inference we dont have control as we use Vitis AI APIs
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef XLNX_PCIe_PLATFORM
#define USE_DMABUF 0
#else /* Embedded */
#define USE_DMABUF 1
#endif

#include <gst/gst.h>
#include <gst/vvas/gstvvasallocator.h>
#include <gst/vvas/gstvvasbufferpool.h>
#include <gst/allocators/gstdmabuf.h>
#include <dlfcn.h>              /* for dlXXX APIs */
#include <sys/mman.h>           /* for munmap */
#include <jansson.h>
#include <math.h>

#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include <vvas/vvas_kernel.h>
#include "gstvvas_xinfer.h"
#include <gst/vvas/gstvvasutils.h>
#include <gst/vvas/gstinferencemeta.h>

#include <vvas_core/vvas_log.h>
#include <vvas_core/vvas_context.h>
#include <vvas_core/vvas_common.h>
#include <vvas_core/vvas_dpuinfer.hpp>
#include <vvas_core/vvas_scaler.h>
#include <vvas_core/vvas_postprocessor.hpp>
#include <gst/vvas/gstvvascoreutils.h>
#include <vvas/vvas_structure.h>

/**
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xinfer_debug"
 */
GST_DEBUG_CATEGORY_STATIC (gst_vvas_xinfer_debug);

/** @def GST_CAT_DEFAULT
 *  @brief Setting gst_vvas_xinfer_debug as default debug category for logging
 */
#define GST_CAT_DEFAULT gst_vvas_xinfer_debug

/**
 *  @brief Defines a static GstDebugCategory global variable with name
 *  GST_CAT_PERFORMANCE for performance logging purpose
 */
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

/** @def PRINT_METADATA_TREE
 *  @brief Enable prints inference metadata
 */
#define PRINT_METADATA_TREE

/** @def DUMP_INFER_INPUT
 *  @brief Dump raw data in file which is prepared for inference
 */
#undef DUMP_INFER_INPUT
//#define DUMP_INFER_INPUT

/** @def DEFAULT_INFER_LEVEL
 *  @brief Setting default inference level required for cascade use case
 */
#define DEFAULT_INFER_LEVEL 1

/** @def BATCH_SIZE_ZERO
 *  @brief Setting batch size for infrence processing
 */
#define BATCH_SIZE_ZERO 0

/** @def PPE_WIDTH_ALIGN
 *  @brief Alignment for width must be 8 * pixel per clock for PPE.
 */
#define PPE_WIDTH_ALIGN  (8 * self->priv->ppc)

/** @def PPE_WIDTH_ALIGN
 *  @brief Alignment for height for PPE.
 */
#define PPE_HEIGHT_ALIGN  1

/** @def DEFAULT_ATTACH_EMPTY_METADATA
 * @brief Default flag to enable or disable attach metadata
 */
#define DEFAULT_ATTACH_EMPTY_METADATA  TRUE

// TODO: On embedded also installation path also should go to /opt area
#ifdef XLNX_PCIe_PLATFORM
/* In PCIe platforms only we will have multiple devices.
 * In Embedded platforms, we will have single device with dev-idx = 0 */
#define DEFAULT_VVAS_LIB_PATH "/opt/xilinx/vvas/lib/"
#define DEFAULT_DEVICE_INDEX -1
#else
#define DEFAULT_VVAS_LIB_PATH "/usr/lib/"
#define DEFAULT_DEVICE_INDEX 0
#endif

#ifdef XLNX_PCIe_PLATFORM
#define DEFAULT_PPC 4
#else
#define DEFAULT_PPC 2
#endif

#define MAX_ROI 40
/** @def VVAS_SCALER_MIN_WIDTH
 *  *  @brief This is the minimum width which can be processed using MultiScaler IP
 *   */
#define VVAS_SCALER_MIN_WIDTH    16

/** @def VVAS_SCALER_MIN_HEIGHT
 *  *  @brief This is the minimum height which can be processed using MultiScaler IP
 *   */
#define VVAS_SCALER_MIN_HEIGHT   16

/** @def DEFAULT_BATCH_SUBMIT_TIMEOUT
 *  *  @brief Time to wait in milliseconds before pushing current batch
 *   */
#define DEFAULT_BATCH_SUBMIT_TIMEOUT  0

#include <vvas_core/vvas_device.h>

GQuark _scale_quark;
GQuark _copy_quark;

typedef struct _GstVvas_XInferPrivate GstVvas_XInferPrivate;
typedef struct _Vvas_XInferFrame Vvas_XInferFrame;

enum
{
  /** Signal to be emitted when acceleration library processed a frame */
  SIGNAL_VVAS,

  /* add more signal above this */
  SIGNAL_LAST
};

static guint vvas_signals[SIGNAL_LAST] = { 0 };


/** @struct Vvas_XInfer
 *  @brief  Holds members specific to VVAS kernel acceleration library
 */
typedef struct
{
  /** Name of the kernel */
  gchar *name;
  /** Array of input frame */
  VvasVideoFrame *input[MAX_NUM_OBJECT];
  /** Array of output frame */
  VvasVideoFrame *output[MAX_NUM_OBJECT];
  /** Stipulate core library is ready for processing */
  gboolean init_done;
  /** Handle to core library */
  void *handle;
} VvasCoreModule;

enum
{
  PROP_0,
  /** Property ID of the ppe kernels-config property */
  PROP_PPE_CONFIG_LOCATION,
  /** Property ID of the infer kernels-config property */
  PROP_INFER_CONFIG_LOCATION,
  /** Property ID to indicate attach empty metadata or not */
  PROP_ATTACH_EMPTY_METADATA,
  /** Property ID for timeout to submit batch */
  PROP_BATCH_SUBMIT_TIMEOUT,
};

/** @enum VvasThreadState
 *  @brief  Contains different states of kernel threads
 */
typedef enum
{
  VVAS_THREAD_NOT_CREATED,
  VVAS_THREAD_RUNNING,
  VVAS_THREAD_EXITED,
} VvasThreadState;

struct _roi
{
  uint32_t y_cord;
  uint32_t x_cord;
  uint32_t height;
  uint32_t width;
};

typedef struct _vvas_ms_roi
{
  uint32_t nobj;
  struct _roi roi[MAX_ROI];
} vvas_ms_roi;

/** @struct Vvas_XInferNodeInfo
 *  @brief  Contains video information of particular node
 */
typedef struct _vvas_xinfer_nodeinfo
{
  /** Handle to private members */
  GstVvas_XInfer *self;
  /** Video info of parent node */
  GstVideoInfo *parent_vinfo;
  /** Video info of node */
  GstVideoInfo *child_vinfo;
  /** vvas input frame roi data*/
  vvas_ms_roi input_roi;
  /** vvas output frame roi data*/
  vvas_ms_roi output_roi;
  /** flag to use roi data for update metadata*/
  gboolean use_roi_data;
} Vvas_XInferNodeInfo;

/** @struct Vvas_XInferNumSubs
 *  @brief  Contains sub buffer information
 */
typedef struct _vvas_xinfer_numsubs
{
  /** Handle to private members */
  GstVvas_XInfer *self;
  /** Sub buffer available or not */
  gboolean available;
} Vvas_XInferNumSubs;

/** @struct _Vvas_XInferFrame
 *  @brief  Contains video frame information
 */
struct _Vvas_XInferFrame
{
  /** gstreamer parent buffer */
  GstBuffer *parent_buf;
  /** parent node video info */
  GstVideoInfo *parent_vinfo;
  /** useful to avoid pushing duplicate parent buffers in inference_level > 1 */
  gboolean last_parent_buf;
  /** gstreamer child buffer */
  GstBuffer *child_buf;
  /** child node video info */
  GstVideoInfo *child_vinfo;
  /** vvas frame info */
  VvasVideoFrame *vvas_frame;
  /** Skip frame if previous parent node does not have metadata */
  gboolean skip_processing;
  /** Received events */
  GstEvent *event;
  /** vvas input frame roi data*/
  vvas_ms_roi input_roi;
  /** vvas output frame roi data*/
  vvas_ms_roi output_roi;
  /** Use input and output roi info for scale metadata*/
  gboolean use_roi_data;
};

/** @struct _GstVvas_XInferPrivate
 *  @brief  Contains private member of xinfer
 */
struct _GstVvas_XInferPrivate
{
  /*common members */
  /** Helps to return from Query, before Initialization of kernels */
  gboolean do_init;
  /** Holds Input video configuration */
  GstVideoInfo *in_vinfo;
  /** internal input buffer pool */
  GstBufferPool *input_pool;
  /** Dynamic kernel configuration in JSON object */
  json_t *dyn_json_config;
  /** pre-process HW available or not */
  gboolean do_preprocess;
  /** Stop triggered on EOS or ctrl^c */
  gboolean stop;
  /** Holds last status of pad_push, to be returned in generate_output */
  GstFlowReturn last_fret;
  /** Sets on GST_EVENT_EOS */
  gboolean is_eos;
  /** Sets on CUSTOM_PAD_EOS */
  gboolean is_pad_eos;
  /** Holds status on error conditions */
  gboolean is_error;

  /* preprocessing members */
  /** pre-process device index */
  gint ppe_dev_idx;
  /** Handle to pre-process device with dev-idx */
  vvasDeviceHandle ppe_dev_handle;
  /** Handle to hold pre-process's VVAS Acceleration Library information */
  VvasCoreModule *ppe_handle;
  /** UUID of xclbin */
  uuid_t ppe_xclbinId;
  /** To protect ppe context */
  GMutex ppe_lock;
  /** condition represents PPE has input to process */
  GCond ppe_has_input;
  /** condition represents PPE need input to process */
  GCond ppe_need_input;
  /** Holds handle to PPE thread */
  GThread *ppe_thread;
  /** Location of the xclbin to programmed on device */
  gchar *ppe_xclbin_loc;
  /** PPE video frame information */
  Vvas_XInferFrame *ppe_frame;
  /** PPE need data to process */
  gboolean ppe_need_data;
  /** Holds PPE output buffer video info, which is input to infer */
  GstVideoInfo *ppe_out_vinfo;
  /** PPE output buffer pool */
  GstBufferPool *ppe_outpool;
  /** number of bbox/sub_buffer at inference level */
  guint nframes_in_level;
  /** State of PPE thread */
  VvasThreadState ppe_thread_state;
  /** PPC requirement of scaler HW */
  gint ppc;
  /** Flag to inform if the PPE in use sofware */
  gboolean software_ppe;
  /** VVAS Core Global context */
  VvasContext *ppe_vvas_ctx;
  /** PPE input memory bank */
  gint ppe_in_mem_bank;
  /** PPE output memory bank */
  gint ppe_out_mem_bank;
  /** PPE log level */
  gint ppe_log_level;
  /** PPE output buffer queue */
  GQueue *ppe_buf_queue;
  /** roi data for pre-processing */
  vvas_ms_roi roi_data;
  /** vvas scaler parametes */
  VvasScalerParam param;
  /** PP Initial buffer value*/
  gint init_value;

  /* inference members */
  VvasContext *infer_vvas_ctx;
  /** Handle to infer's VVAS acceleration library */
  VvasCoreModule *infer_handle;
  /** Cascade level of current instance */
  gint infer_level;
  /** To protect infer context */
  GMutex infer_lock;
  /** Condition represents infer_batch_queue has sufficient buffer to process */
  GCond infer_cond;
  /** Condition represents infer_batch_queue is full */
  GCond infer_batch_full;
  /** Holds handle to infer thread */
  GThread *infer_thread;
  /** Required infer batch size */
  guint infer_batch_size;
  /** Max size of infer queue which holds inputs */
  guint max_infer_queue;
  /** Infer input frame queue */
  GQueue *infer_batch_queue;
  /** Low latency mode, where we do not wait for full batch to get full */
  gboolean low_latency_infer;
  /** Width supported by infer model */
  gint pref_infer_width;
  /** Height supported by infer model */
  gint pref_infer_height;
  /** Video format supported by model */
  GstVideoFormat pref_infer_format;
  /** Queue to holds sub buffer from previous xinfer prediction */
  GQueue *infer_sub_buffers;
  /** decides whether sub_buffers need to be attached with metadata or not */
  gboolean infer_attach_ppebuf;
  /** State of Infer thread */
  VvasThreadState infer_thread_state;
  /** Inference core Log level */
  gint infer_log_level;
  /** Model configuration */
  VvasModelConf model_conf;
  /** DPU input configuration */
  VvasDpuInferConf *dpu_conf;
#ifdef DUMP_INFER_INPUT
  /** pointer to output FILE used for dumping all input frame to infer */
  FILE *fp;
#endif
  void *dpu_kernel_config;

  /* post-processing members */
  /** post-process needed or not */
  gboolean do_postprocess;
  /** absolute path of VVAS core library */
  gchar *postproc_lib_path;
  /** post-procesing function name */
  gchar *postproc_func;
  /** file descriptor of opened postprocessing lib */
  void *postproc_lib_fd;
  /** post-processor input configuration */
  VvasPostProcessConf postproc_conf;
  /** Postprocess function pointer */
  VvasPostProcessor *postproc_handle;
  VvasPostProcessor *(*postprocess_create) (VvasPostProcessConf *,
      VvasLogLevel);
  VvasInferPrediction *(*postprocess_run) (VvasPostProcessor *,
      VvasInferPrediction *);
    VvasReturnType (*postprocess_destroy) (VvasPostProcessor *);
};

/**
 *  @var GstStaticPadTemplate sink_template
 *  @brief Contains capabilites associated with xinfer's sink pad
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{NV12, BGR, RGB}")));

/**
 *  @var GstStaticPadTemplate src_template
 *  @brief Contains capabilities associated with xinfer's src pad
 */
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{NV12, BGR, RGB}")));

#define gst_vvas_xinfer_parent_class parent_class

/** @brief  Glib's convenience macro for GstVvas_XInfer type implementation.
 *  @details This macro does below tasks:\n
 *		- Declares a class initialization function with prefix gst_vvas_xinfer
 *		- Declares an instance initialization function
 *		- A static variable named gst_vvas_xinfer_parent_class pointing to the parent class
 *		- Defines a gst_vvas_xinfer_get_type() function with below tasks
 *			- Initializes GTypeInfo function pointers
 *			- Registers created GTypeInfo with GType system as declaring parent type as
 *			  GST_TYPE_BASE_TRANSFORM
 *			- Registers GstVvas_XInferPrivate as private structure to GstVvas_XInfer type
 */
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XInfer, gst_vvas_xinfer,
    GST_TYPE_BASE_TRANSFORM);

#define GST_VVAS_XINFER_PRIVATE(self) (GstVvas_XInferPrivate *) (gst_vvas_xinfer_get_instance_private (self))

static gboolean
vvas_xinfer_prepare_ppe_output_frame (GstVvas_XInfer * self, GstBuffer * outbuf,
    GstVideoInfo * out_vinfo, VvasVideoFrame ** vvas_frame);
static void gst_vvas_xinfer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vvas_xinfer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_vvas_xinfer_finalize (GObject * obj);

/**
 *  @fn static inline GstVideoFormat get_gst_format (VVASVideoFormat kernel_fmt)
 *  @param [in] kernel_fmt - VVAS Video format
 *  @return GstVideoFormat
 *  @brief  Gets GstVideoFormat value corresponding to VVAS video format
 */
static inline GstVideoFormat
get_gst_format (VvasVideoFormat video_fmt)
{
  switch (video_fmt) {
    case VVAS_VIDEO_FORMAT_GRAY8:
      return GST_VIDEO_FORMAT_GRAY8;
    case VVAS_VIDEO_FORMAT_Y_UV8_420:
      return GST_VIDEO_FORMAT_NV12;
    case VVAS_VIDEO_FORMAT_I420:
      return GST_VIDEO_FORMAT_I420;
    case VVAS_VIDEO_FORMAT_BGR:
      return GST_VIDEO_FORMAT_BGR;
    case VVAS_VIDEO_FORMAT_RGB:
      return GST_VIDEO_FORMAT_RGB;
    case VVAS_VIDEO_FORMAT_YUY2:
      return GST_VIDEO_FORMAT_YUY2;
    case VVAS_VIDEO_FORMAT_r210:
      return GST_VIDEO_FORMAT_r210;
    case VVAS_VIDEO_FORMAT_v308:
      return GST_VIDEO_FORMAT_v308;
    case VVAS_VIDEO_FORMAT_GRAY10_LE32:
      return GST_VIDEO_FORMAT_GRAY10_LE32;
    case VVAS_VIDEO_FORMAT_BGRA:
      return GST_VIDEO_FORMAT_BGRA;
    case VVAS_VIDEO_FORMAT_RGBA:
      return GST_VIDEO_FORMAT_RGBA;
    default:
      GST_ERROR ("Not supporting kernel format %d yet", video_fmt);
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

/**
 * @fn static gboolean check_bbox_buffers_availability (GNode * node, gpointer data)
 * @param [in] node - node in a tree
 * @param [in] data - pointer to Vvas_XInferNumSubs
 * @return TRUE or FALSE
 *         The traversal can be halted by returning TRUE when suitable
 *         sub-buffer found
 *
 * @brief This function return either the sub-buffer can be used for
 *        current level of prediction
 */
static gboolean
check_bbox_buffers_availability (GNode * node, gpointer data)
{
  Vvas_XInferNumSubs *pNumSubs = (Vvas_XInferNumSubs *) data;
  GstVvas_XInfer *self = pNumSubs->self;
  GstInferencePrediction *prediction = NULL;

  /* ignore node which is not at same level of current inference */
  if (g_node_depth ((GNode *) node) != self->priv->infer_level) {
    GST_LOG_OBJECT (self, "ignoring node %p at level %d", node,
        g_node_depth ((GNode *) node));
    return FALSE;
  }
  prediction = (GstInferencePrediction *) node->data;
  if (prediction->sub_buffer) {
    GstVideoMeta *vmeta;

    if (!prediction->prediction.enabled) {
      GST_DEBUG_OBJECT (self,
          "Skipping inference on this node as it is disabled");
      return FALSE;
    }
    GST_LOG_OBJECT (self, "bbox buffer availble for node %p", node);
    vmeta = gst_buffer_get_video_meta (prediction->sub_buffer);
    if (vmeta) {
      GST_LOG_OBJECT (self, "bbox width = %d, height = %d and format = %s",
          vmeta->width, vmeta->height,
          gst_video_format_to_string (vmeta->format));
      GST_LOG_OBJECT (self,
          "infer preffed width = %d, height = %d and format = %s",
          self->priv->pref_infer_width, self->priv->pref_infer_height,
          gst_video_format_to_string (self->priv->pref_infer_format));
    }
    /* Sub-buffer can be useded for current level of inference only if
     * its height, width and format matched with preferred height, width and
     * format resp of current level inferance */
    if (vmeta && (vmeta->width == self->priv->pref_infer_width) &&
        vmeta->height == self->priv->pref_infer_height &&
        vmeta->format == self->priv->pref_infer_format) {
      pNumSubs->available = TRUE;
      return TRUE;
    }
  }
  return FALSE;
}

/**
 * @fn static gboolean prepare_inference_sub_buffers (GNode * node, gpointer data)
 * @param [in] node - node in a tree
 * @param [in] data - pointer to GstVvas_XInfer handle
 * @return TRUE when the traversal can be halted
 *         FALSE when the traversal can not be halted
 *
 * @brief This function finds either the sub-buffer can be used for
 *        current level of prediction and add them to current infer_sub_buffers
 *        queue
 */
static gboolean
prepare_inference_sub_buffers (GNode * node, gpointer data)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (data);
  GstInferencePrediction *prediction = NULL;
  GstVvas_XInferPrivate *priv = self->priv;

  /* ignore node which is not at same level of current inference */
  if (g_node_depth (node) != self->priv->infer_level) {
    GST_LOG_OBJECT (self, "ignoring node %p at level %d", node,
        g_node_depth (node));
    return FALSE;
  }

  prediction = (GstInferencePrediction *) node->data;
  if (!prediction->sub_buffer) {
    GST_LOG_OBJECT (self, "bbox buffer not availble for node %p", node);
  } else {
    GstVideoMeta *vmeta;

    if (!prediction->prediction.enabled) {
      GST_DEBUG_OBJECT (self,
          "Skipping inference on this node as it is disabled");
      return FALSE;
    }
    vmeta = gst_buffer_get_video_meta (prediction->sub_buffer);
    if (vmeta) {
      GST_LOG_OBJECT (self, "bbox width = %d, height = %d and format = %s",
          vmeta->width, vmeta->height,
          gst_video_format_to_string (vmeta->format));
      GST_LOG_OBJECT (self,
          "infer preffed width = %d, height = %d and format = %s",
          priv->pref_infer_width, priv->pref_infer_height,
          gst_video_format_to_string (priv->pref_infer_format));
    }
    /* Add sub-buffer to current infer_sub_buffers queue only if
     * its height, width and format are matching with preferred
     * height, width and format resp of current inference */
    if (vmeta && (vmeta->width == priv->pref_infer_width) &&
        vmeta->height == priv->pref_infer_height &&
        vmeta->format == priv->pref_infer_format) {
      GstInferenceMeta *sub_meta;
      GstInferencePrediction *parent_prediction =
          (GstInferencePrediction *) node->parent->data;

      GST_DEBUG_OBJECT (self, "queueing subbuffer %p", prediction->sub_buffer);
      g_queue_push_tail (self->priv->infer_sub_buffers, prediction->sub_buffer);

      sub_meta =
          ((GstInferenceMeta *) gst_buffer_get_meta (prediction->sub_buffer,
              gst_inference_meta_api_get_type ()));
      if (!sub_meta) {
        GST_LOG_OBJECT (self, "add inference metadata to %p",
            prediction->sub_buffer);
        sub_meta =
            (GstInferenceMeta *) gst_buffer_add_meta (prediction->sub_buffer,
            gst_inference_meta_get_info (), NULL);
      }

      gst_inference_prediction_unref (sub_meta->prediction);
      /* increase the ref count as sub-buffer is used by current inference */
      gst_inference_prediction_ref (parent_prediction);
      sub_meta->prediction = prediction;
    }
  }

  return FALSE;
}

#ifdef PRINT_METADATA_TREE
/**
 * @fn static gboolean printf_all_nodes (GNode * node, gpointer data)
 * @param [in] node - node in a tree
 * @param [in] data - pointer to GstVvas_XInfer handle
 * @return TRUE when the traversal can be halted
 *         FALSE when the traversal can not be halted
 *
 * @brief This function prints node
 */
static gboolean
printf_all_nodes (GNode * node, gpointer data)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (data);

  GST_LOG_OBJECT (self, "node = %p at level %d", node, g_node_depth (node));
  return FALSE;
}
#endif

/**
 * @fn static gboolean prepare_ppe_outbuf_at_level (GNode * node, gpointer data)
 * @param [in] node - node in a tree
 * @param [in] data - pointer to GstVvas_XInfer handle
 * @return TRUE when the traversal can be halted on error
 *         FALSE when the traversal can not be halted
 *
 * @brief This function create ppe output frame and populate its
 *        member with required information. The function execute
 *        for each node which is at depth equal to current
 *        inference level
 */
static gboolean
prepare_ppe_outbuf_at_level (GNode * node, gpointer data)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (data);
  GstVvas_XInferPrivate *priv = self->priv;

  GST_LOG_OBJECT (self, "node = %p at level %d", node, g_node_depth (node));

  /* Process node only if current inference level and node depth are same */
  if (g_node_depth (node) == priv->infer_level) {
    GstBuffer *outbuf;
    GstFlowReturn fret;
    VvasVideoFrame *out_vvas_frame;
    GstInferencePrediction *prediction = (GstInferencePrediction *) node->data;
    GstInferencePrediction *parent_prediction =
        (GstInferencePrediction *) node->parent->data;
    GstInferenceMeta *infer_meta;
    gboolean bret;

    if ((prediction->prediction.bbox.width < VVAS_SCALER_MIN_WIDTH)
        || (prediction->prediction.bbox.height < VVAS_SCALER_MIN_HEIGHT)) {
      GST_DEBUG_OBJECT (self,
          "Width/Height of the ROI is less the minimum supported(%dx%d), discarding",
          VVAS_SCALER_MIN_WIDTH, VVAS_SCALER_MIN_HEIGHT);
      return FALSE;
    }

    if (!prediction->prediction.enabled) {
      GST_DEBUG_OBJECT (self,
          "Skipping inference on this node as it is disabled");
      return FALSE;
    }

    GST_LOG_OBJECT (self, "found node %p at level inference level %d", node,
        priv->infer_level);

    /* acquire ppe output buffer */
    fret = gst_buffer_pool_acquire_buffer (priv->ppe_outpool, &outbuf, NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
          priv->ppe_outpool);
      priv->is_error = TRUE;
      return TRUE;
    }
    /* Prepare VvasVideoFrame from GstBuffer required by core for pre-processing */
    bret =
        vvas_xinfer_prepare_ppe_output_frame (self, outbuf, priv->ppe_out_vinfo,
        &out_vvas_frame);
    if (!bret) {
      priv->is_error = TRUE;
      return TRUE;
    }

    infer_meta =
        ((GstInferenceMeta *) gst_buffer_get_meta (outbuf,
            gst_inference_meta_api_get_type ()));
    if (!infer_meta) {
      GST_LOG_OBJECT (self, "add inference metadata to %p", outbuf);
      infer_meta =
          (GstInferenceMeta *) gst_buffer_add_meta (outbuf,
          gst_inference_meta_get_info (), NULL);
    }

    gst_inference_prediction_unref (infer_meta->prediction);
    /* Increase te ref count as it is required by ppe */
    gst_inference_prediction_ref (parent_prediction);
    infer_meta->prediction = prediction;

    if (priv->infer_attach_ppebuf)
      prediction->sub_buffer = gst_buffer_ref (outbuf);

    GST_DEBUG_OBJECT (self,
        "acquired PPE output buffer %p and %s as sub_buffer", outbuf,
        priv->infer_attach_ppebuf ? "attached" : "NOT attached");

    /* Add out_vvas_frame to array of ppe kernel output frame.
     * The ppe kenel would fill buffer in this frame with output data */
    priv->ppe_handle->output[priv->nframes_in_level] = out_vvas_frame;
    g_queue_push_tail (priv->ppe_buf_queue, outbuf);
    priv->nframes_in_level++;
  }

  return FALSE;
}

/**
 * @fn static gboolean vvas_xinfer_is_sub_buffer_useful (GstVvas_XInfer * self, GstBuffer * buf)
 * @param [in] self - handle to GstVvas_XInfer
 * @param [in] buf - input GstBuffer
 * @return TRUE when buffer parameters matched with inference requirement
 *         FALSE when buffer parameters not matched with inference requirement
 *
 * @brief This function compare the current inference parameters with input buf
 *        parameters
 */
static gboolean
vvas_xinfer_is_sub_buffer_useful (GstVvas_XInfer * self, GstBuffer * buf)
{
  GstVideoMeta *vmeta = NULL;

  if (!buf)
    return FALSE;

  vmeta = gst_buffer_get_video_meta (buf);
  if (vmeta && (vmeta->width == self->priv->pref_infer_width) &&
      vmeta->height == self->priv->pref_infer_height &&
      vmeta->format == self->priv->pref_infer_format) {
    GST_LOG_OBJECT (self,
        "buffer parameters matched with inference requirement");
    return TRUE;
  }
  return FALSE;
}

/**
 * @fn static gboolean vvas_xinfer_allocate_sink_internal_pool (GstVvas_XInfer * self)
 * @param [in] self - handle to GstVvas_XInfer
 * @return TRUE when pool created
 *         FALSE when Pool not created
 *
 * @brief This function creates the internal input pool of physical continuous
 *        buffer for xinfer. The internal input pool is created only
 *        when input to xinfer is
 *           - not vvas buffer
 *           - not dma buffer
 *           - not on same device
 *           - is not on same memory bank of same device
 */
static gboolean
vvas_xinfer_allocate_sink_internal_pool (GstVvas_XInfer * self)
{
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;
  GstVideoAlignment align;

  caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM (self)->sinkpad);

  /* get the video parameters of sink pad */
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }
  // TODO: get stride requirements from PPE
  pool = gst_vvas_buffer_pool_new (PPE_WIDTH_ALIGN, 1);
  GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

  /* Create new allocator */
  allocator = gst_vvas_allocator_new (self->priv->ppe_dev_idx,
      USE_DMABUF, self->priv->ppe_in_mem_bank);

  /* kernel need physically contiguous memory */
  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;

  config = gst_buffer_pool_get_config (pool);
  /* No max limit for allocated buffer */
  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (&info),
      3, 0);
  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  /* reset the video alignment before configuring */
  gst_video_alignment_reset (&align);

  /* Let's set our alignment info into the pool config */
  for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&info); idx++) {
    align.stride_align[idx] = (PPE_WIDTH_ALIGN - 1);
  }

  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_video_alignment (config, &align);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config on input pool");
    goto error;
  }

  if (self->priv->input_pool)
    gst_object_unref (self->priv->input_pool);

  /* Store for further reference */
  self->priv->input_pool = pool;

  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " pool", pool);
  gst_caps_unref (caps);
  /* reduce the ref count as its control taken by pool */
  if (allocator)
    gst_object_unref (allocator);

  return TRUE;

error:
  gst_caps_unref (caps);
  return FALSE;
}

/**
 * @fn static gboolean vvas_xinfer_copy_input_buffer (GstVvas_XInfer * self, GstBuffer * inbuf,
 *						      GstBuffer ** internal_inbuf)
 * @param [in] self - handle to GstVvas_XInfer
 * @param [in] inbuf - input buffer on sink pad of xinfer
 * @param [out] internal_inbuf - new buffer based on kernel requirement
 * @return TRUE on success
 *         FALSE on failure
 *
 * @brief This function copy the input buffer to internal input buffer of
 *        required video parameter of kernel
 *        Internal input pool of buffer is also created if it is not already
 *        available
 */
static gboolean
vvas_xinfer_copy_input_buffer (GstVvas_XInfer * self, GstBuffer * inbuf,
    GstBuffer ** internal_inbuf)
{
  GstBuffer *new_inbuf;
  GstFlowReturn fret;
  GstVideoFrame in_vframe, new_vframe;
  gboolean bret;

  memset (&in_vframe, 0x0, sizeof (GstVideoFrame));
  memset (&new_vframe, 0x0, sizeof (GstVideoFrame));

  if (!self->priv->input_pool) {
    /* allocate input internal pool */
    bret = vvas_xinfer_allocate_sink_internal_pool (self);
    if (!bret)
      goto error;

    if (!gst_buffer_pool_is_active (self->priv->input_pool)) {
      gst_buffer_pool_set_active (self->priv->input_pool, TRUE);
    }
  }

  /* acquire buffer from own input pool */
  fret =
      gst_buffer_pool_acquire_buffer (self->priv->input_pool, &new_inbuf, NULL);
  if (fret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
        self->priv->input_pool);
    goto error;
  }
  GST_LOG_OBJECT (self, "acquired buffer %p from own pool", new_inbuf);

  /* map internal buffer in write mode */
  if (!gst_video_frame_map (&new_vframe, self->priv->in_vinfo, new_inbuf,
          GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "failed to map internal input buffer");
    goto error;
  }

  /* map input buffer in read mode */
  if (!gst_video_frame_map (&in_vframe, self->priv->in_vinfo, inbuf,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "failed to map input buffer");
    goto error;
  }
  GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, self,
      "slow copy to internal input pool buffer");

  /* frame_copy will take care of stride too */
  gst_video_frame_copy (&new_vframe, &in_vframe);
  gst_video_frame_unmap (&in_vframe);
  gst_video_frame_unmap (&new_vframe);
  gst_buffer_copy_into (new_inbuf, inbuf,
      (GstBufferCopyFlags) GST_BUFFER_COPY_METADATA, 0, -1);
  *internal_inbuf = new_inbuf;

  return TRUE;

error:
  if (in_vframe.data[0]) {
    gst_video_frame_unmap (&in_vframe);
  }
  if (new_vframe.data[0]) {
    gst_video_frame_unmap (&new_vframe);
  }
  return FALSE;
}

static void
free_dpuinfer_conf (VvasDpuInferConf * dpu_conf)
{
  if (dpu_conf->model_path)
    g_free (dpu_conf->model_path);
  if (dpu_conf->model_name)
    g_free (dpu_conf->model_name);
  if (dpu_conf->modelclass)
    g_free (dpu_conf->modelclass);
  for (int i = 0; i < dpu_conf->num_filter_labels; i++) {
    if (dpu_conf->filter_labels[i])
      g_free (dpu_conf->filter_labels[i]);
  }
  if (dpu_conf->filter_labels)
    free (dpu_conf->filter_labels);

  free (dpu_conf);
}

/**
 * @fn static gboolean vvas_xinfer_ppe_init (GstVvas_XInfer * self)
 * @param [in] self - Handle to GstVvas_XInfer
 * @return TRUE on success
 *         FALSE on failure
 *
 * @brief Initialize the PPE private parameters
 * @detail This function open xrt context and call kenrel init func and
 *	  populate vvas_handle for ppe kernel.
 *        The function also download xclbin if it is not already downloaded.
 *        Pre-process kernel init should be called after infer kernel init
 *        as pre-process parameters depends on infer parameters
 *
 */
static gboolean
vvas_xinfer_ppe_init (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  VvasScaler *ppe_handle = NULL;
  VvasScalerProp scaler_prop = { 0 };
  VvasReturnType vret;

  VvasLogLevel core_log_level =
      vvas_get_core_log_level (gst_debug_category_get_threshold
      (gst_vvas_xinfer_debug));
  priv->ppe_vvas_ctx =
      vvas_context_create (priv->ppe_dev_idx,
      priv->ppe_xclbin_loc, core_log_level, &vret);
  if (!priv->ppe_vvas_ctx) {
    GST_ERROR_OBJECT (self, "Couldn't create VVAS context");
    return FALSE;
  }
  priv->ppe_dev_handle = priv->ppe_vvas_ctx->dev_handle;

  ppe_handle =
      vvas_scaler_create (priv->ppe_vvas_ctx,
      (const char *) priv->ppe_handle->name,
      (VvasLogLevel) priv->ppe_log_level);
  if (!ppe_handle) {
    GST_ERROR_OBJECT (self, "Couldn't create Scaler");
    return FALSE;
  }
  priv->ppe_handle->handle = ppe_handle;

  vret = vvas_scaler_prop_get (ppe_handle, &scaler_prop);
  if (VVAS_IS_ERROR (vret)) {
    GST_ERROR_OBJECT (self, "Couldn't get scaler props");
    return FALSE;
  }

  scaler_prop.ppc = priv->ppc;
  scaler_prop.mem_bank = priv->ppe_in_mem_bank;
  vret = vvas_scaler_prop_set (ppe_handle, &scaler_prop);
  if (VVAS_IS_ERROR (vret)) {
    GST_ERROR_OBJECT (self, "Couldn't set scaler props");
    return FALSE;
  }

  /* Hardware PPE is used, return NULL pointer upstream */
  if (priv->dpu_kernel_config) {
    vvas_structure_free ((VvasStructure *) priv->dpu_kernel_config);
    priv->dpu_kernel_config = NULL;
  }

  GST_INFO_OBJECT (self, "completed preprocess init");

  priv->ppe_frame = g_slice_new0 (Vvas_XInferFrame);
  /* wait on event ppe_need_input or ppe_has_input as per ppe_need_data */
  priv->ppe_need_data = TRUE;
  priv->ppe_out_vinfo = gst_video_info_new ();
  priv->ppe_buf_queue = g_queue_new ();

  g_mutex_init (&priv->ppe_lock);
  g_cond_init (&priv->ppe_has_input);
  g_cond_init (&priv->ppe_need_input);

  return TRUE;
}

/**
 * @fn static gboolean vvas_xinfer_postproc_init (GstVvas_XInfer * self)
 * @param [in] self - handle to GstVvas_XInfer
 * @return TRUE on success
 *         FALSE on failure
 *
 * @brief Initialize the infer private parameters
 * @detail This function loads the postprocess library and populate the
 *         function address of symbols
 *
 */
static gboolean
vvas_xinfer_postproc_init (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;

  priv->postproc_lib_fd = dlopen (priv->postproc_lib_path, RTLD_LAZY);
  if (!priv->postproc_lib_fd) {
    GST_ERROR_OBJECT (self, " unable to open shared library %s",
        priv->postproc_lib_path);
    return FALSE;
  }

  GST_INFO_OBJECT (self,
      "opened post-processing library %s successfully with fd %p",
      priv->postproc_lib_path, priv->postproc_lib_fd);

  /* Clear any existing error */
  dlerror ();

  priv->postprocess_create =
      dlsym (priv->postproc_lib_fd, "vvas_postprocess_create");
  if (!priv->postprocess_create) {
    GST_ERROR_OBJECT (self,
        "could not find vvas_postprocess_create function. reason : %s",
        dlerror ());
    return FALSE;
  }

  priv->postprocess_run = dlsym (priv->postproc_lib_fd, priv->postproc_func);
  if (!priv->postprocess_run) {
    GST_ERROR_OBJECT (self,
        "could not find %s function. reason : %s",
        priv->postproc_func, dlerror ());
    return FALSE;
  }

  priv->postprocess_destroy =
      dlsym (priv->postproc_lib_fd, "vvas_postprocess_destroy");
  if (!priv->postprocess_destroy) {
    GST_ERROR_OBJECT (self,
        "could not find vvas_postprocess_destroy function. reason : %s",
        dlerror ());
    return FALSE;
  }

  priv->postproc_handle =
      priv->postprocess_create (&priv->postproc_conf, priv->infer_log_level);
  if (!priv->postproc_handle) {
    GST_ERROR_OBJECT (self, "could not intialise post-processing library");
    return FALSE;
  }
  return TRUE;
}

/**
 * @fn static gboolean vvas_xinfer_infer_init (GstVvas_XInfer * self)
 * @param [in] self - handle to GstVvas_XInfer
 * @return TRUE on success
 *         FALSE on failure
 *
 * @brief Initialize the infer private parameters
 * @detail This function calls kenrel init func and populate vvas_handle for infer kernel
 *
 */
static gboolean
vvas_xinfer_infer_init (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  VvasReturnType vret;
  VvasDpuInfer *infer_handle = NULL;

  VvasLogLevel core_log_level =
      vvas_get_core_log_level (gst_debug_category_get_threshold
      (gst_vvas_xinfer_debug));

  priv->infer_vvas_ctx = vvas_context_create (-1, NULL, core_log_level, &vret);
  if (!priv->infer_vvas_ctx) {
    GST_ERROR_OBJECT (self, "Couldn't create VVAS context");
    return FALSE;
  }

  infer_handle = vvas_dpuinfer_create (priv->dpu_conf, priv->infer_log_level);
  if (!infer_handle) {
    GST_ERROR_OBJECT (self, "failed to do inference init..");
    return FALSE;
  }
  priv->infer_handle->handle = infer_handle;
  GST_INFO_OBJECT (self, "completed inference kernel init");

  vret = vvas_dpuinfer_get_config (infer_handle, &priv->model_conf);
  if (vret == VVAS_RET_SUCCESS) {
    GST_DEBUG_OBJECT (self, "Got DPU kernel configuration");
  } else {
    GST_ERROR_OBJECT (self, "couldn't get DPU kernel configuration");
    return FALSE;
  }

  priv->dpu_kernel_config = vvas_structure_new ("pp_config",
      "alpha_r", G_TYPE_FLOAT, priv->model_conf.mean_r,
      "alpha_g", G_TYPE_FLOAT, priv->model_conf.mean_g,
      "alpha_b", G_TYPE_FLOAT, priv->model_conf.mean_b,
      "beta_r", G_TYPE_FLOAT, priv->model_conf.scale_r,
      "beta_g", G_TYPE_FLOAT, priv->model_conf.scale_g,
      "beta_b", G_TYPE_FLOAT, priv->model_conf.scale_b, NULL);

  g_mutex_init (&priv->infer_lock);
  g_cond_init (&priv->infer_cond);
  g_cond_init (&priv->infer_batch_full);

  priv->infer_batch_queue = g_queue_new ();
  priv->infer_sub_buffers = g_queue_new ();

  if (priv->infer_batch_size == BATCH_SIZE_ZERO ||
      priv->infer_batch_size > priv->model_conf.batch_size) {
    GST_WARNING_OBJECT (self, "infer_batch_size (%d) can't be zero"
        "or greater than model supported batch size."
        "taking batch-size %d",
        priv->infer_batch_size, priv->model_conf.batch_size);
    priv->infer_batch_size = priv->model_conf.batch_size;
  }

  if (priv->max_infer_queue < priv->infer_batch_size) {
    GST_WARNING_OBJECT (self, "inference-max-queue can't be less than "
        "batch-size. taking batch-size %d as default queue length",
        priv->infer_batch_size);
    priv->max_infer_queue = priv->infer_batch_size;
  }
  return TRUE;
}

/**
 * @fn static gboolean vvas_xinfer_read_ppe_config (GstVvas_XInfer * self)
 * @param [in] self - Handle to GstVvas_XInfer
 * @return TRUE when reads all mandatory parameter from json
 *         FALSE when not able to read mandatory parameter from json
 *
 * @brief This function reads ppe json file and populate infer private
 *        parameters
 *
 */
static gboolean
vvas_xinfer_read_ppe_config (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  json_t *root = NULL, *kernel, *config, *value;
  json_error_t error;

  /* get root json object */
  root = json_load_file (self->ppe_json_file, JSON_DECODE_ANY, &error);
  if (!root) {
    GST_ERROR_OBJECT (self, "failed to load json file. reason %s", error.text);

    /* print to console */
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to load json file. reason %s", error.text), (NULL));
    return FALSE;
  }

  /* get xclbin location */
  // TODO: xclbin-loc is optional as XRM might have downloaded
  value = json_object_get (root, "xclbin-location");
  if (json_is_string (value)) {
    priv->ppe_xclbin_loc = g_strdup (json_string_value (value));
    GST_INFO_OBJECT (self, "xclbin location to download %s",
        priv->ppe_xclbin_loc);
  } else {
    priv->ppe_xclbin_loc = NULL;
    GST_INFO_OBJECT (self, "xclbin path is not set");
  }

  value = json_object_get (root, "software-ppe");
  if (value) {
    if (!json_is_boolean (value)) {
      GST_ERROR_OBJECT (self, "software-ppe is not a boolean type");
      goto error;
    } else {
      priv->software_ppe = json_boolean_value (value);
    }
  } else {
    /* If not mentioned, then the ppe used will be accelerated IP */
    priv->software_ppe = FALSE;
  }

#if defined(XLNX_PCIe_PLATFORM)
  value = json_object_get (root, "device-index");
  if (!json_is_integer (value)) {
    GST_ERROR_OBJECT (self,
        "device-index is not set in preprocess json file %s",
        self->ppe_json_file);
    goto error;
  }
  priv->ppe_dev_idx = json_integer_value (value);
  GST_INFO_OBJECT (self, "preprocess device index %d", priv->ppe_dev_idx);
#endif

  /* get kernels object */
  kernel = json_object_get (root, "kernel");
  if (!json_is_object (kernel)) {
    GST_ERROR_OBJECT (self, "failed to find kernel object");
    goto error;
  }

  priv->ppe_handle = (VvasCoreModule *) calloc (1, sizeof (VvasCoreModule));
  if (!priv->ppe_handle) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  /* get kernel name */
  value = json_object_get (kernel, "kernel-name");
  if (value) {
    if (!json_is_string (value)) {
      GST_ERROR_OBJECT (self, "primary kernel name is not of string type");
      goto error;
    }
    priv->ppe_handle->name = g_strdup (json_string_value (value));
  } else {
    priv->ppe_handle->name = NULL;
  }
  GST_INFO_OBJECT (self, "Primary kernel name %s", priv->ppe_handle->name);

  /* get vvas kernel lib internal configuration */
  config = json_object_get (kernel, "config");
  if (!json_is_object (config)) {
    GST_ERROR_OBJECT (self, "config is not of object type");
    goto error;
  }

  value = json_object_get (config, "ppc");
  if (!value || !json_is_integer (value)) {
    priv->ppc = DEFAULT_PPC;
    GST_DEBUG_OBJECT (self, "PPC not set. taking default : %d", priv->ppc);
  } else {
    priv->ppc = json_integer_value (value);
    GST_INFO_OBJECT (self, "PPC : %d", priv->ppc);
  }

  value = json_object_get (config, "in-mem-bank");
  if (!value || !json_is_integer (value)) {
    priv->ppe_in_mem_bank = DEFAULT_MEM_BANK;
    GST_DEBUG_OBJECT (self, "PPE In mem bank not set. taking default : %d",
        priv->ppe_in_mem_bank);
  } else {
    priv->ppe_in_mem_bank = json_integer_value (value);
    GST_INFO_OBJECT (self, "In mem bank : %d", priv->ppe_in_mem_bank);
  }

  value = json_object_get (config, "out-mem-bank");
  if (!value || !json_is_integer (value)) {
    priv->ppe_out_mem_bank = DEFAULT_MEM_BANK;
    GST_DEBUG_OBJECT (self, "PPE out mem bank not set. taking default : %d",
        priv->ppe_out_mem_bank);
  } else {
    priv->ppe_out_mem_bank = json_integer_value (value);
    GST_INFO_OBJECT (self, "out mem bank : %d", priv->ppe_out_mem_bank);
  }

  value = json_object_get (config, "debug-level");
  if (!value || !json_is_integer (value)) {
    VvasLogLevel core_log_level =
        vvas_get_core_log_level (gst_debug_category_get_threshold
        (gst_vvas_xinfer_debug));
    priv->ppe_log_level = core_log_level;
  } else {
    priv->ppe_log_level = json_integer_value (value);
  }

  value = json_object_get (root, "scaler-type");
  if (!value || !json_is_string (value)) {
    priv->param.type = VVAS_SCALER_DEFAULT;
    GST_DEBUG_OBJECT (self, "Scaler type is not set. taking default : %d",
        priv->param.type);
  } else {
    gchar *scaler_type = g_strdup (json_string_value (value));
    if (g_str_equal (scaler_type, "letterbox"))
      priv->param.type = VVAS_SCALER_LETTERBOX;
    else if (g_str_equal (scaler_type, "envelope_cropped"))
      priv->param.type = VVAS_SCALER_ENVELOPE_CROPPED;
    else {
      priv->param.type = VVAS_SCALER_DEFAULT;
      GST_DEBUG_OBJECT (self, "Scaler type is not valid. taking default : %d",
          priv->param.type);
    }
    g_free (scaler_type);
    GST_INFO_OBJECT (self, "Scaler type : %d", priv->param.type);
  }

  value = json_object_get (root, "scaler-horz-align");
  if (!value || !json_is_string (value)) {
    priv->param.horz_align = VVAS_SCALER_HORZ_ALIGN_LEFT;
    GST_DEBUG_OBJECT (self,
        "Scaler Horizontal Alignment is not set. taking default : %d",
        priv->param.horz_align);
  } else {
    gchar *horz_align = g_strdup (json_string_value (value));
    if (g_str_equal (horz_align, "left"))
      priv->param.horz_align = VVAS_SCALER_HORZ_ALIGN_LEFT;
    else if (g_str_equal (horz_align, "right"))
      priv->param.horz_align = VVAS_SCALER_HORZ_ALIGN_RIGHT;
    else if (g_str_equal (horz_align, "center"))
      priv->param.horz_align = VVAS_SCALER_HORZ_ALIGN_CENTER;
    else {
      priv->param.horz_align = VVAS_SCALER_HORZ_ALIGN_LEFT;
      GST_DEBUG_OBJECT (self,
          "Scaler Horizontal Alignment is not valid. taking default : %d",
          priv->param.horz_align);
    }
    g_free (horz_align);
    GST_INFO_OBJECT (self, "Scaler Horizontal Alignment : %d",
        priv->param.horz_align);
  }

  value = json_object_get (root, "scaler-vert-align");
  if (!value || !json_is_string (value)) {
    priv->param.vert_align = VVAS_SCALER_VERT_ALIGN_TOP;
    GST_DEBUG_OBJECT (self,
        "Scaler Vertical Alignment is not set. taking default : %d",
        priv->param.vert_align);
  } else {
    gchar *vert_align = g_strdup (json_string_value (value));
    if (g_str_equal (vert_align, "top"))
      priv->param.vert_align = VVAS_SCALER_VERT_ALIGN_TOP;
    else if (g_str_equal (vert_align, "bottom"))
      priv->param.vert_align = VVAS_SCALER_VERT_ALIGN_BOTTOM;
    else if (g_str_equal (vert_align, "center"))
      priv->param.vert_align = VVAS_SCALER_VERT_ALIGN_CENTER;
    else {
      priv->param.vert_align = VVAS_SCALER_VERT_ALIGN_TOP;
      GST_DEBUG_OBJECT (self,
          "Scaler Vertical Alignment is not valid. taking default : %d",
          priv->param.vert_align);
    }
    g_free (vert_align);
    GST_INFO_OBJECT (self, "Scaler Vertical Alignment : %d",
        priv->param.vert_align);
  }

  value = json_object_get (root, "scaler-pad-value");
  if (!value || !json_is_integer (value)) {
    priv->init_value = 0;
    GST_DEBUG_OBJECT (self, "Scaler Pad Value not set. taking default : %d",
        priv->init_value);
  } else {
    priv->init_value = json_integer_value (value);
    GST_INFO_OBJECT (self, "Scaler Pad Value : %d", priv->init_value);
  }

  GST_DEBUG_OBJECT (self, "preprocess kernel config size = %lu",
      json_object_size (value));

  if (root)
    json_decref (root);
  return TRUE;

error:
  /* print to console */
  GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
      ("%s file parse error", self->ppe_json_file), (NULL));
  if (root)
    json_decref (root);
  return FALSE;
}

static VvasVideoFormat
get_vvas_video_fmt (char *name)
{
  if (!strncmp (name, "RGB", 3))
    return VVAS_VIDEO_FORMAT_RGB;
  else if (!strncmp (name, "BGR", 3))
    return VVAS_VIDEO_FORMAT_BGR;
  else if (!strncmp (name, "GRAY8", 5))
    return VVAS_VIDEO_FORMAT_GRAY8;
  else
    return VVAS_VMFT_UNKNOWN;
}

/**
 * @fn static gboolean vvas_xinfer_read_infer_config (GstVvas_XInfer * self)
 * @param [in] self - Handle to GstVvas_XInfer
 * @return TRUE when reads all mandatory parameter from json
 *         FALSE when not able to read mandatory parameter from json
 *
 * @brief This function reads infer json file and populate infer private
 *        parameters
 *
 */
static gboolean
vvas_xinfer_read_infer_config (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  json_t *root = NULL, *kernel, *value, *config, *label;
  json_error_t error;
  VvasDpuInferConf *dpu_conf;

  /* Set default values for non mandatory parameter */
  priv->infer_level = DEFAULT_INFER_LEVEL;
  priv->infer_batch_size = BATCH_SIZE_ZERO;
  priv->low_latency_infer = TRUE;
  priv->infer_attach_ppebuf = FALSE;

  /* get root json object */
  root = json_load_file (self->infer_json_file, JSON_DECODE_ANY, &error);
  if (!root) {
    GST_ERROR_OBJECT (self, "failed to load json file. reason %s", error.text);

    /* print to console */
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to load json file. reason %s", error.text), (NULL));
    return FALSE;
  }


  /* get kernels object */
  kernel = json_object_get (root, "kernel");
  if (!json_is_object (kernel)) {
    GST_ERROR_OBJECT (self, "failed to find kernel object");
    goto error;
  }

  priv->infer_handle = (VvasCoreModule *) calloc (1, sizeof (VvasCoreModule));
  if (!priv->infer_handle) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  /* get vvas kernel lib internal configuration */
  config = json_object_get (kernel, "config");
  if (!json_is_object (config)) {
    GST_ERROR_OBJECT (self, "config is not of object type");
    goto error;
  }

  GST_DEBUG_OBJECT (self, "kernel config size = %lu",
      json_object_size (config));

  value = json_object_get (config, "batch-size");
  if (json_is_integer (value)) {
    priv->infer_batch_size = json_integer_value (value);
    if (priv->infer_batch_size >= MAX_NUM_OBJECT) {
      GST_ERROR_OBJECT (self, "batch-size should not >= %d",
          MAX_NUM_OBJECT - 1);
      goto error;
    }
  }

  value = json_object_get (root, "inference-level");
  if (json_is_integer (value)) {
    priv->infer_level = json_integer_value (value);
    if (priv->infer_level < 1) {
      GST_ERROR_OBJECT (self,
          "inference-level %d can't be less than 1", priv->infer_level);
      goto error;
    }
  }

  /* making batch size as default max-queue-size */
  priv->max_infer_queue = priv->infer_batch_size;

  value = json_object_get (root, "low-latency");
  if (value) {
    if (!json_is_boolean (value)) {
      GST_ERROR_OBJECT (self, "low-latency is not a boolean type");
      goto error;
    }

    priv->low_latency_infer = json_boolean_value (value);
    GST_INFO_OBJECT (self, "setting low-latency to %d",
        priv->low_latency_infer);
  }

  value = json_object_get (root, "inference-max-queue");
  if (!json_is_integer (value)) {
    GST_WARNING_OBJECT (self, "inference-max-queue is not set."
        "taking batch-size %d as default", priv->infer_batch_size);
  } else {
    priv->max_infer_queue = json_integer_value (value);
    if (priv->max_infer_queue < priv->infer_batch_size) {
      GST_WARNING_OBJECT (self, "inference-max-queue can't be less than "
          "batch-size. taking batch-size %d as default queue length",
          priv->infer_batch_size);
      priv->max_infer_queue = priv->infer_batch_size;
    } else {
      GST_INFO_OBJECT (self, "setting inference-max-queue to %d",
          priv->max_infer_queue);
    }
  }

  value = json_object_get (root, "attach-ppe-outbuf");
  if (value) {
    if (!json_is_boolean (value)) {
      GST_ERROR_OBJECT (self, "attach-ppe-outbuf is not a boolean type");
      goto error;
    }

    priv->infer_attach_ppebuf = json_boolean_value (value);
    GST_INFO_OBJECT (self, "setting attach-ppe-outbuf to %d",
        priv->infer_attach_ppebuf);
  }

  /* Fill VvasDpuInferConf here to call core API's */
  priv->dpu_conf = (VvasDpuInferConf *) calloc (1, sizeof (VvasDpuInferConf));
  dpu_conf = priv->dpu_conf;
  if (!dpu_conf) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  value = json_object_get (config, "model-path");
  if (json_is_string (value)) {
    dpu_conf->model_path = g_strdup ((char *) json_string_value (value));
    priv->postproc_conf.model_path = dpu_conf->model_path;
    GST_DEBUG_OBJECT (self, "model-path is %s",
        (char *) json_string_value (value));
  } else {
    if (!value) {
      GST_ERROR_OBJECT (self, "model-path missing");
    } else {
      GST_ERROR_OBJECT (self, "model-path is not of string type");
    }
    goto error;
  }

  value = json_object_get (config, "model-name");
  if (json_is_string (value)) {
    dpu_conf->model_name = g_strdup ((char *) json_string_value (value));
    priv->postproc_conf.model_name = dpu_conf->model_name;
    GST_DEBUG_OBJECT (self, "model-name is %s",
        (char *) json_string_value (value));
  } else {
    if (!value) {
      GST_ERROR_OBJECT (self, "model-name missing");
    } else {
      GST_ERROR_OBJECT (self, "model-name is not of string type");
    }
    goto error;
  }

  value = json_object_get (config, "model-format");
  if (!json_is_string (value)) {
    GST_WARNING_OBJECT (self,
        "model-format is not proper, taking BGR as default");
    dpu_conf->model_format = VVAS_VIDEO_FORMAT_BGR;
  } else {
    dpu_conf->model_format =
        get_vvas_video_fmt ((char *) json_string_value (value));
    GST_DEBUG_OBJECT (self, "model-format %s",
        (char *) json_string_value (value));
  }
  if (dpu_conf->model_format == VVAS_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "SORRY NOT SUPPORTED MODEL FORMAT %s",
        (char *) json_string_value (value));
    goto error;
  }

  value = json_object_get (config, "model-class");
  if (json_is_string (value)) {
    dpu_conf->modelclass = g_strdup ((char *) json_string_value (value));
    GST_DEBUG_OBJECT (self, "model-class is %s",
        (char *) json_string_value (value));
  } else {
    if (!value) {
      GST_ERROR_OBJECT (self, "model-format missing");
    } else {
      GST_ERROR_OBJECT (self, "model-format is not of string type");
    }
    goto error;
  }

  value = json_object_get (config, "vitis-ai-preprocess");
  if (!value || !json_is_boolean (value)) {
    if (!self->ppe_json_file) {
      GST_DEBUG_OBJECT (self, "Setting need_preprocess as TRUE");
      dpu_conf->need_preprocess = true;
    } else {
      dpu_conf->need_preprocess = false;
      GST_DEBUG_OBJECT (self, "Setting need_preprocess as FALSE");
    }
  } else {
    if (!self->ppe_json_file) {
      dpu_conf->need_preprocess = json_boolean_value (value);
    } else {
      /** Prefer hardware pre-processing if ppe config provided */
      dpu_conf->need_preprocess = false;
    }
  }

  if (dpu_conf->need_preprocess
      && dpu_conf->model_format != VVAS_VIDEO_FORMAT_BGR) {
    GST_ERROR_OBJECT (self,
        "Vitis-AI preprocess expect input as a BGR format. "
        "Set model_foramt as BGR in infer config json file.");
    goto error;
  }

  value = json_object_get (config, "performance-test");
  if (!value || !json_is_boolean (value)) {
    GST_DEBUG_OBJECT (self, "Setting performance_test as FALSE");
    dpu_conf->performance_test = false;
  } else {
    dpu_conf->performance_test = json_boolean_value (value);
  }

  value = json_object_get (config, "max-objects");
  if (!value || !json_is_integer (value)) {
    GST_DEBUG_OBJECT (self, "Setting max-objects as %d", UINT_MAX);
    dpu_conf->objs_detection_max = UINT_MAX;
  } else {
    dpu_conf->objs_detection_max = json_integer_value (value);
    GST_DEBUG_OBJECT (self, "Setting max-objects as %d",
        dpu_conf->objs_detection_max);
  }

  value = json_object_get (config, "float-feature");
  if (!value || !json_is_boolean (value)) {
    GST_DEBUG_OBJECT (self, "Setting float_feature as FALSE");
    dpu_conf->float_feature = false;
  } else {
    dpu_conf->float_feature = json_boolean_value (value);
  }

  value = json_object_get (config, "seg-out-format");
  if (json_is_string (value)) {
    dpu_conf->segoutfmt =
        get_vvas_video_fmt ((char *) json_string_value (value));
    GST_DEBUG_OBJECT (self, "seg out format %s",
        (char *) json_string_value (value));
  } else {
    dpu_conf->segoutfmt = VVAS_VIDEO_FORMAT_UNKNOWN;
  }

  value = json_object_get (config, "segoutfactor");
  if (!value || !json_is_integer (value)) {
    if (dpu_conf->segoutfmt) {
      dpu_conf->segoutfactor = 1;
      GST_DEBUG_OBJECT (self, "Setting segoutfactor as 1");
    }
  } else {
    GST_DEBUG_OBJECT (self, "Setting segoutfactor as %d",
        dpu_conf->segoutfactor);
    dpu_conf->segoutfactor = json_integer_value (value);;
  }

  value = json_object_get (config, "filter-labels");
  if (json_is_array (value)) {
    dpu_conf->num_filter_labels = json_array_size (value);
    dpu_conf->filter_labels =
        (char **) calloc (dpu_conf->num_filter_labels, sizeof (char *));
    for (int i = 0; i < dpu_conf->num_filter_labels; i++) {
      label = json_array_get (value, i);
      if (json_is_string (label)) {
        dpu_conf->filter_labels[i] =
            g_strdup ((char *) json_string_value (label));
        GST_DEBUG_OBJECT (self, "Adding filter label %s",
            dpu_conf->filter_labels[i]);
      } else {
        dpu_conf->filter_labels[i] = NULL;
        GST_DEBUG_OBJECT (self, "Filter label %d is not of string type", i + 1);
      }
    }
  } else {
    dpu_conf->num_filter_labels = 0;
    GST_DEBUG_OBJECT (self, "No filter labels given");
  }

  value = json_object_get (config, "debug-level");
  if (json_is_integer (value)) {
    priv->infer_log_level = json_integer_value (value);
  } else {
    priv->infer_log_level = 1;
  }

  priv->do_postprocess = FALSE;
  value = json_object_get (config, "postprocess-lib-path");
  if (json_is_string (value)) {
    if (!g_strcmp0 (dpu_conf->modelclass, "RAWTENSOR")) {
      priv->postproc_lib_path = g_strdup (json_string_value (value));
      GST_DEBUG_OBJECT (self, "post-processing library %s",
          (char *) json_string_value (value));
      priv->do_postprocess = TRUE;
    }
  }

  if (priv->do_postprocess) {
    value = json_object_get (config, "postprocess-function");
    if (json_is_string (value)) {
      priv->postproc_func = g_strdup (json_string_value (value));
      GST_DEBUG_OBJECT (self, "post-processing function %s",
          (char *) json_string_value (value));
    }
  }

  dpu_conf->batch_size = priv->infer_batch_size;
  GST_INFO_OBJECT (self, "inference-level = %d and batch-size = %d",
      priv->infer_level, priv->infer_batch_size);

  if (root)
    json_decref (root);

  return TRUE;

error:
  /* print to console */
  GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
      ("%s file parse error", self->infer_json_file), (NULL));
  if (root)
    json_decref (root);
  if (priv->dpu_conf) {
    free_dpuinfer_conf (priv->dpu_conf);
    priv->dpu_conf = NULL;
  }
  return FALSE;
}

/**
 * @fn static gboolean vvas_xinfer_ppe_deinit (GstVvas_XInfer * self)
 * @param [in] self - Handle to GstVvas_XInfer
 * @return TRUE on success
 *         FALSE on failure
 *
 * @brief De-Initialize the PPE private parameters
 * @detail This function calls kernel deinit function and
 *        also close the xrt context. Deallocation of different
 *        memory is also part of this function.
 *
 */
static gboolean
vvas_xinfer_ppe_deinit (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  VvasCoreModule *ppe_handle = priv->ppe_handle;
  VvasReturnType vret;

  if (ppe_handle) {
    if (ppe_handle->handle) {
      vret = vvas_scaler_destroy (ppe_handle->handle);
      if (vret != VVAS_RET_SUCCESS) {
        GST_ERROR_OBJECT (self, "failed to do preprocess deinit..");
      }
      GST_DEBUG_OBJECT (self, "successfully completed preprocess deinit");
    }
    if (priv->ppe_buf_queue) {
      g_queue_free (priv->ppe_buf_queue);
    }

    g_free (ppe_handle->name);
    free (priv->ppe_handle);
    priv->ppe_handle = NULL;
  }

  if (priv->ppe_frame)
    g_slice_free1 (sizeof (Vvas_XInferFrame), priv->ppe_frame);

  g_mutex_clear (&self->priv->ppe_lock);
  g_cond_clear (&self->priv->ppe_has_input);
  g_cond_clear (&self->priv->ppe_need_input);

  if (priv->ppe_outpool && gst_buffer_pool_is_active (priv->ppe_outpool)) {
    if (!gst_buffer_pool_set_active (priv->ppe_outpool, FALSE)) {
      GST_ERROR_OBJECT (self, "failed to deactivate preprocess output pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to deactivate pool."),
          ("failed to deactivate preprocess output pool"));
      return FALSE;
    }
  }

  if (priv->input_pool && gst_buffer_pool_is_active (priv->input_pool)) {
    if (!gst_buffer_pool_set_active (priv->input_pool, FALSE)) {
      GST_ERROR_OBJECT (self, "failed to deactivate PPE internal input pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to deactivate pool."),
          ("failed to deactivate PPE internal input pool"));
      return FALSE;
    }
  }

  if (priv->ppe_outpool) {
    gst_object_unref (priv->ppe_outpool);
    priv->ppe_outpool = NULL;
  }
  if (priv->ppe_out_vinfo) {
    gst_video_info_free (priv->ppe_out_vinfo);
    priv->ppe_out_vinfo = NULL;
  }

  if (priv->ppe_xclbin_loc)
    free (priv->ppe_xclbin_loc);

  /* Destroy VVAS Context */
  if (self->priv->ppe_vvas_ctx) {
    vvas_context_destroy (self->priv->ppe_vvas_ctx);
    self->priv->ppe_vvas_ctx = NULL;
  }

  return TRUE;
}

/**
 * @fn static gboolean vvas_xinfer_postproc_deinit (GstVvas_XInfer * self)
 * @param [in] self - Handle to GstVvas_XInfer
 * @return TRUE on success
 *         FALSE on failure
 *
 * @brief De-Initialize the post process private parameters
 * @detail Deallocation of different memory is part of this function.
 *
 */
static gboolean
vvas_xinfer_postproc_deinit (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  VvasReturnType vret;

  if (priv->postproc_lib_path)
    g_free (priv->postproc_lib_path);

  if (priv->postproc_func)
    g_free (priv->postproc_func);

  if (priv->postproc_handle) {
    vret = priv->postprocess_destroy (priv->postproc_handle);
    if (vret != VVAS_RET_SUCCESS) {
      GST_ERROR_OBJECT (self, "failed to deinitialise post-processing library");
      goto error;
    }
  }

  if (priv->postproc_lib_fd)
    dlclose (priv->postproc_lib_fd);

  return TRUE;

error:
  if (priv->postproc_lib_fd)
    dlclose (priv->postproc_lib_fd);

  return FALSE;
}

/**
 * @fn static gboolean vvas_xinfer_infer_deinit (GstVvas_XInfer * self)
 * @param [in] self - Handle to GstVvas_XInfer
 * @return TRUE on success
 *         FALSE on failure
 *
 * @brief This function calls kernel deinit function and
 *        deallocation of different memory is also part of this function.
 *
 */
static gboolean
vvas_xinfer_infer_deinit (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  VvasCoreModule *infer_handle = priv->infer_handle;
  VvasReturnType vret;

  if (infer_handle) {
    if (infer_handle->handle) {
      vret = vvas_dpuinfer_destroy (infer_handle->handle);
      if (vret != VVAS_RET_SUCCESS) {
        GST_ERROR_OBJECT (self, "failed to do inference kernel deinit..");
      }
      GST_DEBUG_OBJECT (self, "successfully completed inference deinit");
    }

    if (priv->dpu_conf) {
      free_dpuinfer_conf (priv->dpu_conf);
      priv->dpu_conf = NULL;
    }

    free (infer_handle);
    priv->infer_handle = NULL;
  }

  if (priv->dpu_kernel_config) {
    vvas_structure_free ((VvasStructure *) priv->dpu_kernel_config);
  }

  g_mutex_lock (&priv->infer_lock);

  /* free all frames inside infer_batch_queue */
  if (priv->infer_batch_queue) {
    GST_INFO_OBJECT (self, "free infer batch queue of size %d",
        g_queue_get_length (priv->infer_batch_queue));

    while (!g_queue_is_empty (priv->infer_batch_queue)) {
      Vvas_XInferFrame *frame = g_queue_pop_head (priv->infer_batch_queue);

      if (frame->last_parent_buf)
        gst_buffer_unref (frame->parent_buf);

      if (frame->parent_vinfo)
        gst_video_info_free (frame->parent_vinfo);

      if (priv->infer_level > 1) {
        if (frame->child_buf) {
          GstInferenceMeta *child_meta = NULL;
          /* Clear the prediction of child buf */
          child_meta =
              (GstInferenceMeta *) gst_buffer_get_meta (frame->child_buf,
              gst_inference_meta_api_get_type ());
          child_meta->prediction = gst_inference_prediction_new ();
        }
      }

      if (frame->child_buf)
        gst_buffer_unref (frame->child_buf);

      if (frame->child_vinfo)
        gst_video_info_free (frame->child_vinfo);
      if (frame->vvas_frame) {
        vvas_video_frame_free (frame->vvas_frame);
      }
      if (frame->event)
        gst_event_unref (frame->event);

      g_slice_free1 (sizeof (Vvas_XInferFrame), frame);
    }
  }
  g_mutex_unlock (&priv->infer_lock);

  g_mutex_clear (&priv->infer_lock);
  g_cond_clear (&priv->infer_cond);

  if (priv->infer_batch_queue) {
    g_queue_free (priv->infer_batch_queue);
  }

  /* buf inside infer_sub_buffers get freed when prediction
   * node get freed, so here just remove the queue */
  if (priv->infer_sub_buffers) {
    g_queue_free (priv->infer_sub_buffers);
  }

  /* Destroy VVAS Context */
  if (self->priv->infer_vvas_ctx) {
    vvas_context_destroy (self->priv->infer_vvas_ctx);
    self->priv->infer_vvas_ctx = NULL;
  }
  return TRUE;
}

/**
 * @fn static gboolean vvas_xinfer_prepare_ppe_input_frame (GstVvas_XInfer * self, GstBuffer * inbuf,
 *                                                          GstVideoInfo * in_vinfo, GstBuffer ** new_inbuf,
 *                                                          VvasVideoFrame ** vvas_frame)
 * @param [in] self - handle to GstVvas_XInfer
 * @param [in] inbuf - input GstBuffer
 * @param [in] in_vinfo - input video info
 * @param [out] new_inbuf - new buf if it is copied to internal buffer else NULL
 * @param [inout] vvas_frame - populated values from buffer and video info
 *
 * @return TRUE on success
 *         FALSE on failure
 *
 * @brief Prepare ppe frame from input buffer and input video info
 * @detail This function populate the vvas frame with required values extracted
 *	  from input buffer and video info. The function also copies the data
 *	  from inbuf to new internal buffer and return new buffer in new_inbuf
 *	  only if inbuf is
 *           - not vvas buffer
 *           - not dma buffer
 *           - not on same device
 *           - is not on same memory bank of same device
 *
 */
static gboolean
vvas_xinfer_prepare_ppe_input_frame (GstVvas_XInfer * self, GstBuffer * inbuf,
    GstVideoInfo * in_vinfo, GstBuffer ** new_inbuf,
    VvasVideoFrame ** vvas_frame)
{
  GstVvas_XInferPrivate *priv = self->priv;
  guint64 phy_addr = 0;
  vvasBOHandle bo_handle = NULL;
  gboolean free_bo = FALSE;
  gboolean bret = FALSE;
  GstMemory *in_mem = NULL;
  GstMapFlags map_flags;

  if (!priv->software_ppe) {
    in_mem = gst_buffer_get_memory (inbuf, 0);
    if (in_mem == NULL) {
      GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
      goto error;
    }

    /* prepare HW input buffer to send it to preprocess */
    if (gst_is_vvas_memory (in_mem) &&
        gst_vvas_memory_can_avoid_copy (in_mem, priv->ppe_dev_idx,
            priv->ppe_in_mem_bank)) {
      phy_addr = gst_vvas_allocator_get_paddr (in_mem);
      bo_handle = gst_vvas_allocator_get_bo (in_mem);
    } else if (gst_is_dmabuf_memory (in_mem)) {
      gint dma_fd = -1;

      dma_fd = gst_dmabuf_memory_get_fd (in_mem);
      if (dma_fd < 0) {
        GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
        goto error;
      }

      /* dmabuf but not from vvas allocator */
      bo_handle = vvas_xrt_import_bo (priv->ppe_dev_handle, dma_fd);
      if (bo_handle == NULL) {
        GST_WARNING_OBJECT (self,
            "failed to get XRT BO...fall back to copy input");
      }
      /* Lets free the bo_handle after sub bo creation */
      free_bo = TRUE;

      GST_DEBUG_OBJECT (self, "received dma fd %d and its xrt BO = %p", dma_fd,
          bo_handle);

      phy_addr = vvas_xrt_get_bo_phy_addres (bo_handle);
    }

    /* not vvas or dma buffer */
    if (!phy_addr) {
      GST_DEBUG_OBJECT (self,
          "could not get phy_addr, copy input buffer to internal pool buffer");
      bret = vvas_xinfer_copy_input_buffer (self, inbuf, new_inbuf);
      if (!bret)
        goto error;

      gst_memory_unref (in_mem);
      in_mem = gst_buffer_get_memory (*new_inbuf, 0);
      if (in_mem == NULL) {
        GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
        goto error;
      }

      phy_addr = gst_vvas_allocator_get_paddr (in_mem);
      if (!phy_addr) {
        GST_ERROR_OBJECT (self, "failed to get physical address");
        goto error;
      }
      bo_handle = gst_vvas_allocator_get_bo (in_mem);
      inbuf = *new_inbuf;
    }
    /* syncs data when VVAS_SYNC_TO_DEVICE flag is enabled */
    bret = gst_vvas_memory_sync_bo (in_mem);
    if (!bret)
      goto error;

    GST_LOG_OBJECT (self, "input paddr %p", (void *) phy_addr);

    gst_memory_unref (in_mem);
    in_mem = NULL;

    if (free_bo && bo_handle) {
      vvas_xrt_free_bo (bo_handle);
    }

    map_flags = GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF;
    *vvas_frame =
        vvas_videoframe_from_gstbuffer (priv->ppe_vvas_ctx,
        priv->ppe_in_mem_bank, inbuf, in_vinfo, map_flags);
  } else {
    map_flags = GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF;
    *vvas_frame =
        vvas_videoframe_from_gstbuffer (priv->ppe_vvas_ctx, -1, inbuf, in_vinfo,
        map_flags);
  }

  GST_LOG_OBJECT (self, "successfully prepared ppe input vvas frame");
  return TRUE;

error:
  if (in_mem)
    gst_memory_unref (in_mem);

  return FALSE;
}

/**
 * @fn static gboolean vvas_xinfer_prepare_infer_input_frame (GstVvas_XInfer * self, GstBuffer * inbuf,
 *							      GstVideoInfo * in_vinfo, VvasVideoFrame * vvas_frame)
 * @param [in] self - handle to GstVvas_XInfer
 * @param [in] inbuf - input GstBuffer
 * @param [in] in_vinfo - input video info
 * @param [out] vvas_frame - populated values from buffer and video info
 *
 * @return TRUE on success
 *         FALSE on failure
 *
 * @brief Prepare infer frame from input buffer and video info
 * @detail This function populate the vvas frame with required values extracted
 *	  from input buffer and video info. As infer kernel copies the data to
 *	  its tensor from virtual address, so this function also populate vaddr of frame
 *
 */
static gboolean
vvas_xinfer_prepare_infer_input_frame (GstVvas_XInfer * self, GstBuffer * inbuf,
    GstVideoInfo * in_vinfo, VvasVideoFrame ** vvas_frame)
{
  GstMapFlags map_flags;
  GstVvas_XInferPrivate *priv = self->priv;

  map_flags = GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF;
  *vvas_frame =
      vvas_videoframe_from_gstbuffer (priv->infer_vvas_ctx, -1, inbuf, in_vinfo,
      map_flags);

#ifdef DUMP_INFER_INPUT
  {
    int ret;
    VvasReturnType vret;
    char str[100];
    VvasVideoFrameMapInfo map_info = { 0 };

    vret = vvas_video_frame_map (*vvas_frame, VVAS_DATA_MAP_READ, &map_info);

    sprintf (str, "inferinput_%d_%dx%d.bgr",
        self->priv->infer_level, map_info.width, map_info.height);

    if (self->priv->fp == NULL) {
      self->priv->fp = fopen (str, "w+");
      printf ("opened %s\n", str);
    }

    ret =
        fwrite (map_info.planes[0].data, 1,
        map_info.width * map_info.height * 3, self->priv->fp);

    if (self->priv->fp) {
      fclose (self->priv->fp);
      self->priv->fp = NULL;
    }
    printf ("written %s infer input frame size = %d  %dx%d\n", str, ret,
        map_info.width, map_info.height);

    vret = vvas_video_frame_unmap (*vvas_frame, &map_info);
  }
#endif

  GST_LOG_OBJECT (self, "successfully prepared inference input vvas frame");
  return TRUE;

}

static int
xlnx_multiscaler_descriptor_create (GstVvas_XInfer * self,
    VvasVideoFrame * input[MAX_NUM_OBJECT],
    VvasVideoFrame * output[MAX_NUM_OBJECT], vvas_ms_roi roi_data)
{
  guint chan_id;
  GstVvas_XInferPrivate *priv = self->priv;
  VvasReturnType vret;
  VvasScalerRect src_rect = { 0 };
  VvasScalerRect dst_rect = { 0 };
  VvasScalerPpe ppe = { 0 };
  VvasVideoInfo out_vinfo = { 0 };
  VvasScalerParam param = { 0 };

  GST_DEBUG_OBJECT (self,
      "Creating descriptor for %d scaling tasks", roi_data.nobj);

  for (chan_id = 0; chan_id < roi_data.nobj; chan_id++) {

    vvas_video_frame_get_videoinfo (output[chan_id], &out_vinfo);

    src_rect.x = roi_data.roi[chan_id].x_cord;
    src_rect.y = roi_data.roi[chan_id].y_cord;
    src_rect.width = roi_data.roi[chan_id].width;
    src_rect.height = roi_data.roi[chan_id].height;
    src_rect.frame = input[0];

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = out_vinfo.width;
    dst_rect.height = out_vinfo.height;
    dst_rect.frame = output[chan_id];

    /* Add pre-processing parameters */
    ppe.mean_r = priv->model_conf.mean_r;
    ppe.mean_g = priv->model_conf.mean_g;
    ppe.mean_b = priv->model_conf.mean_b;
    ppe.scale_r = priv->model_conf.scale_r;
    ppe.scale_g = priv->model_conf.scale_g;
    ppe.scale_b = priv->model_conf.scale_b;

    param = priv->param;

    if (!priv->dpu_conf->need_preprocess) {
      /* Add the channel for scaler */
      vret =
          vvas_scaler_channel_add (priv->ppe_handle->handle, &src_rect,
          &dst_rect, &ppe, &param);
    } else {
      /* Add the channel for scaler */
      vret =
          vvas_scaler_channel_add (priv->ppe_handle->handle, &src_rect,
          &dst_rect, NULL, &param);
    }

    priv->ppe_frame->input_roi.nobj = 1;
    priv->ppe_frame->output_roi.nobj = 1;
    priv->ppe_frame->input_roi.roi[0].width = (uint32_t) src_rect.width;
    priv->ppe_frame->input_roi.roi[0].height = (uint32_t) src_rect.height;
    priv->ppe_frame->input_roi.roi[0].x_cord = (uint32_t) src_rect.x;
    priv->ppe_frame->input_roi.roi[0].y_cord = (uint32_t) src_rect.y;
    priv->ppe_frame->output_roi.roi[0].width = (uint32_t) dst_rect.width;
    priv->ppe_frame->output_roi.roi[0].height = (uint32_t) dst_rect.height;
    priv->ppe_frame->output_roi.roi[0].x_cord = (uint32_t) dst_rect.x;
    priv->ppe_frame->output_roi.roi[0].y_cord = (uint32_t) dst_rect.y;

    if (VVAS_IS_ERROR (vret)) {
      GST_ERROR_OBJECT (self, "failed to add processing channel in scaler");
      return 0;
    }

    GST_DEBUG_OBJECT (self, "In width %u In Height %u", src_rect.width,
        src_rect.height);
  }

  return 1;
}

static gboolean
preprocessor_node_foreach (GNode * node, gpointer ptr)
{
  GstVvas_XInfer *self = (GstVvas_XInfer *) ptr;
  GstVvas_XInferPrivate *priv = self->priv;

  if (g_node_depth (node) == priv->infer_level) {
    GstInferencePrediction *pred = (GstInferencePrediction *) node->data;

    if (priv->roi_data.nobj == MAX_ROI) {
      GST_WARNING_OBJECT (self, "reached max ROI "
          "supported by preprocessor i.e. %d", MAX_ROI);
      return TRUE;
    }

    GST_DEBUG_OBJECT (self, "Got node %p at level %d", node, priv->infer_level);
    if ((pred->prediction.bbox.width < VVAS_SCALER_MIN_WIDTH)
        || (pred->prediction.bbox.height < VVAS_SCALER_MIN_HEIGHT)) {
      GST_DEBUG_OBJECT (self,
          "Width/Height of the ROI is less the minimum supported(%dx%d), discarding",
          VVAS_SCALER_MIN_WIDTH, VVAS_SCALER_MIN_HEIGHT);
      return FALSE;
    }

    if (!pred->prediction.enabled) {
      return FALSE;
    }

    priv->roi_data.roi[priv->roi_data.nobj].x_cord = pred->prediction.bbox.x;
    priv->roi_data.roi[priv->roi_data.nobj].y_cord = pred->prediction.bbox.y;
    priv->roi_data.roi[priv->roi_data.nobj].width = pred->prediction.bbox.width;
    priv->roi_data.roi[priv->roi_data.nobj].height =
        pred->prediction.bbox.height;

    GST_DEBUG_OBJECT (self,
        "bbox : x = %d, y = %d, width = %d, height = %d",
        pred->prediction.bbox.x, pred->prediction.bbox.y,
        pred->prediction.bbox.width, pred->prediction.bbox.height);

    priv->roi_data.nobj++;
  }

  return FALSE;
}

static int32_t
xlnx_ppe_start (GstVvas_XInfer * self, VvasVideoFrame * input[MAX_NUM_OBJECT],
    VvasVideoFrame * output[MAX_NUM_OBJECT], GstBuffer * inbuf)
{
  int ret;
  VvasReturnType vret;
  GstInferenceMeta *vvas_meta = NULL;
  GstVvas_XInferPrivate *priv = self->priv;
  GNode *root;

  priv->roi_data.nobj = 0;

  vvas_meta = ((GstInferenceMeta *) gst_buffer_get_meta ((GstBuffer *)
          inbuf, gst_inference_meta_api_get_type ()));

  if (priv->infer_level != 1) {
    if (!vvas_meta) {
      GST_ERROR_OBJECT (self,
          "metadata not available to extract bbox co-ordinates");
      return 0;
    }

    root = (GNode *) vvas_meta->prediction->prediction.node;
    g_node_traverse ((GNode *) root, G_PRE_ORDER, G_TRAVERSE_ALL,
        priv->infer_level, preprocessor_node_foreach, (gpointer) self);
  } else {
    /* Input Params : in level-1 scale up/down entire frames */

    VvasVideoInfo vinfo;
    vvas_video_frame_get_videoinfo (input[0], &vinfo);
    priv->roi_data.roi[priv->roi_data.nobj].x_cord = 0;
    priv->roi_data.roi[priv->roi_data.nobj].y_cord = 0;
    priv->roi_data.roi[priv->roi_data.nobj].width = vinfo.width;
    priv->roi_data.roi[priv->roi_data.nobj].height = vinfo.height;
    priv->roi_data.nobj = 1;
  }

  /* set descriptor */
  ret =
      xlnx_multiscaler_descriptor_create (self, input, output, priv->roi_data);
  if (!ret) {
    return ret;
  }

  vret = vvas_scaler_process_frame (priv->ppe_handle->handle);
  if (VVAS_IS_ERROR (vret)) {
    GST_ERROR_OBJECT (self, "Failed to process frame in scaler");
    return 0;
  }
  return 1;
}

/**
 * @fn static gboolean vvas_xinfer_prepare_ppe_output_frame (GstVvas_XInfer * self, GstBuffer * outbuf,
 *							     GstVideoInfo * out_vinfo, VvasVideoFrame ** vvas_frame)
 * @param [in] self - Handle to GstVvas_XInfer
 * @param [in] outbuf - Gstreamer buffer from out pool
 * @param [in] out_vinfo - Required output video information
 * @param [out] vvas_frame - Handle to VvasVideoFrame
 * @return TRUE on success
 *         FALSE - GstMemory or GstVideoMeta is not available
 *
 * @brief This function populate the vvas_frame with required information extracted
 *        from outbuf and out_vinfo
 */
static gboolean
vvas_xinfer_prepare_ppe_output_frame (GstVvas_XInfer * self, GstBuffer * outbuf,
    GstVideoInfo * out_vinfo, VvasVideoFrame ** vvas_frame)
{
  GstMemory *out_mem = NULL;
  GstMapFlags map_flags;
  GstVvas_XInferPrivate *priv = self->priv;

  if (!priv->software_ppe) {
    out_mem = gst_buffer_get_memory (outbuf, 0);
    if (out_mem == NULL) {
      GST_ERROR_OBJECT (self, "failed to get memory from output buffer");
      goto error;
    }

    map_flags = GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF;
    *vvas_frame =
        vvas_videoframe_from_gstbuffer (priv->ppe_vvas_ctx,
        priv->ppe_out_mem_bank, outbuf, out_vinfo, map_flags);
    /* setting SYNC_FROM_DEVICE here to avoid work by kernel lib */
    gst_vvas_memory_set_sync_flag (out_mem, VVAS_SYNC_FROM_DEVICE);
    gst_memory_unref (out_mem);
    GST_LOG_OBJECT (self, "successfully prepared output vvas frame");
  } else {
    map_flags = GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF;
    *vvas_frame =
        vvas_videoframe_from_gstbuffer (priv->ppe_vvas_ctx, -1, outbuf,
        out_vinfo, map_flags);
  }
  return TRUE;

error:
  if (out_mem)
    gst_memory_unref (out_mem);

  return FALSE;
}

/**
 * @fn static gpointer vvas_xinfer_ppe_loop (gpointer data)
 * @param [in] data - Handle to GstVvas_XInfer
 * @return NULL when thread exit normally
 *
 * @brief The function to execute as the PPE thread
 * @detail The function receive the PPE input frame and do
 *         1. At level == 1
 *            Prepare output frame and send for pre-procssing
 *         2. At level > 1
 *             a. Skip frame if no prediction in previous inference level
 *             b. IF sub buffer from level 1 present and can be used, then
 *                add them to infer_batch_queue to be process by infer
 *             c. Otherwise create ppe output frame of each node at current level
 *                and send for ppe processing.
 */
static gpointer
vvas_xinfer_ppe_loop (gpointer data)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (data);
  GstVvas_XInferPrivate *priv = self->priv;
  VvasCoreModule *ppe_handle = priv->ppe_handle;

  /* Update state of PPE thread to running */
  priv->ppe_thread_state = VVAS_THREAD_RUNNING;

  /* Loop thread unless stop called on EOS or ERROR or ctrl^c */
  while (!priv->stop) {
    int ret;
    GstFlowReturn fret = GST_FLOW_OK;
    gboolean bret;
    GstInferenceMeta *parent_meta = NULL;
    guint out_frames_count = 0; /* no of output expected from PPE */
    guint oidx;
    gboolean do_ppe = FALSE;

    g_mutex_lock (&priv->ppe_lock);
    if (priv->ppe_need_data && !priv->stop) {
      /* wait for input to PPE */
      g_cond_wait (&priv->ppe_has_input, &priv->ppe_lock);
    }
    g_mutex_unlock (&priv->ppe_lock);

    if (priv->stop)
      goto exit;

    /* here ppe receive a frame */

    /* on EOS, send same event to Infer thread */
    if (priv->ppe_frame->event &&
        GST_EVENT_TYPE (priv->ppe_frame->event) == GST_EVENT_EOS) {
      Vvas_XInferFrame *event_frame;

      event_frame = g_slice_new0 (Vvas_XInferFrame);
      event_frame->event = priv->ppe_frame->event;
      event_frame->skip_processing = TRUE;

      g_mutex_lock (&priv->infer_lock);
      g_queue_push_tail (priv->infer_batch_queue, event_frame);
      g_mutex_unlock (&priv->infer_lock);
      GST_INFO_OBJECT (self, "received EOS event, push frame %p and exit",
          event_frame);
      goto exit;
    }

    /* PPE get metadata at level > 1 */
    parent_meta = (GstInferenceMeta *) gst_buffer_get_meta
        (priv->ppe_frame->parent_buf, gst_inference_meta_api_get_type ());

    if (parent_meta && parent_meta->prediction
        && !parent_meta->prediction->prediction.enabled) {
      /** Skipping this frame as root node has enabled = FALSE */
      GST_DEBUG_OBJECT (self, "Skipping inference on this frame");
      goto skipframe;
    }

    if (priv->infer_level == 1) {
      GstBuffer *child_buf = NULL;
      GstVideoInfo *child_vinfo = NULL;

      if (parent_meta &&
          vvas_xinfer_is_sub_buffer_useful (self,
              parent_meta->prediction->sub_buffer)) {
        GstVideoMeta *vmeta = NULL;
        Vvas_XInferFrame *infer_frame;
        VvasVideoFrame *vvas_frame;

        child_buf = gst_buffer_ref (parent_meta->prediction->sub_buffer);
        child_vinfo = gst_video_info_new ();
        vmeta = gst_buffer_get_video_meta (child_buf);
        gst_video_info_set_format (child_vinfo, vmeta->format,
            vmeta->width, vmeta->height);
        /* use sub_buffer directly in inference stage. No need of PPE */
        out_frames_count = 0;

        infer_frame = g_slice_new0 (Vvas_XInferFrame);

        bret = vvas_xinfer_prepare_infer_input_frame (self, child_buf,
            child_vinfo, &vvas_frame);
        if (!bret) {
          GST_ERROR_OBJECT (self, "Failed to prepare infer input frame");
          goto error;
        }

        infer_frame->parent_buf = priv->ppe_frame->parent_buf;
        infer_frame->parent_vinfo =
            gst_video_info_copy (priv->ppe_frame->parent_vinfo);
        infer_frame->last_parent_buf = TRUE;
        infer_frame->vvas_frame = vvas_frame;
        infer_frame->child_buf = child_buf;
        infer_frame->child_vinfo = gst_video_info_copy (child_vinfo);
        infer_frame->skip_processing = FALSE;
        infer_frame->use_roi_data = FALSE;

        g_mutex_lock (&priv->infer_lock);
        g_queue_push_tail (priv->infer_batch_queue, infer_frame);
        g_mutex_unlock (&priv->infer_lock);

        gst_video_info_free (child_vinfo);
        do_ppe = FALSE;

      } else {
        GstBuffer *outbuf;
        VvasVideoFrame *out_vvas_frame;

        /* first level of inference will be on full frame */
        out_frames_count = 1;
        /* acquire ppe output buffer */
        fret =
            gst_buffer_pool_acquire_buffer (priv->ppe_outpool, &outbuf, NULL);
        if (fret != GST_FLOW_OK) {
          GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
              priv->ppe_outpool);
          goto error;
        }

        GST_LOG_OBJECT (self, "acquired PPE output buffer %p", outbuf);

        if (parent_meta) {      /* level-1 has metadata already,
                                   make current inference as siblings */
          GstInferenceMeta *out_meta = NULL;

          out_meta =
              ((GstInferenceMeta *) gst_buffer_get_meta (outbuf,
                  gst_inference_meta_api_get_type ()));
          if (!out_meta) {
            GST_LOG_OBJECT (self, "add inference metadata to %p", outbuf);
            out_meta =
                (GstInferenceMeta *) gst_buffer_add_meta (outbuf,
                gst_inference_meta_get_info (), NULL);
          }

          gst_inference_prediction_unref (out_meta->prediction);
          gst_inference_prediction_ref (parent_meta->prediction);
          out_meta->prediction = parent_meta->prediction;
        }

        /* Preprocessing required, prepare output frame for ppe */
        bret =
            vvas_xinfer_prepare_ppe_output_frame (self, outbuf,
            priv->ppe_out_vinfo, &out_vvas_frame);
        if (!bret)
          goto error;

        /* one output expected from ppe */
        priv->ppe_handle->output[0] = out_vvas_frame;
        g_queue_push_tail (priv->ppe_buf_queue, outbuf);
        do_ppe = TRUE;
      }
    } else {                    /* level > 1 */
      if (parent_meta) {
        guint sub_bufs_len = 0;

        GST_LOG_OBJECT (self, "infer_level = %d & meta data depth = %d",
            priv->infer_level,
            g_node_max_height ((GNode *) parent_meta->prediction->
                prediction.node));

        if (priv->infer_level >
            g_node_max_height ((GNode *) parent_meta->prediction->
                prediction.node)) {
          goto skipframe;
        }

        /* Check either sub buffer is useful */
        if (priv->infer_level <=
            g_node_max_height ((GNode *) parent_meta->prediction->
                prediction.node)) {
          g_node_traverse ((GNode *) parent_meta->prediction->prediction.node,
              G_PRE_ORDER, G_TRAVERSE_ALL, -1, prepare_inference_sub_buffers,
              self);
        }

        sub_bufs_len = g_queue_get_length (priv->infer_sub_buffers);

        if (sub_bufs_len > 0) {
          GstVideoInfo *child_vinfo = NULL;
          GstBuffer *child_buf = NULL;

          /* use sub_buffer directly in inference stage. No need of PPE */
          out_frames_count = 0;

          child_vinfo = gst_video_info_new ();

          GST_DEBUG_OBJECT (self,
              "input buffer %p has %u in inference level %u",
              priv->ppe_frame->parent_buf, sub_bufs_len, priv->infer_level);

          for (oidx = 0; oidx < sub_bufs_len; oidx++) {
            GstVideoMeta *vmeta;
            Vvas_XInferFrame *infer_frame;
            VvasVideoFrame *vvas_frame;

            child_buf =
                gst_buffer_ref (g_queue_pop_head (priv->infer_sub_buffers));
            vmeta = gst_buffer_get_video_meta (child_buf);
            gst_video_info_set_format (child_vinfo, vmeta->format,
                vmeta->width, vmeta->height);

            infer_frame = g_slice_new0 (Vvas_XInferFrame);

            bret = vvas_xinfer_prepare_infer_input_frame (self, child_buf,
                child_vinfo, &vvas_frame);
            if (!bret) {
              GST_ERROR_OBJECT (self, "Failed to prepare infer input frame");
              goto error;
            }

            infer_frame->parent_buf = priv->ppe_frame->parent_buf;
            infer_frame->parent_vinfo =
                gst_video_info_copy (priv->ppe_frame->parent_vinfo);
            infer_frame->last_parent_buf =
                (oidx == (sub_bufs_len - 1)) ? TRUE : FALSE;
            infer_frame->vvas_frame = vvas_frame;
            infer_frame->child_buf = child_buf;
            infer_frame->child_vinfo = gst_video_info_copy (child_vinfo);
            infer_frame->skip_processing = FALSE;
            infer_frame->use_roi_data = FALSE;

            g_mutex_lock (&priv->infer_lock);
            /* add frame for infer processing */
            g_queue_push_tail (priv->infer_batch_queue, infer_frame);
            g_mutex_unlock (&priv->infer_lock);
          }
          gst_video_info_free (child_vinfo);
          do_ppe = FALSE;
        } else {
          priv->nframes_in_level = 0;

          /* ppe_kernel->output array will be filled on node traversal */
          g_node_traverse ((GNode *) parent_meta->prediction->prediction.node,
              G_PRE_ORDER, G_TRAVERSE_ALL, priv->infer_level,
              prepare_ppe_outbuf_at_level, self);
          if (priv->is_error)
            goto error;

          GST_DEBUG_OBJECT (self, "number of nodes at level-%d = %d",
              priv->infer_level, priv->nframes_in_level);

          out_frames_count = priv->nframes_in_level;
          if (!out_frames_count)
            goto skipframe;
          do_ppe = TRUE;
        }
      } else {
        /* no level-1 inference available, skip this frame */
        Vvas_XInferFrame *infer_frame;

      skipframe:
        out_frames_count = 0;
        do_ppe = FALSE;

        infer_frame = g_slice_new0 (Vvas_XInferFrame);
        infer_frame->parent_buf = priv->ppe_frame->parent_buf;
        infer_frame->parent_vinfo =
            gst_video_info_copy (priv->ppe_frame->parent_vinfo);
        infer_frame->last_parent_buf = TRUE;
        infer_frame->vvas_frame = NULL;
        infer_frame->child_buf = NULL;
        infer_frame->child_vinfo = NULL;
        infer_frame->skip_processing = TRUE;
        infer_frame->use_roi_data = FALSE;

        GST_DEBUG_OBJECT (self, "skipping buffer %p in ppe & inference stage",
            infer_frame->parent_buf);
        g_mutex_lock (&priv->infer_lock);
        if (priv->stop) {
          g_slice_free1 (sizeof (Vvas_XInferFrame), infer_frame);
          g_mutex_unlock (&priv->infer_lock);
          goto exit;
        }
        /* send input frame to inference thread */
        g_queue_push_tail (priv->infer_batch_queue, infer_frame);

        if (priv->infer_batch_size ==
            g_queue_get_length (priv->infer_batch_queue)) {
          g_cond_signal (&priv->infer_cond);
        }
        g_mutex_unlock (&priv->infer_lock);
      }
    }

    if (do_ppe) {
      /* Add parent frame as input */
      priv->ppe_handle->input[0] = priv->ppe_frame->vvas_frame;

      /* Run PPE */
      ret =
          xlnx_ppe_start (self, priv->ppe_handle->input,
          priv->ppe_handle->output, priv->ppe_frame->child_buf);
      if (!ret) {
        GST_ERROR_OBJECT (self, "kernel start failed");
        goto error;
      }

      GST_DEBUG_OBJECT (self, "completed preprocessing of %d output frames",
          out_frames_count);

      for (oidx = 0; oidx < out_frames_count; oidx++) {
        Vvas_XInferFrame *infer_frame;
        GstBuffer *inbuf;
        /* input to inference stage */
        VvasVideoFrame *in_vvas_frame = priv->ppe_handle->output[oidx];

        infer_frame = g_slice_new0 (Vvas_XInferFrame);
        inbuf = g_queue_pop_head (priv->ppe_buf_queue);

        /* output of PPE will be input of inference stage */
        /* vvas_xinfer_prepare_infer_input_frame */
        /* not needed as we have VvasVideoFrame from PPE */

        infer_frame->parent_buf = priv->ppe_frame->parent_buf;
        infer_frame->parent_vinfo =
            gst_video_info_copy (priv->ppe_frame->parent_vinfo);
        infer_frame->last_parent_buf =
            (oidx == (out_frames_count - 1)) ? TRUE : FALSE;
        infer_frame->vvas_frame = in_vvas_frame;
        infer_frame->child_buf = inbuf;
        infer_frame->child_vinfo = gst_video_info_copy (priv->ppe_out_vinfo);
        infer_frame->skip_processing = FALSE;
        infer_frame->input_roi = priv->ppe_frame->input_roi;
        infer_frame->output_roi = priv->ppe_frame->output_roi;
        infer_frame->use_roi_data = TRUE;

        /* send input frame to inference thread */
        g_mutex_lock (&priv->infer_lock);
        GST_LOG_OBJECT (self, "pushing child_buf %p in infer_frame %p to queue",
            infer_frame->child_buf, infer_frame);
        g_queue_push_tail (priv->infer_batch_queue, infer_frame);
        g_mutex_unlock (&priv->infer_lock);
      }
    }

    /* on low latency each frame is send for processing without filling the
     * batch */
    g_mutex_lock (&priv->infer_lock);
    if ((priv->infer_level > 1 && priv->low_latency_infer) ||
        (g_queue_get_length (priv->infer_batch_queue) >=
            priv->infer_batch_size)) {
      /* Signal infer thread that frame is available */
      g_cond_signal (&priv->infer_cond);
    }
    g_mutex_unlock (&priv->infer_lock);

    /* free PPE input members */
    if (priv->ppe_frame->child_buf)
      gst_buffer_unref (priv->ppe_frame->child_buf);
    if (priv->ppe_frame->child_vinfo)
      gst_video_info_free (priv->ppe_frame->child_vinfo);
    if (priv->ppe_frame->vvas_frame) {
      vvas_video_frame_free (priv->ppe_frame->vvas_frame);
    }
    if (priv->ppe_frame->parent_vinfo) {
      gst_video_info_free (priv->ppe_frame->parent_vinfo);
      priv->ppe_frame->parent_vinfo = NULL;
    }

    memset (ppe_handle->input, 0x0, sizeof (VvasVideoFrame *) * MAX_NUM_OBJECT);
    memset (ppe_handle->output, 0x0,
        sizeof (VvasVideoFrame *) * MAX_NUM_OBJECT);

    g_mutex_lock (&priv->ppe_lock);
    priv->ppe_need_data = TRUE;
    g_cond_signal (&priv->ppe_need_input);
    g_mutex_unlock (&priv->ppe_lock);
  }

error:
  g_mutex_lock (&priv->ppe_lock);
  priv->stop = TRUE;
  g_cond_signal (&priv->ppe_need_input);
  g_mutex_unlock (&priv->ppe_lock);

  GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to process frame in PPE."),
      ("failed to process frame in PPE."));
  priv->last_fret = GST_FLOW_ERROR;

exit:
  /* Inform Infer thread */
  priv->ppe_thread_state = VVAS_THREAD_EXITED;

  g_mutex_lock (&priv->infer_lock);
  g_cond_signal (&priv->infer_cond);
  g_mutex_unlock (&priv->infer_lock);
  return NULL;
}

/**
 * @fn static void transform_coordinates (GstVvas_XInfer * self,VvasBoundingBox * c_bbox,
 *					  VvasBoundingBox * p_bbox, gdouble hfactor, gdouble vfactor)
 * @param [in] self - Handle to GstVvas_XInfer
 * @param [inout] c_bbox - child bbox which need to transform to new values
 * @param [in] p_bbox - Parent bbox to which child has to transform
 * @param [in] hfactor - x transform factor
 * @param [in] vfactor - y transform factor
 * @param [in] xOffset - x offset value
 * @param [in] yOffset - y offset value
 * @return None
 *
 * @brief Transform child bbox to Parent bbox resolution
 */
static void
transform_coordinates (GstVvas_XInfer * self,
    VvasBoundingBox * c_bbox, VvasBoundingBox * p_bbox,
    gdouble hfactor, gdouble vfactor, int32_t xOffset, int32_t yOffset)
{
  GST_LOG_OBJECT (self, "bbox : x = %d, y = %d, w = %d, h = %d",
      c_bbox->x, c_bbox->y, c_bbox->width, c_bbox->height);

  c_bbox->x = nearbyintf (c_bbox->x * hfactor) + xOffset;
  c_bbox->y = nearbyintf (c_bbox->y * vfactor) + yOffset;
  c_bbox->width = nearbyintf (c_bbox->width * hfactor);
  c_bbox->height = nearbyintf (c_bbox->height * vfactor);

  GST_LOG_OBJECT (self, "hfactor = %f vfactor = %f", hfactor, vfactor);
  GST_LOG_OBJECT (self, "bbox add factor : x = %d, y = %d, w = %d, h = %d",
      c_bbox->x, c_bbox->y, c_bbox->width, c_bbox->height);

  GST_LOG_OBJECT (self, "(x,y) : (%d, %d) -> (%d, %d)",
      c_bbox->x, c_bbox->y, c_bbox->x + p_bbox->x, c_bbox->y + p_bbox->y);

  /* scale x offset to parent bbox resolution */
  c_bbox->x += p_bbox->x;
  c_bbox->y += p_bbox->y;
}

/**
 * @fn static void transform_landmark (GstVvas_XInfer * self, GstInferencePrediction * cur_prediction,
 *				       GstInferencePrediction * parent_prediction, gdouble hfactor, gdouble vfactor)
 * @param [in] self - Handle to GstVvas_XInfer
 * @param [inout] cur_prediction - current prediction which need to transform
 * @param [in] parent_prediction - parent prediction
 * @param [in] hfactor - x transform factor
 * @param [in] vfactor - y transform factor
 * @param [in] xOffset - x offset value
 * @param [in] yOffset - y offset value 
 * @return None
 *
 * @brief Build bbox from landmark points and send for transform
 */
static void
transform_landmark (GstVvas_XInfer * self,
    GstInferencePrediction * cur_prediction,
    GstInferencePrediction * parent_prediction,
    gdouble hfactor, gdouble vfactor, int32_t xOffset, int32_t yOffset)
{
  gint num;
  VvasBoundingBox c_bbox;

  for (num = 0; num < NUM_LANDMARK_POINT; num++) {
    Pointf *point_ptr =
        (Pointf *) & (cur_prediction->prediction.feature.landmark[num].x);
    c_bbox.x = point_ptr->x;
    c_bbox.y = point_ptr->y;
    c_bbox.width = 0;
    c_bbox.height = 0;

    transform_coordinates (self, &c_bbox, &parent_prediction->prediction.bbox,
        hfactor, vfactor, xOffset, yOffset);
    point_ptr->x = c_bbox.x;
    point_ptr->y = c_bbox.y;
  }
}

static void
transform_roadlines (GstVvas_XInfer * self,
    GstInferencePrediction * cur_prediction,
    GstInferencePrediction * parent_prediction,
    gdouble hfactor, gdouble vfactor, int32_t xOffset, int32_t yOffset)
{
  gint num;
  VvasBoundingBox c_bbox;

  for (num = 0; num < cur_prediction->prediction.feature.line_size; num++) {
    Pointf *point_ptr =
        (Pointf *) & (cur_prediction->prediction.feature.road_line[num].x);
    c_bbox.x = point_ptr->x;
    c_bbox.y = point_ptr->y;
    c_bbox.width = 0;
    c_bbox.height = 0;

    transform_coordinates (self, &c_bbox, &parent_prediction->prediction.bbox,
        hfactor, vfactor, xOffset, yOffset);
    point_ptr->x = c_bbox.x;
    point_ptr->y = c_bbox.y;
  }
}

/**
 * @fn static void transform_pose14pt (GstVvas_XInfer * self, GstInferencePrediction * cur_prediction,
 *				       GstInferencePrediction * parent_prediction, gdouble hfactor, gdouble vfactor)
 * @param [in] self - Handle to GstVvas_XInfer
 * @param [inout] cur_prediction - current prediction which need to transform
 * @param [in] parent_prediction - parent prediction
 * @param [in] hfactor - x transform factor
 * @param [in] vfactor - y transform factor
 * @param [in] xOffset - x offset value
 * @param [in] yOffset - y offset value
 * @return None
 *
 * @brief Build bbox from Pose14Pt points and send for transform
 */
static void
transform_pose14pt (GstVvas_XInfer * self,
    GstInferencePrediction * cur_prediction,
    GstInferencePrediction * parent_prediction,
    gdouble hfactor, gdouble vfactor, int32_t xOffset, int32_t yOffset)
{
  gint num;
  VvasBoundingBox c_bbox;
  Pose14Pt *pose14pt = &cur_prediction->prediction.pose14pt;
  Pointf *point_ptr = (Pointf *) pose14pt;

  for (num = 0; num < NUM_POSE_POINT; num++) {
    c_bbox.x = point_ptr->x;
    c_bbox.y = point_ptr->y;
    c_bbox.width = 0;
    c_bbox.height = 0;

    transform_coordinates (self, &c_bbox, &parent_prediction->prediction.bbox,
        hfactor, vfactor, xOffset, yOffset);
    point_ptr->x = c_bbox.x;
    point_ptr->y = c_bbox.y;

    point_ptr++;
  }
}

/**
 * @fn static void update_child_bbox (GNode * node, gpointer data)
 * @param [in] node - a node in tree
 * @param [inout] data - used to typecast to Vvas_XInferNodeInfo
 * @return None
 *
 * @brief Transform child prediction coordinate as per parent coordinates
 */
static void
update_child_bbox (GNode * node, gpointer data)
{
  Vvas_XInferNodeInfo *node_info = (Vvas_XInferNodeInfo *) data;
  GstVvas_XInfer *self = node_info->self;
  gdouble hfactor, vfactor;
  gint fw = 1, fh = 1, tw, th;
  int32_t xOffset = 0, yOffset = 0;
  GstInferencePrediction *cur_prediction =
      (GstInferencePrediction *) node->data;
  GstInferencePrediction *parent_prediction =
      (GstInferencePrediction *) node->parent->data;

  if (cur_prediction->prediction.bbox_scaled)
    return;

  if (node_info->use_roi_data) {
    tw = node_info->input_roi.roi[0].width;
    th = node_info->input_roi.roi[0].height;
    fw = node_info->output_roi.roi[0].width;
    fh = node_info->output_roi.roi[0].height;
  } else {
    tw = GST_VIDEO_INFO_WIDTH (node_info->parent_vinfo);
    th = GST_VIDEO_INFO_HEIGHT (node_info->parent_vinfo);
    fw = GST_VIDEO_INFO_WIDTH (node_info->child_vinfo);
    fh = GST_VIDEO_INFO_HEIGHT (node_info->child_vinfo);
  }

  if (!parent_prediction->prediction.bbox.width
      && !parent_prediction->prediction.bbox.height) {
    hfactor = tw * 1.0 / fw;
    vfactor = th * 1.0 / fh;
    if (node_info->use_roi_data) {
      xOffset =
          node_info->input_roi.roi[0].x_cord -
          (int32_t) ((double) node_info->output_roi.roi[0].x_cord * hfactor);
      yOffset =
          node_info->input_roi.roi[0].y_cord -
          (int32_t) ((double) node_info->output_roi.roi[0].y_cord * vfactor);
    }
  } else {
    hfactor = parent_prediction->prediction.bbox.width * 1.0 / fw;
    vfactor = parent_prediction->prediction.bbox.height * 1.0 / fh;
  }

  if (cur_prediction->prediction.bbox.width
      && cur_prediction->prediction.bbox.height) {
    transform_coordinates (self, &cur_prediction->prediction.bbox,
        &parent_prediction->prediction.bbox, hfactor, vfactor,
        xOffset, yOffset);
  }

  if (cur_prediction->prediction.model_class == VVAS_XCLASS_POSEDETECT) {
    transform_pose14pt (self, cur_prediction,
        parent_prediction, hfactor, vfactor, xOffset, yOffset);
  }

  if (cur_prediction->prediction.feature.type == LANDMARK)
    transform_landmark (self, cur_prediction,
        parent_prediction, hfactor, vfactor, xOffset, yOffset);

  if (cur_prediction->prediction.feature.type == ROADLINE
      || cur_prediction->prediction.feature.type == ULTRAFAST) {
    transform_roadlines (self, cur_prediction,
        parent_prediction, hfactor, vfactor, xOffset, yOffset);
  }

  cur_prediction->prediction.bbox_scaled = TRUE;
}

/**
 * @fn static gboolean vvas_xinfer_add_metadata_at_level_1 (GstVvas_XInfer * self, GstBuffer * parent_buf,
 *							    GstVideoInfo * parent_vinfo, GstBuffer * infer_inbuf)
 * @param [in] self - Handle to GstVvas_XInfer
 * @param [in] parent_buf - Parent buffer
 * @param [in] parent_vinfo - Video info of parent buffer
 * @param [in] infer_inbuf - Buffer on which inference/prediction happened
 *
 * @return TRUE on success
 *         FALSE on failure
 *
 * @brief Add full frame prediction metadata to parent
 */
static gboolean
vvas_xinfer_add_metadata_at_level_1 (GstVvas_XInfer * self,
    GstBuffer * parent_buf, GstVideoInfo * parent_vinfo,
    GstBuffer * infer_inbuf)
{
  GstInferenceMeta *parent_meta = NULL;
  GstInferenceMeta *child_meta = NULL;

  parent_meta = (GstInferenceMeta *) gst_buffer_get_meta (parent_buf,
      gst_inference_meta_api_get_type ());

  if (parent_buf != infer_inbuf) {
    child_meta = (GstInferenceMeta *) gst_buffer_add_meta (infer_inbuf,
        gst_inference_meta_get_info (), NULL);
    if (!child_meta) {
      GST_ERROR_OBJECT (self, "failed to add metadata to buffer %p",
          infer_inbuf);
      return FALSE;
    }

    if (parent_meta) {          /* assign parent prediction to inference */
      gst_inference_prediction_unref (child_meta->prediction);
      child_meta->prediction =
          gst_inference_prediction_ref (parent_meta->prediction);
    }

    if (self->priv->infer_attach_ppebuf) {
      if (child_meta->prediction->sub_buffer != infer_inbuf) {
        if (child_meta->prediction->sub_buffer)
          gst_buffer_unref (child_meta->prediction->sub_buffer);
        GST_LOG_OBJECT (self, "attaching %p as sub_buffer", infer_inbuf);
        child_meta->prediction->sub_buffer = gst_buffer_ref (infer_inbuf);
      } else {
        gst_buffer_ref (infer_inbuf);
      }
    }
  } else {
    /* parent buffer as inference buffer i.e. No PPE */
    if (!parent_meta) {
      child_meta = (GstInferenceMeta *) gst_buffer_add_meta (infer_inbuf,
          gst_inference_meta_get_info (), NULL);
      if (!child_meta) {
        GST_ERROR_OBJECT (self, "failed to add metadata to buffer %p",
            infer_inbuf);
        return FALSE;
      }
      child_meta->prediction->prediction.bbox.width =
          GST_VIDEO_INFO_WIDTH (parent_vinfo);
      child_meta->prediction->prediction.bbox.height =
          GST_VIDEO_INFO_HEIGHT (parent_vinfo);
    } else {
      /* no need to add metadata as it is already present */
    }
  }

  return TRUE;
}

/**
 * @fn static gpointer vvas_xinfer_infer_loop (gpointer data)
 * @param [in] data - Handle to GstVvas_XInfer
 * @return NULL when thread exit normally
 *
 * @brief The function to execute as the Infer thread
 * @detail The function receives the input frame and do
 *         1. Wait till no of input is same as decided batch size
 *            Once the sufficient frame of batch size are available then
 *         2. At level == 1
 *             Add metadata to buffer as input buffer came from either
 *             submit_input_buffer or PPE_thread this case, so metadata is not
 *             available
 *         3. Send batch of frames to Infer kernel and wait for output
 *         4. Scale and attached new infer metadata to its parent metadata
 *         5. push the frame to downstream
 */
static gpointer
vvas_xinfer_infer_loop (gpointer data)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (data);
  GstVvas_XInferPrivate *priv = self->priv;
  VvasCoreModule *infer_handle = priv->infer_handle;
  gint batch_len = 0;
  GstBuffer **parent_bufs = NULL, **child_bufs = NULL;
  GstVideoInfo **parent_vinfos = NULL, **child_vinfos = NULL;
  GstEvent **events = NULL;
  VvasVideoFrame **vvas_frames = NULL;
  VvasInferPrediction **predictions = NULL;
  guint cur_batch_size = 0;
  gint total_queued_size = 0, cur_queued_size = 0;
  gboolean sent_eos = FALSE;
  gboolean *push_parent_bufs = NULL;
  vvas_ms_roi *input_roi = NULL;
  vvas_ms_roi *output_roi = NULL;
  gboolean *use_roi_data = NULL;
  gboolean timeout_triggered = FALSE;
  VvasReturnType vret;

  /* Mark thread is running */
  priv->infer_thread_state = VVAS_THREAD_RUNNING;

  /* Create gstBuffer equal to max hold by infer queue */
  parent_bufs =
      (GstBuffer **) calloc (priv->max_infer_queue, sizeof (GstBuffer *));
  if (parent_bufs == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  parent_vinfos =
      (GstVideoInfo **) calloc (priv->max_infer_queue, sizeof (GstVideoInfo *));
  if (parent_vinfos == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  child_bufs =
      (GstBuffer **) calloc (priv->max_infer_queue, sizeof (GstBuffer *));
  if (child_bufs == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  child_vinfos =
      (GstVideoInfo **) calloc (priv->max_infer_queue, sizeof (GstVideoInfo *));
  if (child_vinfos == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  vvas_frames =
      (VvasVideoFrame **) calloc (priv->max_infer_queue,
      sizeof (VvasVideoFrame *));
  if (vvas_frames == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  predictions =
      (VvasInferPrediction **) calloc (priv->max_infer_queue,
      sizeof (VvasInferPrediction *));
  if (predictions == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  events = (GstEvent **) calloc (priv->max_infer_queue, sizeof (GstEvent *));
  if (events == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  push_parent_bufs =
      (gboolean *) calloc (priv->max_infer_queue, sizeof (gboolean));
  if (push_parent_bufs == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  input_roi =
      (vvas_ms_roi *) calloc (priv->max_infer_queue, sizeof (vvas_ms_roi));
  if (input_roi == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  output_roi =
      (vvas_ms_roi *) calloc (priv->max_infer_queue, sizeof (vvas_ms_roi));
  if (output_roi == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  use_roi_data = (gboolean *) calloc (priv->max_infer_queue, sizeof (gboolean));
  if (use_roi_data == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  /* Execute till stop raised */
  while (!priv->stop) {
    Vvas_XInferFrame *inframe = NULL;
    guint idx, tmp_idx;
    guint min_batch = 0;

    g_mutex_lock (&priv->infer_lock);
    batch_len = g_queue_get_length (priv->infer_batch_queue);

    g_cond_signal (&priv->infer_batch_full);
    if (batch_len < priv->infer_batch_size &&
        priv->ppe_thread_state != VVAS_THREAD_EXITED && !priv->is_eos
        && !priv->is_pad_eos) {
      /* wait for batch size frames */
      GST_DEBUG_OBJECT (self, "wait for the next batch");
      if (self->batch_timeout) {
        gint64 end_time =
            g_get_monotonic_time () +
            self->batch_timeout * G_TIME_SPAN_MILLISECOND;
        g_cond_wait_until (&priv->infer_cond, &priv->infer_lock, end_time);
        GST_DEBUG_OBJECT (self, "timeout triggered");
        timeout_triggered = TRUE;
      } else {
        g_cond_wait (&priv->infer_cond, &priv->infer_lock);
      }
    }
    g_mutex_unlock (&priv->infer_lock);

    if (priv->stop)
      goto exit;

    if (priv->infer_level == 1 || !priv->low_latency_infer) {
      if ((g_queue_get_length (priv->infer_batch_queue) <
              priv->infer_batch_size)
          && priv->ppe_thread_state != VVAS_THREAD_EXITED && !priv->is_eos
          && !priv->is_pad_eos && !timeout_triggered) {
        GST_ERROR_OBJECT (self,
            "unexpected behaviour!!! "
            "batch length (%d) < required batch size %d",
            g_queue_get_length (priv->infer_batch_queue),
            priv->infer_batch_size);
        goto error;
      }
    }

    /* here we have sufficient buffer to process */

    /* get updated batch length */
    g_mutex_lock (&priv->infer_lock);
    batch_len = g_queue_get_length (priv->infer_batch_queue);
    g_mutex_unlock (&priv->infer_lock);
    GST_LOG_OBJECT (self, "batch length = %d & ppe_thread state = %d",
        batch_len, priv->ppe_thread_state);

  infer_pending:

    min_batch = batch_len > priv->infer_batch_size ?
        priv->infer_batch_size : batch_len;
    min_batch = priv->max_infer_queue - total_queued_size < min_batch ?
        priv->max_infer_queue - total_queued_size : min_batch;
    GST_DEBUG_OBJECT (self, "preparing batch of %d frames", min_batch);

    cur_queued_size = 0;

    for (idx = 0; idx < min_batch; idx++) {
      g_mutex_lock (&priv->infer_lock);
      inframe = g_queue_pop_head (priv->infer_batch_queue);
      GST_LOG_OBJECT (self,
          "popped frame %p from batch queue with skip_processing = %d", inframe,
          inframe->skip_processing);

      g_mutex_unlock (&priv->infer_lock);

      if (!inframe->skip_processing) {
        infer_handle->input[cur_batch_size] = inframe->vvas_frame;
        predictions[cur_batch_size] = NULL;
        cur_batch_size++;

        if (priv->infer_level == 1) {
          /* inference input buffer will come from either submit_input_buffer
           * or PPE_thread in case of level-1,
           * so metadata is not added in ppe_thread
           */
          if (inframe->child_buf) {
            vvas_xinfer_add_metadata_at_level_1 (self, inframe->parent_buf,
                inframe->parent_vinfo, inframe->child_buf);
          } else {
            vvas_xinfer_add_metadata_at_level_1 (self, inframe->parent_buf,
                inframe->parent_vinfo, inframe->parent_buf);
          }
        }
      } else {
        GST_LOG_OBJECT (self, "skipping frame %p from inference", inframe);
      }

      vvas_frames[total_queued_size + idx] = inframe->vvas_frame;
      parent_bufs[total_queued_size + idx] = inframe->parent_buf;
      parent_vinfos[total_queued_size + idx] = inframe->parent_vinfo;
      child_bufs[total_queued_size + idx] = inframe->child_buf;
      child_vinfos[total_queued_size + idx] = inframe->child_vinfo;
      events[total_queued_size + idx] = inframe->event;
      push_parent_bufs[total_queued_size + idx] = inframe->last_parent_buf;
      input_roi[total_queued_size + idx] = inframe->input_roi;
      output_roi[total_queued_size + idx] = inframe->output_roi;
      use_roi_data[total_queued_size + idx] = inframe->use_roi_data;

      g_slice_free1 (sizeof (Vvas_XInferFrame), inframe);

      if (cur_batch_size == priv->infer_batch_size) {
        GST_LOG_OBJECT (self, "input batch is ready for inference");
        /* incrementing to represent number elements popped */
        idx++;
        break;
      }

      if (priv->infer_level > 1 && priv->low_latency_infer) {
        if (inframe->last_parent_buf) {
          GST_LOG_OBJECT (self, "in low latency mode, push current batch");
          /* incrementing to represent number elements popped */
          idx++;
          break;
        }
      }

      if (timeout_triggered) {
        GST_LOG_OBJECT (self, "timeout.. pushing current batch to inference");
        /* incrementing to represent number elements popped */
        idx++;
        break;
      }
    }                           /* close of for loop */

    cur_queued_size = idx;
    total_queued_size += cur_queued_size;

    if (!priv->low_latency_infer && cur_batch_size < priv->infer_batch_size &&
        !priv->is_eos && !priv->is_pad_eos
        && total_queued_size < priv->max_infer_queue && !timeout_triggered) {
      GST_DEBUG_OBJECT (self,
          "current batch %d is not enough. " "continue to fetch data",
          cur_batch_size);
      continue;
    }

    /* Reset for next iteration */
    timeout_triggered = FALSE;

    GST_LOG_OBJECT (self, "sending batch of %u frames", cur_batch_size);

    if (cur_batch_size && priv->last_fret == GST_FLOW_OK) {
      vret =
          vvas_dpuinfer_process_frames (infer_handle->handle,
          infer_handle->input, predictions, cur_batch_size);
      if (vret != VVAS_RET_SUCCESS) {
        GST_ERROR_OBJECT (self, "DPU failed to process frames");
        goto error;
      }

      tmp_idx = 0;
      /** Convert vvasinfer predictions to gstinfer here */
      for (idx = 0; idx < total_queued_size; idx++) {
        GstInferenceMeta *gst_meta = NULL;
        GstInferencePrediction *new_gst_pred = NULL;
        GstBuffer *buf = NULL;
        if (infer_handle->input[tmp_idx] != vvas_frames[idx])
          continue;
        /** indicates skip_processing is set to TRUE */
        if (!child_bufs[idx] && !parent_bufs[idx])
          continue;
        (child_bufs[idx] != NULL) ? (buf = child_bufs[idx]) : (buf =
            parent_bufs[idx]);

        gst_meta =
            (GstInferenceMeta *) gst_buffer_get_meta (buf,
            gst_inference_meta_api_get_type ());
        if (predictions[tmp_idx]) {
          if (priv->do_postprocess) {
            /* Here prediction node containing the raw tensors is sent to
             * postprocessing library, which will return a tree of
             * VvasInferPredictions with post-processed results
             */
            VvasInferPrediction *postprocess_pred =
                priv->postprocess_run (priv->postproc_handle,
                predictions[tmp_idx]->node->children->data);
            /* Freeing the prediction tree with raw tensors here as we are
             * attaching a tree with post-processed results to the GstBuffer
             */
            vvas_inferprediction_free (predictions[tmp_idx]);
            predictions[tmp_idx] = postprocess_pred;
          }
          if (gst_meta) {
            if (gst_meta->prediction) {
              /** Append all leaf nodes of vvasinfer prediction */
              VvasList *iter = NULL;
              VvasList *pred_nodes =
                  vvas_inferprediction_get_nodes (predictions[tmp_idx]);
              for (iter = pred_nodes; iter != NULL; iter = iter->next) {
                VvasInferPrediction *leaf = (VvasInferPrediction *) iter->data;
                GstInferencePrediction *gst_leaf = NULL;
                gst_leaf = gst_infer_node_from_vvas_infer (leaf);
                gst_inference_prediction_append (gst_meta->prediction,
                    gst_leaf);
              }
              vvas_list_free (pred_nodes);
            } else {
              /** Convert complete tree from vvasinfer to gstinfer */
              VvasList *iter = NULL;
              VvasList *pred_nodes =
                  vvas_inferprediction_get_nodes (predictions[tmp_idx]);
              /** Convert root node */
              new_gst_pred =
                  gst_infer_node_from_vvas_infer (predictions[tmp_idx]);
              /** Convert all leaf nodes and append to root */
              for (iter = pred_nodes; iter != NULL; iter = iter->next) {
                VvasInferPrediction *leaf = (VvasInferPrediction *) iter->data;
                GstInferencePrediction *gst_leaf = NULL;
                gst_leaf = gst_infer_node_from_vvas_infer (leaf);
                gst_inference_prediction_append (new_gst_pred, gst_leaf);
              }
              vvas_list_free (pred_nodes);
              gst_meta->prediction = new_gst_pred;
            }
          } else {
            VvasList *iter = NULL;
            VvasList *pred_nodes =
                vvas_inferprediction_get_nodes (predictions[tmp_idx]);
            gst_meta =
                (GstInferenceMeta *) gst_buffer_add_meta (buf,
                gst_inference_meta_get_info (), NULL);
            /** Convert root node */
            new_gst_pred =
                gst_infer_node_from_vvas_infer (predictions[tmp_idx]);
              /** Convert all leaf nodes and append to root */
            for (iter = pred_nodes; iter != NULL; iter = iter->next) {
              VvasInferPrediction *leaf = (VvasInferPrediction *) iter->data;
              gst_inference_prediction_append (new_gst_pred,
                  gst_infer_node_from_vvas_infer (leaf));
            }
            vvas_list_free (pred_nodes);
            if (gst_meta->prediction)
              gst_inference_prediction_unref (gst_meta->prediction);
            gst_meta->prediction = new_gst_pred;
          }
        }

        if (predictions[tmp_idx]) {
          vvas_inferprediction_free (predictions[tmp_idx]);
          predictions[tmp_idx] = NULL;
        }
        tmp_idx++;
      }

      /* signal listeners that we have processed one batch */
      g_signal_emit (self, vvas_signals[SIGNAL_VVAS], 0);
    }

    for (idx = 0; idx < total_queued_size; idx++) {

      if (vvas_frames[idx]) {
        vvas_video_frame_free (vvas_frames[idx]);
        vvas_frames[idx] = NULL;
      }

      if (child_bufs[idx]) {
        if (priv->infer_level == 1) {
          if (parent_bufs[idx] != child_bufs[idx]) {
            /* parent_buf and child_buf same, then dpu library itself will
             * provide scaled metadata. So scaling is required like below
             * when parent_buf != child_buf
             */
            GstInferenceMeta *child_meta;
            Vvas_XInferNodeInfo node_info = { self,
              parent_vinfos[idx], child_vinfos[idx],
              input_roi[idx], output_roi[idx], use_roi_data[idx]
            };

            /* child_buf received from PPE, so update metadata in parent buf */
            child_meta =
                (GstInferenceMeta *) gst_buffer_get_meta (child_bufs[idx],
                gst_inference_meta_api_get_type ());
            if (child_meta) {
              if (g_node_n_children ((GNode *) child_meta->
                      prediction->prediction.node)) {
                GstBuffer *writable_buf = NULL;
                GstInferenceMeta *parent_meta;

                /*scale child prediction to match with parent */
                g_node_children_foreach ((GNode *) child_meta->prediction->
                    prediction.node, G_TRAVERSE_ALL, update_child_bbox,
                    &node_info);

                if (!gst_buffer_is_writable (parent_bufs[idx])) {
                  GST_DEBUG_OBJECT (self, "create writable buffer of %p",
                      parent_bufs[idx]);
                  writable_buf = gst_buffer_make_writable (parent_bufs[idx]);
                  parent_bufs[idx] = writable_buf;
                }

                parent_meta =
                    (GstInferenceMeta *) gst_buffer_get_meta (parent_bufs[idx],
                    gst_inference_meta_api_get_type ());
                if (!parent_meta) {
                  parent_meta = (GstInferenceMeta *)
                      gst_buffer_add_meta (parent_bufs[idx],
                      gst_inference_meta_get_info (), NULL);
                  if (!parent_meta) {
                    GST_ERROR_OBJECT (self,
                        "failed to add metadata to parent buffer");
                    goto error;
                  }
                  /* assigning childmeta to parent metadata prediction */
                  gst_inference_prediction_unref (parent_meta->prediction);
                  parent_meta->prediction = child_meta->prediction;
                  child_meta->prediction = gst_inference_prediction_new ();

                  parent_meta->prediction->prediction.bbox.width =
                      GST_VIDEO_INFO_WIDTH (parent_vinfos[idx]);
                  parent_meta->prediction->prediction.bbox.height =
                      GST_VIDEO_INFO_HEIGHT (parent_vinfos[idx]);
                  GST_LOG_OBJECT (self, "add inference metadata to %p",
                      parent_bufs[idx]);
                } else {
                  gst_inference_prediction_unref (child_meta->prediction);
                  child_meta->prediction = gst_inference_prediction_new ();
                }
              }
              if (self->priv->infer_attach_ppebuf) {
                /* remove as child_buf will be attached as sub_buffer */
                gst_buffer_unref (child_bufs[idx]);
                gst_buffer_remove_meta (child_bufs[idx],
                    GST_META_CAST (child_meta));
                child_bufs[idx] = NULL;
              }
            }
          }
        } else {                /* inference level > 1 */
          GstInferenceMeta *child_meta = NULL;
          GstInferencePrediction *parent_prediction = NULL;
          Vvas_XInferNodeInfo node_info = { self,
            parent_vinfos[idx], child_vinfos[idx]
          };

          child_meta =
              (GstInferenceMeta *) gst_buffer_get_meta (child_bufs[idx],
              gst_inference_meta_api_get_type ());
          if (child_meta) {
            parent_prediction = (GstInferencePrediction *)
                child_meta->prediction->prediction.node->parent->data;

            g_node_children_foreach ((GNode *) child_meta->
                prediction->prediction.node, G_TRAVERSE_ALL, update_child_bbox,
                &node_info);
            gst_inference_prediction_unref (parent_prediction);
            child_meta->prediction = gst_inference_prediction_new ();
          }
        }
      }

      if (child_bufs[idx]) {
        gst_buffer_unref (child_bufs[idx]);
        child_bufs[idx] = NULL;
      }
      if (child_vinfos[idx]) {
        gst_video_info_free (child_vinfos[idx]);
        child_vinfos[idx] = NULL;
      }

      if (push_parent_bufs[idx]) {
        if (priv->last_fret == GST_FLOW_OK) {
          GstInferenceMeta *parent_meta = NULL;
          GstBuffer *writable_buf = NULL;
          gchar *infer_meta_str = NULL;

          parent_meta =
              (GstInferenceMeta *) gst_buffer_get_meta (parent_bufs[idx],
              gst_inference_meta_api_get_type ());
          /* Attaching empty metadata if flag_attach_empty_infer is true */
          if (!parent_meta && self->flag_attach_empty_infer) {
            if (!gst_buffer_is_writable (parent_bufs[idx])) {
              GST_DEBUG_OBJECT (self, "create writable buffer of %p",
                  parent_bufs[idx]);
              writable_buf = gst_buffer_make_writable (parent_bufs[idx]);
              parent_bufs[idx] = writable_buf;
            }

            parent_meta = (GstInferenceMeta *)
                gst_buffer_add_meta (parent_bufs[idx],
                gst_inference_meta_get_info (), NULL);
            /* assigning childmeta to parent metadata prediction */
            gst_inference_prediction_unref (parent_meta->prediction);
            parent_meta->prediction = gst_inference_prediction_new ();
            parent_meta->prediction->prediction.bbox.width =
                GST_VIDEO_INFO_WIDTH (parent_vinfos[idx]);
            parent_meta->prediction->prediction.bbox.height =
                GST_VIDEO_INFO_HEIGHT (parent_vinfos[idx]);
          }
#ifdef PRINT_METADATA_TREE
          /* convert metadata to string for debug log */
          if (parent_meta) {
            infer_meta_str =
                gst_inference_prediction_to_string (parent_meta->prediction);
            GST_DEBUG_OBJECT (self, "output inference metadata : %s",
                infer_meta_str);
            g_free (infer_meta_str);

            g_node_traverse ((GNode *) parent_meta->prediction->prediction.node,
                G_PRE_ORDER, G_TRAVERSE_ALL, -1, printf_all_nodes, self);
          }
#endif
          GST_DEBUG_OBJECT (self, "pushing %" GST_PTR_FORMAT, parent_bufs[idx]);

          priv->last_fret = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (self),
              parent_bufs[idx]);
          if (priv->last_fret < GST_FLOW_OK) {
            switch (priv->last_fret) {
              case GST_FLOW_FLUSHING:
              case GST_FLOW_EOS:
                GST_WARNING_OBJECT (self, "failed to push buffer. reason %s",
                    gst_flow_get_name (priv->last_fret));
                break;
              default:
                GST_ELEMENT_ERROR (self, STREAM, FAILED,
                    ("failed to push buffer."),
                    ("failed to push buffer. reason %s (%d)",
                        gst_flow_get_name (priv->last_fret), priv->last_fret));
                goto exit;
            }
          }
        } else {
          gst_buffer_unref (parent_bufs[idx]);
        }
      }

      if (parent_vinfos[idx]) {
        gst_video_info_free (parent_vinfos[idx]);
        parent_vinfos[idx] = NULL;
      }

      if (events[idx]) {
        if (GST_EVENT_TYPE (events[idx]) == GST_EVENT_EOS) {
          sent_eos = TRUE;
          //EOS event will be sent from _sink_event()
          GST_INFO_OBJECT (self,
              "received EOS, exiting thread %" GST_PTR_FORMAT, events[idx]);
          goto exit;
        }
        if (GST_EVENT_TYPE (events[idx]) == GST_EVENT_CUSTOM_DOWNSTREAM) {
          GST_INFO_OBJECT (self,
              "received PAD-EOS, sending downstream %" GST_PTR_FORMAT,
              events[idx]);
          GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (data,
              events[idx]);
          events[idx] = NULL;
          priv->is_pad_eos = FALSE;
        }
      }
    }                           /*end of for loop */

    memset (infer_handle->input, 0x0,
        sizeof (VvasVideoFrame *) * MAX_NUM_OBJECT);
    memset (infer_handle->output, 0x0,
        sizeof (VvasVideoFrame *) * MAX_NUM_OBJECT);

    if (priv->infer_level > 1 && (batch_len - cur_queued_size > 0)) {
      GST_LOG_OBJECT (self, "processing pending %d inference frames",
          batch_len - cur_queued_size);
      batch_len = batch_len - cur_queued_size;
      /* reset variables for next batch */
      total_queued_size = 0;
      cur_batch_size = 0;
      goto infer_pending;
    }
    /* reset variables for next batch */
    total_queued_size = 0;
    cur_batch_size = 0;

    if (sent_eos)
      goto exit;
  }

error:
  GST_ELEMENT_ERROR (self, STREAM, FAILED,
      ("failed to process frame in inference."),
      ("failed to process frame in inference."));
  priv->last_fret = GST_FLOW_ERROR;

exit:
  if (parent_bufs)
    free (parent_bufs);
  if (parent_vinfos)
    free (parent_vinfos);
  if (child_bufs)
    free (child_bufs);
  if (child_vinfos)
    free (child_vinfos);
  if (vvas_frames)
    free (vvas_frames);
  if (events)
    free (events);
  if (push_parent_bufs)
    free (push_parent_bufs);
  if (predictions)
    free (predictions);
  if (input_roi)
    free (input_roi);
  if (output_roi)
    free (output_roi);
  if (use_roi_data)
    free (use_roi_data);
  priv->infer_thread_state = VVAS_THREAD_EXITED;

  return NULL;
}

/**
 * @fn static inline gboolean vvas_xinfer_send_ppe_frame (GstVvas_XInfer * self, GstBuffer * parent_buf,
 *							   GstVideoInfo * parent_vinfo,
 *							   GstBuffer * child_buf, GstVideoInfo * child_vinfo,
 *							   VvasVideoFrame * vvas_frame, gboolean skip_process,
 *							   gboolean is_first_parent, GstEvent * event)
 * @param [in] self - handle to GstVvas_XInfer
 * @param [in] parent_buf - input gstreamer buffer from upstream
 * @param [in] parent_vinfo - video info of input buffer
 * @param [in] child_buf - Buffer which need to be preprocessed
 * @param [in] child_vinfo - Video info of child buffer
 * @param [in] vvas_frame - Pointer to VvasVideoFrame
 * @param [in] skip_process - Processing of frame need to be skipped or not
 * @param [in] is_first_parent - used to update last_parent_buf
 * @param [in] event - GstEvent
 *
 * @return TRUE on success
 *         FALSE on failure
 *
 * @brief This function prepare ppe_frame with buffer which need to be pre
 *        processed and generate ppe_has_input. The function is called from
 *        submit_input_buffer when input buffer is available on sink pad of
 *        infer
 */
static inline gboolean
vvas_xinfer_send_ppe_frame (GstVvas_XInfer * self, GstBuffer * parent_buf,
    GstVideoInfo * parent_vinfo,
    GstBuffer * child_buf, GstVideoInfo * child_vinfo,
    VvasVideoFrame * vvas_frame, gboolean skip_process,
    gboolean is_first_parent, GstEvent * event)
{
  GstVvas_XInferPrivate *priv = self->priv;

  g_mutex_lock (&priv->ppe_lock);
  if (!priv->ppe_need_data && !priv->stop) {
    /* ppe inbuf is not consumed wait till thread consumes it */
    g_cond_wait (&priv->ppe_need_input, &priv->ppe_lock);
  }

  /* Do not process if stop is set */
  if (priv->stop) {
    g_mutex_unlock (&priv->ppe_lock);
    return FALSE;
  }
  priv->ppe_frame->parent_buf = parent_buf;
  priv->ppe_frame->parent_vinfo = parent_vinfo;
  priv->ppe_frame->last_parent_buf = is_first_parent;
  priv->ppe_frame->vvas_frame = vvas_frame;
  priv->ppe_frame->child_buf = child_buf;
  priv->ppe_frame->child_vinfo = child_vinfo;
  priv->ppe_frame->event = NULL;
  priv->ppe_frame->skip_processing = skip_process;
  /* ppe data is available now */
  priv->ppe_need_data = FALSE;

  GST_LOG_OBJECT (self, "send frame to ppe loop with skip_processing %d",
      skip_process);
  /* wakeup ppe thread as data is available for processing */
  g_cond_signal (&priv->ppe_has_input);
  g_mutex_unlock (&priv->ppe_lock);
  return TRUE;
}

/**
 * @fn static GstFlowReturn gst_vvas_xinfer_submit_input_buffer (GstBaseTransform * trans,
 *								 gboolean is_discont, GstBuffer * inbuf)
 * @param [in] trans - Handle to GstBaseTransform
 * @param [in] is_discont - Indicates whether input buffer is coming after discontinuity from previous input buffer
 * @param [in] inbuf - input buffer from upstream
 *
 * @return GST_FLOW_ERROR in case of error
 *         GST_FLOW_OK exit without error
 *
 * @brief Function which accepts a new input buffer from upstream and processes it
 * @detail This function called when the plugin receives a buffer from upstream
 *        element. Two way of handling of input buffer is provided
 *        1. When preprocessing is enable
 *           - xinfer is at level 1
 *           The input buffer is mapped to ppe frame and send to ppe
 *           thread to process by emitting ppe_has_input signal
 *           - xinfer is at level > 1
 *           It is checked either the sub-buffer from previous inference can
 *           be used for inference or either previous infer has some detection.
 *           Accordingly the input buffer or sub-buffer are mapped to ppe frame
 *           and send to ppe thread to process by emitting ppe_has_input signal.
 *        2. When no preprocessing
 *           - xinfer is at level 1
 *           Mapped input buffer to infer_frame and add to infer_batch_queue to
 *           be processed by infer thread
 *           - xinfer is at level > 1
 *           Skip inbuffer if previous infer do not detect anything
 *           Check either previous infer sub-buffer can be used and if yes add
 *           them to infer_batch_queue after mapping to infer_frame.
 *
 */
static GstFlowReturn
gst_vvas_xinfer_submit_input_buffer (GstBaseTransform * trans,
    gboolean is_discont, GstBuffer * inbuf)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  GstVvas_XInferPrivate *priv = self->priv;
  gboolean bret = FALSE;

  GST_LOG_OBJECT (self, "received %" GST_PTR_FORMAT, inbuf);

  g_mutex_lock (&priv->infer_lock);
  if (g_queue_get_length (priv->infer_batch_queue) >=
      (priv->infer_batch_size << 1)) {
    GST_LOG_OBJECT (self, "inference batch queue is full. Wait for free space");
    g_cond_wait (&priv->infer_batch_full, &priv->infer_lock);
  }
  g_mutex_unlock (&priv->infer_lock);

  if (priv->stop)
    return GST_FLOW_OK;

  if (priv->do_preprocess) {    /* send frames to PPE thread */
    GstBuffer *new_inbuf = NULL;
    VvasVideoFrame *vvas_frame = NULL;
    GstInferenceMeta *parent_meta = NULL;
    GstBuffer *child_buf = NULL;
    GstVideoInfo *child_vinfo = NULL;

    parent_meta = (GstInferenceMeta *) gst_buffer_get_meta (inbuf,
        gst_inference_meta_api_get_type ());

    if (priv->infer_level == 1) {
      if (!parent_meta
          || !vvas_xinfer_is_sub_buffer_useful (self,
              parent_meta->prediction->sub_buffer)) {
        /* PPE needs HW buffer as input */
        bret = vvas_xinfer_prepare_ppe_input_frame (self, inbuf,
            priv->in_vinfo, &new_inbuf, &vvas_frame);
        if (!bret) {
          return GST_FLOW_ERROR;
        }
        child_buf = (new_inbuf == NULL ? gst_buffer_ref (inbuf) : new_inbuf);
        child_vinfo = gst_video_info_copy (priv->in_vinfo);
      }
    } else {                    /* priv->infer_level > 1 */
      // TODO: find is it needed to call prepare_ppe_input_frame API as it
      // involves memory copy
      if (parent_meta) {
        /* Previous infer has meta i.e found something */
        Vvas_XInferNumSubs numSubs = { self, FALSE };

        /* Check either sub-buffer from previous infer can be used */
        if (priv->infer_level <=
            g_node_max_height ((GNode *) parent_meta->prediction->
                prediction.node)) {
          g_node_traverse ((GNode *) parent_meta->prediction->prediction.node,
              G_PRE_ORDER, G_TRAVERSE_ALL, -1, check_bbox_buffers_availability,
              &numSubs);
        }

        if (!numSubs.available) {
          /* PPE needs HW buffer as input */
          bret =
              vvas_xinfer_prepare_ppe_input_frame (self, inbuf, priv->in_vinfo,
              &new_inbuf, &vvas_frame);
          if (!bret) {
            return GST_FLOW_ERROR;
          }
        }
      }
      child_buf = (new_inbuf == NULL ? gst_buffer_ref (inbuf) : new_inbuf);
      child_vinfo = gst_video_info_copy (priv->in_vinfo);
    }

    /* mapped inbuf/child_buf to ppe_frame and raise signal ppe_has_input */
    bret = vvas_xinfer_send_ppe_frame (self, inbuf,
        gst_video_info_copy (priv->in_vinfo), child_buf, child_vinfo,
        vvas_frame, FALSE, TRUE, NULL);
    if (!bret) {
      gst_video_info_free (child_vinfo);
      return GST_FLOW_OK;
    }
  } else {                      /* !priv->do_preprocess */
    /* send frames to inference thread directly */
    Vvas_XInferFrame *infer_frame = NULL;
    VvasVideoFrame *vvas_frame = NULL;
    GstInferenceMeta *infer_meta = NULL;

    infer_meta =
        (GstInferenceMeta *) gst_buffer_get_meta (inbuf,
        gst_inference_meta_api_get_type ());

    if (priv->infer_level == 1) {
      GstBuffer *infer_buf = NULL, *child_buf = NULL;
      GstVideoInfo *infer_vinfo = NULL, *child_vinfo = NULL;

      infer_frame = g_slice_new0 (Vvas_XInferFrame);

      if (infer_meta && infer_meta->prediction->sub_buffer &&
          vvas_xinfer_is_sub_buffer_useful (self,
              infer_meta->prediction->sub_buffer)) {
        GstVideoMeta *vmeta = NULL;

        child_vinfo = infer_vinfo = gst_video_info_new ();
        child_buf = infer_buf =
            gst_buffer_ref (infer_meta->prediction->sub_buffer);

        vmeta = gst_buffer_get_video_meta (child_buf);
        gst_video_info_set_format (child_vinfo, vmeta->format,
            vmeta->width, vmeta->height);
      } else {
        /* inference operates on big buffer, no child_buf */
        child_buf = NULL;
        child_vinfo = NULL;
        infer_vinfo = priv->in_vinfo;
        infer_buf = inbuf;
      }

      bret = vvas_xinfer_prepare_infer_input_frame (self, infer_buf,
          infer_vinfo, &vvas_frame);
      if (!bret) {
        return GST_FLOW_ERROR;
      }

      infer_frame->parent_buf = inbuf;
      infer_frame->parent_vinfo = gst_video_info_copy (priv->in_vinfo);
      infer_frame->last_parent_buf = TRUE;
      infer_frame->child_buf = child_buf;
      infer_frame->child_vinfo = child_vinfo;
      infer_frame->vvas_frame = vvas_frame;
      infer_frame->skip_processing = FALSE;
      infer_frame->event = NULL;

      GST_LOG_OBJECT (self, "send frame %p to level-%d inference", infer_frame,
          priv->infer_level);

      /* send input frame to inference thread */
      g_mutex_lock (&priv->infer_lock);
      g_queue_push_tail (priv->infer_batch_queue, infer_frame);
      g_mutex_unlock (&priv->infer_lock);
    }

    if (g_queue_get_length (priv->infer_batch_queue) >= priv->infer_batch_size) {
      g_mutex_lock (&priv->infer_lock);
      GST_LOG_OBJECT (self, "signal inference thread as queue size reached %u",
          g_queue_get_length (priv->infer_batch_queue));
      g_cond_signal (&priv->infer_cond);
      g_mutex_unlock (&priv->infer_lock);
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vvas_xinfer_generate_output (GstBaseTransform * trans, GstBuffer ** outbuf)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);

  return self->priv->last_fret;
}

/**
 *  @fn static void gst_vvas_xfilter_set_property (GObject * object, guint prop_id, const GValue * value,
 *						   GParamSpec * pspec)
 *  @param [in] Handle - to GstVvas_XFilter typecasted to GObject
 *  @param [in] prop_id - Property ID value
 *  @param [in] value - GValue which holds property value set by user
 *  @param [in] pspec - Handle to metadata of a property with property ID prop_id
 *  @return None
 *  @brief This API stores values sent from the user in GstVvas_XFilter object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::set_property
 *           function pointer and this will be invoked when developer sets properties on
 *           GstVvas_XFilter object. Based on property value type, corresponding
 *           g_value_get_xxx API will be called to get property value from GValue handle.
 */
static void
gst_vvas_xinfer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (object);

  switch (prop_id) {
    case PROP_PPE_CONFIG_LOCATION:
      if (GST_STATE (self) != GST_STATE_NULL) {
        g_warning
            ("can't set preprocess json_file path when instance is NOT in NULL state");
        return;
      }
      if (self->ppe_json_file)
        g_free (self->ppe_json_file);

      self->ppe_json_file = g_value_dup_string (value);
      break;
    case PROP_INFER_CONFIG_LOCATION:
      if (GST_STATE (self) != GST_STATE_NULL) {
        g_warning
            ("can't set inference json_file path when instance is NOT in NULL state");
        return;
      }
      if (self->infer_json_file)
        g_free (self->infer_json_file);
      self->infer_json_file = g_value_dup_string (value);
      break;
    case PROP_ATTACH_EMPTY_METADATA:
      self->flag_attach_empty_infer = g_value_get_boolean (value);
      break;
    case PROP_BATCH_SUBMIT_TIMEOUT:
      self->batch_timeout = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static void gst_vvas_xfilter_get_property (GObject * object, guint prop_id, GValue * value,
 *					           GParamSpec * pspec)
 *  @param [in] Handle - to GstVvas_XFilter typecasted to GObject
 *  @param [in] prop_id - Property ID value
 *  @param [in] value - GValue which holds property value to get by user
 *  @param [in] pspec - Handle to metadata of a property with property ID prop_id
 *  @return None
 *  @brief This API gets currently configured value of a property from GstVvas_XFilter instance
 *  @details This API is registered with GObjectClass by overriding GObjectClass::get_property
 *	     function pointer and this will be invoked when developer gets properties on
 *	     GstVvas_XFilter object. Based on property value type, corresponding g_value_set_xxx API
 *	     will be called to set property value to GValue handle.
 */
static void
gst_vvas_xinfer_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (object);

  switch (prop_id) {
    case PROP_PPE_CONFIG_LOCATION:
      g_value_set_string (value, self->ppe_json_file);
      break;
    case PROP_INFER_CONFIG_LOCATION:
      g_value_set_string (value, self->infer_json_file);
      break;
    case PROP_ATTACH_EMPTY_METADATA:
      g_value_set_boolean (value, self->flag_attach_empty_infer);
      break;
    case PROP_BATCH_SUBMIT_TIMEOUT:
      g_value_set_uint (value, self->batch_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static gboolean gst_vvas_xfilter_start (GstBaseTransform * trans)
 *  @param [in] - trans xinfer's parents instance handle which will be type casted to xinfer instance
 *  @return TRUE on success\n
 *          FALSE on failure
 *  @brief This API will be invoked by parent class (i.e. GstBaseTranform) before start processing frames.
 *  @details This API initializes xinfer member variables, reads json of ppe and infer, opens device handle
 *           and invokes initialization of INFER and PPE acceleration library. This function also started
 *           PPE and INFER thread.
 *        */
static gboolean
gst_vvas_xinfer_start (GstBaseTransform * trans)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  GstVvas_XInferPrivate *priv = self->priv;
  gboolean bret = FALSE;
  gchar *thread_name = NULL;

  self->priv = priv;
  priv->ppe_dev_idx = DEFAULT_DEVICE_INDEX;
  priv->in_vinfo = gst_video_info_new ();
  priv->do_init = TRUE;
  priv->stop = FALSE;
  priv->is_eos = FALSE;
  priv->is_pad_eos = FALSE;

  if (self->ppe_json_file) {
    bret = vvas_xinfer_read_ppe_config (self);
    priv->do_preprocess = TRUE;
    GST_INFO_OBJECT (self,
        "ppe configuration is available, enable preprocessing");
    if (!bret)
      goto error;
  } else {
    priv->do_preprocess = FALSE;
    GST_INFO_OBJECT (self, "preprocess config file is not present");
    GST_INFO_OBJECT (self, "software preprocessing will be done by DPU");
  }

  bret = vvas_xinfer_read_infer_config (self);
  if (!bret)
    goto error;

  if (priv->infer_level > 1 && !self->ppe_json_file) {
    GST_ERROR_OBJECT (self, "PPE is not availble, when inference-level(%d) > 1",
        priv->infer_level);
    goto error;
  }

  if (self->ppe_json_file) {
    memset (priv->ppe_handle->input, 0x0,
        sizeof (VvasVideoFrame *) * MAX_NUM_OBJECT);
    memset (priv->ppe_handle->output, 0x0,
        sizeof (VvasVideoFrame *) * MAX_NUM_OBJECT);
  }


  memset (priv->infer_handle->input, 0x0,
      sizeof (VvasVideoFrame *) * MAX_NUM_OBJECT);
  memset (priv->infer_handle->output, 0x0,
      sizeof (VvasVideoFrame *) * MAX_NUM_OBJECT);

  priv->infer_handle->init_done = FALSE;

  if (priv->do_init) {          /*TODO: why it is protected under do_init */
    if (!vvas_xinfer_infer_init (self))
      goto error;

    priv->infer_handle->init_done = TRUE;

    if (self->ppe_json_file) {
      priv->ppe_handle->init_done = FALSE;

      if (!vvas_xinfer_ppe_init (self))
        goto error;

      /* should be used before starting deinit */
      priv->ppe_handle->init_done = TRUE;
    }

    if (priv->do_postprocess) {
      if (!vvas_xinfer_postproc_init (self))
        goto error;
    }

    priv->do_init = FALSE;
  }

  GST_INFO_OBJECT (self, "%s to preprocess inference input frames",
      priv->do_preprocess ? "need" : "not needed");

  priv->ppe_thread_state = VVAS_THREAD_NOT_CREATED;
  priv->infer_thread_state = VVAS_THREAD_NOT_CREATED;

  if (priv->do_preprocess) {
    thread_name = g_strdup_printf ("%s-ppe-thread", GST_ELEMENT_NAME (self));
    priv->ppe_thread = g_thread_new (thread_name, vvas_xinfer_ppe_loop, self);
    g_free (thread_name);
  }

  thread_name = g_strdup_printf ("%s-infer-thread", GST_ELEMENT_NAME (self));
  priv->infer_thread = g_thread_new (thread_name, vvas_xinfer_infer_loop, self);
  g_free (thread_name);

  GST_INFO_OBJECT (self, "start completed");
  return TRUE;

error:
  if (self->ppe_json_file)
    vvas_xinfer_ppe_deinit (self);

  if (priv->do_postprocess)
    vvas_xinfer_postproc_deinit (self);

  vvas_xinfer_infer_deinit (self);
  gst_video_info_free (self->priv->in_vinfo);
  self->priv->in_vinfo = NULL;
  return FALSE;
}

/**
 *  @fn static gboolean gst_vvas_xinfer_query (GstBaseTransform * trans, GstPadDirection direction, GstQuery * query)
 *  @param [in] trans - xinfer's parents instance handle which will be type casted to xinfer instance
 *  @param [in] direction - The direction(sink/src) of a pad on which query received
 *  @param [in] query - Query received by xinfer instance on pad with direction
 *  @return TRUE if query handled successfully
 *          FALSE if query is not handled
 *  @brief Answers the capabilities query (i.e. GST_QUERY_CAPS) by populating it with vvas acceleration
 *         capabilities and invokes parent's query vmethod if a query is not handled by xinfer
 *  @ detail This also convert kcaps from infer kernel to GstCaps
 */
static gboolean
gst_vvas_xinfer_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  GstVvas_XInferPrivate *priv = self->priv;
  gboolean ret = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:{
      /* Take kernel kcap and convert to gst caps */
      GstCaps *newcap, *allcaps, *filter = NULL;
      GstStructure *s;
      GValue list = { 0, };
      GValue aval = { 0, };
      const char *fourcc;

      if (self->priv->do_init == TRUE)
        return FALSE;

      gst_query_parse_caps (query, &filter);

      /* Same buffer for sink and src,
       * so the same caps for sink and src pads
       * and for all pads
       */

      allcaps = gst_caps_new_empty ();
      newcap = gst_caps_new_empty ();

      g_value_init (&list, GST_TYPE_LIST);
      g_value_init (&aval, G_TYPE_STRING);

      s = gst_structure_new ("video/x-raw", "height", G_TYPE_INT,
          priv->model_conf.model_height, NULL);
      gst_structure_set (s, "width", G_TYPE_INT,
          priv->model_conf.model_width, NULL);
      fourcc =
          gst_video_format_to_string (get_gst_format (priv->
              dpu_conf->model_format));
      g_value_set_string (&aval, fourcc);
      gst_value_list_append_value (&list, &aval);
      gst_structure_set_value (s, "format", &list);
      g_value_reset (&aval);
      gst_caps_append_structure (newcap, s);
      gst_caps_append (allcaps, newcap);

      newcap = gst_caps_new_empty ();
      s = gst_structure_new ("video/x-raw", "height", GST_TYPE_INT_RANGE, 1,
          1080, NULL);
      gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, 1920, NULL);
      fourcc =
          gst_video_format_to_string (get_gst_format (priv->
              dpu_conf->model_format));
      g_value_set_string (&aval, fourcc);
      gst_value_list_append_value (&list, &aval);
      gst_structure_set_value (s, "format", &list);
      g_value_reset (&aval);
      g_value_unset (&list);
      gst_caps_append_structure (newcap, s);
      gst_caps_append (allcaps, newcap);


      if (self->ppe_json_file) {
        GstCaps *templ_caps;
        if (direction == GST_PAD_SRC) {
          templ_caps = gst_pad_get_pad_template_caps (trans->srcpad);
        } else {
          templ_caps = gst_pad_get_pad_template_caps (trans->sinkpad);
        }

        gst_caps_append (allcaps, templ_caps);
      }

      if (filter) {
        GstCaps *tmp = allcaps;
        allcaps =
            gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (tmp);
      }

      {
        gchar *str = gst_caps_to_string (allcaps);
        GST_INFO_OBJECT (self, "returning caps = %s", str);
        g_free (str);
      }

      gst_query_set_caps_result (query, allcaps);
      gst_caps_unref (allcaps);

      return TRUE;
    }

    case GST_QUERY_CUSTOM:{
      const GstStructure *s = gst_query_get_structure (query);
      GstStructure *st;
      if (!gst_structure_has_name (s, "vvas-kernel-config")) {
        break;
      }
      GST_DEBUG_OBJECT (self, "received vvas-kernel-config query");
      query = gst_query_make_writable (query);
      st = gst_query_writable_structure (query);

      if (st) {
        gst_structure_set (st,
            "pp_config", G_TYPE_POINTER, priv->dpu_kernel_config, NULL);
      }
      return TRUE;
    }
      break;

    default:
      ret = TRUE;
      break;
  }

  GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction, query);
  return ret;
}

/**
 *  @fn static gboolean gst_vvas_xinfer_sink_event (GstBaseTransform * trans, GstEvent * event)
 *  @param [in] trans - xinfer's parents instance handle which will be type casted to xinfer instance
 *  @param [in] event - GstEvent received by xinfer instance on sink pad
 *  @return TRUE if event handled successfully
 *          FALSE if event is not handled
 *  @brief Handle the GstEvent and invokes parent's event vmethod if event is not handled by xinfer
 */
static gboolean
gst_vvas_xinfer_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  GstVvas_XInferPrivate *priv = self->priv;

  GST_LOG_OBJECT (self, "received sink event: %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:{
      GST_INFO_OBJECT (self, "received EOS event");
      priv->is_eos = TRUE;

      if (priv->ppe_thread) {
        g_mutex_lock (&priv->ppe_lock);
        if (!priv->ppe_need_data && !priv->stop) {
          /* ppe inbuf is not consumed wait till thread consumes it */
          g_cond_wait (&priv->ppe_need_input, &priv->ppe_lock);
        }

        if (priv->stop) {
          /* return if stop is already generated */
          g_mutex_unlock (&priv->ppe_lock);
          gst_event_unref (event);
          return TRUE;
        }

        memset (priv->ppe_frame, 0x0, sizeof (Vvas_XInferFrame));
        priv->ppe_frame->skip_processing = TRUE;
        priv->ppe_frame->event = event;
        priv->ppe_need_data = FALSE;

        GST_INFO_OBJECT (self, "send event %p to preprocess thread", event);

        g_cond_signal (&priv->ppe_has_input);
        g_mutex_unlock (&priv->ppe_lock);
      } else {
        Vvas_XInferFrame *event_frame = NULL;

        event_frame = g_slice_new0 (Vvas_XInferFrame);
        event_frame->skip_processing = TRUE;
        event_frame->event = event;

        GST_INFO_OBJECT (self, "send event %p to inference thread",
            event_frame);

        g_mutex_lock (&self->priv->infer_lock);
        g_cond_signal (&self->priv->infer_cond);
        GST_INFO_OBJECT (self, "signalled infer thread to exit");
        /* send input frame to inference thread */
        g_queue_push_tail (priv->infer_batch_queue, event_frame);
        g_mutex_unlock (&self->priv->infer_lock);
      }

      /* stuck here untill PPE and INFER thread exits */
      if (priv->infer_thread) {
        GST_DEBUG_OBJECT (self, "waiting for inference thread to exit");
        g_thread_join (priv->infer_thread);
        GST_DEBUG_OBJECT (self, "inference thread exited");
        priv->infer_thread = NULL;
      }
      if (priv->ppe_thread) {
        GST_DEBUG_OBJECT (self, "waiting for ppe thread to exit");
        g_thread_join (priv->ppe_thread);
        GST_DEBUG_OBJECT (self, "ppe thread exited");
        priv->ppe_thread = NULL;
      }
      priv->is_eos = FALSE;
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:{
      /* Got Custom downstream event */
      const GstStructure *structure = gst_event_get_structure (event);
      guint pad_idx;
      Vvas_XInferFrame *event_frame;
      /* Get index of sender pad of this event */
      gst_structure_get_uint (structure, "pad-index", &pad_idx);

      if (!g_strcmp0 (gst_structure_get_name (structure), "pad-eos")) {
        /* Got custom Pad-EOS event */
        GST_DEBUG_OBJECT (self, "Got pad-eos event on pad %u", pad_idx);

        if (priv->ppe_thread) {
          g_mutex_lock (&priv->ppe_lock);
          if (!priv->ppe_need_data && !priv->stop) {
            /* ppe inbuf is not consumed wait till thread consumes it */
            g_cond_wait (&priv->ppe_need_input, &priv->ppe_lock);
          }
          g_mutex_unlock (&priv->ppe_lock);
        }

        event_frame = g_slice_new0 (Vvas_XInferFrame);
        event_frame->event = event;
        event_frame->skip_processing = TRUE;

        GST_INFO_OBJECT (self, "send event %p to inference thread",
            event_frame);

        g_mutex_lock (&self->priv->infer_lock);
        /* send input frame to inference thread */
        priv->is_pad_eos = TRUE;
        g_cond_signal (&self->priv->infer_cond);
        g_queue_push_tail (priv->infer_batch_queue, event_frame);
        g_mutex_unlock (&self->priv->infer_lock);
        return TRUE;
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:{
      GST_INFO_OBJECT (self, "freeing internal queues");
      // TODO: handle flush
      //while (g_queue_get_length (self->priv->infer_batch_queue))
      // gst_buffer_unref (g_queue_pop_head (self->priv->infer_batch_queue));
      break;
    }
    default:
      break;
  }
  GST_LOG_OBJECT (self, "pushing %" GST_PTR_FORMAT, event);
  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

/**
 *  @fn static gboolean gst_vvas_xinfer_propose_allocation (GstBaseTransform * trans, GstQuery * decide_query,
 *							    GstQuery * query)
 *  @param [in] trans - xinfer's parents instance handle which will be type casted to xinfer instance
 *  @param [in] decide_query - Query that was passed to the decide_allocation callback
 *  @param [inout] query - Query received on sink pad from upstream which needs to be updated xinfer requirements
 *  @return TRUE on success
 *          FALSE on failure
 *  @brief Proposes buffer allocation parameters for upstream element
 *  @details Pass the query to downstream and if there is no proposals from downstream then add xinfer proposal
 *           only if pre-process is enable, by setting Video buffer pool, VVAS allocator, alignment required and
 *           buffers required on query
*/
static gboolean
gst_vvas_xinfer_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  GstVvas_XInferPrivate *priv = self->priv;
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool;
  GstVideoAlignment align;
  guint size;

  /* call parent's propose allocation to send query downstream */
  GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
      decide_query, query);

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  /* downstream does not have any proposals or element */
  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstStructure *structure;
    GstAllocator *allocator = NULL;
    GstAllocationParams params = { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0,
      0
    };

    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    } else {
      if (self->priv->do_preprocess && !self->priv->software_ppe) {
        /* create VVAS allocator object to create memory when pre-process is
         * enabled */
        allocator =
            gst_vvas_allocator_new (self->priv->ppe_dev_idx,
            USE_DMABUF, priv->ppe_in_mem_bank);
      } else {
        GST_FIXME_OBJECT (self,
            "need to create allocator based on inference device id");
        /* TODO: currently not aware of api to get device id,
         * so allocator is NULL (ie. system/default gst allocator) */
        allocator = NULL;
        /* if pre-process lib is not available, then same buffer send to infer */
      }
      gst_query_add_allocation_param (query, allocator, &params);
    }

    /* TODO: once vvas sw libs mentions stride information
     * use that info to configure pool */
    if (!priv->software_ppe && GST_IS_VVAS_ALLOCATOR (allocator))
      pool = gst_vvas_buffer_pool_new (PPE_WIDTH_ALIGN, 1);
    else
      pool = gst_video_buffer_pool_new ();

    structure = gst_buffer_pool_get_config (pool);

    /* TODO: check whether propose_allocation will
     * be called after set_caps or not */
    gst_buffer_pool_config_set_params (structure, caps, size,
        /* one extra for preprocessing */
        self->priv->infer_batch_size + 1, 0);

    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    gst_video_alignment_reset (&align);
    for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&info); idx++) {
      align.stride_align[idx] = (PPE_WIDTH_ALIGN - 1);
    }

    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (structure, &align);

    if (allocator)
      gst_buffer_pool_config_set_allocator (structure, allocator, &params);

    if (!gst_buffer_pool_set_config (pool, structure))
      goto config_failed;

    GST_OBJECT_LOCK (self);
    gst_query_add_allocation_pool (query, pool, size,
        self->priv->infer_batch_size + 1, 0);
    GST_OBJECT_UNLOCK (self);

    GST_INFO_OBJECT (self, "allocated internal pool %" GST_PTR_FORMAT, pool);

    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

    GST_DEBUG_OBJECT (self, "prepared query %" GST_PTR_FORMAT, query);

    if (allocator)
      gst_object_unref (allocator);
    if (pool)
      gst_object_unref (pool);
  }

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (self, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }
}

/** @def MIN_SCALAR_INPUT_WIDTH
 *  @brief Set minimum width requirement of HW scaler
 */
#define MIN_SCALAR_INPUT_WIDTH 64

/** @def MIN_SCALAR_INPUT_HEIGHT
 *  @brief Set minimum height requirement of HW scaler
 */
#define MIN_SCALAR_INPUT_HEIGHT 64

/**
 *  @fn static gboolean gst_vvas_xinfer_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
 *  @param [in] trans - xinfer's parents instance handle which will be type casted to xinfer instance
 *  @param [in] incaps - Input capabilities configured on xinfer instance sink pad
 *  @param [in] outcaps - Output capabilities configured on xinfer instance source pad
 *  @return TRUE on success
 *          FALSE on failure
 *  @brief  Stores input and output capabilities in xinfer's private structure
 *  @detail Stores input caps in xinfer's private structure and create output caps for PPE. The PPE output
 *          caps is decided as per infer requirement and also PPE output buffer pool is created taking
 *          consideration of stride requirement of scaler HW.
 */
static gboolean
gst_vvas_xinfer_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  gboolean bret = TRUE;
  GstVvas_XInferPrivate *priv = self->priv;
  GstStructure *structure;
  const gchar *format;
  gint32 max_width, stride_align;
  gfloat max_scale_factor;
  gint32 max_height;

  GST_INFO_OBJECT (self,
      "incaps = %" GST_PTR_FORMAT " and outcaps = %" GST_PTR_FORMAT, incaps,
      outcaps);

  if (!gst_video_info_from_caps (priv->in_vinfo, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse input caps");
    return FALSE;
  }


  priv->pref_infer_width = priv->model_conf.model_width;
  priv->pref_infer_height = priv->model_conf.model_height;
  priv->pref_infer_format = get_gst_format (priv->dpu_conf->model_format);
  format = gst_video_format_to_string (priv->pref_infer_format);

  GST_INFO_OBJECT (self,
      "inference preferred caps : width = %d, height = %d, format = %s",
      priv->pref_infer_width, priv->pref_infer_height, format);

  if (self->ppe_json_file) {
    GstCaps *ppe_out_caps = NULL;
    gchar *caps_str = NULL;
    gint width, height;
    GstAllocator *allocator = NULL;
    GstAllocationParams params =
        { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0, 0 };
    gsize size;


    width = priv->pref_infer_width;
    height = priv->pref_infer_height;
    format =
        gst_video_format_to_string (get_gst_format (priv->
            dpu_conf->model_format));

    /* Changing width according to worst case scenario */
    stride_align = 8 * priv->ppc;
    max_scale_factor = ((gfloat) width) / MIN_SCALAR_INPUT_WIDTH;
    max_width = (gint32) ((((stride_align - 1) + MIN_SCALAR_INPUT_WIDTH +
                (priv->ppc - 1)) * max_scale_factor) + 1.0);
    max_width = ALIGN (max_width, priv->ppc);

    //3 rows on top for handling croma and 1 at bottom for even number of height
    max_scale_factor = ((gfloat) height) / MIN_SCALAR_INPUT_HEIGHT;
    max_height =
        (gint32) (((MIN_SCALAR_INPUT_HEIGHT + 4) * max_scale_factor) + 1.0);
    max_height = ALIGN (max_height, 2);

    ppe_out_caps = gst_caps_new_simple ("video/x-raw",
        "width", G_TYPE_INT, width, "height", G_TYPE_INT, height,
        "format", G_TYPE_STRING, format, NULL);

    caps_str = gst_caps_to_string (ppe_out_caps);
    GST_INFO_OBJECT (self, "pre processing output caps %s", caps_str);
    g_free (caps_str);

    if (!gst_video_info_from_caps (priv->ppe_out_vinfo, ppe_out_caps)) {
      GST_ERROR_OBJECT (self, "Failed to parse ppe out caps");
      if (ppe_out_caps)
        gst_caps_unref (ppe_out_caps);
      return FALSE;
    }

    if (self->ppe_json_file) {
      if (priv->param.type == VVAS_SCALER_ENVELOPE_CROPPED) {
        if (strstr (priv->dpu_conf->model_name, "efficientnet")) {
          priv->param.smallest_side_num = 256;
        } else {
          priv->param.smallest_side_num =
              (priv->ppe_out_vinfo->height <
              priv->ppe_out_vinfo->width ? priv->ppe_out_vinfo->
              height : priv->ppe_out_vinfo->width);
        }
      }
    }

    switch (get_gst_format (priv->dpu_conf->model_format)) {
      case GST_VIDEO_FORMAT_GRAY8:
        size = ALIGN (max_width, stride_align);
        break;
      case GST_VIDEO_FORMAT_NV12:
        size = ALIGN ((max_width + (max_width >> 1)), stride_align);
        break;
      case GST_VIDEO_FORMAT_BGR:
      case GST_VIDEO_FORMAT_RGB:
        size = ALIGN (max_width * 3, stride_align);
        break;
      default:
        size = ALIGN (max_width * 3, stride_align);
        break;
    }

    size = size * max_height;

    if (priv->ppe_outpool) {
      if (gst_buffer_pool_is_active (priv->ppe_outpool)) {
        if (!gst_buffer_pool_set_active (priv->ppe_outpool, FALSE)) {
          GST_ERROR_OBJECT (self,
              "failed to deactivate preprocess output pool");
          GST_ELEMENT_ERROR (self, STREAM, FAILED,
              ("failed to deactivate pool."),
              ("failed to deactivate preprocess output pool"));
          return FALSE;
        }
      }
      gst_object_unref (priv->ppe_outpool);
      priv->ppe_outpool = NULL;
    }

    if (priv->software_ppe) {
      priv->ppe_outpool = gst_video_buffer_pool_new ();

      structure = gst_buffer_pool_get_config (priv->ppe_outpool);

      gst_buffer_pool_config_set_params (structure, ppe_out_caps, size,
          self->priv->infer_batch_size, 0);
      gst_buffer_pool_config_add_option (structure,
          GST_BUFFER_POOL_OPTION_VIDEO_META);

      GST_LOG_OBJECT (self, "allocated preprocess output pool %" GST_PTR_FORMAT,
          priv->ppe_outpool);

    } else {
      GstVideoAlignment align;
      GstVideoInfo ppe_out_info;
      priv->ppe_outpool = gst_vvas_buffer_pool_new (PPE_WIDTH_ALIGN, 1);

      allocator = gst_vvas_allocator_new_and_set (self->priv->ppe_dev_idx,
          USE_DMABUF, priv->ppe_out_mem_bank, priv->init_value);
      params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
      params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;

      GST_LOG_OBJECT (self, "allocated preprocess output pool %" GST_PTR_FORMAT
          "output allocator %" GST_PTR_FORMAT, priv->ppe_outpool, allocator);

      structure = gst_buffer_pool_get_config (priv->ppe_outpool);

      gst_video_alignment_reset (&align);
      gst_video_info_from_caps (&ppe_out_info, ppe_out_caps);
      for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&ppe_out_info); idx++) {
        align.stride_align[idx] = (PPE_WIDTH_ALIGN - 1);
      }

      gst_buffer_pool_config_add_option (structure,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
      gst_buffer_pool_config_set_video_alignment (structure, &align);

      /* set the max size required as calculated above */
      gst_buffer_pool_config_set_params (structure, ppe_out_caps, size,
          self->priv->infer_batch_size, 0);
      gst_buffer_pool_config_add_option (structure,
          GST_BUFFER_POOL_OPTION_VIDEO_META);
      gst_buffer_pool_config_set_allocator (structure, allocator, &params);

      if (allocator)
        gst_object_unref (allocator);
    }

    if (ppe_out_caps)
      gst_caps_unref (ppe_out_caps);

    if (!gst_buffer_pool_set_config (priv->ppe_outpool, structure)) {
      GST_ERROR_OBJECT (self, "failed to configure pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to configure pool."),
          ("failed to configure preprocess output pool"));
      return FALSE;
    }

    if (!gst_buffer_pool_set_active (priv->ppe_outpool, TRUE)) {
      GST_ERROR_OBJECT (self, "failed to activate preprocess output pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to activate pool."),
          ("failed to activate preprocess output pool"));
      return FALSE;
    }
  } else {
    const gchar *infer_format = NULL;
    const gchar *in_format = NULL;
    GstStructure *structure;

    /* TODO: loop through different formats accepted by DPU */
    infer_format =
        gst_video_format_to_string (get_gst_format (priv->
            dpu_conf->model_format));
    structure = gst_caps_get_structure (incaps, 0);
    in_format = gst_structure_get_string (structure, "format");

    if (g_strcmp0 (infer_format, in_format)) {
      GST_ERROR_OBJECT (self, "input format %s is not acceptable for inference",
          in_format);
      return FALSE;
    }
  }

  return bret;
}

/**
 *  @fn static gboolean gst_vvas_xinfer_stop (GstBaseTransform * trans)
 *  @param [in] - trans xinfer's parents instance handle which will be type casted to xfilter instance
 *  @return TRUE on success
 *          FALSE on failure
 *  @brief This API will be invoked by parent class (i.e. GstBaseTranform) before stop processing
 *         frames to free up resources
 *  @detail This function broadcast signals to PPE and INFER thread to exit and wait till
 *          both thread exit. De-initialization of INFER and PPE is also done after both thread exits.
 */
static gboolean
gst_vvas_xinfer_stop (GstBaseTransform * trans)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  GST_INFO_OBJECT (self, "stopping");
  self->priv->stop = TRUE;

  gst_video_info_free (self->priv->in_vinfo);

  if (self->priv->infer_thread) {
    g_mutex_lock (&self->priv->infer_lock);
    g_cond_broadcast (&self->priv->infer_cond);
    g_cond_broadcast (&self->priv->infer_batch_full);
    GST_INFO_OBJECT (self, "signalled infer thread to exit");
    g_mutex_unlock (&self->priv->infer_lock);
  }

  if (self->priv->ppe_thread) {
    g_mutex_lock (&self->priv->ppe_lock);
    self->priv->ppe_need_data = FALSE;
    g_cond_broadcast (&self->priv->ppe_has_input);
    g_cond_broadcast (&self->priv->ppe_need_input);
    GST_INFO_OBJECT (self, "signalled ppe thread to exit");
    g_mutex_unlock (&self->priv->ppe_lock);
  }

  if (self->priv->infer_thread) {
    GST_DEBUG_OBJECT (self, "waiting for inference thread to exit");
    g_thread_join (self->priv->infer_thread);
    GST_DEBUG_OBJECT (self, "inference thread exited. sending EOS downstream");
    self->priv->infer_thread = NULL;
  }

  if (self->priv->ppe_thread) {
    GST_DEBUG_OBJECT (self, "waiting for inference thread to exit");
    g_thread_join (self->priv->ppe_thread);
    GST_DEBUG_OBJECT (self, "inference thread exited");
    self->priv->ppe_thread = NULL;
  }
#ifdef DUMP_INFER_INPUT
  if (self->priv->fp)
    fclose (self->priv->fp);
#endif
  vvas_xinfer_infer_deinit (self);

  if (self->ppe_json_file)
    vvas_xinfer_ppe_deinit (self);

  if (self->priv->do_postprocess)
    vvas_xinfer_postproc_deinit (self);

  self->priv->do_init = TRUE;

  return TRUE;
}

/**
 *  @fn static void gst_vvas_xinfer_finalize (GObject * obj)
 *  @param [in] GObject - which will typecast to GstVvas_XInfer
 *  @return None
 *  @brief This API will be called during GstVvas_XInfer object's destruction phase.
 *         Close references to devices and free memories if any
 */
static void
gst_vvas_xinfer_finalize (GObject * obj)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (obj);

  if (self->priv->input_pool)
    gst_object_unref (self->priv->input_pool);

  if (self->ppe_json_file) {
    g_free (self->ppe_json_file);
    self->ppe_json_file = NULL;
  }

  if (self->infer_json_file) {
    g_free (self->infer_json_file);
    self->infer_json_file = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GstStateChangeReturn
gst_vvas_xinfer_change_state (GstElement * element, GstStateChange transition)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      if (!gst_vvas_xinfer_start (GST_BASE_TRANSFORM_CAST (element))) {
        GST_ERROR_OBJECT (self, "failed to do initialization");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:{
      if (!gst_vvas_xinfer_stop (GST_BASE_TRANSFORM_CAST (element))) {
        GST_ERROR_OBJECT (self, "failed to do de-initialization");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    default:
      break;
  }

  return ret;
}

/**
 *  @fn static void gst_vvas_xinfer_class_init (GstVvas_XInferClass * klass)
 *  @param [in] klass - Handle to GstVvas_XInferClass
 *  @return None
 *  @brief  Add properties and signals of GstVvas_XInfer to parent GObjectClass and ovverrides function
 *          pointers present in itself and/or its parent class structures
 *  @details This function publishes properties those can be set/get from application on
 *           GstVvas_XInfer object. And, while publishing a property it also declares type,
 *           range of acceptable values, default value, readability/writability and in which
 *           GStreamer state a property can be changed.
 */
static void
gst_vvas_xinfer_class_init (GstVvas_XInferClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *transform_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_vvas_xinfer_set_property;
  gobject_class->get_property = gst_vvas_xinfer_get_property;
  gobject_class->finalize = gst_vvas_xinfer_finalize;
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vvas_xinfer_change_state);

  transform_class->set_caps = gst_vvas_xinfer_set_caps;
  transform_class->query = gst_vvas_xinfer_query;
  transform_class->sink_event = gst_vvas_xinfer_sink_event;
  transform_class->propose_allocation = gst_vvas_xinfer_propose_allocation;
  transform_class->submit_input_buffer = gst_vvas_xinfer_submit_input_buffer;
  transform_class->generate_output = gst_vvas_xinfer_generate_output;

  g_object_class_install_property (gobject_class, PROP_PPE_CONFIG_LOCATION,
      g_param_spec_string ("preprocess-config",
          "Preprocessing library json file path",
          "Location of the preprocessing config file in json format "
          "(Note : Changable only in NULL state)", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INFER_CONFIG_LOCATION,
      g_param_spec_string ("infer-config",
          "Inference library json file path",
          "Location of the inference config file in json format "
          "(Note : Changable only in NULL state)", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ATTACH_EMPTY_METADATA,
      g_param_spec_boolean ("attach-empty-metadata",
          "Attaching empty metadata when there is no infer data",
          "Flag to decide attaching empty metadata strucutre when there is no infer",
          DEFAULT_ATTACH_EMPTY_METADATA, (GParamFlags) (G_PARAM_READWRITE
              | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BATCH_SUBMIT_TIMEOUT,
      g_param_spec_uint ("batch-timeout",
          "timeout in milliseconds",
          "time (in milliseconds) to wait when batch is not full, before pushing batch of frames for inference"
          "By default infer waits indefinitely for batch to be formed", 0,
          UINT_MAX, DEFAULT_BATCH_SUBMIT_TIMEOUT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple (gstelement_class,
      "VVAS Generic Filter Plugin",
      "Filter/Effect/Video",
      "Performs operations on HW IP/SW IP/Softkernel using VVAS library APIs",
      "Xilinx Inc <www.xilinx.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  /*
   * Will be emitted when kernel is successfully done.
   */
  vvas_signals[SIGNAL_VVAS] =
      g_signal_new ("vvas-kernel-done", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xinfer_debug, "vvas_xinfer", 0,
      "VVAS Inference plugin");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");

  _scale_quark = gst_video_meta_transform_scale_get_quark ();
  _copy_quark = g_quark_from_static_string ("gst-copy");
}

/**
 *  @fn static void gst_vvas_xinfer_init (GstVvas_XInfer * self)
 *  @param [in] self - Handle to GstVvas_XInfer instance
 *  @return None
 *  @brief  Initializes GstVvas_XInfer member variables to default.
 *          Also set pass-through and in_place mode for this filter by default
 */
static void
gst_vvas_xinfer_init (GstVvas_XInfer * self)
{
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (self);
  GstVvas_XInferPrivate *priv = GST_VVAS_XINFER_PRIVATE (self);

  self->priv = priv;
  priv->ppe_dev_idx = DEFAULT_DEVICE_INDEX;
  priv->do_init = TRUE;
  priv->do_preprocess = FALSE;
  priv->is_error = FALSE;
  self->flag_attach_empty_infer = DEFAULT_ATTACH_EMPTY_METADATA;
  self->batch_timeout = DEFAULT_BATCH_SUBMIT_TIMEOUT;

  priv->last_fret = GST_FLOW_OK;
  priv->dpu_kernel_config = NULL;
#ifdef DUMP_INFER_INPUT
  priv->fp = NULL;
#endif

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (btrans), TRUE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (btrans), TRUE);
}

/**
 *  @fn static gboolean plugin_init (GstPlugin * vvas_xinfer)
 *  @param [in] vvas_xinfer - Handle to plugin to register with GStreamer core
 *  @return TRUE on success
 *          FALSE on failure
 *  @brief Registers xinfer plugin with GStreamer core
 */
static gboolean
plugin_init (GstPlugin * vvas_xinfer)
{
  return gst_element_register (vvas_xinfer, "vvas_xinfer", GST_RANK_PRIMARY,
      GST_TYPE_VVAS_XINFER);
}

/**
 *  @def GST_PLUGIN_DEFINE
 *  @param [in] GST_VERSION_MAJOR - GStreamer major version with which xinfer is compiled
 *  @param [in] GST_VERSION_MINOR - GStreamer minor version with which xinfer is compiled
 *  @param [in] vvas_xinfer - Plugin name to be registered with GStreamer core
 *  @param [in] description - Purpose of the plugin
 *  @param [in] plugin_init - function pointer to the plugin_init method to be called to register xinfer
 *  @param [in] Version - of the plugin
 *  @param [in] Licence - of the plugin
 *  @param [in] Package - name
 *  @param [in] Package - Origin
 *  @return TRUE on success
 *          FALSE on failure
 *  @brief Entry point and meta data of a plugin to be exported to application
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xinfer,
    "GStreamer VVAS plug-in for filters", plugin_init, VVAS_API_VERSION,
    "MIT/X11", "Xilinx VVAS SDK plugin", "http://xilinx.com/")
