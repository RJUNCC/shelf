import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A plan for the next exam, from the study review a teacher hands out. The
// local AI reads the review's topics; Shelf finds each topic's slides by
// their words and spreads the topics over the days left, a practice test on
// each day's topics, and one on everything the day before the exam. The
// days are worked out afresh each time, so a missed day just moves along.
Rectangle {
    id: plan
    color: "#141418"

    property string workspaceId
    property var ws: ({})
    property var decks: []
    signal openSlide(int deck, int slide, var words)
    signal focusRequested()
    signal explainSlide(int deck, int slide)
    signal practise(string title, var choices)
    signal closeRequested()

    readonly property var current: ws.plan || null
    property bool editing: false
    property bool previewing: false
    // Typing in the review, where Tab and other keys belong to the text.
    readonly property bool typing: reviewField.activeFocus
    property bool writing: false
    property string error: ""
    readonly property string key: "plan:" + workspaceId

    // --- Making a plan -------------------------------------------------------

    function make() {
        error = ""
        writing = true
        explainer.studyTopics(key, reviewField.text)
    }
    Connections {
        target: workspaces
        function onReviewLoaded(id, name, text) {
            if (id !== plan.workspaceId)
                return
            reviewField.text = text
            importedName.name = name
            plan.previewing = true
            plan.editing = plan.current !== null
        }
    }
    Connections {
        target: explainer
        function onTopicsReady(k, topics) {
            if (k !== plan.key)
                return
            plan.writing = false
            plan.editing = false
            workspaces.setPlan(plan.workspaceId, {
                review: reviewField.text, examDate: plan.formDate, created: Date.now(),
                topics: topics.map(t => Object.assign({}, t, { slides: plan.match(t), done: false }))
            })
        }
        function onTopicsFailed(k, message) {
            if (k !== plan.key)
                return
            plan.writing = false
            plan.error = message
        }
    }

    // The slides that best fit a topic: its words in titles count most;
    // a slide shown in steps counts once.
    function match(topic) {
        const words = topic.keywords.concat(topic.topic.toLowerCase().split(/[^a-z0-9]+/).filter(w => w.length > 3))
        const scored = []
        decks.forEach((d, di) => {
            let lastTitle = null
            d.slides.forEach((s, si) => {
                const title = (s.title || "").toLowerCase()
                if (title && title === lastTitle)
                    return
                lastTitle = title
                const text = (title + " " + (s.paragraphs || []).map(p => p.text).join(" ") + " " + (s.notes || "")).toLowerCase()
                let score = 0
                for (const w of words) {
                    if (title.indexOf(w) >= 0) score += 3
                    else if (text.indexOf(w) >= 0) score += 1
                }
                if (score >= 2)
                    scored.push({ deckId: d.id, slide: si, score: score })
            })
        })
        scored.sort((a, b) => b.score - a.score)
        return scored.slice(0, 8).map(x => ({ deckId: x.deckId, slide: x.slide }))
    }

    // --- The days ------------------------------------------------------------

    function dayStart(t) { const d = new Date(t); d.setHours(0, 0, 0, 0); return d.getTime() }
    // yyyy-mm-dd read as a local date; Date.parse would read it as UTC
    // midnight, the day before in the Americas.
    function parseDay(text) {
        const m = /^(\d{4})-(\d{1,2})-(\d{1,2})$/.exec(String(text || "").trim())
        if (m)
            return new Date(+m[1], +m[2] - 1, +m[3]).getTime()
        const t = Date.parse(text)
        return isNaN(t) ? 0 : dayStart(t)
    }
    function isoDay(t) {
        const d = new Date(t)
        return d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0")
    }
    function niceDay(t) { return new Date(t).toLocaleDateString(Qt.locale(), "dddd d MMMM yyyy") }
    readonly property real exam: current ? parseDay(current.examDate || "") : 0
    // The date chosen in the form, yyyy-mm-dd or "".
    property string formDate: ""
    readonly property int daysLeft: exam ? Math.round((exam - dayStart(Date.now())) / 86400000) : -1
    function deckIndex(id) { return decks.findIndex(d => d.id === id) }

    // Days from today to the day before the exam (a week when no date is
    // given), unfinished topics spread over all but the last, which is for
    // a test on everything.
    readonly property var days: {
        if (!current)
            return []
        const todo = []
        current.topics.forEach((t, i) => { if (!t.done) todo.push(i) })
        const count = daysLeft > 1 ? daysLeft : daysLeft === 1 ? 1 : 7
        const studyDays = Math.max(1, count - 1)
        // Topics spread evenly over the study days (not packed at the start);
        // a day with no new topic is for catching up on what came before.
        const byDay = []
        for (let d = 0; d < studyDays; ++d)
            byDay.push([])
        todo.forEach((topic, k) => byDay[Math.min(studyDays - 1, Math.floor(k * studyDays / todo.length))].push(topic))
        const out = []
        const sofar = []
        for (let d = 0; d < count; ++d) {
            const date = dayStart(Date.now()) + d * 86400000
            const last = d === count - 1 && count > 1
            const topics = last ? [] : byDay[d] || []
            out.push({ date: date, topics: topics, final: last, catchUp: !last && topics.length === 0, before: sofar.slice() })
            sofar.push(...topics)
        }
        // Catch-up days are only shown once there's something to catch up on.
        return out.filter(day => day.final || day.topics.length > 0 || (day.catchUp && day.before.length > 0))
    }
    function dayLabel(t) {
        const today = dayStart(Date.now())
        if (t === today) return qsTr("Today")
        if (t === today + 86400000) return qsTr("Tomorrow")
        return new Date(t).toLocaleDateString(Qt.locale(), "dddd d MMMM")
    }
    function toggleDone(i) {
        const next = JSON.parse(JSON.stringify(current))
        next.topics[i].done = !next.topics[i].done
        workspaces.setPlan(workspaceId, next)
    }
    // A test on some topics' slides.
    function practiseTopics(indexes, title) {
        let text = []
        for (const i of indexes) {
            const t = current.topics[i]
            for (const s of t.slides) {
                const d = deckIndex(s.deckId)
                if (d < 0 || !decks[d].slides[s.slide])
                    continue
                const sl = decks[d].slides[s.slide]
                text.push(qsTr("Topic: %1").arg(t.topic) + "\n" + [sl.title || ""].concat((sl.paragraphs || []).map(p => p.text)).join("\n")
                          + (sl.notes ? "\n" + sl.notes : ""))
            }
            if (t.slides.length === 0)
                text.push(qsTr("Topic: %1").arg(t.topic) + "\n" + (t.details || ""))
        }
        practise(ws.name || "", [{ key: "plan", label: title, text: text.join("\n\n").slice(0, 12000) }])
    }

    // --- The view --------------------------------------------------------------

    // A click on empty space gives the keys back to the workspace (1, 2).
    MouseArea {
        anchors.fill: parent
        z: -1
        onPressed: mouse => { plan.focusRequested(); mouse.accepted = false }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Icon { name: "notes"; size: 20; color: theme.accent }
            Label {
                text: qsTr("Study plan")
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            Button {
                visible: plan.current !== null && !plan.editing
                flat: true
                Layout.leftMargin: 6
                font.pixelSize: 13
                Material.foreground: "#f5b347"
                text: plan.daysLeft > 1 ? qsTr("Exam in %1 days (%2)").arg(plan.daysLeft).arg(new Date(plan.exam).toLocaleDateString(Qt.locale(), "d MMM"))
                      : plan.daysLeft === 1 ? qsTr("Exam tomorrow")
                      : plan.daysLeft === 0 ? qsTr("Exam today")
                      : plan.exam ? qsTr("The exam date has passed") : qsTr("Set the exam date…")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Change the exam date; the days are planned again")
                onClicked: calendar.ask(plan.exam ? plan.isoDay(plan.exam) : "", day => {
                    const next = JSON.parse(JSON.stringify(plan.current))
                    next.examDate = day
                    workspaces.setPlan(plan.workspaceId, next)
                })
            }
            Item { Layout.fillWidth: true }
            Button {
                visible: plan.current !== null && !plan.editing
                flat: true
                text: qsTr("Change the review…")
                onClicked: {
                    reviewField.text = plan.current.review || ""
                    plan.formDate = plan.current.examDate ? plan.isoDay(plan.parseDay(plan.current.examDate)) : ""
                    plan.editing = true
                }
            }
            Button {
                visible: plan.current !== null && !plan.editing
                flat: true
                text: qsTr("Delete plan")
                onClicked: workspaces.setPlan(plan.workspaceId, {})
            }
            Button {
                flat: true
                text: qsTr("Back to the slides (1)")
                onClicked: plan.closeRequested()
            }
        }

        // Pasting the review.
        ColumnLayout {
            visible: plan.current === null || plan.editing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("Add the study review or exam guide from your teacher or course (the list of what the next exam covers) by pasting it or importing the file. The local AI reads its topics, Shelf finds the slides for each, and spreads them over the days you have, with practice tests along the way.")
            }
            // The review: pasted, or read from a file.
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("Import a file…")
                    icon.source: "image://icon/plus?color=%23e6e6e6"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("PDF, Word, LibreOffice, a saved web page or text. From Google Docs: File → Download → PDF or Word. From Canvas: save or print the page as PDF, or copy and paste it.")
                    onClicked: workspaces.pickReviewDocument(plan.workspaceId)
                }
                // The review is Markdown: write it, or see it formatted.
                Segmented {
                    Layout.preferredWidth: 190
                    options: [{ key: "edit", label: qsTr("Edit") }, { key: "preview", label: qsTr("Preview") }]
                    value: plan.previewing ? "preview" : "edit"
                    onPicked: key => plan.previewing = key === "preview"
                }
                Label {
                    id: importedName
                    property string name: ""
                    visible: name !== ""
                    text: qsTr("Read from %1").arg(name)
                    opacity: 0.6
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Math.min(6, theme.radius)
                color: "#101014"
                border.color: reviewField.activeFocus ? theme.accent : "#2c2c33"
                Flickable {
                    id: previewScroll
                    visible: plan.previewing
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    contentHeight: preview.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    RichText {
                        id: preview
                        width: previewScroll.width - 10
                        text: plan.previewing ? reviewField.text : ""
                        background: "#101014"
                        flickable: previewScroll
                    }
                }
                Flickable {
                    id: reviewScroll
                    visible: !plan.previewing
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    contentWidth: width
                    contentHeight: reviewField.height
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    // Keeps the line being typed in view.
                    function follow(r) {
                        if (contentY >= r.y)
                            contentY = r.y
                        else if (contentY + height <= r.y + r.height)
                            contentY = r.y + r.height - height
                    }
                    TextEdit {
                        id: reviewField
                        width: reviewScroll.width - 10
                        // The whole box takes clicks, even while it's empty.
                        height: Math.max(contentHeight, reviewScroll.height)
                        wrapMode: TextEdit.Wrap
                        font.pixelSize: 13
                        color: "#e8e6ea"
                        selectionColor: Qt.alpha(theme.accent, 0.4)
                        selectByMouse: true
                        focus: true
                        onCursorRectangleChanged: reviewScroll.follow(cursorRectangle)
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton
                        cursorShape: Qt.IBeamCursor
                    }
                }
                Label {
                    visible: reviewField.text === "" && !reviewField.activeFocus && !plan.previewing
                    x: 12
                    y: 12
                    text: qsTr("Paste the review here (Markdown works: # headings, - lists, **bold**), or import it from a file above.")
                    opacity: 0.4
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Exam date (optional)"); opacity: 0.7 }
                Button {
                    text: plan.formDate ? plan.niceDay(plan.parseDay(plan.formDate)) : qsTr("Pick a date…")
                    icon.source: "image://icon/notes?color=%23e6e6e6"
                    onClicked: calendar.ask(plan.formDate, day => plan.formDate = day)
                }
                ToolButton {
                    visible: plan.formDate !== ""
                    text: "✕"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("No date: plan a week")
                    onClicked: plan.formDate = ""
                }
                Item { Layout.fillWidth: true }
                BusyIndicator { visible: plan.writing; running: visible; Layout.preferredHeight: 28; Layout.preferredWidth: 28 }
                Button {
                    visible: plan.editing
                    flat: true
                    text: qsTr("Cancel")
                    onClicked: plan.editing = false
                }
                AccentButton {
                    text: plan.writing ? qsTr("Reading the review…") : qsTr("Make my plan")
                    enabled: !plan.writing && reviewField.text.trim().length > 10 && ollama.status === "ready"
                    onClicked: plan.make()
                }
            }
            Label {
                visible: plan.error !== "" || ollama.status !== "ready"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#ff8a80"
                font.pixelSize: 12
                text: plan.error !== "" ? plan.error : qsTr("Making a plan needs the local AI; set it up under Local AI.")
            }
        }

        // The plan.
        ListView {
            id: dayList
            visible: plan.current !== null && !plan.editing
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 14
            model: plan.days
            ScrollBar.vertical: ScrollBar {}
            header: Label {
                width: dayList.width
                bottomPadding: 10
                visible: plan.current !== null
                text: plan.current ? qsTr("%1 of %2 topics done").arg(plan.current.topics.filter(t => t.done).length).arg(plan.current.topics.length) : ""
                opacity: 0.6
                font.pixelSize: 12
            }
            delegate: Rectangle {
                id: dayCard
                required property var modelData
                width: dayList.width - 12
                implicitHeight: dayColumn.implicitHeight + 24
                radius: Math.min(8, theme.radius)
                color: "#1a1a20"
                border.color: dayCard.modelData.date === plan.dayStart(Date.now()) ? Qt.alpha(theme.accent, 0.6) : "#2a2a31"
                ColumnLayout {
                    id: dayColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: plan.dayLabel(dayCard.modelData.date)
                            font.weight: Font.DemiBold
                            font.pixelSize: 15
                            Layout.fillWidth: true
                        }
                        Button {
                            flat: true
                            font.pixelSize: 12
                            text: dayCard.modelData.final ? qsTr("Practice test on everything")
                                  : dayCard.modelData.catchUp ? qsTr("Practice test on everything so far")
                                  : qsTr("Practice test on these topics")
                            onClicked: dayCard.modelData.final
                                       ? plan.practiseTopics(plan.current.topics.map((t, i) => i), qsTr("Everything on the exam"))
                                       : dayCard.modelData.catchUp
                                       ? plan.practiseTopics(dayCard.modelData.before, qsTr("Everything so far"))
                                       : plan.practiseTopics(dayCard.modelData.topics, plan.dayLabel(dayCard.modelData.date))
                        }
                    }
                    Label {
                        visible: dayCard.modelData.catchUp
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        font.pixelSize: 12
                        text: qsTr("Catch-up day: go back over anything unfinished, and take a practice test on what you've covered so far.")
                    }
                    Label {
                        visible: dayCard.modelData.final
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        font.pixelSize: 12
                        text: qsTr("Review day: a practice test on every topic, then look again at anything you missed.")
                    }
                    Repeater {
                        model: dayCard.modelData.topics
                        delegate: ColumnLayout {
                            id: topicItem
                            required property int modelData
                            readonly property var topic: plan.current.topics[modelData]
                            Layout.fillWidth: true
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                CheckBox {
                                    checked: topicItem.topic.done
                                    onToggled: plan.toggleDone(topicItem.modelData)
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true
                                        text: topicItem.topic.topic
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 14
                                    }
                                    Label {
                                        visible: text !== ""
                                        Layout.fillWidth: true
                                        text: topicItem.topic.details || ""
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 12
                                        opacity: 0.6
                                    }
                                }
                                Button {
                                    flat: true
                                    font.pixelSize: 12
                                    text: qsTr("Practice")
                                    onClicked: plan.practiseTopics([topicItem.modelData], topicItem.topic.topic)
                                }
                            }
                            // Its slides: click one to go there.
                            Flow {
                                Layout.fillWidth: true
                                Layout.leftMargin: 44
                                spacing: 6
                                Repeater {
                                    model: topicItem.topic.slides
                                    delegate: Rectangle {
                                        required property var modelData
                                        readonly property int deck: plan.deckIndex(modelData.deckId)
                                        readonly property var sl: deck >= 0 ? plan.decks[deck].slides[modelData.slide] : null
                                        visible: sl !== null && sl !== undefined
                                        implicitWidth: Math.min(260, chipLabel.implicitWidth + 20)
                                        implicitHeight: 26
                                        radius: 13
                                        color: chipMouse.containsMouse ? Qt.alpha(theme.accent, 0.25) : "#24242b"
                                        Label {
                                            id: chipLabel
                                            anchors.centerIn: parent
                                            width: Math.min(implicitWidth, 240)
                                            elide: Text.ElideRight
                                            font.pixelSize: 11
                                            text: parent.sl ? qsTr("%1 · %2").arg(parent.sl.title || qsTr("Slide %1").arg(modelData.slide + 1))
                                                                            .arg(plan.decks[parent.deck].title) : ""
                                        }
                                        MouseArea {
                                            id: chipMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: plan.openSlide(parent.deck, modelData.slide,
                                                                      topicItem.topic.keywords.concat(topicItem.topic.topic.split(/\s+/).filter(w => w.length > 4)))
                                        }
                                    }
                                }
                                Label {
                                    visible: topicItem.topic.slides.length === 0
                                    text: qsTr("No slide mentions this; your books may.")
                                    font.pixelSize: 11
                                    opacity: 0.5
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // A month to pick the exam's day from.
    Popup {
        id: calendar
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 16
        width: 340
        Material.roundedScale: theme.radius
        background: Rectangle { color: "#1b1b20"; radius: theme.radius; border.color: "#2c2c33" }
        property var then: null
        property int month: new Date().getMonth()
        property int year: new Date().getFullYear()
        property real chosen: 0
        function ask(current, done) {
            then = done
            const t = current ? plan.parseDay(current) : 0
            chosen = t
            const d = t ? new Date(t) : new Date()
            month = d.getMonth()
            year = d.getFullYear()
            open()
        }
        function step(by) {
            const d = new Date(year, month + by, 1)
            month = d.getMonth()
            year = d.getFullYear()
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label { text: qsTr("When is the exam?"); font.weight: Font.DemiBold; font.pixelSize: 15 }
            RowLayout {
                Layout.fillWidth: true
                ToolButton { text: "‹"; font.pixelSize: 18; onClicked: calendar.step(-1) }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: new Date(calendar.year, calendar.month, 1).toLocaleDateString(Qt.locale(), "MMMM yyyy")
                    font.weight: Font.DemiBold
                }
                ToolButton { text: "›"; font.pixelSize: 18; onClicked: calendar.step(1) }
            }
            DayOfWeekRow {
                Layout.fillWidth: true
                locale: Qt.locale()
                delegate: Label {
                    required property string shortName
                    text: shortName
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 11
                    opacity: 0.5
                }
            }
            MonthGrid {
                id: grid
                Layout.fillWidth: true
                month: calendar.month
                year: calendar.year
                locale: Qt.locale()
                delegate: Rectangle {
                    readonly property real day: new Date(model.year, model.month, model.day).getTime()
                    implicitWidth: 38
                    readonly property bool past: day < plan.dayStart(Date.now())
                    implicitHeight: 34
                    radius: 17
                    color: day === calendar.chosen ? theme.accent : dayMouse.containsMouse && !past ? "#2a2a33" : "transparent"
                    border.color: model.today ? theme.accent : "transparent"
                    Label {
                        anchors.centerIn: parent
                        text: model.day
                        opacity: model.month === calendar.month ? (parent.past ? 0.3 : 1) : 0.25
                        color: parent.day === calendar.chosen ? "#111" : "#e8e6ea"
                        font.weight: parent.day === calendar.chosen ? Font.Bold : Font.Normal
                    }
                    MouseArea {
                        id: dayMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !parent.past
                        onClicked: {
                            calendar.chosen = parent.day
                            if (calendar.then)
                                calendar.then(plan.isoDay(parent.day))
                            calendar.close()
                        }
                    }
                }
            }
        }
    }
}
