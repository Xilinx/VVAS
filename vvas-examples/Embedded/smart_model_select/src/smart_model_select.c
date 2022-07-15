/*
 * Copyright 2021 - 2022 Xilinx, Inc.
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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

//#define DEBUG
#define DEFAULT_XCLBIN_PATH "/run/media/mmcblk0p1/dpu.xclbin"

char dpu_template_json[40] = "./jsons/kernel_dpu.json";
char bbox_template_json[40] = "./jsons/kernel_bbox.json";
char preprocessor_template_json[40] = "./jsons/kernel_preprocessor.json";
char dpu_json[100] = "./jsons/kernel_dpu_MODEL.json";
char bbox_json[100] = "./jsons/kernel_bbox_MODEL.json";
char preprocessor_json[100] = "./jsons/kernel_preprocessor_MODEL.json";

typedef struct user_choice {
  int src;
  int model;
  int sink;
  char input_file[1024];
}options;

int perf = 0;

static void execute_cmd(char *file)
{
  FILE *fp;
  long fsize;
  char buff[5120] = {'\0'};
  char *temp = NULL;

  fp = fopen(file, "r");
  fseek(fp, 0, SEEK_END);
  fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  fread(buff, 1, fsize, fp);
  if ((strcmp(file, "templates/welcome.cfg")) &&  perf) {
    printf ("Appending option -v\n");
    /* Find if there is a new line and replace it with
     * 0, so that -v can be appeneded */
    temp = strrchr(buff, 10);
    printf(" Temp : %p\n", temp);
    if(temp)
      *temp = '\0';
    strcat(buff, " -v");
  }
  perf = 0;
  fclose(fp);
#ifdef DEBUG
  printf ("DEBUG: Got the cmd: %s\n", buff);
#endif
  system(buff);

  printf ("\n\n >>>>> Type any key, then ENTER to return to main menu <<<<<\n");
}

static void *welcome (void *argp)
{
  execute_cmd("templates/welcome.cfg");
  return NULL;
}

static void *run_model (void *argp)
{
  execute_cmd(argp);
  return NULL;
}

#ifdef DEBUG
static void display_menu (options *op, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    printf ("DEBUG: src [%d] => %d\n", i, op[i].src);
    printf ("DEBUG: model [%d] => %d\n", i, op[i].model);
    printf ("DEBUG: sink [%d] => %d\n", i, op[i].sink);
  }
}
#endif

static char *get_class(int n)
{
  switch (n) {
    case 1: return "CLASSIFICATION";
    case 2: return "CLASSIFICATION";
    case 3: return "CLASSIFICATION";
    case 4: return "CLASSIFICATION";
    case 5: return "SSD";
    case 6: return "SSD";
    case 7: return "SSD";
    case 8: return "SSD";
    case 9: return "PLATEDETECT";
    case 10: return "YOLOV3";
    case 11: return "YOLOV3";
    case 12: return "REFINEDET";
    case 13: return "YOLOV2";
    case 14: return "YOLOV2";
    case 15: return "FACEDETECT";
    case 16: return "FACEDETECT";
    default: return "NULL";
  }
}

static char *get_model(int n)
{
  switch (n) {
    case 1: return "resnet50";
    case 2: return "resnet18";
    case 3: return "mobilenet_v2";
    case 4: return "inception_v1";
    case 5: return "ssd_adas_pruned_0_95";
    case 6: return "ssd_traffic_pruned_0_9";
    case 7: return "ssd_mobilenet_v2";
    case 8: return "ssd_pedestrian_pruned_0_97";
    case 9: return "plate_detect";
    case 10: return "yolov3_voc_tf";
    case 11: return "yolov3_adas_pruned_0_9";
    case 12: return "refinedet_pruned_0_96";
    case 13: return "yolov2_voc";
    case 14: return "yolov2_voc_pruned_0_77";
    case 15: return "densebox_320_320";
    case 16: return "densebox_640_360";
    default: return "NULL";
  }
}

static char *get_src(int n)
{
  switch (n) {
    case 1: return "file";
    case 2: return "rtsp";
    default: return "NULL";
  }
}

static char *get_sink(int n)
{
  switch (n) {
    case 1: return "file";
    case 2: return "fake";
    case 3: return "kms";
    default: return "NULL";
  }
}

static int menu (options *op, int n)
{
  int i, j;
  char *pch, str[20];

  for (i = 0; i < n; i++) {
    printf ("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
    printf ("Menu displayed on the monitor shows various options available\n");
    printf ("for input source, ML model, output sink. Each option carry an\n");
    printf ("index number along side.\n");
    printf ("Select elements to be used in the pipeline in the sequence of\n");
    printf ("\"input source, ML model, output sink and performance\n");
    printf ("mode flag\" seperated by commas.\n");
    printf ("eg: 1,1,3,0\n");
    printf ("Above input will run \"filesrc\" input, \"resnet50\" model\n");
    printf ("\"kmssink\" used as output sink and performance mode disabled.\n");
    printf ("Enter 'q' to exit\n");
    printf ("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
    scanf("    %s", str);
    j = 0;
    pch = strtok (str," ,.-");
    while (pch != NULL) {
      j++;
      if (!strcmp(pch, "q"))
        return -1;
      if (!strcmp(pch, "Q"))
        return -1;
      if (j == 1)
        op[i].src = atoi(pch);
      if (j == 2)
        op[i].model = atoi(pch);
      if (j == 3)
        op[i].sink = atoi(pch);
      if (j == 4)
        perf = atoi(pch);
      pch = strtok (NULL, " ,.-");
    }
  }
  return 0;
}

static int validate_input (options *op, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    if (op[i].src < 1 || op[i].src > 2) {
      printf ("ERROR: Invalid source selected %d\n", op[i].src);
      return -1;
    }
    if (op[i].model < 1 || op[i].model > 16) {
      printf ("ERROR: Invalid model selected %d\n", op[i].model);
      return -1;
    }
    if (op[i].sink < 1 || op[i].sink > 3) {
      printf ("ERROR: Invalid sink selected %d\n", op[i].sink);
      return -1;
    }
    if (perf == 1 && op[i].sink != 2) {
      printf ("WARNING: Performance option is meant to be used with fakesink.\n");
      printf ("         Using it with other sinks may not give valid info.\n");
      return -1;
    }
  }
  return 0;
}

static void update_pre_process_cfg (char *file, int n)
{
  char cmd[100];
  char *mean_r;
  char *mean_g;
  char *mean_b;
  char *scale_r;
  char *scale_g;
  char *scale_b;

  switch (n) {
    case 1:
      mean_r = "104";
      mean_g = "107";
      mean_b = "123";
      scale_r = "0.5";
      scale_g = "0.5";
      scale_b = "0.5";
      break;
    case 2:
      mean_r = "104";
      mean_g = "117";
      mean_b = "123";
      scale_r = "0.5";
      scale_g = "0.5";
      scale_b = "0.5";
      break;
    case 3:
      mean_r = "104";
      mean_g = "117";
      mean_b = "123";
      scale_r = "0.501961";
      scale_g = "0.501961";
      scale_b = "0.501961";
      break;
    case 4:
      mean_r = "104";
      mean_g = "117";
      mean_b = "123";
      scale_r = "0.5";
      scale_g = "0.5";
      scale_b = "0.5";
      break;
    case 5:
      mean_r = "104";
      mean_g = "117";
      mean_b = "123";
      scale_r = "0.5";
      scale_g = "0.5";
      scale_b = "0.5";
      break;
    case 6:
      mean_r = "104";
      mean_g = "117";
      mean_b = "123";
      scale_r = "0.5";
      scale_g = "0.5";
      scale_b = "0.5";
      break;
    case 7:
      mean_r = "104";
      mean_g = "117";
      mean_b = "123";
      scale_r = "0.5";
      scale_g = "0.5";
      scale_b = "0.5";
      break;
    case 8:
      mean_r = "104";
      mean_g = "117";
      mean_b = "123";
      scale_r = "0.5";
      scale_g = "0.5";
      scale_b = "0.5";
      break;
    case 9:
      mean_r = "128";
      mean_g = "128";
      mean_b = "128";
      scale_r = "1";
      scale_g = "1";
      scale_b = "1";
      break;
    case 10:
      mean_r = "0";
      mean_g = "0";
      mean_b = "0";
      scale_r = "0.25";
      scale_g = "0.25";
      scale_b = "0.25";
      break;
    case 11:
      mean_r = "0";
      mean_g = "0";
      mean_b = "0";
      scale_r = "0.5";
      scale_g = "0.5";
      scale_b = "0.5";
      break;
    case 12:
      mean_r = "104";
      mean_g = "117";
      mean_b = "123";
      scale_r = "0.5";
      scale_g = "0.5";
      scale_b = "0.5";
      break;
    case 13:
      mean_r = "0";
      mean_g = "0";
      mean_b = "0";
      scale_r = "0.5";
      scale_g = "0.5";
      scale_b = "0.5";
      break;
    case 14:
      mean_r = "0";
      mean_g = "0";
      mean_b = "0";
      scale_r = "0.5";
      scale_g = "0.5";
      scale_b = "0.5";
      break;
    case 15:
      mean_r = "128";
      mean_g = "128";
      mean_b = "128";
      scale_r = "1";
      scale_g = "1";
      scale_b = "1";
      break;
    case 16:
      mean_r = "128";
      mean_g = "128";
      mean_b = "128";
      scale_r = "1";
      scale_g = "1";
      scale_b = "1";
      break;
    default:
      return;
  }

  memset(cmd, '\0', 100);
  sprintf(cmd, "sed -i \'s/MEANR/%s/g\' %s", mean_r, file);
  system(cmd);
  memset(cmd, '\0', 100);
  sprintf(cmd, "sed -i \'s/MEANG/%s/g\' %s", mean_g, file);
  system(cmd);
  memset(cmd, '\0', 100);
  sprintf(cmd, "sed -i \'s/MEANB/%s/g\' %s", mean_b, file);
  system(cmd);
  memset(cmd, '\0', 100);
  sprintf(cmd, "sed -i \'s/SCALER/%s/g\' %s", scale_r, file);
  system(cmd);
  memset(cmd, '\0', 100);
  sprintf(cmd, "sed -i \'s/SCALEG/%s/g\' %s", scale_g, file);
  system(cmd);
  memset(cmd, '\0', 100);
  sprintf(cmd, "sed -i \'s/SCALEB/%s/g\' %s", scale_b, file);
  system(cmd);
}

static void update_input_filename (char *file, char *input_file_name)
{
  char cmd[100];

  memset(cmd, '\0', 100);
  sprintf(cmd, "sed -i \'s#INPUTFILENAME#%s#g\' %s", input_file_name, file);
  system(cmd);
}

static void update_color_format (char *dpu_json, int model)
{
  char cmd[300];

  switch (model) {
    case 1: /*"resnet50"*/
    case 2: /* "resnet18"*/
    case 3: /* "mobilenet_v2"*/
    case 4: /* "inception_v1"*/
    case 5: /* "ssd_adas_pruned_0_95"*/
    case 6: /* "ssd_traffic_pruned_0_9"*/
    case 7: /* "ssd_mobilenet_v2"*/
    case 8: /* "ssd_pedestrian_pruned_0_97"*/
    case 9: /* "plate_detect"*/
    case 12: /* "refinedet_pruned_0_96"*/
    case 15: /* "densebox_320_320"*/
    case 16: /* "densebox_640_360"*/
      memset(cmd, '\0', 300);
      sprintf(cmd, "sed -i \'s#FORMAT#%s#g\' %s", "BGR", dpu_json);
      system(cmd);
      break;
    case 10: /* "yolov3_voc_tf"*/
    case 11: /* "yolov3_adas_pruned_0_9"*/
    case 13: /* "yolov2_voc"*/
    case 14: /* "yolov2_voc_pruned_0_77"*/
      memset(cmd, '\0', 300);
      sprintf(cmd, "sed -i \'s#FORMAT#%s#g\' %s", "RGB", dpu_json);
      system(cmd);
      break;
    default: /* The argument "model" is already verified for its correctness,
          just break here */
      break;
  }
}

static void update_xclbin_location (char *tmp_file, char *dpu_json,
                                    char *bbox_json, char *preprocessor_json)
{
  char xclbin_loc[200];
  char cmd[300];

  if(getenv("XCLBIN_PATH")) {
    strcpy(xclbin_loc, getenv("XCLBIN_PATH"));
    printf("Using \"%s\" for xclbin path\n", xclbin_loc);
  } else {
    printf("\"XCLBIN_PATH\" is not set using the default xclbin path : %s\n", DEFAULT_XCLBIN_PATH);
    strcpy(xclbin_loc, DEFAULT_XCLBIN_PATH);
  }

  memset(cmd, '\0', 100);
  sprintf(cmd, "sed -i \'s#XCLBIN_PATH#%s#g\' %s", xclbin_loc, tmp_file);
  system(cmd);
  sprintf(cmd, "sed -i \'s#XCLBIN_PATH#%s#g\' %s", xclbin_loc, dpu_json);
  system(cmd);
  sprintf(cmd, "sed -i \'s#XCLBIN_PATH#%s#g\' %s", xclbin_loc, bbox_json);
  system(cmd);
  sprintf(cmd, "sed -i \'s#XCLBIN_PATH#%s#g\' %s", xclbin_loc, preprocessor_json);
  system(cmd);
}

static void update_model (char *file_temp, char *file, char *model, char *class)
{
  char cmd[300];

  sprintf(cmd, "cat %s | sed \'s/MODEL/%s/g\' > %s", file_temp, model, file);
#ifdef DEBUG
  printf("DEBUG: CMD is > %s\n", cmd);
#endif
  system(cmd);
  memset(cmd, '\0', 100);
  sprintf(cmd, "sed -i \'s/CLASS/%s/g\' %s", class, file);
#ifdef DEBUG
  printf("DEBUG: CMD is > %s\n", cmd);
#endif
  system(cmd);
}

static void parse_input (options *op, int n, char *file)
{
  char *model, *class, *src, *sink, file_temp[50];

  if (n == 1) {
    src = get_src(op[0].src);
    sink = get_sink(op[0].sink);
    model = get_model(op[0].model);
    class = get_class(op[0].model);
    if(perf)
      sprintf(file_temp, "./templates/%s_%s_template_perf.cfg", src, sink);
    else
      sprintf(file_temp, "./templates/%s_%s_template.cfg", src, sink);
    sprintf(file, "./tmp_%s_%s.cfg", src, sink);
#ifdef DEBUG
    printf ("DEBUG Model name is %s\n", model);
    printf ("DEBUG Class name is %s\n", class);
    printf ("DEBUG SRC is %s\n", src);
    printf ("DEBUG SINK is %s\n", sink);
#endif
    sprintf(dpu_json, "./jsons/kernel_dpu_%s.json", model);
    sprintf(bbox_json, "./jsons/kernel_bbox_%s.json", model);
    sprintf(preprocessor_json, "./jsons/kernel_preprocessor_%s.json", model);
    update_model (file_temp, file, model, class);
    update_model (dpu_template_json, dpu_json, model, class); /* dpuinfer */
    update_model (bbox_template_json, bbox_json, model, class); /* bbox */
    update_model (preprocessor_template_json, preprocessor_json, model, class); /* xinfer */
    update_input_filename (file, op[0].input_file);
    update_pre_process_cfg (preprocessor_json, op[0].model);
    update_xclbin_location (file, dpu_json, bbox_json, preprocessor_json);
    update_color_format (dpu_json, op[0].model);

  } else {
     printf ("ERROR: Not supported %d models\n", n);
  }
}

static void read_input (options *op, int n)
{
  char input_file[200];
  int i;
  struct stat buf;

  for (i = 0; i < n; i++) {
    sprintf(input_file, "videos/%s.mp4", get_class(op[i].model));
    if(op[i].src == 1) {
      if(stat(input_file, &buf) == 0) {
        printf("Starting pipeline with input file : %s\n", input_file);
        strcpy(op[i].input_file, input_file);
      } else {
        do {
          if(op[i].src == 1) {
            printf("Enter the input filename to be processed\n");
            scanf("%s", op[i].input_file);
            if(!(stat(op[i].input_file, &buf) == 0)) {
              printf("Entered file not accessible, please enter the correct file\n");
              continue;
            } else {
              break;
            }
          }
        } while (1);
      }
    } else if(op[i].src == 2) {
      printf("Enter the RTSP url to be processed\n");
      scanf("%s", op[i].input_file);
    }
  }
}

static void check_exit()
{
  char opt;

  while (1) {
    printf (" >>>>> Type any key, then ENTER to return to main menu <<<<<\n");
    scanf(" %c", &opt);
    return;
  }
}

static void cancel_thread (pthread_t id)
{
  void *res;
  int ret;

  ret = pthread_cancel (id);
  if(ret && errno)
        printf ("ERROR : unable to terminate thread, errno : %d\n", errno);

  ret = pthread_join(id, &res);
  if (ret)
    printf ("ERROR: unable to join thread\n");
}

int main()
{
  char file[50];
  options op[4];
  pthread_t wel_id, first_id;
  int n;

  while (1) {
    memset(file, '\0', 50);
    memset(op, 0, 4*sizeof(options));
    printf ("############################################\n");
    printf ("################## WELCOME #################\n");
    printf ("############################################\n\n");

    pthread_create(&wel_id, NULL, welcome, NULL);
    sleep(1);
#if 0
    printf ("\n\n>>Please enter the number of models you want to run:\n");
    scanf("%d", &n);
#endif
    n = 1;  /* Supporting single model */
    if (menu(op, n)) {
      cancel_thread (wel_id);
      break;
    }

#ifdef DEBUG
    display_menu(op, n);
#endif
    if(validate_input(op, n)) {
      cancel_thread (wel_id);
      continue;
    }
    read_input(op, n);
    parse_input(op, n, file);
#ifdef DEBUG
    printf ("DEBUG: File name is %s\n", file);
#endif
    cancel_thread (wel_id);
    pthread_create(&first_id, NULL, run_model, file);
    sleep(3);
    check_exit();
    if (strcmp(file, "welcome.cfg")) {
      remove (file);
      remove (dpu_json);
      remove (bbox_json);
      remove (preprocessor_json);
    }
    cancel_thread (first_id);
  }
  return 0;
}
