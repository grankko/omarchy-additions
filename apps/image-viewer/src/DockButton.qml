import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

T.Button {
    id: button
    required property string description
    property string glyph: ""
    property bool accent: false
    property bool tipsEnabled: true

    Accessible.name: description
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    implicitWidth: text.length ? Math.max(50, label.implicitWidth + 20) : 34
    implicitHeight: 34
    padding: 0

    ToolTip.visible: tipsEnabled && hovered && enabled && !pressed
    ToolTip.delay: 600
    ToolTip.text: description

    contentItem: Item {
        Label {
            id: label
            anchors.centerIn: parent
            visible: button.text.length > 0
            text: button.text
            color: button.enabled && button.accent ? omarchyTheme.foreground
                  : button.enabled ? "#f3f4f5" : "#94969a"
            font.pixelSize: 13
            padding: 0
            font.weight: Font.Medium
        }

        Glyph {
            anchors.centerIn: parent
            visible: button.glyph.length > 0
            name: button.glyph
            color: button.enabled ? "#f3f4f5" : "#707278"
        }
    }

    background: Rectangle {
        radius: Math.min(5, omarchyTheme.radius)
        color: button.accent && button.enabled ? omarchyTheme.accent
               : button.down ? "#66ffffff" : button.hovered ? "#26ffffff" : "transparent"
        border.width: button.visualFocus ? 1 : 0
        border.color: button.accent ? omarchyTheme.foreground : omarchyTheme.accent
    }
}
