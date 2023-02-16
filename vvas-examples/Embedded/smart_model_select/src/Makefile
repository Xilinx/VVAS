#
# Copyright 2021-2022 Xilinx, Inc.
# Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#


SYSROOT ?= ${SDKTARGETSYSROOT}
CC := aarch64-xilinx-linux-gcc
#CC := gcc
RM := rm -rf

# Add inputs and outputs from these tool invocations to the build variables
C_SRCS += \
./smart_model_select.c

OBJS += \
./smart_model_select.o

C_DEPS += \
./smart_model_select.d

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

LIBS := -lpthread

# All Target
all: smart_model_select

%.o: ./%.c
	@echo 'Invoking: ${CC} compiler'
	${CC} -Wall -O0 -g3 -I${SYSROOT}/usr/include -c -fmessage-length=0 -MT"$@" --sysroot=${SYSROOT} -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo ' '


# Tool invocations
smart_model_select: $(OBJS)  $(USER_OBJS)
	@echo 'Invoking: ${CC} linker'
	${CC} -L=/lib -L=/usr/lib --sysroot=${SYSROOT} -Xlinker -rpath-link=${SYSROOT}/lib -Xlinker -rpath-link=${SYSROOT}/usr/lib -o "smart_model_select" $(OBJS) $(USER_OBJS) $(LIBS)
	-$(RM) $(OBJS) $(S_UPPER_DEPS) $(C_DEPS)

# Other Targets
clean:
	-$(RM) $(OBJS) $(S_UPPER_DEPS) $(C_DEPS) smart_model_select
	-@echo ' '

.PHONY: all clean
