#!/bin/bash

TTYDEV=/dev/ttyACM0
PICOMOUNT=/run/media/$USER/RPI-RP2/
PITOOL=picotool

if [ -z "$1" ]; then
      echo "No input parameter, specify uf2 file"
      exit	
fi

sudo stty -F $TTYDEV 1200
echo waiting
while [ ! -d $PICOMOUNT ]; do sleep 0.1; done
sleep 0.5
if [ "$*" = "" ]; then echo rebooting; sudo $PITOOL reboot; exit; fi
echo copying
cp $1 $PICOMOUNT
echo done
