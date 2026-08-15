#!/bin/bash
# Switch what the display shows.
#
#   mode.sh native      the C++ renderer, animated          (the gallery)
#   mode.sh edit        edit.html in Chromium, mouse editing
#   mode.sh gallery     index.html from GitHub Pages, static
#   mode.sh calibrate   calibrate.html, numbered rings
#   mode.sh status      what is running now
#
# native and the Chromium modes are mutually exclusive - both want the whole
# screen - so switching one stops the other.
set -e
CONF=/home/pi/kiosk/kiosk.conf

browser_mode() {   # $1 = url, $2 = watchdog match string
  systemctl --user stop gallery-native 2>/dev/null || true
  sed -i -E "s|^KIOSK_URL=.*|KIOSK_URL=\"$1\"|" "$CONF"
  sed -i -E "s|^KIOSK_URL_MATCH=.*|KIOSK_URL_MATCH=\"$2\"|" "$CONF"
  systemctl --user restart kiosk kiosk-watchdog
  echo "kiosk -> $1"
}

case "${1:-}" in
  native)
    # the watchdog exists to repair Chromium; with Chromium stopped on purpose
    # it would only fight us, so it goes down too
    systemctl --user stop kiosk-watchdog kiosk 2>/dev/null || true
    systemctl --user restart gallery-native
    echo "display -> native renderer"
    ;;
  edit)      browser_mode "http://127.0.0.1:8000/edit.html" "127.0.0.1:8000" ;;
  gallery)   browser_mode "https://llama-with-thumbs.github.io/newalliancegallery/?r=$(date +%s)" "newalliancegallery" ;;
  calibrate) browser_mode "https://llama-with-thumbs.github.io/newalliancegallery/calibrate.html?r=$(date +%s)" "newalliancegallery" ;;
  status)
    for u in gallery-native kiosk kiosk-watchdog gallery-editor; do
      printf '%-16s %s\n' "$u" "$(systemctl --user is-active $u 2>/dev/null)"
    done
    grep -E '^KIOSK_URL=' "$CONF"
    ;;
  *) echo "usage: $0 native|edit|gallery|calibrate|status"; exit 1 ;;
esac
