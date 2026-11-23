// Molecules for the molecule builder, without the DOM so Shelf's C++ side
// can run it too (to check a model's SMILES before showing a question).
//
// A molecule is {atoms: [{el, charge, h, aromatic}], bonds: [{a, b, order}]}
// holding heavy atoms only; `h` is the number of hydrogens on each atom
// (explicit hydrogens drawn by the student are folded in). Order 1.5 is an
// aromatic bond. Two molecules are the same when there is a one-to-one
// match of atoms keeping element, charge, hydrogens and bonds; Kekulé and
// aromatic drawings of a ring match because their hydrogens agree.
'use strict'

var Chem = (function () {
    const ORGANIC = ['Cl', 'Br', 'B', 'C', 'N', 'O', 'P', 'S', 'F', 'I']
    const AROMATIC = ['b', 'c', 'n', 'o', 'p', 's']
    const ELEMENTS = ['H', 'He', 'Li', 'Be', 'B', 'C', 'N', 'O', 'F', 'Ne', 'Na', 'Mg', 'Al', 'Si', 'P', 'S', 'Cl',
                      'Ar', 'K', 'Ca', 'Fe', 'Cu', 'Zn', 'Se', 'Br', 'I', 'Li', 'Mn', 'Co', 'Ni', 'Sn', 'Hg', 'Pt', 'Ag', 'Au']

    // The usual numbers of bonds, by element and charge.
    function valences(el, charge) {
        const base = { B: [3], C: [4], N: [3, 5], O: [2], P: [3, 5], S: [2, 4, 6], F: [1], Cl: [1], Br: [1], I: [1],
                       Si: [4], Se: [2, 4, 6], H: [1] }[el]
        if (!base)
            return null
        if (!charge)
            return base
        // N+ and O+ take one more bond, O- and N- one fewer; C+ and C- three.
        if (el === 'C')
            return [3]
        if (['N', 'P', 'O', 'S', 'Se'].includes(el))
            return base.map(v => v + charge).filter(v => v >= 0)
        if (['B'].includes(el))
            return [base[0] - charge]
        return base.map(v => v - Math.abs(charge)).filter(v => v >= 0)
    }

    // Hydrogens an atom carries implicitly, given the bonds it has.
    function implicitH(el, charge, bondSum, aromatic) {
        const vs = valences(el, charge)
        if (!vs)
            return 0
        const used = Math.ceil(bondSum + (aromatic ? 1 : 0) - 1e-9)
        for (const v of vs)
            if (v >= used)
                return v - used
        return 0
    }

    function parseSmiles(text) {
        const s = String(text).trim()
        const atoms = [], bonds = [], stack = [], rings = {}
        let prev = -1, order = 0, i = 0
        const addAtom = atom => {
            atoms.push(atom)
            const idx = atoms.length - 1
            if (prev >= 0) {
                const o = order || (atom.aromatic && atoms[prev].aromatic ? 1.5 : 1)
                bonds.push({ a: prev, b: idx, order: o })
            }
            prev = idx
            order = 0
        }
        while (i < s.length) {
            const c = s[i]
            if (c === '(') { stack.push(prev); i++; continue }
            if (c === ')') { if (!stack.length) throw new Error('unbalanced )'); prev = stack.pop(); i++; continue }
            if (c === '-') { order = 1; i++; continue }
            if (c === '=') { order = 2; i++; continue }
            if (c === '#') { order = 3; i++; continue }
            if (c === ':') { order = 1.5; i++; continue }
            if (c === '/' || c === '\\') { i++; continue }       // cis/trans: not checked
            if (c === '.') { prev = -1; i++; continue }
            if (c === '[') {
                const end = s.indexOf(']', i)
                if (end < 0) throw new Error('unclosed [')
                const m = s.slice(i + 1, end).match(/^(\d*)([A-Z][a-z]?|[bcnops]|se|as)(@*)(H\d*)?([+-]+\d*)?(:\d+)?$/)
                if (!m) throw new Error('bad atom ' + s.slice(i, end + 1))
                let el = m[2], aromatic = false
                if (/^[a-z]/.test(el)) { aromatic = true; el = el[0].toUpperCase() + el.slice(1) }
                let charge = 0
                if (m[5]) {
                    const sign = m[5][0] === '+' ? 1 : -1
                    const digits = m[5].replace(/[+-]/g, '')
                    charge = sign * (digits ? parseInt(digits) : m[5].length)
                }
                const h = m[4] ? (m[4].length > 1 ? parseInt(m[4].slice(1)) : 1) : 0
                addAtom({ el, charge, h, aromatic, bracket: true })
                i = end + 1
                continue
            }
            if (/\d|%/.test(c)) {
                let n
                if (c === '%') { n = s.slice(i + 1, i + 3); i += 3 } else { n = c; i++ }
                if (prev < 0) throw new Error('ring bond without atom')
                if (rings[n]) {
                    const r = rings[n]
                    const o = order || r.order || (atoms[prev].aromatic && atoms[r.atom].aromatic ? 1.5 : 1)
                    bonds.push({ a: r.atom, b: prev, order: o })
                    delete rings[n]
                } else {
                    rings[n] = { atom: prev, order }
                }
                order = 0
                continue
            }
            const two = s.slice(i, i + 2)
            if (ORGANIC.includes(two) && two.length === 2) { addAtom({ el: two, charge: 0, aromatic: false }); i += 2; continue }
            if (ORGANIC.includes(c)) { addAtom({ el: c, charge: 0, aromatic: false }); i++; continue }
            // Not valid SMILES, but models write it ("CC(=O)OH"): an explicit hydrogen.
            if (c === 'H') { addAtom({ el: 'H', charge: 0, h: 0, aromatic: false, bracket: true }); i++; continue }
            if (AROMATIC.includes(c)) { addAtom({ el: c.toUpperCase(), charge: 0, aromatic: true }); i++; continue }
            throw new Error('unexpected "' + c + '"')
        }
        if (stack.length) throw new Error('unbalanced (')
        if (Object.keys(rings).length) throw new Error('unclosed ring')
        if (!atoms.length) throw new Error('empty')
        const sums = atoms.map(() => 0)
        for (const b of bonds) {
            const o = b.order === 1.5 ? 1 : b.order
            sums[b.a] += o
            sums[b.b] += o
        }
        atoms.forEach((a, k) => {
            if (!a.bracket)
                a.h = implicitH(a.el, a.charge, sums[k], a.aromatic)
            delete a.bracket
        })
        return foldHydrogens({ atoms, bonds })
    }

    // Hydrogen atoms drawn (or written as [H]) become counts on their
    // neighbour, so H–O–H and O compare the same.
    function foldHydrogens(mol) {
        const keep = [], map = {}
        mol.atoms.forEach((a, i) => {
            const isH = a.el === 'H' && !a.charge
            const neighbours = mol.bonds.filter(b => b.a === i || b.b === i)
            if (isH && neighbours.length === 1 && neighbours[0].order === 1) {
                const other = neighbours[0].a === i ? neighbours[0].b : neighbours[0].a
                if (!(mol.atoms[other].el === 'H' && !mol.atoms[other].charge)) {
                    map[i] = -1 - other
                    return
                }
            }
            map[i] = keep.length
            keep.push(Object.assign({}, a, { h: a.h || 0 }))
        })
        mol.atoms.forEach((a, i) => {
            if (map[i] < 0)
                keep[map[-1 - map[i]]].h += 1
        })
        const bonds = mol.bonds.filter(b => map[b.a] >= 0 && map[b.b] >= 0)
                               .map(b => ({ a: map[b.a], b: map[b.b], order: b.order }))
        return { atoms: keep, bonds }
    }

    // A molecule drawn in the editor: atoms with bonds, hydrogens implied.
    function fromDrawing(atoms, bonds) {
        const sums = atoms.map(() => 0)
        for (const b of bonds) {
            sums[b.a] += b.order
            sums[b.b] += b.order
        }
        const mol = {
            atoms: atoms.map((a, i) => ({ el: a.el, charge: a.charge || 0, aromatic: false,
                                         h: a.el === 'H' ? Math.max(0, 1 - sums[i]) : implicitH(a.el, a.charge || 0, sums[i], false) })),
            bonds: bonds.map(b => ({ a: b.a, b: b.b, order: b.order })),
        }
        return foldHydrogens(mol)
    }

    // Atoms with more bonds than their element allows.
    function overloaded(atoms, bonds) {
        const sums = atoms.map(() => 0)
        for (const b of bonds) {
            sums[b.a] += b.order
            sums[b.b] += b.order
        }
        return atoms.map((a, i) => {
            const vs = valences(a.el, a.charge || 0)
            return vs ? sums[i] > Math.max(...vs) : false
        })
    }

    function formula(mol) {
        const counts = {}
        for (const a of mol.atoms) {
            counts[a.el] = (counts[a.el] || 0) + 1
            if (a.h)
                counts.H = (counts.H || 0) + a.h
        }
        const charge = mol.atoms.reduce((t, a) => t + (a.charge || 0), 0)
        const order = Object.keys(counts).sort((x, y) => {
            const rank = e => (counts.C ? (e === 'C' ? 0 : e === 'H' ? 1 : 2) : 2)
            return rank(x) - rank(y) || (x < y ? -1 : x > y ? 1 : 0)
        })
        let out = order.map(e => e + (counts[e] > 1 ? counts[e] : '')).join('')
        if (charge)
            out += (Math.abs(charge) > 1 ? Math.abs(charge) : '') + (charge > 0 ? '+' : '-')
        return out
    }

    // "C2H6O", "H2O", "C2 H5 OH": element counts, for comparing formulas.
    function formulaCounts(text) {
        const counts = {}
        const t = String(text).replace(/[\s₀-₉]/g, ch => ch.trim() ? String(ch.charCodeAt(0) - 0x2080) : '')
                              .replace(/[⁺⁻+\-]+\d*$/, '')
        const re = /([A-Z][a-z]?)(\d*)/g
        let m, seen = 0
        while ((m = re.exec(t))) {
            counts[m[1]] = (counts[m[1]] || 0) + (m[2] ? parseInt(m[2]) : 1)
            seen += m[0].length
        }
        return seen === t.length && seen ? counts : null
    }

    function sameCounts(a, b) {
        const ka = Object.keys(a).sort(), kb = Object.keys(b).sort()
        return ka.length === kb.length && ka.every((k, i) => k === kb[i] && a[k] === b[k])
    }

    function label(a) {
        return a.el + '/' + (a.charge || 0) + '/' + (a.h || 0)
    }

    // Colours atoms by their neighbourhood until stable (Weisfeiler–Lehman),
    // which almost always tells atoms apart; a backtracking match then
    // settles it exactly.
    function colours(mol) {
        const adj = mol.atoms.map(() => [])
        for (const b of mol.bonds) {
            adj[b.a].push([b.b, b.order])
            adj[b.b].push([b.a, b.order])
        }
        let c = mol.atoms.map(label)
        for (let round = 0; round < mol.atoms.length; ++round) {
            const next = c.map((x, i) => x + '|' + adj[i].map(([j, o]) => (o === 1.5 ? 'a' : o) + c[j]).sort().join(','))
            const names = [...new Set(next)].sort()
            const renamed = next.map(x => 'k' + names.indexOf(x))
            if (new Set(renamed).size === new Set(c).size) {
                c = next
                break
            }
            c = next
        }
        return { c, adj }
    }

    function bondMatches(o1, o2) {
        if (o1 === o2)
            return true
        // An aromatic bond matches either Kekulé bond; the hydrogens keep the
        // pattern honest.
        return (o1 === 1.5 && (o2 === 1 || o2 === 2)) || (o2 === 1.5 && (o1 === 1 || o1 === 2))
    }

    function same(m1, m2) {
        if (m1.atoms.length !== m2.atoms.length || m1.bonds.length !== m2.bonds.length)
            return false
        if (formula(m1) !== formula(m2))
            return false
        const n = m1.atoms.length
        const lab1 = m1.atoms.map(label), lab2 = m2.atoms.map(label)
        const adj1 = m1.atoms.map(() => new Map()), adj2 = m2.atoms.map(() => new Map())
        for (const b of m1.bonds) { adj1[b.a].set(b.b, b.order); adj1[b.b].set(b.a, b.order) }
        for (const b of m2.bonds) { adj2[b.a].set(b.b, b.order); adj2[b.b].set(b.a, b.order) }
        // Match atoms in an order that follows bonds, so each new atom is
        // checked against ones already placed.
        const order = [], seen = new Array(n).fill(false)
        for (let s = 0; s < n; ++s) {
            if (seen[s]) continue
            const queue = [s]
            seen[s] = true
            while (queue.length) {
                const x = queue.shift()
                order.push(x)
                for (const y of adj1[x].keys())
                    if (!seen[y]) { seen[y] = true; queue.push(y) }
            }
        }
        const map = new Array(n).fill(-1), used = new Array(n).fill(false)
        let steps = 0
        const place = k => {
            if (k === n)
                return true
            if (++steps > 200000)
                return false
            const x = order[k]
            for (let y = 0; y < n; ++y) {
                if (used[y] || lab1[x] !== lab2[y] || adj1[x].size !== adj2[y].size)
                    continue
                let ok = true
                for (const [x2, o] of adj1[x]) {
                    if (map[x2] < 0) continue
                    const o2 = adj2[y].get(map[x2])
                    if (o2 === undefined || !bondMatches(o, o2)) { ok = false; break }
                }
                if (!ok) continue
                map[x] = y
                used[y] = true
                if (place(k + 1))
                    return true
                map[x] = -1
                used[y] = false
            }
            return false
        }
        return place(0)
    }

    // The smallest rings: for each bond, the shortest way round from one
    // end to the other without it. Each ring is its atoms in order.
    function rings(mol) {
        const n = mol.atoms.length
        const adj = mol.atoms.map(() => [])
        for (const b of mol.bonds) { adj[b.a].push(b.b); adj[b.b].push(b.a) }
        const found = [], keys = new Set()
        for (const b of mol.bonds) {
            const prev = new Array(n).fill(-1), seen = new Array(n).fill(false)
            const queue = [b.a]
            seen[b.a] = true
            while (queue.length && !seen[b.b]) {
                const x = queue.shift()
                for (const y of adj[x]) {
                    if (seen[y] || (x === b.a && y === b.b)) continue
                    seen[y] = true
                    prev[y] = x
                    queue.push(y)
                }
            }
            if (!seen[b.b]) continue
            const ring = []
            for (let x = b.b; x !== -1; x = prev[x]) ring.push(x)
            if (ring.length > 8) continue
            const key = ring.slice().sort((p, q) => p - q).join(',')
            if (!keys.has(key)) { keys.add(key); found.push(ring) }
        }
        return found.sort((p, q) => q.length - p.length || 0).reverse()
    }

    // The middle of the widest gap between bond directions.
    function widestGap(angles) {
        const a = angles.map(x => (x + 2 * Math.PI) % (2 * Math.PI)).sort((p, q) => p - q)
        let best = 0, at = 0
        for (let k = 0; k < a.length; ++k) {
            const next = k + 1 < a.length ? a[k + 1] : a[0] + 2 * Math.PI
            if (next - a[k] > best) { best = next - a[k]; at = a[k] + best / 2 }
        }
        return at
    }

    // A 2D picture, built the way a person draws one: rings as regular
    // polygons (fused rings on their shared bond), chains zigzagging away
    // from what is already there. Bond length 1.
    function layout(mol) {
        const n = mol.atoms.length
        const pos = new Array(n).fill(null)
        const adj = mol.atoms.map(() => [])
        for (const b of mol.bonds) { adj[b.a].push(b.b); adj[b.b].push(b.a) }
        const ringList = rings(mol)
        const placedRing = ringList.map(() => false)

        const centroid = () => {
            let x = 0, y = 0, k = 0
            pos.forEach(p => { if (p) { x += p.x; y += p.y; k++ } })
            return k ? { x: x / k, y: y / k } : { x: 0, y: 0 }
        }
        // A free direction from a placed atom, as in the editor.
        const freeAngle = i => {
            const ns = adj[i].filter(j => pos[j])
            const p = pos[i]
            if (!ns.length) return -Math.PI / 6
            if (ns.length === 1) {
                const q = pos[ns[0]]
                const back = Math.atan2(q.y - p.y, q.x - p.x)
                const before = adj[ns[0]].filter(j => j !== i && pos[j])
                let turn = -2 * Math.PI / 3
                if (before.length) {
                    const m = pos[before[0]]
                    const cross = (p.x - q.x) * (m.y - q.y) - (p.y - q.y) * (m.x - q.x)
                    turn = cross > 0 ? 2 * Math.PI / 3 : -2 * Math.PI / 3
                }
                return back + turn
            }
            return widestGap(ns.map(j => Math.atan2(pos[j].y - p.y, pos[j].x - p.x)))
        }
        // Places a ring's atoms on a regular polygon with centre c, the
        // first atom at angle `start`, going round in direction `dir`.
        const polygon = (ring, c, start, dir) => {
            const r = 1 / (2 * Math.sin(Math.PI / ring.length))
            ring.forEach((atom, k) => {
                if (!pos[atom])
                    pos[atom] = { x: c.x + r * Math.cos(start + dir * 2 * Math.PI * k / ring.length),
                                  y: c.y + r * Math.sin(start + dir * 2 * Math.PI * k / ring.length) }
            })
        }
        const placeRing = idx => {
            const ring = ringList[idx], m = ring.length
            placedRing[idx] = true
            const r = 1 / (2 * Math.sin(Math.PI / m)), apothem = 1 / (2 * Math.tan(Math.PI / m))
            // Fused: two neighbours in the ring already placed.
            for (let k = 0; k < m; ++k) {
                const i = ring[k], j = ring[(k + 1) % m]
                if (!pos[i] || !pos[j]) continue
                const mx = (pos[i].x + pos[j].x) / 2, my = (pos[i].y + pos[j].y) / 2
                const nx = -(pos[j].y - pos[i].y), ny = pos[j].x - pos[i].x
                const c0 = centroid()
                const side = (nx * (c0.x - mx) + ny * (c0.y - my)) > 0 ? -1 : 1
                const c = { x: mx + side * nx * apothem, y: my + side * ny * apothem }
                const ai = Math.atan2(pos[i].y - c.y, pos[i].x - c.x), aj = Math.atan2(pos[j].y - c.y, pos[j].x - c.x)
                const dir = Math.sin(aj - ai) > 0 ? 1 : -1
                // Walk the ring from i away from j.
                const order = []
                for (let t = 0; t < m; ++t) order.push(ring[(k - t + m) % m])
                polygon(order, c, ai, -dir)
                return
            }
            // Joined by one atom (or the first ring of all).
            const anchor = ring.find(a => pos[a])
            if (anchor === undefined) {
                polygon(ring, centroid(), -Math.PI / 2, 1)
                return
            }
            const ang = freeAngle(anchor)
            const k = ring.indexOf(anchor)
            const order = ring.slice(k).concat(ring.slice(0, k))
            polygon(order, { x: pos[anchor].x + r * Math.cos(ang), y: pos[anchor].y + r * Math.sin(ang) }, ang + Math.PI, 1)
        }

        for (;;) {
            // Rings fused to what's placed come first.
            const fused = ringList.findIndex((ring, idx) => !placedRing[idx]
                && ring.some((a, k) => pos[a] && pos[ring[(k + 1) % ring.length]]))
            if (fused >= 0) { placeRing(fused); continue }
            const spiro = ringList.findIndex((ring, idx) => !placedRing[idx] && ring.some(a => pos[a]))
            if (spiro >= 0) { placeRing(spiro); continue }
            // Then a chain atom bonded to a placed one.
            let grown = false
            for (let i = 0; i < n && !grown; ++i) {
                if (!pos[i]) continue
                for (const j of adj[i]) {
                    if (pos[j]) continue
                    const inRing = ringList.findIndex((ring, idx) => !placedRing[idx] && ring.includes(j))
                    const ang = freeAngle(i)
                    pos[j] = { x: pos[i].x + Math.cos(ang), y: pos[i].y + Math.sin(ang) }
                    if (inRing >= 0) placeRing(inRing)
                    grown = true
                    break
                }
            }
            if (grown) continue
            // A new piece (or the very first atom).
            const next = pos.findIndex(p => !p)
            if (next < 0) break
            const firstRing = ringList.findIndex((ring, idx) => !placedRing[idx] && ring.includes(next))
            if (firstRing >= 0 && pos.every(p => !p)) { placeRing(firstRing); continue }
            const c = centroid()
            pos[next] = pos.some(p => p) ? { x: c.x + 3, y: c.y } : { x: 0, y: 0 }
        }
        return pos
    }

    // Aromatic bonds as alternating single and double bonds, for drawing:
    // each aromatic atom with a bond to spare gets exactly one double bond.
    function kekulize(mol) {
        const bonds = mol.bonds.map(b => Object.assign({}, b))
        const arom = bonds.map((b, k) => b.order === 1.5 ? k : -1).filter(k => k >= 0)
        if (!arom.length)
            return bonds
        const sigma = mol.atoms.map(() => 0)
        for (const b of bonds) {
            const o = b.order === 1.5 ? 1 : b.order
            sigma[b.a] += o
            sigma[b.b] += o
        }
        const needs = mol.atoms.map((a, i) => {
            if (!a.aromatic) return false
            const vs = valences(a.el, a.charge) || [0]
            return vs.some(v => v - a.h - sigma[i] === 1)
        })
        const taken = mol.atoms.map(() => false)
        const pick = k => {
            // Next atom still needing a double bond.
            const i = needs.findIndex((n, x) => n && !taken[x])
            if (i < 0) return true
            for (const bk of arom) {
                const b = bonds[bk]
                if (b.order !== 1.5) continue
                const j = b.a === i ? b.b : b.b === i ? b.a : -1
                if (j < 0 || taken[j] || !needs[j]) continue
                b.order = 2; taken[i] = taken[j] = true
                if (pick(k + 1)) return true
                b.order = 1.5; taken[i] = taken[j] = false
            }
            return false
        }
        pick(0)
        for (const b of bonds)
            if (b.order === 1.5) b.order = 1
        return bonds
    }

    // For Shelf's C++ side: whether a SMILES is usable, and its formula.
    function check(smiles, givenFormula) {
        try {
            const mol = parseSmiles(smiles)
            if (mol.atoms.length > 40)
                return { ok: false, error: 'too big' }
            const f = formula(mol)
            if (givenFormula) {
                const given = formulaCounts(givenFormula), mine = formulaCounts(f)
                if (given && mine && !sameCounts(given, mine))
                    return { ok: false, error: 'formula ' + f + ' is not ' + givenFormula, formula: f }
            }
            return { ok: true, formula: f, atoms: mol.atoms.length }
        } catch (e) {
            return { ok: false, error: String(e.message || e) }
        }
    }

    return { parseSmiles, fromDrawing, kekulize, rings, widestGap, overloaded, formula, formulaCounts, same, layout, check, valences, implicitH, ELEMENTS }
})()
if (typeof window !== 'undefined')
    window.Chem = Chem
