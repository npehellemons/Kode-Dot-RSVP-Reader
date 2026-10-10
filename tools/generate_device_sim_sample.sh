#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "Usage: tools/generate_device_sim_sample.sh BOOK.txt [output.json]" >&2
  exit 2
fi

input=$1
output=${2:-apps/device-sim/sample.frames.json}

build-host/rsvp_frame_dump --filename "$(basename "$input")" < "$input" > "$output"
echo "Wrote $output"
