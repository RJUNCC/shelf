import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A book in the library grid: cover, title, author, and how far along it is.
Item {
    id: card
    property var book: ({})
    property bool current: false
    // Select mode: a check circle on the cover, and clicks pick the book.
    property bool selecting: false
    property bool picked: false
    signal clicked(int modifiers)
    signal rightClicked()

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        anchors.rightMargin: 14
        radius: Math.min(6, theme.radius)
        color: card.picked ? Qt.alpha(theme.accent, 0.12) : mouse.containsMouse ? "#18181c" : "transparent"
        border.color: card.current || card.picked ? theme.accent : "transparent"
        border.width: 2
    }

    Column {
        anchors.fill: parent
        anchors.margins: 10
        anchors.rightMargin: 20
        spacing: 6
        opacity: card.book.missing ? 0.4 : 1

        BookCover {
            width: parent.width
            height: width * 1.45
            book: card.book

            Rectangle {
                visible: card.selecting
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 6
                width: 24
                height: 24
                radius: 12
                color: card.picked ? theme.accent : Qt.rgba(0, 0, 0, 0.55)
                border.color: card.picked ? theme.accent : "white"
                border.width: 2
                Label {
                    anchors.centerIn: parent
                    visible: card.picked
                    text: "✓"
                    color: theme.accentForeground
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }
            }

            // Finished books get a quiet check in the corner.
            Rectangle {
                visible: card.book.finished === true
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 6
                width: 22
                height: 22
                radius: 11
                color: theme.accent
                Label {
                    anchors.centerIn: parent
                    text: "✓"
                    color: theme.accentForeground
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }
            }

            // Reading progress along the bottom edge.
            Rectangle {
                visible: !card.book.finished && card.book.progress > 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 4
                color: Qt.rgba(0, 0, 0, 0.5)
                Rectangle {
                    height: parent.height
                    width: parent.width * (card.book.progress || 0)
                    color: theme.accent
                }
            }
        }

        Label {
            width: parent.width
            text: card.book.title || ""
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.WordWrap
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Label {
            width: parent.width
            text: card.book.missing ? qsTr("File missing") : (card.book.author || "")
            elide: Text.ElideRight
            font.pixelSize: 12
            opacity: 0.6
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => mouse.button === Qt.RightButton ? card.rightClicked() : card.clicked(mouse.modifiers)
    }
}
