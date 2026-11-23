import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Picks any color: a saturation/brightness square, a hue strip, and a hex field.
ColumnLayout {
    id: picker
    property color value: "#2b2b30"
    signal picked(color value)

    property real hue: 0
    property real saturation: 0
    property real brightness: 0.2

    function setFrom(c) {
        const col = Qt.color(c)
        hue = Math.max(0, col.hsvHue)
        saturation = col.hsvSaturation
        brightness = col.hsvValue
    }
    function emitColor() {
        value = Qt.hsva(hue, saturation, brightness, 1)
        picked(value)
    }
    function hex(c) {
        const h = v => ("0" + Math.round(v * 255).toString(16)).slice(-2)
        return "#" + h(c.r) + h(c.g) + h(c.b)
    }

    Component.onCompleted: setFrom(value)
    spacing: 10

    // Saturation left to right, brightness bottom to top.
    Rectangle {
        id: square
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        radius: Math.min(6, theme.radius)
        color: Qt.hsva(picker.hue, 1, 1, 1)
        clip: true

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "white" }
                GradientStop { position: 1; color: "transparent" }
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: "black" }
            }
        }
        Rectangle {
            x: picker.saturation * parent.width - width / 2
            y: (1 - picker.brightness) * parent.height - height / 2
            width: 14
            height: 14
            radius: 7
            color: "transparent"
            border.color: picker.brightness > 0.5 ? "black" : "white"
            border.width: 2
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            function pick(mouse) {
                picker.saturation = Math.max(0, Math.min(1, mouse.x / width))
                picker.brightness = Math.max(0, Math.min(1, 1 - mouse.y / height))
                picker.emitColor()
            }
            onPressed: mouse => pick(mouse)
            onPositionChanged: mouse => pick(mouse)
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 14
        radius: 7
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.000; color: "#ff0000" }
            GradientStop { position: 0.167; color: "#ffff00" }
            GradientStop { position: 0.333; color: "#00ff00" }
            GradientStop { position: 0.500; color: "#00ffff" }
            GradientStop { position: 0.667; color: "#0000ff" }
            GradientStop { position: 0.833; color: "#ff00ff" }
            GradientStop { position: 1.000; color: "#ff0000" }
        }
        Rectangle {
            x: picker.hue * parent.width - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            radius: 9
            color: Qt.hsva(picker.hue, 1, 1, 1)
            border.color: "white"
            border.width: 2
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -6
            preventStealing: true
            function pick(mouse) {
                picker.hue = Math.max(0, Math.min(0.999, (mouse.x - 6) / (width - 12)))
                picker.emitColor()
            }
            onPressed: mouse => pick(mouse)
            onPositionChanged: mouse => pick(mouse)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Rectangle {
            width: 28
            height: 28
            radius: Math.min(6, theme.radius)
            color: picker.value
            border.color: "#3a3a42"
        }
        CompactField {
            id: hexField
            Layout.fillWidth: true
            text: picker.hex(picker.value)
            validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
            onAccepted: {
                const t = text.startsWith("#") ? text : "#" + text
                if (/^#[0-9a-fA-F]{6}$/.test(t)) {
                    picker.setFrom(t)
                    picker.emitColor()
                }
            }
        }
    }
}
