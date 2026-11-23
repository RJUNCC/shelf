// The page every hands-on question runs in. Shelf loads a question with
// Tool.load({question, accent, strings}); Tool.check() marks the answer and
// reports {correct, usable, summary, answer} to host.checked(). The tools
// themselves are in code.js and markov.js.
'use strict'

let host = null
let current = null
let strings = {}

function tr(key, ...args) {
    let s = strings[key] || key
    args.forEach((a, i) => { s = s.split('%' + (i + 1)).join(String(a)) })
    return s
}

function el(tag, attrs = {}, ...children) {
    const e = document.createElement(tag)
    for (const [k, v] of Object.entries(attrs)) {
        if (k === 'class') e.className = v
        else if (k.startsWith('on')) e.addEventListener(k.slice(2), v)
        else if (k === 'text') e.textContent = v
        else e.setAttribute(k, v)
    }
    for (const c of children)
        if (c !== null && c !== undefined)
            e.append(c)
    return e
}

const Tool = {
    load(options) {
        strings = options.strings || {}
        if (options.accent)
            document.documentElement.style.setProperty('--accent', options.accent)
        if (current && current.destroy)
            current.destroy()
        const root = document.getElementById('root')
        root.replaceChildren()
        const q = options.question
        const kinds = { code: CodeTool, markov: MarkovTool, molecule: MoleculeTool, scenario: ScenarioTool }
        current = new kinds[q.type](root, q)
    },
    async check() {
        let result
        try {
            result = await current.check()
        } catch (e) {
            result = { correct: false, usable: true, summary: String(e && e.message || e), answer: '' }
        }
        host.checked(JSON.stringify(result))
    },
    reveal() {
        if (current && current.reveal)
            current.reveal()
    },
}
window.Tool = Tool

new QWebChannel(qt.webChannelTransport, channel => {
    host = channel.objects.host
    host.ready()
})
