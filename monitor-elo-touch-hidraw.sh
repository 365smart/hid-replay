#!/bin/bash
set -e

find_elo_touch_hidraw() {
    local f
    for f in /dev/hidraw*
    do
        if grep 'HID_ID=' /sys/class/hidraw/$(basename ${f})/device/uevent | grep '00002149:' >/dev/null; then
            echo ${f}
        fi
    done
}

kill_all_hid_recorder() {
    local pid=$(ps -ef | grep " hid-recorder ${ELO_HIDRAW}" | grep -v ' grep ' | awk '{print $2}')
    if [[ ${pid} != '' ]]; then
        echo "Killing existing hid-recorder: ${pid}"
        (set -x
        kill ${pid}
        )
    fi
}

start_hid_recorder() {
    mkdir -p /var/log/hidraw
    local reallog="/var/log/hidraw/touch.events.$(date +%F-%H%M%S).log"
    local log="/var/log/hidraw/touch.events.log"
    local nohuplog="/var/log/hidraw/nohup"

    ln -sf ${reallog} ${log}

    echo ${reallog} >> ${nohuplog}

    (set -x
    nohup hid-recorder ${ELO_HIDRAW} > ${reallog} 2>&1&
    )
    echo "Logfile: ${log}"
}

ELO_HIDRAW=$(find_elo_touch_hidraw)

if [[ ! -e ${ELO_HIDRAW} ]]; then
    echo "Cannot find ELO touch device hidraw device file" >2&
    exit 2
fi

kill_all_hid_recorder
start_hid_recorder
