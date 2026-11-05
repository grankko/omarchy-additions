import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    popupType: Popup.Item
    margins: 4
    padding: 4
    width: 198
    delegate: ViewerMenuItem {}

    background: Rectangle {
        radius: omarchyTheme.radius
        color: "#ed202124"
        border.color: "#65ffffff"
    }
}
