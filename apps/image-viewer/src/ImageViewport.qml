import QtQuick
import Omarchy.Viewer

Item {
    id: viewport
    objectName: "imageViewport"
    required property var backend
    property real pixels: 1
    readonly property bool showingPhoto: backend.original.width > 0
    property bool interactive: true
    clip: true
    LayoutMirroring.enabled: false
    onPixelsChanged: requestPreview.restart()
    onWidthChanged: requestPreview.restart()
    onHeightChanged: requestPreview.restart()

    property real zoom: 1
    property real offsetX: 0
    property real offsetY: 0
    readonly property bool sideways: backend.turns % 2 === 1
    readonly property real sourceWidth: sideways ? backend.original.height : backend.original.width
    readonly property real sourceHeight: sideways ? backend.original.width : backend.original.height
    readonly property real fitScale: Math.max(0.0001, Math.min(width / Math.max(1, sourceWidth),
                                                             height / Math.max(1, sourceHeight)))
    readonly property real viewScale: fitScale * zoom

    function fit() {
        zoom = 1
        offsetX = 0
        offsetY = 0
        detailTimer.stop()
        backend.detail(0)
    }

    function clampPan() {
        const limitX = Math.max(0, (sourceWidth * viewScale - width) / 2)
        const limitY = Math.max(0, (sourceHeight * viewScale - height) / 2)
        offsetX = Math.max(-limitX, Math.min(limitX, offsetX))
        offsetY = Math.max(-limitY, Math.min(limitY, offsetY))
    }

    function zoomAt(value, x, y) {
        if (!viewport.showingPhoto || backend.busy)
            return
        const newZoom = Math.max(Math.min(1, 1 / (fitScale * viewport.pixels)), Math.min(32, value))
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
        zoomAt(1 / (fitScale * viewport.pixels), width / 2, height / 2)
    }

    Timer {
        id: requestPreview
        interval: 120
        running: true
        onTriggered: {
            backend.setPreviewEdge(Math.ceil(Math.max(viewport.width, viewport.height) * viewport.pixels))
            viewport.clampPan()
            detailTimer.restart()
        }
    }

    Timer {
        id: detailTimer
        interval: 500
        onTriggered: backend.detail(Math.ceil(Math.max(viewport.sourceWidth, viewport.sourceHeight)
                                              * viewport.viewScale * viewport.pixels))
    }

    PhotoItem {
        objectName: "photo"
        visible: viewport.showingPhoto
        presentationId: backend.displaySerial
        photo: backend.frame
        onPresentationFailed: backend.presentationFailed()
        width: backend.original.width * viewport.viewScale
        height: backend.original.height * viewport.viewScale
        x: (parent.width - width) / 2 + viewport.offsetX
        y: (parent.height - height) / 2 + viewport.offsetY
        rotation: backend.turns * 90
    }

    PinchHandler {
        objectName: "imagePinch"
        target: null
        acceptedDevices: PointerDevice.TouchPad
        enabled: viewport.interactive && backend.photoActionable
        rotationAxis.enabled: false
        xAxis.enabled: false
        yAxis.enabled: false
        onScaleChanged: delta => viewport.zoomAt(viewport.zoom * delta,
                                                centroid.position.x, centroid.position.y)
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
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
        onWheel: event => {
            if (viewport.interactive && event.angleDelta.y)
                viewport.zoomAt(viewport.zoom * Math.pow(1.18, event.angleDelta.y / 120), event.x, event.y)
        }
    }
}
