import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A row of mutually exclusive choices: [{ key, label }].
Rectangle {
    id: control
    property var options: []
    property string value
    signal picked(string key)

    implicitHeight: 34
    radius: Math.min(8, theme.radius)
    color: "#141418"
    border.color: "#2c2c33"

    RowLayout {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 3
        Repeater {
            model: control.options
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Math.min(6, theme.radius)
                color: control.value === modelData.key ? Qt.alpha(theme.accent, 0.2)
                     : optionMouse.containsMouse ? "#202026" : "transparent"
                Label {
                    anchors.centerIn: parent
                    text: modelData.label
                    font.pixelSize: 12
                    color: control.value === modelData.key ? theme.accent : "#d4d4da"
                }
                MouseArea {
                    id: optionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: control.picked(modelData.key)
                }
            }
        }
    }
}
