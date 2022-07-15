/*       
 * Copyright (C) 2019, Xilinx Inc - All rights reserved
 * Xilinx Lookahead Plugin
 *                                    
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations 
 * under the License.
 */
#ifndef XLNX_RC_AQ_H
#define XLNX_RC_AQ_H

#include "xlnx_aq_common.h"

typedef void *xlnx_rc_aq_t;

xlnx_rc_aq_t xlnx_algo_rc_create (uint32_t la_depth);
void xlnx_algo_rc_destroy (xlnx_rc_aq_t rc);
xlnx_status xlnx_algo_rc_write_fsfa (xlnx_rc_aq_t rc, xlnx_rc_fsfa_t * fsfa);
xlnx_status xlnx_algo_rc_read_fsfa (xlnx_rc_aq_t rc,
    xlnx_aq_buf_t * fsfa_buf, uint64_t * frame_num);
int xlnx_algo_rc_fsfa_available (xlnx_rc_aq_t rc);

#endif // XLNX_RC_AQ_H
