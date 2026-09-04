#!/bin/sh
DIR=$(cd $(dirname $0); pwd)
cmake -D INPUT_FILE=$1 -P $DIR/DoxygenFileFilter.cmake #2>&1
#cat $1
