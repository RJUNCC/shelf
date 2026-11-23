// A molecule question: the student draws the structure.
//
// Click empty space to place an atom; drag from an atom to draw a bond to a
// new atom (or to another atom); click an atom to grow a chain from it, or
// to change its element to the one picked; click a bond to change its
// order. Hydrogens are implied, as in a skeletal drawing: carbons are
// corners, other atoms show their hydrogens ("OH", "NH2"). Hovering an atom
// and pressing a letter changes its element; Delete removes it.
//
// Checked with Chem.same against the model's SMILES (which Shelf checked
// against its formula), so how the drawing is laid out doesn't matter.
'use strict'

const BOND = 40
const ELEMENT_KEYS = { c: 'C', n: 'N', o: 'O', s: 'S', p: 'P', f: 'F', h: 'H', i: 'I', b: 'Br', l: 'Cl' }

class MoleculeTool {
    constructor(root, q) {
        this.q = q
        this.atoms = []          // {el, charge, x, y}
        this.bonds = []          // {a, b, order}
        this.history = []
        this.element = 'C'
        this.order = 1
        this.mode = 'draw'       // draw | move | erase | charge
        this.hover = null        // {kind: 'atom'|'bond', index}

        const bar = el('div', { class: 'toolbar mol-bar' })
        this.buttons = {}
        const pick = (key, label, title, action) => {
            const b = el('button', { class: 'tool', text: label, title, onclick: () => { action(); this.refreshBar() } })
            this.buttons[key] = b
            return b
        }
        for (const e of ['C', 'N', 'O', 'S', 'P', 'F', 'Cl', 'Br', 'I', 'H'])
            bar.append(pick('el' + e, e, tr('Draw %1', e), () => { this.element = e; this.mode = 'draw' }))
        bar.append(el('span', { class: 'sep' }))
        ;[[1, '—', tr('Single bond')], [2, '═', tr('Double bond')], [3, '≡', tr('Triple bond')]].forEach(([o, l, t]) =>
            bar.append(pick('order' + o, l, t, () => { this.order = o; this.mode = 'draw' })))
        bar.append(el('span', { class: 'sep' }))
        // Ring templates, drawn as little polygons.
        const ringIcon = (n, aromatic) => {
            const icon = svg('svg', { viewBox: '-10 -10 20 20', width: 18, height: 18 })
            const pts = Array.from({ length: n }, (_, k) => {
                const a = -Math.PI / 2 + 2 * Math.PI * k / n
                return [8 * Math.cos(a), 8 * Math.sin(a)]
            })
            icon.append(svg('polygon', { points: pts.map(p => p.join(',')).join(' '), class: 'ring-icon' }))
            if (aromatic)
                icon.append(svg('circle', { r: 4, class: 'ring-icon' }))
            return icon
        }
        ;[[6, true, tr('Benzene ring')], [6, false, tr('Six-membered ring')], [5, false, tr('Five-membered ring')]].forEach(([n, ar, t]) => {
            const b = pick('ring' + n + (ar ? 'a' : ''), '', t + ' — ' + tr('click empty space, an atom, or a bond'),
                           () => { this.mode = 'ring'; this.ring = { n, aromatic: ar } })
            b.append(ringIcon(n, ar))
            bar.append(b)
        })
        bar.append(el('span', { class: 'sep' }))
        bar.append(pick('move', tr('Move'), tr('Drag atoms to move them'), () => { this.mode = 'move' }))
        bar.append(pick('charge', '±', tr('Click an atom to change its charge'), () => { this.mode = 'charge' }))
        bar.append(pick('erase', tr('Erase'), tr('Click an atom or bond to remove it'), () => { this.mode = 'erase' }))
        bar.append(el('span', { class: 'sep' }))
        bar.append(el('button', { class: 'tool', text: tr('Undo'), title: 'Ctrl+Z', onclick: () => this.undo() }))
        bar.append(el('button', { class: 'tool', text: tr('Clear'), onclick: () => { this.save(); this.atoms = []; this.bonds = []; this.draw() } }))
        this.formulaLabel = el('span', { class: 'status' })
        bar.append(this.formulaLabel)

        this.svg = svg('svg', { class: 'mol-canvas' })
        this.layer = svg('g')
        this.svg.append(this.layer)
        this.hint = el('div', { class: 'hint', text: tr('Click to place an atom. Drag from an atom to bond a new one, or click it to grow a chain. Click a bond to change it. Hydrogens are added for you.') })
        this.answerBox = el('div', { class: 'mol-answer hidden' })
        root.append(el('div', { class: 'molecule-tool' }, bar,
                       el('div', { class: 'mol-area' }, this.svg, this.answerBox), this.hint))
        this.svg.addEventListener('pointerdown', e => this.down(e))
        this.keyHandler = e => this.key(e)
        document.addEventListener('keydown', this.keyHandler)
        this.refreshBar()
        // Redraw at the new size (rather than stretching) when the answer
        // panel opens or the window changes.
        this.resizer = new ResizeObserver(() => this.draw())
        this.resizer.observe(this.svg)
        requestAnimationFrame(() => this.draw())
    }

    destroy() {
        document.removeEventListener('keydown', this.keyHandler)
        this.resizer.disconnect()
    }

    refreshBar() {
        for (const [k, b] of Object.entries(this.buttons)) {
            const ringKey = this.ring ? 'ring' + this.ring.n + (this.ring.aromatic ? 'a' : '') : ''
            const on = (k === 'el' + this.element || k === 'order' + this.order) && this.mode === 'draw' || k === this.mode
                       || (this.mode === 'ring' && k === ringKey)
            b.classList.toggle('on', on)
        }
        this.svg && this.svg.classList.toggle('erasing', this.mode === 'erase')
    }

    save() {
        this.history.push(JSON.stringify({ atoms: this.atoms, bonds: this.bonds }))
        if (this.history.length > 100)
            this.history.shift()
    }

    undo() {
        const last = this.history.pop()
        if (!last)
            return
        const s = JSON.parse(last)
        this.atoms = s.atoms
        this.bonds = s.bonds
        this.draw()
    }

    point(e) {
        const r = this.svg.getBoundingClientRect()
        return { x: e.clientX - r.left, y: e.clientY - r.top }
    }

    atomAt(p, except = -1) {
        let best = -1, bestD = 14
        this.atoms.forEach((a, i) => {
            const d = Math.hypot(a.x - p.x, a.y - p.y)
            if (i !== except && d < bestD) { best = i; bestD = d }
        })
        return best
    }

    bondAt(p) {
        let best = -1, bestD = 7
        this.bonds.forEach((b, i) => {
            const A = this.atoms[b.a], B = this.atoms[b.b]
            const dx = B.x - A.x, dy = B.y - A.y, len2 = dx * dx + dy * dy
            const t = Math.max(0, Math.min(1, ((p.x - A.x) * dx + (p.y - A.y) * dy) / len2))
            const d = Math.hypot(A.x + t * dx - p.x, A.y + t * dy - p.y)
            if (t > 0.15 && t < 0.85 && d < bestD) { best = i; bestD = d }
        })
        return best
    }

    neighbours(i) {
        return this.bonds.filter(b => b.a === i || b.b === i).map(b => b.a === i ? b.b : b.a)
    }

    bondBetween(i, j) {
        return this.bonds.findIndex(b => (b.a === i && b.b === j) || (b.a === j && b.b === i))
    }

    // Where a new atom bonded to atom i goes: away from its other bonds, in
    // a zigzag along a chain.
    freeSpot(i) {
        const a = this.atoms[i], ns = this.neighbours(i)
        let angle
        if (ns.length === 0) {
            // Toward the middle of the canvas.
            const w = this.svg ? this.svg.getBoundingClientRect().width : 0
            angle = a.x > w / 2 ? -5 * Math.PI / 6 : -Math.PI / 6
        } else if (ns.length === 1) {
            const n = this.atoms[ns[0]]
            const back = Math.atan2(n.y - a.y, n.x - a.x)
            // Turn the way that keeps the chain zigzagging.
            const nn = this.neighbours(ns[0]).filter(k => k !== i)
            let turn = -2 * Math.PI / 3
            if (nn.length) {
                const m = this.atoms[nn[0]]
                const cross = (a.x - n.x) * (m.y - n.y) - (a.y - n.y) * (m.x - n.x)
                turn = cross > 0 ? 2 * Math.PI / 3 : -2 * Math.PI / 3
            }
            angle = back + turn
        } else {
            angle = Chem.widestGap(ns.map(k => Math.atan2(this.atoms[k].y - a.y, this.atoms[k].x - a.x)))
        }
        return { x: a.x + BOND * Math.cos(angle), y: a.y + BOND * Math.sin(angle) }
    }

    addAtom(p) {
        this.atoms.push({ el: this.element, charge: 0, x: p.x, y: p.y })
        return this.atoms.length - 1
    }

    bond(i, j) {
        if (i === j)
            return
        const k = this.bondBetween(i, j)
        if (k >= 0)
            this.bonds[k].order = this.bonds[k].order === this.order ? this.bonds[k].order % 3 + 1 : this.order
        else
            this.bonds.push({ a: i, b: j, order: this.order })
    }

    removeAtom(i) {
        this.atoms.splice(i, 1)
        this.bonds = this.bonds.filter(b => b.a !== i && b.b !== i)
                               .map(b => ({ a: b.a > i ? b.a - 1 : b.a, b: b.b > i ? b.b - 1 : b.b, order: b.order }))
    }

    // A ring of n atoms: around a point, sharing an atom, or fused on a
    // bond (on the side with more room). Benzene gets alternating double
    // bonds. New corners that land on existing atoms are merged into them.
    addRing(n, aromatic, atom, bondIndex, p) {
        const step = 2 * Math.PI / n
        const radius = BOND / (2 * Math.sin(Math.PI / n))
        let corners
        const room = (cx, cy) => this.atoms.filter(a => Math.hypot(a.x - cx, a.y - cy) < radius * 1.5).length
        if (bondIndex >= 0) {
            const b = this.bonds[bondIndex], A = this.atoms[b.a], B = this.atoms[b.b]
            const mx = (A.x + B.x) / 2, my = (A.y + B.y) / 2
            const len = Math.hypot(B.x - A.x, B.y - A.y) || 1
            const nx = -(B.y - A.y) / len, ny = (B.x - A.x) / len
            const apothem = BOND / (2 * Math.tan(Math.PI / n))
            const side = room(mx + nx * apothem, my + ny * apothem) <= room(mx - nx * apothem, my - ny * apothem) ? 1 : -1
            const cx = mx + side * nx * apothem, cy = my + side * ny * apothem
            const start = Math.atan2(A.y - cy, A.x - cx)
            const dir = Math.sign(Math.sin(Math.atan2(B.y - cy, B.x - cx) - start)) || 1
            corners = Array.from({ length: n }, (_, k) => ({ x: cx + radius * Math.cos(start + dir * step * k),
                                                             y: cy + radius * Math.sin(start + dir * step * k) }))
        } else if (atom >= 0) {
            // Hung off the atom by a new single bond, as a substituent
            // (a phenyl group, say).
            const a = this.atoms[atom], spot = this.freeSpot(atom)
            const ang = Math.atan2(spot.y - a.y, spot.x - a.x)
            const cx = spot.x + radius * Math.cos(ang), cy = spot.y + radius * Math.sin(ang)
            const start = ang + Math.PI
            this.pendingLink = atom
            corners = Array.from({ length: n }, (_, k) => ({ x: cx + radius * Math.cos(start + step * k),
                                                             y: cy + radius * Math.sin(start + step * k) }))
        } else {
            corners = Array.from({ length: n }, (_, k) => ({ x: p.x + radius * Math.cos(-Math.PI / 2 + step * k),
                                                             y: p.y + radius * Math.sin(-Math.PI / 2 + step * k) }))
        }
        const ids = corners.map(c => {
            const near = this.atomAt(c)
            if (near >= 0 && Math.hypot(this.atoms[near].x - c.x, this.atoms[near].y - c.y) < 8)
                return near
            this.atoms.push({ el: 'C', charge: 0, x: c.x, y: c.y })
            return this.atoms.length - 1
        })
        if (this.pendingLink !== undefined) {
            this.bonds.push({ a: this.pendingLink, b: ids[0], order: 1 })
            delete this.pendingLink
        }
        for (let k = 0; k < n; ++k) {
            const i = ids[k], j = ids[(k + 1) % n]
            const existing = this.bondBetween(i, j)
            const order = aromatic && n === 6 ? (k % 2 === 0 ? 2 : 1) : 1
            if (existing >= 0) {
                // A shared bond keeps its order; the pattern starts from it.
                continue
            }
            this.bonds.push({ a: i, b: j, order })
        }
        // A benzene ring fused on a double bond: shift the pattern so the
        // shared bond's double counts.
        if (aromatic && n === 6 && bondIndex >= 0 && this.bonds[bondIndex].order === 2)
            for (let k = 0; k < n; ++k) {
                const bk = this.bondBetween(ids[k], ids[(k + 1) % n])
                if (bk !== bondIndex)
                    this.bonds[bk].order = k % 2 === 0 ? 1 : 2
            }
    }

    down(e) {
        e.preventDefault()
        const p = this.point(e)
        const atom = this.atomAt(p), bondIndex = atom < 0 ? this.bondAt(p) : -1
        if (this.mode === 'erase') {
            this.save()
            if (atom >= 0) this.removeAtom(atom)
            else if (bondIndex >= 0) this.bonds.splice(bondIndex, 1)
            this.draw()
            return
        }
        if (this.mode === 'ring') {
            this.save()
            this.addRing(this.ring.n, this.ring.aromatic, atom, bondIndex, p)
            this.draw()
            return
        }
        if (this.mode === 'charge') {
            if (atom >= 0) {
                this.save()
                const c = this.atoms[atom].charge || 0
                this.atoms[atom].charge = c === 0 ? 1 : c === 1 ? -1 : 0
                this.draw()
            }
            return
        }
        if (bondIndex >= 0 && this.mode === 'draw') {
            this.save()
            const b = this.bonds[bondIndex]
            b.order = b.order === this.order ? b.order % 3 + 1 : this.order
            this.draw()
            return
        }
        if (atom < 0) {
            if (this.mode !== 'draw') return
            this.save()
            this.addAtom(p)
            this.draw()
            return
        }
        // On an atom: drag to move (Move) or to bond (Draw); a click without
        // dragging grows a chain or changes the element.
        const start = p, origin = { x: this.atoms[atom].x, y: this.atoms[atom].y }
        let moved = false, saved = false
        const move = ev => {
            const q = this.point(ev)
            if (Math.hypot(q.x - start.x, q.y - start.y) > 6)
                moved = true
            if (!moved) return
            if (this.mode === 'move') {
                if (!saved) { this.save(); saved = true }
                this.atoms[atom].x = origin.x + q.x - start.x
                this.atoms[atom].y = origin.y + q.y - start.y
                this.draw()
            } else {
                this.rubber = { from: atom, to: q }
                this.draw()
            }
        }
        const up = ev => {
            window.removeEventListener('pointermove', move)
            window.removeEventListener('pointerup', up)
            this.rubber = null
            if (this.mode === 'move') { this.draw(); return }
            this.save()
            const q = this.point(ev)
            if (!moved) {
                const a = this.atoms[atom]
                if (a.el !== this.element) {
                    a.el = this.element
                } else {
                    const j = this.addAtom(this.freeSpot(atom))
                    this.bond(atom, j)
                }
            } else {
                const target = this.atomAt(q, atom)
                if (target >= 0) {
                    this.bond(atom, target)
                } else {
                    // Snap the new bond to a standard length along the drag.
                    const a = this.atoms[atom], ang = Math.atan2(q.y - a.y, q.x - a.x)
                    const j = this.addAtom({ x: a.x + BOND * Math.cos(ang), y: a.y + BOND * Math.sin(ang) })
                    this.bond(atom, j)
                }
            }
            this.draw()
        }
        window.addEventListener('pointermove', move)
        window.addEventListener('pointerup', up)
    }

    key(e) {
        if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') {
            e.preventDefault()
            this.undo()
            return
        }
        if (!this.hover || e.ctrlKey || e.metaKey || e.altKey)
            return
        const k = e.key.toLowerCase()
        if (this.hover.kind === 'atom') {
            if (k === 'delete' || k === 'backspace') {
                this.save()
                this.removeAtom(this.hover.index)
                this.hover = null
            } else if (ELEMENT_KEYS[k]) {
                this.save()
                this.atoms[this.hover.index].el = ELEMENT_KEYS[k]
            } else if (k === '+' || k === '-') {
                this.save()
                this.atoms[this.hover.index].charge = k === '+' ? 1 : -1
            } else {
                return
            }
        } else if (this.hover.kind === 'bond') {
            if (['1', '2', '3'].includes(k)) {
                this.save()
                this.bonds[this.hover.index].order = parseInt(k)
            } else if (k === 'delete' || k === 'backspace') {
                this.save()
                this.bonds.splice(this.hover.index, 1)
                this.hover = null
            } else {
                return
            }
        }
        e.preventDefault()
        this.draw()
    }

    // Draws atoms and bonds into an SVG group; used for the student's
    // drawing and for the answer.
    static render(layer, atoms, bonds, options = {}) {
        layer.replaceChildren()
        const mol = Chem.fromDrawing(atoms, bonds)
        const sums = atoms.map(() => 0)
        bonds.forEach(b => { sums[b.a] += b.order; sums[b.b] += b.order })
        const bad = Chem.overloaded(atoms, bonds)
        const degree = atoms.map((_, i) => bonds.filter(b => b.a === i || b.b === i).length)
        const labelled = atoms.map((a, i) => a.el !== 'C' || degree[i] === 0 || a.charge)
        bonds.forEach((b, k) => {
            const A = atoms[b.a], B = atoms[b.b]
            const dx = B.x - A.x, dy = B.y - A.y, len = Math.hypot(dx, dy) || 1
            const nx = -dy / len, ny = dx / len
            // Pull the line back from labelled atoms.
            const t0 = labelled[b.a] ? 9 / len : 0, t1 = labelled[b.b] ? 9 / len : 0
            const x1 = A.x + dx * t0, y1 = A.y + dy * t0, x2 = B.x - dx * t1, y2 = B.y - dy * t1
            const offsets = b.order === 1 ? [0] : b.order === 2 ? [-2.6, 2.6] : [-4, 0, 4]
            const g = svg('g', { class: 'bond' + (options.hover && options.hover.kind === 'bond' && options.hover.index === k ? ' hover' : '') })
            for (const o of offsets)
                g.append(svg('line', { x1: x1 + nx * o, y1: y1 + ny * o, x2: x2 + nx * o, y2: y2 + ny * o }))
            g.append(svg('line', { class: 'hit', x1, y1, x2, y2 }))
            if (options.interactive) {
                g.addEventListener('pointerenter', () => options.onHover({ kind: 'bond', index: k }))
                g.addEventListener('pointerleave', () => options.onHover(null))
            }
            layer.append(g)
        })
        // Folded hydrogens line up with heavy atoms in order.
        let heavy = 0
        atoms.forEach((a, i) => {
            const isH = a.el === 'H' && degree[i] === 1 && !a.charge
            const info = isH ? null : mol.atoms[heavy++]
            const g = svg('g', { class: 'atom' + (bad[i] ? ' bad' : '') + (options.hover && options.hover.kind === 'atom' && options.hover.index === i ? ' hover' : ''),
                                 transform: `translate(${a.x},${a.y})` })
            g.append(svg('circle', { class: 'hit', r: 11 }))
            if (labelled[i]) {
                const h = info ? info.h : 0
                let text = a.el + (h ? 'H' + (h > 1 ? h : '') : '')
                const t = svg('text', { y: 5, class: 'el-' + a.el })
                t.textContent = text
                if (a.charge) {
                    const c = svg('tspan', { dy: -6, class: 'charge' })
                    c.textContent = a.charge > 0 ? '+' : '−'
                    t.append(c)
                }
                g.append(svg('rect', { class: 'mask', x: -8, y: -9, width: 16 + (text.length - 1) * 8, height: 18, rx: 4 }))
                g.append(t)
            } else if (a.charge) {
                const c = svg('text', { x: 7, y: -6, class: 'charge' })
                c.textContent = a.charge > 0 ? '+' : '−'
                g.append(c)
            }
            if (options.interactive) {
                g.addEventListener('pointerenter', () => options.onHover({ kind: 'atom', index: i }))
                g.addEventListener('pointerleave', () => options.onHover(null))
            }
            layer.append(g)
        })
        return mol
    }

    // Slides the whole drawing back inside the canvas when an addition
    // went past an edge (if it fits at all).
    keepInView() {
        const r = this.svg.getBoundingClientRect(), m = 24
        if (!this.atoms.length || !r.width) return
        const xs = this.atoms.map(a => a.x), ys = this.atoms.map(a => a.y)
        const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys)
        let dx = 0, dy = 0
        if (maxX - minX < r.width - 2 * m) dx = minX < m ? m - minX : maxX > r.width - m ? r.width - m - maxX : 0
        if (maxY - minY < r.height - 2 * m) dy = minY < m ? m - minY : maxY > r.height - m ? r.height - m - maxY : 0
        if (dx || dy)
            for (const a of this.atoms) { a.x += dx; a.y += dy }
    }

    draw() {
        if (!this.rubber && this.mode !== 'move')
            this.keepInView()
        const r = this.svg.getBoundingClientRect()
        this.svg.setAttribute('viewBox', `0 0 ${Math.max(1, r.width)} ${Math.max(1, r.height)}`)
        const mol = MoleculeTool.render(this.layer, this.atoms, this.bonds, {
            interactive: true, hover: this.hover,
            onHover: h => { this.hover = h },
        })
        if (this.rubber) {
            const a = this.atoms[this.rubber.from]
            this.layer.append(svg('line', { class: 'rubber', x1: a.x, y1: a.y, x2: this.rubber.to.x, y2: this.rubber.to.y }))
        }
        this.formulaLabel.textContent = this.atoms.length ? Chem.formula(mol) : ''
    }

    // The answer, laid out by Chem.layout, beside the student's drawing.
    showAnswer() {
        const mol = Chem.parseSmiles(this.q.smiles)
        const pos = Chem.layout(mol)
        const xs = pos.map(p => p.x), ys = pos.map(p => p.y)
        const minX = Math.min(...xs), minY = Math.min(...ys)
        const atoms = mol.atoms.map((a, i) => ({ el: a.el, charge: a.charge, x: (pos[i].x - minX) * BOND + 24, y: (pos[i].y - minY) * BOND + 24 }))
        // Aromatic rings are drawn as a Kekulé structure.
        const bonds = Chem.kekulize(mol)
        const w = (Math.max(...xs) - minX) * BOND + 48, h = (Math.max(...ys) - minY) * BOND + 48
        // Drawn at the same scale as the student's drawing.
        const pic = svg('svg', { viewBox: `0 0 ${w} ${h}`, width: w, height: h, class: 'mol-answer-svg' })
        const layer = svg('g')
        pic.append(layer)
        MoleculeTool.render(layer, atoms, bonds)
        this.answerBox.replaceChildren(el('div', { class: 'label', text: tr('Answer: %1', this.q.formula) }), pic)
        this.answerBox.classList.remove('hidden')
    }

    check() {
        if (!this.atoms.length)
            return { correct: false, usable: true, answer: '', checkedNothing: true, summary: tr('Draw the molecule first.') }
        const bad = Chem.overloaded(this.atoms, this.bonds)
        const i = bad.indexOf(true)
        if (i >= 0)
            return { correct: false, usable: true, answer: '', checkedNothing: true,
                     summary: tr('%1 has more bonds than it can make (shown in red).', this.atoms[i].el) }
        const drawn = Chem.fromDrawing(this.atoms, this.bonds)
        const expected = Chem.parseSmiles(this.q.smiles)
        const correct = Chem.same(drawn, expected)
        const mine = Chem.formula(drawn), theirs = Chem.formula(expected)
        if (!correct)
            this.showAnswer()
        return {
            correct, usable: true, answer: '',
            summary: correct ? tr('That is %1.', theirs)
                   : mine === theirs ? tr('Right formula (%1), but the atoms are connected differently.', mine)
                   : tr('Your drawing is %1; the answer is %2.', mine, theirs),
        }
    }

    reveal() {
        this.showAnswer()
    }
}
window.MoleculeTool = MoleculeTool
