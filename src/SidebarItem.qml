import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One row in the sidebar: a shelf or a collection.
Rectangle {
    id: item
    property string icon
    property string text
    property int count: -1
    property bool selected: false
    signal clicked()
    signal rightClicked()

    implicitHeight: 36
    radius: Math.min(6, theme.radius)
    color: selected ? Qt.alpha(theme.accent, 0.16) : mouse.containsMouse ? "#1d1d22" : "transparent"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 10
        Label {
            text: item.icon
            color: item.selected ? theme.accent : Material.foreground
            opacity: item.selected ? 1 : 0.6
            Layout.preferredWidth: 16
            horizontalAlignment: Text.AlignHCenter
        }
        Label {
            text: item.text
            elide: Text.ElideRight
            Layout.fillWidth: true
            color: item.selected ? theme.accent : Material.foreground
            font.weight: item.selected ? Font.DemiBold : Font.Normal
        }
        Label {
            visible: item.count >= 0
            text: item.count.toLocaleString(Qt.locale(), 'f', 0)
            opacity: 0.45
            font.pixelSize: 12
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => mouse.button === Qt.RightButton ? item.rightClicked() : item.clicked()
    }
}
