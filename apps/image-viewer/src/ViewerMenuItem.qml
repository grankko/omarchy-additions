import QtQuick
import QtQuick.Controls

MenuItem {
    id: item
    implicitHeight: 30
    leftPadding: 10
    rightPadding: 28
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true

    contentItem: Label {
        text: item.text
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        color: item.enabled ? "#f5f5f5" : "#939598"
        font.pixelSize: 13
    }

    arrow: Glyph {
        name: "next"
        x: item.width - width - 7
        y: (item.height - height) / 2
        visible: item.subMenu !== null
        color: item.enabled ? "#f5f5f5" : "#939598"
    }

    background: Rectangle {
        radius: Math.min(4, omarchyTheme.radius)
        color: item.highlighted && item.enabled ? "#36ffffff" : "transparent"
    }
}
