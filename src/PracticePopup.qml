import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Shelf

// A practice test on one part of a book: choose the chapter, how many
// questions and of what kind; the local model writes them from that
// chapter's text. Multiple choice is marked at once, free answers by the
// model; questions you miss can become flashcards.
Popup {
    id: practice
    anchors.centerIn: Overlay.overlay
    width: Math.min(handsOn ? 1040 : 780, (parent ? parent.width : 820) - 40)
    height: Math.min(handsOn ? 820 : 640, (parent ? parent.height : 680) - 40)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#18181d"; radius: theme.radius; border.color: "#2c2c33" }

    property var book: ({})
    property var entries: []        // the numbered outline
    property int entryIndex: 0
    property int count: 5
    property string types: "mixed"
    property bool fetching: false

    // Answers so far: index → { picked (choice) or text, correct, credit, error, checking }
    property var answers: ({})
    property int current: 0
    readonly property var question: test.questions.length > current ? test.questions[current] : null
    readonly property var answer: answers[current] || null
    // Code and Markov questions are answered in a tool rather than a box.
    readonly property var toolTypes: ["code", "markov", "molecule", "scenario"]
    readonly property bool handsOn: question !== null && toolTypes.includes(question.type)
    readonly property bool anyHandsOn: test.questions.some(q => toolTypes.includes(q.type))
    readonly property bool finished: test.state === "ready" && current >= test.questions.length
    readonly property int score: {
        let n = 0
        for (const k in answers)
            if (answers[k].correct)
                n += 1
        return n
    }

    // Slides instead of a book: [{label, text}], one per choice of what to
    // be tested on (a slide, a deck, a folder, the whole workspace).
    property var scopes: []
    property string scopeTitle: ""
    function openForSlides(title, choices, index) {
        book = ({})
        entries = []
        scopes = choices
        scopeTitle = title
        entryIndex = Math.max(0, Math.min(choices.length - 1, index))
        test.reset()
        answers = {}
        current = 0
        open()
    }
    // What the test is on, as the header and lessons show it.
    function partLabel() {
        if (scopes.length > 0)
            return scopes[entryIndex] ? scopes[entryIndex].label : ""
        return entries[entryIndex] ? entries[entryIndex].label : ""
    }

    function openFor(bookMap, outline, index) {
        scopes = []
        book = bookMap
        entries = outline
        entryIndex = Math.max(0, Math.min(outline.length - 1, index))
        test.reset()
        answers = {}
        current = 0
        open()
    }
    // The outline entry after this one at its depth or above: where its text ends.
    function endOf(i) {
        for (let k = i + 1; k < entries.length; ++k)
            if (entries[k].depth <= entries[i].depth)
                return entries[k].target
        return ""
    }
    function start() {
        answers = {}
        current = 0
        if (scopes.length > 0) {
            const sc = scopes[entryIndex]
            test.generate(sc.text, scopeTitle + " — " + sc.label, practice.count, practice.types)
            return
        }
        fetching = true
        const e = entries[entryIndex]
        bookSearch.chapterText({ id: book.id || "", title: book.title || "", path: book.path || "",
                                 format: book.format || "" }, e ? e.target : 0, e ? endOf(entryIndex) : "")
    }
    function setAnswer(i, value) {
        const next = Object.assign({}, answers)
        next[i] = Object.assign({}, answers[i] || {}, value)
        answers = next
    }
    function pick(choice) {
        if (answer)
            return
        setAnswer(current, { picked: choice, correct: choice === question.answer })
    }
    function check(text) {
        if (answer || text.trim() === "")
            return
        setAnswer(current, { text: text, checking: true })
        test.grade(current, text)
    }
    function checkTool() {
        if (answer || !toolLoader.item)
            return
        setAnswer(current, { checking: true })
        toolLoader.item.check()
    }
    function toolChecked(result) {
        // Not yet an answer (a row that doesn't add up to 1): try again.
        if (result.checkedNothing) {
            const next = Object.assign({}, answers)
            delete next[current]
            answers = next
            win.showToast(result.summary)
            return
        }
        if (result.usable) {
            setAnswer(current, { correct: result.correct, summary: result.summary, checking: false })
            return
        }
        // The tool could not check it (its tests were wrong, or Python is
        // missing): the AI marks the code against the model's solution.
        if (!result.answer || result.answer.trim() === "") {
            setAnswer(current, { correct: false, checking: false })
            return
        }
        setAnswer(current, { text: result.answer, checking: true,
                             summary: qsTr("This question's tests could not run, so the AI marked your code.") })
        test.grade(current, result.answer)
    }
    function missedToCards() {
        let added = 0
        test.questions.forEach((q, i) => {
            const a = answers[i]
            if (!a || a.correct)
                return
            const back = q.type === "choice" ? q.choices[q.answer] : q.answer
            if (cards.add(book.id || "", q.question, back + (q.why ? "\n\n" + q.why : "")) !== "")
                added += 1
        })
        win.showToast(qsTr("Added %n flashcard(s). Review them from the library.", "", added))
    }

    PracticeTest {
        id: test
        onGraded: (index, correct, credit, error) => practice.setAnswer(index, { correct: correct, credit: credit,
                                                                                error: error, checking: false })
    }
    Connections {
        target: bookSearch
        function onTextReady(id, from, text) {
            if (!practice.fetching || id !== (practice.book.id || ""))
                return
            practice.fetching = false
            const e = practice.entries[practice.entryIndex]
            test.generate(text, (practice.book.title || "") + (e ? " — " + e.label : ""), practice.count, practice.types)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Practice test")
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            Label {
                Layout.leftMargin: 8
                Layout.fillWidth: true
                text: practice.partLabel()
                font.pixelSize: 13
                opacity: 0.55
                elide: Text.ElideRight
            }
            ToolbarButton {
                iconName: "close"
                tip: qsTr("Close (Esc)")
                onClicked: practice.close()
            }
        }

        Label {
            visible: test.note !== "" && practice.question !== null
            Layout.fillWidth: true
            text: test.note
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: "#f5b347"
        }

        // Setting it up.
        ColumnLayout {
            visible: test.state === "" && !practice.fetching
            Layout.fillWidth: true
            spacing: 12
            Label { text: practice.scopes.length > 0 ? qsTr("Be tested on") : qsTr("Part of the book"); font.pixelSize: 13; opacity: 0.7 }
            ComboBox {
                Layout.fillWidth: true
                model: practice.scopes.length > 0 ? practice.scopes.map(s => s.label)
                                                  : practice.entries.map(e => "    ".repeat(e.depth) + e.label)
                currentIndex: practice.entryIndex
                onActivated: index => practice.entryIndex = index
            }
            Label { text: qsTr("Questions"); font.pixelSize: 13; opacity: 0.7 }
            Segmented {
                Layout.fillWidth: true
                options: [{ key: "mixed", label: qsTr("Mixed") }, { key: "choice", label: qsTr("Multiple choice") },
                          { key: "free", label: qsTr("Free response") }, { key: "handson", label: qsTr("Hands-on") }]
                value: practice.types
                onPicked: key => practice.types = key
            }
            RowLayout {
                Label { text: qsTr("How many"); font.pixelSize: 13; opacity: 0.7 }
                SpinBox {
                    from: 1; to: 20
                    value: practice.count
                    onValueModified: practice.count = value
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                opacity: 0.6
                text: practice.types === "handson"
                      ? qsTr("Hands-on questions have you do something: write code that is run against tests, build a Markov chain, draw a molecule, or work with a formula in a simulator. Shelf picks the one that fits the chapter; chapters that fit none get free-response questions.")
                      : practice.types === "mixed"
                        ? qsTr("Chapters that suit it may also get a hands-on question: code, a Markov chain, a molecule, or a simulator.") : ""
                visible: text !== ""
            }
            Label {
                visible: ollama.status !== "ready"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#f5b347"
                font.pixelSize: 12
                text: qsTr("Practice tests are written by local AI, which is not ready yet.")
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    visible: ollama.status !== "ready"
                    flat: true
                    text: qsTr("Local AI settings")
                    onClicked: win.openAiSettings()
                }
                AccentButton {
                    enabled: ollama.status === "ready" && practice.entries.length > 0
                    text: qsTr("Write the test")
                    onClicked: practice.start()
                }
            }
        }

        // Writing it.
        ColumnLayout {
            visible: practice.fetching || test.state === "writing"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Item { Layout.fillHeight: true }
            BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: parent.visible }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: practice.fetching ? qsTr("Reading the chapter…") : qsTr("Writing questions… this takes a minute or so.")
                opacity: 0.7
            }
            Item { Layout.fillHeight: true }
        }

        // It failed.
        ColumnLayout {
            visible: test.state === "failed"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#ff8a80"
                text: test.error
            }
            Item { Layout.fillHeight: true }
            Button { text: qsTr("Back"); onClicked: test.reset() }
        }

        // A hands-on question: the tool takes the room.
        ColumnLayout {
            visible: practice.handsOn
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            Label {
                text: qsTr("Question %1 of %2").arg(practice.current + 1).arg(test.questions.length)
                font.pixelSize: 12
                opacity: 0.55
            }
            Label {
                Layout.fillWidth: true
                Layout.maximumHeight: 150
                text: practice.question ? practice.question.question : ""
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                font.pixelSize: 16
                lineHeight: 1.2
            }
            Loader {
                id: toolLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: practice.anyHandsOn
                visible: practice.handsOn
                sourceComponent: ToolPanel {
                    question: practice.handsOn ? practice.question : null
                    onChecked: result => practice.toolChecked(result)
                }
            }
            Rectangle {
                visible: practice.answer !== null && practice.answer.checking !== true
                Layout.fillWidth: true
                implicitHeight: toolVerdict.implicitHeight + 20
                radius: Math.min(6, theme.radius)
                color: "#141418"
                border.color: practice.answer && practice.answer.correct ? "#66bb6a" : "#ef5350"
                ColumnLayout {
                    id: toolVerdict
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        text: (practice.answer && practice.answer.correct ? qsTr("Correct") : qsTr("Not quite"))
                              + (practice.answer && practice.answer.summary ? "  ·  " + practice.answer.summary : "")
                        font.weight: Font.DemiBold
                        color: practice.answer && practice.answer.correct ? "#81c784" : "#e57373"
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: practice.answer && practice.answer.error
                              ? qsTr("Could not mark this answer: %1").arg(practice.answer.error) : ""
                        wrapMode: Text.WordWrap
                        color: "#ff8a80"
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: practice.question && practice.question.why ? practice.question.why : ""
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                    }
                }
            }
        }

        // A question.
        ScrollView {
            id: questionScroll
            visible: practice.question !== null && !practice.handsOn
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: questionScroll.availableWidth
                spacing: 12
                Label {
                    text: qsTr("Question %1 of %2").arg(practice.current + 1).arg(test.questions.length)
                    font.pixelSize: 12
                    opacity: 0.55
                }
                RichText {
                    Layout.fillWidth: true
                    text: practice.question ? practice.question.question : ""
                    fontSize: 17
                    flickable: questionScroll.contentItem
                }
                // Multiple choice.
                Repeater {
                    model: practice.question && practice.question.type === "choice" ? practice.question.choices : []
                    delegate: ItemDelegate {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        readonly property bool isRight: practice.answer !== null && index === practice.question.answer
                        readonly property bool isWrong: practice.answer !== null && index === practice.answer.picked && !isRight
                        background: Rectangle {
                            radius: Math.min(6, theme.radius)
                            color: parent.isRight ? Qt.alpha("#66bb6a", 0.18) : parent.isWrong ? Qt.alpha("#ef5350", 0.18)
                                 : parent.hovered && practice.answer === null ? "#202026" : "#141418"
                            border.color: parent.isRight ? "#66bb6a" : parent.isWrong ? "#ef5350" : "#2c2c33"
                        }
                        contentItem: Label {
                            text: String.fromCharCode(65 + index) + ")  " + modelData
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                        }
                        onClicked: practice.pick(index)
                    }
                }
                // Free response.
                ColumnLayout {
                    visible: practice.question !== null && practice.question.type === "free"
                    Layout.fillWidth: true
                    spacing: 8
                    TextArea {
                        id: freeAnswer
                        Layout.fillWidth: true
                        Layout.minimumHeight: 90
                        wrapMode: TextEdit.Wrap
                        placeholderText: qsTr("Your answer")
                        readOnly: practice.answer !== null
                        // A new question, or a new test, starts with an empty box.
                        Connections {
                            target: practice
                            function onQuestionChanged() { freeAnswer.text = "" }
                        }
                    }
                    RowLayout {
                        AccentButton {
                            visible: practice.answer === null
                            text: qsTr("Check")
                            enabled: freeAnswer.text.trim() !== ""
                            onClicked: practice.check(freeAnswer.text)
                        }
                        BusyIndicator {
                            visible: practice.answer !== null && practice.answer.checking === true
                            running: visible
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                        }
                    }
                }
                // The verdict, and why.
                Rectangle {
                    visible: practice.answer !== null && practice.answer.checking !== true
                    Layout.fillWidth: true
                    implicitHeight: verdict.implicitHeight + 24
                    radius: Math.min(6, theme.radius)
                    color: "#141418"
                    border.color: practice.answer && practice.answer.correct ? "#66bb6a" : "#ef5350"
                    ColumnLayout {
                        id: verdict
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        Label {
                            text: practice.answer && practice.answer.correct ? qsTr("Correct") : qsTr("Not quite")
                            font.weight: Font.DemiBold
                            color: practice.answer && practice.answer.correct ? "#81c784" : "#e57373"
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: practice.answer && practice.answer.error
                                  ? qsTr("Could not mark this answer: %1").arg(practice.answer.error) : ""
                            wrapMode: Text.WordWrap
                            color: "#ff8a80"
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: practice.answer && practice.answer.credit && !practice.answer.correct
                                  ? qsTr("Right so far: “%1”").arg(practice.answer.credit) : ""
                            wrapMode: Text.WordWrap
                        }
                        // The model answer and the explanation, with any
                        // formulas or diagrams drawn.
                        RichText {
                            Layout.fillWidth: true
                            visible: text !== ""
                            background: "#141418"
                            flickable: questionScroll.contentItem
                            text: {
                                const q = practice.question
                                if (!q || practice.answer === null || practice.answer.checking === true)
                                    return ""
                                const parts = []
                                if (q.type === "free" && q.answer)
                                    parts.push("**" + qsTr("Answer:") + "** " + q.answer)
                                if (q.why)
                                    parts.push(q.why)
                                return parts.join("\n\n")
                            }
                        }
                        // The same answer as an animated lesson.
                        Button {
                            visible: practice.answer !== null && practice.answer.checking !== true && ollama.status === "ready"
                            flat: true
                            text: qsTr("Explain visually")
                            icon.source: "image://icon/sparkle?color=" + encodeURIComponent(theme.accent)
                            onClicked: {
                                const q = practice.question
                                const answer = q.type === "choice" ? q.choices[q.answer] || q.answer : q.answer
                                win.explainVisually({
                                    key: "question:" + String(q.question).slice(0, 80),
                                    title: practice.partLabel(),
                                    text: q.question + "\n\nThe right answer: " + answer + "\n\n" + (q.why || ""),
                                    notes: "", passages: [], sources: [], question: ""
                                })
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            visible: practice.question !== null
            Layout.fillWidth: true
            Label {
                text: qsTr("Score: %1 / %2").arg(practice.score).arg(test.questions.length)
                opacity: 0.6
            }
            Item { Layout.fillWidth: true }
            BusyIndicator {
                visible: practice.handsOn && practice.answer !== null && practice.answer.checking === true
                running: visible
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
            }
            AccentButton {
                visible: practice.handsOn && practice.answer === null
                text: qsTr("Check")
                onClicked: practice.checkTool()
            }
            AccentButton {
                visible: !practice.handsOn || practice.answer !== null
                enabled: practice.answer !== null && practice.answer.checking !== true
                text: practice.current + 1 < test.questions.length ? qsTr("Next") : qsTr("See results")
                onClicked: practice.current += 1
            }
        }

        // The results.
        ColumnLayout {
            visible: practice.finished
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            Item { Layout.fillHeight: true }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("%1 of %2 right").arg(practice.score).arg(test.questions.length)
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: practice.score === test.questions.length ? qsTr("Every one. Well done.")
                    : qsTr("Questions you missed can become flashcards, so they come back until you know them.")
                opacity: 0.65
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    flat: true
                    text: qsTr("New test")
                    onClicked: test.reset()
                }
                Item { Layout.fillWidth: true }
                Button {
                    visible: practice.score < test.questions.length
                    text: qsTr("Make flashcards from missed")
                    onClicked: practice.missedToCards()
                }
                AccentButton {
                    text: qsTr("Done")
                    onClicked: practice.close()
                }
            }
        }
    }
}
