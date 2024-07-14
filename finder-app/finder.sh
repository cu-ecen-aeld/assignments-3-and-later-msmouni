#!/bin/sh


if [ $# -lt 2 ]
then
    if [ $# -eq 1]
    then
        echo "Please specify the second argument as the text string which will be searched"
    else
        echo "Please specify a path to a directory on the filesystem as first argument"
    fi

    exit 1
else
    filesdir="$1"
    searchstr="$2"

    if ! [ -d $filesdir ]
    then
        echo "Directory $filesdir does not exist"
        exit 1
    else
        X=$(find "$filesdir" -type f | wc -l)

        Y=$(grep -r "$searchstr" "$filesdir" | wc -l)

        echo "The number of files are $X and the number of matching lines are $Y"
    fi
fi
