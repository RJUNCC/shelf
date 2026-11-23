// A Markov chain question. Three kinds:
//   build   the student turns a description into a chain: a diagram whose
//           states can be dragged, and a matrix of transition probabilities
//           (click one state, then another, to jump to that cell)
//   steady  the chain is given; the student finds the long-run share of
//           time in each state
//   steps   the chain is given; the student finds where it is likely to be
//           after n steps from a start state
// The expected numbers were computed by Shelf, not the model.
'use strict'

const SVG = 'http://www.w3.org/2000/svg'
const TOLERANCE = 0.011

function svg(tag, attrs = {}) {
    const e = document.createElementNS(SVG, tag)
    for (const [k, v] of Object.entries(attrs))
        e.setAttribute(k, v)
    return e
}

// "0.4", ".4", "40%", "2/5" all mean the same.
function number(text) {
    const t = String(text).trim().replace(',', '.')
    if (t === '')
        return 0
    let m = t.match(/^(-?\d*\.?\d+)\s*%$/)
    if (m)
        return parseFloat(m[1]) / 100
    m = t.match(/^(-?\d*\.?\d+)\s*\/\s*(\d*\.?\d+)$/)
    if (m)
        return parseFloat(m[2]) ? parseFloat(m[1]) / parseFloat(m[2]) : NaN
    return /^-?\d*\.?\d+$/.test(t) ? parseFloat(t) : NaN
}

function fixed(x) {
    return (Math.round(x * 1000) / 1000).toString()
}

class MarkovTool {
    constructor(root, q) {
        this.q = q
        this.n = q.states.length
        this.build = q.task === 'build'
        this.matrix = this.build ? q.states.map(() => q.states.map(() => 0)) : q.matrix.map(r => r.slice())
        this.picked = -1
        const w = 360, h = 300
        this.size = { w, h }
        const radius = this.n <= 2 ? 80 : 110
        this.pos = q.states.map((_, i) => {
            const a = -Math.PI / 2 + (2 * Math.PI * i) / this.n + (this.n === 2 ? Math.PI / 2 : 0)
            return { x: w / 2 + radius * Math.cos(a), y: h / 2 + radius * Math.sin(a) }
        })

        this.svg = svg('svg', { class: 'diagram', viewBox: `0 0 ${w} ${h}` })
        const defs = svg('defs')
        const marker = svg('marker', { id: 'arrow', viewBox: '0 0 10 10', refX: '9', refY: '5', markerWidth: '7',
                                       markerHeight: '7', orient: 'auto-start-reverse' })
        marker.append(svg('path', { d: 'M0,0 L10,5 L0,10 z', class: 'arrowhead' }))
        defs.append(marker)
        this.svg.append(defs)
        this.edgeLayer = svg('g')
        this.nodeLayer = svg('g')
        this.svg.append(this.edgeLayer, this.nodeLayer)

        this.hint = el('div', { class: 'hint', text: this.build ? tr('Drag states to move them. Click one state, then another, to set the chance of moving between them.') : '' })
        this.table = el('table', { class: 'matrix' })
        this.cells = []
        this.sums = []
        this.answerInputs = []
        const parts = [el('div', { class: 'label', text: this.build ? tr('Transition probabilities (row: from, column: to)') : tr('The chain') }), this.table]
        if (!this.build) {
            const prompt = q.task === 'steady' ? tr('Long-run share of time in each state')
                         : tr('Chance of being in each state after %1 steps from %2', q.steps, q.states[q.start])
            const row = el('div', { class: 'answers' })
            q.states.forEach((s, i) => {
                const input = el('input', { class: 'cell', inputmode: 'decimal', placeholder: '0' })
                input.addEventListener('input', () => this.answerSum())
                this.answerInputs.push(input)
                row.append(el('label', { class: 'answer' }, el('span', { text: s }), input))
            })
            this.answerTotal = el('div', { class: 'sum' })
            parts.push(el('div', { class: 'label', text: prompt }), row, this.answerTotal,
                       el('div', { class: 'hint', text: tr('Decimals, percentages, or fractions like 1/3.') }))
        }
        root.append(el('div', { class: 'markov-tool' },
                       el('div', { class: 'diagram-wrap' }, this.svg, this.hint),
                       el('div', { class: 'side' }, ...parts)))
        this.makeTable()
        this.draw()
    }

    makeTable() {
        const head = el('tr', {}, el('th'))
        this.q.states.forEach(s => head.append(el('th', { text: s })))
        if (this.build)
            head.append(el('th', { text: tr('Sum') }))
        this.table.append(head)
        this.q.states.forEach((s, i) => {
            const tr_ = el('tr', {}, el('th', { text: s }))
            this.cells.push([])
            this.q.states.forEach((_, j) => {
                if (this.build) {
                    const input = el('input', { class: 'cell', inputmode: 'decimal', placeholder: '0' })
                    input.addEventListener('input', () => {
                        const v = number(input.value)
                        this.matrix[i][j] = isNaN(v) ? 0 : v
                        input.classList.toggle('bad', isNaN(v) || v < 0 || v > 1)
                        input.classList.remove('wrong', 'right')
                        this.draw()
                    })
                    input.addEventListener('focus', () => { this.focus = [i, j]; this.draw() })
                    input.addEventListener('blur', () => { this.focus = null; this.draw() })
                    this.cells[i].push(input)
                    tr_.append(el('td', {}, input))
                } else {
                    tr_.append(el('td', { class: 'given', text: fixed(this.matrix[i][j]) }))
                }
            })
            if (this.build) {
                const sum = el('td', { class: 'sum' })
                this.sums.push(sum)
                tr_.append(sum)
            }
            this.table.append(tr_)
        })
    }

    answerSum() {
        const total = this.answerInputs.reduce((t, inp) => t + (number(inp.value) || 0), 0)
        this.answerTotal.textContent = tr('Total: %1', fixed(total))
        this.answerTotal.classList.toggle('ok', Math.abs(total - 1) < TOLERANCE)
    }

    draw() {
        this.edgeLayer.replaceChildren()
        this.nodeLayer.replaceChildren()
        const R = 24
        for (let i = 0; i < this.n; ++i) {
            if (this.build) {
                const sum = this.matrix[i].reduce((a, b) => a + b, 0)
                this.sums[i].textContent = fixed(sum)
                this.sums[i].classList.toggle('ok', Math.abs(sum - 1) < TOLERANCE)
            }
            for (let j = 0; j < this.n; ++j) {
                const p = this.matrix[i][j]
                const focused = this.focus && this.focus[0] === i && this.focus[1] === j
                if (!(p > 0) && !focused)
                    continue
                const a = this.pos[i], b = this.pos[j]
                const cls = 'edge' + (focused ? ' focused' : '')
                let path, lx, ly
                if (i === j) {
                    // A loop on the side away from the middle.
                    const cx = this.size.w / 2, cy = this.size.h / 2
                    const ang = Math.atan2(a.y - cy, a.x - cx)
                    const ux = Math.cos(ang), uy = Math.sin(ang)
                    const s1 = { x: a.x + R * Math.cos(ang - 0.5), y: a.y + R * Math.sin(ang - 0.5) }
                    const s2 = { x: a.x + R * Math.cos(ang + 0.5), y: a.y + R * Math.sin(ang + 0.5) }
                    const c1 = { x: s1.x + 50 * Math.cos(ang - 0.6), y: s1.y + 50 * Math.sin(ang - 0.6) }
                    const c2 = { x: s2.x + 50 * Math.cos(ang + 0.6), y: s2.y + 50 * Math.sin(ang + 0.6) }
                    path = `M${s1.x},${s1.y} C${c1.x},${c1.y} ${c2.x},${c2.y} ${s2.x},${s2.y}`
                    lx = a.x + ux * (R + 46)
                    ly = a.y + uy * (R + 46)
                } else {
                    // Curved, so the edges both ways between two states don't overlap.
                    const dx = b.x - a.x, dy = b.y - a.y, len = Math.hypot(dx, dy)
                    const ux = dx / len, uy = dy / len, nx = -uy, ny = ux
                    const bend = 18
                    const s = { x: a.x + ux * R + nx * 6, y: a.y + uy * R + ny * 6 }
                    const e = { x: b.x - ux * (R + 2) + nx * 6, y: b.y - uy * (R + 2) + ny * 6 }
                    const c = { x: (a.x + b.x) / 2 + nx * bend * 2, y: (a.y + b.y) / 2 + ny * bend * 2 }
                    path = `M${s.x},${s.y} Q${c.x},${c.y} ${e.x},${e.y}`
                    lx = (a.x + b.x) / 2 + nx * bend * 1.5
                    ly = (a.y + b.y) / 2 + ny * bend * 1.5
                }
                this.edgeLayer.append(svg('path', { d: path, class: cls, 'marker-end': 'url(#arrow)' }))
                const label = svg('text', { x: lx, y: ly + 4, class: 'edge-label' + (focused ? ' focused' : '') })
                label.textContent = p > 0 ? fixed(p) : '?'
                this.edgeLayer.append(label)
            }
        }
        this.q.states.forEach((s, i) => {
            const g = svg('g', { class: 'node' + (this.picked === i ? ' picked' : ''),
                                 transform: `translate(${this.pos[i].x},${this.pos[i].y})` })
            g.append(svg('circle', { r: R }))
            const t = svg('text', { y: 4 })
            t.textContent = s.length > 7 ? s.slice(0, 6) + '…' : s
            g.append(t)
            const title = svg('title')
            title.textContent = s
            g.append(title)
            g.addEventListener('pointerdown', e => this.grab(e, i, g))
            this.nodeLayer.append(g)
        })
    }

    // Dragging moves a state; a click without moving picks it, and a second
    // click on a state jumps to the cell for that pair.
    grab(e, i, g) {
        e.preventDefault()
        const pt = this.svg.createSVGPoint()
        const toSvg = ev => {
            pt.x = ev.clientX
            pt.y = ev.clientY
            return pt.matrixTransform(this.svg.getScreenCTM().inverse())
        }
        const start = toSvg(e), origin = { ...this.pos[i] }
        let moved = false
        const move = ev => {
            const p = toSvg(ev)
            if (Math.hypot(p.x - start.x, p.y - start.y) > 3)
                moved = true
            this.pos[i] = { x: Math.max(26, Math.min(this.size.w - 26, origin.x + p.x - start.x)),
                            y: Math.max(26, Math.min(this.size.h - 26, origin.y + p.y - start.y)) }
            this.draw()
        }
        const up = () => {
            window.removeEventListener('pointermove', move)
            window.removeEventListener('pointerup', up)
            if (moved || !this.build)
                return
            if (this.picked < 0) {
                this.picked = i
                this.draw()
            } else {
                const from = this.picked
                this.picked = -1
                const cell = this.cells[from][i]
                cell.focus()
                cell.select()
            }
        }
        window.addEventListener('pointermove', move)
        window.addEventListener('pointerup', up)
    }

    // The right value, under an input that was wrong.
    should(input, value) {
        let note = input.parentElement.querySelector('.should')
        if (!note) {
            note = el('div', { class: 'should' })
            input.parentElement.append(note)
        }
        note.textContent = value
    }

    check() {
        const q = this.q
        if (this.build) {
            for (let i = 0; i < this.n; ++i) {
                for (let j = 0; j < this.n; ++j)
                    if (isNaN(number(this.cells[i][j].value)))
                        return { correct: false, usable: true, answer: '', checkedNothing: true, summary: tr('“%1” is not a number.', this.cells[i][j].value) }
                const sum = this.matrix[i].reduce((a, b) => a + b, 0)
                if (Math.abs(sum - 1) >= TOLERANCE)
                    return { correct: false, usable: true, answer: '', checkedNothing: true,
                             summary: tr('The chances out of %1 add up to %2; they must add up to 1.', q.states[i], fixed(sum)) }
            }
            let wrong = 0
            for (let i = 0; i < this.n; ++i)
                for (let j = 0; j < this.n; ++j) {
                    const ok = Math.abs(this.matrix[i][j] - q.expected[i][j]) < TOLERANCE
                    wrong += ok ? 0 : 1
                    this.cells[i][j].classList.toggle('wrong', !ok)
                    this.cells[i][j].classList.toggle('right', ok)
                    this.should(this.cells[i][j], ok ? '' : fixed(q.expected[i][j]))
                }
            return { correct: wrong === 0, usable: true, answer: '',
                     summary: wrong === 0 ? tr('Every transition is right.') : tr('%1 of %2 transitions are off; the right values are under the red ones.', wrong, this.n * this.n) }
        }
        let wrong = 0
        this.answerInputs.forEach((input, i) => {
            const v = number(input.value)
            const ok = !isNaN(v) && Math.abs(v - q.expected[i]) < TOLERANCE
            wrong += ok ? 0 : 1
            input.classList.toggle('wrong', !ok)
            input.classList.toggle('right', ok)
            this.should(input, ok ? '' : fixed(q.expected[i]))
        })
        const values = q.states.map((s, i) => s + ' ' + fixed(q.expected[i])).join(', ')
        return { correct: wrong === 0, usable: true, answer: '',
                 summary: wrong === 0 ? tr('All right: %1.', values) : tr('The answer is %1.', values) }
    }
}
window.MarkovTool = MarkovTool
