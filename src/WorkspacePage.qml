import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Pdf
import QtQuick.Shapes
import "numbering.js" as Numbering

// A learning workspace: a course's slides on the left, the slide in the
// middle, and on the right the book sections it links to, sections Shelf
// suggests from the slide's words, and the slide's speaker notes.
FocusScope {
    id: page

    property string workspaceId
    property bool active: false
    property bool renameFirst: false

    property var ws: workspaces.workspace(workspaceId)
    property int deckIndex: 0
    property int slideIndex: 0
    readonly property var decks: ws.decks || []
    readonly property var deck: deckIndex < decks.length ? decks[deckIndex] : null
    readonly property var slides: deck ? deck.slides : []
    readonly property var slide: slideIndex < slides.length ? slides[slideIndex] : null
    readonly property bool typing: titleField.activeFocus || linkPopup.visible || booksPopup.visible || nameDialog.visible
                                   || slideFilter.activeFocus || slideNumber.activeFocus || myNote.activeFocus || planOpen
                                   || findField.activeFocus
    property var suggestions: []

    function reload() {
        ws = workspaces.workspace(workspaceId)
        deckIndex = Math.min(deckIndex, Math.max(0, decks.length - 1))
        slideIndex = Math.min(slideIndex, Math.max(0, slides.length - 1))
    }
    Connections {
        target: workspaces
        function onChanged(id) {
            if (id !== page.workspaceId)
                return
            const before = page.decks.length
            page.reload()
            // A deck just added is shown, with the list folded so the
            // decks stay easy to find.
            if (page.decks.length > before) {
                page.deckIndex = page.decks.length - 1
                page.slideIndex = 0
                page.collapseAll()
            }
        }
    }
    Component.onCompleted: takePendingSlide()
    // A new workspace starts with its name ready to type. Opening the tab
    // focuses the page a moment later, so the name waits for that.
    Timer {
        interval: 250
        running: page.renameFirst
        onTriggered: { titleField.forceActiveFocus(); titleField.selectAll() }
    }
    // A slide asked for from elsewhere (the knowledge graph).
    function takePendingSlide() {
        const p = win.pendingSlide
        if (!p || p.workspaceId !== workspaceId)
            return
        win.pendingSlide = null
        const d = decks.findIndex(x => x.id === p.deckId)
        if (d >= 0) {
            deckIndex = d
            slideIndex = Math.min(p.slide, Math.max(0, slides.length - 1))
        }
    }
    Connections {
        target: win
        function onPendingSlideChanged() { page.takePendingSlide() }
    }

    // --- The slide list ---------------------------------------------------

    // The decks shown open in the list, by id (the list starts folded).
    property var openDecks: ({})
    function isOpen(d) { return !!(decks[d] && openDecks[decks[d].id]) }
    function setOpen(d, on) {
        if (!decks[d])
            return
        const next = Object.assign({}, openDecks)
        if (on)
            next[decks[d].id] = true
        else
            delete next[decks[d].id]
        openDecks = next
    }
    // The two side panels fold away to give the slide the room.
    property bool listOpen: library.setting("workspaceList", true) === true
    property bool panelOpen: library.setting("workspacePanel", true) === true
    function toggleList() { listOpen = !listOpen; library.setSetting("workspaceList", listOpen) }
    function togglePanel() { panelOpen = !panelOpen; library.setSetting("workspacePanel", panelOpen) }
    // "slides" or "history" (the explanations watched).
    property string listMode: "slides"
    property bool historyRecent: library.setting("workspaceHistoryRecent", false) === true
    property bool thumbnails: library.setting("workspaceThumbnails", false) === true
    function setThumbnails(on) {
        thumbnails = on
        library.setSetting("workspaceThumbnails", on)
    }
    function openDeckAt(i) {
        if (i === deckIndex) {
            setOpen(i, !isOpen(i))
            return
        }
        deckIndex = i
        slideIndex = 0
        setOpen(i, true)
    }
    function moveDeck(i, step) {
        const current = deck ? deck.id : ""
        workspaces.moveDeck(workspaceId, decks[i].id, step)
        deckIndex = Math.max(0, decks.findIndex(d => d.id === current))
    }
    function showSlide(d, i, fromSearch) {
        deckIndex = d
        slideIndex = i
        if (fromSearch)
            slideFilter.text = ""
        // After the click is done, so the arrows turn slides again.
        Qt.callLater(() => page.takeKeys())
    }
    // Slides in a row with the same title are one slide being built up
    // step by step (as Beamer and PowerPoint animations export), so the
    // outline lists the title once.
    function outlineOf(deckIdx) {
        const out = []
        const list = decks[deckIdx].slides
        for (let i = 0; i < list.length; ++i) {
            const title = String(list[i].title || "").trim()
            const links = (list[i].links || []).length
            const last = out[out.length - 1]
            if (last && title !== "" && last.title === title) {
                last.last = i
                last.count += 1
                last.links += links
            } else {
                out.push({ type: "entry", deck: deckIdx, slide: i, last: i, title: title, count: 1, links: links })
            }
        }
        return out
    }
    // Folders, by name, numbers in order ("Week 2" before "Week 10").
    readonly property var folders: [...new Set(decks.map(d => d.folder || "").filter(f => f !== ""))]
                                   .sort((a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: "base" }))
    property var closedFolders: ({})

    // Picking decks (Select, or Ctrl/Shift-click) to put them in a folder
    // or be tested on them together.
    property bool picking: false
    property string newFolderName: ""   // a folder being made, waiting for its decks
    property var picked: ({})           // deck id → true
    property int lastPicked: -1
    readonly property var pickedIds: Object.keys(picked)
    function togglePick(d, range) {
        const next = Object.assign({}, picked)
        if (range && lastPicked >= 0) {
            const order = deckOrder()
            const a = order.indexOf(lastPicked), b = order.indexOf(d)
            for (let k = Math.min(a, b); k <= Math.max(a, b); ++k)
                next[decks[order[k]].id] = true
        } else if (next[decks[d].id]) {
            delete next[decks[d].id]
        } else {
            next[decks[d].id] = true
        }
        picked = next
        lastPicked = d
        picking = true
    }
    function stopPicking() { picking = false; picked = ({}); lastPicked = -1; newFolderName = "" }
    // Decks in the order the list shows them: folders first, then the rest.
    function deckOrder() {
        const out = []
        for (const f of folders)
            decks.forEach((d, i) => { if ((d.folder || "") === f) out.push(i) })
        decks.forEach((d, i) => { if (!d.folder) out.push(i) })
        return out
    }
    function movePicked(folder) {
        for (const id of pickedIds)
            workspaces.setDeckFolder(workspaceId, id, folder)
        if (folder !== "") {
            const next = Object.assign({}, closedFolders)
            delete next[folder]
            closedFolders = next
        }
        stopPicking()
    }
    // Whether any deck or folder is open in the list.
    readonly property bool anythingOpen: Object.keys(openDecks).length > 0 || folders.some(f => !closedFolders[f])
    function collapseAll() {
        const all = {}
        for (const f of folders)
            all[f] = true
        closedFolders = all
        openDecks = ({})
    }
    // Every folder and every deck open.
    function expandAll() {
        closedFolders = ({})
        const all = {}
        for (const d of decks)
            all[d.id] = true
        openDecks = all
    }
    function toggleFolder(f) {
        const next = Object.assign({}, closedFolders)
        next[f] = !next[f]
        closedFolders = next
    }

    // --- Practice tests on slides ------------------------------------------

    // The text of some slides for a test: build-up steps once (the last,
    // fullest one), with notes. A big folder is sampled across all its
    // slides, differently each time, so tests in turn cover everything.
    function testText(list) {
        const kept = []
        for (let k = 0; k < list.length; ++k) {
            const cur = list[k], next = list[k + 1]
            if (next && next.deck === cur.deck && (next.slide.title || "") !== "" && next.slide.title === cur.slide.title)
                continue
            kept.push(cur)
        }
        const piece = e => qsTr("Slide %1 of %2").arg(e.index + 1).arg(decks[e.deck].title) + ":\n" + slideText(e.slide)
                           + (e.slide.notes ? "\n" + qsTr("Notes:") + " " + e.slide.notes : "")
        const limit = 12000
        let total = kept.reduce((n, e) => n + piece(e).length, 0)
        let chosen = kept
        if (total > limit) {
            const order = kept.map((e, i) => i).sort(() => Math.random() - 0.5)
            const pick = new Set()
            let used = 0
            for (const i of order) {
                const n = piece(kept[i]).length
                if (used + n > limit)
                    continue
                pick.add(i)
                used += n
            }
            chosen = kept.filter((e, i) => pick.has(i))
        }
        return chosen.map(piece).join("\n\n")
    }
    function slidesOf(deckIndexes) {
        const out = []
        for (const d of deckIndexes)
            decks[d].slides.forEach((sl, i) => out.push({ deck: d, index: i, slide: sl }))
        return out
    }
    // What a test can be on, from the slide shown outwards; `which` picks
    // the first one offered ("slide", "deck", "folder", "all").
    function practice(which, deckIdx, folder) {
        const d = deckIdx !== undefined ? deckIdx : deckIndex
        const f = folder !== undefined ? folder : (decks[d] ? decks[d].folder || "" : "")
        const choices = []
        if (d === deckIndex && slide && which !== "folder")
            choices.push({ key: "slide", label: qsTr("This slide: %1").arg(slide.title || qsTr("slide %1").arg(slideIndex + 1)),
                           text: testText([{ deck: d, index: slideIndex, slide: slide }]) })
        if (decks[d] && which !== "folder")
            choices.push({ key: "deck", label: qsTr("These slides: %1").arg(decks[d].title), text: testText(slidesOf([d])) })
        if (f !== "") {
            const inFolder = []
            decks.forEach((dk, i) => { if ((dk.folder || "") === f) inFolder.push(i) })
            choices.push({ key: "folder", label: qsTr("Everything in the folder “%1” (%n deck(s))", "", inFolder.length).arg(f),
                           text: testText(slidesOf(inFolder)) })
        }
        choices.push({ key: "all", label: qsTr("The whole workspace"), text: testText(slidesOf(decks.map((x, i) => i))) })
        win.practiceOnSlides(ws.name || "", choices, Math.max(0, choices.findIndex(c => c.key === which)))
    }

    // The explanations watched, in course order (folder, deck, slide) or
    // newest first.
    readonly property var historyRows: {
        const out = []
        const deckAt = {}
        decks.forEach((d, i) => deckAt[d.id] = i)
        const folderRank = f => f === "" ? folders.length : folders.indexOf(f)
        for (const h of (ws.history || [])) {
            const d = deckAt[h.deckId]
            if (d === undefined || !decks[d].slides[h.slide])
                continue
            out.push({ type: "history", deck: d, slide: h.slide, question: h.question || "", time: h.time,
                       title: decks[d].slides[h.slide].title || qsTr("Slide %1").arg(h.slide + 1),
                       deckTitle: decks[d].title, rank: [folderRank(decks[d].folder || ""), d, h.slide] })
        }
        if (historyRecent)
            out.sort((a, b) => b.time - a.time)
        else
            out.sort((a, b) => a.rank[0] - b.rank[0] || a.rank[1] - b.rank[1] || a.rank[2] - b.rank[2] || a.time - b.time)
        return out
    }
    function ago(time) {
        const minutes = Math.round((Date.now() - time) / 60000)
        if (minutes < 1) return qsTr("just now")
        if (minutes < 60) return qsTr("%1 min ago").arg(minutes)
        if (minutes < 60 * 24) return qsTr("%1 h ago").arg(Math.round(minutes / 60))
        return new Date(time).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    readonly property var navRows: {
        if (listMode === "history")
            return historyRows
        const query = slideFilter.text.trim().toLowerCase()
        const rows = []
        if (query !== "") {
            const words = query.split(/\s+/)
            for (let d = 0; d < decks.length && rows.length < 300; ++d) {
                let lastTitle = null
                decks[d].slides.forEach((sl, i) => {
                    const text = (slideText(sl) + "\n" + (sl.notes || "") + "\n" + (sl.myNote || "")).toLowerCase()
                    if (!words.every(w => text.indexOf(w) >= 0) || rows.length >= 300)
                        return
                    // One hit for a slide shown in several steps.
                    if (sl.title && sl.title === lastTitle && rows.length && rows[rows.length - 1].deck === d)
                        return
                    lastTitle = sl.title
                    rows.push({ type: "hit", deck: d, slide: i, title: sl.title || "", deckTitle: decks[d].title,
                                links: (sl.links || []).length })
                })
            }
            return rows
        }
        const pushDeck = (d, indent) => {
            const open = isOpen(d)
            rows.push({ type: "deck", deck: d, title: decks[d].title, count: decks[d].slides.length, open: open, indent: indent })
            if (!open)
                return
            if (thumbnails)
                decks[d].slides.forEach((sl, i) => rows.push({ type: "thumb", deck: d, slide: i, title: sl.title || "",
                                                               links: (sl.links || []).length, indent: indent }))
            else
                rows.push(...outlineOf(d).map(r => Object.assign(r, { indent: indent })))
        }
        // Folders first, each with its decks; then decks in no folder.
        for (const f of folders) {
            const inside = []
            decks.forEach((dk, d) => { if ((dk.folder || "") === f) inside.push(d) })
            const open = !closedFolders[f]
            rows.push({ type: "folder", folder: f, count: inside.length,
                        slides: inside.reduce((n, d) => n + decks[d].slides.length, 0), open: open })
            if (open)
                for (const d of inside)
                    pushDeck(d, 1)
        }
        decks.forEach((dk, d) => { if (!dk.folder) pushDeck(d, 0) })
        return rows
    }
    // Keep the current slide in view in the list.
    function revealCurrent() {
        for (let k = 0; k < navRows.length; ++k) {
            const r = navRows[k]
            if (r.type !== "deck" && r.deck === deckIndex && slideIndex >= r.slide
                    && slideIndex <= (r.last !== undefined ? r.last : r.slide)) {
                thumbs.positionViewAtIndex(k, ListView.Contain)
                return
            }
        }
    }
    onNavRowsChanged: Qt.callLater(revealCurrent)

    function go(step) {
        if (!deck)
            return
        const next = slideIndex + step
        if (next >= 0 && next < slides.length)
            slideIndex = next
        else if (next >= slides.length && deckIndex < decks.length - 1) {
            deckIndex += 1
            slideIndex = 0
        } else if (next < 0 && deckIndex > 0) {
            deckIndex -= 1
            slideIndex = Math.max(0, slides.length - 1)
        }
    }

    function slideText(s) {
        if (!s)
            return ""
        return [s.title || ""].concat((s.paragraphs || []).map(p => p.text)).join("\n")
    }

    // --- Sections -------------------------------------------------------

    property var sectionCache: ({})
    function sectionsOf(bookId) {
        if (!sectionCache[bookId]) {
            let raw = workspaces.sections(bookId)
            if (raw.length === 0)
                raw = outlines.entries(bookId)   // the outline Shelf built for a PDF without bookmarks
            sectionCache[bookId] = Numbering.labelled(raw)
        }
        return sectionCache[bookId]
    }
    // The section a passage of the book falls in.
    function sectionFor(bookId, passage) {
        const list = sectionsOf(bookId)
        if (list.length === 0)
            return null
        if (typeof list[0].target === "number") {
            let found = null
            for (const s of list)
                if (s.target <= passage.target)
                    found = s
            return found
        }
        const title = (passage.label || "").toLowerCase()
        return list.find(s => (s.title || s.label).toLowerCase() === title) || null
    }

    // Suggestions: the passages of the workspace's books that best match
    // the slide's words, found locally without any AI.
    Timer {
        id: suggestTimer
        interval: 350
        onTriggered: page.suggest()
    }
    onSlideChanged: { suggestions = []; suggestTimer.restart(); hitTimer.restart() }
    property string asked: ""
    function suggest() {
        const text = slideText(slide).trim()
        asked = text
        if (text === "" || !page.active)
            return
        for (const id of (ws.books || []).slice(0, 4)) {
            const b = library.book(id)
            if (b.id && !b.missing)
                bookSearch.passages({ id: b.id, title: b.title, path: b.path, format: b.format }, text, 3)
        }
    }
    onActiveChanged: if (active && suggestions.length === 0) suggestTimer.restart()
    Connections {
        target: bookSearch
        function onPassagesReady(bookId, question, passages) {
            if (question !== page.asked || (page.ws.books || []).indexOf(bookId) < 0)
                return
            const b = library.book(bookId)
            const linked = (page.slide && page.slide.links) || []
            const next = page.suggestions.filter(s => s.bookId !== bookId)
            for (const p of passages.slice(0, 2)) {
                const section = page.sectionFor(bookId, p)
                const target = section && typeof section.target !== "number" ? section.target
                             : b.format === "pdf" ? p.target : (section ? section.target : p.target)
                if (linked.some(l => l.bookId === bookId && l.target === target))
                    continue
                if (next.some(s => s.bookId === bookId && s.target === target))
                    continue
                next.push({ bookId: bookId, bookTitle: b.title, target: target, score: p.score || 0,
                            label: section ? section.label : (p.label || ""),
                            snippet: page.snippet(p.text, section ? section.title || section.label : p.label) })
            }
            page.suggestions = next.slice(0, 4)
        }
    }

    // A passage's opening words, without the heading it repeats.
    function snippet(text, heading) {
        let t = String(text || "").replace(/\s+/g, " ").trim()
        const h = String(heading || "").replace(/^\S*\d\S*\s+/, "").trim()
        if (h && t.toLowerCase().startsWith(h.toLowerCase()))
            t = t.slice(h.length).trim()
        return t.slice(0, 150)
    }

    function link(entry) {
        if (!deck)
            return
        workspaces.addLink(workspaceId, deck.id, slideIndex, {
            bookId: entry.bookId, target: entry.target, label: entry.label,
            bookTitle: entry.bookTitle || library.book(entry.bookId).title || "", snippet: entry.snippet || ""
        })
        suggestions = suggestions.filter(s => !(s.bookId === entry.bookId && s.target === entry.target))
    }

    // --- Explanations -----------------------------------------------------

    property var lesson: null
    property bool lessonOpen: false
    // The study plan for the next exam, shown in place of the slide.
    property bool planOpen: false
    property string lessonKey: ""
    property var gathering: null     // {input, waiting: [{bookId, from}], texts: []}
    onSlideIndexChanged: { closeLesson(); selection = null; Qt.callLater(revealCurrent) }
    onDeckIndexChanged: { closeLesson(); setOpen(deckIndex, true) }
    function closeLesson() {
        if (lessonOpen)
            voice.stop()
        lessonOpen = false
        if (gathering)
            gathering = null
    }

    // What a lesson is made from; the same slide and links give the same
    // lesson, so it's written once.
    function lessonInput(question) {
        const s = slide
        const links = s.links || []
        return {
            key: workspaceId + "/" + deck.id + "/" + slideIndex + (question ? "?" + question : ""),
            title: s.title || "",
            text: slideText(s),
            notes: s.notes || "",
            question: question || "",
            sources: links.map(l => l.bookId + ":" + l.target),
            passages: []
        }
    }
    property int lessonsWritten: 0
    readonly property bool hasLesson: lessonsWritten >= 0 && slide !== null && explainerKnows(slide)
    function explainerKnows(s) {
        return s !== null && Object.keys(explainer.cached(lessonInput(""))).length > 0
    }

    function explainSlide(question) {
        if (!slide)
            return
        const input = lessonInput(question)
        lessonKey = input.key
        const known = explainer.cached(input)
        if (Object.keys(known).length > 0) {
            lesson = known
            lessonOpen = true
            workspaces.addHistory(workspaceId, deck.id, slideIndex, question || "")
            return
        }
        // Fetch the linked sections' text first, so the lesson follows the books.
        const waiting = []
        for (const l of (slide.links || []).slice(0, 3)) {
            const b = library.book(l.bookId)
            if (!b.id || b.missing)
                continue
            const list = sectionsOf(l.bookId)
            const at = list.findIndex(e => e.target === l.target)
            let to = ""
            if (at >= 0) {
                for (let k = at + 1; k < list.length; ++k)
                    if (list[k].depth <= list[at].depth) { to = list[k].target; break }
            } else if (typeof l.target === "number") {
                to = l.target + (b.format === "pdf" ? 3 : 1)
            }
            waiting.push({ bookId: l.bookId, from: l.target, label: (b.title || "") + ", " + (l.label || "") })
            bookSearch.chapterText({ id: b.id, title: b.title, path: b.path, format: b.format }, l.target, to)
        }
        gathering = { input: input, waiting: waiting, texts: [] }
        if (waiting.length === 0)
            sendLesson()
        else
            gatherTimer.restart()
    }
    Timer {
        id: gatherTimer
        interval: 8000
        onTriggered: page.sendLesson()
    }
    Connections {
        target: bookSearch
        function onTextReady(bookId, from, text) {
            const g = page.gathering
            if (!g)
                return
            const i = g.waiting.findIndex(w => w.bookId === bookId && String(w.from) === String(from))
            if (i < 0)
                return
            g.texts.push({ label: g.waiting[i].label, text: page.relevant(text, g.input.text + "\n" + g.input.notes, 1800) })
            g.waiting.splice(i, 1)
            if (g.waiting.length === 0)
                page.sendLesson()
        }
    }
    function sendLesson() {
        gatherTimer.stop()
        const g = gathering
        if (!g)
            return
        gathering = null
        g.input.passages = g.texts
        explainer.explain(g.input)
    }
    // The paragraphs of a section that share the most words with the
    // slide, kept in their order, up to `limit` characters.
    function relevant(text, query, limit) {
        const words = new Set(String(query).toLowerCase().match(/[a-z]{4,}/g) || [])
        const paragraphs = String(text || "").split(/\n+/).map(p => p.trim()).filter(p => p.length > 0)
        const scored = paragraphs.map((p, i) => ({ i: i, p: p,
            score: (p.toLowerCase().match(/[a-z]{4,}/g) || []).filter(w => words.has(w)).length }))
        const picked = []
        let used = 0
        for (const s of scored.slice().sort((a, b) => b.score - a.score)) {
            if (used + s.p.length > limit && picked.length > 0)
                continue
            picked.push(s)
            used += s.p.length
            if (used >= limit)
                break
        }
        return picked.sort((a, b) => a.i - b.i).map(s => s.p).join("\n").slice(0, limit)
    }
    Connections {
        target: explainer
        function onReady(key, lesson) {
            page.lessonsWritten += 1
            if (key !== page.lessonKey)
                return
            page.lesson = lesson
            page.lessonOpen = true
            const parts = key.split("/")
            if (parts.length >= 3 && parts[0] === page.workspaceId) {
                const rest = parts.slice(2).join("/")
                const q = rest.indexOf("?") >= 0 ? rest.slice(rest.indexOf("?") + 1) : ""
                workspaces.addHistory(page.workspaceId, parts[1], parseInt(rest), q)
            }
        }
    }
    readonly property bool writing: (explainer.busy && explainer.pending === lessonKey && lessonKey !== "") || gathering !== null

    // The voice for a sentence; `now` when the lesson is waiting for it.
    function speak(scene, sentence, text, now) {
        if (typeof voice === "undefined" || !voice.ready) {
            if (now)
                player.voiceReady(scene, sentence, "")
            return
        }
        voice.say(lessonKey, scene, sentence, text, now)
    }

    Connections {
        target: voice
        function onSpoken(key, scene, sentence, url) {
            if (key === page.lessonKey && page.lessonOpen)
                player.voiceReady(scene, sentence, url)
        }
    }

    // The notes box shows the slide's note; what's typed is saved a moment
    // later, and before moving to another slide.
    readonly property string noteKey: deck && slide ? deck.id + "/" + slideIndex : ""
    function saveNote() {
        noteSave.stop()
        if (!myNote.dirty || myNote.key === "")
            return
        const parts = myNote.key.split("/")
        workspaces.setSlideNote(workspaceId, parts[0], parseInt(parts[1]), myNote.text)
        myNote.dirty = false
    }
    onNoteKeyChanged: {
        saveNote()
        myNote.key = noteKey
        myNote.text = slide ? slide.myNote || "" : ""
        myNote.dirty = false
    }
    Timer {
        id: noteSave
        interval: 700
        onTriggered: page.saveNote()
    }

    // --- Text on slides ----------------------------------------------------

    // The selected text and its boxes in PDF points: {text, boxes: [{x, y, width, height}]}.
    property var selection: null
    function select(from, to, unit) {
        if (!deck || !deck.pdfPath)
            return
        const r = pdfGeometry.select(deck.pdfPath, slideIndex, from, to, unit)
        if (!r || !r.text) {
            if (unit !== "drag")
                selection = null
            return
        }
        // Each line's corners into a box.
        const boxes = (r.rects || []).map(flat => {
            let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9
            for (let k = 0; k + 1 < flat.length; k += 2) {
                x0 = Math.min(x0, flat[k]); x1 = Math.max(x1, flat[k])
                y0 = Math.min(y0, flat[k + 1]); y1 = Math.max(y1, flat[k + 1])
            }
            return { x: x0, y: y0, width: x1 - x0, height: y1 - y0 }
        })
        selection = { text: r.text, boxes: boxes }
    }
    function useSelection(what) {
        if (!selection)
            return
        const text = selection.text
        if (what === "copy") {
            annotations.copyToClipboard(text)
            win.showToast(qsTr("Copied."))
        } else if (what === "note") {
            const before = slide.myNote || ""
            workspaces.setSlideNote(workspaceId, deck.id, slideIndex, (before ? before + "\n" : "") + "“" + text.trim() + "”")
            myNote.text = (before ? before + "\n" : "") + "“" + text.trim() + "”"
            win.showToast(qsTr("Added to your notes on this slide."))
        } else if (what === "explain") {
            win.explainVisually({ key: "slide-text:" + workspaceId + ":" + text.length + ":" + text.slice(0, 60),
                                  title: slide.title || "", text: text, notes: "",
                                  passages: [{ label: deck.title, text: slideText(slide) }], sources: [], question: "" })
        }
        selection = null
    }

    // Finding text in the deck (Ctrl+F): matches are highlighted, and
    // Enter goes from one to the next across the deck's slides.
    property string findText: ""
    property bool findOpen: false
    PdfSearchModel {
        id: slideSearch
        document: pdf
        searchString: page.findText
    }
    // The found words on the slide shown, worked out when the search or the
    // slide changes (asking inside a binding makes the search run again).
    property var hitPaths: []
    function refreshHits() { hitPaths = findText !== "" ? slideSearch.boundingPolygonsOnPage(slideIndex) : [] }
    Timer {
        id: hitTimer
        interval: 60
        onTriggered: page.refreshHits()
    }
    Connections {
        target: slideSearch
        function onCountChanged() { hitTimer.restart() }
    }
    onFindTextChanged: hitTimer.restart()
    function findNext(step) {
        if (slideSearch.count <= 0)
            return
        slideSearch.currentResult = (slideSearch.currentResult + step + slideSearch.count) % slideSearch.count
        slideIndex = slideSearch.currentPage
    }
    function closeFind() {
        findOpen = false
        findText = ""
        page.takeKeys()
    }
    // A slide opened from the study plan, with the topic's words lit up:
    // the first of them that the slide holds.
    function showSlideFor(d, i, words) {
        showSlide(d, i, false)
        const text = slideText(decks[d].slides[i]).toLowerCase()
        const hit = (words || []).find(w => w && text.indexOf(String(w).toLowerCase()) >= 0)
        if (hit && decks[d].pdf) {
            findText = hit
            findOpen = true
        }
    }

    // --- Keys -----------------------------------------------------------

    Shortcut { sequences: ["Right", "PgDown", "Space"]; enabled: page.active && !page.typing && !page.lessonOpen; onActivated: page.go(1) }
    Shortcut { sequences: ["Left", "PgUp", "Shift+Space"]; enabled: page.active && !page.typing && !page.lessonOpen; onActivated: page.go(-1) }
    Shortcut { sequence: "Home"; enabled: page.active && !page.typing; onActivated: page.slideIndex = 0 }
    Shortcut { sequence: "End"; enabled: page.active && !page.typing; onActivated: page.slideIndex = Math.max(0, page.slides.length - 1) }
    Shortcut { sequence: "/"; enabled: page.active && !page.typing; onActivated: slideFilter.forceActiveFocus() }
    // 1 shows the slides, 2 the study plan (Tab stays for moving between
    // buttons without a mouse).
    Shortcut {
        sequence: "1"
        enabled: page.active && page.planOpen && !studyPlan.typing
        onActivated: page.planOpen = false
    }
    Shortcut {
        sequence: "2"
        enabled: page.active && !page.planOpen && !page.typing
        onActivated: { page.closeLesson(); page.planOpen = true }
    }
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: page.active && !page.planOpen && !page.lessonOpen && !!(page.deck && page.deck.pdf)
        onActivated: { page.findOpen = true; Qt.callLater(() => { findField.forceActiveFocus(); findField.selectAll() }) }
    }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: page.active && page.selection !== null && !page.typing
        onActivated: page.useSelection("copy")
    }
    onPlanOpenChanged: Qt.callLater(() => page.takeKeys())
    Shortcut { sequence: "["; enabled: page.active && !page.typing; onActivated: page.toggleList() }
    Shortcut { sequence: "]"; enabled: page.active && !page.typing; onActivated: page.togglePanel() }
    Shortcut { sequence: "T"; enabled: page.active && !page.typing && page.decks.length > 0 && ollama.status === "ready"; onActivated: page.practice("deck") }
    Shortcut { sequence: "L"; enabled: page.active && !page.typing && page.slide !== null; onActivated: linkPopup.open() }
    Shortcut { sequence: "E"; enabled: page.active && !page.typing && page.slide !== null && !page.lessonOpen; onActivated: page.explainSlide("") }
    Shortcut { sequence: "Escape"; enabled: page.active && page.lessonOpen; onActivated: page.closeLesson() }
    Shortcut { sequence: "Escape"; enabled: page.active && page.picking && !page.lessonOpen; onActivated: page.stopPicking() }
    Shortcut { sequence: "Escape"; enabled: page.active && !page.picking && !page.lessonOpen && (page.findOpen || page.selection !== null); onActivated: { page.selection = null; page.closeFind() } }

    PdfDocument {
        id: pdf
        source: page.deck && page.deck.pdf ? page.deck.pdf : ""
    }

    // Where the keys go when no text box is in use. Focusing the page itself
    // would hand them straight back to whichever box had them last.
    Item {
        id: keySink
        focus: true
    }
    function takeKeys() { keySink.forceActiveFocus() }

    Rectangle {
        anchors.fill: parent
        color: "#141418"
        // A click anywhere that isn't a text box gives the keys back to the
        // workspace (1, 2, arrows, E, T…), from the slide-number box say.
        MouseArea {
            anchors.fill: parent
            onPressed: mouse => { page.takeKeys(); mouse.accepted = false }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The workspace's name, and adding slides.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 52
            color: "#18181d"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                spacing: 10
                Icon { name: "slides"; size: 18; color: theme.accent }
                TextField {
                    id: titleField
                    Layout.preferredWidth: Math.min(420, Math.max(160, contentWidth + 30))
                    text: page.ws.name || ""
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    background: Rectangle {
                        color: titleField.activeFocus ? "#101014" : "transparent"
                        border.color: titleField.activeFocus ? theme.accent : "transparent"
                        radius: Math.min(4, theme.radius)
                    }
                    topPadding: 4; bottomPadding: 4; leftPadding: 6
                    onEditingFinished: {
                        if (text.trim() !== "" && text.trim() !== page.ws.name)
                            workspaces.rename(page.workspaceId, text.trim())
                        page.takeKeys()
                    }
                    Keys.onEscapePressed: { text = page.ws.name || ""; page.takeKeys() }
                }
                Label {
                    visible: page.decks.length > 0
                    text: qsTr("%n slide(s)", "", page.decks.reduce((n, d) => n + d.slides.length, 0))
                    opacity: 0.5
                    font.pixelSize: 12
                }
                Item { Layout.fillWidth: true }
                BusyIndicator {
                    visible: workspaces.importing !== ""
                    running: visible
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                }
                Label {
                    visible: workspaces.importing !== ""
                    text: workspaces.importing
                    opacity: 0.7
                    font.pixelSize: 12
                }
                ToolbarButton {
                    iconName: "notes"
                    label: qsTr("Study plan")
                    checkable: true
                    checked: page.planOpen
                    tip: qsTr("Paste your teacher's exam review and get a plan for it (2; 1 goes back to the slides)")
                    onClicked: { page.closeLesson(); page.planOpen = !page.planOpen }
                }
                ToolbarButton {
                    iconName: "outline"
                    checkable: true
                    checked: page.listOpen
                    tip: qsTr("Show or hide the slide list ([)")
                    onClicked: page.toggleList()
                }
                ToolbarButton {
                    iconName: "panel-right"
                    checkable: true
                    checked: page.panelOpen
                    tip: qsTr("Show or hide explanations, links and notes (])")
                    onClicked: page.togglePanel()
                }
                ToolbarButton {
                    iconName: "library"
                    label: qsTr("Books")
                    tip: qsTr("The books Shelf suggests sections from")
                    onClicked: booksPopup.open()
                }
                ToolbarButton {
                    iconName: "plus"
                    label: qsTr("Add slides")
                    tip: qsTr("PDF or PowerPoint; you can also drop files here")
                    onClicked: workspaces.pickDecks(page.workspaceId)
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#26262c" }
        }

        // Nothing yet: say what goes here.
        Item {
            visible: page.decks.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 40)
                spacing: 14
                Icon { name: "slides"; size: 48; color: theme.accent; Layout.alignment: Qt.AlignHCenter }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Add a course's slides")
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    opacity: 0.65
                    text: qsTr("PDF or PowerPoint files. Link each slide to the sections of your books it comes from, and get explanations with diagrams.")
                }
                Label {
                    visible: !workspaces.hasLibreOffice
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    opacity: 0.45
                    font.pixelSize: 12
                    text: qsTr("PowerPoint slides are shown as their text and pictures. Install LibreOffice to see them exactly as designed.")
                }
                AccentButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Add slides…")
                    onClicked: workspaces.pickDecks(page.workspaceId)
                }
            }
        }

        RowLayout {
            visible: page.decks.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Decks and their slides: an outline of slide titles (repeated
            // titles, as in slides that build up step by step, as one entry),
            // or thumbnails, one deck open at a time; or slides found by
            // their words across every deck.
            Rectangle {
                Layout.preferredWidth: page.listOpen ? 250 : 0
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                visible: Layout.preferredWidth > 0.5
                clip: true
                Layout.fillHeight: true
                color: "#16161b"
                ColumnLayout {
                    width: 250 - 16
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.margins: 8
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        CompactField {
                            id: slideFilter
                            Layout.fillWidth: true
                            hint: qsTr("Find a slide…")
                            Keys.onEscapePressed: { text = ""; page.takeKeys() }
                        }
                        ToolButton {
                            implicitWidth: 30
                            implicitHeight: 32
                            checkable: true
                            checked: page.listMode === "slides" && !page.thumbnails
                            icon.source: "image://icon/outline?color=" + encodeURIComponent(checked ? theme.accent : "#9a98a0")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Slide titles")
                            onClicked: { page.listMode = "slides"; page.setThumbnails(false) }
                        }
                        ToolButton {
                            implicitWidth: 30
                            implicitHeight: 32
                            checkable: true
                            checked: page.listMode === "slides" && page.thumbnails
                            icon.source: "image://icon/pages?color=" + encodeURIComponent(checked ? theme.accent : "#9a98a0")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Thumbnails")
                            onClicked: { page.listMode = "slides"; page.setThumbnails(true) }
                        }
                        ToolButton {
                            visible: page.listMode === "slides"
                            implicitWidth: 30
                            implicitHeight: 32
                            text: page.anythingOpen ? "⊟" : "⊞"
                            font.pixelSize: 16
                            ToolTip.visible: hovered
                            ToolTip.text: page.anythingOpen ? qsTr("Collapse all") : qsTr("Expand all folders")
                            onClicked: page.anythingOpen ? page.collapseAll() : page.expandAll()
                        }
                        ToolButton {
                            implicitWidth: 30
                            implicitHeight: 32
                            checkable: true
                            checked: page.listMode === "history"
                            icon.source: "image://icon/history?color=" + encodeURIComponent(checked ? theme.accent : "#9a98a0")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Explanations you've watched")
                            onClicked: page.listMode = "history"
                        }
                    }
                    // Picking decks: what to do with them.
                    RowLayout {
                        visible: page.listMode === "slides"
                        Layout.fillWidth: true
                        spacing: 4
                        Button {
                            flat: true
                            font.pixelSize: 12
                            text: page.picking ? qsTr("Done") : qsTr("Select decks")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Pick decks to put in a folder or be tested on together (or Ctrl-click and Shift-click them)")
                            onClicked: page.picking ? page.stopPicking() : (page.picking = true)
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            flat: true
                            font.pixelSize: 12
                            text: qsTr("New folder…")
                            onClicked: page.pickedIds.length > 0
                                       ? nameDialog.ask(qsTr("New folder for %n deck(s)", "", page.pickedIds.length), "", name => page.movePicked(name))
                                       : nameDialog.ask(qsTr("New folder"), "", name => { page.newFolderName = name; page.picking = true })
                        }
                    }
                    Rectangle {
                        visible: page.picking
                        Layout.fillWidth: true
                        implicitHeight: pickColumn.implicitHeight + 16
                        radius: Math.min(6, theme.radius)
                        color: Qt.alpha(theme.accent, 0.12)
                        border.color: Qt.alpha(theme.accent, 0.4)
                        ColumnLayout {
                            id: pickColumn
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 4
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 12
                                text: page.pickedIds.length === 0
                                      ? (page.newFolderName !== "" ? qsTr("Tick the decks to put in “%1”.").arg(page.newFolderName)
                                                                   : qsTr("Tick the decks you want."))
                                      : qsTr("%n deck(s) picked", "", page.pickedIds.length)
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                visible: page.pickedIds.length > 0
                                Button {
                                    visible: page.newFolderName !== ""
                                    highlighted: true
                                    font.pixelSize: 12
                                    text: qsTr("Put in “%1”").arg(page.newFolderName)
                                    onClicked: { page.movePicked(page.newFolderName); page.newFolderName = "" }
                                }
                                Button {
                                    font.pixelSize: 12
                                    text: qsTr("Into a folder ▾")
                                    onClicked: pickedFolderMenu.popup(this, 0, height)
                                }
                                Button {
                                    font.pixelSize: 12
                                    text: qsTr("Test on these")
                                    enabled: ollama.status === "ready"
                                    onClicked: {
                                        const indexes = page.pickedIds.map(id => page.decks.findIndex(d => d.id === id)).filter(i => i >= 0)
                                        win.practiceOnSlides(page.ws.name || "", [{ key: "picked",
                                            label: qsTr("%n picked deck(s)", "", indexes.length), text: page.testText(page.slidesOf(indexes)) }], 0)
                                    }
                                }
                            }
                        }
                    }
                    Menu {
                        id: pickedFolderMenu
                        Material.roundedScale: theme.radius
                        background: Rectangle { implicitWidth: 200; color: "#202027"; radius: Math.min(6, theme.radius); border.color: "#2c2c33" }
                        Repeater {
                            model: page.folders
                            delegate: MenuItem {
                                required property string modelData
                                text: modelData
                                onTriggered: page.movePicked(modelData)
                            }
                        }
                        MenuItem {
                            text: qsTr("New folder…")
                            onTriggered: nameDialog.ask(qsTr("New folder for %n deck(s)", "", page.pickedIds.length), "", name => page.movePicked(name))
                        }
                        MenuItem {
                            text: qsTr("Out of their folders")
                            onTriggered: page.movePicked("")
                        }
                    }

                    // History: in course order or newest first.
                    RowLayout {
                        visible: page.listMode === "history"
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("%n explanation(s)", "", page.historyRows.length)
                            font.pixelSize: 11
                            opacity: 0.55
                        }
                        Button {
                            flat: true
                            font.pixelSize: 11
                            text: page.historyRecent ? qsTr("Newest first") : qsTr("Course order")
                            onClicked: {
                                page.historyRecent = !page.historyRecent
                                library.setSetting("workspaceHistoryRecent", page.historyRecent)
                            }
                        }
                    }
                    ListView {
                        id: thumbs
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        model: page.navRows
                        // Only what's on screen is made, so a 300-slide deck costs nothing.
                        reuseItems: true
                        delegate: Item {
                            id: row
                            required property var modelData
                            required property int index
                            readonly property var r: modelData
                            readonly property bool current: r.type !== "deck" && r.deck === page.deckIndex
                                                            && page.slideIndex >= r.slide && page.slideIndex <= (r.last !== undefined ? r.last : r.slide)
                            width: thumbs.width - 10   // room for the scroll bar
                            readonly property int indent: (r.indent || 0) * 14
                            height: r.type === "history" ? 54 : r.type === "folder" ? 40 : r.type === "deck" ? 40 : r.type === "thumb" ? (width - 8) * 9 / 16 + 8 : r.type === "hit" ? 50 : 30

                            // An explanation watched: click to watch it again.
                            Rectangle {
                                visible: row.r.type === "history"
                                anchors.fill: parent
                                radius: Math.min(5, theme.radius)
                                color: historyMouse.containsMouse ? "#1e1e25" : "transparent"
                                MouseArea {
                                    id: historyMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        page.showSlide(row.r.deck, row.r.slide, false)
                                        Qt.callLater(() => page.explainSlide(row.r.question))
                                    }
                                }
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 6
                                    spacing: 1
                                    Label {
                                        Layout.fillWidth: true
                                        text: row.r.type === "history" ? (row.r.question ? "“" + row.r.question + "”" : row.r.title) : ""
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: row.r.type === "history"
                                              ? (row.r.question ? row.r.title + " · " : "") + qsTr("%1 · slide %2").arg(row.r.deckTitle).arg(row.r.slide + 1)
                                              : ""
                                        elide: Text.ElideRight
                                        font.pixelSize: 10
                                        opacity: 0.5
                                    }
                                    Label {
                                        text: row.r.type === "history" ? page.ago(row.r.time) : ""
                                        font.pixelSize: 10
                                        opacity: 0.4
                                    }
                                }
                            }

                            // A folder of decks: click to fold it.
                            Rectangle {
                                visible: row.r.type === "folder"
                                anchors.fill: parent
                                anchors.topMargin: 4
                                radius: Math.min(5, theme.radius)
                                color: folderMouse.containsMouse ? "#202027" : "transparent"
                                MouseArea {
                                    id: folderMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: mouse => {
                                        if (mouse.button === Qt.RightButton) {
                                            folderMenu.folder = row.r.folder
                                            folderMenu.popup()
                                        } else {
                                            page.toggleFolder(row.r.folder)
                                        }
                                    }
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    spacing: 6
                                    Label { text: row.r.open ? "▾" : "▸"; opacity: 0.6; font.pixelSize: 11 }
                                    Icon { name: "library"; size: 14; color: theme.accent }
                                    Label {
                                        Layout.fillWidth: true
                                        text: row.r.folder || ""
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                    }
                                    Label {
                                        text: row.r.slides || 0
                                        opacity: 0.45
                                        font.pixelSize: 11
                                    }
                                    ToolButton {
                                        text: "⋯"
                                        implicitWidth: 26
                                        implicitHeight: 26
                                        onClicked: { folderMenu.folder = row.r.folder; folderMenu.popup() }
                                    }
                                }
                            }

                            // A deck: click to open it (or close the open one).
                            Rectangle {
                                visible: row.r.type === "deck"
                                anchors.fill: parent
                                anchors.topMargin: 4
                                anchors.leftMargin: row.indent
                                radius: Math.min(5, theme.radius)
                                color: deckMouse.containsMouse ? "#202027" : row.r.deck === page.deckIndex ? "#1c1c23" : "transparent"
                                MouseArea {
                                    id: deckMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: mouse => {
                                        if (mouse.button === Qt.RightButton) {
                                            deckMenu.deck = page.decks[row.r.deck]
                                            deckMenu.index = row.r.deck
                                            deckMenu.popup()
                                        } else if (page.picking || (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                                            page.togglePick(row.r.deck, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                                        } else {
                                            page.openDeckAt(row.r.deck)
                                        }
                                    }
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    spacing: 6
                                    // Ticked when picked.
                                    Rectangle {
                                        visible: page.picking
                                        width: 16; height: 16
                                        radius: 3
                                        color: page.picked[page.decks[row.r.deck] ? page.decks[row.r.deck].id : ""] ? theme.accent : "transparent"
                                        border.color: page.picked[page.decks[row.r.deck] ? page.decks[row.r.deck].id : ""] ? theme.accent : "#6a6872"
                                        Label {
                                            anchors.centerIn: parent
                                            visible: !!page.picked[page.decks[row.r.deck] ? page.decks[row.r.deck].id : ""]
                                            text: "✓"
                                            font.pixelSize: 11
                                            font.weight: Font.Bold
                                            color: "#111"
                                        }
                                    }
                                    Label {
                                        visible: !page.picking
                                        text: row.r.open ? "▾" : "▸"
                                        opacity: 0.6
                                        font.pixelSize: 11
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: row.r.title || ""
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: row.r.deck === page.deckIndex ? theme.accent : "#d8d8de"
                                    }
                                    Label {
                                        text: row.r.count || 0
                                        opacity: 0.45
                                        font.pixelSize: 11
                                    }
                                    ToolButton {
                                        text: "⋯"
                                        implicitWidth: 26
                                        implicitHeight: 26
                                        onClicked: {
                                            deckMenu.deck = page.decks[row.r.deck]
                                            deckMenu.index = row.r.deck
                                            deckMenu.popup()
                                        }
                                    }
                                }
                            }

                            // A slide title in the outline, or a slide found.
                            Rectangle {
                                visible: row.r.type === "entry" || row.r.type === "hit"
                                anchors.fill: parent
                                anchors.leftMargin: row.indent
                                radius: Math.min(4, theme.radius)
                                color: row.current ? Qt.alpha(theme.accent, 0.16) : entryMouse.containsMouse ? "#1e1e25" : "transparent"
                                MouseArea {
                                    id: entryMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: page.showSlide(row.r.deck, row.r.slide, row.r.type === "hit")
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: row.r.type === "hit" ? 8 : 16
                                    anchors.rightMargin: 6
                                    spacing: 8
                                    Label {
                                        visible: row.r.type === "entry"
                                        Layout.preferredWidth: 26
                                        horizontalAlignment: Text.AlignRight
                                        text: row.r.slide + 1
                                        font.pixelSize: 11
                                        font.features: { "tnum": 1 }
                                        opacity: 0.45
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0
                                        Label {
                                            Layout.fillWidth: true
                                            text: row.r.title || qsTr("Slide %1").arg(row.r.slide + 1)
                                            elide: Text.ElideRight
                                            font.pixelSize: 12
                                            color: row.current ? theme.accent : "#e2e0e6"
                                        }
                                        Label {
                                            visible: row.r.type === "hit"
                                            Layout.fillWidth: true
                                            text: row.r.type === "hit" ? qsTr("%1 · slide %2").arg(row.r.deckTitle).arg(row.r.slide + 1) : ""
                                            elide: Text.ElideRight
                                            font.pixelSize: 10
                                            opacity: 0.5
                                        }
                                    }
                                    Label {
                                        visible: (row.r.count || 1) > 1
                                        text: "×" + row.r.count
                                        font.pixelSize: 10
                                        opacity: 0.45
                                        ToolTip.visible: stepsHover.hovered
                                        ToolTip.text: qsTr("%n slide(s) with this title, as it builds up", "", row.r.count || 1)
                                        HoverHandler { id: stepsHover }
                                    }
                                    Rectangle {
                                        visible: (row.r.links || 0) > 0
                                        width: 8; height: 8; radius: 4
                                        color: theme.accent
                                    }
                                }
                            }

                            // A thumbnail.
                            Rectangle {
                                id: thumb
                                visible: row.r.type === "thumb"
                                anchors.fill: parent
                                anchors.margins: 4
                                anchors.leftMargin: 12 + row.indent
                                radius: Math.min(4, theme.radius)
                                readonly property bool live: row.r.deck === page.deckIndex && !!(page.deck && page.deck.pdf)
                                color: live ? "white" : "#202027"
                                border.width: row.current ? 2 : 1
                                border.color: row.current ? theme.accent : "#2c2c33"
                                clip: true
                                Loader {
                                    anchors.fill: parent
                                    anchors.margins: thumb.border.width
                                    active: row.r.type === "thumb" && thumb.live
                                    sourceComponent: PdfPageImage {
                                        document: pdf
                                        currentFrame: row.r.slide
                                        asynchronous: true
                                        fillMode: Image.PreserveAspectFit
                                        sourceSize.width: 220
                                    }
                                }
                                Label {
                                    visible: !thumb.live
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    text: row.r.title || ""
                                    wrapMode: Text.WordWrap
                                    elide: Text.ElideRight
                                    font.pixelSize: 11
                                    color: "#d8d8de"
                                }
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 4
                                    width: numberLabel.implicitWidth + 8
                                    height: 16
                                    radius: 3
                                    color: "#000000a0"
                                    Label {
                                        id: numberLabel
                                        anchors.centerIn: parent
                                        text: row.r.slide + 1
                                        font.pixelSize: 10
                                        color: "white"
                                    }
                                }
                                Rectangle {
                                    visible: (row.r.links || 0) > 0
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 4
                                    width: 10; height: 10; radius: 5
                                    color: theme.accent
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: page.showSlide(row.r.deck, row.r.slide, false)
                                }
                            }
                        }
                        Label {
                            visible: page.listMode === "history" && page.historyRows.length === 0
                            anchors.centerIn: parent
                            width: parent.width - 20
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr("Explanations you watch are listed here, to watch again.")
                            opacity: 0.5
                            font.pixelSize: 12
                        }
                        Label {
                            visible: page.listMode === "slides" && page.navRows.length === 0 && slideFilter.text.trim() !== ""
                            anchors.centerIn: parent
                            width: parent.width - 20
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr("No slide mentions that.")
                            opacity: 0.5
                            font.pixelSize: 12
                        }
                    }
                }
            }

            // The slide, or its lesson.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                StudyPlan {
                    id: studyPlan
                    anchors.fill: parent
                    z: 3
                    visible: page.planOpen
                    workspaceId: page.workspaceId
                    ws: page.ws
                    decks: page.decks
                    onCloseRequested: page.planOpen = false
                    onOpenSlide: (d, i, words) => { page.planOpen = false; page.showSlideFor(d, i, words) }
                    onFocusRequested: page.takeKeys()
                    onPractise: (title, choices) => win.practiceOnSlides(title, choices, 0)
                }
                LessonPlayer {
                    id: player
                    anchors.fill: parent
                    visible: page.lessonOpen
                    lesson: page.lessonOpen ? page.lesson : null
                    canAsk: ollama.status === "ready"
                    onAskRequested: question => page.explainSlide(question)
                    onCloseRequested: page.closeLesson()
                    onSpeakRequested: (scene, sentence, text) => page.speak(scene, sentence, text, true)
                    onPrepareRequested: (scene, sentence, text) => page.speak(scene, sentence, text, false)
                }
                // Writing the lesson.
                Rectangle {
                    visible: page.writing && !page.lessonOpen
                    anchors.centerIn: parent
                    width: writingColumn.implicitWidth + 48
                    height: writingColumn.implicitHeight + 36
                    radius: theme.radius
                    color: "#1e1e25"
                    border.color: "#2c2c33"
                    z: 2
                    ColumnLayout {
                        id: writingColumn
                        anchors.centerIn: parent
                        spacing: 10
                        BusyIndicator { running: parent.visible; Layout.alignment: Qt.AlignHCenter }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: page.gathering ? qsTr("Reading the linked sections…") : qsTr("Writing the lesson…")
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("The local AI usually takes under a minute.")
                            opacity: 0.6
                            font.pixelSize: 12
                        }
                        Button {
                            Layout.alignment: Qt.AlignHCenter
                            flat: true
                            text: qsTr("Stop")
                            onClicked: { page.gathering = null; explainer.cancel() }
                        }
                    }
                }
                ColumnLayout {
                    visible: !page.lessonOpen
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12
                    Item {
                        id: stage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        // Find in this deck, floating over the slide's top corner.
                        Rectangle {
                            visible: page.findOpen
                            z: 5
                            anchors.top: parent.top
                            anchors.right: parent.right
                            width: findRow.implicitWidth + 16
                            height: findRow.implicitHeight + 12
                            radius: Math.min(8, theme.radius)
                            color: "#1e1e25"
                            border.color: "#2c2c33"
                            RowLayout {
                                id: findRow
                                anchors.centerIn: parent
                                spacing: 4
                            Rectangle {
                                Layout.preferredWidth: 220
                                implicitHeight: 30
                                radius: Math.min(5, theme.radius)
                                color: "#101014"
                                border.color: findField.activeFocus ? theme.accent : "#2c2c33"
                                TextInput {
                                    id: findField
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    verticalAlignment: TextInput.AlignVCenter
                                    color: "#e8e6ea"
                                    font.pixelSize: 13
                                    selectByMouse: true
                                    clip: true
                                    text: page.findText
                                    onTextEdited: page.findText = text
                                    Keys.onReturnPressed: event => page.findNext(event.modifiers & Qt.ShiftModifier ? -1 : 1)
                                    Keys.onEscapePressed: page.closeFind()
                                    Label {
                                        visible: findField.text === ""
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Find in these slides")
                                        opacity: 0.4
                                        font.pixelSize: 13
                                    }
                                }
                            }
                            Label {
                                text: page.findText === "" ? "" : slideSearch.count === 0 ? qsTr("No match")
                                      : qsTr("%1 of %2").arg(slideSearch.currentResult + 1).arg(slideSearch.count)
                                font.pixelSize: 12
                                opacity: 0.6
                            }
                            ToolButton { text: "‹"; font.pixelSize: 16; onClicked: page.findNext(-1) }
                            ToolButton { text: "›"; font.pixelSize: 16; onClicked: page.findNext(1) }
                            ToolButton { text: "✕"; onClicked: page.closeFind() }
                        }
                        }
                        // The wheel turns slides, as it turns pages in a PDF: a
                        // notch, or a short swipe on a touchpad, is one slide.
                        WheelHandler {
                            id: slideWheel
                            acceptedDevices: PointerDevice.AllDevices
                            property real pending: 0
                            onWheel: event => {
                                pending += event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y * 2
                                if (pending <= -120) { page.go(1); pending = 0 }
                                else if (pending >= 120) { page.go(-1); pending = 0 }
                                wheelRest.restart()
                            }
                        }
                        Timer {
                            id: wheelRest
                            interval: 350
                            onTriggered: slideWheel.pending = 0
                        }
                        // Height over width of the page: a wide slide, or a tall
                        // document page from an ordinary PDF. Either way it fits
                        // inside the space, never over the controls below.
                        readonly property real aspect: {
                            if (!(page.deck && page.deck.pdf && pdf.status === PdfDocument.Ready))
                                return 9 / 16
                            const size = pdf.pagePointSize(page.slideIndex)
                            return size.width > 0 ? size.height / size.width : 9 / 16
                        }
                        readonly property real slideWidth: Math.min(width, height / aspect)
                        clip: true
                        Rectangle {
                            id: slideFrame
                            anchors.centerIn: parent
                            width: stage.slideWidth
                            height: width * stage.aspect
                            color: page.deck && page.deck.pdf ? "white" : "#1c1c23"
                            radius: page.deck && page.deck.pdf ? 0 : Math.min(8, theme.radius)
                            border.color: "#2c2c33"
                            border.width: page.deck && page.deck.pdf ? 0 : 1
                            clip: true
                            PdfPageImage {
                                visible: !!(page.deck && page.deck.pdf)
                                anchors.fill: parent
                                document: visible ? pdf : null
                                currentFrame: page.slideIndex
                                asynchronous: true
                                retainWhileLoading: true
                                fillMode: Image.PreserveAspectFit
                                sourceSize.width: Math.min(2400, slideFrame.width * Screen.devicePixelRatio)
                            }
                            // Text on the slide: found words, the selection, and
                            // the mouse that selects (drag, double click for a
                            // word, triple click for a sentence), as in PDFs.
                            Item {
                                id: textLayer
                                visible: !!(page.deck && page.deck.pdf)
                                anchors.fill: parent
                                readonly property real pageScale: pdf.status === PdfDocument.Ready
                                    ? width / Math.max(1, pdf.pagePointSize(page.slideIndex).width) : 1
                                Shape {
                                    anchors.fill: parent
                                    visible: page.findText !== ""
                                    ShapePath {
                                        strokeWidth: -1
                                        fillColor: "#66f5d547"
                                        scale: Qt.size(textLayer.pageScale, textLayer.pageScale)
                                        PathMultiline { paths: page.hitPaths }
                                    }
                                }
                                Repeater {
                                    model: page.selection ? page.selection.boxes : []
                                    delegate: Rectangle {
                                        required property var modelData
                                        x: modelData.x * textLayer.pageScale
                                        y: modelData.y * textLayer.pageScale
                                        width: modelData.width * textLayer.pageScale
                                        height: modelData.height * textLayer.pageScale
                                        color: Qt.alpha(theme.accent, 0.35)
                                    }
                                }
                                // What to do with the selected text.
                                Row {
                                    visible: page.selection !== null && !selectMouse.pressed
                                    z: 3
                                    spacing: 2
                                    readonly property var first: page.selection && page.selection.boxes.length ? page.selection.boxes[0] : null
                                    x: first ? Math.max(0, Math.min(textLayer.width - width, first.x * textLayer.pageScale)) : 0
                                    y: first ? Math.max(0, first.y * textLayer.pageScale - height - 6) : 0
                                    Repeater {
                                        model: [
                                            { key: "copy", label: qsTr("Copy") },
                                            { key: "note", label: qsTr("Add to my notes") },
                                            { key: "explain", label: qsTr("Explain visually") }
                                        ]
                                        delegate: Button {
                                            required property var modelData
                                            text: modelData.label
                                            font.pixelSize: 12
                                            padding: 6
                                            visible: modelData.key !== "explain" || ollama.status === "ready"
                                            background: Rectangle { color: parent.hovered ? "#33333d" : "#24242b"; radius: Math.min(5, theme.radius) }
                                            onClicked: page.useSelection(modelData.key)
                                        }
                                    }
                                }
                                MouseArea {
                                    id: selectMouse
                                    anchors.fill: parent
                                    cursorShape: Qt.IBeamCursor
                                    property point start
                                    property int clicks: 0
                                    property real lastClick: 0
                                    function toPage(m) { return Qt.point(m.x / textLayer.pageScale, m.y / textLayer.pageScale) }
                                    onPressed: mouse => {
                                        page.takeKeys()
                                        const now = Date.now()
                                        clicks = now - lastClick < 400 ? clicks + 1 : 1
                                        lastClick = now
                                        start = toPage(mouse)
                                        if (clicks >= 2)
                                            page.select(start, start, clicks >= 3 ? "sentence" : "word")
                                        else
                                            page.selection = null
                                    }
                                    onPositionChanged: mouse => {
                                        if (pressed && clicks < 2)
                                            page.select(start, toPage(mouse), "drag")
                                    }
                                }
                            }
                            // PowerPoint without LibreOffice: the slide as a card.
                            ColumnLayout {
                                visible: !!(page.deck && !page.deck.pdf)
                                anchors.fill: parent
                                anchors.margins: Math.max(18, slideFrame.width * 0.05)
                                spacing: slideFrame.width * 0.015
                                Label {
                                    Layout.fillWidth: true
                                    text: page.slide ? page.slide.title || "" : ""
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: Math.max(16, slideFrame.width * 0.04)
                                    font.weight: Font.DemiBold
                                    color: theme.accent
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 16
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignTop
                                        spacing: slideFrame.width * 0.008
                                        Repeater {
                                            model: page.slide && !page.deck.pdf ? page.slide.paragraphs : []
                                            delegate: Label {
                                                required property var modelData
                                                Layout.fillWidth: true
                                                Layout.leftMargin: modelData.level * slideFrame.width * 0.03
                                                text: (modelData.level === 0 ? "•  " : "–  ") + modelData.text
                                                wrapMode: Text.WordWrap
                                                font.pixelSize: Math.max(12, slideFrame.width * (modelData.level === 0 ? 0.024 : 0.021))
                                                color: modelData.level === 0 ? "#e8e6ea" : "#bdbbc4"
                                            }
                                        }
                                    }
                                    ColumnLayout {
                                        visible: page.slide && (page.slide.images || []).length > 0
                                        Layout.preferredWidth: slideFrame.width * 0.36
                                        Layout.preferredHeight: slideFrame.height * 0.6
                                        Repeater {
                                            model: page.slide && !page.deck.pdf ? (page.slide.images || []).slice(0, 2) : []
                                            delegate: Image {
                                                required property string modelData
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                source: modelData
                                                fillMode: Image.PreserveAspectFit
                                                asynchronous: true
                                                sourceSize.width: 800
                                            }
                                        }
                                    }
                                }
                                Item { Layout.fillHeight: true }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ToolButton {
                            icon.source: "image://icon/chevron-left?color=%23e6e6e6"
                            enabled: page.deckIndex > 0 || page.slideIndex > 0
                            onClicked: page.go(-1)
                        }
                        // "Slide [12] of 55": type a number to go there.
                        Label {
                            text: qsTr("Slide")
                            opacity: 0.6
                            font.pixelSize: 12
                        }
                        TextField {
                            id: slideNumber
                            implicitWidth: 52
                            implicitHeight: 28
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 12
                            topPadding: 2; bottomPadding: 2
                            background: Rectangle {
                                color: "#101014"
                                radius: Math.min(4, theme.radius)
                                border.color: slideNumber.activeFocus ? theme.accent : "#2c2c33"
                            }
                            text: page.deck ? String(page.slideIndex + 1) : ""
                            validator: IntValidator { bottom: 1; top: Math.max(1, page.slides.length) }
                            onAccepted: {
                                page.slideIndex = Math.max(0, Math.min(page.slides.length - 1, parseInt(text) - 1))
                                page.takeKeys()
                            }
                            onActiveFocusChanged: if (activeFocus) selectAll()
                            Keys.onEscapePressed: { text = String(page.slideIndex + 1); page.takeKeys() }
                        }
                        Label {
                            text: page.deck ? qsTr("of %1").arg(page.slides.length) : ""
                            opacity: 0.6
                            font.pixelSize: 12
                        }
                        ToolButton {
                            icon.source: "image://icon/chevron-right?color=%23e6e6e6"
                            enabled: page.deckIndex < page.decks.length - 1 || page.slideIndex < page.slides.length - 1
                            onClicked: page.go(1)
                        }
                        Item { Layout.fillWidth: true }
                        ToolButton {
                            visible: !page.findOpen && !!(page.deck && page.deck.pdf)
                            icon.source: "image://icon/search?color=%23c8c8ce"
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Find in these slides (Ctrl+F)")
                            onClicked: { page.findOpen = true; Qt.callLater(() => findField.forceActiveFocus()) }
                        }
                    }
                }
            }

            // Links, suggestions and notes.
            Rectangle {
                id: sidePanel
                readonly property real openWidth: Math.min(380, page.width * 0.3)
                Layout.preferredWidth: page.panelOpen && !page.planOpen ? openWidth : 0
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                visible: Layout.preferredWidth > 0.5
                clip: true
                Layout.fillHeight: true
                color: "#18181d"
                Rectangle { width: 1; height: parent.height; color: "#26262c" }
                ScrollView {
                    id: side
                    width: sidePanel.openWidth - 28
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.margins: 14
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: side.availableWidth
                        spacing: 8

                        // An animated, narrated explanation of the slide.
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: explainColumn.implicitHeight + 24
                            radius: Math.min(8, theme.radius)
                            color: Qt.alpha(theme.accent, 0.10)
                            border.color: Qt.alpha(theme.accent, 0.35)
                            visible: page.slide !== null
                            ColumnLayout {
                                id: explainColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8
                                RowLayout {
                                    spacing: 8
                                    Icon { name: "sparkle"; size: 18; color: theme.accent }
                                    Label {
                                        text: qsTr("Explain this slide")
                                        font.weight: Font.DemiBold
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 12
                                    opacity: 0.7
                                    text: ollama.status !== "ready" && !page.hasLesson
                                          ? qsTr("Needs the local AI. Set it up under Local AI in the library.")
                                          : (page.slide && (page.slide.links || []).length > 0)
                                            ? qsTr("A short lesson with diagrams and animation, built from the slide and its linked sections.")
                                            : qsTr("A short lesson with diagrams and animation. Link sections first and it follows your books.")
                                }
                                AccentButton {
                                    text: page.lessonOpen ? qsTr("Playing") : page.hasLesson ? qsTr("Watch (E)") : qsTr("Explain (E)")
                                    enabled: !page.writing && !page.lessonOpen && (ollama.status === "ready" || page.hasLesson)
                                    onClicked: page.explainSlide("")
                                }
                                Button {
                                    flat: true
                                    text: qsTr("Practice test… (T)")
                                    enabled: ollama.status === "ready"
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Questions on this slide, these slides, their folder, or the whole workspace")
                                    onClicked: page.practice("deck")
                                }
                                Label {
                                    visible: explainer.error !== "" && !explainer.busy
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 12
                                    color: "#ff8a80"
                                    text: explainer.error
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            Label {
                                text: qsTr("FROM THE BOOKS")
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                opacity: 0.55
                                Layout.fillWidth: true
                            }
                            Button {
                                flat: true
                                text: qsTr("Link a section…")
                                font.pixelSize: 12
                                enabled: page.slide !== null
                                onClicked: linkPopup.open()
                            }
                        }
                        Label {
                            visible: page.slide !== null && (page.slide.links || []).length === 0
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("No sections linked to this slide yet.")
                            opacity: 0.45
                            font.pixelSize: 12
                        }
                        Repeater {
                            model: page.slide ? page.slide.links || [] : []
                            delegate: SectionCard {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                entry: modelData
                                linked: true
                                onOpen: win.openBookAt(modelData.bookId, modelData.target)
                                onRemove: workspaces.removeLink(page.workspaceId, page.deck.id, page.slideIndex, index)
                            }
                        }

                        Label {
                            visible: page.suggestions.length > 0
                            Layout.topMargin: 10
                            text: qsTr("SUGGESTED")
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            opacity: 0.55
                        }
                        Repeater {
                            model: page.suggestions
                            delegate: SectionCard {
                                required property var modelData
                                Layout.fillWidth: true
                                entry: modelData
                                onOpen: win.openBookAt(modelData.bookId, modelData.target)
                                onAdd: page.link(modelData)
                            }
                        }
                        Label {
                            visible: (page.ws.books || []).length === 0 && page.slide !== null
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            wrapMode: Text.WordWrap
                            text: qsTr("Choose the course's books (Books, above) and Shelf suggests the sections each slide comes from.")
                            opacity: 0.45
                            font.pixelSize: 12
                        }

                        Label {
                            visible: page.slide !== null && (page.slide.notes || "") !== ""
                            Layout.topMargin: 14
                            text: qsTr("SPEAKER NOTES")
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            opacity: 0.55
                        }
                        Label {
                            visible: text !== ""
                            Layout.fillWidth: true
                            text: page.slide ? page.slide.notes || "" : ""
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: "#c8c8ce"
                        }

                        // The user's own notes on the slide, saved as they type.
                        Label {
                            visible: page.slide !== null
                            Layout.topMargin: 14
                            text: qsTr("MY NOTES")
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            opacity: 0.55
                        }
                        Rectangle {
                            visible: page.slide !== null
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.max(96, myNote.contentHeight + 26)
                            radius: Math.min(6, theme.radius)
                            color: "#121216"
                            border.color: myNote.activeFocus ? theme.accent : "#2c2c33"
                            TextEdit {
                                id: myNote
                                anchors.fill: parent
                                anchors.margins: 12
                                wrapMode: TextEdit.Wrap
                                font.pixelSize: 13
                                color: "#e8e6ea"
                                selectionColor: Qt.alpha(theme.accent, 0.4)
                                selectByMouse: true
                                property string key: ""
                                property bool dirty: false
                                onTextChanged: if (activeFocus) { dirty = true; noteSave.restart() }
                                Keys.onEscapePressed: page.takeKeys()
                                Label {
                                    visible: myNote.text === "" && !myNote.activeFocus
                                    text: qsTr("Anything to remember about this slide…")
                                    opacity: 0.4
                                    font.pixelSize: 13
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                cursorShape: Qt.IBeamCursor
                            }
                        }
                    }
                }
            }
        }
    }

    // A linked or suggested section.
    component SectionCard: Rectangle {
        id: card
        property var entry: ({})
        property bool linked: false
        signal open()
        signal add()
        signal remove()
        implicitHeight: cardColumn.implicitHeight + 18
        radius: Math.min(6, theme.radius)
        color: cardMouse.containsMouse ? "#222229" : "#1e1e24"
        border.color: linked ? Qt.alpha(theme.accent, 0.45) : "#2a2a31"
        MouseArea {
            id: cardMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: card.open()
        }
        ColumnLayout {
            id: cardColumn
            anchors.left: parent.left
            anchors.right: actions.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 10
            anchors.rightMargin: 6
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: card.entry.bookTitle || ""
                elide: Text.ElideRight
                font.pixelSize: 11
                opacity: 0.55
            }
            Label {
                Layout.fillWidth: true
                text: card.entry.label || ""
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.pixelSize: 13
                font.weight: Font.Medium
                color: card.linked ? theme.accent : "#e8e6ea"
            }
            Label {
                visible: text !== ""
                Layout.fillWidth: true
                text: card.entry.snippet ? card.entry.snippet + "…" : ""
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.pixelSize: 11
                opacity: 0.55
            }
        }
        ToolButton {
            id: actions
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 32
            implicitHeight: 32
            icon.source: card.linked ? "image://icon/close?color=%239a98a0" : "image://icon/plus?color=" + encodeURIComponent(theme.accent)
            ToolTip.visible: hovered
            ToolTip.text: card.linked ? qsTr("Unlink") : qsTr("Link to this slide")
            onClicked: card.linked ? card.remove() : card.add()
        }
    }

    Menu {
        id: deckMenu
        property var deck: ({})
        property int index: 0
        Material.roundedScale: theme.radius
        background: Rectangle {
            implicitWidth: 220
            color: "#202027"
            radius: Math.min(6, theme.radius)
            border.color: "#2c2c33"
        }
        MenuItem {
            text: qsTr("Move up")
            enabled: deckMenu.index > 0
            onTriggered: page.moveDeck(deckMenu.index, -1)
        }
        MenuItem {
            text: qsTr("Move down")
            enabled: deckMenu.index < page.decks.length - 1
            onTriggered: page.moveDeck(deckMenu.index, 1)
        }
        MenuItem {
            text: qsTr("Sort all by name")
            onTriggered: {
                const current = page.deck ? page.deck.id : ""
                workspaces.sortDecks(page.workspaceId)
                page.deckIndex = Math.max(0, page.decks.findIndex(d => d.id === current))
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Practice test on these slides…")
            onTriggered: page.practice("deck", deckMenu.index)
        }
        Menu {
            title: qsTr("Move to folder")
            background: Rectangle {
                implicitWidth: 200
                color: "#202027"
                radius: Math.min(6, theme.radius)
                border.color: "#2c2c33"
            }
            Repeater {
                model: page.folders
                delegate: MenuItem {
                    required property string modelData
                    text: modelData
                    checkable: true
                    checked: (deckMenu.deck.folder || "") === modelData
                    onTriggered: workspaces.setDeckFolder(page.workspaceId, deckMenu.deck.id, modelData)
                }
            }
            MenuItem {
                text: qsTr("New folder…")
                onTriggered: nameDialog.ask(qsTr("New folder"), "", name => workspaces.setDeckFolder(page.workspaceId, deckMenu.deck.id, name))
            }
            MenuItem {
                enabled: (deckMenu.deck.folder || "") !== ""
                text: qsTr("Out of its folder")
                onTriggered: workspaces.setDeckFolder(page.workspaceId, deckMenu.deck.id, "")
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Remove these slides")
            onTriggered: workspaces.removeDeck(page.workspaceId, deckMenu.deck.id)
        }
    }

    Menu {
        id: folderMenu
        property string folder
        Material.roundedScale: theme.radius
        background: Rectangle {
            implicitWidth: 240
            color: "#202027"
            radius: Math.min(6, theme.radius)
            border.color: "#2c2c33"
        }
        MenuItem {
            text: qsTr("Practice test on everything in it…")
            onTriggered: {
                const d = page.decks.findIndex(x => (x.folder || "") === folderMenu.folder)
                page.practice("folder", Math.max(0, d), folderMenu.folder)
            }
        }
        MenuItem {
            text: qsTr("Rename folder…")
            onTriggered: nameDialog.ask(qsTr("Rename folder"), folderMenu.folder,
                                        name => workspaces.renameFolder(page.workspaceId, folderMenu.folder, name))
        }
        MenuItem {
            text: qsTr("Remove folder (keeps the slides)")
            onTriggered: workspaces.removeFolder(page.workspaceId, folderMenu.folder)
        }
    }

    // A name for a folder.
    Dialog {
        id: nameDialog
        property var then: null
        function ask(title, value, done) {
            nameDialog.title = title
            nameField.text = value
            then = done
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel
        Material.roundedScale: theme.radius
        onOpened: { nameField.forceActiveFocus(); nameField.selectAll() }
        onAccepted: if (nameField.text.trim() !== "" && then) then(nameField.text.trim())
        onClosed: page.takeKeys()
        TextField {
            id: nameField
            width: parent.width
            placeholderText: qsTr("Name, like “Midterm 2” or “GPU unit”")
            onAccepted: nameDialog.accept()
        }
    }

    // Picking a section by hand: a book, then a section of it.
    Popup {
        id: linkPopup
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, page.width - 40)
        height: Math.min(560, page.height - 40)
        modal: true
        focus: true
        padding: 16
        Material.roundedScale: theme.radius
        background: Rectangle { color: "#18181d"; radius: theme.radius; border.color: "#2c2c33" }
        property string bookId: ""
        readonly property var allSections: bookId ? page.sectionsOf(bookId) : []
        readonly property var shown: filter.text.trim() === "" ? allSections
                                     : allSections.filter(s => s.label.toLowerCase().indexOf(filter.text.trim().toLowerCase()) >= 0)
        onOpened: {
            const books = library.allBooks()
            if (!bookId && books.length > 0)
                bookId = (page.ws.books || [])[0] || books[0].id
            bookBox.currentIndex = Math.max(0, books.findIndex(b => b.id === bookId))
            filter.text = ""
            filter.forceActiveFocus()
        }
        onClosed: page.takeKeys()
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: qsTr("Link a section to this slide")
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            ComboBox {
                id: bookBox
                Layout.fillWidth: true
                model: library.allBooks()
                textRole: "title"
                onActivated: linkPopup.bookId = model[currentIndex].id
            }
            CompactField {
                id: filter
                Layout.fillWidth: true
                hint: qsTr("Find a section…")
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: linkPopup.shown
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    leftPadding: 10 + Math.min(4, modelData.depth) * 16
                    contentItem: Label {
                        text: modelData.label
                        elide: Text.ElideRight
                        font.weight: modelData.depth === 0 ? Font.DemiBold : Font.Normal
                    }
                    onClicked: {
                        page.link({ bookId: linkPopup.bookId, target: modelData.target, label: modelData.label })
                        linkPopup.close()
                    }
                }
                Label {
                    visible: linkPopup.allSections.length === 0 && linkPopup.bookId !== ""
                    anchors.centerIn: parent
                    width: parent.width - 40
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("This book has no table of contents. Open it and build an outline, or pick a suggested section.")
                    opacity: 0.55
                }
            }
        }
    }

    // The books suggestions come from.
    Popup {
        id: booksPopup
        anchors.centerIn: Overlay.overlay
        width: Math.min(460, page.width - 40)
        height: Math.min(520, page.height - 40)
        modal: true
        focus: true
        padding: 16
        Material.roundedScale: theme.radius
        background: Rectangle { color: "#18181d"; radius: theme.radius; border.color: "#2c2c33" }
        property var picked: []
        onOpened: picked = (page.ws.books || []).slice()
        onClosed: {
            workspaces.setBooks(page.workspaceId, picked)
            page.takeKeys()
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: qsTr("The course's books")
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Shelf suggests sections from these for each slide.")
                opacity: 0.6
                font.pixelSize: 12
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: library.allBooks()
                ScrollBar.vertical: ScrollBar {}
                delegate: CheckDelegate {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.title
                    checked: booksPopup.picked.indexOf(modelData.id) >= 0
                    onToggled: {
                        const next = booksPopup.picked.filter(id => id !== modelData.id)
                        if (checked)
                            next.push(modelData.id)
                        booksPopup.picked = next
                    }
                }
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Done")
                highlighted: true
                onClicked: booksPopup.close()
            }
        }
    }

    // What can be done here, the first time.
    TipCard {
        key: "workspace"
        title: qsTr("Studying slides")
        z: 50
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 18
        when: page.active
        lines: [
            qsTr("<b>Add slides</b> (PDF or PowerPoint) from a course, or drop them here."),
            qsTr("<b>Explain this slide</b> makes a short animated lesson; <b>T</b> starts a practice test."),
            qsTr("Select text on a slide to copy it, add it to your notes, or explain it; <b>Ctrl+F</b> finds words."),
            qsTr("The <b>Study plan</b> turns your exam review into a day-by-day plan.")
        ]
    }
}
