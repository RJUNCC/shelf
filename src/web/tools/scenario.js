// A scenario question, built on a formula from the book: variables the
// student can change and outputs Shelf computes (expr.js, never eval).
//   predict  the given values are listed and the student works out one
//            output; afterwards the whole simulator opens to explore
//   find     the student moves one variable with a slider, watching the
//            output and a chart, to reach a target (or a peak, or a low)
'use strict'

function nice(x) {
    if (!isFinite(x)) return '—'
    const a = Math.abs(x)
    if (a !== 0 && (a >= 1e6 || a < 1e-3)) return x.toExponential(3).replace(/\.?0+e/, 'e')
    return parseFloat(x.toPrecision(4)).toString()
}

class ScenarioTool {
    constructor(root, q) {
        this.q = q
        this.run = Expr.compile(q)
        this.values = Expr.givens(q)
        this.vars = Object.fromEntries(q.variables.map(v => [v.name, v]))
        this.outs = Object.fromEntries(q.outputs.map(o => [o.name, o]))
        this.chartVar = q.task === 'find' ? q.vary : q.variables[0].name
        this.root = el('div', { class: 'scenario-tool' })
        root.append(this.root)
        this.build(q.task === 'find')
    }

    label(x) {
        return x.label || x.name
    }

    unit(x) {
        return x.unit ? ' ' + x.unit : ''
    }

    // "$80", but "20 m/s".
    show(value, x) {
        const u = (x && x.unit || '').trim()
        if (/^[$€£¥₹]$/.test(u))
            return (value < 0 ? '−' : '') + u + nice(Math.abs(value))
        return nice(value) + (u ? ' ' + u : '')
    }

    // The question's part: given values and an answer box, plus (when
    // exploring) sliders, live outputs and the chart.
    build(exploring) {
        const q = this.q
        this.exploring = exploring
        this.root.replaceChildren()
        const left = el('div', { class: 'sc-side' })
        this.sliders = {}
        for (const v of q.variables) {
            const locked = !exploring || (q.task === 'find' && !this.revealed && v.name !== q.vary)
            const value = el('span', { class: 'sc-value', text: this.show(this.values[v.name], v) })
            const row = el('div', { class: 'sc-var' + (locked ? ' locked' : '') },
                           el('div', { class: 'sc-name' }, el('span', { text: this.label(v) }), value))
            if (!locked) {
                const step = (v.max - v.min) / 1000
                const slider = el('input', { type: 'range', min: v.min, max: v.max, step, value: this.values[v.name] })
                slider.addEventListener('input', () => {
                    this.values[v.name] = parseFloat(slider.value)
                    value.textContent = this.show(this.values[v.name], v)
                    if (q.task === 'find' && v.name === q.vary && !this.revealed)
                        this.answer.value = nice(this.values[v.name])
                    this.update()
                })
                row.append(slider)
                this.sliders[v.name] = slider
            }
            left.append(row)
        }
        // The answer box.
        const asked = q.task === 'find' ? this.vars[q.vary] : this.outs[q.output]
        const prompt = q.task === 'find'
            ? (q.goal === 'equal' ? tr('%1 that makes %2 = %3', this.label(asked), this.label(this.outs[q.output]), this.show(q.target, this.outs[q.output]))
               : q.goal === 'max' ? tr('%1 that makes %2 as large as it can be', this.label(asked), this.label(this.outs[q.output]))
               : tr('%1 that makes %2 as small as it can be', this.label(asked), this.label(this.outs[q.output])))
            : tr('%1 for these values', this.label(asked))
        this.answer = this.answer || el('input', { class: 'cell sc-answer', inputmode: 'decimal', placeholder: '?' })
        if (q.task === 'find' && !this.answer.value)
            this.answer.value = ''
        left.append(el('div', { class: 'label', text: prompt }),
                    el('div', { class: 'sc-answer-row' }, this.answer, el('span', { class: 'sc-unit', text: asked.unit || '' })))
        if (this.result)
            left.append(this.result)

        const right = el('div', { class: 'sc-main' })
        if (exploring) {
            this.outputsBox = el('div', { class: 'sc-outputs' })
            const chooser = el('select', { class: 'sc-select' })
            for (const v of q.variables) {
                const o = el('option', { value: v.name, text: this.label(v) })
                if (v.name === this.chartVar) o.selected = true
                chooser.append(o)
            }
            chooser.disabled = q.task === 'find' && !this.revealed
            chooser.addEventListener('change', () => { this.chartVar = chooser.value; this.update() })
            this.canvas = el('canvas', { class: 'sc-chart' })
            right.append(this.outputsBox,
                         el('div', { class: 'label' }, document.createTextNode(tr('%1 against', this.label(this.outs[q.output])) + ' '), chooser),
                         this.canvas)
        } else {
            right.append(el('div', { class: 'hint', text: tr('Work it out from the values given. Once you check, the simulator opens so you can see how the answer changes.') }))
        }
        this.root.append(left, right)
        if (exploring)
            requestAnimationFrame(() => this.update())
        setTimeout(() => this.answer.focus(), 50)
    }

    outputsAt(values) {
        try { return this.run(values) } catch (e) { return {} }
    }

    update() {
        const q = this.q
        const out = this.outputsAt(this.values)
        this.outputsBox.replaceChildren(...q.outputs.map(o =>
            el('div', { class: 'sc-out' + (o.name === q.output ? ' main' : '') },
               el('span', { text: this.label(o) }),
               // In a predict question the asked output stays hidden until checked.
               el('b', { text: (o.name === q.output && q.task === 'predict' && !this.revealed) ? '?' : this.show(out[o.name], o) }))))
        this.chart()
    }

    chart() {
        const c = this.canvas, q = this.q
        const box = c.getBoundingClientRect()
        const dpr = window.devicePixelRatio || 1
        c.width = Math.max(1, box.width * dpr)
        c.height = Math.max(1, box.height * dpr)
        const g = c.getContext('2d')
        g.scale(dpr, dpr)
        const W = box.width, H = box.height, L = 52, R = 12, T = 10, B = 28
        const v = this.vars[this.chartVar]
        const xs = [], ys = []
        for (let i = 0; i <= 200; ++i) {
            const x = v.min + (v.max - v.min) * i / 200
            const vals = Object.assign({}, this.values, { [v.name]: x })
            xs.push(x)
            ys.push(this.outputsAt(vals)[q.output])
        }
        const finite = ys.filter(isFinite)
        if (!finite.length) return
        let lo = Math.min(...finite), hi = Math.max(...finite)
        if (q.task === 'find' && q.goal === 'equal') { lo = Math.min(lo, q.target); hi = Math.max(hi, q.target) }
        if (hi - lo < 1e-12) { hi += 1; lo -= 1 }
        const pad = (hi - lo) * 0.08
        lo -= pad; hi += pad
        const X = x => L + (x - v.min) / (v.max - v.min) * (W - L - R)
        const Y = y => T + (hi - y) / (hi - lo) * (H - T - B)
        const css = getComputedStyle(document.documentElement)
        const muted = '#6f6d76', line = '#2c2c33', accent = css.getPropertyValue('--accent').trim() || '#c678dd'
        g.font = '11px sans-serif'
        g.fillStyle = muted
        g.strokeStyle = line
        g.lineWidth = 1
        for (let i = 0; i <= 4; ++i) {
            const y = lo + (hi - lo) * i / 4
            g.beginPath(); g.moveTo(L, Y(y)); g.lineTo(W - R, Y(y)); g.stroke()
            g.textAlign = 'right'
            g.fillText(nice(y), L - 6, Y(y) + 4)
        }
        g.textAlign = 'center'
        for (let i = 0; i <= 4; ++i) {
            const x = v.min + (v.max - v.min) * i / 4
            g.fillText(nice(x), X(x), H - 10)
        }
        if (q.task === 'find' && q.goal === 'equal') {
            g.setLineDash([4, 4]); g.strokeStyle = muted
            g.beginPath(); g.moveTo(L, Y(q.target)); g.lineTo(W - R, Y(q.target)); g.stroke()
            g.setLineDash([])
        }
        // A predict question doesn't show its curve until checked.
        if (q.task === 'predict' && !this.revealed) return
        g.strokeStyle = accent
        g.lineWidth = 2
        g.beginPath()
        let pen = false
        xs.forEach((x, i) => {
            if (!isFinite(ys[i])) { pen = false; return }
            pen ? g.lineTo(X(x), Y(ys[i])) : g.moveTo(X(x), Y(ys[i]))
            pen = true
        })
        g.stroke()
        const cur = this.values[v.name], cy = this.outputsAt(this.values)[q.output]
        if (isFinite(cy)) {
            g.fillStyle = accent
            g.beginPath(); g.arc(X(cur), Y(cy), 4.5, 0, 2 * Math.PI); g.fill()
        }
    }

    check() {
        const v = number(this.answer.value)
        if (this.answer.value.trim() === '' || isNaN(v))
            return { correct: false, usable: true, answer: '', checkedNothing: true, summary: tr('Enter a number first.') }
        const q = this.q
        const correct = Math.abs(v - q.expected) <= q.tolerance
        this.answer.classList.toggle('right', correct)
        this.answer.classList.toggle('wrong', !correct)
        this.answer.readOnly = true
        this.revealed = true
        const asked = q.task === 'find' ? this.vars[q.vary] : this.outs[q.output]
        this.result = el('div', { class: correct ? 'sc-result right' : 'sc-result wrong',
                                  text: tr('Answer: %1', this.show(q.expected, asked)) })
        // Open the simulator with the answer's values, to explore from there.
        if (q.task === 'find')
            this.values[q.vary] = q.expected
        this.build(true)
        return { correct, usable: true, answer: '',
                 summary: correct ? tr('%1 is right.', nice(v)) : tr('The answer is %1.', this.show(q.expected, asked)) }
    }

    reveal() {}
}
window.ScenarioTool = ScenarioTool
