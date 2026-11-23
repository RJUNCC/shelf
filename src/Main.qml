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
        tabs.append({ bookId: id, title: book.title, format: book.format })
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

    Component.onCompleted: {
        const ids = library.setting("openTabs", [])
        for (const id of ids) {
            const book = library.book(id)
            if (book.id && !book.missing)
                tabs.append({ bookId: id, title: book.title, format: book.format })
        }
    }

    function showToast(message) { toast.show(message) }
    function openAiSettings() { aiSettings.open() }
    function openReview() { reviewPopup.open() }
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
                if (!library.book(tabs.get(i).bookId).id)
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

            TabStrip {
                Layout.fillWidth: true
                visible: win.visibility !== Window.FullScreen
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

            // Every open book keeps its reader, so switching tabs never loses
            // the page or panel you left it on.
            StackLayout {
                id: pages
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: win.currentTab

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
                        readonly property bool current: StackLayout.isCurrentItem
                        sourceComponent: format === "pdf" ? pdfReader : epubReader

                        Component {
                            id: epubReader
                            EpubReader {
                                book: library.book(tabLoader.bookId)
                                active: tabLoader.current
                                onLibraryRequested: win.showTab(0)
                            }
                        }
                        Component {
                            id: pdfReader
                            PdfReader {
                                book: library.book(tabLoader.bookId)
                                active: tabLoader.current
                                onLibraryRequested: win.showTab(0)
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
