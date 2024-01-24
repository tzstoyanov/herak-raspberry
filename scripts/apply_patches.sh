#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>

PATCH_DIR=$1

if [ -z "$PATCH_DIR" ] || ! [ -d $PATCH_DIR ]; then
	PATCH_DIR=./patches
fi

files="$PATCH_DIR/*.patch"
for f in $files
do
  patch --no-backup-if-mismatch -N -r - -p1 -i $f
done

