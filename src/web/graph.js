// The knowledge graph: books, chapters, highlights, notes and concepts,
// drawn with a small force simulation on a canvas (no library). It moves
// while it settles and then stops completely, so an open graph costs
// nothing when left alone.
//
// Shelf calls Graph.update({nodes, links}) and Graph.filter({query,
// allConcepts}); the page calls host.open(json) to open a passage.
'use strict'

let host = null
let strings = {}
const tr = (k, ...a) => { let s = strings[k] || k; a.forEach((x, i) => { s = s.split('%' + (i + 1)).join(String(x)) }); return s }

const canvas = document.getElementById('canvas')
const g = canvas.getContext('2d')
const tip = document.getElementById('tip')
const card = document.getElementById('card')

const COLORS = { yellow: '#f2c94c', green: '#6fcf97', blue: '#56ccf2', pink: '#f28cb1', purple: '#bb9af7', orange: '#f2994a' }
let accent = '#c678dd'

let nodes = [], links = [], byId = new Map(), neighbours = new Map()
let allNodes = [], allLinks = []
let view = { x: 0, y: 0, k: 1 }
let alpha = 0, running = false, userMoved = false, firstFit = true
let hover = null, selected = null, query = '', matches = null, showAll = false
let dragging = null

function radius(n) {
    switch (n.kind) {
    case 'book': return 13
    case 'chapter': return 6
    case 'concept': return 4 + Math.min(10, 2.2 * Math.sqrt(n.uses || 1))
    default: return 4
    }
}

function color(n) {
    switch (n.kind) {
    case 'book': return accent
    case 'chapter': return '#8a8892'
    case 'concept': return n.books > 1 ? '#4fd1c5' : '#7aa2f7'
    case 'note': return '#e8e6ea'
    default: return COLORS[n.color] || COLORS.yellow
    }
}

// Concepts used only once, in one book, are leaves that add little; they
// are left out unless asked for (typed [[links]] always show).
function visible(n) {
    return n.kind !== 'concept' || showAll || n.typed || n.uses > 1
}

// The same graph comes out the same way each time it is opened: starting
// places come from each node's id, not chance.
function seeded(id, salt) {
    let h = 2166136261 ^ salt
    for (let i = 0; i < id.length; ++i) h = Math.imul(h ^ id.charCodeAt(i), 16777619)
    return ((h >>> 0) % 100000) / 100000
}

function rebuild() {
    nodes = allNodes.filter(visible)
    byId = new Map(nodes.map(n => [n.id, n]))
    links = allLinks.filter(l => byId.has(l.source) && byId.has(l.target))
                    .map(l => ({ source: byId.get(l.source), target: byId.get(l.target), concept: l.concept }))
    neighbours = new Map(nodes.map(n => [n, new Set()]))
    for (const l of links) {
        neighbours.get(l.source).add(l.target)
        neighbours.get(l.target).add(l.source)
    }
    for (const n of nodes) n.degree = neighbours.get(n).size
}

// Keeps the place of nodes already shown; new ones start beside a
// neighbour and fade in.
function update(data) {
    const old = new Map(allNodes.map(n => [n.id, n]))
    const now = performance.now()
    allNodes = data.nodes.map(n => {
        const was = old.get(n.id)
        return was ? Object.assign(was, n) : Object.assign(n, { born: old.size ? now : 0 })
    })
    allLinks = data.links
    rebuild()
    const placed = n => n.x !== undefined
    for (let pass = 0; pass < 3; ++pass)
        for (const n of nodes) {
            if (placed(n)) continue
            const near = [...neighbours.get(n)].find(placed)
            if (near) {
                n.x = near.x + (seeded(n.id, 1) - 0.5) * 40
                n.y = near.y + (seeded(n.id, 2) - 0.5) * 40
            } else if (pass === 2) {
                const a = seeded(n.id, 3) * 2 * Math.PI, r = 50 + seeded(n.id, 4) * 150
                n.x = Math.cos(a) * r
                n.y = Math.sin(a) * r
            }
        }
    for (const n of nodes) { n.vx = n.vx || 0; n.vy = n.vy || 0 }
    if (selected && !byId.has(selected.id)) closeCard()
    applyQuery()
    // A first look settles quietly before it is drawn.
    if (firstFit) {
        alpha = 1
        for (let i = 0; i < 80; ++i) tick()
    }
    reheat(old.size ? 0.35 : 1)
}

function reheat(a) {
    alpha = Math.max(alpha, a)
    if (!running) {
        running = true
        requestAnimationFrame(frame)
    }
}

function tick() {
    const n = nodes.length
    // Springs along links.
    for (const l of links) {
        const s = l.source, t = l.target
        const dx = t.x - s.x || 0.01, dy = t.y - s.y || 0.01
        const d = Math.hypot(dx, dy)
        const want = s.kind === 'book' || t.kind === 'book' ? 70 : l.concept ? 55 : 32
        const strength = 0.5 / Math.min(s.degree, t.degree)
        const f = (d - want) / d * alpha * strength
        const bias = s.degree / (s.degree + t.degree)
        t.vx -= dx * f * bias; t.vy -= dy * f * bias
        s.vx += dx * f * (1 - bias); s.vy += dy * f * (1 - bias)
    }
    // Pushing apart, between nodes near each other (a grid keeps it quick).
    const cell = 90, grid = new Map()
    for (const a of nodes) {
        const key = Math.floor(a.x / cell) + ',' + Math.floor(a.y / cell)
        if (!grid.has(key)) grid.set(key, [])
        grid.get(key).push(a)
    }
    for (const a of nodes) {
        const cx = Math.floor(a.x / cell), cy = Math.floor(a.y / cell)
        for (let i = -1; i <= 1; ++i)
            for (let j = -1; j <= 1; ++j) {
                const list = grid.get((cx + i) + ',' + (cy + j))
                if (!list) continue
                for (const b of list) {
                    if (b === a) continue
                    let dx = a.x - b.x, dy = a.y - b.y
                    let d2 = dx * dx + dy * dy
                    if (d2 > cell * cell) continue
                    if (d2 < 1) { dx = seeded(a.id, 5) - 0.5; dy = seeded(b.id, 6) - 0.5; d2 = 1 }
                    const push = (b.kind === 'book' ? 4 : 1) * 60 * alpha / d2
                    a.vx += dx * push; a.vy += dy * push
                }
            }
    }
    // A gentle pull to the middle keeps separate books near each other.
    for (const a of nodes) {
        a.vx -= a.x * 0.004 * alpha
        a.vy -= a.y * 0.004 * alpha
    }
    for (const a of nodes) {
        if (a.fx !== undefined) { a.x = a.fx; a.y = a.fy; a.vx = a.vy = 0; continue }
        a.vx *= 0.6; a.vy *= 0.6
        a.x += a.vx; a.y += a.vy
    }
    alpha *= 0.985
}

function frame() {
    if (alpha > 0.004 || dragging) {
        tick()
        if (firstFit && !userMoved) fit(nodes, false)
        draw()
        requestAnimationFrame(frame)
    } else {
        running = false
        if (firstFit && !userMoved) fit(nodes, false)
        firstFit = false
        draw()
    }
}

function fit(list, animate = true) {
    if (!list.length) return
    const xs = list.map(n => n.x), ys = list.map(n => n.y)
    const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys)
    const w = canvas.clientWidth, h = canvas.clientHeight, pad = 60
    const k = Math.min(2.5, Math.max(0.15, Math.min((w - 2 * pad) / (maxX - minX || 1), (h - 2 * pad) / (maxY - minY || 1))))
    const target = { k, x: w / 2 - k * (minX + maxX) / 2, y: h / 2 - k * (minY + maxY) / 2 }
    if (!animate) { view = target; return }
    const from = Object.assign({}, view), start = performance.now()
    const step = t => {
        const p = Math.min(1, (t - start) / 450), e = 1 - Math.pow(1 - p, 3)
        view = { k: from.k + (target.k - from.k) * e, x: from.x + (target.x - from.x) * e, y: from.y + (target.y - from.y) * e }
        draw()
        if (p < 1) requestAnimationFrame(step)
    }
    requestAnimationFrame(step)
}

function lit(n) {
    // What stands out: the hovered or selected node and its neighbours,
    // or what matches the search.
    const focus = hover || selected
    if (focus) return n === focus || neighbours.get(focus).has(n)
    if (matches) return matches.has(n)
    return true
}

function draw() {
    const dpr = window.devicePixelRatio || 1
    const w = canvas.clientWidth, h = canvas.clientHeight
    if (canvas.width !== w * dpr || canvas.height !== h * dpr) { canvas.width = w * dpr; canvas.height = h * dpr }
    g.setTransform(dpr, 0, 0, dpr, 0, 0)
    g.clearRect(0, 0, w, h)
    g.translate(view.x, view.y)
    g.scale(view.k, view.k)
    const now = performance.now()
    const fade = n => n.born ? Math.min(1, (now - n.born) / 700) : 1
    let fading = false
    const dimming = !!(hover || selected || matches)
    g.lineWidth = 1 / view.k
    for (const l of links) {
        const on = !dimming || (lit(l.source) && lit(l.target) && (!(hover || selected) || l.source === (hover || selected) || l.target === (hover || selected)))
        g.globalAlpha = (on ? (dimming ? 0.7 : 0.28) : 0.05) * Math.min(fade(l.source), fade(l.target))
        g.strokeStyle = on && dimming ? accent : l.concept ? '#4a5a7a' : '#55535c'
        g.beginPath(); g.moveTo(l.source.x, l.source.y); g.lineTo(l.target.x, l.target.y); g.stroke()
    }
    for (const n of nodes) {
        const f = fade(n)
        if (f < 1) fading = true
        g.globalAlpha = (lit(n) ? 1 : 0.12) * f
        g.fillStyle = color(n)
        g.beginPath(); g.arc(n.x, n.y, radius(n) * (0.6 + 0.4 * f), 0, 2 * Math.PI); g.fill()
        if (n === selected || n === hover) {
            g.strokeStyle = '#fff'; g.lineWidth = 2 / view.k
            g.stroke(); g.lineWidth = 1 / view.k
        }
    }
    // Labels, more as you zoom in.
    g.textAlign = 'center'
    for (const n of nodes) {
        const focus = n === hover || n === selected
        const show = focus || (lit(n) && dimming && (hover || selected)) ||
                     (n.kind === 'book') || (n.kind === 'concept' && (view.k > 0.7 || n.uses > 3)) ||
                     (n.kind === 'chapter' && view.k > 1.3) || (matches && matches.has(n) && view.k > 0.5)
        if (!show || (n.kind === 'highlight' || n.kind === 'note') && !focus && !(matches && matches.has(n))) continue
        const size = (n.kind === 'book' ? 14 : n.kind === 'concept' ? 12 : 11) / Math.max(0.6, view.k)
        g.font = (n.kind === 'book' ? '600 ' : '') + size + 'px sans-serif'
        g.globalAlpha = (lit(n) ? 1 : 0.2) * fade(n)
        const text = n.label.length > 40 ? n.label.slice(0, 39) + '…' : n.label
        g.lineWidth = 3 / view.k; g.strokeStyle = '#121216'
        g.strokeText(text, n.x, n.y - radius(n) - 5 / view.k)
        g.fillStyle = n.kind === 'book' ? '#fff' : '#d8d6dc'
        g.fillText(text, n.x, n.y - radius(n) - 5 / view.k)
        g.lineWidth = 1 / view.k
    }
    g.globalAlpha = 1
    if (fading && !running) requestAnimationFrame(draw)
}

function toWorld(e) {
    const r = canvas.getBoundingClientRect()
    return { x: (e.clientX - r.left - view.x) / view.k, y: (e.clientY - r.top - view.y) / view.k }
}

function nodeAt(p) {
    let best = null, bestD = Infinity
    for (const n of nodes) {
        const d = Math.hypot(n.x - p.x, n.y - p.y)
        if (d < radius(n) + 4 / view.k && d < bestD) { best = n; bestD = d }
    }
    return best
}

canvas.addEventListener('pointerdown', e => {
    canvas.setPointerCapture(e.pointerId)
    const p = toWorld(e), n = nodeAt(p)
    const start = { x: e.clientX, y: e.clientY }, origin = { x: view.x, y: view.y }
    let moved = false
    canvas.classList.add('dragging')
    const move = ev => {
        if (Math.hypot(ev.clientX - start.x, ev.clientY - start.y) > 4) moved = true
        if (!moved) return
        userMoved = true
        if (n) {
            dragging = n
            const q = toWorld(ev)
            n.fx = q.x; n.fy = q.y
            reheat(0.25)
        } else {
            view.x = origin.x + ev.clientX - start.x
            view.y = origin.y + ev.clientY - start.y
            draw()
        }
    }
    const up = () => {
        canvas.removeEventListener('pointermove', move)
        canvas.removeEventListener('pointerup', up)
        canvas.classList.remove('dragging')
        if (n) { delete n.fx; delete n.fy }
        dragging = null
        if (!moved) {
            if (n) select(n)
            else closeCard()
        }
    }
    canvas.addEventListener('pointermove', move)
    canvas.addEventListener('pointerup', up)
})

canvas.addEventListener('pointermove', e => {
    if (dragging) return
    const n = nodeAt(toWorld(e))
    if (n !== hover) {
        hover = n
        canvas.classList.toggle('pointing', !!n)
        draw()
    }
    if (n) {
        tip.style.display = 'block'
        tip.textContent = n.kind === 'highlight' || n.kind === 'note' ? (n.text || n.note).slice(0, 220) : n.label
        const r = canvas.getBoundingClientRect()
        tip.style.left = Math.min(e.clientX - r.left + 14, r.width - 330) + 'px'
        tip.style.top = (e.clientY - r.top + 14) + 'px'
    } else {
        tip.style.display = 'none'
    }
})
canvas.addEventListener('pointerleave', () => { hover = null; tip.style.display = 'none'; draw() })

canvas.addEventListener('wheel', e => {
    e.preventDefault()
    userMoved = true
    const r = canvas.getBoundingClientRect()
    const mx = e.clientX - r.left, my = e.clientY - r.top
    const k = Math.max(0.1, Math.min(6, view.k * Math.exp(-e.deltaY * (e.ctrlKey ? 0.01 : 0.0015))))
    view.x = mx - (mx - view.x) * k / view.k
    view.y = my - (my - view.y) * k / view.k
    view.k = k
    draw()
}, { passive: false })

window.addEventListener('resize', () => draw())

function openItem(n) {
    host.open(JSON.stringify({ bookId: n.bookId, location: n.location, format: n.format }))
}

function el(tag, attrs = {}, ...children) {
    const e = document.createElement(tag)
    for (const [k, v] of Object.entries(attrs)) {
        if (k === 'text') e.textContent = v
        else if (k.startsWith('on')) e.addEventListener(k.slice(2), v)
        else e.setAttribute(k, v)
    }
    e.append(...children.filter(c => c !== null && c !== undefined))
    return e
}

// The card for a selected node: a passage with its concepts and an Open
// button; or a concept, chapter or book with what is linked to it.
function select(n) {
    selected = n
    const parts = [el('button', { class: 'close', text: '✕', onclick: closeCard })]
    const linked = [...neighbours.get(n)]
    const items = linked.filter(m => m.kind === 'highlight' || m.kind === 'note')
    const concepts = linked.filter(m => m.kind === 'concept')
    const itemRow = m => el('div', { class: 'item', onclick: () => openItem(m) },
                            document.createTextNode(m.label), el('small', { text: m.bookTitle + (m.chapter ? ' · ' + m.chapter : '') }))
    if (n.kind === 'highlight' || n.kind === 'note') {
        parts.push(el('div', { class: 'sub', text: n.bookTitle + (n.chapter ? ' · ' + n.chapter : '') }))
        if (n.text) {
            const q = el('div', { class: 'quote', text: n.text })
            q.style.setProperty('--c', color(n))
            parts.push(q)
        }
        if (n.note) parts.push(el('div', { class: 'note', text: n.note }))
        if (concepts.length)
            parts.push(el('div', {}, ...concepts.map(c => el('span', { class: 'tag', text: c.label, onclick: () => select(c) }))))
        parts.push(el('button', { text: tr('Open in the book'), onclick: () => openItem(n) }))
    } else {
        parts.push(el('h3', { text: n.label }))
        const sub = n.kind === 'concept'
            ? tr('%1 passages in %2 books', n.uses, n.books)
            : n.kind === 'chapter' ? tr('%1 highlights and notes', items.length) : tr('Book')
        parts.push(el('div', { class: 'sub', text: sub }))
        if (n.kind === 'book') {
            const chapters = linked.filter(m => m.kind === 'chapter')
            parts.push(el('div', {}, ...chapters.map(c => el('span', { class: 'tag', text: c.label, onclick: () => select(c) }))))
        }
        parts.push(...items.slice(0, 60).map(itemRow))
    }
    card.replaceChildren(...parts)
    card.style.display = 'block'
    tip.style.display = 'none'
    draw()
}

function closeCard() {
    selected = null
    card.style.display = 'none'
    draw()
}

function applyQuery() {
    const q = query.trim().toLowerCase()
    matches = q ? new Set(nodes.filter(n => (n.label + ' ' + (n.text || '') + ' ' + (n.note || '')).toLowerCase().includes(q))) : null
}

window.Graph = {
    update(data) { update(data) },
    setup(options) {
        strings = options.strings || {}
        if (options.accent) { accent = options.accent; document.documentElement.style.setProperty('--accent', accent) }
    },
    filter(options) {
        const all = !!options.allConcepts
        query = options.query || ''
        if (all !== showAll) {
            showAll = all
            rebuild()
            for (const n of nodes) if (n.x === undefined) {
                const near = [...neighbours.get(n)].find(m => m.x !== undefined)
                n.x = (near ? near.x : 0) + (Math.random() - 0.5) * 30
                n.y = (near ? near.y : 0) + (Math.random() - 0.5) * 30
                n.vx = n.vy = 0
                n.born = performance.now()
            }
            reheat(0.3)
        }
        applyQuery()
        if (matches && matches.size) fit([...matches])
        draw()
    },
    fit() { fit(nodes) },
}

new QWebChannel(qt.webChannelTransport, channel => {
    host = channel.objects.host
    host.ready()
})
