import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The library, as a panel beside every tab: the books (searched, and
// filtered by shelf or collection), the workspaces, and the study tools.
// It folds away with Ctrl+B or the button at the start of the tab strip.
Rectangle {
    id: sidebar
    color: "#131316"
    implicitWidth: 270

    readonly property var shelves: [
        { key: "all", label: qsTr("All books") },
        { key: "reading", label: qsTr("Reading") },
        { key: "finished", label: qsTr("Finished") },
        { key: "epub", label: qsTr("EPUB") },
        { key: "pdf", label: qsTr("PDF") }
    ]
    // Shelves, then the user's collections, for the filter above the books.
    readonly property var filters: shelves.concat(library.collections.map(c => ({ key: "collection:" + c, label: "◆ " + c })))
    readonly property bool typing: bookSearch.activeFocus || collectionDialog.visible || removeDialog.visible
    readonly property string openBookId: win.currentTab > 0 && win.currentTab <= tabs.count ? tabs.get(win.currentTab - 1).bookId : ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Label {
                text: "Shelf"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            ToolButton {
                implicitWidth: 34
                implicitHeight: 34
                icon.source: "image://icon/plus?color=" + encodeURIComponent(theme.accent)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Add books (Ctrl+O), or drop files on the window")
                onClicked: library.pickFiles()
            }
        }

        CompactField {
            id: bookSearch
            Layout.fillWidth: true
            hint: qsTr("Search title or author")
            text: library.search
            onTextChanged: if (text !== library.search) library.search = text
            Keys.onEscapePressed: { text = ""; focus = false }
        }

        // Which books: a shelf or a collection.
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            ComboBox {
                id: filterBox
                Layout.fillWidth: true
                implicitHeight: 36
                font.pixelSize: 13
                model: sidebar.filters
                textRole: "label"
                currentIndex: Math.max(0, sidebar.filters.findIndex(f => f.key === library.shelf))
                onActivated: library.shelf = sidebar.filters[currentIndex].key
            }
            ToolButton {
                text: "⋯"
                implicitWidth: 32
                implicitHeight: 32
                onClicked: filterMenu.popup()
            }
        }

        // The books themselves.
        ListView {
            id: bookList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: library.books
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            reuseItems: true
            delegate: Rectangle {
                id: row
                required property var modelData
                width: bookList.width - 8
                height: 54
                radius: Math.min(6, theme.radius)
                readonly property bool open: sidebar.openBookId === modelData.id
                color: open ? Qt.alpha(theme.accent, 0.16) : rowMouse.containsMouse ? "#1d1d22" : "transparent"
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            bookMenu.book = row.modelData
                            bookMenu.popup()
                        } else {
                            win.openBook(row.modelData.id)
                        }
                    }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 10
                    BookCover {
                        book: row.modelData
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 44
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: row.modelData.title || ""
                            elide: Text.ElideRight
                            font.pixelSize: 13
                            font.weight: row.open ? Font.DemiBold : Font.Normal
                            color: row.open ? theme.accent : Material.foreground
                        }
                        Label {
                            Layout.fillWidth: true
                            text: row.modelData.author || row.modelData.format.toUpperCase()
                            elide: Text.ElideRight
                            font.pixelSize: 11
                            opacity: 0.5
                        }
                        Rectangle {
                            visible: (row.modelData.progress || 0) > 0
                            Layout.preferredWidth: parent.width * 0.6
                            Layout.preferredHeight: 2
                            color: "#2c2c33"
                            Rectangle {
                                width: parent.width * Math.min(1, row.modelData.progress || 0)
                                height: parent.height
                                color: theme.accent
                            }
                        }
                    }
                }
            }
            Label {
                visible: bookList.count === 0
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                opacity: 0.5
                font.pixelSize: 12
                text: library.search !== "" ? qsTr("No book matches.")
                      : library.totalCount === 0 ? qsTr("Add books with + or by dropping files on the window.")
                      : qsTr("No books here yet.")
            }
        }

        // Learning workspaces: slides linked to the books.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Label {
                text: qsTr("Workspaces")
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                opacity: 0.55
                Layout.fillWidth: true
            }
            ToolButton {
                text: "+"
                font.pixelSize: 18
                implicitWidth: 30
                implicitHeight: 30
                ToolTip.visible: hovered
                ToolTip.text: qsTr("New workspace for a course's slides")
                onClicked: win.openWorkspace(workspaces.create(qsTr("New workspace")), true)
            }
        }
        Repeater {
            model: workspaces.list
            delegate: SidebarItem {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 32
                icon: "▤"
                text: modelData.name
                count: modelData.slides > 0 ? modelData.slides : -1
                selected: sidebar.openBookId === "ws:" + modelData.id
                onClicked: win.openWorkspace(modelData.id)
                onRightClicked: {
                    workspaceMenu.workspace = modelData
                    workspaceMenu.popup()
                }
            }
        }
        Label {
            visible: workspaces.list.length === 0
            Layout.fillWidth: true
            leftPadding: 4
            wrapMode: Text.WordWrap
            text: qsTr("Put a course's slides beside your books, with diagrams and explanations.")
            opacity: 0.45
            font.pixelSize: 12
        }

        // The study tools and settings, as a row of icons.
        Rectangle { Layout.fillWidth: true; Layout.topMargin: 4; implicitHeight: 1; color: "#232329" }
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            ToolCell { glyph: "⌕"; tip: qsTr("Search all books (Ctrl+Shift+F)"); onClicked: win.openSearch() }
            ToolCell { glyph: "⁂"; tip: qsTr("Knowledge graph"); onClicked: win.openGraph("") }
            ToolCell {
                glyph: "❏"
                tip: cards.dueCount > 0 ? qsTr("Review: %n card(s) due", "", cards.dueCount) : qsTr("Review flashcards")
                dot: cards.dueCount > 0 ? theme.accent : "transparent"
                onClicked: win.openReview()
            }
            ToolCell {
                id: learnCell
                property bool seen: library.setting("learnSeen", false)
                glyph: "?"
                tip: qsTr("Learn Shelf")
                dot: seen ? "transparent" : theme.accent
                onClicked: { seen = true; win.openLearn() }
            }
            ToolCell {
                glyph: "ⓘ"
                tip: qsTr("About Shelf")
                dot: updater.state === "available" || updater.state === "installed" ? theme.accent : "transparent"
                onClicked: win.openAbout()
            }
            ToolCell {
                glyph: "✦"
                tip: qsTr("Local AI and voices")
                dot: ollama.status === "ready" ? "#6fd08c" : ollama.status === "checking" ? "#55555e" : "#f5b347"
                onClicked: win.openAiSettings()
                Component.onCompleted: ollama.refresh()
            }
        }
    }

    // One of the tools along the bottom.
    component ToolCell: ToolButton {
        property string glyph
        property string tip
        property color dot: "transparent"
        Layout.fillWidth: true
        implicitHeight: 38
        text: glyph
        font.pixelSize: 16
        ToolTip.visible: hovered
        ToolTip.text: tip
        ToolTip.delay: 300
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 7
            width: 7; height: 7; radius: 3.5
            color: parent.dot
        }
    }

    Menu {
        id: filterMenu
        Material.roundedScale: theme.radius
        MenuItem {
            text: qsTr("New collection…")
            onTriggered: { collectionDialog.forBook = ""; collectionDialog.open() }
        }
        MenuItem {
            enabled: library.shelf.startsWith("collection:")
            text: qsTr("Delete this collection")
            onTriggered: {
                library.deleteCollection(library.shelf.substring(11))
                library.shelf = "all"
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Sort: %1").arg({ recent: qsTr("Recently read"), title: qsTr("Title"), author: qsTr("Author"), added: qsTr("Date added") }[library.sort] || "")
            onTriggered: {
                const order = ["recent", "title", "author", "added"]
                library.sort = order[(order.indexOf(library.sort) + 1) % order.length]
            }
        }
    }

    Menu {
        id: bookMenu
        property var book: ({})
        Material.roundedScale: theme.radius
        MenuItem {
            text: qsTr("Open")
            onTriggered: win.openBook(bookMenu.book.id)
        }
        Menu {
            title: qsTr("Add to collection")
            Repeater {
                model: library.collections
                delegate: MenuItem {
                    required property string modelData
                    text: modelData
                    checkable: true
                    checked: (bookMenu.book.collections || []).indexOf(modelData) >= 0
                    onTriggered: library.setInCollection(bookMenu.book.id, modelData, checked)
                }
            }
            MenuItem {
                text: qsTr("New collection…")
                onTriggered: { collectionDialog.forBook = bookMenu.book.id; collectionDialog.open() }
            }
        }
        MenuItem {
            text: bookMenu.book.finished ? qsTr("Mark as not finished") : qsTr("Mark as finished")
            onTriggered: library.setFinished(bookMenu.book.id, !bookMenu.book.finished)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Remove from library…")
            onTriggered: { removeDialog.book = bookMenu.book; removeDialog.open() }
        }
    }

    Dialog {
        id: collectionDialog
        property string forBook: ""
        title: qsTr("New collection")
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel
        Material.roundedScale: theme.radius
        onOpened: { nameField.text = ""; nameField.forceActiveFocus() }
        onAccepted: {
            const name = nameField.text.trim()
            if (library.createCollection(name) && forBook !== "")
                library.setInCollection(forBook, name, true)
        }
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
        title: qsTr("Remove “%1”?").arg(book.title || "")
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 420
        standardButtons: Dialog.Cancel | Dialog.Ok
        Material.roundedScale: theme.radius
        onAccepted: library.remove(book.id)
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("It leaves your library and its reading position is forgotten. The file itself stays where it is.")
            opacity: 0.75
        }
    }

    Menu {
        id: collectionMenu
        property string name
        Material.roundedScale: theme.radius
        MenuItem {
            text: qsTr("Delete collection")
            onTriggered: library.deleteCollection(collectionMenu.name)
        }
    }

    Menu {
        id: workspaceMenu
        property var workspace: ({})
        Material.roundedScale: theme.radius
        MenuItem {
            text: qsTr("Open")
            onTriggered: win.openWorkspace(workspaceMenu.workspace.id)
        }
        MenuItem {
            text: qsTr("Delete workspace…")
            onTriggered: { deleteWorkspaceDialog.workspace = workspaceMenu.workspace; deleteWorkspaceDialog.open() }
        }
    }

    Dialog {
        id: deleteWorkspaceDialog
        property var workspace: ({})
        title: qsTr("Delete “%1”?").arg(workspace.name || "")
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 420
        standardButtons: Dialog.Cancel | Dialog.Ok
        Material.roundedScale: theme.radius
        onAccepted: win.deleteWorkspace(workspace.id)
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Its copies of the slides, its links and its explanations are deleted. Your own slide files and books stay where they are.")
            opacity: 0.75
        }
    }
}
