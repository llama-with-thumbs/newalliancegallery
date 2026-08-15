#!/bin/bash
# Install the native renderer on the Pi. Run it ON the Pi, from this folder,
# after copying the repo's native/ directory to /home/pi/gallery/native/.
#
#   bash pi-install.sh
#
# Idempotent: safe to re-run after pulling a new main.cpp.
set -e
cd "$(dirname "$0")"

if ! pkg-config --exists sdl2; then
  echo "installing libsdl2-dev..."
  sudo apt-get update -qq
  sudo apt-get install -y libsdl2-dev
fi

bash build.sh

install -m 755 mode.sh /home/pi/gallery/mode.sh
mkdir -p "$HOME/.config/systemd/user"
install -m 644 gallery-native.service "$HOME/.config/systemd/user/gallery-native.service"

systemctl --user daemon-reload
systemctl --user enable gallery-native.service

echo
echo "installed. switch the display with:"
echo "  /home/pi/gallery/mode.sh native      animated gallery"
echo "  /home/pi/gallery/mode.sh edit        mouse editor"
echo "  /home/pi/gallery/mode.sh status"
