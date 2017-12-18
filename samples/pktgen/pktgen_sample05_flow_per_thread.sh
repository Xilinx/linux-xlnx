#!/bin/bash
#
# Script will generate one flow per thread (-t N)
#  - Same destination IP
#  - Fake source IPs for each flow (fixed based on thread number)
#
# Useful for scale testing on receiver, to see whether silo'ing flows
# works and scales.  For optimal scalability (on receiver) each
# separate-flow should not access shared variables/data. This script
# helps magnify any of these scaling issues by overloading the receiver.
#
basedir=`dirname $0`
source ${basedir}/functions.sh
root_check_run_with_sudo "$@"

# Parameter parsing via include
source ${basedir}/parameters.sh
# Set some default params, if they didn't get set
[ -z "$DEST_IP" ]   && DEST_IP="198.18.0.42"
[ -z "$DST_MAC" ]   && DST_MAC="90:e2:ba:ff:ff:ff"
[ -z "$CLONE_SKB" ] && CLONE_SKB="0"
[ -z "$BURST" ]     && BURST=32


# Base Config
DELAY="0"  # Zero means max speed
COUNT="0"  # Zero means indefinitely

# General cleanup everything since last run
pg_ctrl "reset"

# Threads are specified with parameter -t value in $THREADS
for ((thread = 0; thread < $THREADS; thread++)); do
    dev=${DEV}@${thread}

    # Add remove all other devices and add_device $dev to thread
    pg_thread $thread "rem_device_all"
    pg_thread $thread "add_device" $dev

    # Base config
    pg_set $dev "flag QUEUE_MAP_CPU"
    pg_set $dev "count $COUNT"
    pg_set $dev "clone_skb $CLONE_SKB"
    pg_set $dev "pkt_size $PKT_SIZE"
    pg_set $dev "delay $DELAY"
    pg_set $dev "flag NO_TIMESTAMP"

    # Single destination
    pg_set $dev "dst_mac $DST_MAC"
    pg_set $dev "dst $DEST_IP"

    # Setup source IP-addresses based on thread number
    pg_set $dev "src_min 198.18.$((thread+1)).1"
    pg_set $dev "src_max 198.18.$((thread+1)).1"

    # Setup burst, for easy testing -b 0 disable bursting
    # (internally in pktgen default and minimum burst=1)
    if [[ ${BURST} -ne 0 ]]; then
	pg_set $dev "burst $BURST"
    else
	info "$dev: Not using burst"
    fi

done

# Run if user hits control-c
function print_result() {
    # Print results
    for ((thread = 0; thread < $THREADS; thread++)); do
	dev=${DEV}@${thread}
	echo "Device: $dev"
	cat /proc/net/pktgen/$dev | grep -A2 "Result:"
    done
}
# trap keyboard interrupt (Ctrl-C)
trap true SIGINT

echo "Running... ctrl^C to stop" >&2
pg_ctrl "start"

print_result
