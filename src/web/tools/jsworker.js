// Runs JavaScript for a code question, away from the page: no DOM, no
// network (the page's policy forbids it), and Shelf stops it if it runs
// too long. Gets {code, tests}; answers {results, output, error}.
'use strict'

function same(a, b) {
    if (typeof a === 'number' && typeof b === 'number')
        return a === b || Math.abs(a - b) <= 1e-9 + 1e-6 * Math.abs(b)
    if (Array.isArray(a) && Array.isArray(b))
        return a.length === b.length && a.every((x, i) => same(x, b[i]))
    if (a && b && typeof a === 'object' && typeof b === 'object') {
        const ka = Object.keys(a).sort(), kb = Object.keys(b).sort()
        return same(ka, kb) && ka.every(k => same(a[k], b[k]))
    }
    return a === b
}

function show(v) {
    if (typeof v === 'string')
        return JSON.stringify(v)
    if (v === undefined)
        return 'undefined'
    try { return JSON.stringify(v) } catch (e) { return String(v) }
}

function literal(text) {
    try {
        return new Function('return (' + text + ');')()
    } catch (e) {
        // Python spellings, as a model sometimes writes them.
        return new Function('return (' + text.replace(/\bTrue\b/g, 'true').replace(/\bFalse\b/g, 'false')
                                             .replace(/\bNone\b/g, 'null') + ');')()
    }
}

onmessage = event => {
    const { code, tests } = event.data
    const output = []
    const log = (...args) => output.push(args.map(a => typeof a === 'string' ? a : show(a)).join(' '))
    const console = { log, info: log, warn: log, error: log }
    const results = []
    // The student's code runs once; each test call is evaluated inside its
    // scope, so it sees the functions the code defined.
    let evaluate
    try {
        evaluate = new Function('console', code + '\n;return function (__call) { return eval(__call) }')(console)
    } catch (e) {
        postMessage({ results: [], output: output.join('\n'), error: e.name + ': ' + e.message })
        return
    }
    for (const t of tests) {
        const r = { call: t.call }
        try {
            const expected = literal(t.expected)
            const got = evaluate('(' + t.call + ')')
            r.expected = show(expected)
            r.got = show(got)
            r.pass = same(got, expected)
        } catch (e) {
            r.error = e.name + ': ' + e.message
            r.pass = false
        }
        results.push(r)
    }
    postMessage({ results, output: output.join('\n').slice(0, 4000) })
}
