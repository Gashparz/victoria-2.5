// Edit operations on V2.Save (each is one undo step)
"use strict";
(function (V2) {
  const P = V2.Save.prototype;
  const q = (s) => '"' + s + '"';

  // --- pops ---
  P.popOps = function (p, pop, changes) { // changes: {culture, religion, type, size, money, literacy, mil, con}
    const t = this.segs[p.seg].text;
    const ops = [];
    if (changes.type && changes.type !== pop.type) ops.push([pop.node.ks, pop.node.ke, V2.toFileChars(changes.type)]);
    if (pop.cr) {
      if (changes.culture && changes.culture !== pop.culture) ops.push([pop.cr.ks, pop.cr.ke, V2.toFileChars(changes.culture)]);
      if (changes.religion && changes.religion !== pop.religion) ops.push([pop.cr.vs, pop.cr.ve, V2.toFileChars(changes.religion)]);
    }
    for (const f of ["size", "money", "literacy", "mil", "con", "bank"]) {
      if (changes[f] === undefined || changes[f] === "") continue;
      const v = String(changes[f]);
      if (!V2.isNum(v)) throw new Error(f + ": not a number");
      const n = pop.fields[f];
      if (n) { if (n.v !== v) ops.push([n.vs, n.ve, v]); }
      else {
        const anchor = pop.fields.size || pop.cr;
        const ind = this.indentAt(t, anchor.ks);
        ops.push([anchor.ve, anchor.ve, this.nl(t) + ind + f + "=" + v]);
      }
    }
    return this.edit(p.seg, ops, "pop " + pop.id);
  };
  P.deletePops = function (p, pops) {
    const t = this.segs[p.seg].text;
    const ops = pops.map((pop) => { const [a, b] = this.lineRange(t, pop.node.ks, pop.node.be + 1); return [a, b, ""]; });
    return this.edit(p.seg, ops, "delete pops");
  };
  P.nextPopId = function () { // start_pop_index is the game's next free pop id
    const i = this.top.get("start_pop_index");
    if (i !== undefined) {
      const n = this.tree(i, 0);
      const id = +n.v;
      if (id > this.maxPopId) { this.edit(i, [[n.vs, n.ve, String(id + 1)]], "start_pop_index"); return id; }
      this.edit(i, [[n.vs, n.ve, String(this.maxPopId + 2)]], "start_pop_index");
    }
    return ++this.maxPopId;
  };
  P.clonePop = function (p, pop, ch) { return this.grouped("pop nou", () => this._clonePop(p, pop, ch)); };
  P._clonePop = function (p, pop, ch) { // copy of pop with a new id, placed after the LAST pop of its type
    const t = this.segs[p.seg].text;
    const [a, b] = this.lineRange(t, pop.node.ks, pop.node.be + 1);
    let blk = t.slice(a, b);
    const off = a;
    const newType = ch.type || pop.type;
    const same = p.pops.filter((x) => x.type === newType);
    const last = same.length ? same[same.length - 1] : p.pops[p.pops.length - 1];
    const at = this.lineRange(t, last.node.ks, last.node.be + 1)[1];
    const newId = this.nextPopId();
    const edits = [];
    edits.push([pop.fields.id.vs - off, pop.fields.id.ve - off, String(newId)]);
    if (ch.size !== undefined && pop.fields.size) edits.push([pop.fields.size.vs - off, pop.fields.size.ve - off, String(ch.size)]);
    if (pop.cr && ch.culture) edits.push([pop.cr.ks - off, pop.cr.ke - off, V2.toFileChars(ch.culture)]);
    if (pop.cr && ch.religion) edits.push([pop.cr.vs - off, pop.cr.ve - off, V2.toFileChars(ch.religion)]);
    if (ch.type) edits.push([pop.node.ks - off, pop.node.ke - off, V2.toFileChars(ch.type)]);
    // money and bank scale with the size, so the copy does not double the savings
    const ratio = ch.size !== undefined && pop.size > 0 ? ch.size / pop.size : 1;
    for (const f of ["money", "bank"]) {
      const n = pop.fields[f];
      if (n && V2.isNum(n.v)) edits.push([n.vs - off, n.ve - off, (+n.v * ratio).toFixed(5)]);
    }
    // a fresh pop: no employment, no movement, no rebel faction
    for (const f of ["faction", "movement_issue", "movement_tag", "rebel"]) {
      const n = pop.fields[f];
      if (n) { const [la, lb] = this.lineRange(t, n.ks, n.ve); edits.push([la - off, lb - off, ""]); }
    }
    edits.sort((x, y) => y[0] - x[0]);
    for (const [s, e, x] of edits) blk = blk.slice(0, s) + x + blk.slice(e);
    this.edit(p.seg, [[at, at, blk]], "add pop " + newId);
    return newId;
  };

  // ---- employment references: {province_id, index (within the pop type), type (pop type number)} ----
  const REF_RE = /province_pop_id=\s*\{\s*province_id=(\d+)\s*index=(\d+)\s*type=(\d+)\s*\}/g;
  P.popOrder = function (p) {
    const m = new Map();
    for (const x of p.pops) { if (!m.has(x.type)) m.set(x.type, []); m.get(x.type).push(x.id); }
    return m;
  };
  P.popTypeNumbers = function () { // infer type number -> type name from every employment reference
    if (this._ptn) return this._ptn;
    const cand = new Map();
    const counts = new Map();
    const cnt = (pid) => {
      if (!counts.has(pid)) { const p = this.province(pid); const c = new Map(); if (p) for (const x of p.pops) c.set(x.type, (c.get(x.type) || 0) + 1); counts.set(pid, c); }
      return counts.get(pid);
    };
    for (const sg of this.segs) {
      if (!sg.block || sg.text.indexOf("province_pop_id") < 0) continue;
      REF_RE.lastIndex = 0;
      let m;
      while ((m = REF_RE.exec(sg.text))) {
        const [, pid, idx, ty] = m;
        const c = cnt(+pid);
        const ok = new Set([...c].filter(([, n]) => n > +idx).map(([k]) => k));
        if (!cand.has(ty)) cand.set(ty, ok);
        else { const prev = cand.get(ty); for (const k of [...prev]) if (!ok.has(k)) prev.delete(k); }
      }
    }
    const res = new Map();
    // resolve unique candidates repeatedly
    let changed = true;
    while (changed) {
      changed = false;
      for (const [ty, set] of cand) {
        if (res.has(ty)) continue;
        for (const v of res.values()) set.delete(v);
        if (set.size === 1) { res.set(ty, [...set][0]); changed = true; }
      }
    }
    this._ptn = res; // number(string) -> type name
    return res;
  };
  // After pops of province p were deleted or re-typed: fix every reference to it.
  P.remapEmployment = function (pid, before, after) {
    const ptn = this.popTypeNumbers();
    const segIdx = [this.provinces.get(pid), ...this.countries.values()];
    let fixed = 0, dropped = 0, unknown = 0;
    for (const si of segIdx) {
      const t = this.segs[si].text;
      if (t.indexOf("province_id=" + pid) < 0) continue;
      const ops = [];
      REF_RE.lastIndex = 0;
      let m;
      while ((m = REF_RE.exec(t))) {
        if (+m[1] !== pid) continue;
        const type = ptn.get(m[3]);
        if (!type) { unknown++; continue; }
        const oldIds = before.get(type) || [];
        const popId = oldIds[+m[2]];
        const newIdx = popId === undefined ? -1 : (after.get(type) || []).indexOf(popId);
        if (newIdx === +m[2]) continue;
        if (newIdx >= 0) {
          const is = m.index + m[0].indexOf("index=") + 6;
          ops.push([is, is + m[2].length, String(newIdx)]);
          fixed++;
        } else { // drop the whole employee entry { province_pop_id=... count=... }
          const open = t.lastIndexOf("{", m.index - 1);
          const close = V2.matchBrace(t, open, t.length);
          const [a, b] = this.lineRange(t, open, close + 1);
          ops.push([a, b, ""]);
          dropped++;
        }
      }
      // drop ops nested in a dropped range are impossible (refs are leaves)
      if (ops.length) this.edit(si, ops, "employment " + pid);
    }
    return { fixed, dropped, unknown };
  };
  P.structural = function (p, fn) { return this.grouped("pop", () => this._structural(p, fn)); };
  P._structural = function (p, fn) { // run fn (which may delete / re-type pops), then repair references
    const before = this.popOrder(p);
    const mark = this.undo.length;
    fn();
    const p2 = this.province(p.id);
    const after = this.popOrder(p2);
    const r = this.remapEmployment(p.id, before, after);
    r.steps = this.undo.length - mark;
    return r;
  };

  // --- province fields ---
  P.setProvinceTag = function (p, key, tag) { // owner / controller
    const n = (p.node.c || []).find((x) => x.k === key && x.v !== undefined);
    if (!n) return false;
    return this.setScalar(p.seg, n, tag, key);
  };
  P.addCore = function (p, tag) {
    if (p.cores.some((c) => c.tag === tag)) return false;
    const t = this.segs[p.seg].text;
    const anchor = p.cores.length ? p.cores[p.cores.length - 1].node : (p.node.c || []).find((x) => x.k === "controller" || x.k === "owner" || x.k === "name");
    if (!anchor) return false;
    const ind = this.indentAt(t, anchor.ks);
    return this.edit(p.seg, [[anchor.ve, anchor.ve, this.nl(t) + ind + "core=" + q(tag)]], "core +" + tag);
  };
  P.removeCore = function (p, tag) {
    const c = p.cores.find((x) => x.tag === tag);
    if (!c) return false;
    const t = this.segs[p.seg].text;
    const [a, b] = this.lineRange(t, c.node.ks, c.node.ve);
    return this.edit(p.seg, [[a, b, ""]], "core -" + tag);
  };

  // --- countries ---
  P.setCountryScalar = function (c, key, value) {
    const n = c.scalars.find((x) => x.k === key);
    if (!n) return false;
    return this.setScalar(c.seg, n, value, c.tag + "." + key);
  };
  P.setAccepted = function (c, list) {
    const t = this.segs[c.seg].text;
    const nl = this.nl(t);
    const uniq = [...new Set(list)].filter((x) => x && x !== c.primary);
    if (c.acceptedNode) {
      const n = c.acceptedNode;
      const ind = this.indentAt(t, n.ks);
      if (!uniq.length) { const [a, b] = this.lineRange(t, n.ks, n.be + 1); return this.edit(c.seg, [[a, b, ""]], c.tag + " accepted"); }
      const body = nl + uniq.map((x) => q(V2.toFileChars(x))).join(nl) + nl + ind;
      return this.edit(c.seg, [[n.bs + 1, n.be, body]], c.tag + " accepted");
    }
    if (!uniq.length) return false;
    const anchor = c.scalars.find((x) => x.k === "primary_culture") || c.scalars[0];
    const ind = this.indentAt(t, anchor.ks);
    const blk = nl + ind + "culture=" + nl + ind + "{" + nl + uniq.map((x) => q(V2.toFileChars(x))).join(nl) + nl + ind + "}";
    return this.edit(c.seg, [[anchor.ve, anchor.ve, blk]], c.tag + " accepted");
  };

  // --- bulk ---
  // scope: {provinces:[ids]} | {owner:TAG} | {all:true}; filter: {culture, religion, type}; set: {culture, religion}
  P.bulkConvert = function (scope, filter, set) { return this.grouped("conversie", () => this._bulkConvert(scope, filter, set)); };
  P._bulkConvert = function (scope, filter, set) {
    let ids;
    if (scope.provinces) ids = scope.provinces;
    else ids = [...this.provinces.keys()];
    let pops = 0, people = 0, provs = 0;
    const undoMark = this.undo.length;
    for (const id of ids) {
      const p = this.province(id);
      if (!p) continue;
      if (scope.owner && p.owner !== scope.owner) continue;
      const ops = [];
      for (const pop of p.pops) {
        if (filter.culture && pop.culture !== filter.culture) continue;
        if (filter.religion && pop.religion !== filter.religion) continue;
        if (filter.type && pop.type !== filter.type) continue;
        if (!pop.cr) continue;
        let hit = false;
        if (set.culture && set.culture !== pop.culture) { ops.push([pop.cr.ks, pop.cr.ke, V2.toFileChars(set.culture)]); hit = true; }
        if (set.religion && set.religion !== pop.religion) { ops.push([pop.cr.vs, pop.cr.ve, V2.toFileChars(set.religion)]); hit = true; }
        if (hit) { pops++; people += pop.size; }
      }
      if (ops.length && this.edit(p.seg, ops, "bulk convert")) provs++;
    }
    return { pops, people, provs, steps: this.undo.length - undoMark };
  };

  P.ownersIndex = function () {
    const m = new Map();
    for (const id of this.provinces.keys()) {
      const p = this.province(id);
      if (!p.owner) continue;
      let e = m.get(p.owner);
      if (!e) m.set(p.owner, (e = { provinces: 0, people: 0, pops: 0 }));
      e.provinces++; e.pops += p.pops.length;
      for (const x of p.pops) e.people += x.size;
    }
    this._owners = m;
    return m;
  };
})(V2);
