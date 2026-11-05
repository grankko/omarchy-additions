import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Templates as T
import Omarchy.Viewer

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
    property bool continueAfterSave: false
    property bool closing: false
    readonly property bool modalOpen: edits.visible || help.visible || contextMenu.visible

    onPixelsChanged: requestPreview.restart()
    onActiveChanged: if (active) viewer.refreshIfStale()

    function act(action) {
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

    function perform(action) {
        switch (action) {
        case "previous": viewer.navigate(-1); break
        case "next": viewer.navigate(1); break
        case "open": viewer.choose(); break
        case "reload": viewer.refresh(true); break
        case "send": viewer.send(); break
        case "close":
            closing = true
            window.close()
            break
        }
    }

    onClosing: event => {
        if (!closing && (viewer.operating || viewer.turns)) {
            event.accepted = false
            act("close")
        }
    }

    Connections {
        target: viewer

        function onPhotoChanged() {
            viewport.fit()
        }

        function onSaveFinished(success) {
            if (!continueAfterSave)
                return
            continueAfterSave = false
            const action = pendingAction
            pendingAction = ""
            if (success)
                perform(action)
        }

        function onStateChanged() {
            if (viewer.busy) {
                if (!waitTimer.running)
                    waitTimer.start()
                return
            }
            waitTimer.stop()
            waitTimer.show = false
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
        interval: viewer.status === qsTr("Saved") || viewer.status === qsTr("Image copied") ? 2200 : 6500
        onTriggered: viewer.dismiss()
    }

    Item {
        id: viewport
        anchors.fill: parent
        clip: true
        LayoutMirroring.enabled: false
        onWidthChanged: requestPreview.restart()
        onHeightChanged: requestPreview.restart()

        property real zoom: 1
        property real offsetX: 0
        property real offsetY: 0
        readonly property bool sideways: viewer.turns % 2 === 1
        readonly property real sourceWidth: sideways ? viewer.original.height : viewer.original.width
        readonly property real sourceHeight: sideways ? viewer.original.width : viewer.original.height
        readonly property real fitScale: Math.max(0.0001, Math.min(width / Math.max(1, sourceWidth),
                                                                 height / Math.max(1, sourceHeight)))
        readonly property real viewScale: fitScale * zoom

        function fit() {
            zoom = 1
            offsetX = 0
            offsetY = 0
        }

        function clampPan() {
            const limitX = Math.max(0, (sourceWidth * viewScale - width) / 2)
            const limitY = Math.max(0, (sourceHeight * viewScale - height) / 2)
            offsetX = Math.max(-limitX, Math.min(limitX, offsetX))
            offsetY = Math.max(-limitY, Math.min(limitY, offsetY))
        }

        function zoomAt(value, x, y) {
            if (!window.showingPhoto || viewer.busy)
                return
            const newZoom = Math.max(Math.min(1, 1 / (fitScale * window.pixels)), Math.min(32, value))
            const ratio = newZoom / zoom
            offsetX = x - width / 2 - (x - width / 2 - offsetX) * ratio
            offsetY = y - height / 2 - (y - height / 2 - offsetY) * ratio
            zoom = newZoom
            clampPan()
            detailTimer.restart()
        }

        function zoomBy(multiplier) {
            zoomAt(zoom * multiplier, width / 2, height / 2)
        }

        function actualSize() {
            zoomAt(1 / (fitScale * window.pixels), width / 2, height / 2)
        }

        Timer {
            id: requestPreview
            interval: 120
            running: true
            onTriggered: {
                viewer.setPreviewEdge(Math.ceil(Math.max(viewport.width, viewport.height) * window.pixels))
                viewport.clampPan()
                detailTimer.restart()
            }
        }

        Timer {
            id: detailTimer
            interval: 120
            onTriggered: viewer.detail(Math.ceil(Math.max(viewport.sourceWidth, viewport.sourceHeight)
                                                  * viewport.viewScale * window.pixels))
        }

        PhotoItem {
            objectName: "photo"
            visible: window.showingPhoto
            photo: viewer.frame
            width: viewer.original.width * viewport.viewScale
            height: viewer.original.height * viewport.viewScale
            x: (parent.width - width) / 2 + viewport.offsetX
            y: (parent.height - height) / 2 + viewport.offsetY
            rotation: viewer.turns * 90
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: pressed && pressedButtons & Qt.LeftButton && viewport.zoom > 1
                         ? Qt.ClosedHandCursor : Qt.ArrowCursor
            property real lastX: 0
            property real lastY: 0

            onPressed: event => {
                lastX = event.x
                lastY = event.y
            }
            onPositionChanged: event => {
                if (pressedButtons & Qt.LeftButton) {
                    viewport.offsetX += event.x - lastX
                    viewport.offsetY += event.y - lastY
                    viewport.clampPan()
                    lastX = event.x
                    lastY = event.y
                }
            }
            onClicked: event => {
                if (event.button === Qt.RightButton)
                    contextMenu.popup(event.x, event.y)
            }
            onWheel: event => {
                if (event.angleDelta.y)
                    viewport.zoomAt(viewport.zoom * Math.pow(1.18, event.angleDelta.y / 120), event.x, event.y)
            }
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

    Item {
        id: dock
        objectName: "dock"
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 7
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(62, controls.width + 12)
        height: handle.height + 5 + controls.height
        visible: window.showingPhoto
        property bool latched: false
        readonly property bool expanded: hover.hovered || latched || handle.activeFocus
                                         || controls.activeFocus || contextMenu.visible && contextMenu.fromDock

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
                    tipsEnabled: !window.modalOpen && !statusNotice.visible
                    glyph: "previous"; description: qsTr("Previous image")
                    enabled: viewer.previous && !viewer.operating
                    onClicked: window.act("previous")
                }
                DockButton {
                    tipsEnabled: !window.modalOpen && !statusNotice.visible
                    glyph: "next"; description: qsTr("Next image")
                    enabled: viewer.next && !viewer.operating
                    onClicked: window.act("next")
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
                    tipsEnabled: !window.modalOpen && !statusNotice.visible
                    glyph: "minus"; description: qsTr("Zoom out")
                    enabled: !viewer.busy
                    onClicked: viewport.zoomBy(1 / 1.25)
                }
                DockButton {
                    tipsEnabled: !window.modalOpen && !statusNotice.visible
                    glyph: "plus"; description: qsTr("Zoom in")
                    enabled: !viewer.busy
                    onClicked: viewport.zoomBy(1.25)
                }
                DockButton {
                    tipsEnabled: !window.modalOpen && !statusNotice.visible
                    glyph: "fit"; description: qsTr("Fit to window")
                    enabled: !viewer.busy
                    onClicked: viewport.fit()
                }
                DockButton {
                    tipsEnabled: !window.modalOpen && !statusNotice.visible
                    glyph: "rotate"; description: qsTr("Rotate right")
                    enabled: !viewer.busy
                    onClicked: viewer.rotate(1)
                }
                DockButton {
                    objectName: "saveButton"
                    tipsEnabled: !window.modalOpen && !statusNotice.visible
                    text: qsTr("Save"); description: qsTr("Save rotation to this file")
                    accent: true
                    enabled: viewer.canSave
                    onClicked: viewer.save()
                }
                DockButton {
                    tipsEnabled: !window.modalOpen && !statusNotice.visible
                    glyph: "more"; description: qsTr("More actions")
                    onClicked: {
                        contextMenu.fromDock = true
                        const edge = controls.mapToItem(window.contentItem, controls.width, 0)
                        contextMenu.popup(edge.x - contextMenu.width,
                                          Math.max(contextMenu.margins, edge.y - contextMenu.implicitHeight - 6))
                    }
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

    Rectangle {
        id: statusNotice
        anchors.bottom: dock.top
        anchors.bottomMargin: 5
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 24, Math.max(110, notice.implicitWidth + 24))
        height: notice.height + 16
        radius: omarchyTheme.radius
        color: "#e9202124"
        border.color: "#44ffffff"
        visible: !window.modalOpen && (waitTimer.show || viewer.status.length > 0)

        Label {
            id: notice
            anchors.centerIn: parent
            width: parent.width - 24
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: 13
            text: waitTimer.show ? qsTr("Working…") : viewer.status
        }
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
        ViewerMenuItem { text: qsTr("Rotate right"); enabled: window.showingPhoto && !viewer.busy; onTriggered: viewer.rotate(1) }
        ViewerMenuItem { text: qsTr("Rotate left"); enabled: window.showingPhoto && !viewer.busy; onTriggered: viewer.rotate(-1) }
        ViewerMenuItem { text: qsTr("Save"); enabled: viewer.canSave; onTriggered: viewer.save() }
        ViewerMenuItem { text: qsTr("Copy image"); enabled: window.showingPhoto && !viewer.busy; onTriggered: viewer.copy() }
        ViewerMenu {
            title: qsTr("Send to")
            enabled: window.showingPhoto && !viewer.busy
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

    Dialog {
        id: edits
        anchors.centerIn: parent
        width: Math.min(360, window.width - 24)
        modal: true
        title: qsTr("Unsaved rotation")
        background: Rectangle { color: "#202124"; border.color: "#54565a"; radius: omarchyTheme.radius }
        Overlay.modal: Rectangle { color: "#70000000" }
        onRejected: window.pendingAction = ""

        contentItem: Label {
            text: window.pendingAction === "send" ? qsTr("Save the rotation before opening LocalSend?")
                  : qsTr("Save the rotation to this file before continuing?")
            wrapMode: Text.Wrap
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: edits.reject()
            }
            Button {
                text: window.pendingAction === "send" ? qsTr("Send saved file") : qsTr("Discard")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: {
                    const action = window.pendingAction
                    window.pendingAction = ""
                    edits.close()
                    if (action !== "send")
                        viewer.discard()
                    window.perform(action)
                }
            }
            Button {
                text: qsTr("Save")
                enabled: viewer.canSave
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    edits.close()
                    window.continueAfterSave = true
                    viewer.save()
                }
            }
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
    Shortcut { sequence: "Escape"; enabled: !window.modalOpen; onActivated: dock.latched = false }
    Shortcut { sequence: "Shift+F10"; enabled: !edits.visible && !help.visible; onActivated: contextMenu.popup() }
    Shortcut { sequence: "?"; onActivated: help.open() }
    Shortcut { sequences: ["Q", StandardKey.Quit]; onActivated: window.act("close") }
}
