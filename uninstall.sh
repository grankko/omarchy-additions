#!/usr/bin/env bash
set -euo pipefail
ID=grankko.usb-storage
[[ -n ${XDG_RUNTIME_DIR:-} && -d $XDG_RUNTIME_DIR && -O $XDG_RUNTIME_DIR ]] || { echo 'Run from your desktop session' >&2; exit 1; }
exec 9>"$XDG_RUNTIME_DIR/grankko-usb-storage.lock"
flock -n 9 || { echo 'A drive removal is in progress; retry when it finishes' >&2; exit 1; }
BACKUPS="$HOME/.local/state/omarchy-additions/backups"
mkdir -p "$BACKUPS"
BACKUP=$(mktemp -d "$BACKUPS/uninstall-XXXXXXXX")
if [[ -f $HOME/.config/omarchy/shell.json ]]; then
  cp -p "$HOME/.config/omarchy/shell.json" "$BACKUP/shell.json"
fi
# Omarchy removes only this plugin's registration and retains its files as a backup.
omarchy plugin remove "$ID" --yes
printf 'Removed USB Storage. Previous bar configuration: %s\n' "$BACKUP/shell.json"
