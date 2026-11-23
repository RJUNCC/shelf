import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A single-line field that fits a toolbar: Material's own floats its
// placeholder above the box, which a 48px bar would clip.
TextField {
    id: field
    property string hint

    implicitHeight: 34
    topPadding: 6
    bottomPadding: 6
    leftPadding: 10
    rightPadding: 10
    font.pixelSize: 13
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true

    background: Rectangle {
        radius: Math.min(6, theme.radius)
        color: "#1c1c21"
        border.width: field.activeFocus ? 2 : 1
        border.color: field.activeFocus ? theme.accent : "#33333b"
    }

    Label {
        anchors.verticalCenter: parent.verticalCenter
        x: field.leftPadding
        visible: field.text === ""
        text: field.hint
        font: field.font
        opacity: 0.45
    }
}
