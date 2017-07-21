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

ELO_HIDRAW=$(find_elo_touch_hidraw)

if [[ ! -e ${ELO_HIDRAW} ]]; then
    echo "Cannot find ELO touch device hidraw device file" >2&
    exit 2
fi

mkdir -p /var/log/hidraw
EVENTS_LOG="/var/log/hidraw/touch.events.$(date +%F-%H%M%S).log"
NOHUB_LOG="/var/log/hidraw/nohup"

echo ${EVENT_LOGS} >> ${NOHUB_LOG}

nohup hid-recorder ${ELO_HIDRAW} > ${EVENTS_LOG} 2>&1&
