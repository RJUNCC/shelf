.pragma library

// Which slides a study-plan topic is about, and which lines on them to read.
//
// A topic's words are weighted by how rare they are across the workspace's
// slides (inverse document frequency): a word on most slides ("cloud" in a
// cloud computing course) says little and counts for almost nothing, a word
// on a few slides ("NameNode") counts a lot. The best slides become seeds,
// and each grows into a section with its neighbours that are still about
// the topic (and build steps of the same slide), so the plan can say
// "slides 12–18" instead of pointing at one slide.

const stop = new Set(("about above after again against also among being below between both could does doing during each " +
    "from further have having here into itself just more most only other over same should some such than that their " +
    "them then there these they this those through under until very what when where which while will with would your " +
    "explain describe understand know able using used uses based compare contrast discuss difference differences between " +
    "including include introduction overview example examples part chapter section topic topics concept concepts " +
    "big picture main key basic basics core components component model models side end back front use uses case cases " +
    "type types work works working make makes made well good first second third make many much like need needs").split(" "))

function slideText(s) {
    if (!s)
        return ""
    return [s.title || ""].concat((s.paragraphs || []).map(p => p.text)).concat([s.notes || ""]).join("\n").toLowerCase()
}

// A word or phrase in a text, starting at a word's start (so "map" finds
// "maps" but not "bitmap").
function has(text, term) {
    let at = text.indexOf(term)
    while (at >= 0) {
        if (at === 0 || !/[a-z0-9]/.test(text[at - 1]))
            return true
        at = text.indexOf(term, at + 1)
    }
    return false
}

// The topic's words and phrases, each with how much it says by where it
// came from: a keyword phrase most, a single keyword fully, a word out of a
// phrase or the topic's title less.
function topicTerms(topic) {
    const out = []
    const add = (t, f) => {
        t = String(t || "").toLowerCase().replace(/[^a-z0-9+#.\- ]+/g, " ").replace(/\s+/g, " ").trim()
        if (t.length < 3 || stop.has(t))
            return
        const had = out.find(x => x.t === t)
        if (had)
            had.f = Math.max(had.f, f)
        else
            out.push({ t: t, f: f })
    }
    for (const k of topic.keywords || []) {
        const words = String(k).trim().split(/\s+/)
        add(k, words.length > 1 ? 1.5 : 1)
        if (words.length > 1)
            words.forEach(w => add(w, 0.4))
    }
    String(topic.topic || "").split(/[^A-Za-z0-9+#]+/).filter(w => w.length > 3).forEach(w => add(w, 0.5))
    return out
}

// [{t, w, generic}] for a topic over the decks.
function weights(topic, decks) {
    const texts = []
    for (const d of decks) {
        let last = null
        for (const s of d.slides || []) {
            const title = (s.title || "").toLowerCase()
            if (title && title === last)
                continue
            last = title
            texts.push(slideText(s))
        }
    }
    const n = Math.max(1, texts.length)
    const out = []
    for (const term of topicTerms(topic)) {
        let df = 0
        for (const x of texts)
            if (has(x, term.t))
                ++df
        if (df === 0)
            continue
        const generic = df / n > 0.3
        const idf = Math.log((n + 1) / (df + 1)) + 0.1
        out.push({ t: term.t, w: (generic ? idf * 0.15 : idf) * term.f, generic: generic, phrase: term.t.indexOf(" ") > 0,
                   keyword: term.f >= 1 })
    }
    return out
}

// How much a slide is about the topic. Words in its title count most. A
// page full of text (a paper's page rather than a slide) mentions more of
// anything, so its body counts for less. One stray word isn't enough: a
// slide needs two of the topic's keywords, a keyword phrase, or a keyword
// in its title. A deck's first slide (its cover) counts for less.
function slideScore(slide, terms, cover) {
    const title = (slide.title || "").toLowerCase()
    const text = slideText(slide)
    const words = text.split(/\s+/).length
    let titleScore = 0, body = 0, telling = 0, strong = false
    for (const x of terms) {
        if (has(title, x.t)) {
            titleScore += x.w * 2.5
            if (!x.generic && x.keyword)
                strong = true
        } else if (has(text, x.t)) {
            body += x.w
        } else {
            continue
        }
        if (!x.generic && x.keyword) {
            ++telling
            if (x.phrase)
                strong = true
        }
    }
    if (telling < 2 && !strong)
        return 0
    return (titleScore + body / Math.max(1, Math.sqrt(words / 60))) * (cover ? 0.3 : 1)
}

// Sections of slides for a topic, best first then in deck order:
// [{deckId, slide (first), to (last)}].
function match(topic, decks, terms) {
    terms = terms || weights(topic, decks)
    if (terms.length === 0)
        return []
    const scores = decks.map(d => (d.slides || []).map((s, i) => slideScore(s, terms, i === 0)))
    let best = 0
    scores.forEach(list => list.forEach(v => { best = Math.max(best, v) }))
    if (best <= 0)
        return []
    const sameTitle = (d, a, b) => {
        const sa = (decks[d].slides[a].title || "").toLowerCase(), sb = (decks[d].slides[b].title || "").toLowerCase()
        return sa !== "" && sa === sb
    }
    let runs = []
    decks.forEach((d, di) => {
        const list = scores[di]
        list.forEach((v, si) => {
            if (v < best * 0.45 || v <= 0)
                return
            let from = si, to = si
            const keep = j => list[j] > 0 && list[j] >= v * 0.3
            while (from > 0 && si - from < 12 && (keep(from - 1) || sameTitle(di, from - 1, from)))
                --from
            while (to < list.length - 1 && to - si < 12 && (keep(to + 1) || sameTitle(di, to, to + 1)))
                ++to
            runs.push({ deck: di, from: from, to: to })
        })
    })
    // Overlapping or touching runs in a deck become one.
    runs.sort((a, b) => a.deck - b.deck || a.from - b.from)
    const merged = []
    for (const r of runs) {
        const last = merged[merged.length - 1]
        if (last && last.deck === r.deck && r.from <= last.to + 1)
            last.to = Math.max(last.to, r.to)
        else
            merged.push(Object.assign({}, r))
    }
    for (const r of merged) {
        r.score = 0
        for (let i = r.from; i <= r.to; ++i)
            r.score += scores[r.deck][i]
    }
    merged.sort((a, b) => b.score - a.score)
    return merged.slice(0, 6)
        .sort((a, b) => a.deck - b.deck || a.from - b.from)
        .map(r => ({ deckId: decks[r.deck].id, slide: r.from, to: r.to }))
}

// The lines of a slide to read for the topic: those with its keywords, or
// failing that its other telling words, or failing that its common ones. `lines` as
// pdfGeometry.lines gives them; returns their indexes.
function linesToRead(lines, terms) {
    const pick = which => {
        const out = []
        lines.forEach((l, i) => {
            const text = String(l.text || "").toLowerCase()
            if (terms.some(x => which(x) && has(text, x.t)))
                out.push(i)
        })
        return out
    }
    // The topic's own keywords first; then any of its telling words; then
    // even its common ones.
    let chosen = pick(x => !x.generic && x.keyword)
    if (chosen.length === 0)
        chosen = pick(x => !x.generic)
    if (chosen.length === 0)
        chosen = pick(x => x.generic)
    // A sentence or bullet wrapped onto the next line is read whole.
    const out = []
    for (const i of chosen) {
        out.push(i)
        let j = i
        while (j + 1 < lines.length && !/[.!?:]$/.test(String(lines[j].text).trim())
               && /^[a-z(]/.test(String(lines[j + 1].text).trim()) && j - i < 3)
            out.push(++j)
    }
    return out.filter((v, k) => out.indexOf(v) === k)
}

// Whether a slide's title is about the topic, so all of the slide is.
function titleOnTopic(title, terms) {
    const t = String(title || "").toLowerCase()
    return t !== "" && terms.some(x => !x.generic && x.keyword && has(t, x.t))
}
