// Draws the AI's answers so they're easy to take in: formulas typeset with
// KaTeX instead of plain-text numbers, Mermaid diagrams for processes and
// cycles, charts for numbers that depend on each other, callout boxes and
// numbered steps. Markdown comes in through Rich.show(); the page reports
// its height so the QML side can size it like a label (src/RichText.qml).
//
// Everything is drawn from Shelf's own files; the page can't reach the
// network (see the scheme handler's Content-Security-Policy).
'use strict'

var Rich = (function () {
    let host = null
    let accent = '#c678dd'
    const svgCache = new Map()   // diagram source → svg (or null when it can't be drawn)
    let strings = {}
    const tr = s => strings[s] || s

    // --- Maths -----------------------------------------------------------

    function math(tex, display) {
        try {
            return katex.renderToString(tex, {
                displayMode: display, throwOnError: false, strict: 'ignore', output: 'html',
                // \htmlClass lets a lesson light up the term being talked about.
                trust: ctx => ctx.command === '\\htmlClass',
            })
        } catch (e) {
            const span = document.createElement('code')
            span.textContent = tex
            return span.outerHTML
        }
    }

    // $…$, $$…$$, \(…\) and \[…\] as Markdown tokens, so maths inside code
    // stays code and Markdown never sees a formula's underscores.
    const mathExtension = {
        extensions: [{
            name: 'mathBlock', level: 'block',
            start: src => { const m = src.match(/\$\$|\\\[/); return m ? m.index : undefined },
            tokenizer(src) {
                const m = /^\$\$([\s\S]+?)\$\$/.exec(src) || /^\\\[([\s\S]+?)\\\]/.exec(src)
                if (m) return { type: 'mathBlock', raw: m[0], text: m[1].trim() }
            },
            renderer: t => '<div class="math-block">' + math(t.text, true) + '</div>',
        }, {
            name: 'mathInline', level: 'inline',
            start: src => { const m = src.match(/\$|\\\(/); return m ? m.index : undefined },
            tokenizer(src) {
                let m = /^\$\$([^$]+?)\$\$/.exec(src)
                if (m) return { type: 'mathInline', raw: m[0], text: m[1].trim(), display: true }
                m = /^\\\(([\s\S]+?)\\\)/.exec(src)
                if (m) return { type: 'mathInline', raw: m[0], text: m[1].trim() }
                // A dollar sign opens maths only when it touches what follows
                // and the closing one touches what precedes it and isn't
                // followed by a digit, so "$5 and $10" stays money.
                m = /^\$(?=\S)((?:\\\$|[^$\n])+?)(?<=\S)\$(?!\d)/.exec(src)
                if (m) return { type: 'mathInline', raw: m[0], text: m[1] }
            },
            renderer: t => t.display ? '<span class="math-block">' + math(t.text, true) + '</span>'
                // A bare number needs no typesetting: "$2$" is just 2.
                : /^-?\d+([.,]\d+)?$/.test(t.text.trim()) ? t.text.trim() : math(t.text, false),
        }],
    }

    // --- Diagrams ----------------------------------------------------------

    let mermaidReady = false
    // 'clean', or a board lessons are written on: 'whiteboard',
    // 'chalkboard', 'notebook'. Boards draw diagrams by hand, in their ink.
    let board = 'clean'
    const boardInks = {
        whiteboard: { dark: false, ink: '#1d2733', edge: '#1f5fbf', fill: '#ffffff', second: '#1e8449', note: '#c0392b' },
        notebook: { dark: false, ink: '#22305c', edge: '#2b4c9b', fill: '#fffef8', second: '#b0306a', note: '#c0392b' },
        chalkboard: { dark: true, ink: '#f2f1e8', edge: '#f6e27a', fill: '#2a4236', second: '#9fd8f2', note: '#f4a6b8' },
    }
    function setUpMermaid() {
        if (mermaidReady || typeof mermaid === 'undefined') return
        const b = boardInks[board]
        if (b) {
            mermaid.initialize({
                startOnLoad: false, securityLevel: 'strict', theme: 'base', look: 'handDrawn', handDrawnSeed: 7,
                fontFamily: 'Caveat, "Comic Neue", cursive',
                themeVariables: {
                    darkMode: b.dark, background: 'transparent', fontSize: '22px', fontFamily: 'Caveat, cursive',
                    primaryColor: b.fill, primaryTextColor: b.ink, primaryBorderColor: b.edge,
                    secondaryColor: b.fill, secondaryTextColor: b.ink, secondaryBorderColor: b.second,
                    tertiaryColor: b.fill, tertiaryTextColor: b.ink, tertiaryBorderColor: b.ink,
                    lineColor: b.ink, textColor: b.ink, mainBkg: b.fill, nodeBorder: b.edge,
                    clusterBkg: 'transparent', clusterBorder: b.ink, edgeLabelBackground: b.fill,
                    noteBkgColor: b.fill, noteTextColor: b.ink, noteBorderColor: b.note,
                    actorBkg: b.fill, actorBorder: b.edge, actorTextColor: b.ink, signalColor: b.ink, signalTextColor: b.ink,
                    labelBoxBkgColor: b.fill, labelTextColor: b.ink,
                    cScale0: b.edge, cScale1: b.second, cScale2: b.note, pie1: b.edge, pie2: b.second, pie3: b.note,
                },
                flowchart: { curve: 'basis', padding: 12, htmlLabels: false, nodeSpacing: 34, rankSpacing: 38 },
            })
            mermaidReady = true
            return
        }
        mermaid.initialize({
            startOnLoad: false, securityLevel: 'strict', theme: 'base',
            fontFamily: 'Roboto, "Noto Sans", system-ui, sans-serif',
            themeVariables: {
                darkMode: true, background: 'transparent', fontSize: '14px',
                primaryColor: '#2a2a33', primaryTextColor: '#e8e6ea', primaryBorderColor: accent,
                secondaryColor: '#23303a', secondaryTextColor: '#e8e6ea', secondaryBorderColor: '#4fc3f7',
                tertiaryColor: '#24242b', tertiaryTextColor: '#e8e6ea', tertiaryBorderColor: '#55535c',
                lineColor: '#8a8892', textColor: '#e8e6ea', mainBkg: '#2a2a33', nodeBorder: accent,
                clusterBkg: '#1f1f26', clusterBorder: '#3a3a44', edgeLabelBackground: '#18181d',
                noteBkgColor: '#2c2a22', noteTextColor: '#e8e6ea', noteBorderColor: '#f5b347',
                actorBkg: '#2a2a33', actorBorder: accent, actorTextColor: '#e8e6ea', signalColor: '#c8c8ce',
                signalTextColor: '#e8e6ea', labelBoxBkgColor: '#2a2a33', labelTextColor: '#e8e6ea',
                cScale0: accent, cScale1: '#4fc3f7', cScale2: '#81c784', cScale3: '#f5b347',
                pie1: accent, pie2: '#4fc3f7', pie3: '#81c784', pie4: '#f5b347', pie5: '#ef9a9a',
            },
            flowchart: { curve: 'basis', padding: 10, htmlLabels: false, nodeSpacing: 28, rankSpacing: 30 },
            themeCSS: '.node rect, .cluster rect { rx: 7px; ry: 7px; } .edgeLabel { font-size: 12px; }',
        })
        mermaidReady = true
    }

    // A long left-to-right flowchart turns into a strip of tiny boxes in a
    // narrow panel; there it reads better top to bottom.
    function fitWidth(source) {
        const m = /^(flowchart|graph)\s+(LR|RL)\b/.exec(source)
        if (!m || document.documentElement.clientWidth >= 620) return source
        const nodes = new Set(source.split('\n').slice(1).join('\n').match(/\b[A-Za-z_][\w]*(?=\s*[\[({>])/g) || [])
        return nodes.size > 3 ? source.replace(m[0], m[1] + ' TD') : source
    }

    let diagramCount = 0
    // Resolves to the SVG, or null when the model's diagram can't be drawn.
    async function diagram(source) {
        source = fitWidth(source.trim())
        if (svgCache.has(source)) return svgCache.get(source)
        setUpMermaid()
        let svg = null
        try {
            if (await mermaid.parse(source, { suppressErrors: true })) {
                const out = await mermaid.render('diagram' + (++diagramCount), source)
                svg = out.svg
            }
        } catch (e) {
            svg = null
        }
        // Mermaid leaves a scratch element behind when it fails.
        document.querySelectorAll('body > [id^="ddiagram"], body > [id^="diagram"]').forEach(n => n.remove())
        svgCache.set(source, svg)
        return svg
    }

    // --- Charts ------------------------------------------------------------

    const cssVar = (name, fallback) => getComputedStyle(document.documentElement).getPropertyValue(name).trim() || fallback
    const palette = () => {
        const custom = cssVar('--chart-colors', '')
        return custom ? custom.split(',').map(c => c.trim()) : [accent, '#4fc3f7', '#81c784', '#f5b347', '#ef9a9a']
    }

    function nice(v) {
        if (!isFinite(v)) return ''
        const a = Math.abs(v)
        if (a !== 0 && (a >= 1e6 || a < 1e-3)) return v.toExponential(1)
        return String(+v.toPrecision(4))
    }

    // {type: "line"|"bar", title, xLabel, yLabel, x: [...], series: [{name, values}]}
    // or a formula: {formula | formulas: [{name, formula}], variable, from, to, ...}.
    function chartData(spec) {
        if (spec.formula || spec.formulas) {
            const variable = spec.variable || 'x'
            const from = Number(spec.from ?? 0), to = Number(spec.to ?? 10)
            const list = spec.formulas || [{ name: spec.name || spec.yLabel || '', formula: spec.formula }]
            const x = []
            for (let i = 0; i <= 120; ++i) x.push(from + (to - from) * i / 120)
            const series = list.map(f => {
                const node = Expr.parse(f.formula)
                return { name: f.name || '', values: x.map(v => { try { return Expr.evaluate(node, { [variable]: v }) } catch (e) { return NaN } }) }
            })
            return { type: 'line', x, series, xLabel: spec.xLabel || variable, yLabel: spec.yLabel || '', title: spec.title }
        }
        const series = (spec.series || []).map(s => ({ name: s.name || '', values: (s.values || []).map(Number) }))
        return { type: spec.type === 'bar' ? 'bar' : 'line', x: spec.x || [], series, xLabel: spec.xLabel || '',
                 yLabel: spec.yLabel || '', title: spec.title }
    }

    // Draws a chart into a canvas; `upTo` (0–1) draws only that much of it,
    // for lessons that draw a chart as they talk about it.
    function drawChart(canvas, data, upTo = 1) {
        const box = canvas.getBoundingClientRect()
        const dpr = window.devicePixelRatio || 1
        canvas.width = Math.max(1, box.width * dpr)
        canvas.height = Math.max(1, box.height * dpr)
        const g = canvas.getContext('2d')
        g.scale(dpr, dpr)
        const W = box.width, H = box.height, L = 54, R = 14, T = 12, B = data.xLabel ? 40 : 26
        const all = data.series.flatMap(s => s.values).filter(isFinite)
        if (!all.length) return false
        let lo = Math.min(0, ...all), hi = Math.max(...all)
        if (hi - lo < 1e-12) { hi += 1; lo -= 1 }
        // Round the axis to steps of 1, 2 or 5 times a power of ten.
        const raw = (hi - lo) / 4, mag = Math.pow(10, Math.floor(Math.log10(raw)))
        const step = [1, 2, 2.5, 5, 10].map(k => k * mag).find(k => k >= raw)
        lo = Math.floor(lo / step) * step
        hi = Math.ceil(hi / step) * step
        const ticks = Math.round((hi - lo) / step)
        const numericX = data.type === 'line' && data.x.every(v => isFinite(Number(v)))
        const xs = numericX ? data.x.map(Number) : data.x.map((_, i) => i)
        const x0 = Math.min(...xs), x1 = Math.max(...xs)
        const X = data.type === 'bar'
            ? i => L + (i + 0.5) * (W - L - R) / Math.max(1, data.x.length)
            : v => L + (x1 === x0 ? 0.5 : (v - x0) / (x1 - x0)) * (W - L - R)
        const Y = y => T + (hi - y) / (hi - lo) * (H - T - B)
        g.font = cssVar('--chart-font', '11px Roboto, sans-serif')
        g.fillStyle = cssVar('--chart-text', '#7d7b84')
        g.strokeStyle = cssVar('--chart-grid', '#2c2c33')
        g.lineWidth = 1
        for (let i = 0; i <= ticks; ++i) {
            const y = lo + step * i
            g.beginPath(); g.moveTo(L, Y(y)); g.lineTo(W - R, Y(y)); g.stroke()
            g.textAlign = 'right'
            g.fillText(nice(y), L - 6, Y(y) + 4)
        }
        g.textAlign = 'center'
        if (data.type === 'bar' || !numericX) {
            data.x.forEach((label, i) => g.fillText(String(label).slice(0, 14), X(data.type === 'bar' ? i : xs[i]), H - B + 16))
        } else {
            for (let i = 0; i <= 4; ++i) {
                const v = x0 + (x1 - x0) * i / 4
                g.fillText(nice(v), X(v), H - B + 16)
            }
        }
        if (data.xLabel) g.fillText(data.xLabel, L + (W - L - R) / 2, H - 6)
        if (data.yLabel) {
            g.save(); g.translate(11, T + (H - T - B) / 2); g.rotate(-Math.PI / 2)
            g.fillText(data.yLabel, 0, 0); g.restore()
        }
        const colors = palette()
        if (data.type === 'bar') {
            const n = data.series.length, slot = (W - L - R) / Math.max(1, data.x.length)
            const w = Math.min(36, slot * 0.7 / n)
            data.series.forEach((s, k) => {
                g.fillStyle = colors[k % colors.length]
                s.values.forEach((v, i) => {
                    if (!isFinite(v)) return
                    const h = (Y(0) - Y(v)) * upTo
                    const x = X(i) - n * w / 2 + k * w
                    g.fillRect(x + 1, Y(0) - h, w - 2, h)
                })
            })
        } else {
            data.series.forEach((s, k) => {
                g.strokeStyle = colors[k % colors.length]
                g.lineWidth = parseFloat(cssVar('--chart-line', '2.2'))
                g.lineJoin = 'round'
                g.beginPath()
                const last = Math.floor((xs.length - 1) * upTo)
                let pen = false
                for (let i = 0; i <= last; ++i) {
                    const v = s.values[i]
                    if (!isFinite(v)) { pen = false; continue }
                    pen ? g.lineTo(X(xs[i]), Y(v)) : g.moveTo(X(xs[i]), Y(v))
                    pen = true
                }
                g.stroke()
                if (xs.length <= 24) {
                    g.fillStyle = g.strokeStyle
                    for (let i = 0; i <= last; ++i)
                        if (isFinite(s.values[i])) { g.beginPath(); g.arc(X(xs[i]), Y(s.values[i]), 3, 0, 2 * Math.PI); g.fill() }
                }
            })
        }
        return true
    }

    function chart(spec) {
        const fig = document.createElement('div')
        fig.className = 'figure chart'
        let data
        try {
            data = chartData(typeof spec === 'string' ? JSON.parse(spec) : spec)
        } catch (e) {
            return null
        }
        if (!data.series.length) return null
        const canvas = document.createElement('canvas')
        fig.appendChild(canvas)
        if (data.series.length > 1 || data.series[0].name) {
            const legend = document.createElement('div')
            legend.className = 'legend'
            const colors = palette()
            data.series.forEach((s, k) => {
                const item = document.createElement('span')
                item.textContent = s.name
                item.style.setProperty('--swatch', colors[k % colors.length])
                legend.appendChild(item)
            })
            fig.appendChild(legend)
        }
        if (data.title) {
            const cap = document.createElement('div')
            cap.className = 'caption'
            cap.textContent = data.title
            fig.appendChild(cap)
        }
        fig._chart = data
        return fig
    }

    // --- Markdown ----------------------------------------------------------

    const renderer = {
        code(code, lang) {
            const language = (lang || '').trim().toLowerCase()
            const escaped = code.replace(/&/g, '&amp;').replace(/</g, '&lt;')
            if (language === 'mermaid')
                return '<div class="figure diagram" data-source="' + encodeURIComponent(code) + '"></div>'
            if (language === 'chart')
                return '<div class="chart-spec" data-spec="' + encodeURIComponent(code) + '"></div>'
            if (language === 'math' || language === 'latex' || language === 'tex')
                return '<div class="math-block">' + math(code, true) + '</div>'
            return '<pre><code>' + escaped + '</code></pre>'
        },
    }

    let configured = false
    function configure() {
        if (configured) return
        marked.use({ gfm: true, breaks: false, renderer }, mathExtension)
        configured = true
    }

    // Callouts: a blockquote opening with "Key term:", "Example:", "Note:",
    // "Warning:" or "Tip:" (bold or not), or GitHub's [!NOTE] form.
    const calloutKinds = [
        [/^(key (term|idea|point)s?|definition|remember|important)\b/i, 'key'],
        [/^(example|for example|worked example)\b/i, 'example'],
        [/^(warning|careful|caution|common mistake|watch out)\b/i, 'warning'],
        [/^(tip|hint|note)\b/i, 'tip'],
    ]
    function decorate(root) {
        root.querySelectorAll('blockquote').forEach(q => {
            const first = q.querySelector('p')
            if (!first) return
            let text = first.textContent.trim()
            const gh = /^\[!(\w+)\]\s*/.exec(text)
            if (gh) text = gh[1] + ':' + text.slice(gh[0].length)
            for (const [re, kind] of calloutKinds) {
                const m = re.exec(text)
                if (!m) continue
                q.classList.add(kind)
                // Move the label out of the sentence into a small heading.
                const strong = first.querySelector('strong')
                const title = document.createElement('span')
                title.className = 'callout-title'
                if (gh) {
                    first.innerHTML = first.innerHTML.replace(/^\s*\[!\w+\]\s*/, '')
                    title.textContent = gh[1]
                } else if (strong && first.firstChild === strong) {
                    title.textContent = strong.textContent.replace(/[:.]\s*$/, '')
                    strong.remove()
                    first.innerHTML = first.innerHTML.replace(/^\s*[:.—-]?\s*/, '')
                } else {
                    const label = /^[^:]{1,30}:/.exec(text)
                    if (!label) break
                    title.textContent = label[0].slice(0, -1)
                    first.innerHTML = first.innerHTML.replace(/^[^:]{1,30}:\s*/, '')
                }
                q.insertBefore(title, q.firstChild)
                break
            }
        })
        // Citations ([1], [2]) as small links to their passages.
        root.querySelectorAll('a[href^="source:"]').forEach(a => a.classList.add('source'))
    }

    // --- Showing an answer ---------------------------------------------------

    const content = () => document.getElementById('content')
    let shown = ''
    let pendingDraw = null

    // `options`: {partial: still being written, sources: [{n, label, text}]}.
    async function show(text, options = {}) {
        configure()
        const root = content()
        const html = marked.parse(text || '')
        root.innerHTML = html
        decorate(root)
        if (options.sources && options.sources.length) {
            const box = document.createElement('div')
            box.className = 'sources'
            box.innerHTML = '<div class="label"></div>'
            box.firstChild.textContent = tr('SOURCES')
            for (const s of options.sources) {
                const a = document.createElement('a')
                a.href = 'source:' + s.n
                const b = document.createElement('b')
                b.textContent = '[' + s.n + ']'
                a.appendChild(b)
                a.appendChild(document.createTextNode((s.label ? s.label + ' — ' : '') + String(s.text || '').replace(/\s+/g, ' ').slice(0, 110) + '…'))
                box.appendChild(a)
            }
            root.appendChild(box)
        }
        // Charts draw at once; diagrams wait until the answer is finished,
        // since half a diagram can't be drawn.
        root.querySelectorAll('.chart-spec').forEach(el => {
            const fig = chart(decodeURIComponent(el.dataset.spec))
            if (fig) {
                el.replaceWith(fig)
                requestAnimationFrame(() => drawChart(fig.querySelector('canvas'), fig._chart))
            } else {
                el.outerHTML = '<pre><code></code></pre>'
            }
        })
        const figures = [...root.querySelectorAll('.figure.diagram')]
        const token = {}
        pendingDraw = token
        for (const fig of figures) {
            const source = decodeURIComponent(fig.dataset.source)
            if (options.partial && !svgCache.has(source.trim())) {
                fig.classList.add('pending')
                fig.textContent = tr('Drawing the diagram…')
                continue
            }
            const svg = await diagram(source)
            if (pendingDraw !== token) return   // a newer answer has replaced this one
            if (svg) {
                fig.innerHTML = svg
                if (shown !== text) fig.classList.add('enter')
            } else {
                fig.classList.add('failed')
                fig.textContent = tr('This diagram could not be drawn. What the AI wrote:')
                const pre = document.createElement('pre')
                pre.textContent = source
                fig.appendChild(pre)
            }
        }
        if (!options.partial) shown = text
        report()
    }

    // --- Talking to Shelf ------------------------------------------------------

    let lastHeight = -1
    function report() {
        const h = Math.ceil(content().getBoundingClientRect().height) + 6
        if (h !== lastHeight && host) {
            lastHeight = h
            host.resized(h)
        }
    }

    function setStyle(style) {
        if (style.accent) {
            accent = style.accent
            document.documentElement.style.setProperty('--accent', accent)
            mermaidReady = false
            svgCache.clear()
        }
        if (style.background) document.documentElement.style.setProperty('--bg', style.background)
        if (style.size) document.documentElement.style.setProperty('--size', style.size + 'px')
        if (style.board !== undefined && style.board !== board) {
            board = style.board || 'clean'
            mermaidReady = false
            svgCache.clear()
        }
        if (style.strings) strings = style.strings
    }

    function start() {
        new ResizeObserver(() => requestAnimationFrame(report)).observe(content())
        // Charts follow the width they're given.
        window.addEventListener('resize', () => {
            document.querySelectorAll('.figure.chart').forEach(f => drawChart(f.querySelector('canvas'), f._chart))
        })
        // Links go to Shelf, which decides what they open.
        document.addEventListener('click', e => {
            const a = e.target.closest('a')
            if (!a) return
            e.preventDefault()
            if (host) host.link(a.getAttribute('href'))
        })
        // The page never scrolls itself; the wheel scrolls what holds it.
        document.addEventListener('wheel', e => {
            if (host) host.wheel(e.deltaY)
            e.preventDefault()
        }, { passive: false })
        if (typeof QWebChannel !== 'undefined' && typeof qt !== 'undefined') {
            new QWebChannel(qt.webChannelTransport, channel => {
                host = channel.objects.host
                host.ready()
            })
        }
    }

    return { show, setStyle, start, math, diagram, chart, chartData, drawChart, report, nice }
})()

// The answer page starts itself; the lesson page (stage.js) only borrows
// the drawing.
const startRich = () => { if (document.getElementById('content')) Rich.start() }
if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', startRich)
else
    startRich()
