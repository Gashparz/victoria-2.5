// Victoria II save core: split / parse / edit / write. Works on the text as a JS string whose
// char codes are the file's bytes (0..255), so writing it back is byte-exact.
"use strict";
const V2 = (() => {
  const WS = new Uint8Array(256);
  for (const c of [9, 10, 13, 32]) WS[c] = 1;
  const DELIM = new Uint8Array(256);
  for (const c of [9, 10, 13, 32, 123, 125, 61, 34]) DELIM[c] = 1; // ws { } = "

  function bytesToStr(u8) {
    let s = "";
    const CH = 0x8000;
    for (let i = 0; i < u8.length; i += CH) s += String.fromCharCode.apply(null, u8.subarray(i, i + CH));
    return s;
  }
  // Windows-1252 for user-typed text (chars above 255 that cp1252 can encode)
  const CP1252 = { 0x20ac: 0x80, 0x201a: 0x82, 0x192: 0x83, 0x201e: 0x84, 0x2026: 0x85, 0x2020: 0x86, 0x2021: 0x87, 0x2c6: 0x88,
    0x2030: 0x89, 0x160: 0x8a, 0x2039: 0x8b, 0x152: 0x8c, 0x17d: 0x8e, 0x2018: 0x91, 0x2019: 0x92, 0x201c: 0x93, 0x201d: 0x94,
    0x2022: 0x95, 0x2013: 0x96, 0x2014: 0x97, 0x2dc: 0x98, 0x2122: 0x99, 0x161: 0x9a, 0x203a: 0x9b, 0x153: 0x9c, 0x17e: 0x9e, 0x178: 0x9f };
  function toFileChars(s) { // user text -> byte chars
    let o = "";
    for (const ch of s) {
      const c = ch.codePointAt(0);
      if (c < 256) o += ch; else if (CP1252[c]) o += String.fromCharCode(CP1252[c]); else o += "?";
    }
    return o;
  }
  const DEC1252 = typeof TextDecoder !== "undefined" ? new TextDecoder("windows-1252") : null;
  function display(s) { // byte chars -> readable
    if (!s || !DEC1252) return s;
    let hi = false;
    for (let i = 0; i < s.length; i++) if (s.charCodeAt(i) > 127) { hi = true; break; }
    if (!hi) return s;
    const u = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i);
    return DEC1252.decode(u);
  }
  function strToBytes(s) {
    const u = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i) & 255;
    return u;
  }

  // ---- tokenizer ----
  function skipWs(t, i, end) {
    for (;;) {
      while (i < end && WS[t.charCodeAt(i)]) i++;
      if (i < end && t.charCodeAt(i) === 35) { // # comment
        while (i < end && t.charCodeAt(i) !== 10) i++;
        continue;
      }
      return i;
    }
  }
  function tokEnd(t, i, end) {
    if (t.charCodeAt(i) === 34) { const j = t.indexOf('"', i + 1); return j < 0 || j >= end ? end : j + 1; }
    while (i < end && !DELIM[t.charCodeAt(i)]) i++;
    return i;
  }
  // matching '}' for '{' at bs (skips quoted strings)
  function matchBrace(t, bs, end) {
    let d = 0;
    for (let i = bs; i < end; i++) {
      const c = t.charCodeAt(i);
      if (c === 34) { const j = t.indexOf('"', i + 1); i = j < 0 ? end : j; }
      else if (c === 123) d++;
      else if (c === 125) { if (--d === 0) return i; }
    }
    return end;
  }

  // Node: {k, ks, ke, vs, ve, v} scalar | {k, ks, ke, bs, be, c} block (c = null until expanded)
  // k null for keyless items. Offsets are into the string parsed.
  function parseRange(t, start, end, depth) {
    const out = [];
    let i = start;
    for (;;) {
      i = skipWs(t, i, end);
      if (i >= end) break;
      const c0 = t.charCodeAt(i);
      if (c0 === 125) { i++; continue; } // stray }
      if (c0 === 123) { // keyless block
        const be = matchBrace(t, i, end);
        const n = { k: null, ks: i, ke: i, bs: i, be, c: null };
        if (depth > 0) n.c = parseRange(t, i + 1, be, depth - 1);
        out.push(n);
        i = be + 1;
        continue;
      }
      const ks = i, ke = tokEnd(t, i, end);
      let j = skipWs(t, ke, end);
      if (j < end && t.charCodeAt(j) === 61) { // =
        j = skipWs(t, j + 1, end);
        const key = t.slice(ks, ke);
        if (j < end && t.charCodeAt(j) === 123) {
          const be = matchBrace(t, j, end);
          const n = { k: key, ks, ke, bs: j, be, c: null };
          if (depth > 0) n.c = parseRange(t, j + 1, be, depth - 1);
          out.push(n);
          i = be + 1;
        } else {
          const ve = tokEnd(t, j, end);
          out.push({ k: key, ks, ke, vs: j, ve, v: t.slice(j, ve) });
          i = ve === j ? j + 1 : ve;
        }
      } else {
        out.push({ k: null, ks, ke, vs: ks, ve: ke, v: t.slice(ks, ke) });
        i = ke === ks ? ks + 1 : ke;
      }
    }
    return out;
  }
  function expand(t, n, depth) { if (n.c === null && n.bs !== undefined) n.c = parseRange(t, n.bs + 1, n.be, depth); return n.c; }

  // ---- top-level split into contiguous segments ----
  function split(text) {
    const segs = [];
    let i = 0, last = 0;
    const end = text.length;
    for (;;) {
      i = skipWs(text, i, end);
      if (i >= end) break;
      const ks = i, ke = tokEnd(text, i, end);
      let j = skipWs(text, ke, end);
      let vend;
      if (j < end && text.charCodeAt(j) === 61) {
        j = skipWs(text, j + 1, end);
        if (j < end && text.charCodeAt(j) === 123) vend = matchBrace(text, j, end) + 1;
        else vend = Math.max(tokEnd(text, j, end), j + 1);
        segs.push({ key: text.slice(ks, ke), text: text.slice(last, vend), block: text.charCodeAt(j) === 123 });
      } else {
        vend = Math.max(ke, ks + 1);
        segs.push({ key: null, text: text.slice(last, vend), block: false });
      }
      last = i = vend;
    }
    if (last < end) segs.push({ key: null, text: text.slice(last), block: false, tail: true });
    return segs;
  }

  const unq = (s) => (s && s.length >= 2 && s[0] === '"' && s[s.length - 1] === '"' ? s.slice(1, -1) : s);
  const isNum = (s) => /^-?\d+(\.\d+)?$/.test(s);
  const TAG_RE = /^[A-Z][A-Z0-9]{2}$/;
  const POP_FIELDS = new Set(["id", "size", "money", "ideology", "issues", "con", "mil", "literacy", "bank", "production_type",
    "stockpile", "need", "last_spending", "current_producing", "percent_afforded", "percent_sold_domestic", "percent_sold_export",
    "leftover", "throttle", "needs_cost", "production_income", "con_factor", "everyday_needs", "luxury_needs", "life_needs",
    "random", "promoted", "demoted", "local_migration", "external_migration", "colonial_migration", "days_of_loss", "faction",
    "movement_issue", "movement_tag", "rebel", "converted", "assimilated", "unemployment"]);

  class Save {
    constructor(text, name) {
      this.name = name || "save.v2";
      this.segs = split(text);
      this.cache = new Map();
      this.undo = [];
      this.provinces = new Map(); // id -> seg index
      this.countries = new Map(); // tag -> seg index
      this.top = new Map();       // key -> first seg index
      this.dirty = false;
      this.segs.forEach((s, i) => {
        if (s.key === null) return;
        if (!this.top.has(s.key)) this.top.set(s.key, i);
        if (!s.block) return;
        if (/^\d+$/.test(s.key)) this.provinces.set(+s.key, i);
        else if (TAG_RE.test(s.key)) this.countries.set(s.key, i);
      });
      this.maxPopId = 0;
    }
    text() { return this.segs.map((s) => s.text).join(""); }
    // parse of a segment: root node (the top-level entry)
    tree(i, depth = 2) {
      const c = this.cache.get(i);
      if (c && c.depth >= depth) return c.node;
      const t = this.segs[i].text;
      const nodes = parseRange(t, 0, t.length, depth + 1);
      const node = nodes.find((n) => n.k !== null) || nodes[0];
      this.cache.set(i, { depth, node });
      return node;
    }
    setText(i, txt, label) {
      if (txt === this.segs[i].text) return false;
      this.undo.push({ i, old: this.segs[i].text, label, group: this.group || null });
      if (this.undo.length > 500) this.undo.shift();
      this.segs[i].text = txt;
      this.cache.delete(i);
      this.dirty = true;
      return true;
    }
    undoLast() { // one step, or a whole group of steps made by one action
      const u = this.undo.pop();
      if (!u) return null;
      this.segs[u.i].text = u.old;
      this.cache.delete(u.i);
      while (u.group && this.undo.length && this.undo[this.undo.length - 1].group === u.group) {
        const v = this.undo.pop();
        this.segs[v.i].text = v.old;
        this.cache.delete(v.i);
      }
      this.dirty = true;
      return u;
    }
    grouped(label, fn) { // every edit made inside fn is undone together
      const outer = this.group;
      if (!outer) this.group = label + ":" + (++this._gid || (this._gid = 1));
      try { return fn(); } finally { if (!outer) this.group = null; }
    }
    steps() { let n = 0, last = undefined; for (const u of this.undo) { if (!u.group || u.group !== last) n++; last = u.group; } return n; }
    // apply several [start,end,text] edits to segment i in one undo step
    edit(i, ops, label) {
      if (!ops.length) return false;
      ops.sort((a, b) => b[0] - a[0]);
      let t = this.segs[i].text;
      let prev = Infinity;
      for (const [s, e, x] of ops) {
        if (e > prev) throw new Error("overlapping edits");
        t = t.slice(0, s) + x + t.slice(e);
        prev = s;
      }
      return this.setText(i, t, label);
    }

    // ---- provinces ----
    province(id) {
      const i = this.provinces.get(id);
      if (i === undefined) return null;
      const n = this.tree(i, 2);
      const p = { id, seg: i, node: n, name: "", owner: "", controller: "", cores: [], pops: [], scalars: [] };
      for (const ch of n.c || []) {
        if (ch.v !== undefined) {
          if (ch.k === "name") p.name = unq(ch.v);
          else if (ch.k === "owner") p.owner = unq(ch.v);
          else if (ch.k === "controller") p.controller = unq(ch.v);
          else if (ch.k === "core") p.cores.push({ tag: unq(ch.v), node: ch });
          if (ch.k) p.scalars.push(ch);
        } else if (ch.c && ch.c.some((x) => x.k === "size") && ch.c.some((x) => x.k === "id")) {
          p.pops.push(this.popFrom(ch, p));
        }
      }
      return p;
    }
    popFrom(n, prov) {
      const pop = { type: n.k, node: n, id: 0, size: 0, culture: "", religion: "", cr: null, fields: {} };
      for (const x of n.c) {
        if (x.v === undefined || x.k === null) continue;
        if (x.k === "id") pop.id = +x.v;
        else if (x.k === "size") pop.size = +x.v;
        if (!pop.cr && !POP_FIELDS.has(x.k) && !isNum(x.k) && !isNum(x.v) && x.v[0] !== '"') {
          pop.cr = x; pop.culture = x.k; pop.religion = x.v;
        }
        pop.fields[x.k] = x;
      }
      if (pop.id > this.maxPopId) this.maxPopId = pop.id;
      return pop;
    }
    allProvinces() { return [...this.provinces.keys()].sort((a, b) => a - b).map((id) => this.province(id)); }

    // ---- countries ----
    country(tag) {
      const i = this.countries.get(tag);
      if (i === undefined) return null;
      const n = this.tree(i, 1);
      const c = { tag, seg: i, node: n, scalars: [], accepted: [], acceptedNode: null, primary: "", religion: "" };
      for (const ch of n.c || []) {
        if (ch.v !== undefined && ch.k) {
          c.scalars.push(ch);
          if (ch.k === "primary_culture") c.primary = unq(ch.v);
          if (ch.k === "religion") c.religion = unq(ch.v);
        } else if (ch.k === "culture" && ch.c) {
          c.acceptedNode = ch;
          c.accepted = ch.c.filter((x) => x.k === null && x.v !== undefined).map((x) => unq(x.v));
        }
      }
      return c;
    }
    exists(tag) { // a country "exists" if it owns any province
      return this._owners ? this._owners.has(tag) : true;
    }

    // ---- generic edit helpers ----
    setScalar(seg, node, value, label) { // keeps the original quoting
      const q = node.v && node.v[0] === '"';
      let v = toFileChars(String(value));
      if (q && v[0] !== '"') v = '"' + v.replace(/"/g, "") + '"';
      return this.edit(seg, [[node.vs, node.ve, v]], label);
    }
    lineRange(t, s, e) { // whole lines covering [s,e)
      let a = t.lastIndexOf("\n", s - 1) + 1;
      let b = t.indexOf("\n", e);
      b = b < 0 ? t.length : b + 1;
      return [a, b];
    }
    indentAt(t, pos) { const a = t.lastIndexOf("\n", pos - 1) + 1; return t.slice(a, pos).match(/^[\t ]*/)[0]; }
    nl(t) { return t.indexOf("\r\n") >= 0 ? "\r\n" : "\n"; }
  }

  return { Save, parseRange, expand, split, unq, isNum, bytesToStr, strToBytes, toFileChars, display, matchBrace, POP_FIELDS };
})();
if (typeof module !== "undefined") module.exports = V2;
