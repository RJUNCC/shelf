import QtQuick
import QtQuick.Controls

// A book's cover, or a generated one in a color picked from the title when
// the book has none, so every book is recognisable on the shelf.
Rectangle {
    id: cover
    property var book: ({})

    readonly property var coverColors: ["#3b4a6b", "#5b3b6b", "#6b3b4a", "#3b6b5b", "#6b5b3b", "#3b5b6b", "#4a6b3b", "#6b4a3b"]
    function hash(text) {
        let h = 0
        for (let i = 0; i < text.length; ++i)
            h = (h * 31 + text.charCodeAt(i)) | 0
        return Math.abs(h)
    }

    radius: Math.min(4, theme.radius)
    color: coverColors[hash(book.title || "") % coverColors.length]
    clip: true

    Image {
        id: image
        anchors.fill: parent
        source: cover.book.cover || ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.height: 480
        visible: status === Image.Ready
    }

    Column {
        visible: image.status !== Image.Ready
        anchors.fill: parent
        anchors.margins: Math.max(6, cover.width / 13)
        spacing: Math.max(3, cover.width / 20)
        Label {
            width: parent.width
            text: cover.book.title || ""
            wrapMode: Text.WordWrap
            maximumLineCount: 5
            elide: Text.ElideRight
            font.pixelSize: Math.max(8, Math.round(cover.width / 10))
            font.weight: Font.DemiBold
            color: "white"
        }
        Label {
            width: parent.width
            text: cover.book.author || ""
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font.pixelSize: Math.max(7, Math.round(cover.width / 13))
            color: Qt.rgba(1, 1, 1, 0.75)
        }
    }

    Label {
        visible: cover.width > 90
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        text: (cover.book.format || "").toUpperCase()
        font.pixelSize: 10
        font.weight: Font.DemiBold
        color: "white"
        padding: 3
        leftPadding: 5
        rightPadding: 5
        background: Rectangle { color: Qt.rgba(0, 0, 0, 0.55); radius: Math.min(3, theme.radius) }
    }
}
