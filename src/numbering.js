.pragma library

// Numbers every outline: 14 for a chapter, 14.2 for its sections, 14.2.1 for
// their subsections. Numbers a book prints ("3.1 Protein Structure") are
// kept and the count carries on from them; headings without one get the
// next number. Parts and anything before the first chapter stay unnumbered.
//
// Takes [{label, depth, target}] and returns copies with `number` (maybe
// empty), `title` (the label without a printed number), and `label` as the
// two together, so everything showing a label shows the number too.

var chapterPattern = /^chapter\s+(\d+)\b/i
var partPattern = /^(part|topic|unit|module)\b/i
var printedPattern = /^(\d+(?:\.\d+)+|[A-Z]\.\d+(?:\.\d+)*)\.?\s+/

function labelled(entries) {
    if (!entries || entries.length === 0)
        return []
    // The chapters' depth: entries named "Chapter N", or else the top level
    // that is not a part.
    var chapterDepth = -1, i, e
    for (i = 0; i < entries.length; ++i) {
        e = entries[i]
        if (chapterPattern.test(e.label) && (chapterDepth < 0 || e.depth < chapterDepth))
            chapterDepth = e.depth
    }
    if (chapterDepth < 0) {
        for (i = 0; i < entries.length; ++i) {
            e = entries[i]
            if (!partPattern.test(e.label) && (chapterDepth < 0 || e.depth < chapterDepth))
                chapterDepth = e.depth
        }
    }
    if (chapterDepth < 0)
        chapterDepth = 0
    // When the book names its chapters, only those count: a "Copyright" or
    // "Index" beside them is front or back matter, not a numbered chapter.
    var named = false
    for (i = 0; i < entries.length; ++i)
        if (chapterPattern.test(entries[i].label))
            named = true

    var out = []
    var chapter = 0, inChapter = false
    var prefix = ""        // what generated numbers start with: the chapter, or a printed "A"
    var counters = []      // per level below the chapter
    for (i = 0; i < entries.length; ++i) {
        e = entries[i]
        var label = String(e.label || "")
        var result = { label: label, depth: e.depth, target: e.target, number: "", title: label }
        if (e.depth < chapterDepth || (e.depth === chapterDepth && partPattern.test(label))
                || (e.depth === chapterDepth && named && !chapterPattern.test(label))) {
            inChapter = false
        } else if (e.depth === chapterDepth) {
            var m = chapterPattern.exec(label)
            chapter = m ? parseInt(m[1]) : chapter + 1
            prefix = String(chapter)
            counters = []
            inChapter = true
            // "Chapter 14: …" already says its number.
            if (!m) {
                result.number = prefix
                result.label = prefix + "  " + label
            }
        } else if (inChapter) {
            var level = e.depth - chapterDepth
            var printed = printedPattern.exec(label)
            if (printed) {
                var parts = printed[1].split(".")
                prefix = parts[0]
                counters = parts.slice(1).map(function (p) { return parseInt(p) || 0 })
                result.number = printed[1]
                result.title = label.slice(printed[0].length)
            } else {
                while (counters.length < level)
                    counters.push(0)
                counters.length = level
                counters[level - 1] += 1
                result.number = [prefix].concat(counters).join(".")
            }
            result.label = result.number + "  " + result.title
        }
        out.push(result)
    }
    return out
}
