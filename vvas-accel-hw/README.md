# vvas-accel-hw

## Copyright and license statement
Copyright 2022 Xilinx Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

- Step 1: Source 2021.2 vitis
- Step 2: edit Makefile to point PLATFORM_FILE to any 2021.2 vitis platform (tested with zcu104 base platform)
- Step 3: edit options eg: multiscaler/v_multi_scaler_config.h of your choice
- Step 4: make

Kernels will be generate at xo folder. eg: Multiscaler kernel will be generated as xo/v_multi_scaler.xo
