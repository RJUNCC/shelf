import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Shelf

// Asks the local model about a passage: explain, summarize, quiz, or any
// question, with the answer streaming in. "Save to notes" hands the answer
// back to the reader, which keeps it with the highlight.
Rectangle {
    id: panel
    // { text, context, existing } — the passage, the text around it, and
    // the highlight it belongs to, if it is one already.
    property var target: null
    property string bookTitle: ""
    property string bookId: ""
    // The book itself ({id, title, path, format}), for questions about all of it.
    property var book: ({})
    // A question about the whole book rather than a passage.
    readonly property bool bookMode: target !== null && target.book === true
    // The passages found for it, numbered as the answer cites them.
    property var sources: []
    property string pendingQuestion: ""
    signal sourceChosen(var target)
    signal practiceRequested()
    // Typing a question: the reader's page keys must not fire.
    readonly property bool typing: questionField.activeFocus
    // Cards made from this answer are not made twice.
    property bool carded: false
    signal closeRequested()
    signal saveRequested(string answer, string action)

    readonly property bool shown: target !== null
    // Saved answers are not saved twice; a new answer can be.
    property bool saved: false
    readonly property var actions: [
        { key: "explain", label: qsTr("Explain") },
        { key: "summarize", label: qsTr("Summarize") },
        { key: "quiz", label: qsTr("Quiz me") }
    ]
    readonly property string actionLabel: request.action === "summarize" ? qsTr("Summary")
                                        : request.action === "quiz" ? qsTr("Flashcards")
                                        : request.action === "ask" || request.action === "book" ? qsTr("Answer")
                                        : qsTr("Explanation")

    function ask(action, question) {
        if (!target)
            return
        saved = false
        carded = false
        request.run(action, { text: target.text, context: target.context || "", title: bookTitle,
                              question: question || "" })
    }
    function askBook(question) {
        sources = []
        saved = false
        pendingQuestion = question
        request.clear()
        bookSearch.passages({ id: book.id || "", title: book.title || "", path: book.path || "",
                              format: book.format || "" }, question, 6)
    }
    Connections {
        target: bookSearch
        function onPassagesReady(id, question, passages) {
            if (id !== (panel.book.id || "") || question !== panel.pendingQuestion || !panel.bookMode)
                return
            panel.pendingQuestion = ""
            panel.sources = passages
            if (passages.length > 0)
                request.run("book", { question: question, passages: passages, title: panel.bookTitle })
        }
    }
    // Citations "[2]" become links to their passage.
    function linked(answer) {
        if (!bookMode)
            return answer
        return answer.replace(/\[(\d+)\]/g, (m, n) => "[\\[" + n + "\\]](source:" + n + ")")
    }
    function openSource(n) {
        for (const s of sources)
            if (s.n === n) {
                sourceChosen(s.target)
                return
            }
    }

    // A new passage starts fresh.
    function open(newTarget) {
        sources = []
        pendingQuestion = ""
        request.clear()
        questionField.text = ""
        target = newTarget
        if (ollama.status !== "ready")
            ollama.refresh()
        // A question about the book is typed straight away.
        if (newTarget && newTarget.book)
            Qt.callLater(() => questionField.forceActiveFocus())
    }
    function close() {
        request.cancel()
        target = null
        closeRequested()
    }

    visible: shown
    width: 400
    radius: Math.min(10, theme.radius + 2)
    color: "#1b1b20"
    border.color: "#2c2c33"

    AiRequest { id: request }

    // Clicks inside never reach the page beneath.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: "✦ " + (panel.bookMode ? qsTr("Ask this book") : qsTr("Ask AI"))
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            ToolbarButton {
                iconName: "close"
                tip: qsTr("Close (Esc)")
                onClicked: panel.close()
            }
        }

        // The passage, so it is clear what the answer is about.
        Rectangle {
            visible: !panel.bookMode
            Layout.fillWidth: true
            implicitHeight: quote.implicitHeight + 16
            radius: Math.min(6, theme.radius)
            color: "#15151a"
            Rectangle { width: 3; height: parent.height; color: theme.accent; radius: 1 }
            Label {
                id: quote
                anchors.fill: parent
                anchors.margins: 8
                anchors.leftMargin: 14
                text: panel.target && panel.target.text ? panel.target.text : ""
                wrapMode: Text.WordWrap
                maximumLineCount: 4
                elide: Text.ElideRight
                font.italic: true
                font.pixelSize: 12
                opacity: 0.8
            }
        }

        // Not ready: say why, and where to fix it.
        ColumnLayout {
            visible: ollama.status !== "ready" && ollama.status !== "checking"
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: "#f5b347"
                text: ollama.status === "notInstalled" ? qsTr("Ollama is not installed, so local AI is not available yet.")
                    : ollama.status === "notRunning" ? qsTr("Ollama is not running.")
                    : qsTr("Ollama has no model yet.")
            }
            Button {
                text: qsTr("Local AI settings")
                flat: true
                onClicked: win.openAiSettings()
            }
        }

        ColumnLayout {
            visible: ollama.status === "ready"
            Layout.fillWidth: true
            spacing: 8
            Label {
                visible: panel.bookMode
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                opacity: 0.6
                text: qsTr("Shelf finds the passages that answer your question and the AI answers from them, citing each.")
            }
            Button {
                visible: panel.bookMode
                flat: true
                text: qsTr("Practice test on a chapter…")
                onClicked: panel.practiceRequested()
            }
            RowLayout {
                visible: !panel.bookMode
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: panel.actions
                    delegate: Button {
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.label
                        flat: request.action !== modelData.key
                        highlighted: request.action === modelData.key
                        enabled: !request.running
                        onClicked: panel.ask(modelData.key)
                    }
                }
            }
            CompactField {
                id: questionField
                Layout.fillWidth: true
                hint: panel.bookMode ? qsTr("Ask anything about this book…") : qsTr("Ask a question about this…")
                enabled: !request.running && panel.pendingQuestion === ""
                onAccepted: {
                    if (text.trim() === "")
                        return
                    if (panel.bookMode)
                        panel.askBook(text.trim())
                    else
                        panel.ask("ask", text.trim())
                }
            }
        }

        // The answer, as it is written.
        ScrollView {
            id: answerScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: request.answer !== "" || request.running || request.error !== "" || panel.pendingQuestion !== ""
                     || (panel.bookMode && panel.sources.length === 0 && questionField.text !== "" && !request.running)
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: answerScroll.availableWidth
                spacing: 8
                Label {
                    visible: request.answer !== "" || request.running
                    text: panel.actionLabel.toUpperCase()
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    opacity: 0.55
                }
                Label {
                    visible: panel.pendingQuestion !== ""
                    text: qsTr("Finding passages… (the first time, Shelf reads the book)")
                    font.pixelSize: 12
                    opacity: 0.6
                }
                Label {
                    Layout.fillWidth: true
                    visible: panel.bookMode && panel.pendingQuestion === "" && !request.running && request.answer === ""
                             && panel.sources.length === 0
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: "#f5b347"
                    text: qsTr("Nothing in the book seems to match. Try other words, or names the book uses.")
                }
                BusyIndicator {
                    visible: (request.running && request.answer === "") || panel.pendingQuestion !== ""
                    running: visible
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                }
                Label {
                    Layout.fillWidth: true
                    text: panel.linked(request.answer)
                    textFormat: Text.MarkdownText
                    linkColor: theme.accent
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                    lineHeight: 1.15
                    onLinkActivated: link => {
                        if (link.startsWith("source:"))
                            panel.openSource(parseInt(link.slice(7)))
                        else
                            Qt.openUrlExternally(link)
                    }
                    HoverHandler { cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
                }
                // Where each cited passage is; click to go there.
                Label {
                    visible: panel.bookMode && panel.sources.length > 0 && !request.running && request.answer !== ""
                    Layout.topMargin: 6
                    text: qsTr("SOURCES")
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    opacity: 0.55
                }
                Repeater {
                    model: panel.bookMode && !request.running && request.answer !== "" ? panel.sources : []
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true
                        padding: 6
                        contentItem: Label {
                            text: "[%1]  %2 — %3".arg(modelData.n).arg(modelData.label || "")
                                                 .arg(modelData.text.replace(/\s+/g, " ").slice(0, 90) + "…")
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            color: "#c8c8ce"
                        }
                        onClicked: panel.sourceChosen(modelData.target)
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: request.error !== "" && request.error !== qsTr("Stopped.")
                    text: request.error
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: "#ff8a80"
                }
            }
            // Keep the newest words in view while it writes.
            Connections {
                target: request
                function onChanged() {
                    if (request.running)
                        answerScroll.ScrollBar.vertical.position = 1 - answerScroll.ScrollBar.vertical.size
                }
            }
        }
        Item { Layout.fillHeight: true; visible: !answerScroll.visible }

        Label {
            Layout.fillWidth: true
            visible: request.answer !== "" && !request.running
            text: qsTr("Written by AI on this computer. Check it against the book.")
            font.pixelSize: 11
            opacity: 0.45
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                visible: request.running
                text: qsTr("Stop")
                flat: true
                onClicked: request.cancel()
            }
            ToolbarButton {
                visible: request.answer !== "" && !request.running
                iconName: "copy"
                tip: qsTr("Copy")
                onClicked: {
                    annotations.copyToClipboard(request.answer)
                    win.showToast(qsTr("Copied."))
                }
            }
            Item { Layout.fillWidth: true }
            // Quiz answers become flashcards for Review.
            Button {
                readonly property int found: request.action === "quiz" && !request.running
                                             ? cards.parseQuiz(request.answer).length : 0
                visible: found > 0
                enabled: !panel.carded
                flat: true
                text: panel.carded ? qsTr("Added to Review") : qsTr("Make %n flashcard(s)", "", found)
                onClicked: {
                    const where = panel.target.cfi ? panel.target.cfi
                                : panel.target.page !== undefined ? String(panel.target.page) : ""
                    const added = cards.addFromQuiz(panel.bookId, request.answer, where)
                    panel.carded = true
                    win.showToast(added > 0 ? qsTr("Added %n flashcard(s). Review them from the library.", "", added)
                                            : qsTr("These flashcards are already in Review."))
                }
            }
            AccentButton {
                visible: request.answer !== "" && !request.running && !panel.bookMode
                enabled: !panel.saved
                text: panel.saved ? qsTr("Saved") : qsTr("Save to notes")
                onClicked: {
                    panel.saved = true
                    panel.saveRequested(request.answer, request.action)
                }
            }
        }
    }
}
