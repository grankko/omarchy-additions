import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io
import qs.Ui
import qs.Commons
import "Model.js" as Model

Panel {
  id: root
  moduleName: "grankko.usb-storage"
  ipcTarget: "grankko.usb-storage"

  property var drives: []
  property int selectedIndex: 0
  property string selectedPath: ""
  property string pendingPath: ""
  property string pendingLabel: ""
  property string message: ""
  property bool messageIsError: false
  property string storageError: ""
  property bool refreshAgain: false
  property bool stopping: false
  readonly property string helper: decodeURIComponent(Qt.resolvedUrl("scripts/usb-storage").toString().replace(/^file:\/\//, ""))
  readonly property color foreground: bar ? bar.foreground : Color.foreground

  visible: drives.length > 0 || action.running || opened
  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  function refresh() {
    if (state.running) { refreshAgain = true; return; }
    state.running = true;
  }
  function moveCursor(delta) {
    if (!drives.length) return;
    selectedIndex = (selectedIndex + delta + drives.length) % drives.length;
    selectedPath = drives[selectedIndex].path;
    list.positionViewAtIndex(selectedIndex, ListView.Contain);
  }
  function removeDrive(drive) {
    if (!drive || drive.reason || action.running) return;
    pendingPath = drive.path;
    pendingLabel = Model.title(drive);
    message = "";
    action.command = [root.helper, "remove", drive.path, drive.id, drive.attached];
    action.running = true;
  }
  function notify(title, body) {
    Quickshell.execDetached(["notify-send", "--app-name=USB Storage", "--icon=drive-removable-media", title, Model.notifyText(body)]);
  }
  onOpenedChanged: if (opened) { message = ""; refresh(); }
  Component.onCompleted: { refresh(); monitor.running = true; }
  Component.onDestruction: stopping = true

  Process {
    id: state
    command: [root.helper, "snapshot"]
    stdout: StdioCollector { id: stateOutput; waitForEnd: true }
    stderr: StdioCollector { waitForEnd: true }
    onExited: function(code, status) {
      if (code === 0) {
        try {
          var rows = JSON.parse(stateOutput.text);
          if (!Array.isArray(rows)) throw new Error("Invalid storage response");
          root.storageError = "";
          root.drives = rows;
          var found = rows.findIndex(function(d) { return d.path === root.selectedPath; });
          root.selectedIndex = found >= 0 ? found : Math.min(root.selectedIndex, Math.max(0, rows.length - 1));
          root.selectedPath = rows.length ? rows[root.selectedIndex].path : "";
          if (!rows.length && !action.running) root.close();
        } catch (e) { root.storageError = "Could not read USB storage state"; }
      } else {
        root.storageError = "Storage service unavailable. Retrying…";
      }
      if (root.refreshAgain) { root.refreshAgain = false; debounce.restart(); }
    }
  }
  Process {
    id: monitor
    command: ["stdbuf", "-oL", "udisksctl", "monitor"]
    stdout: SplitParser { onRead: debounce.restart() }
    onExited: if (!root.stopping) monitorRestart.restart()
  }
  Timer { id: monitorRestart; interval: 2000; onTriggered: { root.refresh(); monitor.running = true; } }
  Timer { id: debounce; interval: 180; onTriggered: root.refresh() }
  Timer { interval: 30000; running: true; repeat: true; onTriggered: root.refresh() }

  Process {
    id: action
    stdout: StdioCollector { id: actionOutput; waitForEnd: true }
    stderr: StdioCollector { waitForEnd: true }
    onExited: function(code, status) {
      var result;
      try { result = JSON.parse(actionOutput.text); }
      catch (e) { result = {ok: false, message: "Removal could not be completed. Check the drive and try again."}; }
      var ok = code === 0 && result.ok === true;
      root.message = result.message;
      root.messageIsError = !ok;
      root.notify(ok ? "Safe to unplug" : "USB removal failed", root.pendingLabel + ": " + result.message);
      root.pendingPath = "";
      root.refresh();
    }
  }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: "󰕓"
    onPressed: root.toggle()
    tooltipText: "USB Storage · " + root.drives.length + " connected"
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keys
    contentWidth: panel.fittedContentWidth(Style.space(400))
    contentHeight: panel.fittedContentHeight(content.implicitHeight)

    PanelKeyCatcher {
      id: keys
      anchors.fill: parent
      onMoveRequested: function(dx, dy) { if (dy !== 0) root.moveCursor(dy); }
      onActivateRequested: root.removeDrive(root.drives[root.selectedIndex])
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.moveCursor(direction); }

      Column {
        id: content
        width: parent.width
        spacing: Style.space(14)
        PanelHero {
          foreground: root.foreground
          title: "USB Storage"
          meta: root.drives.length + (root.drives.length === 1 ? " device connected" : " devices connected")
          iconComponent: Component {
            Text {
              text: "󰕓"
              color: root.foreground
              font.family: Style.font.family
              font.pixelSize: Style.font.display
            }
          }
        }
        PanelSeparator { width: parent.width }
        ListView {
          id: list
          width: parent.width
          implicitHeight: Math.min(contentHeight, Style.space(420), Math.max(Style.space(100), panel.screenH - Style.space(260)))
          clip: true
          spacing: Style.space(8)
          model: root.drives
          boundsBehavior: Flickable.StopAtBounds
          delegate: CursorSurface {
            id: row
            required property var modelData
            required property int index
            width: list.width
            implicitHeight: labels.implicitHeight + Style.space(24)
            foreground: root.foreground
            hasCursor: root.selectedIndex === index
            readonly property bool pending: root.pendingPath === modelData.path && action.running

            Column {
              id: labels
              x: Style.space(12)
              y: Style.space(12)
              width: parent.width - Style.space(24)
              spacing: Style.space(5)
              Text {
                width: parent.width; text: Model.title(row.modelData); textFormat: Text.PlainText
                color: root.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body; font.bold: true
                elide: Text.ElideRight
              }
              Text {
                width: parent.width; text: row.modelData.name; textFormat: Text.PlainText
                color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
                elide: Text.ElideRight
              }
              Text {
                width: parent.width; text: Model.detail(row.modelData); textFormat: Text.PlainText
                color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
                wrapMode: Text.Wrap
              }
              Text {
                visible: row.modelData.reason !== ""
                width: parent.width; text: row.modelData.reason; textFormat: Text.PlainText
                color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
                wrapMode: Text.Wrap
              }
              Text {
                visible: row.modelData.siblingCount > 1
                width: parent.width; text: "Removes all volumes on these linked drives"; textFormat: Text.PlainText
                color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
                wrapMode: Text.Wrap
              }
              Item { width: 1; height: Style.space(3) }
              Button {
                text: row.pending ? "Removing…" : "Safely Remove"
                enabled: !action.running && row.modelData.reason === ""
                foreground: root.foreground
                bordered: true
                onClicked: root.removeDrive(row.modelData)
                onHovered: function(hot) { if (hot) { root.selectedIndex = row.index; root.selectedPath = row.modelData.path; } }
              }
            }
            MouseArea {
              anchors.fill: parent
              z: -1
              hoverEnabled: true
              onEntered: { root.selectedIndex = row.index; root.selectedPath = row.modelData.path; }
              onClicked: { root.selectedIndex = row.index; root.selectedPath = row.modelData.path; }
            }
          }
        }
        Text {
          visible: root.message !== "" || root.storageError !== ""
          width: parent.width
          text: root.message || root.storageError; textFormat: Text.PlainText
          color: root.messageIsError ? root.foreground : Color.accent
          font.family: Style.font.family; font.pixelSize: Style.font.caption; wrapMode: Text.Wrap
        }
        Text {
          width: parent.width
          text: "Unmounts all volumes, then powers off the device."
          color: Qt.darker(root.foreground, 1.4)
          font.family: Style.font.family; font.pixelSize: Style.font.caption; wrapMode: Text.Wrap
        }
      }
    }
  }
}
