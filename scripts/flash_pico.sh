#!/bin/bash

TTYDEV=/dev/ttyACM0
PICO_MOUNT=/run/media/$USER/RPI-RP2/
PICO2_MOUNT="/run/media/$USER/RP2350"
PITOOL=picotool

if [ -z "$1" ]; then
      echo "No input parameter, specify uf2 file"
      exit	
fi

sudo stty -F $TTYDEV 1200
echo waiting
PICOMOUNT=""
while true; do
    if [ -d "$PICO_MOUNT" ]; then PICOMOUNT="$PICO_MOUNT"; break; fi
    if [ -d "$PICO2_MOUNT" ]; then PICOMOUNT="$PICO2_MOUNT"; break; fi
    sleep 0.1
done
sleep 0.5
if [ "$*" = "" ]; then echo rebooting; sudo $PITOOL reboot; exit; fi
echo copying
cp $1 $PICOMOUNT
echo done
