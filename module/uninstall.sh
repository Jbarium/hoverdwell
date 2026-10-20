#!/system/bin/sh
am force-stop com.bariumana.hoverdwell 2>/dev/null
pm uninstall com.bariumana.hoverdwell 2>/dev/null
rm -f /data/adb/hoverdwell.conf
rm -f /data/local/tmp/hoverdwell-boot.log
rm -f /data/local/tmp/hoverdwell.log