/*
 * Copyright 2020 Xilinx, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * file: vvas_xdpuinfer.cpp
 *
 * vvas_xdpuinfer is the dynamic library used with gstreamer vvas filter
 * plugins to have a generic interface for Xilinx DPU Library and applications.
 * vvas_xdpuinfer supports different models of DPU based on model class.
 * Any new class can be added with minimal effort.
 *
 * Example json file parameters required for vvas_xdpuinfer
 * {
 * "xclbin-location":"/usr/lib/dpu.xclbin",
 * "vvas-library-repo": "/usr/local/lib/vvas/",
 * "element-mode":"inplace",
 * "kernels" :[
 *  {
 *    "library-name":"libvvas_xdpuinfer.so",
 *    "config": {
 *      "model-name" : "resnet50",
 *      "model-class" : "CLASSIFICATION",
 *      "model-path" : "/usr/share/vitis_ai_library/models/",
 *      "run_time_model" : flase,
 *      "need_preprocess" : true,
 *      "performance_test" : true,
 *      "debug_level" : 1
 *    }
 *   }
 *  ]
 * }
 *
 * Details of above parametres id under "struct vvas_xkpriv"
 *
 * Example pipe:
 * gst-launch-1.0 filesrc location="./images/001.bgr" blocksize=150528 num-buffers=1 !  \
 * videoparse width=224 height=224 framerate=30/1 format=16 ! \
 * vvas_xfilter name="kernel1" kernels-config="./json_files/kernel_resnet50.json" ! \
 * vvas_xfilter name="kernel2" kernels-config="./json_files/kernel_testresnet50.json" ! \
 * filesink location=./resnet_output_224_224.bgr
 */

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <fstream>

#include <vitis/ai/bounded_queue.hpp>
#include <vitis/ai/env_config.hpp>

#define BATCH_SIZE_ZERO 0

extern "C"
{
#include <vvas/vvas_kernel.h>
}
#include <gst/vvas/gstinferencemeta.h>
#include <gst/vvas/gstvvasinpinfer.h>

#include "vvas_xdpupriv.hpp"
#include "vvas_xdpumodels.hpp"

#ifdef ENABLE_CLASSIFICATION
#include "vvas_xclassification.hpp"
#endif
#ifdef ENABLE_YOLOV3
#include "vvas_xyolov3.hpp"
#endif
#ifdef ENABLE_FACEDETECT
#include "vvas_xfacedetect.hpp"
#endif
#ifdef ENABLE_REID
#include "vvas_xreid.hpp"
#endif
#ifdef ENABLE_SSD
#include "vvas_xssd.hpp"
#endif
#ifdef ENABLE_REFINEDET
#include "vvas_xrefinedet.hpp"
#endif
#ifdef ENABLE_TFSSD
#include "vvas_xtfssd.hpp"
#endif
#ifdef ENABLE_YOLOV2
#include "vvas_xyolov2.hpp"
#endif
#ifdef ENABLE_SEGMENTATION
#include "vvas_xsegmentation.hpp"
#endif
#ifdef ENABLE_PLATEDETECT
#include "vvas_xplatedetect.hpp"
#endif
#ifdef ENABLE_PLATENUM
#include "vvas_xplatenum.hpp"
#endif

using namespace cv;
using namespace std;

vvas_xdpumodel::~vvas_xdpumodel ()
{
}

/**
 * fileexists () - Check either file exists or not
 *
 * check either able to open the file whoes path is in name
 *
 */
inline bool
fileexists (const string & name)
{
  struct stat buffer;
  return (stat (name.c_str (), &buffer) == 0);
}

/**
 * modelexits () - Validate model paths and model files names
 *
 */
static
    std::string
modelexits (vvas_xkpriv * kpriv)
{
  auto elf_name =
      kpriv->modelpath + "/" + kpriv->modelname + "/" + kpriv->modelname +
      ".elf";
  auto xmodel_name =
      kpriv->modelpath + "/" + kpriv->modelname + "/" + kpriv->modelname +
      ".xmodel";
  auto prototxt_name =
      kpriv->modelpath + "/" + kpriv->modelname + "/" + kpriv->modelname +
      ".prototxt";

  if (!fileexists (prototxt_name)) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "%s not found",
        prototxt_name.c_str ());
    elf_name = "";
    return elf_name;
  }

  if (fileexists (xmodel_name))
    return xmodel_name;
  else if (fileexists (elf_name))
    return elf_name;
  else {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "xmodel or elf file not found");
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "%s", elf_name.c_str ());
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "%s", xmodel_name.c_str ());
    elf_name = "";
  }

  return elf_name;
}

/**
 * readlabel () - Read label from json file
 *
 * Read labels and construct the label array
 * used by class files to fill meta data for label.
 *
 */
labels *
readlabel (vvas_xkpriv * kpriv, char *json_file)
{
  json_t *root = NULL, *karray, *label, *value;
  json_error_t error;
  unsigned int num_labels;
  labels *labelptr = NULL;

  /* get root json object */
  root = json_load_file (json_file, JSON_DECODE_ANY, &error);
  if (!root) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "failed to load json file(%s) reason %s", json_file, error.text);
    return NULL;
  }

  value = json_object_get (root, "model-name");
  if (json_is_string (value)) {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "label is for model %s",
        (char *) json_string_value (value));
  }

  value = json_object_get (root, "num-labels");
  if (!value || !json_is_integer (value)) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "num-labels not found in %s", json_file);
    goto error;
  } else {
    num_labels = json_integer_value (value);
    labelptr = (labels *) calloc (num_labels, sizeof (labels));
    kpriv->max_labels = num_labels;
  }

  /* get kernels array */
  karray = json_object_get (root, "labels");
  if (!karray) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "failed to find key labels");
    goto error;
  }

  if (!json_is_array (karray)) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "labels key is not of array type");
    goto error;
  }

  if (num_labels != json_array_size (karray)) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "number of labels(%u) != karray size(%lu)\n", num_labels,
        json_array_size (karray));
    goto error;
  }

  for (unsigned int index = 0; index < num_labels; index++) {
    label = json_array_get (karray, index);
    if (!label) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "failed to get label object");
      goto error;
    }
    value = json_object_get (label, "label");
    if (!value || !json_is_integer (value)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "label num found for array %d", index);
      goto error;
    }

    /*label is index of array */
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "label %d",
        (int) json_integer_value (value));
    labels *lptr = labelptr + (int) json_integer_value (value);
    lptr->label = (int) json_integer_value (value);

    value = json_object_get (label, "name");
    if (!json_is_string (value)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "name is not found for array %d", index);
      goto error;
    } else {
      lptr->name = (char *) json_string_value (value);
      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "name %s",
          lptr->name.c_str ());
    }
    value = json_object_get (label, "display_name");
    if (!json_is_string (value)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "display name is not found for array %d", index);
      goto error;
    } else {
      lptr->display_name = (char *) json_string_value (value);
      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "display_name %s",
          lptr->display_name.c_str ());
    }

  }
  kpriv->num_labels = num_labels;
  return labelptr;
error:
  if (labelptr)
    free (labelptr);
  return NULL;
}

int
vvas_xclass_to_num (char *name)
{
  int nameslen = 0;
  while (vvas_xmodelclass[nameslen] != NULL) {
    if (!strcmp (vvas_xmodelclass[nameslen], name))
      return nameslen;
    nameslen++;
  }
  return VVAS_XCLASS_NOTFOUND;
}

VVASVideoFormat
vvas_fmt_to_xfmt (char *name)
{
  if (!strncmp (name, "RGB", 3))
    return VVAS_VFMT_RGB8;
  else if (!strncmp (name, "BGR", 3))
    return VVAS_VFMT_BGR8;
  else if (!strncmp (name, "GRAY8", 5))
    return VVAS_VFMT_Y8;
  else
    return VVAS_VMFT_UNKNOWN;
}


long long
get_time ()
{
  struct timeval tv;
  gettimeofday (&tv, NULL);
  return ((long long) tv.tv_sec * 1000000 + tv.tv_usec) +
      42 * 60 * 60 * INT64_C (1000000);
}

/**
 * vvas_xsetcaps() - Create and set capability of the DPU
 *
 * DPU works in pass through mode so only Sink pads are created by function.
 * The model supported width and height is at cap[0],
 * which means vvas_xdpuinfer first preference for negotiation.
 * Then next caps will support range 1 to 1024  and BGR and RGB,
 * which means upstream plugin can work within this range and
 * DPU library will do scaling.
 */

int
vvas_xsetcaps (vvas_xkpriv * kpriv, vvas_xdpumodel * model)
{
  kernelcaps *new_caps;
  VVASKernel *handle = kpriv->handle;

  vvas_caps_set_pad_nature (handle, VVAS_PAD_RIGID);

  LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
      "Model required width = %d and height = %d",
      model->requiredwidth (), model->requiredheight ());

  new_caps =
      vvas_caps_new (false, model->requiredheight (), 0, false,
      model->requiredwidth (), 0, kpriv->modelfmt, 0);
  if (!new_caps) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "failed to create sink caps");
    return false;
  }
  if (vvas_caps_add_to_sink (handle, new_caps, 0) == false) {
    vvas_caps_free (handle);
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "failed to add sink caps");
    return false;
  }
  new_caps =
      vvas_caps_new (true, 1, 1080, true, 1, 1920, kpriv->modelfmt, 0);
  if (!new_caps) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "failed to create range sink caps");
    return false;
  }
  if (vvas_caps_add_to_sink (handle, new_caps, 0) == false) {
    vvas_caps_free (handle);
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "failed to add sink caps");
    return false;
  }

  if (kpriv->log_level == LOG_LEVEL_DEBUG)
    vvas_caps_print (handle);

  return true;
}

#if 0
int
vvas_xsetcaps (vvas_xkpriv * kpriv, vvas_xdpumodel * model)
{
  VVASKernel *handle = kpriv->handle;

  vvaspads *padinfo = (vvaspads *) calloc (1, sizeof (vvaspads));

  padinfo->nature = VVAS_PAD_RIGID;
  //padinfo->nature = VVAS_PAD_FLEXIBLE;
  padinfo->nu_sinkpad = 1;
  padinfo->nu_srcpad = 1;

  kernelpads **sinkpads = (kernelpads **) calloc (1, sizeof (kernelpads *));
  /* Create memory of all sink pad */
  for (int i = 0; i < padinfo->nu_sinkpad; i++) {
    sinkpads[i] = (kernelpads *) calloc (1, sizeof (kernelpads));
    sinkpads[i]->nu_caps = 2;
    sinkpads[i]->kcaps = (kernelcaps **) calloc (sinkpads[i]->nu_caps,
        sizeof (kernelcaps *));
    /* Create memory for all caps */
    for (int j = 0; j < sinkpads[i]->nu_caps; j++) {
      sinkpads[i]->kcaps[j] = (kernelcaps *) calloc (1, sizeof (kernelcaps));
    }                           //sinkpad[i]->nu_caps

    /*Fill all caps */
    sinkpads[i]->kcaps[0]->range_height = false;
    sinkpads[i]->kcaps[0]->lower_height = model->requiredheight ();
    sinkpads[i]->kcaps[0]->lower_width = model->requiredwidth ();
    sinkpads[i]->kcaps[0]->num_fmt = 1;
    sinkpads[i]->kcaps[0]->fmt =
        (VVASVideoFormat *) calloc (sinkpads[i]->kcaps[0]->num_fmt,
        sizeof (VVASVideoFormat));
    sinkpads[i]->kcaps[0]->fmt[0] = VVAS_VFMT_BGR8;

    sinkpads[i]->kcaps[1]->range_height = true;
    sinkpads[i]->kcaps[1]->lower_height = 1;
    sinkpads[i]->kcaps[1]->upper_height = 1024;

    sinkpads[i]->kcaps[1]->range_width = true;
    sinkpads[i]->kcaps[1]->lower_width = 1;
    sinkpads[i]->kcaps[1]->upper_width = 1920;
    sinkpads[i]->kcaps[1]->num_fmt = 2;

    sinkpads[i]->kcaps[1]->fmt =
        (VVASVideoFormat *) calloc (sinkpads[i]->kcaps[1]->num_fmt,
        sizeof (VVASVideoFormat));
    sinkpads[i]->kcaps[1]->fmt[0] = VVAS_VFMT_BGR8;
    sinkpads[i]->kcaps[1]->fmt[1] = VVAS_VFMT_RGB8;

#if 0
    /* Just for referance */
    sinkpads[i]->kcaps[2]->range_height = true;
    sinkpads[i]->kcaps[2]->lower_height = 1;
    sinkpads[i]->kcaps[2]->upper_height = 1024;
    sinkpads[i]->kcaps[2]->lower_width = 1;
    sinkpads[i]->kcaps[2]->upper_width = 1920;
    sinkpads[i]->kcaps[2]->fmt = VVAS_VFMT_RGB8;
#endif
  }                             //padinfo->nu_sinkpad

  padinfo->sinkpads = sinkpads;
  handle->padinfo = padinfo;

  return true;
}
#endif
/**
 * vvas_xinitmodel() - Initialize the required models
 *
 * This function calls the constructor of the CLASS provided in the json file
 * and calls create () of the dpu library of respective model.
 * Along with that it check the return from constructor either
 * label file is needed or not.
 */
vvas_xdpumodel *
vvas_xinitmodel (vvas_xkpriv * kpriv, int modelclass)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");
  vvas_xdpumodel *model = NULL;
  kpriv->labelptr = NULL;
  kpriv->labelflags = VVAS_XLABEL_NOT_REQUIRED;

  LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "Creating model %s",
      kpriv->modelname.c_str ());

  const auto labelfile =
      kpriv->modelpath + "/" + kpriv->modelname + "/" + "label.json";
  if (fileexists (labelfile)) {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
        "Label file %s found\n", labelfile.c_str ());
    kpriv->labelptr = readlabel (kpriv, (char *) labelfile.c_str ());
  }

  switch (modelclass) {
#ifdef ENABLE_CLASSIFICATION
    case VVAS_XCLASS_CLASSIFICATION:
    {
      model =
          new vvas_xclassification (kpriv, kpriv->elfname,
          kpriv->need_preprocess);
      break;
    }
#endif
#ifdef ENABLE_YOLOV3
    case VVAS_XCLASS_YOLOV3:
    {
      model = new vvas_xyolov3 (kpriv, kpriv->elfname, kpriv->need_preprocess);
      break;
    }
#endif
#ifdef ENABLE_FACEDETECT
    case VVAS_XCLASS_FACEDETECT:
    {
      model =
          new vvas_xfacedetect (kpriv, kpriv->elfname, kpriv->need_preprocess);
      break;
    }
#endif
#ifdef ENABLE_REID
    case VVAS_XCLASS_REID:
    {
      model = new vvas_xreid (kpriv, kpriv->elfname, kpriv->need_preprocess);
      break;
    }
#endif
#ifdef ENABLE_SSD
    case VVAS_XCLASS_SSD:
    {
      model = new vvas_xssd (kpriv, kpriv->elfname, kpriv->need_preprocess);
      break;
    }
#endif
#ifdef ENABLE_REFINEDET
    case VVAS_XCLASS_REFINEDET:
    {
      model =
          new vvas_xrefinedet (kpriv, kpriv->elfname, kpriv->need_preprocess);
      break;
    }
#endif
#ifdef ENABLE_TFSSD
    case VVAS_XCLASS_TFSSD:
    {
      model = new vvas_xtfssd (kpriv, kpriv->elfname, kpriv->need_preprocess);
      break;
    }
#endif
#ifdef ENABLE_YOLOV2
    case VVAS_XCLASS_YOLOV2:
    {
      model = new vvas_xyolov2 (kpriv, kpriv->elfname, kpriv->need_preprocess);
      break;
    }
#endif
#ifdef ENABLE_SEGMENTATION
    case VVAS_XCLASS_SEGMENTATION:
    {
      model = new vvas_xsegmentation (kpriv, kpriv->elfname, kpriv->need_preprocess);
      break;
    }
#endif
#ifdef ENABLE_PLATEDETECT
    case VVAS_XCLASS_PLATEDETECT:
    {
      model = new vvas_xplatedetect (kpriv, kpriv->elfname, kpriv->need_preprocess);
      break;
    }
#endif
#ifdef ENABLE_PLATENUM
    case VVAS_XCLASS_PLATENUM:
    {
      model = new vvas_xplatenum (kpriv, kpriv->elfname, kpriv->need_preprocess);
      break;
    }
#endif

    default:
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "Not supported model");
      free (kpriv);
      return NULL;
  }

  if ((kpriv->labelflags & VVAS_XLABEL_REQUIRED)
      && (kpriv->labelflags & VVAS_XLABEL_NOT_FOUND)) {
    kpriv->model->close ();
    delete kpriv->model;
    kpriv->model = NULL;
    kpriv->modelclass = VVAS_XCLASS_NOTFOUND;

    if (kpriv->labelptr != NULL)
      free (kpriv->labelptr);

    return NULL;
  }

  vvas_xsetcaps (kpriv, model);

  /* get suported batch from model */
  kpriv->handle->kernel_batch_sz = model->supportedbatchsz();
  LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
		"model supported batch size (%ld)"
		"batch size set by user (%d)",
		kpriv->handle->kernel_batch_sz,
		kpriv->batch_size);
  if (kpriv->batch_size == BATCH_SIZE_ZERO ||
      kpriv->batch_size > kpriv->handle->kernel_batch_sz) {

    kpriv->batch_size = kpriv->handle->kernel_batch_sz;
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
		  "updated batch size (%d)", kpriv->batch_size);
  }

  return model;
}

/**
 * vvas_xrunmodel() - Run respective model
 */
int
vvas_xrunmodel (vvas_xkpriv * kpriv, VVASFrame *inputs[MAX_NUM_OBJECT])
{
  std::vector <cv::Mat> images;
  std::vector <GstInferenceMeta *> metas;
  GstInferencePrediction **predictions = new GstInferencePrediction*[kpriv->batch_size];

  vvas_xdpumodel *model = (vvas_xdpumodel *) kpriv->model;
  unsigned int i = 0;
  VVASFrame *cur_frame = NULL;

  LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");

  for (i = 0; i < kpriv->batch_size; i++)
    predictions[i] = NULL;

  i = 0;
  while (inputs[i]) {
    GstInferenceMeta *infer_meta = NULL;

    if (i == kpriv->batch_size) {
      // TODO: we can handle then in next iteration, but considering as error to simplify the functionality
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "received more frames than batch size (%d) of the DPU", kpriv->batch_size);
      return -1;
    }

    cur_frame = inputs[i];
    cv::Mat image (cur_frame->props.height, cur_frame->props.width, CV_8UC3,
        cur_frame->vaddr[0], cur_frame->props.stride);
    images.push_back(image);

    /* if inference metadata is available, attach prediction in current instance to it */
    infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta ((GstBuffer *) inputs[i]->app_priv, gst_inference_meta_api_get_type ()));
    if (infer_meta)
      predictions[i] = infer_meta->prediction;
    i++;
  }

  if (model->run (kpriv, images, predictions) != true) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "Model run failed %s",
        kpriv->modelname.c_str ());
    return -1;
  }

  for (i = 0; i < kpriv->batch_size; i++) {
    if (inputs[i] && predictions[i]) {
      GstInferenceMeta *infer_meta = NULL;

      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "attaching prediction %p to buffer %p",
          predictions[i], inputs[i]->app_priv);

      infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta ((GstBuffer *) inputs[i]->app_priv, gst_inference_meta_api_get_type ()));
      if (!infer_meta) {
        /* add metadata */
        infer_meta = (GstInferenceMeta *) gst_buffer_add_meta ((GstBuffer *)inputs[i]->app_priv, gst_inference_meta_get_info (), NULL);
      }

      if (infer_meta->prediction != predictions[i]) {
        LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "replacing metadata prediction with %p", predictions[i]);
        gst_inference_prediction_unref (infer_meta->prediction);
        infer_meta->prediction = predictions[i];
      }
    } else {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "skipping prediction %p and buffer %p",
          predictions[i], inputs[i] ? inputs[i]->app_priv : NULL);
    }
  }

  delete[] predictions;

  return true;
}

int prepare_filter_labels (vvas_xkpriv * kpriv, json_t *jconfig)
{
  json_t *val, *label;

  val = json_object_get (jconfig, "filter_labels");
  if (!val) {
    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "filter_labels does not exist process all labels");
    return 0;
  }

  if (!json_is_array (val)) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "filter_labels is not an array");
    goto error;
  }

  for (long unsigned int i = 0; i < json_array_size (val); i++) {
    label = json_array_get (val, i);
    if (!json_is_string (label)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "label is not string type");
      goto error;
    }

    string filter_label;
    filter_label = json_string_value (label);

    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "adding filter label %s to list", filter_label.c_str());
    kpriv->filter_labels.push_back (filter_label);
  }

  return 0;

error:
  return -1;
}

extern "C"
{

  int32_t xlnx_kernel_init (VVASKernel * handle)
  {
    vvas_xkpriv *kpriv = (vvas_xkpriv *) calloc (1, sizeof (vvas_xkpriv));
      kpriv->handle = handle;

    json_t *jconfig = handle->kernel_config;
    json_t *val;                /* kernel config from app */
    kpriv->objs_detection_max = UINT_MAX;

    /* parse config */

    val = json_object_get (jconfig, "debug_level");
    if (!val || !json_is_integer (val))
      kpriv->log_level = LOG_LEVEL_WARNING;
    else
      kpriv->log_level = json_integer_value (val);
      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");

    val = json_object_get (jconfig, "run_time_model");
    if (!val || !json_is_boolean (val))
      kpriv->run_time_model = 0;
    else
      kpriv->run_time_model = json_boolean_value (val);

    val = json_object_get (jconfig, "performance_test");
    if (!val || !json_is_boolean (val))
      kpriv->performance_test = false;
    else
      kpriv->performance_test = json_boolean_value (val);

    val = json_object_get (jconfig, "need_preprocess");
    if (!val || !json_is_boolean (val))
      kpriv->need_preprocess = true;
    else
    {
      kpriv->need_preprocess = json_boolean_value (val);
    }

    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
        "debug_level = %d, performance_test = %d", kpriv->log_level,
        kpriv->performance_test);

    val = json_object_get (jconfig, "batch-size");
    if (!val || !json_is_integer (val)) {
      kpriv->batch_size = BATCH_SIZE_ZERO;
      LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level,
          "batch-size is not available in json, taking default batch size = 1");
    } else {
      kpriv->batch_size = json_integer_value (val);
      LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level,
          "batch-size value received = %d", kpriv->batch_size);
    }

    val = json_object_get (jconfig, "model-format");
    if (!json_is_string (val)) {
      LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
          "model-format is not proper, taking BGR as default\n");
      kpriv->modelfmt = VVAS_VFMT_BGR8;
    } else {
      kpriv->modelfmt = vvas_fmt_to_xfmt ((char *) json_string_value (val));
      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
          "model-format %s", (char *) json_string_value (val));
    }
    if (kpriv->modelfmt == VVAS_VMFT_UNKNOWN) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "SORRY NOT SUPPORTED MODEL FORMAT %s",
          (char *) json_string_value (val));
      goto err;
    }
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
        "modelfmt = %d, need_preprocess = %d", kpriv->modelfmt,
        kpriv->need_preprocess);

    val = json_object_get (jconfig, "model-path");
    if (!json_is_string (val)) {
      LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
          "model-path is not proper");
      kpriv->modelpath = (char *) "/usr/share/vitis_ai_library/models/";
      LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
          "using default path : %s", kpriv->modelpath.c_str ());
    } else {
      kpriv->modelpath = json_string_value (val);
    }
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "model-path (%s)",
        kpriv->modelpath.c_str ());
    if (!fileexists (kpriv->modelpath)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "model-path (%s) not exist", kpriv->modelpath.c_str ());
      goto err;
    }

    if (kpriv->run_time_model) {
      LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level,
          "runtime model load is set");
      handle->kernel_priv = (void *) kpriv;
      return true;
    }

    val = json_object_get (jconfig, "model-class");
    if (!json_is_string (val)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "model-class is not proper\n");
      goto err;
    }
    kpriv->modelclass = vvas_xclass_to_num ((char *) json_string_value (val));
    if (kpriv->modelclass == VVAS_XCLASS_NOTFOUND) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "SORRY NOT SUPPORTED MODEL CLASS %s",
          (char *) json_string_value (val));
      goto err;
    }

    val = json_object_get (jconfig, "model-name");
    if (!json_is_string (val)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "model-name is not proper\n");
      goto err;
    }
    kpriv->modelname = (char *) json_string_value (val);

    kpriv->elfname = modelexits (kpriv);
    if (kpriv->elfname.empty ()) {
      goto err;
    }

    /* max objects to be attached to metadata after object detection */
    val = json_object_get (jconfig, "max-objects");
    if (json_is_integer (val)) {
      kpriv->objs_detection_max = (unsigned int)json_integer_value (val);
      LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level,
          "max-objects to be attached in metadata = %u",
          kpriv->objs_detection_max);
    }

    /* Do we need to move this in init of vvas_xsegmentation.c */
    if (kpriv->modelclass == VVAS_XCLASS_SEGMENTATION) {
      val = json_object_get (jconfig, "seg-out-format");
      if (!json_is_string (val)) {
        LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
            "seg-out-format is not proper, taking BGR as default\n");
        kpriv->segoutfmt = VVAS_VFMT_BGR8;
      } else {
        kpriv->segoutfmt = vvas_fmt_to_xfmt ((char *) json_string_value (val));
        LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
            "seg-out-format %s", (char *) json_string_value (val));
      }
      if (kpriv->segoutfmt == VVAS_VMFT_UNKNOWN ||
	  !(kpriv->segoutfmt == VVAS_VFMT_BGR8 ||
	    kpriv->segoutfmt == VVAS_VFMT_Y8)) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
            "SORRY NOT SUPPORTED SEGMENTATION OUTPUT FORMAT %s",
            (char *) json_string_value (val));
        goto err;
      }
      val = json_object_get (jconfig, "seg-out-factor");
      if (!val || !json_is_integer (val)) {
        LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
            "seg-out-factor is not proper, taking 1 as default\n");
        kpriv->segoutfactor = 1;
      } else {
        kpriv->segoutfactor = json_integer_value (val);
        LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
            "seg-out-factor %d", kpriv->segoutfactor);
      }
    }

    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "model-name = %s\n",
        (char *) json_string_value (val));
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "model class is %d",
        kpriv->modelclass);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "elf class is %s",
        kpriv->elfname.c_str ());

    kpriv->model = vvas_xinitmodel (kpriv, kpriv->modelclass);
    if (kpriv->model == NULL) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "Init vvas_xinitmodel failed for %s", kpriv->modelname.c_str ());
      goto err;
    }

    if (prepare_filter_labels (kpriv, jconfig) < 0)
      goto err;

    handle->kernel_priv = (void *) kpriv;
    return true;

  err:
    free (kpriv);
    return -1;
  }

  uint32_t xlnx_kernel_deinit (VVASKernel * handle)
  {
    vvas_xkpriv *kpriv = (vvas_xkpriv *) handle->kernel_priv;
    if (!kpriv)
      return true;
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");

    vvas_perf *pf = &kpriv->pf;

    if (kpriv->performance_test && kpriv->pf.test_started) {
      double time = (get_time () - pf->timer_start) / 1000000.0;
      double fps = (time > 0.0) ? (pf->frames / time) : 999.99;
      printf ("\rframe=%5lu fps=%6.*f        \n", pf->frames,
          (fps < 9.995) ? 3 : 2, fps);
    }
    pf->test_started = 0;
    pf->frames = 0;
    pf->last_displayed_frame = 0;
    pf->timer_start = 0;
    pf->last_displayed_time = 0;

    if (!kpriv->run_time_model) {
      for (int i = 0; i < int (kpriv->mlist.size ()); i++) {
        if (kpriv->mlist[i].model) {
          kpriv->mlist[i].model->close ();
          delete kpriv->mlist[i].model;
          kpriv->mlist[i].model = NULL;
        }
        kpriv->model = NULL;
      }
    }
    kpriv->modelclass = VVAS_XCLASS_NOTFOUND;

    kpriv->filter_labels.clear();
    if (kpriv->model != NULL) {
      kpriv->model->close ();
      delete kpriv->model;
      kpriv->model = NULL;
    }
    if (kpriv->labelptr != NULL)
      free (kpriv->labelptr);

    vvas_caps_free (handle);
    free (kpriv);

    return true;
  }

  uint32_t xlnx_kernel_start (VVASKernel * handle, int start,
      VVASFrame * input[MAX_NUM_OBJECT], VVASFrame * output[MAX_NUM_OBJECT])
  {
    vvas_xkpriv *kpriv = (vvas_xkpriv *) handle->kernel_priv;
    vvas_perf *pf = &kpriv->pf;
    GstVvasInpInferMeta *vvas_inputmeta = NULL;
    VVASFrame *inframe = input[0];
    int ret, i;

    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");

    if (kpriv->run_time_model) {
      bool found = false;
      vvas_inputmeta =
          gst_buffer_get_vvas_inp_infer_meta ((GstBuffer *) inframe->app_priv);
      if (vvas_inputmeta == NULL) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
            "error getting vvas_inputmeta");
        return -1;
      }

      kpriv->modelclass = vvas_inputmeta->ml_class;
      kpriv->modelname = vvas_inputmeta->model_name;
      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
          "Runtime model clase is %d", kpriv->modelclass);
      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
          "Runtime model name is %s", kpriv->modelname.c_str ());
      kpriv->elfname = modelexits (kpriv);
      if (kpriv->elfname.empty ()) {
        LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
            "Runtime model not found");
        return -1;
      }

      for (i = 0; i < int (kpriv->mlist.size ()); i++) {
        if ((kpriv->mlist[i].modelclass == vvas_inputmeta->ml_class)
            && (kpriv->mlist[i].modelname == vvas_inputmeta->model_name)) {
          LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
              "Model already loaded");
          found = true;
          break;
        }
      }

      if (found) {
        kpriv->model = kpriv->mlist[i].model;
        kpriv->labelptr = kpriv->mlist[i].labelptr;
        kpriv->segoutfmt = kpriv->mlist[i].segoutfmt;
      } else {
        model_list mlist;
        kpriv->model = vvas_xinitmodel (kpriv, kpriv->modelclass);
        if (kpriv->model == NULL) {
          LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
              "Init model failed for %s", kpriv->modelname.c_str ());
          return -1;
        }
        mlist.modelclass = vvas_inputmeta->ml_class;
        mlist.modelname = vvas_inputmeta->model_name;
        mlist.model = kpriv->model;
        mlist.labelptr = kpriv->labelptr;
        mlist.segoutfmt = kpriv->segoutfmt;
        kpriv->mlist.push_back (mlist);
      }
    }

    if (kpriv->performance_test && !kpriv->pf.test_started) {
      pf->timer_start = get_time ();
      pf->last_displayed_time = pf->timer_start;
      pf->test_started = 1;
    }

    unsigned int width = kpriv->model->requiredwidth ();
    unsigned int height = kpriv->model->requiredheight ();
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
        "model required wxh is %dx%d", width, height);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "input image wxh is %dx%d",
        inframe->props.width, inframe->props.height);

    if (width != inframe->props.width || height != inframe->props.height) {
      LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
          "Input height/width not match with model" "requirement");
      LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
          "model required wxh is %dx%d", width, height);
      LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
          "input image wxh is %dx%d", inframe->props.width,
          inframe->props.height);
      // return false; //TODO
    }

    ret = vvas_xrunmodel (kpriv, input);

    if (kpriv->performance_test && kpriv->pf.test_started) {
      pf->frames++;
      if (get_time () - pf->last_displayed_time >= 1000000.0) {
        long long current_time = get_time ();
        double time = (current_time - pf->last_displayed_time) / 1000000.0;
        pf->last_displayed_time = current_time;
        double fps =
            (time >
            0.0) ? ((pf->frames - pf->last_displayed_frame) / time) : 999.99;
        pf->last_displayed_frame = pf->frames;
        printf ("\rframe=%5lu fps=%6.*f        \r", pf->frames,
            (fps < 9.995) ? 3 : 2, fps);
        fflush (stdout);
      }
    }
    //vvas_meta->xmeta.pts = GST_BUFFER_PTS ((GstBuffer *) inframe->app_priv);
    return ret;
  }

  int32_t xlnx_kernel_done (VVASKernel * handle)
  {

    vvas_xkpriv *kpriv = (vvas_xkpriv *) handle->kernel_priv;
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");

    return true;
  }

}
