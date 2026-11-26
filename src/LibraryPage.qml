import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

FocusScope {
    id: page

    signal openRequested(string id)

    // The sidebar's "+" next to Collections.
    function newCollection() { collectionDialog.forBook = ""; collectionDialog.open() }

    // True while the user is typing somewhere, so single-key shortcuts stay quiet.
    readonly property bool typing: searchField.activeFocus || collectionDialog.visible || removeDialog.visible

    // Select mode: books picked by id, for acting on many at once.
    property bool selecting: false
    property var picked: ({})
    property int pickedRevision: 0
    property int anchorIndex: -1
    readonly property var pickedIds: { pickedRevision; return Object.keys(picked).filter(id => picked[id]) }
    function isPicked(id) { pickedRevision; return picked[id] === true }
    function setPicked(id, value) {
        picked[id] = value
        pickedRevision++
    }
    function startSelecting() {
        selecting = true
        picked = ({})
        pickedRevision++
    }
    function stopSelecting() {
        selecting = false
        picked = ({})
        pickedRevision++
        anchorIndex = -1
    }
    // A plain click opens a book, or picks it in select mode; Ctrl-click
    // picks without opening; Shift-click picks everything from the last one.
    function cardClicked(index, modifiers) {
        const book = library.books[index]
        if (modifiers & Qt.ShiftModifier && anchorIndex >= 0) {
            if (!selecting)
                startSelecting()
            const from = Math.min(anchorIndex, index), to = Math.max(anchorIndex, index)
            for (let i = from; i <= to; ++i)
                picked[library.books[i].id] = true
            pickedRevision++
            return
        }
        if (modifiers & Qt.ControlModifier && !selecting)
            startSelecting()
        if (selecting) {
            setPicked(book.id, !isPicked(book.id))
            anchorIndex = index
            return
        }
        page.openRequested(book.id)
    }
    function selectAll() {
        for (const b of library.books)
            picked[b.id] = true
        pickedRevision++
    }

    readonly property var shelves: [
        { key: "all", label: qsTr("All books"), icon: "▤" },
        { key: "reading", label: qsTr("Reading"), icon: "◐" },
        { key: "finished", label: qsTr("Finished"), icon: "✓" },
        { key: "epub", label: qsTr("EPUB"), icon: "❡" },
        { key: "pdf", label: qsTr("PDF"), icon: "▧" }
    ]

    function shelfTitle() {
        if (library.shelf.startsWith("collection:"))
            return library.shelf.substring(11)
        for (const s of shelves)
            if (s.key === library.shelf)
                return s.label
        return ""
    }

    function askRemove(book) {
        removeDialog.books = []
        removeDialog.book = book
        removeDialog.open()
    }
    function addPickedTo(name) {
        const added = library.addToCollection(pickedIds, name)
        win.showToast(added > 0 ? qsTr("Added %n book(s) to %1.", "", added).arg(name)
                                : qsTr("Those books are already in %1.").arg(name))
        stopSelecting()
    }

    function openMenu(book, item) {
        bookMenu.book = book
        bookMenu.popup(item)
    }

    Shortcut {
        sequences: ["/", StandardKey.Find]
        enabled: page.visible
        onActivated: searchField.forceActiveFocus()
    }
    Shortcut {
        sequence: "Delete"
        enabled: page.visible && !page.typing && grid.currentIndex >= 0 && grid.count > 0
        onActivated: page.askRemove(library.books[grid.currentIndex])
    }
    Shortcut {
        sequence: "Esc"
        enabled: page.visible && page.selecting && !page.typing
        onActivated: page.stopSelecting()
    }
    Shortcut {
        sequence: "Ctrl+A"
        enabled: page.visible && !page.typing && grid.count > 0
        onActivated: {
            if (!page.selecting)
                page.startSelecting()
            page.selectAll()
        }
    }
    Shortcut {
        sequence: "Ctrl+N"
        enabled: page.visible
        onActivated: { collectionDialog.forBook = ""; collectionDialog.open() }
    }
    Shortcut {
        sequence: "S"
        enabled: page.visible && !page.typing
        onActivated: {
            const order = ["recent", "title", "author", "added"]
            library.sort = order[(order.indexOf(library.sort) + 1) % order.length]
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0


        // Main area: toolbar and grid.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 18
                Layout.bottomMargin: 6
                spacing: 12

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    Layout.maximumWidth: 460
                    placeholderText: qsTr("Search title or author")
                    text: library.search
                    onTextEdited: library.search = text
                    Keys.onEscapePressed: {
                        text = ""
                        library.search = ""
                        grid.forceActiveFocus()
                    }
                    Keys.onDownPressed: grid.forceActiveFocus()
                    Keys.onReturnPressed: grid.forceActiveFocus()
                }

                Item { Layout.fillWidth: true }

                // Which shelf or collection, so the tab works with the
                // sidebar hidden too (both share library.shelf).
                ShelfPicker {
                    Layout.preferredWidth: 200
                }

                ComboBox {
                    id: sortBox
                    Layout.preferredWidth: 190
                    textRole: "label"
                    valueRole: "key"
                    model: [
                        { key: "recent", label: qsTr("Recently read") },
                        { key: "title", label: qsTr("Title") },
                        { key: "author", label: qsTr("Author") },
                        { key: "added", label: qsTr("Date added") }
                    ]
                    currentIndex: Math.max(0, indexOfValue(library.sort))
                    onActivated: library.sort = currentValue
                }

                Button {
                    text: page.selecting ? qsTr("Done") : qsTr("Select")
                    flat: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Pick several books to add to a collection, mark finished, or remove (Ctrl-click, Shift-click, Ctrl+A)")
                    onClicked: page.selecting ? page.stopSelecting() : page.startSelecting()
                }

                AccentButton {
                    text: qsTr("Add books")
                    onClicked: library.pickFiles()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Ctrl+O, or drop files and folders on the window")
                }
            }

            GridView {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 18
                clip: true
                focus: true
                keyNavigationEnabled: true
                model: library.books
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                readonly property int columns: Math.max(1, Math.floor((width - 6) / 172))
                cellWidth: Math.floor((width - 6) / columns)
                cellHeight: 300

                header: Column {
                    width: grid.width - 18
                    spacing: 0
                    readonly property bool showContinue: library.shelf === "all" && library.search === ""
                                                         && library.continueReading.length > 0

                    Label {
                        visible: parent.showContinue
                        text: qsTr("Continue reading")
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        topPadding: 8
                        bottomPadding: 10
                    }
                    ListView {
                        visible: parent.showContinue
                        width: parent.width
                        height: visible ? 128 : 0
                        orientation: ListView.Horizontal
                        spacing: 12
                        clip: true
                        model: library.continueReading
                        boundsBehavior: Flickable.StopAtBounds
                        delegate: ContinueCard {
                            required property var modelData
                            book: modelData
                            onClicked: page.openRequested(modelData.id)
                            onRightClicked: page.openMenu(modelData, this)
                        }
                    }
                    Label {
                        text: page.shelfTitle()
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        topPadding: parent.showContinue ? 26 : 8
                        bottomPadding: 10
                        Label {
                            anchors.left: parent.right
                            anchors.leftMargin: 8
                            anchors.baseline: parent.baseline
                            text: qsTr("%n book(s)", "", grid.count)
                            opacity: 0.5
                            font.pixelSize: 13
                        }
                    }
                }

                delegate: BookCard {
                    required property var modelData
                    required property int index
                    width: grid.cellWidth
                    height: grid.cellHeight
                    book: modelData
                    current: grid.activeFocus && grid.currentIndex === index && !page.selecting
                    selecting: page.selecting
                    picked: page.isPicked(modelData.id)
                    onClicked: modifiers => { grid.currentIndex = index; page.cardClicked(index, modifiers) }
                    onRightClicked: { grid.currentIndex = index; page.openMenu(modelData, this) }
                }

                Keys.onReturnPressed: if (currentIndex >= 0) page.openRequested(library.books[currentIndex].id)
                Keys.onEnterPressed: if (currentIndex >= 0) page.openRequested(library.books[currentIndex].id)
                Keys.onMenuPressed: if (currentIndex >= 0) page.openMenu(library.books[currentIndex], currentItem)
                Keys.onSpacePressed: if (page.selecting && currentIndex >= 0) page.cardClicked(currentIndex, 0)

                // Empty states say what to do next.
                Column {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 40
                    spacing: 10
                    visible: grid.count === 0
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: library.totalCount === 0 ? qsTr("Your shelf is empty")
                              : library.search !== "" ? qsTr("No books match “%1”").arg(library.search)
                              : qsTr("Nothing here yet")
                        font.pixelSize: 20
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: library.totalCount === 0
                        text: qsTr("Press Ctrl+O to add EPUB and PDF books, or drop them here.")
                        opacity: 0.6
                    }
                }
            }

            // What to do with the picked books.
            Rectangle {
                visible: page.selecting
                Layout.fillWidth: true
                implicitHeight: 56
                color: "#17171b"
                border.color: "#26262d"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 8
                    Label {
                        text: page.pickedIds.length === 0 ? qsTr("Click books to pick them")
                              : qsTr("%n selected", "", page.pickedIds.length)
                        font.weight: Font.DemiBold
                    }
                    Button {
                        text: qsTr("Select all")
                        flat: true
                        onClicked: page.selectAll()
                    }
                    Button {
                        visible: page.pickedIds.length > 0
                        text: qsTr("Clear")
                        flat: true
                        onClicked: { page.picked = ({}); page.pickedRevision++ }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        id: addToButton
                        enabled: page.pickedIds.length > 0
                        text: qsTr("Add to collection") + " ▾"
                        onClicked: addToMenu.popup(addToButton, 0, -addToMenu.implicitHeight - 6)
                        Menu {
                            id: addToMenu
                            Material.roundedScale: theme.radius
                            Instantiator {
                                model: library.collections
                                delegate: MenuItem {
                                    required property string modelData
                                    text: modelData
                                    onTriggered: page.addPickedTo(modelData)
                                }
                                onObjectAdded: (index, object) => addToMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => addToMenu.removeItem(object)
                            }
                            MenuSeparator { visible: library.collections.length > 0 }
                            MenuItem {
                                text: qsTr("New collection…")
                                onTriggered: {
                                    collectionDialog.forBook = ""
                                    collectionDialog.forBooks = page.pickedIds
                                    collectionDialog.open()
                                }
                            }
                        }
                    }
                    Button {
                        enabled: page.pickedIds.length > 0
                        text: qsTr("Mark finished")
                        flat: true
                        onClicked: {
                            library.setFinishedMany(page.pickedIds, true)
                            win.showToast(qsTr("Marked %n book(s) finished.", "", page.pickedIds.length))
                        }
                    }
                    Button {
                        enabled: page.pickedIds.length > 0
                        text: qsTr("Remove…")
                        flat: true
                        Material.foreground: "#ff8a80"
                        onClicked: { removeDialog.books = page.pickedIds; removeDialog.book = ({}); removeDialog.open() }
                    }
                }
            }
        }
    }

    Menu {
        id: bookMenu
        property var book: ({})
        Material.roundedScale: theme.radius

        MenuItem {
            text: qsTr("Open")
            onTriggered: page.openRequested(bookMenu.book.id)
        }
        MenuItem {
            text: bookMenu.book.finished ? qsTr("Mark as unread") : qsTr("Mark as finished")
            onTriggered: library.setFinished(bookMenu.book.id, !bookMenu.book.finished)
        }
        MenuSeparator {}
        Menu {
            id: collectionsSubmenu
            title: qsTr("Collections")
            Material.roundedScale: theme.radius
            Instantiator {
                model: library.collections
                delegate: MenuItem {
                    required property string modelData
                    text: modelData
                    checkable: true
                    checked: (bookMenu.book.collections || []).indexOf(modelData) >= 0
                    onTriggered: library.setInCollection(bookMenu.book.id, modelData, checked)
                }
                onObjectAdded: (index, object) => collectionsSubmenu.insertItem(index, object)
                onObjectRemoved: (index, object) => collectionsSubmenu.removeItem(object)
            }
            MenuSeparator { visible: library.collections.length > 0 }
            MenuItem {
                text: qsTr("New collection…")
                onTriggered: { collectionDialog.forBook = bookMenu.book.id; collectionDialog.open() }
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Remove from library…")
            onTriggered: page.askRemove(bookMenu.book)
        }
    }




    Dialog {
        id: collectionDialog
        property string forBook: ""
        property var forBooks: []
        title: qsTr("New collection")
        anchors.centerIn: parent
        modal: true
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel
        Material.roundedScale: theme.radius
        onOpened: { nameField.text = ""; nameField.forceActiveFocus() }
        onAccepted: {
            const name = nameField.text.trim()
            if (forBooks.length > 0) {
                // Picked books go straight into the new collection.
                if (name !== "")
                    page.addPickedTo(name)
                forBooks = []
            } else if (library.createCollection(name) && forBook !== "") {
                library.setInCollection(forBook, name, true)
            }
            grid.forceActiveFocus()
        }
        onRejected: grid.forceActiveFocus()
        TextField {
            id: nameField
            width: parent.width
            placeholderText: qsTr("Name")
            onAccepted: collectionDialog.accept()
        }
    }

    Dialog {
        id: removeDialog
        property var book: ({})
        property var books: []      // several, from select mode
        title: books.length > 0 ? qsTr("Remove %n book(s)?", "", books.length) : qsTr("Remove “%1”?").arg(book.title || "")
        anchors.centerIn: parent
        modal: true
        width: 420
        standardButtons: Dialog.Cancel | Dialog.Ok
        Material.roundedScale: theme.radius
        onAccepted: {
            if (books.length > 0) {
                library.removeMany(books)
                page.stopSelecting()
            } else {
                library.remove(book.id)
            }
            grid.forceActiveFocus()
        }
        onRejected: grid.forceActiveFocus()
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: removeDialog.books.length > 0
                  ? qsTr("They leave your library and their reading positions are forgotten. The files themselves stay where they are.")
                  : qsTr("It leaves your library and its reading position is forgotten. The file itself stays where it is.")
            opacity: 0.75
        }
    }

    // What can be done here, the first time.
    TipCard {
        key: "library"
        title: qsTr("Your library")
        z: 50
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 18
        lines: [
            qsTr("Add books with <b>Add books</b>, or drop files and folders on the window."),
            qsTr("The panel on the left (Ctrl+B) keeps your books, workspaces and study tools beside every tab."),
            qsTr("Right-click a book to put it in a collection; choose a collection from the list at the top."),
            qsTr("<b>⚙ Settings</b> sets up the local AI and voices, and backs up your notes.")
        ]
    }
}
