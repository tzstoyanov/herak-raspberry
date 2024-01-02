#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>

fname=$1

# $1 - bool, if to expose the param
# $2 - param name
# $3 - param value
function write_param() {
    if [ "$1" = true ]; then
        echo "extern const char $2[];" >> $fname.h
        echo "extern const int $2_len;" >> $fname.h
    fi
    echo -n "const char $2[]={" >> $fname.c
    cval=$(echo "$3" | tr -d '\n\r' | base64)
    length=${#cval}
    for ((i = 0; i < length; i++)); do
       vascii=$(echo -n "${cval:i:1}"|hexdump -ve '"%02X"')
       if [ $i -ge 1 ]; then
          echo -n ",0x$vascii" >> $fname.c
       else
          echo -n "0x$vascii" >> $fname.c
       fi
    done
    echo "};" >> $fname.c
}

# $1 - param name
function write_param_len() {
	echo "const int $1_len=sizeof($1);" >> $fname.c
}

LicenseString="// SPDX-License-Identifier: GPL-2.0-or-later"
CopyStr="// Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>"
WarnString="// The file is auto generated at compile time, do not edit manually"

# $1 - file
function write_header() {
	echo $LicenseString >> $1
	echo $CopyStr >> $1
	echo "" >> $1
	echo $WarnString >> $1
	echo "" >> $1
}

if [ -z "$fname" ] || ! [ -f $fname.txt ]; then
	echo "No valid input file, please specify the name of the params file without the 'txt' extension."
	exit 1
fi

truncate --size 0 $fname.c
truncate --size 0 $fname.h

write_header $fname.c
write_header $fname.h

echo "#ifndef _PARAMS_H_" >> $fname.h
echo "#define _PARAMS_H_" >> $fname.h
echo "" >> $fname.h

write_param false "params" $RANDOM

while read -r line || [ -n "$line" ]; do
    if [ -z "$line" ] || [ "${line:0:1}" = "#" ]; then
       continue
    fi
    tokens=( $line )
    write_param true ${tokens[0]} ${tokens[1]}
done < "$fname.txt"

while read -r line || [ -n "$line" ]; do
    if [ -z "$line" ] || [ "${line:0:1}" = "#" ]; then
       continue
    fi
    tokens=( $line )
    write_param_len ${tokens[0]}
done < "$fname.txt"

echo "" >> $fname.h
echo "#endif // _PARAMS_H_" >> $fname.h
