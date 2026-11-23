// Runs Python for a code question with Pyodide (Python compiled to
// WebAssembly), away from the page and the user's system. Loads once, then
// gets {code, tests}; answers {results, output, error}, or {missing: true}
// when Pyodide is not installed.
'use strict'

let ready = null

const harness = `
import sys, io, json, math, traceback

def __shelf_same(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return a == b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return math.isclose(a, b, rel_tol=1e-6, abs_tol=1e-9)
    if isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
        return len(a) == len(b) and all(__shelf_same(x, y) for x, y in zip(a, b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(__shelf_same(a[k], b[k]) for k in a)
    return a == b

def __shelf_run(code, tests_json):
    tests = json.loads(tests_json)
    out = io.StringIO()
    old = sys.stdout
    sys.stdout = out
    results = []
    try:
        ns = {"__name__": "__main__"}
        try:
            exec(code, ns)
        except BaseException as e:
            if isinstance(e, SyntaxError):
                message = "%s: %s (line %s)" % (type(e).__name__, e.msg, e.lineno)
            else:
                tb = traceback.extract_tb(e.__traceback__)
                line = " (line %d)" % tb[-1].lineno if tb and tb[-1].filename == "<string>" else ""
                message = "%s: %s%s" % (type(e).__name__, e, line)
            return json.dumps({"results": [], "output": out.getvalue()[:4000], "error": message})
        for t in tests:
            r = {"call": t["call"]}
            try:
                try:
                    expected = eval(t["expected"], {})
                    known = True
                except Exception:
                    expected, known = t["expected"], False
                got = eval(t["call"], ns)
                r["got"] = repr(got)
                r["expected"] = repr(expected) if known else t["expected"]
                r["pass"] = __shelf_same(got, expected) if known else (str(got) == expected or repr(got) == expected)
            except BaseException as e:
                r["error"] = "%s: %s" % (type(e).__name__, e)
                r["pass"] = False
            results.append(r)
    finally:
        sys.stdout = old
    return json.dumps({"results": results, "output": out.getvalue()[:4000]})
`

async function start() {
    try {
        importScripts('shelf://app/pyodide/pyodide.js')
    } catch (e) {
        return null
    }
    const py = await loadPyodide({ indexURL: 'shelf://app/pyodide/' })
    py.runPython(harness)
    return py
}

onmessage = async event => {
    if (!ready)
        ready = start()
    const py = await ready
    if (!py) {
        postMessage({ missing: true })
        return
    }
    if (event.data.warm) {
        postMessage({ warm: true })
        return
    }
    const run = py.globals.get('__shelf_run')
    try {
        postMessage(JSON.parse(run(event.data.code, JSON.stringify(event.data.tests))))
    } finally {
        run.destroy()
    }
}
