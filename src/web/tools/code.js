// A code question: an editor, a Run button that tries the example tests,
// and Check, which runs every test. The code runs in a worker (jsworker.js
// or pyworker.js) that is stopped if it takes too long.
//
// The model's own solution is run against its tests first; a test the
// solution fails was written wrong and is dropped. If none survive, the
// question can't be checked by running it and is marked by the AI instead.
'use strict'

const LIMIT_MS = 8000          // for one run, once the language has loaded
const LOAD_MS = 60000          // Python's first load

class CodeTool {
    constructor(root, q) {
        this.q = q
        this.python = q.language !== 'javascript'
        this.worker = null
        this.validTests = null
        this.examples = Math.max(1, Math.ceil(q.tests.length / 2))

        const lang = el('span', { class: 'badge', text: this.python ? 'Python' : 'JavaScript' })
        this.runButton = el('button', { class: 'button', text: tr('Run examples'), title: 'Ctrl+Enter',
                                        onclick: () => this.run() })
        const reset = el('button', { class: 'button flat', text: tr('Start over'),
                                     onclick: () => { this.editor.value = this.starter(); this.renumber() } })
        this.solutionButton = el('button', { class: 'button flat hidden', text: tr('Show solution'),
                                             onclick: () => this.toggleSolution() })
        this.status = el('span', { class: 'status' })
        this.gutter = el('div', { class: 'gutter' })
        this.editor = el('textarea', { class: 'editor', spellcheck: 'false', autocapitalize: 'off',
                                       autocomplete: 'off', wrap: 'off' })
        this.editor.value = this.starter()
        this.editor.addEventListener('keydown', e => this.key(e))
        this.editor.addEventListener('input', () => this.renumber())
        this.editor.addEventListener('scroll', () => { this.gutter.scrollTop = this.editor.scrollTop })
        this.solution = el('pre', { class: 'solution hidden' })
        this.solution.textContent = q.solution
        this.output = el('div', { class: 'output' })
        const examples = el('div', { class: 'examples' }, el('div', { class: 'label', text: tr('Examples') }),
                            ...q.tests.slice(0, this.examples).map(t =>
                                el('code', { text: t.call + '  →  ' + t.expected })))

        root.append(el('div', { class: 'code-tool' },
                       el('div', { class: 'toolbar' }, lang, this.runButton, reset, this.solutionButton, this.status),
                       el('div', { class: 'editor-wrap' }, this.gutter, this.editor),
                       this.solution, examples, this.output))
        this.renumber()
        setTimeout(() => this.editor.focus(), 50)
        if (this.python)
            this.warm()
    }

    destroy() {
        if (this.worker)
            this.worker.terminate()
    }

    starter() {
        const s = (this.q.starter || '').replace(/\s+$/, '')
        return s ? s + '\n' : ''
    }

    renumber() {
        const lines = this.editor.value.split('\n').length
        this.gutter.textContent = Array.from({ length: lines }, (_, i) => i + 1).join('\n')
    }

    // Tab indents, Shift+Tab dedents, Enter keeps the indent (and adds one
    // after a ':' in Python or a '{' in JavaScript), Ctrl+Enter runs.
    key(e) {
        const ed = this.editor
        if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
            e.preventDefault()
            this.run()
            return
        }
        if (e.key === 'Tab') {
            e.preventDefault()
            const start = ed.selectionStart, end = ed.selectionEnd
            const lineStart = ed.value.lastIndexOf('\n', start - 1) + 1
            if (e.shiftKey) {
                const m = ed.value.slice(lineStart).match(/^ {1,4}/)
                if (m) {
                    ed.setRangeText('', lineStart, lineStart + m[0].length, 'preserve')
                    ed.selectionStart = ed.selectionEnd = Math.max(lineStart, start - m[0].length)
                }
            } else {
                ed.setRangeText('    ', start, end, 'end')
            }
            this.renumber()
            return
        }
        if (e.key === 'Enter') {
            e.preventDefault()
            const start = ed.selectionStart
            const lineStart = ed.value.lastIndexOf('\n', start - 1) + 1
            const line = ed.value.slice(lineStart, start)
            let indent = line.match(/^\s*/)[0]
            if (/[:{\[(]\s*$/.test(line))
                indent += '    '
            ed.setRangeText('\n' + indent, start, ed.selectionEnd, 'end')
            this.renumber()
        }
    }

    startWorker() {
        if (!this.worker) {
            this.worker = new Worker('shelf://app/tools/' + (this.python ? 'pyworker.js' : 'jsworker.js'))
            this.loaded = !this.python
        }
        return this.worker
    }

    // Loads Python while the student reads the question.
    warm() {
        const worker = this.startWorker()
        this.status.textContent = tr('Loading Python…')
        worker.onmessage = e => {
            if (e.data.missing)
                this.missing = true
            this.loaded = true
            this.status.textContent = this.missing ? tr('Python is not installed with Shelf.') : ''
        }
        worker.postMessage({ warm: true })
    }

    // Runs code against tests in the worker; resolves {results, output, error}.
    execute(code, tests) {
        return new Promise(resolve => {
            const worker = this.startWorker()
            const timer = setTimeout(() => {
                worker.terminate()
                this.worker = null
                resolve({ results: [], output: '', error: tr('It took too long and was stopped. Is there a loop that never ends?') })
            }, this.loaded ? LIMIT_MS : LOAD_MS)
            worker.onmessage = e => {
                if (e.data.warm)
                    return
                clearTimeout(timer)
                this.loaded = true
                resolve(e.data)
            }
            worker.postMessage({ code, tests })
        })
    }

    async valid() {
        if (this.validTests === null) {
            const r = await this.execute(this.q.solution, this.q.tests)
            if (r.missing) {
                this.missing = true
                return []
            }
            this.validTests = this.q.tests.filter((t, i) => r.results[i] && r.results[i].pass)
        }
        return this.validTests
    }

    async run(all = false) {
        if (this.busy)
            return null
        this.busy = true
        this.runButton.disabled = true
        this.status.textContent = this.loaded ? tr('Running…') : tr('Loading Python…')
        try {
            const tests = await this.valid()
            if (this.missing) {
                this.status.textContent = tr('Python is not installed with Shelf.')
                return { missing: true }
            }
            const chosen = all ? tests : tests.filter(t => this.q.tests.indexOf(t) < this.examples)
            const r = await this.execute(this.editor.value, chosen.length ? chosen : tests)
            this.show(r, all)
            this.status.textContent = ''
            return Object.assign(r, { checked: tests.length })
        } finally {
            this.busy = false
            this.runButton.disabled = false
        }
    }

    show(r, all) {
        const rows = []
        if (r.error)
            rows.push(el('div', { class: 'fail error', text: r.error }))
        r.results.forEach((t, i) => {
            const hidden = all && this.q.tests.indexOf(this.validTests[i]) >= this.examples
            const title = hidden ? tr('Hidden test %1', i + 1) : t.call
            const detail = t.error ? t.error
                         : t.pass ? (hidden ? '' : '→ ' + t.got)
                         : hidden ? tr('gave the wrong result') : tr('got %1, expected %2', t.got, t.expected)
            rows.push(el('div', { class: t.pass ? 'pass' : 'fail' },
                         el('span', { class: 'mark', text: t.pass ? '✓' : '✗' }),
                         el('code', { text: title }), el('span', { class: 'detail', text: ' ' + detail })))
        })
        if (r.output)
            rows.push(el('div', { class: 'label', text: tr('Printed') }), el('pre', { class: 'printed', text: r.output }))
        this.output.replaceChildren(...rows)
    }

    async check() {
        const r = await this.run(true)
        if (!r || r.missing || r.checked === 0)
            return { correct: false, usable: false, answer: this.editor.value, summary: '' }
        const passed = r.results.filter(t => t.pass).length
        this.solutionButton.classList.remove('hidden')
        return {
            correct: !r.error && passed === r.results.length,
            usable: true,
            answer: this.editor.value,
            summary: r.error ? r.error : tr('%1 of %2 tests pass', passed, r.results.length),
        }
    }

    toggleSolution() {
        const hidden = this.solution.classList.toggle('hidden')
        this.solutionButton.textContent = hidden ? tr('Show solution') : tr('Hide solution')
    }

    reveal() {
        this.solutionButton.classList.remove('hidden')
    }
}
window.CodeTool = CodeTool
