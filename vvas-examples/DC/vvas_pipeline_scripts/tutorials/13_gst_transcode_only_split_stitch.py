#
# Copyright 2020-2021 Xilinx, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); 
# you may not use this file except in compliance with the License. 
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software 
# distributed under the License is distributed on an "AS IS" BASIS, 
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
# See the License for the specific language governing permissions and 
# limitations under the License.
#

# This script accepts an 8-bit, YUV420, pre-encoded file and will send the encoded output to a user-defined output path.
# The aspect ratio must 16:9 and the resolution 720p or more.

# The script starts by automatically detecting the number of devices available in the system and then determines how many
# jobs can be run on each device based on the resolution of the input. The input file is then split in as many segments
# of equal length. Parallel GStreamer jobs are submited to transcode all the segments simultaneously.
# Once all the segments have been processed, GStreamer is used to concatenate
# the results and form the final output stream.


# Usage: python 13_gst_transcode_only_split_stitch.py [options]
# e.g. python 13_gst_transcode_only_split_stitch.py  -s ~/videos/bbb_sunflower_1080p_60fps_normal.mp4 -d ./output.mp4 -c h264 -b 5

# Options:
#   -h, --help            show this help message and exit
#   -s INPUT_FILE, --sourcefile=INPUT_FILE
#                         name of the input file to transcode.
#   -d OUTPUT_FILE,   --destinationfile=OUTPUT_FILE 
#                         name of the output file. must be a .mp4 file
#                         default: out.mp4
#   -c OUTPUT_FORMAT, --codec=OUTPUT_FORMAT
#                         output format <h264, hevc, h265>
#                         default: hevc
#   -b BITRATE, --bitrate=BITRATE
#                         output bitrate in Mbit/s. Must be a float or integer value between 1.0 and 25.0
#                         default: 5.0
#                         (example: use -b 3 to specify an output bitrate of 3Mbits/sec)
#   -j NUM_JOBS, --numjobs=NUM_JOBS
#                         number of transcode jobs per device
#                         default: -1 (let the script determine the number of
#                         jobs)
#   -n NUM_DEVICES, --numdevices=NUM_DEVICES
#                         number of devices on which to transcode the segments
#                         default: -1 (use all devices)
#   -x ENCODE_OPTIONS, --extra=ENCODE_OPTIONS
#                         additional options for the encoder, specified as a
#                         string. bitrate values set with this options take
#                         precedence over values set with -b.
#                         example: use -x "-bf 1" to set the number of B frames
#                         to 1

import subprocess
from optparse import OptionParser
import time
from datetime import datetime
from datetime import timedelta
import json
import math
import re
import os.path
import sys
from fractions import Fraction

if sys.version_info[0] < 3:
    print("ERROR: This script requires Python 3")
    raise SystemExit(-1)

def check_system_utilization():
    # Generate resource utilization report and load the resulting JSON output into a Python dictionary
    rc = subprocess.run("xrmadm /opt/xilinx/xrm/test/list_cmd.json", shell = True, stderr=subprocess.PIPE, stdout=subprocess.PIPE, universal_newlines=True)
    status = json.loads( str(rc.stdout) )

    # Get the number of devices from the JSON outputs
    num_devices = int(status['response']['data']['deviceNumber'])

    # For each device
    for id in range(num_devices):
            # Extract resource utilization information
            for c in [0,3,4]:
                cuData       = status['response']['data']['device_'+str(id)]['cu_'+str(c)]
                cuName       = cuData['kernelName   ']
                numChanInuse = cuData['numChanInuse ']
                usedLoad     = cuData['usedLoad     ']
                reservedLoad = cuData['reservedLoad ']

                numChanInuseValue = int(numChanInuse)
                usedLoadValue = int(usedLoad.split()[0])
                reservedLoadValue = int(reservedLoad.split()[0])

                # If any decoding, scaling or encoding resource is used or reserved, return -1 to indicate the system is not entirely free
                if (numChanInuseValue > 0) or (usedLoadValue > 0) or (reservedLoadValue > 0):
                    return -1

    return 0

def progress_meter(percent_donePrev, frames_in_clip, num_segments, running=True):
    if running==False:
        percent_doneNew = 100
        print(" " + str(percent_doneNew).rjust(3) + " percent of transcoding completed")
        return percent_doneNew
    else:
        percent_doneNew = percent_donePrev
        output = subprocess.Popen("tail -1 stdout0000.log", shell = True, stdout = subprocess.PIPE, stderr = subprocess.PIPE, universal_newlines=True).stdout.read()
        outputS = str(output)
        outputpartS = outputS[-150:]
        result = outputpartS.find('rendered:')
        if result != -1:
            frameS = outputpartS[result+10:result+20].split()
            frame = int(frameS[0].partition(",")[0])
            percent_done = int(100.0 * frame/(frames_in_clip/num_segments))
            if ((percent_done-percent_donePrev) >= 10):
                percent_doneNew = int(percent_done/10)*10
                if (percent_doneNew<100):
                    print(" " + str(percent_doneNew).rjust(3) + " percent of transcoding completed")
        return percent_doneNew

def get_number_of_devices():
    p = subprocess.run("xbutil examine | grep -c xilinx_u30", shell = True, stdout = subprocess.PIPE, stderr = subprocess.PIPE, universal_newlines=True)
    if (p.returncode!=0):
        print ("Can't determine number of U30 cards in the system, exiting ...")
        raise SystemExit(-1)
    num_devices = int(str(p.stdout))
    if (num_devices<1):
        print ("No U30 cards found in the system, exiting ...")
        raise SystemExit(-1)
    return num_devices

def get_clip_properties(filename):
    # check that input file exists
    if os.path.exists(filename) == False:
        print ("Input file "+filename+ " not found")
        raise SystemExit(-1)

    # extract stream properties using gst-discoverer-1.0 and put them in a JSON string
    try:
        output = subprocess.Popen("gst-discoverer-1.0 -v "+filename+" | grep Width: 2>&1",
                              shell = True,
                              stdout = subprocess.PIPE, universal_newlines=True).stdout.read()
        outputS = str(output)
        result = outputS.find('Width:')
        width = int(re.search(r'\d+', outputS).group())
    except ValueError as e:
        print("Couldn't extract input stream properties. aborting.")
        raise SystemExit(1)

    try:
        output = subprocess.Popen("gst-discoverer-1.0 -v "+filename+" | grep Height: 2>&1",
                              shell = True,
                              stdout = subprocess.PIPE, universal_newlines=True).stdout.read()
        outputS = str(output)
        result = outputS.find('Height:')
        height = int(re.search(r'\d+', outputS).group())
    except ValueError as e:
        print("Couldn't extract input stream properties. aborting.")
        raise SystemExit(1)

    try:
        output = subprocess.Popen("gst-discoverer-1.0 -v "+filename+" | grep 'Frame rate' 2>&1",
                              shell = True,
                              stdout = subprocess.PIPE, universal_newlines=True).stdout.read()
        outputS = str(output)
        result = outputS.find('Frame rate:')
        tmpS = outputS[result+11:]
        framerate = tmpS.strip()
        framerate = framerate.split( '/' ) # convert fraction string to (num,den) tuple
        fps = float(framerate[0])/float(framerate[1])
    except ValueError as e:
        print("Couldn't extract input stream properties. aborting.")
        raise SystemExit(1)

    try:
        output = subprocess.Popen("gst-discoverer-1.0 -v "+filename+" | grep Duration: 2>&1",
                              shell = True,
                              stdout = subprocess.PIPE, universal_newlines=True).stdout.read()
        outputS = str(output)
        result = outputS.find('Duration:')
        video_lengthS = outputS[result+10:].split(".")[0] + "." + outputS[result+10:].split(".")[1][:6]
        pt = datetime.strptime(video_lengthS,'%H:%M:%S.%f')
        duration_sec = pt.microsecond * (10**-6) + pt.second + pt.minute*60 + pt.hour*3600
        nb_frames = math.ceil(fps * duration_sec)
    except ValueError as e:
        print("Couldn't extract input stream properties. aborting.")
        raise SystemExit(1)

    try:
        output = subprocess.Popen("gst-discoverer-1.0 -v "+filename+" | grep video | grep codec 2>&1",
                              shell = True,
                              stdout = subprocess.PIPE, universal_newlines=True).stdout.read()
        outputS = str(output)
        result = outputS.find('codec')
        codec_tmp = int(re.search(r'\d+', outputS).group())
        codec_num = re.sub("[^0-9]", "", str(codec_tmp))
        if codec_num == "264":
            codec_name = "h264"
        if codec_num == "265":
            codec_name = "hevc"
    except ValueError as e:
        print("Couldn't extract input stream properties. aborting.")
        raise SystemExit(1)

    print("")
    print("Video clip parameters")
    print("  format             : " + codec_name)
    print("  resolution         : " + str(width)+"x"+str(height))
    print("  duration           : " + str(timedelta(seconds=duration_sec)))
    print("  total num frames   : " + str(nb_frames))
    print("  framerate          : " + str(fps) + " fps")

    return (width, height, duration_sec, framerate, nb_frames, codec_name)

def get_max_segments_per_device(width, height, input_format, output_format, framerate_num, framerate_den, num_devices_present):

    # Create a JSON job description
    job_description = {
        "request": {
            "name": "transcode_job",
            "request_id": 1,
            "parameters": {
                "name": "transcode_job_params",
                "resources":
                [
                    {
                        "function": "DECODER",
                        "format":   input_format,
                        "resolution": { "input": { "width": width, "height": height, "frame-rate": { "num": int(framerate_num), "den":int(framerate_den)} } }
                    },
                    {
                        "function": "ENCODER",
                        "format":   output_format,
                        "resolution": { "input": { "width": width, "height": height, "frame-rate": { "num": int(framerate_num), "den":int(framerate_den)} } }
                    }
                ]
            }
        }
    }

    # Write job description to file
    jsonString = json.dumps(job_description)
    jsonFile = open("./xiljobdescription.json", "w")
    jsonFile.write(jsonString)
    jsonFile.close()

    # Launch job slot reservation tool
    sp = subprocess.Popen("jobSlotReservation ./xiljobdescription.json", shell = True, stdin = subprocess.PIPE, stdout = subprocess.PIPE, stderr = subprocess.PIPE)
    # Termine the job slot reservation session by sending ENTER to the program
    sp.communicate(input='\n'.encode())[0]
    # Count how many job slots were reserved for that specific job configuration
    sp = subprocess.run("grep -c XRM_RESERVE_ID /var/tmp/xilinx/xrm_jobReservation.sh", shell = True, stdout = subprocess.PIPE, universal_newlines=True)

    # Determine how many job can be run per device
    max_tot_segments = int(sp.stdout)
    max_dev_segments = int(max_tot_segments/num_devices_present)

    return max_dev_segments

def count_substrings(string, substring):
    string_size = len(string)
    substring_size = len(substring)
    count = 0
    for i in range(0,string_size-substring_size+1):
        if string[i:i+substring_size] == substring:
            count+=1
    return count

def main():

    print("")
    print("DISCLAIMER: This example script is provided for demonstration purposes")
    print("It is not intended to work for all input clips and all use cases.     ")
    print("")

    if check_system_utilization() < 0:
        print ("Some jobs are already running and using Xilinx resources. Make sure all job finish before running this script.")
        return -1

    (input_file, output_file, output_format, bitrate, num_jobs_requested, num_devices_requested, encode_options) = parse_options()
    (width, height, duration_sec, framerate, frames_in_clip, input_format) = get_clip_properties(input_file)

    num_devices_present = get_number_of_devices()

    if output_format == "h265":
            output_format = "hevc"
    if output_format != "hevc" and output_format != "h264":
        print ("Output format needs to be h264, h265 or hevc")
        raise SystemExit(-1)

    if bitrate < 1.0 or bitrate > 25.0:
        print ("Bitrate should be between 1.0 ... 25.0 Mbit/s")
        raise SystemExit(-1)
    bitrate =str(int(bitrate*1000))

    if output_file[-4:] != ".mp4":
        print ("Only .mp4 output file format supported")
        raise SystemExit(-1)
    if input_file[-4:] != ".mp4" and input_file[-4:] != ".mov" and input_file[-4:] != ".mkv" and input_file[-4:] != ".MOV":
        print ("Only .mp4, .mov and .mkv input file format supported")
        raise SystemExit(-1)
    
    if input_file == output_file:
        print ("Source and destination file names cannot be the same")
        raise SystemExit(-1)

    if input_format == "h265":
        input_format = "hevc"
    if input_format != "hevc" and input_format != "h264":
        print ("Input encoder needs to be h264, h265 or hevc")
        raise SystemExit(-1)

    if input_format == "hevc":
        input_parser = "h265parse"
    if input_format == "h264":
        input_parser = "h264parse"

    if output_format == "hevc":
        output_parser = "h265parse"
    if output_format == "h264":
        output_parser = "h264parse"

    if num_devices_requested <= 0:
        num_devices_requested = num_devices_present
    
    if num_devices_requested > num_devices_present:
        print ("Number of devices requested exceeds the number of devices available (" + str(num_devices_requested) + " vs " + str(num_devices_present) + ")")
        num_devices_requested = num_devices_present

    enabled_devices = num_devices_requested

    num_jobs_estimated = get_max_segments_per_device(width, height, input_format, output_format, framerate[0], framerate[1], num_devices_present)

    if (num_jobs_requested>0):
        num_segments_attempted = num_jobs_requested * enabled_devices
    else:
        num_segments_attempted = num_jobs_estimated * enabled_devices
        num_jobs_requested = "default (use estimated number)"

    # Make sure there are no leftover files from previous runs
    cleanup()

    print("")
    print("System parameters")
    print("  number of available devices : " + str(num_devices_present))
    print("  number of devices used      : " + str(enabled_devices))

    startSec = time.time()

    # Split the input clip into segments
    print("")
    print("Splitting input clip in independent segments")
    print("  number of jobs per device (estimated)   : "+str(num_jobs_estimated))
    print("  number of jobs per device (requested)   : "+str(num_jobs_requested))
    print("  number of segments attempted            : "+str(num_segments_attempted))

    if num_segments_attempted > 1:
        max_segment_duration = int(duration_sec / num_segments_attempted) * (10**9)
        if input_file[-4:] == ".mp4" or input_file[-4:] == ".mov":
            split_cmd = "gst-launch-1.0 filesrc location=" + \
                    input_file + \
                    " ! qtdemux ! splitmuxsink muxer=matroskamux max-size-time=" + str(max_segment_duration) + " location=tmpfile" + "%04d." + "mkv" + " > stdout.log 2>&1 \n"
        elif input_file[-4:] == ".mkv":
            split_cmd = "gst-launch-1.0 filesrc location=" + \
                    input_file + \
                    " ! matroskademux ! splitmuxsink muxer=matroskamux max-size-time=" + str(max_segment_duration) + " location=tmpfile" + "%04d." + "mkv" + " > stdout.log 2>&1 \n"
    else:
        split_cmd = "gst-launch-1.0 filesrc location=" + \
                input_file + \
                " ! qtdemux ! matroskamux ! filesink location=tmpfile0000.mkv " + " > stdout.log 2>&1 \n"
    output = subprocess.run(split_cmd, shell = True)

    # Check how many segments were created
    extra_chunks = 0
    proc = subprocess.run("ls tmpfile* | wc -l", shell = True, stdout = subprocess.PIPE, universal_newlines=True)
    num_segments = int(str(proc.stdout))
    print("  actual number of segments created       : "+str(num_segments))
    print("  average segment duration                : "+str(int(duration_sec / num_segments_attempted)) + " seconds")
    if num_segments < num_segments_attempted:
        print("  (!) input clip is too short and does not contain enough GOPs to be split in more segments")
        num_segments_attempted = num_segments
    if num_segments > num_segments_attempted:
        extra_chunks = num_segments - num_segments_attempted
        print ("  (!) " + str(extra_chunks) + " more segments than expected were created")

    # Launch all parallel transcoding jobs
    print ("")
    print ("Transcoding all segments in parallel to " + output_format)
    procList = []
    for n in range(0, num_segments_attempted):
        device_id = n % enabled_devices
        inputfile  = "tmpfile" + format(n, '04d') + ".mkv"
        outputfile = "tmpfileout" + format(n, '04d') + output_file[-4:]
        logfile    = "stdout" + format(n, '04d') +".log"
        transcode_cmd = "gst-launch-1.0 filesrc location=" + inputfile + \
                        " ! matroskademux ! " + input_parser + " ! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx="+str(device_id)+" ! queue ! " \
                        "vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx="+str(device_id)+" target-bitrate="+ str(bitrate) +" max-bitrate="+ str(bitrate) + " " + encode_options + \
                        " ! " + output_parser + " ! qtmux ! fpsdisplaysink video-sink=\"filesink location=" + outputfile + "\" text-overlay=false sync=false -v" + \
                        " > " + logfile + " 2>&1 "
        procList.append( subprocess.Popen(transcode_cmd, shell = True, stdout=subprocess.PIPE, stderr=subprocess.PIPE) )

    # Wait for all parallel transcoding jobs to finish
    proc_running = True
    percent_done = 0
    while proc_running:
        time.sleep(0.1)
        proc_running = False
        for proc in procList:
            if proc.poll() == None:
                proc_running = True
        percent_done = progress_meter(percent_done, frames_in_clip, num_segments, proc_running)

    for proc in procList:
        if proc.returncode != 0:
            proc = subprocess.run("grep -l \"allocate\" stdout*.log | wc -l", shell = True, stdout = subprocess.PIPE, universal_newlines=True)
            if int(str(proc.stdout)) > 0:
                print("  (!) one or more gstreamer segment transcoding jobs failed with resource allocation error.")
                print("      consider rerunning with the -j option to request a smaller number of jobs per device. aborting.")
            else:
                print("  (!) one or more gstreamer segment transcoding jobs didn't finish successfully, check log files for details. aborting.")
            raise SystemExit(-1)

    for n in range(0, extra_chunks):
        device_id = n % enabled_devices
        inputfile  = "tmpfile" + format(n + num_segments_attempted, '04d') + ".mkv"
        outputfile = "tmpfileout" + format(n + num_segments_attempted, '04d') + output_file[-4:]
        logfile    = "stdout" + format(n + num_segments_attempted, '04d') +".log"
        transcode_cmd = "gst-launch-1.0 filesrc location=" + inputfile + \
                        " ! matroskademux ! " + input_parser + " ! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx="+str(device_id)+" ! queue ! " \
                        "vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx="+str(device_id)+" target-bitrate="+ str(bitrate) +" max-bitrate="+ str(bitrate) + " " + encode_options + \
                        " ! " + output_parser + " ! qtmux ! fpsdisplaysink video-sink=\"filesink location=" + outputfile + "\" text-overlay=false sync=false -v" + \
                        " > " + logfile + " 2>&1 "
        procList.append( subprocess.Popen(transcode_cmd, shell = True, stdout=subprocess.PIPE, stderr=subprocess.PIPE))

    # Wait for all parallel transcoding jobs to finish
    proc_running = True
    percent_done = 0
    while proc_running:
        time.sleep(0.1)
        proc_running = False
        for proc in procList:
            if proc.poll() == None:
                proc_running = True

    # Concatenate the transcoded files
    print("")
    print ("Concatenating segments into final clip")
    output = subprocess.run("rm -f " + output_file, shell = True)
    concat_cmd = "gst-launch-1.0 splitmuxsrc location=tmpfileout* ! " + output_parser + " ! qtmux ! filesink location=" + output_file + " > stdout.log 2>&1"
    output = subprocess.Popen(concat_cmd, shell = True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    endSec = time.time()

    # Print performance results
    totSec = int(endSec-startSec)
    print(" ")
    print("Performance summary")
    print("  total processing time        : " + str(timedelta(seconds=totSec)) )
    print("  effective processing rate    : " + str(round(frames_in_clip/totSec,2)) + " fps")
    print("  effective processing speed   : " + str(round(1.0*duration_sec/totSec,1))+" times faster than realtime")
    print(" ")

    # Delete temporary files
    cleanup()

def cleanup():
    time.sleep(1)
    print("cleanup...")
    subprocess.run("rm -f tmpfile* 2> /dev/null", shell = True)
    subprocess.run("rm -f stdout*.log 2> /dev/null", shell = True)
    subprocess.run("rm -f xiljobdescription.json", shell = True)

def round_up(n, decimals=0):
    multiplier = 10 ** decimals
    return math.ceil(n * multiplier) / multiplier

def parse_options():
    parser = OptionParser()
    parser.add_option("-s", "--sourcefile",
                      dest = "input_file",
                      help = "name of the input file to transcode. \
                              only .mp4, .mkv and .mov files are supported.",
                      type = "string",
                      action = "store"
    )
    parser.add_option("-d", "--destinationfile",
                      dest = "output_file",
                      help = "name of the output file. must be a .mp4 file \
                              default: out.mp4",
                      type = "string",
                      action = "store", default = "out.mp4"
    )
    parser.add_option("-c", "--codec",
                      dest = "output_format",
                      help = "output format <h264, hevc, h265> \
                              default: hevc",
                      type = "string",
                      action = "store", default = "hevc"
    )
    parser.add_option("-b", "--bitrate",
                      dest = "bitrate",
                      help = "output bitrate in Mbit/s. must be a float or integer value between 1.0 and 25.0 \
                              default: 5.0 \
                              \
                              example: use -b 3 to specify an output bitrate of 3Mbits/sec) ",
                      type = "float",
                      action = "store", default = 5.0
    )
    parser.add_option("-j", "--numjobs",
                      dest = "num_jobs",
                      help = "number of transcode jobs per device  \
                              default: -1 (let the script determine the number of jobs)",
                      type = "int",
                      action = "store", default = 0
    )
    parser.add_option("-n", "--numdevices",
                      dest = "num_devices",
                      help = "number of devices on which to transcode the segments  \
                              default: -1 (use all devices)",
                      type = "int",
                      action = "store", default = 0
    )
    parser.add_option("-x", "--extra",
                      dest = "encode_options",
                      help = "additional options for the encoder, specified as a string. bitrate values set with this options take precedence over values set with -b. \
                              \
                              example: use -x \"b-frames=1\" to set the number of B frames to 1",
                      type = "string",
                      action = "store", default = ""
    )
    (options, args) = parser.parse_args()
    if options.input_file:
        return (options.input_file, options.output_file, \
                options.output_format, options.bitrate, \
                options.num_jobs, options.num_devices, \
                options.encode_options)
    else:
        parser.print_help()
        raise SystemExit(0)

if __name__ == '__main__':
	try:
		main()
	# When 'Ctrl+C' is pressed, clean-up temp files and exit.
	except KeyboardInterrupt:
            print("Exiting ...")
            cleanup()
