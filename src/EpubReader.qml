import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtWebEngine
import QtWebChannel
import Shelf
import "numbering.js" as Numbering

// Reads an EPUB with foliate-js in a web view (see web/reader.js), with the
// outline docked on the left and a progress slider along the bottom.
FocusScope {
    id: reader
    property var book: ({})
    property bool active: false
    signal libraryRequested()
    // Menus belong to the tab on screen; switching tabs closes them.
    onActiveChanged: if (!active) styleMenu.close()

    property string chapter: ""
    property real fraction: book.progress || 0
    property int sectionMinutes: -1
    property var sectionStarts: []
    property bool loaded: false
    property string error: ""
    property var tocEntries: []
    // The chapter on screen as the outline numbers it.
    readonly property string numberedChapter: {
        for (const e of tocEntries)
            if (e.title === chapter || e.label === chapter)
                return e.label
        return chapter
    }
    property string pageStart: ""       // CFI of the page on screen, for bookmarks
    property string pageBookmark: ""    // the bookmark on this page, if any
    // The passage selected, or the highlight clicked, that the bar acts on.
    property var selection: null        // { cfi, text, existing: annotation or null }

    property string readingTheme: library.setting("epubTheme", "night")
    property int fontScale: library.setting("epubFontScale", 100)
    property string flow: library.setting("epubFlow", "paginated")
    property string fontChoice: library.setting("epubFont", "original")
    property int columns: library.setting("epubColumns", 2)
    property string customBg: library.setting("epubCustomBg", "#2b2b30")
    // Open by default when there is room for it beside the page.
    property bool outlineOpen: library.setting("outlineOpen", win.width > 1100)

    readonly property var themes: [
        { key: "paper", label: qsTr("White"), bg: "#fbfaf6", fg: "#1f1f1f" },
        { key: "sepia", label: qsTr("Sepia"), bg: "#f4ecd8", fg: "#4b3a26" },
        { key: "green", label: qsTr("Green"), bg: "#d9ead3", fg: "#1f2e1c" },
        { key: "night", label: qsTr("Night"), bg: "#16161a", fg: "#d8d6d0" },
        { key: "black", label: qsTr("Black"), bg: "#000000", fg: "#c9c9c9" }
    ]
    function themeInfo(key) {
        if (key === "custom") {
            const c = Qt.color(customBg)
            const dark = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b < 0.45
            return { key: "custom", bg: customBg, fg: dark ? "#dcdcdc" : "#1f1f1f" }
        }
        for (const t of themes)
            if (t.key === key)
                return t
        return themes[3]
    }
    function setCustomBg(c) {
        customBg = c
        readingTheme = "custom"
        library.setSetting("epubCustomBg", c)
        library.setSetting("epubTheme", "custom")
        call("setCustomTheme(" + JSON.stringify(c) + ")")
    }

    // Annotations: the page script draws highlights and checks bookmarks;
    // the store keeps them.
    function pushAnnotations() {
        const all = annotations.list(book.id)
        const highlights = all.filter(a => a.kind === "highlight")
            .map(a => ({ location: a.location, color: a.color, hasNote: !!(a.note && a.note.length) }))
        call("setAnnotations(" + JSON.stringify(highlights) + ")")
        call("setBookmarks(" + JSON.stringify(all.filter(a => a.kind === "bookmark").map(a => a.location)) + ")")
    }
    function annotationAt(cfi) {
        for (const a of annotations.list(book.id))
            if (a.location === cfi)
                return a
        return null
    }
    function highlightSelection(color) {
        if (!selection)
            return
        if (selection.existing) {
            annotations.update(book.id, selection.existing.id, { color: color })
        } else {
            annotations.add(book.id, { kind: "highlight", location: selection.cfi, text: selection.text,
                                       color: color, position: fraction, chapter: chapter })
            call("clearSelection()")
        }
        selection = null
    }
    function noteOnSelection() {
        if (!selection)
            return
        let target = selection.existing
        if (!target) {
            const id = annotations.add(book.id, { kind: "highlight", location: selection.cfi, text: selection.text,
                                                  color: "yellow", position: fraction, chapter: chapter })
            call("clearSelection()")
            target = annotations.get(book.id, id)
        }
        selection = null
        editAnnotation(target)
    }
    function editAnnotation(annotation) {
        noteEditor.annotationId = annotation.id
        noteEditor.heading = annotation.kind === "bookmark" ? qsTr("Bookmark note") : qsTr("Note")
        noteEditor.canDelete = true
        noteEditor.openWith(annotation.text || "", annotation.note || "")
    }
    function undo() { win.announceUndo(annotations.undo(book.id)) }
    function redo() { win.announceUndo(annotations.redo(book.id)) }
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
            id = annotations.add(book.id, { kind: "highlight", location: target.cfi, text: target.text, color: "yellow",
                                            position: fraction, chapter: chapter, note: block })
            call("clearSelection()")
        }
        win.showToast(qsTr("Saved to notes."))
        return id
    }
    function toggleBookmark() {
        if (pageBookmark !== "") {
            const existing = annotationAt(pageBookmark)
            if (existing) {
                annotations.remove(book.id, existing.id)
                win.showToast(qsTr("Bookmark removed. Ctrl+Z brings it back."))
            }
        } else if (pageStart !== "") {
            annotations.add(book.id, { kind: "bookmark", location: pageStart, position: fraction, chapter: chapter })
        }
    }

    Connections {
        target: annotations
        function onChanged(bookId) {
            if (bookId === reader.book.id && reader.loaded)
                reader.pushAnnotations()
        }
    }
    readonly property var currentTheme: themeInfo(readingTheme)

    function call(script) { web.runJavaScript("window.shelf && window.shelf." + script) }
    function setFontScale(value) {
        fontScale = Math.max(60, Math.min(220, value))
        library.setSetting("epubFontScale", fontScale)
        call("setFontScale(" + fontScale + ")")
    }
    function setReadingTheme(name) {
        readingTheme = name
        library.setSetting("epubTheme", name)
        call("setTheme(" + JSON.stringify(name) + ")")
    }
    function setFlow(value) {
        flow = value
        library.setSetting("epubFlow", value)
        call("setFlow(" + JSON.stringify(value) + ")")
    }
    function setFontChoice(value) {
        fontChoice = value
        library.setSetting("epubFont", value)
        call("setFont(" + JSON.stringify(value) + ")")
    }
    function setColumns(value) {
        columns = value
        library.setSetting("epubColumns", value)
        call("setColumns(" + value + ")")
    }
    function toggleOutline() {
        outlineOpen = !outlineOpen
        library.setSetting("outlineOpen", outlineOpen)
    }

    // Keys stay with QML (the page hands focus back after each click), so
    // these work whether or not the page was clicked. Only the visible tab
    // listens.
    // A search result or citation opened this book at a place.
    function tryJump() {
        const j = win.pendingJump
        if (!j || j.bookId !== reader.book.id || !reader.loaded)
            return
        win.pendingJump = null
        reader.call("goTo(" + JSON.stringify(j.target) + ")")
    }
    Connections {
        target: win
        function onPendingJumpChanged() { reader.tryJump() }
    }
    onLoadedChanged: tryJump()
    Shortcut { sequence: "Ctrl+K"; enabled: reader.active; onActivated: aiPanel.bookMode ? aiPanel.close() : aiPanel.open({ book: true }) }
    Shortcut { sequences: ["Left", "PgUp", "Shift+Space"]; enabled: reader.active && !aiPanel.typing; onActivated: reader.call("goLeft()") }
    Shortcut { sequences: ["Right", "PgDown", "Space"]; enabled: reader.active && !aiPanel.typing; onActivated: reader.call("goRight()") }
    Shortcut { sequence: "Up"; enabled: reader.active && !aiPanel.typing && reader.flow === "paginated"; onActivated: reader.call("prev()") }
    Shortcut { sequence: "Down"; enabled: reader.active && !aiPanel.typing && reader.flow === "paginated"; onActivated: reader.call("next()") }
    Shortcut { sequence: "T"; enabled: reader.active && !aiPanel.typing; onActivated: reader.toggleOutline() }
    Shortcut { sequence: "B"; enabled: reader.active && !aiPanel.typing; onActivated: reader.toggleBookmark() }
    Shortcut { sequence: "N"; enabled: reader.active && !aiPanel.typing; onActivated: { outlinePanel.view = "notes"; if (!reader.outlineOpen) reader.toggleOutline() } }
    Shortcut { sequences: [StandardKey.Undo]; enabled: reader.active && !aiPanel.typing && !noteEditor.opened; onActivated: reader.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; enabled: reader.active && !aiPanel.typing && !noteEditor.opened; onActivated: reader.redo() }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: reader.active && !aiPanel.typing && reader.selection !== null
        onActivated: {
            annotations.copyToClipboard(reader.selection.text)
            win.showToast(qsTr("Copied."))
        }
    }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; enabled: reader.active && !aiPanel.typing; onActivated: reader.setFontScale(reader.fontScale + 10) }
    Shortcut { sequences: [StandardKey.ZoomOut]; enabled: reader.active && !aiPanel.typing; onActivated: reader.setFontScale(reader.fontScale - 10) }
    Shortcut { sequence: "Ctrl+0"; enabled: reader.active && !aiPanel.typing; onActivated: reader.setFontScale(100) }
    Shortcut {
        sequence: "Esc"
        enabled: reader.active
        onActivated: {
            if (aiPanel.shown)
                aiPanel.close()
            else if (styleMenu.visible)
                styleMenu.close()
            else if (reader.selection) {
                reader.selection = null
                reader.call("clearSelection()")
            } else
                reader.libraryRequested()
        }
    }

    ReaderBridge {
        id: bridge
        WebChannel.id: "bridge"
        onReady: tocJson => {
            reader.loaded = true
            reader.pushAnnotations()
            reader.tocEntries = Numbering.labelled(JSON.parse(tocJson).map(e => ({ label: e.label, depth: e.depth, target: e.href })))
        }
        onRelocated: (cfi, fraction, chapter, details) => {
            reader.fraction = fraction
            reader.chapter = chapter
            const d = JSON.parse(details)
            reader.sectionMinutes = d.sectionMinutes
            reader.pageStart = d.pageStart || ""
            reader.pageBookmark = d.bookmark || ""
            reader.selection = null
            if (d.sections && d.sections.length !== reader.sectionStarts.length)
                reader.sectionStarts = d.sections
            library.saveProgress(reader.book.id, cfi, fraction)
        }
        onFailed: message => reader.error = message
        onSelected: (cfi, text, x, y, w, h, context) => {
            reader.selection = { cfi: cfi, text: text, context: context, existing: null }
            selectionBar.placeAt(x, y, w, h, web)
        }
        // Clicking the page gives it the keyboard; take it back so arrows,
        // T, B, and the rest keep working.
        onPointerReleased: if (!noteEditor.opened) {
            // Inside this FocusScope, the web view would get it straight back.
            web.focus = false
            reader.forceActiveFocus()
        }
        onSelectionCleared: if (reader.selection && !reader.selection.existing) reader.selection = null
        onAnnotationClicked: (cfi, x, y, w, h) => {
            const a = reader.annotationAt(cfi)
            if (!a)
                return
            reader.selection = { cfi: cfi, text: a.text || "", existing: a }
            selectionBar.placeAt(x, y, w, h, web)
        }
    }

    WebChannel {
        id: channel
        registeredObjects: [bridge]
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ReaderBar {
            Layout.fillWidth: true
            visible: win.visibility !== Window.FullScreen
            title: reader.book.title || ""
            subtitle: reader.numberedChapter
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
                iconName: reader.pageBookmark !== "" ? "bookmark-filled" : "bookmark"
                checked: reader.pageBookmark !== ""
                tip: reader.pageBookmark !== "" ? qsTr("Remove bookmark (B)") : qsTr("Bookmark this page (B)")
                onClicked: reader.toggleBookmark()
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
            ToolbarButton {
                id: styleButton
                iconName: "text"
                label: qsTr("Appearance")
                showLabel: reader.width > 980
                tip: qsTr("Text size, font, and colors")
                checked: styleMenu.visible
                onClicked: styleMenu.visible ? styleMenu.close() : styleMenu.open()

                Popup {
                    id: styleMenu
                    // Keyboard stays with the reader so T, arrows, and Esc
                    // keep working while the popup is open.
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

                        SectionLabel { text: qsTr("Theme") }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Repeater {
                                model: reader.themes
                                delegate: Column {
                                    required property var modelData
                                    // Equal shares of the row: a tiny preferred
                                    // width that fillWidth grows.
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    spacing: 5
                                    Rectangle {
                                        width: parent.width
                                        height: 44
                                        radius: Math.min(8, theme.radius)
                                        color: modelData.bg
                                        border.width: reader.readingTheme === modelData.key ? 2 : 1
                                        border.color: reader.readingTheme === modelData.key ? theme.accent : "#3a3a42"
                                        Label {
                                            anchors.centerIn: parent
                                            text: "Aa"
                                            font.family: "serif"
                                            font.pixelSize: 16
                                            color: modelData.fg
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: reader.setReadingTheme(modelData.key)
                                        }
                                    }
                                    Label {
                                        width: parent.width
                                        horizontalAlignment: Text.AlignHCenter
                                        text: modelData.label
                                        font.pixelSize: 11
                                        opacity: reader.readingTheme === modelData.key ? 1 : 0.6
                                    }
                                }
                            }
                            // Any color, from the picker below.
                            Column {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                spacing: 5
                                Rectangle {
                                    width: parent.width
                                    height: 44
                                    radius: Math.min(8, theme.radius)
                                    border.width: reader.readingTheme === "custom" ? 2 : 1
                                    border.color: reader.readingTheme === "custom" ? theme.accent : "#3a3a42"
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0.0; color: "#e57373" }
                                        GradientStop { position: 0.33; color: "#fff176" }
                                        GradientStop { position: 0.66; color: "#81c784" }
                                        GradientStop { position: 1.0; color: "#64b5f6" }
                                    }
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 20
                                        height: 20
                                        radius: 10
                                        color: reader.customBg
                                        border.color: "white"
                                        border.width: 2
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: reader.setCustomBg(reader.customBg)
                                    }
                                }
                                Label {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    text: qsTr("Custom")
                                    font.pixelSize: 11
                                    opacity: reader.readingTheme === "custom" ? 1 : 0.6
                                }
                            }
                        }

                        ColorPicker {
                            visible: reader.readingTheme === "custom"
                            Layout.fillWidth: true
                            value: reader.customBg
                            onPicked: c => reader.setCustomBg(picker.hex(c))
                            id: picker
                        }

                        SectionLabel { text: qsTr("Text size") }
                        RowLayout {
                            Layout.fillWidth: true
                            ToolbarButton { iconName: "minus"; tip: qsTr("Smaller (Ctrl −)"); onClicked: reader.setFontScale(reader.fontScale - 10) }
                            Slider {
                                Layout.fillWidth: true
                                from: 60; to: 220; stepSize: 10
                                value: reader.fontScale
                                onMoved: reader.setFontScale(value)
                            }
                            ToolbarButton { iconName: "plus"; tip: qsTr("Larger (Ctrl +)"); onClicked: reader.setFontScale(reader.fontScale + 10) }
                            Label {
                                text: reader.fontScale.toLocaleString(Qt.locale(), 'f', 0) + "%"
                                font.pixelSize: 12
                                Layout.preferredWidth: 40
                                horizontalAlignment: Text.AlignRight
                            }
                        }

                        SectionLabel { text: qsTr("Font") }
                        Segmented {
                            Layout.fillWidth: true
                            options: [
                                { key: "original", label: qsTr("Book") },
                                { key: "serif", label: qsTr("Serif") },
                                { key: "sans", label: qsTr("Sans") }
                            ]
                            value: reader.fontChoice
                            onPicked: key => reader.setFontChoice(key)
                        }

                        SectionLabel { text: qsTr("Layout") }
                        Segmented {
                            Layout.fillWidth: true
                            options: [
                                { key: "2", label: qsTr("Two pages") },
                                { key: "1", label: qsTr("One page") },
                                { key: "scrolled", label: qsTr("Scroll") }
                            ]
                            value: reader.flow === "scrolled" ? "scrolled" : String(reader.columns)
                            onPicked: key => {
                                if (key === "scrolled") {
                                    reader.setFlow("scrolled")
                                } else {
                                    if (reader.flow !== "paginated")
                                        reader.setFlow("paginated")
                                    reader.setColumns(parseInt(key))
                                }
                            }
                        }
                    }
                }
            }
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
                progress: reader.fraction
                entries: reader.tocEntries
                currentLabel: reader.chapter
                onChosen: target => reader.call("goTo(" + JSON.stringify(target) + ")")
                onAnnotationChosen: a => reader.call("goTo(" + JSON.stringify(a.location) + ")")
                onAnnotationEdit: a => reader.editAnnotation(a)
                onPracticeRequested: index => win.openPractice(reader.book, reader.tocEntries, index)
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: reader.currentTheme.bg

                WebEngineView {
                    id: web
                    objectName: "readerWeb"
                    anchors.fill: parent
                    webChannel: channel
                    // Qt WebEngine ignores the mouse entirely when this is false.
                    activeFocusOnPress: true
                    backgroundColor: parent.color
                    url: {
                        const q = [
                            "book=" + encodeURIComponent(library.readerUrl(reader.book.id).toString()),
                            "cfi=" + encodeURIComponent(reader.book.location || ""),
                            "theme=" + reader.readingTheme,
                            "fontScale=" + reader.fontScale,
                            "flow=" + reader.flow,
                            "font=" + reader.fontChoice,
                            "columns=" + reader.columns,
                            "customBg=" + encodeURIComponent(reader.customBg)
                        ]
                        return "shelf://app/reader.html?" + q.join("&")
                    }
                    // The URL only matters at load; later changes go through
                    // call(), so stop the binding after the first evaluation.
                    Component.onCompleted: url = url

                    onContextMenuRequested: request => request.accepted = true
                    onNavigationRequested: request => {
                        // The reader page stays put; links out of the book go
                        // to the browser.
                        if (request.isMainFrame && !request.url.toString().startsWith("shelf://app/")) {
                            request.reject()
                            if (/^https?:/.test(request.url.toString()))
                                Qt.openUrlExternally(request.url)
                        }
                    }
                    onNewWindowRequested: request => {
                        if (/^https?:/.test(request.requestedUrl.toString()))
                            Qt.openUrlExternally(request.requestedUrl)
                    }
                    onRenderProcessTerminated: (status, code) => {
                        if (status !== WebEngineView.NormalTerminationStatus)
                            reader.error = qsTr("The reader stopped unexpectedly (code %1).").arg(code)
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
                    onSourceChosen: target => reader.call("goTo(" + JSON.stringify(target) + ")")
                    onSaveRequested: (answer, action) => {
                        const id = reader.saveAiAnswer(answer, action, target)
                        // Later answers about this passage join the same highlight.
                        if (id && !target.existing)
                            target = Object.assign({}, target, { existing: annotations.get(reader.book.id, id) })
                    }
                    onCloseRequested: reader.forceActiveFocus()
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    running: !reader.loaded && reader.error === ""
                    visible: running
                }

                SelectionBar {
                    id: selectionBar
                    visible: reader.selection !== null
                    existing: reader.selection !== null && reader.selection.existing !== null
                    currentColor: existing ? reader.selection.existing.color : ""
                    onColorPicked: color => reader.highlightSelection(color)
                    onNoteRequested: reader.noteOnSelection()
                    onAiRequested: {
                        aiPanel.open({ text: reader.selection.text, context: reader.selection.context || "",
                                       cfi: reader.selection.cfi, existing: reader.selection.existing })
                        reader.selection = null
                    }
                    onCopyRequested: {
                        annotations.copyToClipboard(reader.selection.text)
                        win.showToast(qsTr("Copied."))
                        reader.selection = null
                        reader.call("clearSelection()")
                    }
                    onDeleteRequested: {
                        annotations.remove(reader.book.id, reader.selection.existing.id)
                        reader.selection = null
                        win.showToast(qsTr("Highlight removed. Ctrl+Z brings it back."))
                    }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 12
                    visible: reader.error !== ""
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("This book could not be opened")
                        font.pixelSize: 18
                        color: reader.currentTheme.fg
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: reader.error
                        opacity: 0.7
                        color: reader.currentTheme.fg
                    }
                }
            }
        }

        // Foliate-style footer: where you are and a slider to go anywhere.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            visible: win.visibility !== Window.FullScreen
            color: "#17171b"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 14

                Label {
                    text: reader.sectionMinutes < 0 ? ""
                          : reader.sectionMinutes === 0 ? qsTr("Less than a minute left in chapter")
                          : qsTr("%n min left in chapter", "", reader.sectionMinutes)
                    font.pixelSize: 12
                    opacity: 0.6
                    Layout.preferredWidth: 200
                    elide: Text.ElideRight
                }

                Slider {
                    id: progressSlider
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: reader.fraction
                    // Jump on release, so dragging does not render every
                    // page on the way.
                    onPressedChanged: if (!pressed) reader.call("goToFraction(" + value + ")")

                    // Chapter starts as ticks along the track.
                    Repeater {
                        model: reader.sectionStarts.length < 200 ? reader.sectionStarts : []
                        delegate: Rectangle {
                            required property real modelData
                            x: progressSlider.leftPadding + modelData * progressSlider.availableWidth - 1
                            y: progressSlider.topPadding + progressSlider.availableHeight / 2 + 6
                            width: 2
                            height: 4
                            color: "#55555e"
                        }
                    }
                }

                Label {
                    text: Math.round(reader.fraction * 100).toLocaleString(Qt.locale(), 'f', 0) + "%"
                    font.pixelSize: 12
                    opacity: 0.6
                    Layout.preferredWidth: 40
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
    }

    NoteEditor {
        id: noteEditor
        property string annotationId
        onSaved: note => annotations.update(reader.book.id, annotationId, { note: note })
        onDeleted: annotations.remove(reader.book.id, annotationId)
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
