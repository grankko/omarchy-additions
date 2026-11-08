import QtQuick
import Quickshell
import Quickshell.Io
import qs.Ui
import qs.Commons
import "Model.js" as Model

Panel {
  id: root
  moduleName: "grankko.hardware-status"
  ipcTarget: "grankko.hardware-status"

  property var reading: Model.parse("")
  property string sampleError: ""
  readonly property string helper: decodeURIComponent(Qt.resolvedUrl("scripts/sample").toString().replace(/^file:\/\//, ""))
  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property var averageSpeed: Model.averageGhz(reading.cpus)
  readonly property var maxSpeed: Model.highestGhz(reading.cpus, "maxGhz")
  readonly property var configuredLimit: Model.highestGhz(reading.cpus, "limitGhz")
  readonly property var primarySensor: Model.primaryTemperature(reading.sensors)
  readonly property real usedKiB: reading.totalKiB === null || reading.availableKiB === null ? -1 : Math.max(0, reading.totalKiB - reading.availableKiB)
  readonly property string barText: Model.barText(reading)

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  function refresh() { if (!sample.running) sample.running = true }
  onOpenedChanged: if (opened) refresh()
  Component.onCompleted: refresh()

  Timer { interval: 3000; repeat: true; running: true; onTriggered: root.refresh() }
  Process {
    id: sample
    command: [root.helper]
    stdout: StdioCollector { id: output; waitForEnd: true }
    stderr: StdioCollector { waitForEnd: true }
    onExited: function(code, status) {
      if (code === 0) { root.reading = Model.parse(output.text); root.sampleError = "" }
      else root.sampleError = "Hardware readings unavailable"
    }
  }

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.barText
    tooltipText: "RAM used · average CPU speed · CPU temperature"
    horizontalMargin: 6
    pressFeedbackButtons: Qt.NoButton
    onPressed: if (root.opened) root.close(); else root.open()
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keys
    contentWidth: panel.fittedContentWidth(Style.space(330))
    contentHeight: panel.fittedContentHeight(content.implicitHeight)

    PanelKeyCatcher {
      id: keys
      anchors.fill: parent
      onCloseRequested: root.close()

      Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
          id: content
          width: scroller.width
          spacing: Style.space(12)

          PanelHero {
            foreground: root.foreground
            title: "Hardware Status"
            iconComponent: Component {
              Text { text: "󰍛"; textFormat: Text.PlainText; color: root.foreground; font.family: Style.font.family; font.pixelSize: Style.font.display }
            }
          }
          PanelSeparator { width: parent.width }
          PanelSectionHeader { text: "Memory" }
          Text {
            text: "Used  " + Model.gib(root.usedKiB < 0 ? null : root.usedKiB)
                  + "     Available  " + Model.gib(root.reading.availableKiB)
            textFormat: Text.PlainText; color: root.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body
          }
          Text {
            text: "Total  " + Model.gib(root.reading.totalKiB)
            textFormat: Text.PlainText; color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
          }
          PanelSectionHeader { text: "Swap" }
          Text {
            text: "Used  " + Model.gib(root.reading.swapTotalKiB === null || root.reading.swapFreeKiB === null ? null : Math.max(0, root.reading.swapTotalKiB - root.reading.swapFreeKiB))
                  + "     Available  " + Model.gib(root.reading.swapFreeKiB)
            textFormat: Text.PlainText; color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
          }
          Text {
            text: "Total  " + Model.gib(root.reading.swapTotalKiB)
            textFormat: Text.PlainText; color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
          }
          PanelSeparator { width: parent.width }
          PanelSectionHeader { text: "CPU speed" }
          Row {
            spacing: Style.space(20)
            Text {
              text: "Average  " + Model.ghz(root.averageSpeed)
              textFormat: Text.PlainText; color: root.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body
            }
            Text {
              text: "Max  " + Model.ghz(root.maxSpeed)
              textFormat: Text.PlainText; color: root.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body
            }
          }
          Text {
            visible: root.maxSpeed !== null && root.configuredLimit !== null && root.configuredLimit < root.maxSpeed - 0.01
            text: "Configured limit  " + Model.ghz(root.configuredLimit)
            textFormat: Text.PlainText; color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
          }
          Repeater {
            model: root.reading.cpus
            Text {
              required property var modelData
              text: "Logical CPU " + modelData.id + "     " + Model.ghz(modelData.ghz)
              textFormat: Text.PlainText; color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
            }
          }
          Text {
            visible: root.reading.cpus.length === 0
            text: "Frequency readings unavailable"
            textFormat: Text.PlainText; color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
          }
          PanelSeparator { width: parent.width }
          PanelSectionHeader { text: "Temperatures" }
          Repeater {
            model: root.reading.sensors
            Row {
              required property var modelData
              width: content.width
              spacing: Style.space(8)
              Text {
                width: Math.min(Style.space(190), parent.width - value.width - parent.spacing)
                text: modelData.chip + " · " + modelData.label
                textFormat: Text.PlainText; elide: Text.ElideRight
                color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
              }
              Text {
                id: value
                width: Style.space(55)
                text: Model.degrees(modelData.celsius)
                textFormat: Text.PlainText; horizontalAlignment: Text.AlignRight
                color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
              }
            }
          }
          Text {
            visible: root.reading.sensors.length === 0
            text: "Temperature sensors unavailable"
            textFormat: Text.PlainText; color: Qt.darker(root.foreground, 1.4); font.family: Style.font.family; font.pixelSize: Style.font.caption
          }
          Text {
            visible: root.sampleError !== ""
            text: root.sampleError; textFormat: Text.PlainText
            color: root.foreground; font.family: Style.font.family; font.pixelSize: Style.font.caption
          }
        }
      }
    }
  }
}
