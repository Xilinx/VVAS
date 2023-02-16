#ifndef __VVAS_GST_CORE_UTILS__
#define __VVAS_GST_CORE_UTILS__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/vvas/gstvvasallocator.h>
#include <gst/allocators/gstdmabuf.h>
#include <vvas_core/vvas_memory.h>
#include <vvas_core/vvas_memory_priv.h>
#include <vvas_core/vvas_video.h>
#include <vvas_core/vvas_video_priv.h>
#include <vvas_core/vvas_infer_prediction.h>
#include <vvas_core/vvas_infer_classification.h>
#include "gstinferenceprediction.h"

#define DEFAULT_DEBUG_LOG_LEVEL LOG_LEVEL_ERROR
#ifdef __cplusplus
extern "C"
{
#endif

GST_EXPORT
VvasLogLevel vvas_get_core_log_level (GstDebugLevel gst_level);

GST_EXPORT
VvasMemory * vvas_memory_from_gstbuffer (VvasContext * vvas_ctx, uint8_t mbank_idx, GstBuffer * buf);

GST_EXPORT 
VvasVideoFrame *vvas_videoframe_from_gstbuffer (VvasContext *vvas_ctx,
                                                int8_t mbank_idx, GstBuffer * buf, GstVideoInfo * gst_vinfo,
                                                GstMapFlags flags);

GST_EXPORT
VvasInferPrediction * vvas_infer_from_gstinfer (GstInferencePrediction *pred);

GST_EXPORT
VvasList * vvas_inferprediction_get_nodes (VvasInferPrediction * self);

GST_EXPORT
GstInferencePrediction * gst_infer_node_from_vvas_infer (VvasInferPrediction * vinfer);

GST_EXPORT
GstVideoFormat gst_coreutils_get_gst_fmt_from_vvas (VvasVideoFormat format);

#ifdef __cplusplus
}
#endif
#endif
