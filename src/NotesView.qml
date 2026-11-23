import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A book's highlights, notes, and bookmarks in reading order, grouped by
// chapter, with a filter, search, and export.
Item {
    id: notesView
    property var book: ({})
    readonly property string bookId: book.id || ""
    signal chosen(var annotation)
    signal editRequested(var annotation)

    // Each note's chapter and section in the outline, for Copy all and
    // Export, so the Markdown is organised like this view.
    function places() {
        const out = {}
        for (const a of annotations.list(bookId))
            out[a.id] = placeOf(a)
        return out
    }
    function copyAll() {
        annotations.copyToClipboard(annotations.markdown(bookId, book.title || "", book.author || "", places()))
        win.showToast(qsTr("Notes copied as Markdown."))
    }
    function exportAll() {
        const path = annotations.exportMarkdown(bookId, book.title || "", book.author || "", "", places())
        win.showToast(path !== "" ? qsTr("Saved to %1").arg(path)
                                  : qsTr("Could not save the notes: %1").arg(annotations.lastError()))
    }

    // The book's outline ([{label, depth, target}]); notes on PDF pages are
    // grouped and titled by the chapter and section they fall in.
    property var outline: []
    property string filter: "all"   // "highlight", "note", "bookmark"

    // The depth chapters sit at: below parts, when there are parts.
    readonly property int chapterDepth: {
        let depth = -1
        for (const e of outline)
            if (/^chapter\b/i.test(e.label) && (depth < 0 || e.depth < depth))
                depth = e.depth
        return depth < 0 ? 0 : depth
    }
    // Chapter and section for a note: from the outline for page numbers,
    // from what was recorded when it was made otherwise.
    function placeOf(a) {
        if (!/^\d+$/.test(a.location || "") || outline.length === 0) {
            // EPUB notes recorded their chapter's title; show it numbered.
            for (const e of outline)
                if (a.chapter && e.title === a.chapter)
                    return { group: e.label, subgroup: "" }
            return { group: a.chapter || "", subgroup: "" }
        }
        const page = parseInt(a.location)
        let group = "", subgroup = ""
        for (const e of outline) {
            if (typeof e.target !== "number" || e.target > page)
                continue
            if (e.depth <= chapterDepth) {
                group = e.label
                subgroup = ""
            } else {
                subgroup = e.label
            }
        }
        return { group: group, subgroup: subgroup }
    }

    // Collapsed chapters, and notes opened out in full.
    property var collapsed: ({})
    property var opened: ({})
    property int foldRevision: 0
    function isCollapsed(group) { foldRevision; return collapsed[group] === true }
    function toggleGroup(group) {
        collapsed[group] = !collapsed[group]
        foldRevision++
    }
    function isOpened(id) { foldRevision; return opened[id] === true }
    function toggleOpened(id) {
        opened[id] = !opened[id]
        foldRevision++
    }
    readonly property var groups: {
        const out = []
        for (const a of shown)
            if (out.indexOf(a.group) < 0)
                out.push(a.group)
        return out
    }
    readonly property bool allCollapsed: { foldRevision; return groups.length > 0 && groups.every(g => collapsed[g] === true) }
    function setAllCollapsed(value) {
        const next = {}
        for (const g of groups)
            next[g] = value
        collapsed = next
        foldRevision++
    }
    function countIn(group) {
        let n = 0
        for (const a of shown)
            if (a.group === group)
                n++
        return n
    }
    property string search: ""

    readonly property var items: {
        annotations.revision
        return bookId ? annotations.list(bookId) : []
    }
    readonly property var shown: items.map(a => Object.assign({}, a, placeOf(a))).filter(a => {
        if (filter === "note" && !(a.note && a.note.length))
            return false
        if (filter === "highlight" && a.kind !== "highlight")
            return false
        if (filter === "bookmark" && a.kind !== "bookmark")
            return false
        if (search !== "") {
            const s = search.toLowerCase()
            return (a.text || "").toLowerCase().includes(s) || (a.note || "").toLowerCase().includes(s)
                || (a.group || "").toLowerCase().includes(s) || (a.subgroup || "").toLowerCase().includes(s)
        }
        return true
    })
    readonly property var swatch: ({ yellow: "#f5d547", green: "#6fd08c", blue: "#64b0f4", pink: "#f584b0" })

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            spacing: 4
            CompactField {
                Layout.fillWidth: true
                hint: qsTr("Search notes")
                onTextChanged: notesView.search = text
            }
            ToolbarButton {
                visible: notesView.groups.length > 1
                iconName: notesView.allCollapsed ? "chevron-right" : "chevron-down"
                tip: notesView.allCollapsed ? qsTr("Expand all") : qsTr("Collapse all")
                onClicked: notesView.setAllCollapsed(!notesView.allCollapsed)
            }
        }

        Segmented {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            options: [
                { key: "all", label: qsTr("All") },
                { key: "highlight", label: qsTr("Highlights") },
                { key: "note", label: qsTr("Notes") },
                { key: "bookmark", label: qsTr("Bookmarks") }
            ]
            value: notesView.filter
            onPicked: key => notesView.filter = key
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: notesView.shown
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            section.property: "group"
            section.delegate: Rectangle {
                id: header
                required property string section
                width: ListView.view.width
                height: 34
                color: headerMouse.containsMouse ? "#18181d" : "transparent"
                MouseArea {
                    id: headerMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: notesView.toggleGroup(header.section)
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 14
                    spacing: 4
                    Icon {
                        name: notesView.isCollapsed(header.section) ? "chevron-right" : "chevron-down"
                        size: 14
                        color: "#9a9aa2"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: header.section || qsTr("Before the first chapter")
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        opacity: 0.7
                        elide: Text.ElideRight
                    }
                    Label {
                        text: notesView.countIn(header.section).toLocaleString(Qt.locale(), 'f', 0)
                        font.pixelSize: 11
                        opacity: 0.45
                    }
                }
            }

            delegate: Rectangle {
                id: card
                required property var modelData
                readonly property bool folded: notesView.isCollapsed(modelData.group)
                width: ListView.view.width
                visible: !folded
                height: folded ? 0 : content.implicitHeight + 18
                implicitHeight: content.implicitHeight + 18
                color: cardMouse.containsMouse ? "#1b1b20" : "transparent"

                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => mouse.button === Qt.RightButton ? notesView.editRequested(card.modelData)
                                                                         : notesView.chosen(card.modelData)
                    onDoubleClicked: notesView.editRequested(card.modelData)
                }

                RowLayout {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 9
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 10

                    Rectangle {
                        Layout.fillHeight: true
                        Layout.preferredWidth: 3
                        radius: 1
                        color: card.modelData.kind === "bookmark" ? theme.accent
                             : notesView.swatch[card.modelData.color] || "#8a8a92"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            // PDF highlights say which page they are on.
                            visible: card.modelData.kind !== "highlight" || /^\d+$/.test(card.modelData.location || "")
                            spacing: 6
                            Icon {
                                name: card.modelData.kind === "bookmark" ? "bookmark-filled"
                                    : card.modelData.kind === "highlight" ? "highlight" : "note"
                                size: 13
                                color: card.modelData.kind === "bookmark" ? theme.accent : "#c4c4cc"
                            }
                            Label {
                                text: card.modelData.kind === "bookmark" ? qsTr("Bookmark")
                                    : card.modelData.kind === "highlight" ? qsTr("Highlight") : qsTr("Page note")
                                font.pixelSize: 12
                                opacity: 0.75
                            }
                            Label {
                                visible: /^\d+$/.test(card.modelData.location || "")
                                text: qsTr("p. %1").arg((parseInt(card.modelData.location) + 1).toLocaleString(Qt.locale(), 'f', 0))
                                font.pixelSize: 12
                                opacity: 0.5
                            }
                        }
                        Label {
                            // The section it is in, from the outline.
                            visible: (card.modelData.subgroup || "") !== ""
                            Layout.fillWidth: true
                            text: card.modelData.subgroup || ""
                            font.pixelSize: 11
                            opacity: 0.5
                            elide: Text.ElideRight
                        }
                        Label {
                            visible: (card.modelData.text || "") !== ""
                            Layout.fillWidth: true
                            text: card.modelData.text || ""
                            wrapMode: Text.WordWrap
                            maximumLineCount: 4
                            elide: Text.ElideRight
                            font.pixelSize: 13
                            color: "#d8d8de"
                        }
                        // Long notes (AI answers especially) show their first
                        // lines until opened out.
                        Item {
                            id: noteBox
                            readonly property real collapsedHeight: 112
                            readonly property bool long: noteLabel.implicitHeight > collapsedHeight + 8
                            visible: (card.modelData.note || "") !== ""
                            Layout.fillWidth: true
                            implicitHeight: long && !notesView.isOpened(card.modelData.id) ? collapsedHeight
                                                                                          : noteLabel.implicitHeight
                            clip: true
                            Label {
                                id: noteLabel
                                width: parent.width
                                text: card.modelData.note || ""
                                // AI answers are Markdown; the user's own notes stay as typed.
                                textFormat: (card.modelData.note || "").indexOf("✦ ") >= 0 ? Text.MarkdownText : Text.PlainText
                                wrapMode: Text.WordWrap
                                font.pixelSize: 13
                                color: theme.accent
                            }
                            // Fades the cut-off line into the card.
                            Rectangle {
                                visible: noteBox.long && !notesView.isOpened(card.modelData.id)
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 28
                                gradient: Gradient {
                                    GradientStop { position: 0; color: "transparent" }
                                    GradientStop { position: 1; color: cardMouse.containsMouse ? "#1b1b20" : "#121215" }
                                }
                            }
                        }
                        Label {
                            visible: noteBox.visible && noteBox.long
                            text: notesView.isOpened(card.modelData.id) ? qsTr("Show less") : qsTr("Show more")
                            font.pixelSize: 12
                            font.underline: moreMouse.containsMouse
                            opacity: 0.75
                            MouseArea {
                                id: moreMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: notesView.toggleOpened(card.modelData.id)
                            }
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - 40
                spacing: 8
                visible: list.count === 0
                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: notesView.items.length === 0 ? qsTr("No notes yet") : qsTr("Nothing matches")
                    font.pixelSize: 15
                }
                Label {
                    width: parent.width
                    visible: notesView.items.length === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Select text to highlight it or add a note. The bookmark button marks the page you're on.")
                    font.pixelSize: 12
                    opacity: 0.55
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 10
            visible: notesView.items.length > 0
            ToolbarButton {
                Layout.fillWidth: true
                iconName: "copy"
                label: qsTr("Copy all")
                onClicked: notesView.copyAll()
            }
            ToolbarButton {
                Layout.fillWidth: true
                iconName: "export"
                label: qsTr("Export")
                tip: qsTr("Save as a Markdown file in Documents/Shelf")
                onClicked: notesView.exportAll()
            }
            ToolbarButton {
                Layout.fillWidth: true
                iconName: "graph"
                label: qsTr("Graph")
                tip: qsTr("This book's highlights and notes as a knowledge graph")
                onClicked: win.openGraph(notesView.bookId)
            }
        }
    }

}
