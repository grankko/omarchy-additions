# busctl represents D-Bus variants as {type, data}. Keep identities as strings
# (not JavaScript numbers), including the microsecond attachment timestamp.
def p($iface): (.[$iface] // {}) | with_entries(.value = .value.data);
def bytes: map(select(. != 0)) | implode;
.data[0] | to_entries as $objects |
{
  drives: [$objects[] | select(.value["org.freedesktop.UDisks2.Drive"]) |
    .key as $path | .value | p("org.freedesktop.UDisks2.Drive") |
    {path: $path, id: (.Id // ""), attached: ((.TimeDetected // 0) | tostring),
     name: ([.Vendor, .Model] | map(select(. != null and . != "")) | join(" ")),
     size: (.Size // 0), bus: .ConnectionBus, powerOff: (.CanPowerOff == true),
     sibling: (.SiblingId // ""), media: (.MediaAvailable != false), optical: (.Optical == true)}],
  blocks: [$objects[] | select(.value["org.freedesktop.UDisks2.Block"]) |
    .key as $path | .value as $interfaces | .value | p("org.freedesktop.UDisks2.Block") |
    {path: $path, drive: .Drive, device: ((.Device // []) | bytes),
     label: (.IdLabel // ""), type: (.IdType // ""),
     system: (.HintSystem == true),
     encrypted: (($interfaces | has("org.freedesktop.UDisks2.Encrypted")) or (.CryptoBackingDevice // "/") != "/"),
     unsupported: ((.IdUsage // "") | . != "" and . != "filesystem" and . != "crypto"),
     filesystem: ($interfaces | has("org.freedesktop.UDisks2.Filesystem")),
     mounts: [($interfaces | p("org.freedesktop.UDisks2.Filesystem") | .MountPoints // [])[] | bytes]}]
} as $state |
[$state.drives[] | select(.bus == "usb" and .media and (.optical | not)) |
  . as $selected |
  [$state.drives[] | select(.path == $selected.path or
    ($selected.sibling != "" and .sibling == $selected.sibling))] as $siblings |
  [$siblings[].path] as $paths |
  [$state.blocks[] | select(.drive as $d | $paths | index($d))] as $blocks |
  select(any($blocks[]; .system or any(.mounts[]; . == "/" or . == "/boot" or . == "/boot/efi" or . == "/efi" or . == "/home" or . == "/usr" or . == "/var")) | not) |
  {path: .path, id: .id, attached: .attached,
   group: (if .sibling != "" then .sibling else .path end), name: .name, size: .size,
   siblingCount: ($siblings | length),
   members: [$siblings[] | {path, id, attached}],
   volumes: [$blocks[] | select(.filesystem) | {path, device, label, type, mounted: (.mounts | length > 0)}],
   reason: (if any($blocks[]; .encrypted) then "Encrypted storage is not supported yet"
     elif any($blocks[]; .unsupported) then "Storage arrays and volume managers are not supported yet"
     elif any($siblings[]; .powerOff | not) then "This device does not support power-off"
     elif .id == "" or .attached == "0" then "Device identity is unavailable"
     else "" end)}]
| sort_by(.path) | unique_by(.group)
