########################################################################
 # Copyright 2020 - 2022 Xilinx, Inc.
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
#########################################################################

$CC := gcc

GST_INC_PATH ?= /usr/local/include/gstreamer-1.0
GST_LIB_PATH ?= /usr/lib/gstreamer-1.0

OBJS += \
./vvas_xabrladder.o 

OUTPUT = vvas_xabrladder

CFLAGS +=  -Wall -O0 -g3
CFLAGS += -I/usr/include/
CFLAGS += `pkg-config  --cflags glib-2.0`
CFLAGS += `pkg-config --cflags gstreamer-1.0`

LFLAGS += -L=/lib -L=/usr/lib
LFLAGS += `pkg-config --libs glib-2.0`
LFLAGS += `pkg-config --libs gstreamer-1.0`


LIBS := -lgstpbutils-1.0 -ljansson

# All Target
all: $(OUTPUT)

%.o: ./src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: ${CC} Linux gcc compiler'
	$(CC) -c -o $@ $< $(CFLAGS)
	@echo 'Finished building: $<'
	@echo ' '


# Tool invocations
$(OUTPUT): $(OBJS)  $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: ${CC} Linux gcc linker'
	$(CC) -o $@ $^ $(LFLAGS) $(LIBS)
	-$(RM) $(OBJS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(OBJS) $(OUTPUT)
	-@echo ' '

.PHONY: all clean
