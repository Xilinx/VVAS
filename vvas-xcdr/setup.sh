#
# Copyright (C) 2022, Xilinx Inc - All rights reserved
# Xilinx Transcoder (xcdr)
#
# Licensed under the Apache License, Version 2.0 (the "License"). You may
# not use this file except in compliance with the License. A copy of the
# License is located at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations
# under the License.
#

echo ---------------------------------------
echo -----Source Xilinx U30 setup files-----
source /opt/xilinx/xrt/setup.sh
source /opt/xilinx/xrm/setup.sh
export LD_LIBRARY_PATH=/opt/xilinx/ffmpeg/lib:$LD_LIBRARY_PATH
export PATH=/opt/xilinx/ffmpeg/bin:/opt/xilinx/xcdr/bin:/opt/xilinx/launcher/bin:/opt/xilinx/jobSlotReservation/bin:$PATH
source /opt/xilinx/xcdr/xrmd_start.bash

VVAS_DIR=/opt/xilinx/vvas
if [ -d "$VVAS_DIR" ]; then
    export LD_LIBRARY_PATH=/opt/xilinx/vvas/lib:$LD_LIBRARY_PATH
    export PKG_CONFIG_PATH=/opt/xilinx/vvas/lib/pkgconfig:$PKG_CONFIG_PATH
    export PATH=/opt/xilinx/vvas/bin:$PATH
    export GST_PLUGIN_PATH=/opt/xilinx/vvas/lib/gstreamer-1.0:$GST_PLUGIN_PATH
fi

OLD_SHELLS="xilinx_u30_gen3x4_base_1 "

old_shell_count=0
devicestring="device"
maxlinesoutput=256
u30string="u30"
EXAMINE_FILE=/tmp/xbutil_examine.json

detect_older_shells()
{
    for shell in $OLD_SHELLS
    do
        old=$(grep -A $maxlinesoutput $devicestring $EXAMINE_FILE | grep -c -o $shell)
        if [ "$old" -gt ${mindevice} ] ;
        then
            old_shell_count=$((old_shell_count + "$old"))
        fi
    done
}

mindevice=0
maxdevice=16

remove_tmp=$(rm -f $EXAMINE_FILE)
xe=$(xbutil examine -o $EXAMINE_FILE)
ret=$?
if [ $ret -eq 0 ] ;
then
    detect_older_shells
    numdevice=$(grep -A $maxlinesoutput $devicestring $EXAMINE_FILE | grep -o $u30string | wc -l)

    if [[ "$numdevice" -gt "$maxdevice" ]] ;
    then
        echo "Number of U30 devices $numdevice exceeds maximum supported device count of $maxdevice ";
    else
        echo "Number of U30 devices found : $numdevice ";
    fi

    if [ "$old_shell_count" -gt "$mindevice" ] ;
    then
        echo "$old_shell_count device(s) with an older shell were detected"
        echo "This is not a supported configuration"
        return 1 2>/dev/null
        exit 1
    fi
else
  echo "ERROR: Failed to obtain device status. Aborting."
  return 1 2>/dev/null
  exit 1
fi
remove_tmp=$(rm -f $EXAMINE_FILE)

xrmadm /opt/xilinx/xcdr/scripts/xrm_commands/load_multiple_devices/load_all_devices_cmd.json

echo -----Load xrm plugins-----
xrmadm /opt/xilinx/xcdr/scripts/xrm_commands/load_multi_u30_xrm_plugins_cmd.json
echo ---------------------------------------
