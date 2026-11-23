// Formulas for scenario questions: numbers, variables, + - * / ^, brackets
// and a few functions. Parsed by hand rather than eval'd, so a model's (or
// anyone's) formula can only do arithmetic. No DOM: Shelf's C++ side runs
// this too, to work out the answers before a question is shown.
'use strict'

var Expr = (function () {
    function erf(x) {
        // Abramowitz and Stegun 7.1.26, good to about 1e-7.
        const t = 1 / (1 + 0.3275911 * Math.abs(x))
        const y = 1 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * Math.exp(-x * x)
        return x >= 0 ? y : -y
    }
    const FUNCTIONS = {
        sqrt: Math.sqrt, abs: Math.abs, exp: Math.exp, ln: Math.log, log: Math.log10, log10: Math.log10, log2: Math.log2,
        sin: Math.sin, cos: Math.cos, tan: Math.tan, asin: Math.asin, acos: Math.acos, atan: Math.atan,
        sind: x => Math.sin(x * Math.PI / 180), cosd: x => Math.cos(x * Math.PI / 180), tand: x => Math.tan(x * Math.PI / 180),
        floor: Math.floor, ceil: Math.ceil, round: Math.round,
        min: Math.min, max: Math.max, pow: Math.pow,
        // The standard normal distribution, for Black–Scholes and statistics.
        normcdf: x => 0.5 * (1 + erf(x / Math.SQRT2)),
        normpdf: x => Math.exp(-x * x / 2) / Math.sqrt(2 * Math.PI),
    }
    const CONSTANTS = { pi: Math.PI, e: Math.E, g: 9.81 }

    function tokens(text) {
        const out = []
        const re = /\s*(?:(\d+\.?\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?)|([A-Za-z_][A-Za-z_0-9]*)|(\*\*|[-+*/^(),]))/y
        let m, i = 0
        const s = String(text).replace(/×/g, '*').replace(/÷/g, '/').replace(/−/g, '-')
        while (i < s.length) {
            re.lastIndex = i
            m = re.exec(s)
            if (!m) {
                if (/^\s*$/.test(s.slice(i)))
                    break
                throw new Error('cannot read "' + s.slice(i).trim().slice(0, 10) + '"')
            }
            i = re.lastIndex
            out.push(m[1] !== undefined ? { num: parseFloat(m[1]) } : m[2] !== undefined ? { name: m[2] } : { op: m[3] === '**' ? '^' : m[3] })
        }
        return out
    }

    // Recursive descent: sum := product (± product)*; product := unary
    // (*/ unary)*; unary := -unary | power; power := atom (^ unary)?
    function parse(text) {
        const t = tokens(text)
        let k = 0
        const peek = () => t[k], take = () => t[k++]
        const isOp = (tok, op) => tok && tok.op === op
        function sum() {
            let node = product()
            while (isOp(peek(), '+') || isOp(peek(), '-'))
                node = { op: take().op, a: node, b: product() }
            return node
        }
        function product() {
            let node = unary()
            for (;;) {
                if (isOp(peek(), '*') || isOp(peek(), '/'))
                    node = { op: take().op, a: node, b: unary() }
                else if (peek() && (peek().num !== undefined || peek().name || isOp(peek(), '(')) && node)
                    node = { op: '*', a: node, b: unary() }        // 2pi, 3(x+1)
                else
                    return node
            }
        }
        function unary() {
            if (isOp(peek(), '-')) { take(); return { op: 'neg', a: unary() } }
            if (isOp(peek(), '+')) { take(); return unary() }
            return power()
        }
        function power() {
            const base = atom()
            if (isOp(peek(), '^')) { take(); return { op: '^', a: base, b: unary() } }
            return base
        }
        function atom() {
            const tok = take()
            if (!tok) throw new Error('the formula ends too soon')
            if (tok.num !== undefined) return { num: tok.num }
            if (isOp(tok, '(')) {
                const node = sum()
                if (!isOp(take(), ')')) throw new Error('a bracket is not closed')
                return node
            }
            if (tok.name) {
                if (isOp(peek(), '(')) {
                    const f = FUNCTIONS[tok.name.toLowerCase()]
                    if (!f) throw new Error('unknown function ' + tok.name)
                    take()
                    const args = [sum()]
                    while (isOp(peek(), ',')) { take(); args.push(sum()) }
                    if (!isOp(take(), ')')) throw new Error('a bracket is not closed')
                    return { fn: f, args }
                }
                return { name: tok.name }
            }
            throw new Error('unexpected "' + tok.op + '"')
        }
        const node = sum()
        if (k < t.length) throw new Error('unexpected "' + (t[k].op || t[k].name || t[k].num) + '"')
        return node
    }

    function names(node, out = new Set()) {
        if (node.name) out.add(node.name)
        if (node.a) names(node.a, out)
        if (node.b) names(node.b, out)
        if (node.args) node.args.forEach(a => names(a, out))
        return out
    }

    function evaluate(node, vars) {
        if (node.num !== undefined) return node.num
        if (node.name !== undefined) {
            if (Object.prototype.hasOwnProperty.call(vars, node.name)) return vars[node.name]
            if (Object.prototype.hasOwnProperty.call(CONSTANTS, node.name)) return CONSTANTS[node.name]
            throw new Error('unknown name ' + node.name)
        }
        if (node.fn) return node.fn(...node.args.map(a => evaluate(a, vars)))
        const a = evaluate(node.a, vars)
        switch (node.op) {
        case 'neg': return -a
        case '+': return a + evaluate(node.b, vars)
        case '-': return a - evaluate(node.b, vars)
        case '*': return a * evaluate(node.b, vars)
        case '/': return a / evaluate(node.b, vars)
        case '^': return Math.pow(a, evaluate(node.b, vars))
        }
        throw new Error('bad formula')
    }

    // A scenario: {variables: [{name, min, max, value}], outputs: [{name,
    // formula}]}. Outputs may use variables and earlier outputs.
    function compile(scenario) {
        const known = new Set(scenario.variables.map(v => v.name))
        const outputs = scenario.outputs.map(o => {
            const node = parse(o.formula)
            for (const n of names(node))
                if (!known.has(n) && !(n in CONSTANTS))
                    throw new Error(o.name + ' uses ' + n + ', which is not defined')
            known.add(o.name)
            return { name: o.name, node }
        })
        return values => {
            const vars = Object.assign({}, values)
            const result = {}
            for (const o of outputs) {
                const v = evaluate(o.node, vars)
                vars[o.name] = v
                result[o.name] = v
            }
            return result
        }
    }

    function givens(scenario) {
        const v = {}
        for (const x of scenario.variables) v[x.name] = x.value
        return v
    }

    // The value of `vary` (others at their given values) that makes
    // `output` equal `target`, or largest or smallest. NaN if none.
    function find(scenario, vary, output, goal, target) {
        const run = compile(scenario)
        const variable = scenario.variables.find(v => v.name === vary)
        if (!variable) return NaN
        const base = givens(scenario)
        const f = x => { const v = Object.assign({}, base); v[vary] = x; return run(v)[output] }
        const lo = variable.min, hi = variable.max, steps = 800
        let best = NaN, bestY = goal === 'min' ? Infinity : -Infinity
        let px = lo, py = f(lo)
        if (goal === 'equal') {
            if (Math.abs(py - target) < 1e-12) return lo
            for (let i = 1; i <= steps; ++i) {
                const x = lo + (hi - lo) * i / steps, y = f(x)
                if (isFinite(y) && isFinite(py) && (py - target) * (y - target) <= 0) {
                    let a = px, b = x
                    for (let it = 0; it < 80; ++it) {
                        const m = (a + b) / 2
                        if ((f(a) - target) * (f(m) - target) <= 0) b = m
                        else a = m
                    }
                    return (a + b) / 2
                }
                px = x
                py = y
            }
            return NaN
        }
        for (let i = 0; i <= steps; ++i) {
            const x = lo + (hi - lo) * i / steps, y = f(x)
            if (isFinite(y) && (goal === 'min' ? y < bestY : y > bestY)) { bestY = y; best = x }
        }
        // Refine around the best sample (golden section).
        let a = Math.max(lo, best - (hi - lo) / steps), b = Math.min(hi, best + (hi - lo) / steps)
        const better = (y1, y2) => goal === 'min' ? y1 < y2 : y1 > y2
        for (let it = 0; it < 60; ++it) {
            const m1 = a + (b - a) * 0.382, m2 = a + (b - a) * 0.618
            if (better(f(m1), f(m2))) b = m2
            else a = m1
        }
        return (a + b) / 2
    }

    // For Shelf's C++ side: checks a scenario and works out its answer.
    function solve(json) {
        try {
            const s = typeof json === 'string' ? JSON.parse(json) : json
            for (const v of s.variables)
                if (!(v.max > v.min) || !(v.value >= v.min && v.value <= v.max))
                    return { ok: false, error: v.name + ' has a bad range' }
            const run = compile(s)
            const out = run(givens(s))
            if (!(s.output in out))
                return { ok: false, error: 'no output ' + s.output }
            if (s.task === 'find') {
                const x = find(s, s.vary, s.output, s.goal, s.target)
                return isFinite(x) ? { ok: true, answer: x } : { ok: false, error: 'no solution in range' }
            }
            const y = out[s.output]
            return isFinite(y) ? { ok: true, answer: y } : { ok: false, error: 'not a number' }
        } catch (e) {
            return { ok: false, error: String(e.message || e) }
        }
    }

    return { parse, evaluate, compile, find, solve, givens, names }
})()
if (typeof window !== 'undefined')
    window.Expr = Expr
