# Omarchy Additions

Build user-owned Omarchy shell plugins. Never edit `/usr/share/omarchy`.
Keep runtime dependencies consistent with native Omarchy: QML/JavaScript and
small command-line helpers. Keep public source free of credentials, local
configuration, personal paths and real device identifiers.

USB removal must never format storage, force/lazily unmount, kill applications
or infer permission from device discovery. Revalidate UDisks identities and
all affected volumes before mutation. Test failure handling with fake commands;
real removal requires the user's selection/authorization of a test drive.

Run `node --test tests/*.test.cjs`, Bash syntax checks, and
`omarchy plugin validate plugins/usb-storage` after relevant changes. Inspect the
installed UI and shell logs for QML changes. Embed screenshots of changed UI in
the final response. Install/update and uninstall must preserve unrelated shell
configuration. Do not add unattended pushes, releases or CI runs.
