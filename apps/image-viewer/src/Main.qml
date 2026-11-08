import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1000
    height: 680
    minimumWidth: 350
    minimumHeight: 240
    visible: true
    color: "#111215"
    title: viewer.name || qsTr("Image Viewer")

    Material.theme: Material.Dark
    Material.accent: omarchyTheme.accent
    Material.roundedScale: omarchyTheme.radius

    property bool showingPhoto: viewer.original.width > 0
    property real pixels: Screen.devicePixelRatio || 1
    property string pendingAction: ""
    property string pendingPath: ""
    property string queuedPath: ""
    property bool continueAfterSave: false
    property bool closing: false
    property bool closeAfterCancel: false
    readonly property bool modalOpen: edits.visible || help.visible || contextMenu.visible

    onActiveChanged: if (active) viewer.refreshIfStale()

    function requestOpen(path) {
        if (closing)
            return
        closeAfterCancel = false
        if (path === viewer.path && !viewer.busy) {
            edits.reject()
            queuedPath = ""
            return
        }
        viewer.cancelPicker()
        contextMenu.close()
        help.close()
        if (viewer.operating || continueAfterSave) {
            queuedPath = path
            return
        }
        queuedPath = ""
        pendingPath = path
        if (viewer.turns) {
            pendingAction = "external"
            edits.open()
        } else {
            perform("external", path)
        }
    }

    function drainOpen() {
        if (!queuedPath || viewer.operating || continueAfterSave || edits.visible || closing)
            return
        const path = queuedPath
        queuedPath = ""
        requestOpen(path)
    }

    function showContext(x, y, fromDock = false) {
        if (edits.visible || help.visible)
            return
        contextMenu.fromDock = fromDock
        const margin = contextMenu.margins
        const left = Math.max(margin, Math.min(x, window.contentItem.width - contextMenu.width - margin))
        const top = Math.max(margin, Math.min(y, window.contentItem.height - contextMenu.implicitHeight - margin))
        contextMenu.popup(window.contentItem, left, top)
    }

    function act(action) {
        if (action === "close" && viewer.operating) {
            window.close()
            return
        }
        if (viewer.operating || edits.visible || help.visible
            || (action === "previous" && !viewer.previous) || (action === "next" && !viewer.next))
            return
        if (viewer.turns) {
            pendingAction = action
            edits.open()
        } else {
            perform(action)
        }
    }

    function perform(action, path = "") {
        switch (action) {
        case "previous": viewer.navigate(-1); break
        case "next": viewer.navigate(1); break
        case "open": viewer.choose(); break
        case "external":
            pendingPath = ""
            viewer.open(path)
            break
        case "reload": viewer.refresh(true); break
        case "send": viewer.send(); break
        case "close":
            closing = true
            window.close()
            break
        }
    }

    onClosing: event => {
        if (viewer.picking)
            viewer.cancelPicker()
        if (!closing && (viewer.operating || viewer.turns)) {
            event.accepted = false
            if (viewer.operating)
                window.closeAfterCancel = viewer.cancelSave()
            else
                act("close")
        }
    }

    Connections {
        target: viewer

        function onPhotoChanged() {
            viewport.fit()
        }

        function onSaveFinished(success) {
            if (window.closeAfterCancel) {
                window.closeAfterCancel = false
                continueAfterSave = false
                pendingAction = ""
                act("close")
                return
            }
            if (!continueAfterSave)
                return
            continueAfterSave = false
            const action = pendingAction
            const path = pendingPath
            pendingAction = ""
            pendingPath = ""
            if (success)
                perform(action, path)
            Qt.callLater(window.drainOpen)
        }

        function onStateChanged() {
            if (!viewer.operating)
                Qt.callLater(window.drainOpen)
            if (viewer.busy) {
                if (!waitTimer.running)
                    waitTimer.start()
                return
            }
            waitTimer.stop()
            waitTimer.show = false
            if (viewer.statusPersistent || !viewer.status)
                statusTimer.stop()
            else
                statusTimer.restart()
        }
    }

    Timer {
        id: waitTimer
        property bool show: false
        interval: 240
        onTriggered: show = viewer.busy
    }

    Timer {
        id: statusTimer
        interval: viewer.statusSuccess ? 2200 : 6500
        onTriggered: viewer.dismiss()
    }

    ImageViewport {
        id: viewport
        anchors.fill: parent
        backend: viewer
        pixels: window.pixels
        interactive: !window.modalOpen && window.active
    }


    DockButton {
        id: closeButton
        objectName: "closeWindowButton"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        implicitWidth: 26
        implicitHeight: 26
        description: qsTr("Close")
        glyph: "close"
        tipsEnabled: false
        onClicked: window.close()

        background: Rectangle {
            radius: width / 2
            color: closeButton.down ? "#505054" : closeButton.hovered ? "#434347" : "#343437"
            border.width: closeButton.visualFocus ? 1 : 0
            border.color: omarchyTheme.accent
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 10
        visible: !window.showingPhoto && !viewer.busy

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Image Viewer")
            font.pixelSize: 24
        }
        DockButton {
            objectName: "openImageButton"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Open image")
            description: text
            tipsEnabled: false
            onClicked: window.act("open")
        }
    }

    ViewerDock {
        id: dock
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 7
        anchors.horizontalCenter: parent.horizontalCenter
        visible: window.showingPhoto
        backend: viewer
        keepOpen: contextMenu.visible && contextMenu.fromDock
        showTips: !window.modalOpen && !statusNotice.visible
        onActionRequested: action => {
            switch (action) {
            case "zoomIn": viewport.zoomBy(1.25); break
            case "zoomOut": viewport.zoomBy(1 / 1.25); break
            case "fit": viewport.fit(); break
            default: window.act(action)
            }
        }
        onMoreRequested: edge => {
            window.showContext(edge.x - contextMenu.width, edge.y - contextMenu.implicitHeight - 6, true)
        }
    }

    Rectangle {
        id: statusNotice
        anchors.bottom: dock.top
        anchors.bottomMargin: 5
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 24, Math.max(110, notice.implicitWidth + (viewer.statusPersistent ? 54 : 24)))
        height: Math.max(notice.implicitHeight, viewer.statusPersistent ? 24 : 0) + 16
        radius: omarchyTheme.radius
        color: "#e9202124"
        border.color: "#44ffffff"
        visible: !window.modalOpen && (waitTimer.show || viewer.status.length > 0)

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 6
            Label {
                id: notice
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                font.pixelSize: 13
                text: viewer.status || qsTr("Working…")
                textFormat: Text.PlainText
            }
            DockButton {
                visible: viewer.statusPersistent
                text: "×"
                description: qsTr("Dismiss")
                implicitWidth: 24
                implicitHeight: 24
                tipsEnabled: false
                onClicked: viewer.dismiss()
            }
        }
    }

    Item {
        anchors.fill: parent
        ContextMenu.onRequested: position => window.showContext(position.x, position.y)
    }

    component MenuDivider: MenuSeparator {
        implicitHeight: 9
        padding: 0
        contentItem: Item {
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: 10
                width: parent.width - 20
                height: 1
                color: "#32ffffff"
            }
        }
    }

    ViewerMenu {
        id: contextMenu
        objectName: "imageContextMenu"
        property bool fromDock: false
        onClosed: fromDock = false

        ViewerMenuItem { text: qsTr("Open image…"); enabled: !viewer.operating; onTriggered: window.act("open") }
        MenuDivider {}
        ViewerMenuItem { text: qsTr("Fit to window"); enabled: window.showingPhoto; onTriggered: viewport.fit() }
        ViewerMenuItem { text: qsTr("Actual size"); enabled: window.showingPhoto && !viewer.busy; onTriggered: viewport.actualSize() }
        ViewerMenuItem { text: qsTr("Rotate right"); enabled: viewer.photoActionable; onTriggered: viewer.rotate(1) }
        ViewerMenuItem { text: qsTr("Rotate left"); enabled: viewer.photoActionable; onTriggered: viewer.rotate(-1) }
        ViewerMenuItem { text: qsTr("Save"); enabled: viewer.canSave; onTriggered: viewer.save() }
        ViewerMenuItem { text: qsTr("Copy image"); enabled: viewer.photoActionable; onTriggered: viewer.copy() }
        ViewerMenu {
            title: qsTr("Send to")
            enabled: viewer.photoActionable
            ViewerMenuItem {
                text: viewer.canSend ? qsTr("LocalSend") : qsTr("LocalSend unavailable")
                enabled: viewer.canSend
                onTriggered: window.act("send")
            }
        }
        MenuDivider {}
        ViewerMenuItem { text: qsTr("Refresh folder"); enabled: window.showingPhoto && !viewer.busy; onTriggered: viewer.refresh() }
        ViewerMenuItem { text: qsTr("Reload image"); enabled: window.showingPhoto && !viewer.busy; onTriggered: window.act("reload") }
        ViewerMenuItem { text: qsTr("Keyboard shortcuts"); onTriggered: help.open() }
        MenuDivider {}
        ViewerMenuItem {
            text: qsTr("Image Viewer %1").arg(Qt.application.version)
            enabled: false
            implicitHeight: 25
        }
    }

    UnsavedEdits {
        id: edits
        anchors.centerIn: parent
        width: Math.min(360, window.width - 24)
        sending: window.pendingAction === "send"
        destination: window.pendingAction === "external" ? window.pendingPath.split("/").pop() : ""
        canSave: viewer.canSave
        saveBlock: viewer.saveBlock
        onClosed: {
            if (!window.continueAfterSave) {
                window.pendingAction = ""
                window.pendingPath = ""
            }
        }
        onRejected: window.queuedPath = ""
        onDiscardRequested: {
            const action = window.pendingAction
            const path = window.pendingPath
            edits.close()
            if (action !== "send")
                viewer.discard()
            window.perform(action, path)
            Qt.callLater(window.drainOpen)
        }
        onSaveRequested: {
            window.continueAfterSave = true
            edits.close()
            viewer.save()
        }
    }

    Popup {
        id: help
        objectName: "shortcutHelp"
        anchors.centerIn: Overlay.overlay
        width: Math.min(390, window.width - 24)
        height: Math.min(implicitHeight, window.height - 24)
        padding: 14
        modal: true
        focus: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        Overlay.modal: Rectangle { color: "#70000000" }
        background: Rectangle {
            color: "#202124"
            border.color: "#54565a"
            radius: omarchyTheme.radius
        }

        contentItem: ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Keyboard shortcuts")
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                }
                DockButton {
                    text: "×"
                    description: qsTr("Close")
                    implicitWidth: 28
                    implicitHeight: 28
                    tipsEnabled: false
                    onClicked: help.close()
                }
            }
            Flickable {
                id: shortcutScroller
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: shortcutRows.implicitHeight
                contentHeight: shortcutRows.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: shortcutRows
                    width: shortcutScroller.width - 8
                    spacing: 2
                    Repeater {
                        model: [
                            ["← / →", qsTr("Previous / next image")],
                            ["+ / −", qsTr("Zoom")],
                            [qsTr("Pinch"), qsTr("Zoom")],
                            ["0", qsTr("Fit to window")],
                            ["1", qsTr("Actual size")],
                            ["R / Shift+R", qsTr("Rotate right / left")],
                            ["Ctrl+S", qsTr("Save")],
                            ["Ctrl+C", qsTr("Copy image")],
                            ["Ctrl+O", qsTr("Open image")],
                            ["F5", qsTr("Refresh folder")],
                            ["Shift+F10", qsTr("Open menu")],
                            ["Q", qsTr("Close")]
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 12
                            Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: 1
                                text: modelData[1]
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                            }
                            Label {
                                text: modelData[0]
                                padding: 4
                                leftPadding: 7
                                rightPadding: 7
                                font.pixelSize: 12
                                color: "#eceef2"
                                background: Rectangle {
                                    color: "#303237"
                                    radius: Math.min(4, omarchyTheme.radius)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Shortcut { sequence: "Left"; enabled: !window.modalOpen; onActivated: window.act("previous") }
    Shortcut { sequence: "Right"; enabled: !window.modalOpen; onActivated: window.act("next") }
    Shortcut { sequences: ["+", "="]; enabled: !window.modalOpen; onActivated: viewport.zoomBy(1.25) }
    Shortcut { sequence: "-"; enabled: !window.modalOpen; onActivated: viewport.zoomBy(1 / 1.25) }
    Shortcut { sequence: "0"; enabled: !window.modalOpen; onActivated: viewport.fit() }
    Shortcut { sequence: "1"; enabled: !window.modalOpen; onActivated: viewport.actualSize() }
    Shortcut { sequence: "R"; enabled: !window.modalOpen; onActivated: viewer.rotate(1) }
    Shortcut { sequence: "Shift+R"; enabled: !window.modalOpen; onActivated: viewer.rotate(-1) }
    Shortcut { sequences: [StandardKey.Save]; enabled: viewer.turns !== 0 && !viewer.busy && !window.modalOpen; onActivated: viewer.save() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: !window.modalOpen; onActivated: viewer.copy() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: window.act("open") }
    Shortcut { sequence: "F5"; enabled: !window.modalOpen; onActivated: viewer.refresh() }
    Shortcut { sequence: "Escape"; enabled: !window.modalOpen; onActivated: {
        if (viewer.picking)
            viewer.cancelPicker()
        else
            dock.latched = false
    } }
    Shortcut { sequence: "Shift+F10"; enabled: !edits.visible && !help.visible; onActivated: window.showContext(window.width / 2, window.height / 2) }
    Shortcut { sequence: "?"; onActivated: help.open() }
    Shortcut { sequences: ["Q", StandardKey.Quit]; onActivated: window.act("close") }
}
