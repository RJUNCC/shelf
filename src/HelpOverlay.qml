import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown with "?".
Rectangle {
    id: overlay
    signal dismissed()
    color: Qt.rgba(0, 0, 0, 0.7)

    readonly property var groups: [
        { title: qsTr("Library"), rows: [
            ["Ctrl+O", qsTr("Add books")],
            ["/", qsTr("Search")],
            [qsTr("Arrows"), qsTr("Move between books")],
            ["Enter", qsTr("Open the selected book")],
            ["S", qsTr("Change sort order")],
            ["Ctrl+N", qsTr("New collection")],
            ["Delete", qsTr("Remove from library")],
            ["Ctrl+A", qsTr("Select all books (then add them to a collection)")],
            [qsTr("Ctrl / Shift click"), qsTr("Pick several books")],
            ["Q", qsTr("Quit")]
        ] },
        { title: qsTr("Reading"), rows: [
            ["← → / PgUp PgDn", qsTr("Turn the page")],
            ["Space", qsTr("Next page")],
            ["T", qsTr("Show or hide the outline")],
            ["B", qsTr("Bookmark this page")],
            ["↑ ↓", qsTr("Scroll, or turn the page")],
            ["N", qsTr("Notes (EPUB) or a note on this page (PDF)")],
            [qsTr("Select text"), qsTr("Highlight, add a note, copy, or ask AI")],
            [qsTr("Double / triple click"), qsTr("Select a word / a sentence")],
            ["Ctrl+C", qsTr("Copy the selected text")],
            ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo or redo a highlight, note, or bookmark")],
            ["Ctrl + / Ctrl −", qsTr("Text size or zoom")],
            ["Ctrl+0", qsTr("Reset text size or zoom")],
            ["Ctrl+F", qsTr("Find in PDF")],
            ["Ctrl+Shift+F", qsTr("Search all books")],
            ["Ctrl+K", qsTr("Ask this book")],
            ["Ctrl+G", qsTr("Go to page in PDF")],
            ["Esc", qsTr("Back to the library tab")]
        ] },
        { title: qsTr("Tabs"), rows: [
            ["Ctrl+Tab", qsTr("Next tab")],
            ["Ctrl+Shift+Tab", qsTr("Previous tab")],
            ["Ctrl+W", qsTr("Close tab")],
            ["Ctrl+1 … 9", qsTr("Library, then tabs in order")],
            [qsTr("Middle click"), qsTr("Close a tab")]
        ] },
        { title: qsTr("Anywhere"), rows: [
            ["F11", qsTr("Full screen")],
            ["?", qsTr("Show or hide this help")]
        ] }
    ]

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.dismissed()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 760)
        height: Math.min(parent.height - 48, content.implicitHeight + 48)
        radius: theme.radius
        color: "#17171b"
        border.color: "#2c2c33"

        Flickable {
            anchors.fill: parent
            anchors.margins: 24
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            GridLayout {
                id: content
                width: parent.width
                columns: width > 560 ? 2 : 1
                columnSpacing: 36
                rowSpacing: 20

                Repeater {
                    model: overlay.groups
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: true
                        spacing: 6
                        Label {
                            text: modelData.title
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: theme.accent
                            Layout.bottomMargin: 4
                        }
                        Repeater {
                            model: modelData.rows
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 14
                                Label {
                                    text: modelData[0]
                                    font.family: "monospace"
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 130
                                    opacity: 0.85
                                }
                                Label {
                                    text: modelData[1]
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
