
/*
 * Copyright 2022 Xilinx, Inc.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
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

#include <vvas_core/vvas_context.h>
#include <vvas_core/vvas_video.h>
#include <vvas_core/vvas_video_priv.h>

VvasVideoFrame *
vvas_videoframe_from_vvasframe (VvasContext * vvas_ctx, int8_t mbank_idx,
                                VVASFrame * vframe);
