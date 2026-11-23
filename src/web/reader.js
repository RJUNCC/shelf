// The EPUB reader page. foliate-js does the rendering; this file wires it to
// Shelf: it reports position and contents over Qt WebChannel and exposes a
// small `shelf` API that QML drives with runJavaScript().
import './foliate/view.js'
import { Overlayer } from './foliate/overlayer.js'
import * as CFI from './foliate/epubcfi.js'

const params = new URLSearchParams(location.search)

// Reading colors in the spirit of a Kindle's: white, sepia, green, black,
// plus a softer night for dark rooms.
const themes = {
    paper: { bg: '#fbfaf6', fg: '#1f1f1f', link: '#1a5fb4', dark: false },
    sepia: { bg: '#f4ecd8', fg: '#4b3a26', link: '#8a4b14', dark: false },
    green: { bg: '#d9ead3', fg: '#1f2e1c', link: '#1d5c2e', dark: false },
    night: { bg: '#16161a', fg: '#d8d6d0', link: '#8ab4f8', dark: true },
    black: { bg: '#000000', fg: '#c9c9c9', link: '#8ab4f8', dark: true },
}

// A user-picked background; the text color follows its brightness.
const luminance = hex => {
    const [r, g, b] = [1, 3, 5].map(i => parseInt(hex.slice(i, i + 2), 16) / 255)
        .map(v => v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b
}
const customTheme = bg => {
    const dark = luminance(bg) < 0.18
    return { bg, fg: dark ? '#dcdcdc' : '#1f1f1f', link: dark ? '#8ab4f8' : '#1a5fb4', dark }
}
if (/^#[0-9a-f]{6}$/i.test(params.get('customBg') ?? ''))
    themes.custom = customTheme(params.get('customBg'))

const highlightColors = {
    yellow: '#f5d547',
    green: '#6fd08c',
    blue: '#64b0f4',
    pink: '#f584b0',
}

const fonts = {
    original: null,
    serif: 'Georgia, "Noto Serif", "Liberation Serif", serif',
    sans: '"Inter", "Noto Sans", "Segoe UI", "Liberation Sans", sans-serif',
}

const state = {
    theme: params.get('theme') in themes ? params.get('theme') : 'night',
    fontScale: Number(params.get('fontScale')) || 100,
    flow: params.get('flow') === 'scrolled' ? 'scrolled' : 'paginated',
    font: params.get('font') in fonts ? params.get('font') : 'original',
    columns: params.get('columns') === '1' ? 1 : 2,
}

// Book styles: colors from the reading theme, the user's text size, and
// comfortable defaults that a publisher's own CSS can still refine.
const bookCSS = () => {
    const t = themes[state.theme]
    return `
    @namespace epub "http://www.idpf.org/2007/ops";
    html {
        color-scheme: ${t.dark ? 'dark' : 'light'};
        color: ${t.fg} !important;
        background: ${t.bg} !important;
        font-size: ${state.fontScale}% !important;
    }
    body { color: inherit !important; background: transparent !important; }
    ${fonts[state.font] ? `body, p, li, div, span, blockquote { font-family: ${fonts[state.font]} !important; }` : ''}
    ${t.dark ? 'img { filter: brightness(0.9); }' : ''}
    a:link, a:visited { color: ${t.link} !important; }
    p, li, blockquote, dd {
        line-height: 1.5;
        hyphens: auto;
        widows: 2;
        orphans: 2;
    }
    pre { white-space: pre-wrap !important; }
    /* The same color whether or not the page has the keyboard, so a
       selection stays visible after Shelf takes focus back. */
    ::selection { background: ${t.dark ? 'rgba(100, 176, 244, 0.45)' : 'rgba(26, 95, 180, 0.3)'}; }
    img, svg { max-width: 100%; }
    aside[epub|type~="footnote"], aside[epub|type~="endnote"],
    aside[epub|type~="note"], aside[epub|type~="rearnote"] { display: none; }
    `
}

let bridge = null
const channelReady = new Promise(resolve => {
    new QWebChannel(qt.webChannelTransport, channel => {
        bridge = channel.objects.bridge
        resolve()
    })
})

const view = document.createElement('foliate-view')
document.body.append(view)

const applyLayout = () => {
    const t = themes[state.theme]
    document.documentElement.style.setProperty('--shelf-bg', t.bg)
    const r = view.renderer
    if (!r) return
    r.setAttribute('flow', state.flow)
    r.setAttribute('margin', '48px')
    r.setAttribute('gap', '6%')
    r.setAttribute('max-inline-size', '720px')
    r.setAttribute('max-column-count', String(state.columns))
    r.setStyles?.(bookCSS())
}

const highlightWithNote = (rects, options) => {
    const g = document.createElementNS('http://www.w3.org/2000/svg', 'g')
    g.append(Overlayer.highlight(rects, options), Overlayer.underline(rects, { ...options, width: 2 }))
    return g
}

// Highlights by CFI, so sections can redraw theirs when they load.
const annotations = new Map()

// Bookmarks, to tell QML whether the page on screen has one.
let bookmarks = []
let lastLocation = null
const bookmarkOnPage = () => {
    if (!lastLocation?.cfi) return ''
    try {
        const start = CFI.collapse(lastLocation.cfi)
        const end = CFI.collapse(lastLocation.cfi, true)
        return bookmarks.find(b => CFI.compare(b, start) >= 0 && CFI.compare(b, end) <= 0) ?? ''
    } catch {
        return ''
    }
}
const reportLocation = () => {
    const detail = lastLocation
    if (!detail) return
    const fraction = Number.isFinite(detail.fraction) ? detail.fraction : 0
    const minutes = x => Number.isFinite(x) ? Math.round(x) : -1
    bridge.reportRelocated(detail.cfi ?? '', fraction, detail.tocItem?.label?.trim() ?? '',
        JSON.stringify({
            sectionMinutes: minutes(detail.time?.section),
            totalMinutes: minutes(detail.time?.total),
            sections: view.getSectionFractions(),
            bookmark: bookmarkOnPage(),
            // Where a new bookmark goes: the start of the page.
            pageStart: detail.cfi ? CFI.collapse(detail.cfi) : '',
        }))
}

// The passage the user selected, reported to QML so it can offer to
// highlight it; cleared when the selection goes away.
let selectionDoc = null
const clearSelection = () => {
    selectionDoc?.getSelection()?.removeAllRanges()
    selectionDoc = null
}
const frameOffset = doc => {
    const frame = doc.defaultView.frameElement
    const rect = frame ? frame.getBoundingClientRect() : { left: 0, top: 0 }
    return [rect.left, rect.top]
}
// The paragraphs around a selection, about 2,000 characters, so an AI
// explanation knows what the passage is about.
const contextOf = range => {
    let node = range.commonAncestorContainer
    if (node.nodeType !== Node.ELEMENT_NODE) node = node.parentElement
    let block = node?.closest?.('p, li, blockquote, section, div, td') ?? node
    while (block?.parentElement && block.textContent.length < 1200 && block.parentElement.tagName !== 'BODY')
        block = block.parentElement
    const text = (block?.textContent ?? '').replace(/\s+/g, ' ').trim()
    if (text.length <= 2400) return text
    const at = Math.max(0, text.indexOf(range.toString().trim().slice(0, 40)))
    return text.slice(Math.max(0, at - 1200), at + 1200)
}
const reportSelection = (doc, index) => {
    const selection = doc.getSelection()
    if (!selection || selection.isCollapsed || !selection.rangeCount) return
    const range = selection.getRangeAt(0)
    const text = range.toString().trim()
    if (!text) return
    const [dx, dy] = frameOffset(doc)
    const box = range.getBoundingClientRect()
    selectionDoc = doc
    // Context is a bonus; a page whose structure confuses it still reports
    // the selection.
    let context = ''
    try { context = contextOf(range) } catch { /* no context */ }
    bridge.reportSelection(view.getCFI(index, range), text, box.left + dx, box.top + dy, box.width, box.height,
        context)
}

// A triple click selects the sentence under the pointer rather than the
// whole paragraph, as a double click selects the word.
const rangeFromOffsets = (doc, block, start, end) => {
    const walker = doc.createTreeWalker(block, NodeFilter.SHOW_TEXT)
    const range = doc.createRange()
    let seen = 0, started = false
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
        const length = node.textContent.length
        if (!started && start <= seen + length) {
            range.setStart(node, start - seen)
            started = true
        }
        if (started && end <= seen + length) {
            range.setEnd(node, end - seen)
            return range
        }
        seen += length
    }
    return null
}
const selectSentence = (doc, event) => {
    const caret = doc.caretRangeFromPoint?.(event.clientX, event.clientY)
    if (!caret) return
    const block = caret.startContainer.parentElement?.closest('p, li, blockquote, dd, td, h1, h2, h3, h4, h5, h6, div')
    if (!block) return
    const before = doc.createRange()
    before.setStart(block, 0)
    before.setEnd(caret.startContainer, caret.startOffset)
    const offset = before.toString().length
    const text = block.textContent
    for (const { index, segment } of new Intl.Segmenter(undefined, { granularity: 'sentence' }).segment(text)) {
        if (offset >= index && offset <= index + segment.length) {
            const range = rangeFromOffsets(doc, block, index, index + segment.trimEnd().length)
            if (range) {
                const selection = doc.getSelection()
                selection.removeAllRanges()
                selection.addRange(range)
            }
            return
        }
    }
}

// Flatten the contents into rows QML can list: label, href, depth.
const flattenTOC = (items, depth = 0, out = []) => {
    for (const item of items ?? []) {
        out.push({ label: (item.label ?? '').trim(), href: item.href ?? '', depth })
        flattenTOC(item.subitems, depth + 1, out)
    }
    return out
}

// Clicks on the outer thirds of a page turn it, like an e-reader; the middle
// is left for selecting text and following links.
let pendingTurn = 0
const attachPageZones = doc => {
    doc.addEventListener('click', event => {
        if (state.flow !== 'paginated') return
        if (event.target.closest?.('a[href]')) return
        if (event.detail > 1) { clearTimeout(pendingTurn); return }
        if (!doc.getSelection()?.isCollapsed) return
        // The section's iframe is as wide as all its columns and scrolls
        // inside the paginator, so place the click in the page's viewport.
        const frame = doc.defaultView.frameElement
        const left = frame ? frame.getBoundingClientRect().left : 0
        const ratio = (left + event.clientX) / window.innerWidth
        // Wait to see whether this is the start of a double or triple click
        // (selecting a word or sentence) before turning the page.
        clearTimeout(pendingTurn)
        if (event.detail > 1) return
        if (ratio < 0.25) pendingTurn = setTimeout(() => view.goLeft(), 260)
        else if (ratio > 0.75) pendingTurn = setTimeout(() => view.goRight(), 260)
    })
    doc.addEventListener('wheel', event => {
        if (state.flow !== 'paginated') return
        if (Math.abs(event.deltaY) < 4) return
        event.deltaY > 0 ? view.next() : view.prev()
    }, { passive: true })
}

window.shelf = {
    next: () => view.next(),
    prev: () => view.prev(),
    goLeft: () => view.goLeft(),
    goRight: () => view.goRight(),
    goTo: target => view.goTo(target),
    goToFraction: fraction => view.goToFraction(fraction),
    setTheme: name => { if (name in themes) { state.theme = name; applyLayout() } },
    setCustomTheme: bg => {
        if (!/^#[0-9a-f]{6}$/i.test(bg)) return
        themes.custom = customTheme(bg)
        state.theme = 'custom'
        applyLayout()
    },
    setAnnotations: list => {
        for (const cfi of annotations.keys()) view.deleteAnnotation({ value: cfi })
        annotations.clear()
        for (const a of list) {
            annotations.set(a.location, a)
            view.addAnnotation({ value: a.location })
        }
    },
    addHighlight: (cfi, color, hasNote) => {
        annotations.set(cfi, { location: cfi, color, hasNote })
        view.addAnnotation({ value: cfi })
        clearSelection()
    },
    removeHighlight: cfi => {
        annotations.delete(cfi)
        view.deleteAnnotation({ value: cfi })
    },
    clearSelection: () => clearSelection(),
    setBookmarks: list => { bookmarks = list; reportLocation() },
    setFontScale: percent => { state.fontScale = percent; applyLayout() },
    setFlow: flow => { state.flow = flow === 'scrolled' ? 'scrolled' : 'paginated'; applyLayout() },
    setFont: name => { if (name in fonts) { state.font = name; applyLayout() } },
    setColumns: n => {
        state.columns = n === 1 ? 1 : 2
        applyLayout()
        // The paginator only lays out again when flow or max-inline-size
        // change; a new column count alone just sets a CSS variable.
        view.renderer?.render?.()
    },
}

const main = async () => {
    await channelReady
    try {
        await view.open(params.get('book'))
    } catch (error) {
        bridge.reportFailed(String(error?.message ?? error))
        return
    }
    applyLayout()
    view.addEventListener('load', ({ detail: { doc, index } }) => {
        attachPageZones(doc)
        doc.addEventListener('click', event => { if (event.detail === 3) selectSentence(doc, event) })
        // Selections settle after the pointer is released.
        doc.addEventListener('pointerup', () => setTimeout(() => {
            reportSelection(doc, index)
            bridge.reportPointerUp()
        }, 10))
        doc.addEventListener('keyup', event => { if (event.shiftKey) reportSelection(doc, index) })
        doc.addEventListener('selectionchange', () => {
            if (doc.getSelection()?.isCollapsed && selectionDoc === doc) {
                selectionDoc = null
                bridge.reportSelectionCleared()
            }
        })
    })
    view.addEventListener('create-overlay', ({ detail: { index } }) => {
        for (const cfi of annotations.keys()) {
            try {
                if (view.resolveNavigation(cfi)?.index === index) view.addAnnotation({ value: cfi })
            } catch { /* a CFI from another edition of the book */ }
        }
    })
    view.addEventListener('draw-annotation', ({ detail: { draw, annotation } }) => {
        const a = annotations.get(annotation.value)
        const color = highlightColors[a?.color] ?? highlightColors.yellow
        // A highlight with a note is also underlined, so notes stand out. One
        // drawing per highlight: a second draw() would replace the first.
        draw(a?.hasNote ? highlightWithNote : Overlayer.highlight, { color })
    })
    view.addEventListener('show-annotation', ({ detail: { value, range } }) => {
        const doc = range.startContainer.ownerDocument
        const [dx, dy] = frameOffset(doc)
        const box = range.getBoundingClientRect()
        bridge.reportAnnotationClicked(value, box.left + dx, box.top + dy, box.width, box.height)
    })
    view.addEventListener('relocate', ({ detail }) => {
        lastLocation = detail
        reportLocation()
    })
    // Links to other books or the web open nowhere; inside the book they
    // navigate as usual.
    view.addEventListener('external-link', event => event.preventDefault())

    bridge.reportReady(JSON.stringify(flattenTOC(view.book.toc)))
    const cfi = params.get('cfi')
    try {
        await view.init({ lastLocation: cfi || null, showTextStart: !cfi })
    } catch {
        // A saved position from an older copy of the book may not resolve.
        await view.init({ lastLocation: null, showTextStart: true })
    }
}

main()
