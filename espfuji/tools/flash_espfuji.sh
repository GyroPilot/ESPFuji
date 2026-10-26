#!/bin/bash
# flash_espfuji.sh  23-Oct-2026: put ESPFuji on Tim's external ESProFile from the banana, over its USB port.
# usage:  bash flash_espfuji.sh backup COMn        save the firmware that is on the drive now (whole flash) FIRST
#         bash flash_espfuji.sh bench  COMn        the bench build (no ProFile emulation; requests typed on USB serial)
#         bash flash_espfuji.sh drive  COMn        the real build (ProFile emulation + mailbox) for the Lisa
#         bash flash_espfuji.sh restore COMn       put the saved firmware back
#         bash flash_espfuji.sh talk   COMn        a serial terminal on the drive (bench replies), Ctrl-] to leave
# The module: Alex's upload script uses esp32:esp32:esp32s3:CDCOnBoot=cdc; if the board carries a plain ESP32 the
# FQBN is esp32:esp32:esp32 and the builds must be redone with it (see PARALLEL_FUJINET_NOTES.md).
set -u
T=/c/Users/BananaAdmin/code/esprofile-study/toolchain; S=/c/Users/BananaAdmin/code/esprofile-study/ESProFile/sw/ESProFile
CLI="/c/Program Files/Arduino CLI/arduino-cli.exe"
FQBN=${FQBN:-esp32:esp32:esp32:PartitionScheme=huge_app}
ESPTOOL=$(ls -d /c/Users/BananaAdmin/AppData/Local/Arduino15/packages/esp32/tools/esptool_py/*/esptool.exe 2>/dev/null | head -1)
what=${1:-}; port=${2:-}
[ -n "$what" ] && [ -n "$port" ] || { sed -n 2,7p "$0"; exit 1; }
case $what in
  backup)
    mkdir -p $T/backup; stamp=$(date +%Y%m%d_%H%M)
    "$ESPTOOL" --port $port flash_id | tee $T/backup/flash_id_$stamp.txt | grep -i "detected flash size\|chip is"
    size=$(grep -i "detected flash size" $T/backup/flash_id_$stamp.txt | grep -o "[0-9]*MB" | head -1)
    bytes=$(( ${size%MB} * 1024 * 1024 ))
    echo "reading $size ($bytes bytes) ..."
    "$ESPTOOL" --port $port --baud 460800 read_flash 0 $bytes $T/backup/esprofile_original_$stamp.bin && echo "SAVED $T/backup/esprofile_original_$stamp.bin";;
  bench|drive)
    "$CLI" upload --fqbn $FQBN --port $port --input-dir $T/build_$what $S && echo "FLASHED $what build";;
  restore)
    f=$(ls -t $T/backup/esprofile_original_*.bin | head -1); echo "restoring $f"
    "$ESPTOOL" --port $port --baud 460800 write_flash 0 "$f" && echo RESTORED;;
  talk)
    py -3.12 -m serial.tools.miniterm $port 115200;;
  *) sed -n 2,7p "$0";;
esac
