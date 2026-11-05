import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "unsavedEdits"
    property bool sending: false
    property bool canSave: false
    property string saveBlock: ""
    signal discardRequested()
    signal saveRequested()

    modal: true
    title: qsTr("Unsaved rotation")
    background: Rectangle { color: "#202124"; border.color: "#54565a"; radius: omarchyTheme.radius }
    Overlay.modal: Rectangle { color: "#70000000" }

    contentItem: ColumnLayout {
        Label {
            Layout.fillWidth: true
            text: dialog.sending ? qsTr("Save the rotation before opening LocalSend?")
                  : qsTr("Save the rotation to this file before continuing?")
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            visible: !dialog.canSave && dialog.saveBlock.length > 0
            text: dialog.saveBlock
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: "#c7c9ce"
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "cancelUnsaved"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.reject()
        }
        Button {
            objectName: "discardUnsaved"
            text: dialog.sending ? qsTr("Send saved file") : qsTr("Discard")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.discardRequested()
        }
        Button {
            objectName: "saveUnsaved"
            text: qsTr("Save")
            enabled: dialog.canSave
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.saveRequested()
        }
    }
}
