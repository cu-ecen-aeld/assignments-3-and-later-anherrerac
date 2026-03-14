#!/bin/bash

if [ $# -ne 2 ]
then
    echo "Error: missing arguments"
    exit 1
fi

writefile=$1
writestr=$2

mkdir -p "$(dirname "$writefile")"

echo "$writestr" > "$writefile"

if [ $? -ne 0 ]
then
    echo "Error: could not create file"
    exit 1
fi
