#!/bin/bash
#
# Multiqueue: Using pktgen threads for sending on multiple CPUs
#  * adding devices to kernel threads
#  * notice the naming scheme for keeping device names unique
#  * nameing scheme: dev@thread_number
#  * flow variation via random UDP source port
#
basedir=`dirname $0`
source ${basedir}/functions.sh
root_check_run_with_sudo "$@"
#
# Required param: -i dev in $DEV
source ${basedir}/parameters.sh

# Base Config
DELAY="0"        # Zero means max speed
COUNT="100000"   # Zero means indefinitely
[ -z "$CLONE_SKB" ] && CLONE_SKB="0"

# Flow variation random source port between min and max
UDP_MIN=9
UDP_MAX=109

# (example of setting default params in your script)
if [ -z "$DEST_IP" ]; then
    [ -z "$IP6" ] && DEST_IP="198.18.0.42" || DEST_IP="FD00::1"
fi
[ -z "$DST_MAC" ] && DST_MAC="90:e2:ba:ff:ff:ff"

# General cleanup everything since last run
pg_ctrl "reset"

# Threads are specified with parameter -t value in $THREADS
for ((thread = 0; thread < $THREADS; thread++)); do
    # The device name is extended with @name, using thread number to
    # make then unique, but any name will do.
    dev=${DEV}@${thread}

    # Add remove all other devices and add_device $dev to thread
    pg_thread $thread "rem_device_all"
    pg_thread $thread "add_device" $dev

    # Notice config queue to map to cpu (mirrors smp_processor_id())
    # It is beneficial to map IRQ /proc/irq/*/smp_affinity 1:1 to CPU number
    pg_set $dev "flag QUEUE_MAP_CPU"

    # Base config of dev
    pg_set $dev "count $COUNT"
    pg_set $dev "clone_skb $CLONE_SKB"
    pg_set $dev "pkt_size $PKT_SIZE"
    pg_set $dev "delay $DELAY"

    # Flag example disabling timestamping
    pg_set $dev "flag NO_TIMESTAMP"

    # Destination
    pg_set $dev "dst_mac $DST_MAC"
    pg_set $dev "dst$IP6 $DEST_IP"

    # Setup random UDP port src range
    pg_set $dev "flag UDPSRC_RND"
    pg_set $dev "udp_src_min $UDP_MIN"
    pg_set $dev "udp_src_max $UDP_MAX"
done

# start_run
echo "Running... ctrl^C to stop" >&2
pg_ctrl "start"
echo "Done" >&2

# Print results
for ((thread = 0; thread < $THREADS; thread++)); do
    dev=${DEV}@${thread}
    echo "Device: $dev"
    cat /proc/net/pktgen/$dev | grep -A2 "Result:"
done
