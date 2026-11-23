import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtWebEngine
import QtWebChannel

// The knowledge graph of what has been read: books, chapters, highlights,
// notes and the concepts they share (src/web/graph.js draws it). Opened
// for the whole library or one book; clicking a passage opens it.
Popup {
    id: graphPopup
    anchors.centerIn: Overlay.overlay
    width: (parent ? parent.width : 1000) - 40
    height: (parent ? parent.height : 760) - 40
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#18181d"; radius: theme.radius; border.color: "#2c2c33" }

    property string bookId: ""          // "" for every book
    property bool pageReady: false
    property int itemCount: 0
    // The books with highlights or notes, for the chooser.
    property var books: []
    readonly property var shownBooks: bookId === "" ? books : books.filter(b => b.id === bookId)
    property int pendingCount: 0

    function openFor(id) {
        bookId = id || ""
        books = library.allBooks().filter(b => annotations.count(b.id) > 0)
        open()
    }
    onOpened: refresh()
    function refresh() {
        if (!visible)
            return
        const data = knowledge.graph(shownBooks)
        itemCount = data.nodes.filter(n => n.kind === "highlight" || n.kind === "note").length
        pendingCount = knowledge.pending(shownBooks)
        if (pageReady)
            web.runJavaScript("Graph.update(" + JSON.stringify(data) + ")")
    }
    function pushFilter() {
        if (pageReady)
            web.runJavaScript("Graph.filter(" + JSON.stringify({ query: searchField.text, allConcepts: allConcepts.checked }) + ")")
    }
    onBookIdChanged: refresh()

    Connections {
        target: annotations
        function onChanged() { refreshTimer.restart() }
    }
    Connections {
        target: knowledge
        function onChanged() { refreshTimer.restart() }
    }
    // Several changes in a row (a batch of concepts) redraw once.
    Timer { id: refreshTimer; interval: 300; onTriggered: graphPopup.refresh() }

    QtObject {
        id: host
        WebChannel.id: "host"
        function ready() {
            web.runJavaScript("Graph.setup(" + JSON.stringify({
                accent: String(theme.accent),
                strings: {
                    "Open in the book": qsTr("Open in the book"),
                    "%1 passages in %2 books": qsTr("%1 passages in %2 books"),
                    "%1 highlights and notes": qsTr("%1 highlights and notes"),
                    "Book": qsTr("Book")
                }
            }) + ")")
            graphPopup.pageReady = true
            graphPopup.refresh()
            graphPopup.pushFilter()
        }
        function open(json) {
            const item = JSON.parse(json)
            graphPopup.close()
            win.openBookAt(item.bookId, item.format === "pdf" ? parseInt(item.location) : item.location)
        }
    }
    WebChannel {
        id: channel
        registeredObjects: [host]
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label {
                text: qsTr("Knowledge graph")
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            ComboBox {
                id: bookChooser
                Layout.preferredWidth: 260
                model: [qsTr("All books")].concat(graphPopup.books.map(b => b.title))
                currentIndex: graphPopup.bookId === "" ? 0
                              : graphPopup.books.findIndex(b => b.id === graphPopup.bookId) + 1
                onActivated: index => graphPopup.bookId = index === 0 ? "" : graphPopup.books[index - 1].id
            }
            TextField {
                id: searchField
                Layout.preferredWidth: 220
                placeholderText: qsTr("Find in the graph")
                onTextChanged: searchTimer.restart()
                Timer { id: searchTimer; interval: 250; onTriggered: graphPopup.pushFilter() }
            }
            CheckBox {
                id: allConcepts
                text: qsTr("All concepts")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Also show concepts that only one passage mentions")
                onToggled: graphPopup.pushFilter()
            }
            Item { Layout.fillWidth: true }
            // Concepts from the local model, asked for once per passage.
            Label {
                visible: knowledge.busy
                text: qsTr("Finding concepts… %1 of %2").arg(knowledge.done).arg(knowledge.total)
                opacity: 0.7
            }
            Button {
                visible: knowledge.busy
                flat: true
                text: qsTr("Stop")
                onClicked: knowledge.cancel()
            }
            Button {
                visible: !knowledge.busy && graphPopup.pendingCount > 0
                enabled: ollama.status === "ready"
                text: qsTr("Find concepts (%1 new)").arg(graphPopup.pendingCount)
                ToolTip.visible: hovered
                ToolTip.text: ollama.status === "ready"
                              ? qsTr("The local AI picks out the key ideas of each highlight and note, once, so passages about the same idea connect — across books too.")
                              : qsTr("Needs local AI, which is not ready.")
                onClicked: knowledge.findConcepts(graphPopup.shownBooks)
            }
            ToolbarButton {
                iconName: "fit-width"
                tip: qsTr("Fit everything in view")
                onClicked: web.runJavaScript("Graph.fit()")
            }
            ToolbarButton {
                iconName: "close"
                tip: qsTr("Close (Esc)")
                onClicked: graphPopup.close()
            }
        }
        Label {
            visible: knowledge.error !== ""
            Layout.fillWidth: true
            text: qsTr("Could not find concepts: %1").arg(knowledge.error)
            color: "#ff8a80"
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#121216"
            radius: Math.min(6, theme.radius)
            clip: true
            WebEngineView {
                id: web
                anchors.fill: parent
                anchors.margins: 1
                webChannel: channel
                activeFocusOnPress: true
                backgroundColor: "#121216"
                url: "shelf://app/graph.html"
                onContextMenuRequested: request => request.accepted = true
                onNavigationRequested: request => {
                    if (!request.url.toString().startsWith("shelf://app/"))
                        request.reject()
                }
            }
            Column {
                anchors.centerIn: parent
                visible: graphPopup.itemCount === 0
                spacing: 8
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Nothing to connect yet")
                    font.pixelSize: 18
                }
                Label {
                    width: 420
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    opacity: 0.65
                    text: qsTr("Highlight passages and write notes as you read; they appear here, joined to their chapters. Type [[a concept]] in a note to link it to others, or let the local AI find the concepts.")
                }
            }
        }
        Label {
            Layout.fillWidth: true
            opacity: 0.5
            font.pixelSize: 12
            text: qsTr("Drag to move around, scroll to zoom, click a dot for details. Teal concepts appear in more than one book.")
        }
    }
}
