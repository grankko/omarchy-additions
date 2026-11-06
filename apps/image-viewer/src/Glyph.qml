import QtQuick

Canvas {
    id: icon
    required property string name
    property color color: "white"
    width: 18
    height: 18

    onNameChanged: requestPaint()
    onColorChanged: requestPaint()

    onPaint: {
        const context = getContext("2d")
        context.reset()
        context.strokeStyle = color
        context.fillStyle = color
        context.lineWidth = 1.5
        context.lineCap = "round"
        context.lineJoin = "round"

        function line(points) {
            context.beginPath()
            context.moveTo(points[0], points[1])
            for (let index = 2; index < points.length; index += 2)
                context.lineTo(points[index], points[index + 1])
            context.stroke()
        }

        switch (name) {
        case "previous":
            line([11, 4, 6, 9, 11, 14])
            break
        case "next":
            line([7, 4, 12, 9, 7, 14])
            break
        case "plus":
            line([9, 4, 9, 14])
        case "minus":
            line([4, 9, 14, 9])
            break
        case "fit":
            line([7, 3, 3, 3, 3, 7])
            line([11, 3, 15, 3, 15, 7])
            line([15, 11, 15, 15, 11, 15])
            line([3, 11, 3, 15, 7, 15])
            break
        case "rotate":
            context.beginPath()
            context.arc(9, 9, 5.8, -0.35, Math.PI * 1.63)
            context.stroke()
            line([15, 3, 15, 8, 10, 8])
            break
        case "close":
            line([5.5, 5.5, 12.5, 12.5])
            line([12.5, 5.5, 5.5, 12.5])
            break
        case "more":
            for (let x = 4; x <= 14; x += 5) {
                context.beginPath()
                context.arc(x, 9, 1.2, 0, Math.PI * 2)
                context.fill()
            }
            break
        }
    }
}
