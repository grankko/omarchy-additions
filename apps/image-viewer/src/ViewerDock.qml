import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

Item {
    id: dock
    objectName: "dock"
    required property var backend
    property bool keepOpen: false
    property bool showTips: true
    signal actionRequested(string action)
    signal moreRequested(point edge)
    width: Math.max(62, controls.width + 12)
    height: handle.height + 5 + controls.height
    property bool latched: false
    readonly property bool expanded: hover.hovered || latched || handle.activeFocus
                                     || controls.activeFocus || keepOpen

    HoverHandler { id: hover; margin: 3 }

    FocusScope {
        id: controls
        objectName: "dockControls"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: handle.top
        anchors.bottomMargin: 5
        width: buttons.implicitWidth + 12
        height: buttons.implicitHeight + 8
        opacity: dock.expanded ? 1 : 0
        visible: opacity > 0
        enabled: dock.expanded
        Behavior on opacity { NumberAnimation { duration: 105 } }

        Rectangle {
            anchors.fill: parent
            radius: omarchyTheme.radius
            color: "#de202124"
            border.color: "#55ffffff"
        }

        RowLayout {
            id: buttons
            anchors.centerIn: parent
            spacing: 2

            DockButton {
                tipsEnabled: dock.showTips
                glyph: "previous"; description: qsTr("Previous image")
                enabled: backend.previous && !backend.operating
                onClicked: dock.actionRequested("previous")
            }
            DockButton {
                tipsEnabled: dock.showTips
                glyph: "next"; description: qsTr("Next image")
                enabled: backend.next && !backend.operating
                onClicked: dock.actionRequested("next")
            }
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                Layout.preferredWidth: 1
                Layout.preferredHeight: 16
                color: "#46ffffff"
            }
            DockButton {
                tipsEnabled: dock.showTips
                glyph: "minus"; description: qsTr("Zoom out")
                enabled: !backend.busy
                onClicked: dock.actionRequested("zoomOut")
            }
            DockButton {
                tipsEnabled: dock.showTips
                glyph: "plus"; description: qsTr("Zoom in")
                enabled: !backend.busy
                onClicked: dock.actionRequested("zoomIn")
            }
            DockButton {
                tipsEnabled: dock.showTips
                glyph: "fit"; description: qsTr("Fit to window")
                enabled: !backend.busy
                onClicked: dock.actionRequested("fit")
            }
            DockButton {
                tipsEnabled: dock.showTips
                glyph: "rotate"; description: qsTr("Rotate right")
                enabled: backend.photoActionable
                onClicked: backend.rotate(1)
            }
            DockButton {
                objectName: "saveButton"
                tipsEnabled: dock.showTips
                text: qsTr("Save"); description: qsTr("Save rotation to this file")
                accent: true
                enabled: backend.canSave
                onClicked: backend.save()
            }
            DockButton {
                tipsEnabled: dock.showTips
                glyph: "more"; description: qsTr("More actions")
                onClicked: dock.moreRequested(controls.mapToItem(dock.parent, controls.width, 0))
            }
        }
    }

    T.Button {
        id: handle
        objectName: "dockHandle"
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: 46
        height: 16
        padding: 0
        focusPolicy: Qt.TabFocus
        Accessible.name: qsTr("Show image controls")
        onClicked: dock.latched = !dock.latched

        contentItem: Item {
            Rectangle {
                anchors.centerIn: parent
                width: 28
                height: 4
                radius: 2
                color: handle.visualFocus ? omarchyTheme.accent : "#dce8e9ec"
                border.color: "#80000000"
            }
        }
    }
}
