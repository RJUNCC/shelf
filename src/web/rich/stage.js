// Plays a lesson (src/explainer.cpp): one scene at a time, each visual
// building piece by piece while its narration is shown as captions and,
// when a voice is installed, spoken. A sentence's start reveals the next
// part of the visual, so pictures and words stay together.
//
// Nothing animates once a scene has been built: transitions are CSS, and
// the only animation frames are the second or so a chart takes to draw.
'use strict'

var Stage = (function () {
    let host = null
    let lesson = null
    let scene = 0, sentence = -1
    let playing = false
    let speed = 1
    let voice = false
    let captions = true
    let timer = null
    let token = 0            // bumps whenever playback jumps, so stale callbacks do nothing
    let items = []           // the current scene's revealable pieces, in order
    let revealed = 0
    let strings = {}
    let audio = null
    let volume = 1
    let voiceRate = false      // speed the voice up or down in playback (Kokoro)
    let board = 'whiteboard'   // 'clean', 'whiteboard', 'chalkboard' or 'notebook'
    let writeClock = 0         // when the pen is free to write the next thing
    const writing = () => board !== 'clean' && !document.body.classList.contains('reduced')
    const tr = s => strings[s] || s
    const $ = id => document.getElementById(id)

    function el(tag, attrs = {}, ...children) {
        const e = document.createElement(tag)
        for (const [k, v] of Object.entries(attrs)) {
            if (k === 'class') e.className = v
            else if (k === 'html') e.innerHTML = v
            else e.setAttribute(k, v)
        }
        for (const c of children) e.append(c)
        return e
    }

    // Text with chemical formulas given their subscripts: "C2H5OH" becomes
    // C₂H₅OH. Only runs of real element symbols with a number count, so
    // "MP3" or "COVID19" stay as they are.
    const elements = 'He|Li|Be|Ne|Na|Mg|Al|Si|Cl|Ar|Ca|Fe|Cu|Zn|Br|Ag|Sn|Au|Hg|Pb|Mn|Co|Ni|Cr|Ti|H|B|C|N|O|F|P|S|K|I'
    const formula = new RegExp('\\b(?:(?:' + elements + ')\\d*){2,}(?![a-z])', 'g')
    function setText(node, text) {
        const escaped = String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;')
        node.innerHTML = escaped.replace(formula, m => /\d/.test(m) ? m.replace(/(\d+)/g, '<sub>$1</sub>') : m)
    }

    // On a board, text is written a word at a time, so each word gets a span.
    function wrapWords(node) {
        const walker = document.createTreeWalker(node, NodeFilter.SHOW_TEXT)
        const texts = []
        while (walker.nextNode()) texts.push(walker.currentNode)
        for (const t of texts) {
            if (t.parentNode.closest && t.parentNode.closest('.katex')) continue
            const parts = t.textContent.split(/(\s+)/)
            const frag = document.createDocumentFragment()
            for (const part of parts) {
                if (!part) continue
                if (/^\s+$/.test(part)) { frag.append(part); continue }
                const w = document.createElement('span')
                w.className = 'w'
                w.textContent = part
                frag.append(w)
            }
            t.replaceWith(frag)
        }
    }

    // Writes an element out like a hand at the board: word by word with the
    // pen following, or, for a formula or picture, wiped on left to right.
    // Runs once on the Web Animations clock; nothing is left running.
    function writeOut(el) {
        const sceneEl = el.closest('.scene')
        const pen = sceneEl && sceneEl.querySelector('.pen')
        const now = performance.now()
        let t = Math.max(0, writeClock - now)
        const box = sceneEl.getBoundingClientRect()
        const frames = []
        const words = el.classList.contains('w') ? [el] : [...el.querySelectorAll('.w')]
        const stroke = (target, duration, delay) => target.animate(
            [{ clipPath: 'inset(-20% 100% -20% 0)' }, { clipPath: 'inset(-20% 0 -20% 0)' }],
            { duration, delay, fill: 'both', easing: 'ease-in-out' })
        const start = t
        if (words.length === 0) {
            const duration = 1100 / speed
            stroke(el, duration, t)
            const r = el.getBoundingClientRect()
            frames.push([t, r.left - box.left, r.top - box.top + r.height * 0.6], [t + duration, r.right - box.left, r.top - box.top + r.height * 0.6])
            t += duration
        } else {
            for (const w of words) {
                const duration = Math.max(140, w.textContent.length * 58) / speed
                stroke(w, duration, t)
                const r = w.getBoundingClientRect()
                frames.push([t, r.left - box.left, r.bottom - box.top - 2], [t + duration, r.right - box.left, r.bottom - box.top - 2])
                t += duration + 45 / speed
            }
        }
        writeClock = now + t
        if (pen && frames.length) {
            const total = t
            const keys = [{ offset: 0, opacity: 0, transform: `translate(${frames[0][1]}px, ${frames[0][2]}px)` }]
            for (const [at, x, y] of frames)
                keys.push({ offset: Math.min(1, at / total), opacity: 1, transform: `translate(${x}px, ${y}px)` })
            keys.push({ offset: 1, opacity: 0, transform: keys[keys.length - 1].transform })
            // Offsets must rise; the pen waits where it starts until its turn.
            keys[0].offset = 0
            if (start > 0) keys.splice(1, 0, { offset: Math.min(1, start / total) * 0.999, opacity: 0, transform: keys[0].transform })
            pen.animate(keys, { duration: total, fill: 'none' })
        }
        return t
    }

    // A wobbly line drawn under a heading on a board, in the heading's own
    // pixels so it draws in one even stroke.
    function underline(heading, delay) {
        const w = Math.max(40, heading.getBoundingClientRect().width)
        const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg')
        svg.setAttribute('class', 'underline')
        svg.setAttribute('viewBox', `0 0 ${w} 12`)
        svg.setAttribute('width', w)
        svg.setAttribute('height', 12)
        const path = document.createElementNS('http://www.w3.org/2000/svg', 'path')
        path.setAttribute('d', `M2 8 C ${w * 0.2} 3, ${w * 0.4} 11, ${w * 0.6} 6 S ${w * 0.9} 4, ${w - 2} 7`)
        svg.append(path)
        heading.append(svg)
        const len = path.getTotalLength()
        path.style.strokeDasharray = len + ' ' + len
        path.animate([{ strokeDashoffset: len }, { strokeDashoffset: 0 }], { duration: 500 / speed, delay, fill: 'both' })
    }

    // --- Building a scene ------------------------------------------------

    // Returns the scene's element; fills `items` with what reveals in turn.
    function build(s) {
        items = []
        revealed = 0
        const box = el('div', { class: 'scene entering' })
        const heading = el('h2')
        heading.textContent = s.heading || ''
        box.append(heading)
        box.append(el('div', { class: 'pen' }))
        const visual = el('div', { class: 'visual' })
        box.append(visual)
        box._heading = heading
        const list = (cls, entries) => {
            const ul = el('ul', { class: cls })
            for (const text of entries) {
                const li = el('li', { class: 'item' })
                const words = el('span')
                setText(words, text)
                li.append(words)
                ul.append(li)
                items.push({ el: li })
            }
            visual.append(ul)
        }
        switch (s.kind) {
        case 'steps': list('steps', s.steps || []); break
        case 'diagram': {
            const d = el('div', { class: 'diagram' })
            visual.append(d)
            // Drawn asynchronously; its nodes become the items when ready.
            const placeholder = { el: d, pending: true }
            items.push(placeholder)
            Rich.diagram(s.mermaid).then(svg => {
                if (!svg) {
                    // Can't be drawn: its labels as steps instead.
                    const labels = [...new Set((s.mermaid.match(/"([^"]+)"/g) || []).map(t => t.slice(1, -1)))]
                    d.remove()
                    const at = items.indexOf(placeholder)
                    items.splice(at, 1)
                    const ul = el('ul', { class: 'steps' })
                    const added = (labels.length ? labels : [s.heading || '']).map(text => {
                        const li = el('li', { class: 'item' })
                        li.textContent = text
                        ul.append(li)
                        return { el: li }
                    })
                    visual.append(ul)
                    items.splice(at, 0, ...added)
                } else {
                    d.innerHTML = svg
                    // Fill the board, but don't blow a small diagram up past
                    // twice its size.
                    const svgEl = d.querySelector('svg')
                    const box = svgEl.viewBox && svgEl.viewBox.baseVal
                    svgEl.removeAttribute('style')
                    svgEl.setAttribute('width', '100%')
                    svgEl.setAttribute('height', '100%')
                    if (box && box.width) {
                        svgEl.style.maxWidth = (box.width * 2) + 'px'
                        svgEl.style.maxHeight = (box.height * 2) + 'px'
                    }
                    const nodes = [...d.querySelectorAll('g.node')]
                    if (nodes.length === 0) {
                        d.classList.add('whole')
                        placeholder.pending = false
                    } else {
                        const edges = [...d.querySelectorAll('path.flowchart-link, .edgePaths path, path.transition')]
                        const labels = [...d.querySelectorAll('g.edgeLabel')]
                        edges.forEach(p => {
                            const len = p.getTotalLength ? p.getTotalLength() : 0
                            p.style.strokeDasharray = len
                            p.style.strokeDashoffset = len
                            // The arrowhead isn't dashed, so it waits too.
                            p.style.opacity = '0'
                            p.classList.add('drawing')
                        })
                        const at = items.indexOf(placeholder)
                        // Node k arrives with the edge that leads to it.
                        const pieces = nodes.map((n, k) => ({ el: n, edges: k > 0 ? [edges[k - 1], labels[k - 1]].filter(Boolean) : [] }))
                        // Edges beyond the nodes (cycles) come with the last node.
                        if (edges.length >= nodes.length)
                            pieces[pieces.length - 1].edges.push(...edges.slice(nodes.length - 1), ...labels.slice(nodes.length - 1))
                        items.splice(at, 1, ...pieces)
                    }
                }
                // Catch up with what should already be showing.
                const want = revealed
                revealed = 0
                reveal(want, true)
            })
            break
        }
        case 'formula': {
            const f = el('div', { class: 'formula item', html: Rich.math(s.latex, true) })
            visual.append(f)
            items.push({ el: f })
            if ((s.terms || []).length) {
                const grid = el('div', { class: 'terms' })
                for (const t of s.terms) {
                    const sym = el('div', { class: 'sym item', html: Rich.math(t.symbol, false) })
                    const meaning = el('div', { class: 'meaning item' })
                    meaning.textContent = t.meaning
                    grid.append(sym, meaning)
                    items.push({ el: sym, also: [meaning] })
                }
                visual.append(grid)
            }
            break
        }
        case 'graph':
        case 'bars': {
            const boxEl = el('div', { class: 'chart-box item' })
            const canvas = el('canvas')
            boxEl.append(canvas)
            const data = s.kind === 'graph'
                ? Rich.chartData({ formula: s.formula, variable: s.variable, from: s.from, to: s.to, xLabel: s.xLabel, yLabel: s.yLabel })
                : Rich.chartData({ type: 'bar', x: s.labels, series: [{ name: s.unit || '', values: s.values }],
                                   yLabel: s.unit || '' })
            visual.append(boxEl)
            items.push({ el: boxEl, chart: { canvas, data } })
            break
        }
        case 'compare': {
            const grid = el('div', { class: 'compare' })
            const left = el('div', { class: 'side left item' }), right = el('div', { class: 'side right item' })
            const lt = el('h3'), rt = el('h3')
            lt.textContent = s.leftTitle || ''
            rt.textContent = s.rightTitle || ''
            const lu = el('ul'), ru = el('ul')
            left.append(lt, lu)
            right.append(rt, ru)
            grid.append(left, right)
            visual.append(grid)
            items.push({ el: left }, { el: right })
            const rows = Math.max(s.left.length, s.right.length)
            for (let i = 0; i < rows; ++i)
                for (const [ul, entries] of [[lu, s.left], [ru, s.right]])
                    if (i < entries.length) {
                        const li = el('li', { class: 'item' })
                        setText(li, entries[i])
                        ul.append(li)
                        items.push({ el: li })
                    }
            break
        }
        case 'quote': {
            const q = el('div', { class: 'quote item' })
            setText(q, '“' + s.quote + '”')
            visual.append(q)
            items.push({ el: q })
            if (s.source) {
                const src = el('div', { class: 'quote-source item' })
                src.textContent = '— ' + s.source
                visual.append(src)
                items.push({ el: src })
            }
            break
        }
        default: list('points', s.points || [])
        }
        if (board !== 'clean') {
            wrapWords(heading)
            for (const item of items)
                if (!item.pending && !item.chart && !item.el.classList.contains('formula') && !item.el.classList.contains('sym'))
                    wrapWords(item.el)
            for (const item of items) for (const a of item.also || []) wrapWords(a)
        }
        return box
    }

    function drawChart(item, animate) {
        const { canvas, data } = item.chart
        if (!animate || document.body.classList.contains('reduced')) {
            Rich.drawChart(canvas, data, 1)
            return
        }
        const start = performance.now(), mine = token
        const step = now => {
            if (mine !== token) return
            const t = Math.min(1, (now - start) / 1100)
            Rich.drawChart(canvas, data, 1 - Math.pow(1 - t, 3))
            if (t < 1) requestAnimationFrame(step)
        }
        requestAnimationFrame(step)
    }

    // Shows items up to `count`; the newest is pointed at.
    function reveal(count, instant) {
        count = Math.min(count, items.length)
        for (let i = revealed; i < count; ++i) {
            const item = items[i]
            if (item.pending) { revealed = i; return }
            item.el.classList.add('shown')
            for (const a of item.also || []) a.classList.add('shown')
            if (writing() && !instant && !item.chart && !item.el.closest('.diagram')) {
                writeOut(item.el)
                for (const a of item.also || []) writeOut(a)
            }
            for (const e of item.edges || []) {
                if (e.tagName === 'path') {
                    e.style.opacity = '1'
                    e.style.strokeDashoffset = '0'
                } else {
                    e.classList.add('shown')
                }
            }
            if (item.chart) requestAnimationFrame(() => drawChart(item, !instant))
            if (item.el.closest('.diagram')) item.el.closest('.diagram').classList.add('shown')
        }
        revealed = Math.max(revealed, count)
        document.querySelectorAll('.current').forEach(e => e.classList.remove('current'))
        const last = items[revealed - 1]
        if (last && !instant && items.length > 1) {
            last.el.classList.add('current')
            for (const a of last.also || []) a.classList.add('current')
        }
    }

    // --- Playing -----------------------------------------------------------

    function show(index, autoplay) {
        ++token
        clearTimeout(timer)
        stopAudio()
        scene = Math.max(0, Math.min(lesson.scenes.length - 1, index))
        sentence = -1
        const main = $('stage')
        main.querySelectorAll('.scene').forEach(old => {
            old.classList.add('leaving')
            setTimeout(() => old.remove(), 400)
        })
        const box = build(lesson.scenes[scene])
        main.append(box)
        writeClock = 0
        requestAnimationFrame(() => requestAnimationFrame(() => {
            box.classList.remove('entering')
            box._heading.classList.add('shown')
            if (writing() && autoplay && box._heading.textContent.trim()) {
                const t = writeOut(box._heading)
                underline(box._heading, t)
                writeClock += 500 / speed
            } else if (board !== 'clean' && box._heading.textContent.trim()) {
                underline(box._heading, 0)
            }
        }))
        updateDots()
        setCaption('')
        if (autoplay) {
            const mine = token
            timer = setTimeout(() => { if (mine === token) (voice && host ? narrate() : speak(0)) }, 450)
        } else {
            // Paused: the whole scene, to read at leisure.
            requestAnimationFrame(() => reveal(items.length, true))
        }
        report()
    }

    function sentencesOf(s) {
        if (s.sentences && s.sentences.length) return s.sentences
        return String(s.say || '').split(/(?<=[.!?])\s+(?=[A-Z0-9"“])/).filter(Boolean)
    }

    // How long a sentence takes to read aloud, without a voice.
    function readingTime(text) {
        const words = text.split(/\s+/).filter(Boolean).length
        return Math.max(1600, words / 2.6 * 1000 + 450) / speed
    }

    // Without a voice: one sentence after another at reading pace, each
    // revealing its share of the picture.
    function speak(i) {
        const list = sentencesOf(lesson.scenes[scene])
        if (i >= list.length) {
            sceneDone()
            return
        }
        sentence = i
        const mine = token
        setCaption(list[i])
        reveal(Math.ceil((i + 1) * items.length / list.length), false)
        report()
        timer = setTimeout(() => { if (mine === token) speak(i + 1) }, readingTime(list[i]))
    }

    // With a voice, a scene is spoken as one recording, so it flows like a
    // person talking; captions and drawing follow how far it has played.
    function narrate() {
        const s = lesson.scenes[scene]
        const mine = token
        pendingVoice = { scene, token: mine }
        setStatus(tr('Getting the voice ready…'))
        host.speak(scene, -1, sentencesOf(s).join(' '))
        // The next scene is made while this one plays.
        if (scene + 1 < lesson.scenes.length)
            host.prepare(scene + 1, -1, sentencesOf(lesson.scenes[scene + 1]).join(' '))
        // A voice that never answers: go on at reading pace.
        timer = setTimeout(() => {
            if (mine === token && pendingVoice) {
                pendingVoice = null
                setStatus('')
                speak(0)
            }
        }, 45000)
    }

    let pendingVoice = null
    // A scene's recording is ready (`url`), or couldn't be made (empty).
    function voiceReady(sceneIndex, part, url) {
        if (!pendingVoice || pendingVoice.scene !== sceneIndex || !playing)
            return
        const mine = pendingVoice.token
        pendingVoice = null
        clearTimeout(timer)
        setStatus('')
        if (!url) {
            speak(0)
            return
        }
        // Fetched whole before playing: streamed from Shelf, the end of the
        // recording could be cut off.
        fetch(url).then(r => r.blob()).then(blob => {
            if (mine !== token) return
            stopAudio()
            const list = sentencesOf(lesson.scenes[scene])
            // Where each sentence starts, as a share of the scene's words.
            const lengths = list.map(t => t.length + 1)
            const total = lengths.reduce((a, b) => a + b, 0)
            const starts = []
            let sum = 0
            for (const n of lengths) { starts.push(sum / total); sum += n }
            audio = new Audio(URL.createObjectURL(blob))
            audio.volume = volume
            // sherpa-onnx's speech is played faster or slower, keeping its
            // pitch; Piper's is already made at the lesson's pace.
            audio.playbackRate = voiceRate ? speed : 1
            const follow = () => {
                if (mine !== token || !audio || !audio.duration) return
                const at = audio.currentTime / audio.duration
                let i = 0
                while (i + 1 < starts.length && starts[i + 1] <= at + 0.01) ++i
                if (i !== sentence) {
                    sentence = i
                    setCaption(list[i])
                    reveal(Math.ceil((i + 1) * items.length / list.length), false)
                    report()
                }
            }
            audio.ontimeupdate = follow
            audio.onplay = follow
            audio.onended = () => {
                if (mine !== token) return
                URL.revokeObjectURL(audio.src)
                timer = setTimeout(sceneDone, 250)
            }
            audio.onerror = () => { if (mine === token) speak(0) }
            sentence = -1
            audio.play().catch(() => { if (mine === token) speak(0) })
        }).catch(() => { if (mine === token) speak(0) })
    }

    function stopAudio() {
        if (audio) {
            audio.onended = audio.onerror = audio.ontimeupdate = audio.onplay = null
            audio.pause()
            audio = null
        }
        pendingVoice = null
    }

    function setStatus(text) { $('status').textContent = text }

    function sceneDone() {
        reveal(items.length, false)
        if (scene + 1 < lesson.scenes.length) {
            const mine = token
            timer = setTimeout(() => { if (mine === token) show(scene + 1, true) }, 900 / speed)
        } else {
            playing = false
            setCaption('')
            document.querySelectorAll('.current').forEach(e => e.classList.remove('current'))
            updateButtons()
            report('done')
        }
    }

    function play() {
        if (!lesson) return
        if (playing) return
        playing = true
        updateButtons()
        // At the end, start over; otherwise go on from this scene.
        if (scene === lesson.scenes.length - 1 && sentence === -1 && revealed >= items.length && finished)
            show(0, true)
        else
            show(scene, true)
        finished = false
    }
    let finished = false
    function pause() {
        playing = false
        ++token
        clearTimeout(timer)
        stopAudio()
        reveal(items.length, true)
        setCaption('')
        updateButtons()
        report()
    }
    function toggle() { playing ? pause() : play() }
    function next() { if (lesson && scene + 1 < lesson.scenes.length) show(scene + 1, playing) }
    function prev() { if (lesson) show(scene - 1, playing) }
    function replay() { if (lesson) { playing = true; updateButtons(); show(0, true) } }

    function setCaption(text) {
        const c = $('caption')
        c.classList.toggle('hidden', !captions)
        c.classList.add('fade')
        setTimeout(() => { setText(c, text); c.classList.remove('fade') }, 120)
    }

    function updateDots() {
        const dots = $('dots')
        dots.innerHTML = ''
        lesson.scenes.forEach((s, i) => {
            const b = el('button', { title: s.heading || '' })
            if (i < scene) b.classList.add('done')
            if (i === scene) b.classList.add('current')
            b.onclick = () => show(i, playing)
            dots.append(b)
        })
        $('prev').disabled = scene === 0
        $('next').disabled = scene === lesson.scenes.length - 1
    }

    const icons = {
        play: '<svg viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>',
        pause: '<svg viewBox="0 0 24 24"><path d="M7 5h4v14H7zM13 5h4v14h-4z"/></svg>',
    }
    function updateButtons() {
        $('play').innerHTML = playing ? icons.pause : icons.play
        $('play').title = playing ? tr('Pause (Space)') : tr('Play (Space)')
    }

    function report(state) {
        if (state === 'done') finished = true
        if (host) host.state(JSON.stringify({ playing, scene, sentence, scenes: lesson ? lesson.scenes.length : 0, done: finished }))
    }

    // --- Talking to Shelf ------------------------------------------------------

    function load(data, options = {}) {
        lesson = data
        strings = options.strings || {}
        speed = options.speed || 1
        voice = !!options.voice
        voiceRate = !!options.voiceRate
        volume = options.volume !== undefined ? Math.max(0, Math.min(1, options.volume)) : 1
        setBoard(options.board || 'whiteboard')
        $('volume').value = String(Math.round(volume * 100))
        $('volume').title = tr('Volume')
        $('volumeRow').style.display = voice ? 'flex' : 'none'
        menu('board', 'boardMenu', ['whiteboard', 'chalkboard', 'notebook', 'clean'].map(v => (
                 { value: v, label: tr({ whiteboard: 'Whiteboard', chalkboard: 'Chalkboard', notebook: 'Notebook', clean: 'Clean' }[v]) })),
             () => board, value => {
                 setBoard(value)
                 if (host) host.setting('board', board)
                 if (lesson) show(scene, playing)
             })
        $('speed').value = String(speed)
        showSpeed()
        $('speed').title = tr('Speed')
        captions = options.captions !== false
        document.body.classList.toggle('reduced', !!options.reduced)
        if (options.accent) {
            document.documentElement.style.setProperty('--accent', options.accent)
            Rich.setStyle({ accent: options.accent })
        }
        $('title').textContent = lesson.title || ''
        $('cc').classList.toggle('on', captions)
        $('askField').placeholder = tr('Ask about this…')
        document.querySelector('footer .ask').style.visibility = options.ask === false ? 'hidden' : 'visible'
        $('cc').title = tr('Captions')
        $('replay').title = tr('Start again')
        $('prev').title = tr('Previous scene')
        $('next').title = tr('Next scene')
        $('close').textContent = tr('Back to the slide')
        playing = !!options.autoplay
        finished = false
        updateButtons()
        if (voice && host) host.prepare(0, -1, sentencesOf(lesson.scenes[0]).join(' '))
        show(0, playing)
    }

    function setVoice(on) { voice = !!on }

    // A small menu that opens above its button, drawn by the page itself.
    function menu(buttonId, menuId, choices, current, pick) {
        const button = $(buttonId), list = $(menuId)
        const label = () => (choices.find(c => c.value === current()) || choices[0]).label
        button.textContent = label() + ' ▾'
        list.innerHTML = ''
        for (const c of choices) {
            const b = el('button')
            b.textContent = c.label
            if (c.value === current()) b.classList.add('chosen')
            b.onclick = e => {
                e.stopPropagation()
                list.classList.remove('open')
                pick(c.value)
                menu(buttonId, menuId, choices, current, pick)
            }
            list.append(b)
        }
        button.onclick = e => {
            e.stopPropagation()
            document.querySelectorAll('.menu.open').forEach(m => { if (m !== list) m.classList.remove('open') })
            list.classList.toggle('open')
        }
    }
    document.addEventListener('click', () => document.querySelectorAll('.menu.open').forEach(m => m.classList.remove('open')))

    // The speed slider: 0.5× to 2×, in steps of 0.05.
    function showSpeed() {
        $('speedLabel').textContent = (Math.round(speed * 100) / 100).toLocaleString(undefined, { minimumFractionDigits: 2 }) + '×'
    }

    function setBoard(name) {
        board = ['clean', 'whiteboard', 'chalkboard', 'notebook'].includes(name) ? name : 'whiteboard'
        document.body.dataset.board = board
        Rich.setStyle({ board: board === 'clean' ? 'clean' : board })
    }

    function start() {
        $('play').onclick = toggle
        $('prev').onclick = prev
        $('next').onclick = next
        $('replay').onclick = replay
        $('close').onclick = () => { pause(); if (host) host.close() }
        $('cc').onclick = () => {
            captions = !captions
            $('cc').classList.toggle('on', captions)
            $('caption').classList.toggle('hidden', !captions)
            if (host) host.setting('captions', captions)
        }
        $('speed').oninput = () => {
            speed = parseFloat($('speed').value) || 1
            showSpeed()
            if (audio && voiceRate) audio.playbackRate = speed
        }
        $('speed').onchange = () => { if (host) host.setting('speed', speed) }
        $('volume').oninput = () => {
            volume = (parseInt($('volume').value) || 0) / 100
            if (audio) audio.volume = volume
        }
        $('volume').onchange = () => { if (host) host.setting('volume', volume) }
        $('askField').addEventListener('keydown', e => {
            if (e.key === 'Enter' && $('askField').value.trim() && host) {
                pause()
                host.ask($('askField').value.trim())
                $('askField').value = ''
            }
            e.stopPropagation()
        })
        document.addEventListener('keydown', e => {
            if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return
            if (e.key === ' ') { toggle(); e.preventDefault() }
            else if (e.key === 'ArrowRight') { next(); e.preventDefault() }
            else if (e.key === 'ArrowLeft') { prev(); e.preventDefault() }
            else if (e.key === 'Escape' && host) host.close()
        })
        window.addEventListener('resize', () => {
            for (const item of items) if (item.chart && item.el.classList.contains('shown')) Rich.drawChart(item.chart.canvas, item.chart.data, 1)
        })
        if (typeof QWebChannel !== 'undefined' && typeof qt !== 'undefined')
            new QWebChannel(qt.webChannelTransport, channel => {
                host = channel.objects.host
                host.ready()
            })
    }

    return { load, play, pause, toggle, next, prev, replay, voiceReady, setVoice, start, setStatus }
})()

if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', Stage.start)
else
    Stage.start()
