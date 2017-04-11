#!/bin/sh
# Copyright (c) 2016 Google, Inc
# Written by Simon Glass <sjg@chromium.org>
#
# Check that the u-boot.cfg file provided does not introduce any new
# ad-hoc CONFIG options
#
# You can generate the list of current ad-hoc CONFIG options (those which are
# not in Kconfig) with this command:
#
# export LC_ALL=C LC_COLLATE=C
# git grep CONFIG_ |tr ' \t' '\n\n' |sed -n 's/^\(CONFIG_[A-Z0-9_]*\).*/\1/p' \
#	|sort |uniq >scripts/config_whitelist.txt;
# unset LC_ALL LC_COLLATE

# Usage
#    check-config.sh <path to u-boot.cfg> <path to whitelist file> <source dir>
#
# For example:
#   scripts/check-config.sh b/chromebook_link/u-boot.cfg kconfig_whitelist.txt .

path="$1"
whitelist="$2"
srctree="$3"

# Temporary files
configs="${path}.configs"
suspects="${path}.suspects"
ok="${path}.ok"
new_adhoc="${path}.adhoc"

export LC_ALL=C
export LC_COLLATE=C

cat ${path} |sed -n 's/^#define \(CONFIG_[A-Za-z0-9_]*\).*/\1/p' |sort |uniq \
	>${configs}

comm -23 ${configs} ${whitelist} > ${suspects}

cat `find ${srctree} -name "Kconfig*"` |sed -n \
	-e 's/^config *\([A-Za-z0-9_]*\).*$/CONFIG_\1/p' \
	-e 's/^menuconfig \([A-Za-z0-9_]*\).*$/CONFIG_\1/p' |sort |uniq > ${ok}
comm -23 ${suspects} ${ok} >${new_adhoc}
if [ -s ${new_adhoc} ]; then
	echo "Error: You must add new CONFIG options using Kconfig"
	echo "The following new ad-hoc CONFIG options were detected:"
	cat ${new_adhoc}
	echo
	echo "Please add these via Kconfig instead. Find a suitable Kconfig"
	echo "file and add a 'config' or 'menuconfig' option."
	# Don't delete the temporary files in case they are useful
	exit 1
else
	rm ${suspects} ${ok} ${new_adhoc}
fi
