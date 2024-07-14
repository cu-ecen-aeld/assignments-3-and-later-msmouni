#!/bin/sh

if [ $# -lt 2 ]
then
    if [ $# -eq 1]
    then
        echo "Please specify the second argument as the text string which will be written within this file specified with the first argument"
    else
        echo "Please specify a full path to a file (including filename) on the filesystem as first argument"
    fi

    exit 1
else
    writefile="$1"
    writestr="$2"

    mkdir -p $(dirname "$writefile")

    echo "$writestr" > "$writefile"

    if [ $? -ne 0 ]; then
        echo "Error: Could not create file $writefile"
        exit 1
    fi
fi
