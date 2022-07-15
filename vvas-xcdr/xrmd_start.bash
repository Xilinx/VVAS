#!/usr/bin/env bash

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

service="xrmd"
active=$(systemctl show $service --property ActiveState | cut -d '=' -f2)
substate=$(systemctl show $service --property SubState | cut -d '=' -f2)
activate_xrm=1

if [ "${active}" = "active" ] && [ "${substate}" = "running" ] ; then
  activate_xrm=0
fi

# Check to see if XRM Daemon is active
if [ "${activate_xrm}" = "1" ]; then
  printf "xrmd is inactive, starting xrmd ... \n"
  hasSudoinPath=$(which sudo 2> /dev/null)
  # Check if sudo exists in the path
  # If user is authenticated, no interaction is needed
  if [ "$?" = "0" ]; then
    execute=$(sudo systemctl start $service 2> /dev/null)
  # Since sudo is not in the path
  # User needs to authenticate interactively
  else
    execute=$(systemctl start $service 2> /dev/null)
  fi
  rval=$?
  sleep 1
  # Check if starting xrmd succeeded
  # If it was unsuccessful, notify the user
  if [ "${rval}" != "0" ]; then
    printf "\t !! Unable to start ${service} ... \n"
    printf "\t Please restart by issuing systemctl start ${service} \n"
  fi
# xrmd is already active, exit after notifying the user
else
  printf "$service is already ${active} and ${substate} \n"
fi
