#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>

top_dir=$1
build_dir=$2
version_file=$top_dir/include/version.h

function ver_get() {
   local val="$(cat $version_file | grep $1 | awk '{print $3;}' | sed 's/"//g')"
   echo $val
}

DNAME=$(ver_get IMAGE_NAME)
VER=$(ver_get PROJECT_VERSION)
IFILE=$(ver_get IMAGE_FILE)
BDATE=$(ver_get BUILD_DATE)
BTIME=$(ver_get BUILD_TIME)
COMMIT=$(ver_get GIT_COMMIT_HASH)

if [ -z "$build_dir" ] || ! [ -f $build_dir/$IFILE ]; then
	echo "Invalid build file."
	exit 1
fi

if [ -z "$top_dir" ] || ! [ -f $version_file ]; then
	echo "Invalid version file."
	exit 1
fi


meta_file=$DNAME.meta
SHA=$(sha256sum $build_dir/$IFILE | awk '{print $1;}')

echo "image: $DNAME" > $build_dir/$meta_file
echo "file: $IFILE" >> $build_dir/$meta_file
echo "SHA: $SHA" >> $build_dir/$meta_file
echo "version: $VER" >> $build_dir/$meta_file
echo "commit: $COMMIT" >> $build_dir/$meta_file
echo "build date: $BDATE" >> $build_dir/$meta_file
echo "build time: $BTIME" >> $build_dir/$meta_file
