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

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <fstream>

#include <vvas/vvas_kernel.h>
#include <vvas/vvaslogs.h>
#include <gst/vvas/gstvvasinpinfer.h>

using namespace cv;
using namespace std;

struct testkpriv
{
  int modelclass;               /* Class of model, from Json file */
  int modelnum;                 /* map class to number vvas_xmodelclass[] */
  int log_level;                /* LOG_LEVEL_ERROR=0, LOG_LEVEL_WARNING=1,
                                   LOG_LEVEL_INFO=2, LOG_LEVEL_DEBUG=3 */
    std::string modelname;      /* contain name of model from json */
};
typedef struct testkpriv testkpriv;

static const char *vvas_xmodelclass[VVAS_XCLASS_NOTFOUND + 1] = {
  [VVAS_XCLASS_YOLOV3] = "YOLOV3",
  [VVAS_XCLASS_FACEDETECT] = "FACEDETECT",
  [VVAS_XCLASS_CLASSIFICATION] = "CLASSIFICATION",
  [VVAS_XCLASS_SSD] = "SSD",
  [VVAS_XCLASS_REID] = "REID",
  [VVAS_XCLASS_REFINEDET] = "REFINEDET",
  [VVAS_XCLASS_TFSSD] = "TFSSD",
  [VVAS_XCLASS_YOLOV2] = "YOLOV2",
  [VVAS_XCLASS_SEGMENTATION] = "SEGMENTATION",

  /* Add model above this */
  [VVAS_XCLASS_NOTFOUND] = ""
};

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

extern "C"
{

  int32_t xlnx_kernel_init (VVASKernel * handle)
  {
    testkpriv *kpriv = (testkpriv *) calloc (1, sizeof (testkpriv));

    json_t *jconfig = handle->kernel_config;
    json_t *val;                /* kernel config from app */

    /* parse config */

      val = json_object_get (jconfig, "debug_level");
    if (!val || !json_is_integer (val))
        kpriv->log_level = LOG_LEVEL_WARNING;
    else
        kpriv->log_level = json_integer_value (val);
      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");

      val = json_object_get (jconfig, "model-class");
    if (!json_is_string (val))
    {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "model-class is not proper\n");
      goto err;
    }
    kpriv->modelclass =
        (int) vvas_xclass_to_num ((char *) json_string_value (val));
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

    handle->kernel_priv = (void *) kpriv;
    return true;

  err:
    free (kpriv);
    return -1;
  }

  uint32_t xlnx_kernel_deinit (VVASKernel * handle)
  {
    testkpriv *kpriv = (testkpriv *) handle->kernel_priv;
    if (!kpriv)
      return true;
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");

    free (kpriv);

    return true;
  }

  uint32_t xlnx_kernel_start (VVASKernel * handle, int start,
      VVASFrame * input[MAX_NUM_OBJECT], VVASFrame * output[MAX_NUM_OBJECT])
  {
    static int frame = 0;
    testkpriv *kpriv = (testkpriv *) handle->kernel_priv;
    GstVvasInpInferMeta *vvas_inputmeta = NULL;
    VVASFrame *inframe = input[0];

    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");

    if (frame++ % 2) {
      vvas_inputmeta =
          gst_buffer_add_vvas_inp_infer_meta ((GstBuffer *) inframe->app_priv,
          (VvasClass) kpriv->modelclass, (gchar *) kpriv->modelname.c_str ());
    } else {
      vvas_inputmeta =
          gst_buffer_add_vvas_inp_infer_meta ((GstBuffer *) inframe->app_priv,
          (VvasClass) 2, (gchar *) "resnet50");
    }
    if (vvas_inputmeta == NULL) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "vvas meta data is not available for dpu");
      return -1;
    } else {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "vvas_mata ptr %p",
          vvas_inputmeta);
    }

    return true;
  }

  int32_t xlnx_kernel_done (VVASKernel * handle)
  {

    testkpriv *kpriv = (testkpriv *) handle->kernel_priv;
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");
    return true;
  }

}
