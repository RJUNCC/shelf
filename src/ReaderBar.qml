import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The strip above a book: the Outline toggle, where you are, and the
// reader's own tools on the right.
Rectangle {
    id: bar
    property string title
    property string subtitle
    property bool outlineOpen: false
    default property alias tools: toolRow.data
    signal outlineToggled()

    implicitHeight: 48
    color: "#17171b"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 10
        spacing: 8

        ToolbarButton {
            iconName: "outline"
            label: qsTr("Outline")
            showLabel: bar.width > 820
            checkable: true
            checked: bar.outlineOpen
            tip: qsTr("Show or hide the outline (T)")
            onClicked: bar.outlineToggled()
        }

        Rectangle { width: 1; height: 22; color: "#2a2a31" }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            spacing: 0
            Label {
                Layout.fillWidth: true
                text: bar.title
                elide: Text.ElideRight
                font.weight: Font.DemiBold
                font.pixelSize: 13
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: bar.subtitle
                elide: Text.ElideRight
                font.pixelSize: 11
                opacity: 0.55
            }
        }

        RowLayout {
            id: toolRow
            spacing: 4
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: "#222228"
    }
}
