import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Finds words inside every book in the library. Results come by book, each
// with its page or chapter and the words around the match; clicking one
// opens the book there.
Popup {
    id: search
    anchors.centerIn: Overlay.overlay
    width: Math.min(820, (parent ? parent.width : 860) - 40)
    height: Math.min(620, (parent ? parent.height : 660) - 40)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#18181d"; radius: theme.radius; border.color: "#2c2c33" }

    property var results: []
    property string searched: ""

    onOpened: {
        field.forceActiveFocus()
        field.selectAll()
    }
    function run() {
        const q = field.text.trim()
        if (q.length < 2)
            return
        searched = q
        results = []
        bookSearch.search(q, library.allBooks())
    }
    Connections {
        target: bookSearch
        function onResultsReady(query, found) {
            if (query === search.searched)
                search.results = found
        }
    }
    // Results with a heading row before each book's first one.
    readonly property var rows: {
        const out = []
        let last = ""
        for (const r of results) {
            if (r.bookId !== last) {
                out.push({ heading: true, title: r.title, count: results.filter(x => x.bookId === r.bookId).length })
                last = r.bookId
            }
            out.push(r)
        }
        return out
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Search all books")
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            ToolbarButton {
                iconName: "close"
                tip: qsTr("Close (Esc)")
                onClicked: search.close()
            }
        }
        TextField {
            id: field
            Layout.fillWidth: true
            placeholderText: qsTr("Words to find, then Enter")
            onAccepted: search.run()
        }
        Label {
            Layout.fillWidth: true
            visible: text !== ""
            font.pixelSize: 12
            opacity: 0.6
            wrapMode: Text.WordWrap
            text: bookSearch.busy ? qsTr("Searching… The first search reads each book once, in the background.")
                : search.searched === "" ? qsTr("Finds every word you type, in every book. Scanned books are searchable once their outline has been built.")
                : search.results.length === 0 ? qsTr("Nothing found for “%1”.").arg(search.searched)
                : qsTr("%n result(s)", "", search.results.length)
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: search.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Loader {
                required property var modelData
                width: ListView.view.width
                sourceComponent: modelData.heading ? headingRow : resultRow

                Component {
                    id: headingRow
                    Label {
                        topPadding: 12
                        bottomPadding: 4
                        text: "%1  ·  %2".arg(modelData.title).arg(modelData.count)
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: theme.accent
                        elide: Text.ElideRight
                    }
                }
                Component {
                    id: resultRow
                    ItemDelegate {
                        padding: 8
                        contentItem: RowLayout {
                            spacing: 12
                            Label {
                                Layout.preferredWidth: 110
                                Layout.alignment: Qt.AlignTop
                                text: modelData.label || ""
                                font.pixelSize: 12
                                opacity: 0.6
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.snippet
                                textFormat: Text.StyledText
                                wrapMode: Text.WordWrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                                font.pixelSize: 13
                                color: "#d8d8de"
                            }
                        }
                        onClicked: {
                            search.close()
                            win.openBookAt(modelData.bookId, modelData.target)
                        }
                    }
                }
            }
        }
    }
}
