#!/system/bin/sh
MODDIR=${0%/*}
CONF=/data/adb/hoverdwell.conf
PKG=com.bariumana.hoverdwell
LOG=/data/local/tmp/hoverdwell-boot.log
SDK=$(getprop ro.build.version.sdk)

exec >"$LOG" 2>&1
log() { echo "$(date '+%H:%M:%S') $*"; }

[ -f "$CONF" ] || { cp "$MODDIR/config.conf" "$CONF"; log "created $CONF"; }

while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 2; done
sleep 5
log "boot completed, sdk=$SDK"

chmod 755 "$MODDIR/bin/dwell"

# 1. make sure the overlay app is installed (retry: package manager can be slow)
n=0
until pm list packages | grep -q "package:$PKG\$"; do
  n=$((n+1))
  [ $n -gt 6 ] && { log "FAILED to install app"; break; }
  log "installing app (try $n)"
  pm install -r "$MODDIR/hoverdwell.apk"
  sleep 5
done

start_service() {
  if [ "$SDK" -ge 26 ]; then
    am start-foreground-service -n $PKG/.OverlayService
  else
    am startservice -n $PKG/.OverlayService
  fi
}

# 2. permission + service, re-checked every 15 s
(
  while true; do
    appops set $PKG SYSTEM_ALERT_WINDOW allow
    if ! dumpsys activity services $PKG 2>/dev/null | grep -q OverlayService; then
      log "overlay service not running, starting it"
      start_service
    fi
    sleep 15
  done
) &

# 3. enable hover in the touch driver
NODE=$(ls /sys/class/sec/tsp/proximity_enables 2>/dev/null)
[ -z "$NODE" ] && NODE=$(find /sys/devices -name proximity_enables 2>/dev/null | head -n 1)
log "proximity node: ${NODE:-not found}"
( while true; do [ -n "$NODE" ] && echo 0x03 > "$NODE" 2>/dev/null; sleep 10; done ) &

# 4. daemon, restarted if it dies
( while true; do "$MODDIR/bin/dwell" "$CONF" 2> /data/local/tmp/hoverdwell.log; sleep 3; done ) &
log "started"