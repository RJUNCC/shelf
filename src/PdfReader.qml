import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Pdf
import "numbering.js" as Numbering

// Reads a PDF with Qt's own renderer: scrolled or page by page, zoom, find,
// highlights and notes on selected text, and the document's bookmarks and
// page thumbnails in the docked outline.
FocusScope {
    id: reader
    property var book: ({})
    property bool active: false
    signal libraryRequested()
    // Menus belong to the tab on screen; switching tabs closes them.
    onActiveChanged: if (!active) viewMenu.close()

    property bool restored: false
    // The PDF's own outline, or else one built by local AI.
    property var pdfEntries: []
    readonly property var aiEntries: { outlines.revision; return outlines.entries(book.id || "") }
    // Numbered (14, 14.2, 14.2.1) whatever the outline came from.
    readonly property var tocEntries: Numbering.labelled(pdfEntries.length > 0 ? pdfEntries : aiEntries)
    property bool outlineOpen: library.setting("outlineOpen", win.width > 1100)
    property string mode: library.setting("pdfMode", "scroll")
    property real scrollSpeed: library.setting("pdfScrollSpeed", 1.5)
    // "", "clean", or "sharp"; older settings saved true for "clean".
    property string cleanMode: {
        const saved = library.setting("pdfClean", "")
        return saved === true ? "clean" : saved === false ? "" : String(saved)
    }
    // The passage selected, or the highlight clicked, that the bar acts on.
    property var selection: null        // { page, text, paths, existing: annotation or null }

    function zoomBy(factor) {
        view.zoom = Math.max(0.3, Math.min(6, view.zoom * factor))
    }
    function goToPage(page) {
        view.goToPage(page, true)
    }
    function toggleOutline() {
        outlineOpen = !outlineOpen
        library.setSetting("outlineOpen", outlineOpen)
    }
    function setMode(value) {
        const page = view.currentPage
        mode = value
        library.setSetting("pdfMode", value)
        reanchor.page = page
        reanchor.restart()
    }
    function setScrollSpeed(value) {
        scrollSpeed = value
        library.setSetting("pdfScrollSpeed", value)
    }
    readonly property bool typing: findField.activeFocus || pageField.activeFocus || aiPanel.typing

    // Bookmarks and notes on PDF pages: the location is the page number.
    readonly property var allAnnotations: {
        annotations.revision
        return annotations.list(book.id)
    }
    readonly property var pageAnnotations: allAnnotations.filter(a => a.location === String(view.currentPage))
    readonly property var pageBookmark: pageAnnotations.find(a => a.kind === "bookmark") || null
    readonly property var pageNote: pageAnnotations.find(a => a.kind === "note") || null

    // Highlights by page, ready to draw: stored rectangles become point
    // lists, with a bounding box for clicks and the note marker.
    readonly property var highlightsByPage: {
        const out = {}
        for (const a of allAnnotations) {
            if (a.kind !== "highlight" || !/^\d+$/.test(a.location) || !a.rects)
                continue
            const paths = a.rects.map(flat => {
                const points = []
                for (let i = 0; i + 1 < flat.length; i += 2)
                    points.push(Qt.point(flat[i], flat[i + 1]))
                return points
            })
            let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity
            for (const path of paths)
                for (const p of path) {
                    x0 = Math.min(x0, p.x); y0 = Math.min(y0, p.y)
                    x1 = Math.max(x1, p.x); y1 = Math.max(y1, p.y)
                }
            const page = parseInt(a.location)
            if (!out[page])
                out[page] = []
            out[page].push({ id: a.id, color: a.color, hasNote: !!(a.note && a.note.length), paths: paths,
                             box: Qt.rect(x0, y0, x1 - x0, y1 - y0) })
        }
        return out
    }

    // Keeps an AI answer with the passage: added to the highlight's note,
    // or as a new highlight with the answer as its note.
    function saveAiAnswer(answer, action, target) {
        const block = win.aiNoteBlock(answer, action)
        let id = ""
        if (target.existing) {
            const a = annotations.get(book.id, target.existing.id)
            id = a.id
            annotations.update(book.id, a.id, { note: a.note ? a.note + "\n\n" + block : block })
        } else {
            id = addHighlight("yellow", target, block)
        }
        view.clearSelection()
        win.showToast(qsTr("Saved to notes."))
        return id
    }
    function askAi(target) {
        aiPanel.open({ text: target.text, page: target.page, paths: target.paths, existing: target.existing,
                       context: pdfGeometry.pageText(book.path || "", target.page) })
    }
    function toggleBookmark() {
        if (pageBookmark) {
            annotations.remove(book.id, pageBookmark.id)
            win.showToast(qsTr("Bookmark removed. Ctrl+Z brings it back."))
        } else {
            annotations.add(book.id, { kind: "bookmark", location: String(view.currentPage), position: progress,
                                       chapter: currentLabel })
        }
    }
    function editPageNote() {
        // A page note is only created once something is written in it.
        noteEditor.annotationId = pageNote ? pageNote.id : ""
        noteEditor.page = view.currentPage
        noteEditor.heading = qsTr("Note on page %1").arg((view.currentPage + 1).toLocaleString(Qt.locale(), 'f', 0))
        noteEditor.canDelete = pageNote !== null
        noteEditor.openWith("", pageNote ? pageNote.note : "")
    }
    function editAnnotation(annotation) {
        noteEditor.annotationId = annotation.id
        noteEditor.heading = annotation.kind === "bookmark" ? qsTr("Bookmark note") : qsTr("Note")
        noteEditor.canDelete = true
        noteEditor.openWith(annotation.text || "", annotation.note || "")
    }
    function addHighlight(color, from, note) {
        const s = from || selection
        const size = doc.pagePointSize(s.page)
        let top = Infinity
        for (const path of s.paths)
            for (const p of path)
                top = Math.min(top, p.y)
        const within = size.height > 0 && top !== Infinity ? top / size.height : 0
        const id = annotations.add(book.id, {
            kind: "highlight", location: String(s.page), text: s.text, color: color,
            // Pages in order, and passages in order down each page.
            position: doc.pageCount > 0 ? (s.page + within) / doc.pageCount : 0,
            chapter: labelFor(s.page),
            note: note || "",
            rects: s.paths.map(path => path.reduce((flat, p) => flat.concat([Math.round(p.x * 100) / 100,
                                                                              Math.round(p.y * 100) / 100]), []))
        })
        view.clearSelection()
        return id
    }
    function highlightSelection(color) {
        if (!selection)
            return
        if (selection.existing)
            annotations.update(book.id, selection.existing.id, { color: color })
        else
            addHighlight(color)
        selection = null
    }
    function noteOnSelection() {
        if (!selection)
            return
        const target = selection.existing ? selection.existing : annotations.get(book.id, addHighlight("yellow"))
        selection = null
        editAnnotation(target)
    }
    function clearSelection() {
        selection = null
        view.clearSelection()
    }
    function undo() { win.announceUndo(annotations.undo(book.id)) }
    function redo() { win.announceUndo(annotations.redo(book.id)) }

    // A search result or citation opened this book at a place.
    function tryJump() {
        const j = win.pendingJump
        if (!j || j.bookId !== reader.book.id || !reader.restored)
            return
        win.pendingJump = null
        reader.goToPage(j.target)
    }
    Connections {
        target: win
        function onPendingJumpChanged() { reader.tryJump() }
    }
    onRestoredChanged: tryJump()
    Shortcut { sequence: "Ctrl+K"; enabled: reader.active; onActivated: aiPanel.bookMode ? aiPanel.close() : aiPanel.open({ book: true }) }
    Shortcut { sequences: ["Left", "PgUp", "Shift+Space"]; enabled: reader.active && !reader.typing; onActivated: view.prev() }
    Shortcut { sequences: ["Right", "PgDown", "Space"]; enabled: reader.active && !reader.typing; onActivated: view.next() }
    Shortcut {
        sequence: "Up"
        enabled: reader.active && !reader.typing
        onActivated: view.paged ? view.prev() : view.scrollBy(0, -100 * reader.scrollSpeed, true)
    }
    Shortcut {
        sequence: "Down"
        enabled: reader.active && !reader.typing
        onActivated: view.paged ? view.next() : view.scrollBy(0, 100 * reader.scrollSpeed, true)
    }
    Shortcut { sequence: "Home"; enabled: reader.active && !reader.typing; onActivated: view.goToPage(0) }
    Shortcut { sequence: "End"; enabled: reader.active && !reader.typing; onActivated: view.goToPage(doc.pageCount - 1) }
    Shortcut { sequence: "T"; enabled: reader.active && !reader.typing; onActivated: reader.toggleOutline() }
    Shortcut { sequence: "B"; enabled: reader.active && !reader.typing; onActivated: reader.toggleBookmark() }
    Shortcut {
        sequence: "N"
        enabled: reader.active && !reader.typing
        onActivated: reader.selection ? reader.noteOnSelection() : reader.editPageNote()
    }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: reader.active && !reader.typing && reader.selection !== null
        onActivated: {
            annotations.copyToClipboard(reader.selection.text)
            win.showToast(qsTr("Copied."))
        }
    }
    Shortcut { sequences: [StandardKey.Undo]; enabled: reader.active && !reader.typing && !noteEditor.opened; onActivated: reader.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; enabled: reader.active && !reader.typing && !noteEditor.opened; onActivated: reader.redo() }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; enabled: reader.active; onActivated: reader.zoomBy(1.15) }
    Shortcut { sequences: [StandardKey.ZoomOut]; enabled: reader.active; onActivated: reader.zoomBy(1 / 1.15) }
    Shortcut { sequence: "Ctrl+0"; enabled: reader.active; onActivated: view.zoom = 1 }
    Shortcut { sequences: [StandardKey.Find]; enabled: reader.active; onActivated: findField.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+G"; enabled: reader.active; onActivated: { pageField.forceActiveFocus(); pageField.selectAll() } }
    Shortcut {
        sequence: "Esc"
        enabled: reader.active
        onActivated: {
            if (aiPanel.shown)
                aiPanel.close()
            else if (viewMenu.visible)
                viewMenu.close()
            else if (reader.selection)
                reader.clearSelection()
            else if (reader.typing || findField.text !== "") {
                findField.text = ""
                reader.forceActiveFocus()
            } else
                reader.libraryRequested()
        }
    }

    PdfDocument {
        id: doc
        source: library.readerUrl(reader.book.id)
        onStatusChanged: {
            if (doc.status === PdfDocument.Ready && !reader.restored) {
                reader.pdfEntries = reader.flattenBookmarks()
                reanchor.page = Math.max(0, Math.min(doc.pageCount - 1, parseInt(reader.book.location || "0") || 0))
                reanchor.restart()
            }
        }
    }

    // Brings a page back into view once the layout has settled: after
    // opening, switching between scrolling and turning pages, or zooming.
    Timer {
        id: reanchor
        property int page: 0
        interval: 60
        onTriggered: {
            if (view.width < 100) {
                restart()
                return
            }
            view.goToPage(page)
            reader.restored = true
        }
    }
    Connections {
        target: view
        function onZoomChanged() {
            if (!reanchor.running)
                reanchor.page = view.currentPage
            reanchor.restart()
        }
        function onWidthChanged() {
            if (!reader.restored)
                return
            if (!reanchor.running)
                reanchor.page = view.currentPage
            reanchor.restart()
        }
    }

    PdfBookmarkModel {
        id: bookmarks
        document: doc
    }

    PdfSearchModel {
        id: search
        document: doc
    }
    function findNext(step) {
        if (search.count <= 0)
            return
        search.currentResult = (search.currentResult + step + search.count) % search.count
        view.goToPage(search.currentPage)
    }

    // The bookmark model is a tree; flatten it for the outline.
    function flattenBookmarks() {
        const out = []
        const walk = (parentIndex, depth) => {
            const rows = bookmarks.rowCount(parentIndex)
            for (let r = 0; r < rows; ++r) {
                const index = bookmarks.index(r, 0, parentIndex)
                out.push({
                    label: bookmarks.data(index, PdfBookmarkModel.Title),
                    depth: depth,
                    target: bookmarks.data(index, PdfBookmarkModel.Page)
                })
                walk(index, depth + 1)
            }
        }
        walk(bookmarks.index(-1, -1), 0)
        return out
    }

    function labelFor(page) {
        let label = ""
        for (const e of tocEntries)
            if (e.target <= page)
                label = e.label
        return label
    }
    readonly property string currentLabel: labelFor(view.currentPage)
    readonly property real progress: doc.pageCount > 1 ? view.currentPage / (doc.pageCount - 1) : 0

    Connections {
        target: view
        function onCurrentPageChanged() {
            if (!reader.restored || doc.pageCount <= 0)
                return
            library.saveProgress(reader.book.id, String(view.currentPage), reader.progress)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ReaderBar {
            Layout.fillWidth: true
            visible: win.visibility !== Window.FullScreen
            title: reader.book.title || ""
            subtitle: reader.currentLabel
            outlineOpen: reader.outlineOpen
            onOutlineToggled: reader.toggleOutline()

            ToolbarButton {
                iconName: "ask"
                label: qsTr("Ask this book")
                showLabel: reader.width > 1100
                checked: aiPanel.bookMode
                tip: qsTr("Ask a question about the whole book (Ctrl+K)")
                onClicked: aiPanel.bookMode ? aiPanel.close() : aiPanel.open({ book: true })
            }
            ToolbarButton {
                iconName: reader.pageBookmark ? "bookmark-filled" : "bookmark"
                checked: reader.pageBookmark !== null
                tip: reader.pageBookmark ? qsTr("Remove bookmark (B)") : qsTr("Bookmark this page (B)")
                onClicked: reader.toggleBookmark()
            }
            ToolbarButton {
                iconName: "note"
                label: reader.pageNote ? qsTr("Page note") : qsTr("Add note")
                showLabel: reader.width > 1250
                checked: reader.pageNote !== null
                tip: qsTr("Write a note on this page (N)")
                onClicked: reader.editPageNote()
            }
            ToolbarButton {
                iconName: "undo"
                enabled: { annotations.revision; return annotations.canUndo(reader.book.id) }
                opacity: enabled ? 1 : 0.35
                tip: qsTr("Undo (Ctrl+Z)")
                onClicked: reader.undo()
            }
            ToolbarButton {
                iconName: "redo"
                enabled: { annotations.revision; return annotations.canRedo(reader.book.id) }
                opacity: enabled ? 1 : 0.35
                tip: qsTr("Redo (Ctrl+Shift+Z)")
                onClicked: reader.redo()
            }

            Rectangle { width: 1; height: 22; color: "#2a2a31"; Layout.leftMargin: 4; Layout.rightMargin: 4 }

            CompactField {
                id: findField
                Layout.preferredWidth: reader.width > 1100 ? 200 : 140
                hint: search.count > 0 && text !== ""
                      ? qsTr("%1 of %2").arg(search.currentResult + 1).arg(search.count) : qsTr("Find in document")
                onTextChanged: search.searchString = text
                onAccepted: reader.findNext(1)
                Keys.onUpPressed: reader.findNext(-1)
                Keys.onDownPressed: reader.findNext(1)
            }

            ToolbarButton {
                id: viewButton
                iconName: "view"
                label: qsTr("View")
                showLabel: reader.width > 1100
                tip: qsTr("Scrolling or page turning, and scroll speed")
                checked: viewMenu.visible
                onClicked: viewMenu.visible ? viewMenu.close() : viewMenu.open()

                Popup {
                    id: viewMenu
                    focus: false
                    closePolicy: Popup.CloseOnPressOutsideParent
                    y: parent.height + 8
                    x: parent.width - width
                    width: 380
                    padding: 18
                    Material.roundedScale: theme.radius
                    background: Rectangle { color: "#1b1b20"; radius: theme.radius; border.color: "#2c2c33" }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 16

                        SectionLabel { text: qsTr("Pages") }
                        Segmented {
                            Layout.fillWidth: true
                            options: [
                                { key: "scroll", label: qsTr("Scroll") },
                                { key: "horizontal", label: qsTr("Side to side") },
                                { key: "vertical", label: qsTr("Top to bottom") }
                            ]
                            value: reader.mode
                            onPicked: key => reader.setMode(key)
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: -8
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            opacity: 0.6
                            text: reader.mode === "scroll"
                                  ? qsTr("Pages run on continuously; the wheel scrolls through them.")
                                  : reader.mode === "horizontal"
                                    ? qsTr("One page at a time, turning left and right with the wheel or arrow keys.")
                                    : qsTr("One page at a time, turning up and down with the wheel or arrow keys.")
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { text: qsTr("Clean up scanned pages"); font.pixelSize: 13 }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Makes yellowed or gray paper white and the text darker; sharpen also crisps up blurry scans. The file is not changed.")
                                    font.pixelSize: 11
                                    opacity: 0.6
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                        Segmented {
                            Layout.fillWidth: true
                            Layout.topMargin: -6
                            options: [
                                { key: "", label: qsTr("Off") },
                                { key: "clean", label: qsTr("Clean") },
                                { key: "sharp", label: qsTr("Clean + sharpen") }
                            ]
                            value: reader.cleanMode
                            onPicked: key => {
                                reader.cleanMode = key
                                library.setSetting("pdfClean", key)
                            }
                        }

                        // A clean, reflowable copy of the book as an EPUB.
                        // Only for PDFs with real text: a scan with none (just
                        // pictures of pages) has nothing to reflow.
                        ColumnLayout {
                            id: textVersion
                            visible: hasText
                            readonly property bool hasText: pdfGeometry.hasTextLayer(reader.book.path || "")
                            Layout.fillWidth: true
                            spacing: 6
                            readonly property var job: { textVersions.revision; return textVersions.status(reader.book.id || "") }
                            readonly property string madeId: {
                                textVersions.revision
                                const id = library.setting("textVersion:" + (reader.book.id || ""), "")
                                return id !== "" && library.book(id).id ? id : ""
                            }
                            function make() {
                                textVersions.make(reader.book.id, reader.book.path || "", reader.book.title || "",
                                                  reader.book.author || "", reader.tocEntries)
                            }
                            Label { text: qsTr("Text version"); font.pixelSize: 13 }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("A clean copy that reflows like an EPUB, with your colours and text size; figures and tables are kept as pictures. Words the scan misread stay misread.")
                                font.pixelSize: 11
                                opacity: 0.6
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: textVersion.job.state === "working"
                                spacing: 8
                                ProgressBar {
                                    Layout.fillWidth: true
                                    indeterminate: !(textVersion.job.total > 0)
                                    value: textVersion.job.total > 0 ? textVersion.job.done / textVersion.job.total : 0
                                }
                                Button {
                                    text: qsTr("Stop")
                                    flat: true
                                    onClicked: textVersions.cancel(reader.book.id)
                                }
                            }
                            Label {
                                visible: textVersion.job.state === "failed"
                                Layout.fillWidth: true
                                text: textVersion.job.message || ""
                                color: "#ff8a80"
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                visible: textVersion.job.state !== "working"
                                spacing: 8
                                AccentButton {
                                    visible: textVersion.madeId !== ""
                                    text: qsTr("Open text version")
                                    onClicked: { viewMenu.close(); win.openBook(textVersion.madeId) }
                                }
                                AccentButton {
                                    visible: textVersion.madeId === ""
                                    text: qsTr("Make a text version")
                                    onClicked: textVersion.make()
                                }
                                Button {
                                    visible: textVersion.madeId !== ""
                                    text: qsTr("Make again")
                                    flat: true
                                    onClicked: textVersion.make()
                                }
                            }
                        }

                        SectionLabel { text: qsTr("Scroll speed") }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: qsTr("Slow"); font.pixelSize: 12; opacity: 0.6 }
                            Slider {
                                id: speedSlider
                                Layout.fillWidth: true
                                from: 0.5
                                to: 5
                                stepSize: 0.25
                                value: reader.scrollSpeed
                                onMoved: reader.setScrollSpeed(value)
                            }
                            Label { text: qsTr("Fast"); font.pixelSize: 12; opacity: 0.6 }
                            Label {
                                Layout.preferredWidth: 40
                                horizontalAlignment: Text.AlignRight
                                text: reader.scrollSpeed.toLocaleString(Qt.locale(), 'f', 2).replace(/0$/, "") + "×"
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }

            Rectangle { width: 1; height: 22; color: "#2a2a31"; Layout.leftMargin: 4; Layout.rightMargin: 4 }

            ToolbarButton { iconName: "minus"; tip: qsTr("Zoom out (Ctrl −)"); onClicked: reader.zoomBy(1 / 1.15) }
            ToolbarButton {
                label: Math.round(view.currentScale * 100).toLocaleString(Qt.locale(), 'f', 0) + "%"
                tip: view.paged ? qsTr("Fit page (Ctrl+0)") : qsTr("Fit width (Ctrl+0)")
                onClicked: view.zoom = 1
            }
            ToolbarButton { iconName: "plus"; tip: qsTr("Zoom in (Ctrl +)"); onClicked: reader.zoomBy(1.15) }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            OutlinePanel {
                id: outlinePanel
                Layout.fillHeight: true
                Layout.preferredWidth: 310
                visible: reader.outlineOpen
                book: reader.book
                progress: reader.progress
                entries: reader.tocEntries
                currentLabel: reader.currentLabel
                pdfDocument: doc
                currentPage: view.currentPage
                aiAvailable: doc.status === PdfDocument.Ready && reader.pdfEntries.length === 0
                aiOutline: reader.pdfEntries.length === 0 && reader.aiEntries.length > 0
                onChosen: target => reader.goToPage(target)
                onPageChosen: page => reader.goToPage(page)
                onAnnotationChosen: a => reader.goToPage(parseInt(a.location))
                onAnnotationEdit: a => reader.editAnnotation(a)
                onPracticeRequested: index => win.openPractice(reader.book, reader.tocEntries, index)
            }

            Rectangle {
                id: pageArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#1a1a1e"
                // Pages never draw over the toolbar or the outline.
                clip: true

                PdfPagesView {
                    id: view
                    anchors.fill: parent
                    document: doc
                    path: reader.book.path || ""
                    cleanMode: reader.cleanMode
                    ready: reader.restored
                    searchModel: search
                    mode: reader.mode
                    scrollSpeed: reader.scrollSpeed
                    highlights: reader.highlightsByPage
                    onTextSelected: (page, text, paths, box) => {
                        reader.selection = { page: page, text: text, paths: paths, existing: null }
                        selectionBar.placeAt(box.x, box.y, box.width, box.height, pageArea)
                    }
                    onSelectionCleared: if (reader.selection) reader.selection = null
                    onHighlightClicked: (id, box) => {
                        const a = annotations.get(reader.book.id, id)
                        if (!a.id)
                            return
                        reader.selection = { page: parseInt(a.location), text: a.text || "", paths: [], existing: a }
                        selectionBar.placeAt(box.x, box.y, box.width, box.height, pageArea)
                    }
                    onCurrentPageChanged: if (reader.selection && reader.selection.page !== currentPage) reader.selection = null
                }

                SelectionBar {
                    id: selectionBar
                    visible: reader.selection !== null
                    existing: reader.selection !== null && reader.selection.existing !== null
                    currentColor: existing ? reader.selection.existing.color : ""
                    onColorPicked: color => reader.highlightSelection(color)
                    onNoteRequested: reader.noteOnSelection()
                    onAiRequested: {
                        reader.askAi(reader.selection)
                        reader.selection = null
                    }
                    onCopyRequested: {
                        annotations.copyToClipboard(reader.selection.text)
                        win.showToast(qsTr("Copied."))
                        reader.clearSelection()
                    }
                    onDeleteRequested: {
                        annotations.remove(reader.book.id, reader.selection.existing.id)
                        reader.selection = null
                        win.showToast(qsTr("Highlight removed. Ctrl+Z brings it back."))
                    }
                }

                AiPanel {
                    id: aiPanel
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    width: Math.min(420, parent.width - 24)
                    z: 5
                    bookTitle: reader.book.title || ""
                    bookId: reader.book.id || ""
                    book: reader.book
                    // The chapter being read, as a starting choice.
                    onPracticeRequested: {
                        let i = outlinePanel.currentIndex
                        while (i >= 0 && outlinePanel.parents[i] >= 0 && !/^chapter\b/i.test(reader.tocEntries[i].label))
                            i = outlinePanel.parents[i]
                        win.openPractice(reader.book, reader.tocEntries, Math.max(0, i))
                    }
                    onSourceChosen: target => reader.goToPage(target)
                    onSaveRequested: (answer, action) => {
                        const id = reader.saveAiAnswer(answer, action, target)
                        // Later answers about this passage join the same highlight.
                        if (id && !target.existing)
                            target = Object.assign({}, target, { existing: annotations.get(reader.book.id, id) })
                    }
                    onCloseRequested: reader.forceActiveFocus()
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 12
                    visible: doc.status === PdfDocument.Error
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("This PDF could not be opened")
                        font.pixelSize: 18
                    }
                }
            }
        }

        // Acrobat-style footer: type a page number to go there.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            visible: win.visibility !== Window.FullScreen
            color: "#17171b"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                ToolbarButton { iconName: "chevron-left"; tip: qsTr("Previous page"); onClicked: view.prev() }
                Label { text: qsTr("Page"); font.pixelSize: 12; opacity: 0.7 }
                CompactField {
                    id: pageField
                    Layout.preferredWidth: 64
                    horizontalAlignment: TextInput.AlignHCenter
                    text: (view.currentPage + 1).toString()
                    validator: IntValidator { bottom: 1; top: Math.max(1, doc.pageCount) }
                    onAccepted: { reader.goToPage(parseInt(text) - 1); reader.forceActiveFocus() }
                    onActiveFocusChanged: if (!activeFocus) text = Qt.binding(() => (view.currentPage + 1).toString())
                }
                Label {
                    text: qsTr("of %1").arg(doc.pageCount.toLocaleString(Qt.locale(), 'f', 0))
                    font.pixelSize: 12
                    opacity: 0.7
                }
                ToolbarButton { iconName: "chevron-right"; tip: qsTr("Next page"); onClicked: view.next() }

                Item { Layout.fillWidth: true }

                Label {
                    text: Math.round(reader.progress * 100).toLocaleString(Qt.locale(), 'f', 0) + "%"
                    font.pixelSize: 12
                    opacity: 0.6
                }
            }
        }
    }

    NoteEditor {
        id: noteEditor
        property string annotationId
        property int page: 0
        onSaved: note => {
            if (annotationId === "") {
                if (note !== "")
                    annotations.add(reader.book.id, { kind: "note", location: String(page), note: note,
                                                      position: doc.pageCount > 1 ? page / (doc.pageCount - 1) : 0,
                                                      chapter: reader.labelFor(page) })
                return
            }
            // An emptied note on a page is no note at all.
            const a = annotations.get(reader.book.id, annotationId)
            if (note === "" && a.kind === "note")
                annotations.remove(reader.book.id, annotationId)
            else
                annotations.update(reader.book.id, annotationId, { note: note })
        }
        onDeleted: {
            if (annotationId !== "")
                annotations.remove(reader.book.id, annotationId)
        }
        onClosed: reader.forceActiveFocus()
    }

    component SectionLabel: Label {
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        opacity: 0.55
        Layout.bottomMargin: -8
    }

    // What can be done here, the first time.
    TipCard {
        key: "reader"
        title: qsTr("Reading here")
        z: 50
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 18
        when: reader.active
        lines: [
            qsTr("Select text to highlight it, write a note, or ask the AI to explain it."),
            qsTr("<b>T</b> shows the outline, <b>B</b> bookmarks the page, <b>?</b> lists every shortcut."),
            qsTr("<b>Ctrl+K</b> asks the AI about the whole book, with page references."),
            qsTr("Right-click a chapter in the outline for a practice test on it.")
        ]
    }
}
