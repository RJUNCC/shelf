import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A wide card in the "Continue reading" strip.
Rectangle {
    id: card
    property var book: ({})
    signal clicked()
    signal rightClicked()

    width: 300
    height: 128
    radius: Math.min(8, theme.radius)
    color: mouse.containsMouse ? "#1d1d22" : "#16161a"

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 14

        BookCover {
            Layout.preferredWidth: 72
            Layout.fillHeight: true
            book: card.book
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: card.book.title || ""
                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: card.book.author || ""
                elide: Text.ElideRight
                opacity: 0.6
                font.pixelSize: 12
            }
            Item { Layout.fillHeight: true }
            Label {
                text: qsTr("%1% read").arg(Math.round((card.book.progress || 0) * 100).toLocaleString(Qt.locale(), 'f', 0))
                font.pixelSize: 12
                opacity: 0.7
            }
            Rectangle {
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: "#2a2a30"
                Rectangle {
                    height: parent.height
                    radius: 2
                    width: parent.width * (card.book.progress || 0)
                    color: theme.accent
                }
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => mouse.button === Qt.RightButton ? card.rightClicked() : card.clicked()
    }
}
