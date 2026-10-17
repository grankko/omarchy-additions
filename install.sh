#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ID=grankko.usb-storage
[[ -n ${XDG_RUNTIME_DIR:-} && -d $XDG_RUNTIME_DIR && -O $XDG_RUNTIME_DIR ]] || { echo 'Run from your desktop session' >&2; exit 1; }
exec 9>"$XDG_RUNTIME_DIR/grankko-usb-storage.lock"
flock -n 9 || { echo 'A drive removal is in progress; retry when it finishes' >&2; exit 1; }
for tool in omarchy omarchy-shell busctl udisksctl jq flock notify-send stdbuf; do
  command -v "$tool" >/dev/null || { echo "Missing dependency: $tool" >&2; exit 1; }
done
omarchy plugin validate "$ROOT/plugins/usb-storage"
BACKUPS="$HOME/.local/state/omarchy-additions/backups"
mkdir -p "$BACKUPS" "$HOME/.config/omarchy/plugins"
BACKUP=$(mktemp -d "$BACKUPS/install-XXXXXXXX")
if [[ -f $HOME/.config/omarchy/shell.json ]]; then
  cp -p "$HOME/.config/omarchy/shell.json" "$BACKUP/shell.json"
fi
DEST="$HOME/.config/omarchy/plugins/$ID"
[[ ! -L $DEST ]] || { echo 'Existing plugin is a symlink; refusing replacement' >&2; exit 1; }
cp -a "$ROOT/plugins/usb-storage" "$BACKUP/new-plugin"
# QML engines may retain components across plugin reloads. New content gets a
# new URL, so updates load without restarting the user's entire shell.
REV=$(find "$ROOT/plugins/usb-storage" -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-16)
mkdir -p "$BACKUP/new-plugin/revisions/r$REV"
cp -a "$ROOT/plugins/usb-storage/." "$BACKUP/new-plugin/revisions/r$REV/"
jq --arg entry "revisions/r$REV/Panel.qml" '.entryPoints.barWidget = $entry' "$BACKUP/new-plugin/manifest.json" > "$BACKUP/new-plugin/manifest.next.json"
mv "$BACKUP/new-plugin/manifest.next.json" "$BACKUP/new-plugin/manifest.json"
omarchy plugin validate "$BACKUP/new-plugin"
if [[ -e $DEST ]]; then mv "$DEST" "$BACKUP/previous-plugin"; fi
mv "$BACKUP/new-plugin" "$DEST"
omarchy-shell shell rescanPlugins >/dev/null
for ((i=0;i<30;i++)); do
  if omarchy plugin list --json | jq -e --arg id "$ID" 'any(.[]; .id == $id)' >/dev/null; then break; fi
  sleep 0.2
done
omarchy plugin enable "$ID" --section right --before omarchy.bluetooth
printf 'Installed USB Storage. Previous files: %s\n' "$BACKUP"
