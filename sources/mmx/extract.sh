#!/bin/bash

# Script to convert ac3 (6 channels) to mp3 (stereo)
for i in *.mkv
do
   if [ ! $(mkvinfo $i | grep A_MPEG/L3)]
     then
	# Extract ac3
	echo "/////// Extracting audio... --> $i"
	/opt/local/bin/mkvextract tracks $i 1:audio.ac3

	# Convert ac3 to mp3
	echo "/////// Converting ac3 to mp3 --> $i"
	/opt/local/bin/ffmpeg -i audio.ac3 -acodec libmp3lame audio.mp3

	# Merge mp3 to new mkv
	echo "/////// Merging to output --> $i"
	# /opt/local/bin/mkvmerge -o ${i%.[A-Za-z][0-9]*}.mkv -A $i audio.mp3
	/opt/local/bin/mkvmerge -o `echo $i | awk -F. '{print "test."$0}'` -A $i audio.mp3
	rm $i
	mv `ls test.*` `echo ${i#test.}`
	rm -rf audio.*
   else
	echo "File $i already has MP3 stereo audio!"
   fi
done
mv *.mkv done_tracks/
