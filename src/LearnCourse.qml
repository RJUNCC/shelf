import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A five-minute course on Shelf: short lessons, each with a way to try it
// at once, using a practice book so the user's own books stay untouched.
Popup {
    id: course
    anchors.centerIn: Overlay.overlay
    width: Math.min(820, (parent ? parent.width : 860) - 40)
    height: Math.min(580, (parent ? parent.height : 620) - 40)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#18181d"; radius: theme.radius; border.color: "#2c2c33" }

    property int current: 0
    property var done: library.setting("learnDone", [])
    function isDone(key) { return done.indexOf(key) >= 0 }
    function setDone(key, value) {
        const next = done.filter(k => k !== key)
        if (value)
            next.push(key)
        done = next
        library.setSetting("learnDone", next)
    }
    onOpened: library.setSetting("learnSeen", true)

    // What "Try it" does for a lesson.
    function act(action) {
        switch (action) {
        case "practice": {
            const id = library.addPracticeBook()
            if (id !== "") {
                close()
                win.openBook(id)
            }
            break
        }
        case "ai":
            close()
            win.openAiSettings()
            break
        case "shortcuts":
            close()
            win.helpVisible = true
            break
        }
    }

    readonly property var lessons: [
        { key: "library", title: qsTr("Your library"), minutes: 1, action: "practice", actionLabel: qsTr("Add the practice book"),
          steps: [qsTr("Add books with <b>Ctrl+O</b>, or drop files or whole folders onto the window."),
                  qsTr("Search by title or author with <b>/</b>; press <b>S</b> to change the order."),
                  qsTr("Group books into collections from a book's right-click menu."),
                  qsTr("<b>Continue reading</b> at the top brings back the books you have open.")] },
        { key: "reading", title: qsTr("Reading"), minutes: 1, action: "practice", actionLabel: qsTr("Open the practice book"),
          steps: [qsTr("Turn pages with the arrow keys, <b>Space</b>, or the mouse wheel."),
                  qsTr("<b>T</b> shows the outline; click any chapter to go there."),
                  qsTr("Each book opens in its own tab: <b>Ctrl+Tab</b> moves between them, <b>Ctrl+W</b> closes one."),
                  qsTr("In PDFs, <b>View</b> chooses scrolling or turning pages and the scroll speed. In EPUBs, <b>Appearance</b> sets the colours, font, and text size.")] },
        { key: "notes", title: qsTr("Highlights and notes"), minutes: 1, action: "practice", actionLabel: qsTr("Practise in the book"),
          steps: [qsTr("Drag to select text, double-click for a word, triple-click for a sentence."),
                  qsTr("Pick a colour to highlight it, or <b>Note</b> to write about it. <b>B</b> bookmarks the page."),
                  qsTr("Removed something by mistake? <b>Ctrl+Z</b> brings it back."),
                  qsTr("The <b>Notes</b> tab gathers everything by chapter: fold chapters away, search, or export to Markdown.")] },
        { key: "ai", title: qsTr("Ask AI"), minutes: 1, action: "ai", actionLabel: qsTr("Set up local AI"),
          steps: [qsTr("Select a passage and press <b>Ask AI</b>: <i>Explain</i>, <i>Summarize</i>, <i>Quiz me</i>, or ask your own question."),
                  qsTr("<b>Save to notes</b> keeps the answer with the passage, marked as written by AI."),
                  qsTr("It runs on this computer through Ollama, so nothing leaves it. <b>Local AI</b> in the sidebar installs a model and checks it works.")] },
        { key: "study", title: qsTr("Studying"), minutes: 1, action: "", actionLabel: "",
          steps: [qsTr("<b>Ctrl+K</b> asks the whole book a question; the answer cites the pages it comes from."),
                  qsTr("Right-click a chapter in the outline for a <b>practice test</b> written from that chapter."),
                  qsTr("Choose <b>Hands-on</b> and Shelf picks an exercise that fits the chapter: code that is run against tests, a Markov chain, a molecule to draw, or a formula to explore with sliders."),
                  qsTr("<b>Knowledge graph</b> in the library shows how your highlights and notes connect. Type [[a concept]] in a note to link it to others."),
                  qsTr("<i>Quiz me</i> answers and missed test questions become flashcards: <b>Review</b> in the library brings them back when they are due."),
                  qsTr("<b>Ctrl+Shift+F</b> searches inside every book at once.")] },
        { key: "outline", title: qsTr("Outlines for any PDF"), minutes: 1, action: "", actionLabel: "",
          steps: [qsTr("A PDF without an outline, such as a scanned book, can have one built: open its <b>Outline</b> tab and press <b>Build outline</b>."),
                  qsTr("Chapters, sections, and subsections appear as they are found; keep reading meanwhile."),
                  qsTr("<b>Rebuild</b>, under the outline, starts again, for example after choosing another model.")] },
        { key: "scans", title: qsTr("Scanned books"), minutes: 1, action: "", actionLabel: "",
          steps: [qsTr("<b>View → Clean up scanned pages</b> turns yellowed or gray paper white and the text darker."),
                  qsTr("<b>Ctrl+F</b> finds words; <b>Ctrl+G</b> jumps to a page number."),
                  qsTr("Text on a scan can be selected, highlighted, and sent to the AI like any other, if the scan has a text layer.")] },
        { key: "keys", title: qsTr("Shortcuts"), minutes: 1, action: "shortcuts", actionLabel: qsTr("Show all shortcuts"),
          steps: [qsTr("Press <b>?</b> anywhere to see every shortcut."),
                  qsTr("<b>F11</b> reads full screen; <b>Esc</b> steps back, closing a panel or returning to the library."),
                  qsTr("<b>Ctrl+1</b> is the library and <b>Ctrl+2</b>… your tabs, in order.")] }
    ]
    readonly property var lesson: lessons[current]
    readonly property int doneCount: lessons.filter(l => done.indexOf(l.key) >= 0).length

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // The lessons, with progress.
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 250
            color: "#131316"
            radius: theme.radius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 6

                Label {
                    text: qsTr("Learn Shelf")
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Label {
                    text: qsTr("%1 of %2 done").arg(course.doneCount.toLocaleString(Qt.locale(), 'f', 0))
                                                .arg(course.lessons.length.toLocaleString(Qt.locale(), 'f', 0))
                    font.pixelSize: 12
                    opacity: 0.6
                }
                ProgressBar {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 10
                    value: course.doneCount / course.lessons.length
                }

                Repeater {
                    model: course.lessons
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: 38
                        radius: Math.min(6, theme.radius)
                        color: course.current === index ? Qt.alpha(theme.accent, 0.16)
                             : lessonMouse.containsMouse ? "#1d1d22" : "transparent"
                        MouseArea {
                            id: lessonMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: course.current = index
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10
                            Rectangle {
                                width: 18
                                height: 18
                                radius: 9
                                color: course.isDone(modelData.key) ? theme.accent : "transparent"
                                border.color: course.isDone(modelData.key) ? theme.accent : "#55555e"
                                border.width: 2
                                Label {
                                    anchors.centerIn: parent
                                    visible: course.isDone(modelData.key)
                                    text: "✓"
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                    color: "#18181d"
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.title
                                font.pixelSize: 13
                                color: course.current === index ? theme.accent : "#e2e2e6"
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // The lesson.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 28
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: course.lesson.title
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }
                ToolbarButton {
                    iconName: "close"
                    tip: qsTr("Close (Esc)")
                    onClicked: course.close()
                }
            }
            Label {
                Layout.topMargin: -8
                text: qsTr("Lesson %1 of %2 · about a minute").arg(course.current + 1).arg(course.lessons.length)
                font.pixelSize: 12
                opacity: 0.55
            }

            Repeater {
                model: course.lesson.steps
                delegate: RowLayout {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 12
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        width: 24
                        height: 24
                        radius: 12
                        color: "#24242a"
                        Label {
                            anchors.centerIn: parent
                            text: (index + 1).toLocaleString(Qt.locale(), 'f', 0)
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData
                        textFormat: Text.StyledText
                        wrapMode: Text.WordWrap
                        font.pixelSize: 14
                        lineHeight: 1.2
                    }
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                AccentButton {
                    visible: course.lesson.action !== ""
                    text: course.lesson.actionLabel
                    onClicked: course.act(course.lesson.action)
                }
                Button {
                    text: course.isDone(course.lesson.key) ? qsTr("Done ✓") : qsTr("Mark as done")
                    flat: true
                    onClicked: course.setDone(course.lesson.key, !course.isDone(course.lesson.key))
                }
                Item { Layout.fillWidth: true }
                Button {
                    visible: course.current > 0
                    text: qsTr("Back")
                    flat: true
                    onClicked: course.current--
                }
                Button {
                    text: course.current < course.lessons.length - 1 ? qsTr("Next") : qsTr("Finish")
                    onClicked: {
                        course.setDone(course.lesson.key, true)
                        if (course.current < course.lessons.length - 1)
                            course.current++
                        else
                            course.close()
                    }
                }
            }
        }
    }
}
