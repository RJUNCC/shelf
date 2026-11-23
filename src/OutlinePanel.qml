import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Pdf

// The docked panel beside a book: who and where (cover, title, progress),
// then a collapsible outline, page thumbnails for PDFs, and the book's notes.
Rectangle {
    id: panel
    property var book: ({})
    property real progress: 0
    property var entries: []          // [{ label, depth, target }]
    property string currentLabel
    property var pdfDocument: null    // set for PDFs to show the Pages view
    property int currentPage: 0
    property string view: "contents"  // "pages" or "notes"
    // A PDF with no outline of its own can have one built by local AI;
    // `aiOutline` says the entries shown came from it.
    property bool aiAvailable: false
    property bool aiOutline: false
    signal chosen(var target)
    signal practiceRequested(int index)
    signal pageChosen(int page)
    signal annotationChosen(var annotation)
    signal annotationEdit(var annotation)

    color: "#121215"

    // Parent of each entry, and which entries have children, so the tree
    // can collapse.
    readonly property var parents: {
        const out = []
        const stack = []
        for (let i = 0; i < entries.length; ++i) {
            const depth = entries[i].depth
            while (stack.length && entries[stack[stack.length - 1]].depth >= depth)
                stack.pop()
            out.push(stack.length ? stack[stack.length - 1] : -1)
            stack.push(i)
        }
        return out
    }
    readonly property var hasChildren: {
        const out = entries.map(() => false)
        for (let i = 0; i < parents.length; ++i)
            if (parents[i] >= 0)
                out[parents[i]] = true
        return out
    }
    // The entry clicked last: several can start on one page, and the
    // reader only knows the page, so while it stays there the clicked one
    // is the one shown as current.
    property int picked: -1
    onEntriesChanged: picked = -1
    readonly property int currentIndex: {
        for (let i = 0; i < entries.length; ++i)
            if (entries[i].label === currentLabel || entries[i].title === currentLabel) {
                const p = entries[picked]
                return p && String(p.target) === String(entries[i].target) ? picked : i
            }
        return -1
    }

    // Collapsed state by entry index. Top-level chapters start collapsed in
    // long outlines, except the one being read.
    property var expanded: ({})
    property int expandedRevision: 0
    function isExpanded(i) {
        if (expanded[i] !== undefined)
            return expanded[i]
        return entries.length <= 40 || isAncestorOfCurrent(i)
    }
    function isAncestorOfCurrent(i) {
        for (let p = currentIndex; p >= 0; p = parents[p])
            if (p === i)
                return true
        return false
    }
    function toggle(i) {
        expanded[i] = !isExpanded(i)
        expandedRevision++
    }
    readonly property var visibleRows: {
        expandedRevision
        const rows = []
        for (let i = 0; i < entries.length; ++i) {
            let shown = true
            for (let p = parents[i]; p >= 0; p = parents[p])
                if (!isExpanded(p)) { shown = false; break }
            if (shown)
                rows.push(i)
        }
        return rows
    }

    function revealCurrent() {
        const row = visibleRows.indexOf(currentIndex)
        if (row >= 0)
            tree.positionViewAtIndex(row, ListView.Center)
    }

    onCurrentIndexChanged: if (view === "contents") Qt.callLater(revealCurrent)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Foliate-style header: cover, title, author, progress.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 12
            BookCover {
                Layout.preferredWidth: 54
                Layout.preferredHeight: 78
                book: panel.book
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label {
                    Layout.fillWidth: true
                    text: panel.book.title || ""
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: panel.book.author || ""
                    font.pixelSize: 12
                    opacity: 0.6
                    elide: Text.ElideRight
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8
                    Rectangle {
                        Layout.fillWidth: true
                        height: 4
                        radius: 2
                        color: "#2a2a30"
                        Rectangle {
                            width: parent.width * panel.progress
                            height: parent.height
                            radius: 2
                            color: theme.accent
                        }
                    }
                    Label {
                        text: Math.round(panel.progress * 100).toLocaleString(Qt.locale(), 'f', 0) + "%"
                        font.pixelSize: 11
                        opacity: 0.6
                    }
                }
            }
        }

        // Outline / Pages / Notes switch, Acrobat-style.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 6
            spacing: 4
            ToolbarButton {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                iconName: "outline"
                label: qsTr("Outline")
                checked: panel.view === "contents"
                onClicked: panel.view = "contents"
            }
            ToolbarButton {
                visible: panel.pdfDocument !== null
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                iconName: "pages"
                label: qsTr("Pages")
                checked: panel.view === "pages"
                onClicked: panel.view = "pages"
            }
            ToolbarButton {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                iconName: "notes"
                label: notesView.items.length > 0 ? qsTr("Notes (%1)").arg(notesView.items.length.toLocaleString(Qt.locale(), 'f', 0)) : qsTr("Notes")
                checked: panel.view === "notes"
                onClicked: panel.view = "notes"
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#222228" }

        AiOutlineCard {
            id: aiCard
            visible: panel.view === "contents" && panel.aiAvailable && (panel.entries.length === 0 || running || failed)
            Layout.fillWidth: true
            Layout.margins: 10
            book: panel.book
            hasEntries: panel.entries.length > 0
        }

        ListView {
            id: tree
            visible: panel.view === "contents"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.visibleRows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            topMargin: 6
            bottomMargin: 6

            delegate: Rectangle {
                id: row
                required property int modelData
                readonly property var entry: panel.entries[modelData]
                readonly property bool current: modelData === panel.currentIndex
                width: ListView.view.width
                height: Math.max(34, label.implicitHeight + 14)
                color: current ? Qt.alpha(theme.accent, 0.14) : rowMouse.containsMouse ? "#1b1b20" : "transparent"

                Rectangle {
                    visible: row.current
                    width: 3
                    height: parent.height
                    color: theme.accent
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            entryMenu.index = row.modelData
                            entryMenu.popup()
                            return
                        }
                        panel.picked = row.modelData
                        panel.chosen(row.entry.target)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8 + row.entry.depth * 14
                    anchors.rightMargin: 12
                    spacing: 4
                    AbstractButton {
                        implicitWidth: 22
                        implicitHeight: 22
                        focusPolicy: Qt.NoFocus
                        enabled: panel.hasChildren[row.modelData] === true
                        opacity: enabled ? 1 : 0
                        onClicked: panel.toggle(row.modelData)
                        contentItem: Item {
                            Icon {
                                anchors.centerIn: parent
                                size: 14
                                color: "#9a9aa2"
                                name: { panel.expandedRevision; return panel.isExpanded(row.modelData) ? "chevron-down" : "chevron-right" }
                            }
                        }
                        background: null
                    }
                    // The number in its own muted column, level with the
                    // title's first line, so titles line up.
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: label.implicitHeight
                        Label {
                            id: numberLabel
                            visible: (row.entry.number || "") !== ""
                            // Measured apart from the label, whose own width
                            // would otherwise feed back into it.
                            width: visible ? Math.max(numberMetrics.advanceWidth + 8, row.entry.depth === 0 ? 26 : 38) : 0
                            TextMetrics {
                                id: numberMetrics
                                font: numberLabel.font
                                text: numberLabel.text
                            }
                            text: row.entry.number || ""
                            font.pixelSize: label.font.pixelSize
                            font.features: { "tnum": 1 }
                            color: row.current ? theme.accent : "#8a8a92"
                        }
                        Label {
                            id: label
                            x: numberLabel.width
                            width: parent.width - x
                            text: (row.entry.number ? row.entry.title : row.entry.label) || qsTr("Untitled")
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            font.pixelSize: row.entry.depth === 0 ? 13 : 12
                            font.weight: row.entry.depth === 0 ? Font.DemiBold : Font.Normal
                            color: row.current ? theme.accent : row.entry.depth === 0 ? "#ececf0" : "#c4c4cc"
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: panel.entries.length === 0 && !aiCard.visible
                text: qsTr("This book has no outline.")
                opacity: 0.5
            }
        }

        // Says where an AI outline came from, and offers to build it again.
        RowLayout {
            visible: panel.view === "contents" && panel.aiOutline && !aiCard.running
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 8
            Layout.bottomMargin: 6
            Label {
                Layout.fillWidth: true
                text: aiCard.failed ? qsTr("Outline made by AI, unfinished") : qsTr("Outline made by AI")
                font.pixelSize: 11
                opacity: 0.5
                elide: Text.ElideRight
            }
            ToolbarButton {
                label: qsTr("Rebuild")
                tip: qsTr("Build the outline again")
                onClicked: {
                    outlines.clear(panel.book.id)
                    ollama.refresh()
                    rebuildTimer.start()
                }
                // Gives Ollama a moment to answer which models it has.
                Timer { id: rebuildTimer; interval: 600; onTriggered: aiCard.build() }
            }
        }

        NotesView {
            id: notesView
            visible: panel.view === "notes"
            Layout.fillWidth: true
            Layout.fillHeight: true
            book: panel.book
            outline: panel.entries
            onChosen: annotation => panel.annotationChosen(annotation)
            onEditRequested: annotation => panel.annotationEdit(annotation)
        }

        GridView {
            id: thumbs
            visible: panel.view === "pages" && panel.pdfDocument !== null
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.pdfDocument ? panel.pdfDocument.pageCount : 0
            cellWidth: width / Math.max(1, Math.floor(width / 140))
            cellHeight: cellWidth * 1.45
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            onVisibleChanged: if (visible) positionViewAtIndex(panel.currentPage, GridView.Center)

            delegate: Item {
                required property int index
                width: thumbs.cellWidth
                height: thumbs.cellHeight
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.bottomMargin: 22
                    color: "white"
                    border.width: index === panel.currentPage ? 3 : 0
                    border.color: theme.accent
                    PdfPageImage {
                        anchors.fill: parent
                        anchors.margins: parent.border.width
                        document: panel.pdfDocument
                        currentFrame: index
                        asynchronous: true
                        retainWhileLoading: true
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: 220
                    }
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    text: (index + 1).toLocaleString(Qt.locale(), 'f', 0)
                    font.pixelSize: 11
                    color: index === panel.currentPage ? theme.accent : "#9a9aa2"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.pageChosen(index)
                }
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: "#222228"
    }

    Menu {
        id: entryMenu
        property int index: 0
        Material.roundedScale: theme.radius
        MenuItem { text: qsTr("Go here"); onTriggered: panel.chosen(panel.entries[entryMenu.index].target) }
        MenuItem { text: qsTr("Practice test on this…"); onTriggered: panel.practiceRequested(entryMenu.index) }
    }
}
