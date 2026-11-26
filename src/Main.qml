import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 760
    minimumHeight: 500
    visible: true
    title: currentTab > 0 && currentTab <= tabs.count ? tabs.get(currentTab - 1).title + " — Shelf" : "Shelf"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    // 0 is the library; 1… are the open books, in tab order.
    property int currentTab: 0

    ListModel { id: tabs }

    function tabOf(id) {
        for (let i = 0; i < tabs.count; ++i)
            if (tabs.get(i).bookId === id)
                return i + 1
        return -1
    }

    // A workspace opens in a tab of its own, beside the books; its tab is
    // known by "ws:" and its id.
    function openWorkspace(id, rename) {
        if (!id)
            return
        const key = "ws:" + id
        const existing = tabOf(key)
        if (existing > 0) {
            showTab(existing)
            return
        }
        const w = workspaces.workspace(id)
        if (!w.id)
            return
        tabs.append({ bookId: key, title: w.name, format: "workspace", renameFirst: rename === true })
        saveSession()
        showTab(tabs.count)
    }
    // Opens a workspace at one slide (from the knowledge graph).
    property var pendingSlide: null
    function openWorkspaceAt(id, deckId, slide) {
        pendingSlide = { workspaceId: id, deckId: deckId, slide: slide }
        openWorkspace(id)
    }
    function deleteWorkspace(id) {
        const open = tabOf("ws:" + id)
        if (open > 0)
            closeTab(open)
        workspaces.remove(id)
    }
    Connections {
        target: workspaces
        // Tab titles follow renames.
        function onChanged(id) {
            const open = win.tabOf("ws:" + id)
            if (open > 0)
                tabs.setProperty(open - 1, "title", workspaces.workspace(id).name || "")
        }
        function onMessage(text) { toast.show(text) }
    }

    function openBook(id) {
        const existing = tabOf(id)
        if (existing > 0) {
            showTab(existing)
            return
        }
        const book = library.book(id)
        if (!book.id)
            return
        if (book.missing) {
            toast.show(qsTr("%1 has moved or been deleted.").arg(book.title))
            return
        }
        library.markOpened(id)
        tabs.append({ bookId: id, title: book.title, format: book.format, renameFirst: false })
        saveSession()
        showTab(tabs.count)
    }

    function closeTab(index) {
        if (index < 1 || index > tabs.count)
            return
        tabs.remove(index - 1)
        saveSession()
        // Stay on the tab to the left, like a browser.
        showTab(currentTab >= index ? Math.max(0, currentTab - 1) : currentTab)
    }

    function showTab(index) {
        currentTab = Math.max(0, Math.min(tabs.count, index))
        if (currentTab === 0)
            library.refresh()
        Qt.callLater(() => {
            const item = pages.children[currentTab]
            if (item)
                item.forceActiveFocus()
        })
    }

    function cycleTab(step) {
        showTab((currentTab + step + tabs.count + 1) % (tabs.count + 1))
    }

    // Moves a book tab (1-based, as currentTab), keeping the same tab shown.
    function moveTab(from, to) {
        if (from < 1 || to < 1 || from > tabs.count || to > tabs.count || from === to)
            return
        tabs.move(from - 1, to - 1, 1)
        if (currentTab === from)
            currentTab = to
        else if (from < currentTab && currentTab <= to)
            currentTab -= 1
        else if (to <= currentTab && currentTab < from)
            currentTab += 1
        saveSession()
    }

    // Open tabs come back on the next launch.
    function saveSession() {
        const ids = []
        for (let i = 0; i < tabs.count; ++i)
            ids.push(tabs.get(i).bookId)
        library.setSetting("openTabs", ids)
    }
    // The tab last looked at comes back first.
    onCurrentTabChanged: if (currentTab <= tabs.count) library.setSetting("activeTab", currentTab > 0 ? tabs.get(currentTab - 1).bookId : "library")

    Component.onCompleted: {
        const ids = library.setting("openTabs", [])
        for (const id of ids) {
            if (id.startsWith("ws:")) {
                const w = workspaces.workspace(id.slice(3))
                if (w.id)
                    tabs.append({ bookId: id, title: w.name, format: "workspace", renameFirst: false })
                continue
            }
            const book = library.book(id)
            if (book.id && !book.missing)
                tabs.append({ bookId: id, title: book.title, format: book.format, renameFirst: false })
        }
        const active = library.setting("activeTab", "")
        if (tabs.count > 0 && active !== "library")
            Qt.callLater(() => showTab(Math.max(1, tabOf(active))))
    }

    // The library sidebar beside every tab, shown or hidden.
    property bool sidebarOpen: library.setting("sidebarOpen", true) === true
    function toggleSidebar() {
        sidebarOpen = !sidebarOpen
        library.setSetting("sidebarOpen", sidebarOpen)
    }
    Shortcut { sequence: "Ctrl+B"; context: Qt.ApplicationShortcut; onActivated: win.toggleSidebar() }

    // The library tab, with the whole library in a big view.
    function showLibrary() { showTab(0) }

    function showToast(message) { toast.show(message) }
    function openAiSettings() { aiSettings.open() }
    function openReview() { reviewPopup.open() }
    // An animated lesson about a passage or question (see LessonPopup).
    function explainVisually(input) { lessonPopup.explain(input) }
    // A practice test on slides: choices of [{label, text}], one picked.
    function practiceOnSlides(title, choices, index) { practicePopup.openForSlides(title, choices, index) }
    // A few words in the chosen voice.
    function previewVoice(name) {
        lessonPopup.show({
            title: qsTr("Voice preview"),
            scenes: [{ kind: "points", heading: name,
                       say: qsTr("Hello! This is how I'll sound when I explain your slides. I'll describe each picture as I draw it, and you can slow me down or speed me up at any time."),
                       points: [qsTr("Explains your slides"), qsTr("Describes what it draws"), qsTr("Any speed you like")] }]
        }, "preview:" + voice.current)
    }
    // A built-in lesson that needs no AI, to see and hear what lessons are like.
    function playSample() {
        lessonPopup.show({
            title: qsTr("How a lesson works"),
            scenes: [
                { kind: "steps", heading: qsTr("From slide to lesson"),
                  say: qsTr("Put your slides in a workspace. Link each slide to the sections of your books it comes from. Then press Explain, and the local AI writes a short lesson."),
                  steps: [qsTr("Add slides to a workspace"), qsTr("Link the book sections"), qsTr("Press Explain")] },
                { kind: "diagram", heading: qsTr("Pictures that build"),
                  say: qsTr("Processes become diagrams. Each part appears as it's mentioned. Glucose becomes pyruvate, then enters the Krebs cycle, which feeds the electron transport chain."),
                  mermaid: "flowchart LR\n  A[\"" + qsTr("Glucose") + "\"] --> B[\"" + qsTr("Pyruvate") + "\"]\n  B --> C[\"" + qsTr("Krebs cycle") + "\"]\n  C --> D[\"" + qsTr("Electron transport") + "\"]" },
                { kind: "formula", heading: qsTr("Formulas, typeset"),
                  say: qsTr("Equations are drawn properly, not as plain text. Here, distance fallen is one half g t squared. Each symbol is explained."),
                  latex: "d = \\tfrac{1}{2}\\, g\\, t^2",
                  terms: [{ symbol: "d", meaning: qsTr("distance fallen") }, { symbol: "g", meaning: qsTr("gravity, 9.81 m/s²") },
                          { symbol: "t", meaning: qsTr("time since letting go") }] },
                { kind: "graph", heading: qsTr("Graphs that draw themselves"),
                  say: qsTr("When one thing depends on another, you see the curve. After three seconds, it has fallen about forty-four metres."),
                  formula: "0.5*9.81*t^2", variable: "t", from: 0, to: 3, xLabel: qsTr("time (s)"), yLabel: qsTr("distance (m)") }
            ]
        }, "sample")
    }
    // The voice for one sentence of a lesson playing in `player`.
    function speakFor(player, key, scene, sentence, text, now) {
        if (typeof voice === "undefined" || !voice.ready) {
            if (now)
                player.voiceReady(scene, sentence, "")
            return
        }
        voice.say(key, scene, sentence, text, now)
    }
    function openSearch() { searchPopup.open() }
    function openGraph(bookId) { graphPopup.openFor(bookId) }
    function openAbout() { aboutPopup.open() }
    function openPractice(book, outline, index) { practicePopup.openFor(book, outline, index) }
    // Opens a book at a place (a PDF page, or an EPUB section): the reader
    // goes there once it has loaded, or at once if it already has.
    property var pendingJump: null
    function openBookAt(id, target) {
        pendingJump = { bookId: id, target: target }
        openBook(id)
    }
    function openLearn() { learnCourse.open() }
    // How an AI answer reads in a note: marked as AI, with what was asked.
    function aiNoteBlock(answer, action) {
        const label = action === "summarize" ? qsTr("AI summary")
                    : action === "quiz" ? qsTr("AI flashcards")
                    : action === "ask" ? qsTr("AI answer")
                    : qsTr("AI explanation")
        return "✦ " + label + "\n\n" + answer.trim()
    }

    // Says what an undo or redo of a highlight, note, or bookmark did.
    function announceUndo(result) {
        if (!result || !result.action)
            return
        const gone = (result.action === "add") === result.undone
        const messages = {
            highlight: gone ? qsTr("Highlight removed") : qsTr("Highlight restored"),
            note: gone ? qsTr("Note removed") : qsTr("Note restored"),
            bookmark: gone ? qsTr("Bookmark removed") : qsTr("Bookmark restored")
        }
        if (result.action === "update")
            toast.show(result.undone ? qsTr("Change undone") : qsTr("Change redone"))
        else
            toast.show(messages[result.kind] || (result.undone ? qsTr("Undone") : qsTr("Redone")))
    }

    function toggleFullScreen() {
        visibility = visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    // A finished text version joins the library and opens; the PDF
    // remembers it, so its View menu can open it again.
    Connections {
        target: textVersions
        function onFinished(bookId, path) {
            const id = library.addFile(path)
            if (id === "")
                return
            library.setSetting("textVersion:" + bookId, id)
            win.showToast(qsTr("The text version is ready."))
            win.openBook(id)
        }
    }

    Connections {
        target: library
        function onOpenRequested(id) { win.openBook(id) }
        function onMessageChanged() {
            if (library.message !== "")
                toast.show(library.message)
        }
        // A book removed from the library closes its tab.
        function onViewChanged() {
            for (let i = tabs.count - 1; i >= 0; --i)
                if (!tabs.get(i).bookId.startsWith("ws:") && !library.book(tabs.get(i).bookId).id)
                    win.closeTab(i + 1)
        }
    }

    Shortcut { sequence: "Ctrl+Shift+F"; context: Qt.ApplicationShortcut; onActivated: win.openSearch() }
    Shortcut {
        sequence: "?"
        context: Qt.ApplicationShortcut
        onActivated: win.helpVisible = !win.helpVisible
    }
    Shortcut {
        sequence: "Q"
        context: Qt.ApplicationShortcut
        enabled: win.currentTab === 0 && !libraryPage.typing
        onActivated: Qt.quit()
    }
    Shortcut {
        sequences: ["F11"]
        context: Qt.ApplicationShortcut
        onActivated: win.toggleFullScreen()
    }
    Shortcut {
        sequences: [StandardKey.Open]
        context: Qt.ApplicationShortcut
        onActivated: library.pickFiles()
    }
    Shortcut {
        sequences: ["Ctrl+Tab", "Ctrl+PgDown"]
        context: Qt.ApplicationShortcut
        onActivated: win.cycleTab(1)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Tab", "Ctrl+Backtab", "Ctrl+PgUp"]
        context: Qt.ApplicationShortcut
        onActivated: win.cycleTab(-1)
    }
    Shortcut {
        sequences: [StandardKey.Close]
        context: Qt.ApplicationShortcut
        enabled: win.currentTab > 0
        onActivated: win.closeTab(win.currentTab)
    }
    Instantiator {
        model: 9
        delegate: Shortcut {
            required property int index
            sequence: "Ctrl+" + (index + 1)
            context: Qt.ApplicationShortcut
            // Ctrl+1 is the library, Ctrl+9 the last tab, like a browser.
            onActivated: win.showTab(index === 8 ? tabs.count : index)
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                spacing: 0
                visible: win.visibility !== Window.FullScreen
            // Shows or hides the library sidebar beside every tab (Ctrl+B).
            Rectangle {
                Layout.preferredHeight: 40
                implicitWidth: 40
                color: "#0b0b0d"
                ToolButton {
                    anchors.centerIn: parent
                    implicitWidth: 34
                    implicitHeight: 32
                    icon.source: "image://icon/sidebar?color=" + encodeURIComponent(win.sidebarOpen ? theme.accent : "#c8c8ce")
                    ToolTip.visible: hovered
                    ToolTip.text: win.sidebarOpen ? qsTr("Hide the library (Ctrl+B)") : qsTr("Show the library (Ctrl+B)")
                    onClicked: win.toggleSidebar()
                }
            }
            TabStrip {
                Layout.fillWidth: true
                model: tabs
                currentIndex: win.currentTab
                onActivated: index => win.showTab(index)
                onCloseRequested: index => win.closeTab(index)
                onCloseOthersRequested: index => {
                    for (let i = tabs.count; i >= 1; --i)
                        if (i !== index)
                            win.closeTab(i)
                }
                onCloseRightRequested: index => {
                    for (let i = tabs.count; i > index; --i)
                        win.closeTab(i)
                }
                onMoveRequested: (from, to) => win.moveTab(from, to)
            }
            }

            // Every open book keeps its reader, so switching tabs never loses
            // the page or panel you left it on.
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
            // The library panel slides open and shut.
            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: win.sidebarOpen && win.visibility !== Window.FullScreen ? 270 : 0
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                visible: Layout.preferredWidth > 0.5
                clip: true
                LibrarySidebar {
                    id: sidebar
                    width: 270
                    height: parent.height
                    anchors.right: parent.right
                }
            }
            Rectangle {
                visible: win.sidebarOpen && win.visibility !== Window.FullScreen
                Layout.fillHeight: true
                implicitWidth: 1
                color: "#232329"
            }
            StackLayout {
                id: pages
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: win.currentTab
                // A new tab eases in rather than snapping.
                onCurrentIndexChanged: tabFade.restart()
                ParallelAnimation {
                    id: tabFade
                    NumberAnimation { target: pages; property: "opacity"; from: 0.35; to: 1; duration: 180; easing.type: Easing.OutCubic }
                }

                LibraryPage {
                    id: libraryPage
                    onOpenRequested: id => win.openBook(id)
                }

                Repeater {
                    model: tabs
                    delegate: Loader {
                        id: tabLoader
                        required property string bookId
                        required property string format
                        required property bool renameFirst
                        readonly property bool current: StackLayout.isCurrentItem
                        sourceComponent: format === "workspace" ? workspacePage : format === "pdf" ? pdfReader : epubReader

                        Component {
                            id: workspacePage
                            WorkspacePage {
                                workspaceId: tabLoader.bookId.slice(3)
                                active: tabLoader.current
                                renameFirst: tabLoader.renameFirst
                            }
                        }
                        Component {
                            id: epubReader
                            EpubReader {
                                book: library.book(tabLoader.bookId)
                                active: tabLoader.current
                                onLibraryRequested: win.showLibrary()
                            }
                        }
                        Component {
                            id: pdfReader
                            PdfReader {
                                book: library.book(tabLoader.bookId)
                                active: tabLoader.current
                                onLibraryRequested: win.showLibrary()
                            }
                        }
                    }
                }
            }
            }
        }

        // Drop books or folders anywhere to add them.
        DropArea {
            anchors.fill: parent
            keys: ["text/uri-list"]
            onEntered: drag => dropHint.visible = true
            onExited: dropHint.visible = false
            onDropped: drop => {
                dropHint.visible = false
                const tab = win.currentTab > 0 ? tabs.get(win.currentTab - 1) : null
                if (tab && tab.format === "workspace") {
                    workspaces.addDecks(tab.bookId.slice(3), Array.from(drop.urls, u => u.toString()))
                    return
                }
                const added = library.addUrls(drop.urls)
                if (added > 0)
                    toast.show(qsTr("Added %n book(s).", "", added))
            }
        }

        Rectangle {
            id: dropHint
            anchors.fill: parent
            visible: false
            color: Qt.rgba(0, 0, 0, 0.6)
            border.color: theme.accent
            border.width: 2
            Label {
                anchors.centerIn: parent
                text: qsTr("Drop books to add them")
                font.pixelSize: 22
                color: "white"
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onDismissed: win.helpVisible = false
        }

        AiSettings { id: aiSettings }
        LearnCourse { id: learnCourse }
        ReviewPopup { id: reviewPopup }
        LessonPopup { id: lessonPopup }
        SearchPopup { id: searchPopup }
        GraphPopup { id: graphPopup }
        AboutPopup { id: aboutPopup }
        Connections {
            target: updater
            function onFound(version) { win.showToast(qsTr("Shelf %1 is available: see About Shelf in the library.").arg(version)) }
        }
        PracticePopup { id: practicePopup }

        Toast {
            id: toast
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            onDismissed: library.clearMessage()
        }
    }
}
