#!/bin/sh
declare -A DAB_CHANNELS=(
# Refer to docs/DAB_block_frequencies.pdf for the full document
# Band I: 47MHz to 68MHz
["2A"]="47.936e6"
["2B"]="49.648e6"
["2C"]="51.360e6"
["2D"]="53.072e6"
["3A"]="54.928e6"
["3B"]="56.640e6"
["3C"]="58.352e6"
["3D"]="60.064e6"
["4A"]="61.936e6"
["4B"]="63.648e6"
["4C"]="65.360e6"
["4D"]="67.072e6"
# Band III: 174MHz to 240MHz
["5A"]="174.928e6"
["5B"]="176.640e6"
["5C"]="178.352e6"
["5D"]="180.064e6"
["6A"]="181.936e6"
["6B"]="183.648e6"
["6C"]="185.360e6"
["6D"]="187.072e6"
["7A"]="188.928e6"
["7B"]="190.640e6"
["7C"]="192.352e6"
["7D"]="194.064e6"
["8A"]="195.936e6"
["8B"]="197.648e6"
["8C"]="199.360e6"
["8D"]="201.072e6"
["9A"]="202.928e6"
["9B"]="204.640e6"
["9C"]="206.352e6"
["9D"]="208.064e6"
["10A"]="209.936e6"
["10N"]="210.096e6"
["10B"]="211.648e6"
["10C"]="213.360e6"
["10D"]="215.072e6"
["11A"]="216.928e6"
["11N"]="217.088e6"
["11B"]="218.640e6"
["11C"]="220.352e6"
["11D"]="222.064e6"
["12A"]="223.936e6"
["12N"]="224.096e6"
["12B"]="225.648e6"
["12C"]="227.360e6"
["12D"]="229.072e6"
["13A"]="230.784e6"
["13B"]="232.496e6"
["13C"]="234.208e6"
["13D"]="235.776e6"
["13E"]="237.488e6"
["13F"]="239.200e6"
# L-Band: 1452MHz to 1491.5MHz
["LA"]="1452.960e6"
["LB"]="1454.672e6"
["LC"]="1456.384e6"
["LD"]="1458.096e6"
["LE"]="1459.808e6"
["LF"]="1461.520e6"
["LG"]="1463.232e6"
["LH"]="1464.944e6"
["LI"]="1466.656e6"
["LJ"]="1468.368e6"
["LK"]="1470.080e6"
["LL"]="1471.792e6"
["LM"]="1473.504e6"
["LN"]="1475.216e6"
["LO"]="1476.928e6"
["LP"]="1478.640e6"
["LQ"]="1480.352e6"
["LR"]="1482.064e6"
["LS"]="1483.776e6"
["LT"]="1485.488e6"
["LU"]="1487.200e6"
["LV"]="1488.912e6"
["LW"]="1490.624e6")

channel="9C"
gain="22"
block_size="64e3"
sample_rate="2.048e6"

__usage=\
"get_live_data.sh, runs rtl_sdr with DAB compatible arguments

Usage:  [-c channel block (default: ${channel})]
            Refer to script for full list
        [-b block size (default: ${block_size})]
        [-s sample rate (default: ${sample_rate})]
        [-g rf gain (default: ${gain})]
        [-h (short usage)]
"
show_help() {
    >&2 echo "$__usage"
    exit 1
}

while getopts c:b:g:s:h flag
do
    case "${flag}" in
        c) channel=${OPTARG};;
        b) block_size=${OPTARG};;
        g) gain=${OPTARG};;
        s) sample_rate=${OPTARG};;
        h) show_help;;
    esac
done

frequency=${DAB_CHANNELS[${channel}]}

if [ -z ${frequency} ]
then
    >&2 echo Invalid channel ${channel}
    exit 1
fi

(>&2 echo Selected channel ${channel} at ${frequency};
set -x;
./bin/rtl_sdr.exe -f ${frequency} -s ${sample_rate} -b ${block_size} -g ${gain};)
