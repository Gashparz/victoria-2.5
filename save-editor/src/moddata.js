// Game / mod data: cultures, religions, pop types, localisation. Folders are layered: later folders
// override earlier ones file by file (like the game does for a mod).
"use strict";
const ModData = (() => {
  const dec = new TextDecoder("windows-1252");
  const data = { folders: [], cultures: new Map(), groups: new Map(), religions: new Map(), poptypes: [], loc: new Map(),
    tagColors: new Map(), cultureColors: new Map(), religionColors: new Map(), map: null };

  // A folder source: {name, list(dir) -> [names], read(path) -> text|null}
  function fromHandle(h) {
    async function dirOf(path) {
      let d = h;
      for (const part of path.split("/").filter(Boolean)) {
        try { d = await d.getDirectoryHandle(part); } catch { return null; }
      }
      return d;
    }
    return {
      name: h.name, handle: h,
      async list(dir) {
        const d = await dirOf(dir); if (!d) return [];
        const out = [];
        for await (const [n, e] of d.entries()) if (e.kind === "file") out.push(n);
        return out;
      },
      async read(path) {
        const b = await this.readBin(path); return b ? dec.decode(b) : null;
      },
      async readBin(path) {
        const parts = path.split("/"); const f = parts.pop();
        const d = await dirOf(parts.join("/")); if (!d) return null;
        try { const fh = await d.getFileHandle(f); return new Uint8Array(await (await fh.getFile()).arrayBuffer()); } catch { return null; }
      },
    };
  }
  function fromFileList(files) { // <input webkitdirectory>
    const map = new Map();
    let root = "";
    for (const f of files) {
      const parts = f.webkitRelativePath.split("/");
      root = parts.shift();
      map.set(parts.join("/").toLowerCase(), f);
    }
    return {
      name: root,
      async list(dir) {
        const pre = dir ? dir.toLowerCase().replace(/\/?$/, "/") : "";
        return [...map.keys()].filter((k) => k.startsWith(pre) && !k.slice(pre.length).includes("/")).map((k) => map.get(k).name);
      },
      async read(path) { const f = map.get(path.toLowerCase()); return f ? dec.decode(await f.arrayBuffer()) : null; },
      async readBin(path) { const f = map.get(path.toLowerCase()); return f ? new Uint8Array(await f.arrayBuffer()) : null; },
    };
  }

  // tiny clausewitz parser for data files (no offsets needed)
  function parse(t) {
    const toks = t.replace(/#[^\n]*/g, "").match(/"[^"]*"|[{}=]|[^\s{}="]+/g) || [];
    let i = 0;
    function block() {
      const out = [];
      while (i < toks.length) {
        const a = toks[i];
        if (a === "}") { i++; return out; }
        if (toks[i + 1] === "=") {
          i += 2;
          if (toks[i] === "{") { i++; out.push([a, block()]); } else out.push([a, toks[i++]]);
        } else if (a === "{") { i++; out.push([null, block()]); } else { out.push([null, a]); i++; }
      }
      return out;
    }
    return block();
  }

  function fromPack(pack) { return { name: pack.name, pack, async list() { return []; }, async read() { return null; }, async readBin() { return null; } }; }
  const packOf = () => { for (let k = data.folders.length - 1; k >= 0; k--) if (data.folders[k].pack) return data.folders[k].pack; return null; };
  function rgb(items) {
    let v = items.map(([, x]) => parseFloat(x)).filter((x) => !isNaN(x)).slice(0, 3);
    if (v.length < 3) return null;
    if (v.every((x) => x <= 1)) v = v.map((x) => x * 255);
    return v.map((x) => Math.max(0, Math.min(255, Math.round(x))));
  }
  // a layer with replace_path hides every lower layer's files under those folders (like the game)
  const replaces = (f, path) => (f.replace || []).some((r) => { const d = r.toLowerCase().replace(/\/+$/, "") + "/"; return path.toLowerCase().startsWith(d); });
  async function newestBin(path) {
    for (let k = data.folders.length - 1; k >= 0; k--) {
      const f = data.folders[k];
      if (!f.pack) { const b = await f.readBin(path); if (b) return b; }
      if (replaces(f, path)) break;
    }
    return null;
  }
  async function newest(path) { // file from the topmost layer that has it
    for (let k = data.folders.length - 1; k >= 0; k--) {
      const f = data.folders[k];
      if (!f.pack) { const t = await f.read(path); if (t !== null) return t; }
      if (replaces(f, path)) break;
    }
    return null;
  }
  async function dirFiles(dir, ext) { // name -> layer (top wins; replace_path clears what is below)
    const m = new Map();
    for (const f of data.folders) {
      if (replaces(f, dir + "/x")) m.clear();
      for (const n of await f.list(dir)) if (n.toLowerCase().endsWith(ext)) m.set(n.toLowerCase(), f);
    }
    return m;
  }

  // ---- a whole game folder: base game + the mods chosen like in the launcher ----
  function sub(src, prefix, name, replace) { // view of src under prefix/
    const p = prefix.replace(/\/+$/, "") + "/";
    return { name, replace: replace || [], base: src, prefix: p,
      list: (d) => src.list(p + d), read: (f) => src.read(p + f), readBin: (f) => src.readBin(p + f) };
  }
  function parseMod(text, file) {
    const m = { file, name: file.replace(/\.mod$/i, ""), path: "", replace: [], deps: [], userDir: "" };
    for (const [k, v] of parse(text)) {
      const u = (x) => (typeof x === "string" ? x.replace(/^"|"$/g, "") : x);
      if (k === "name") m.name = u(v);
      else if (k === "path") m.path = u(v).replace(/\\/g, "/");
      else if (k === "user_dir") m.userDir = u(v);
      else if (k === "replace_path") m.replace.push(u(v).replace(/\\/g, "/"));
      else if (k === "dependencies" && Array.isArray(v)) for (const [, d] of v) m.deps.push(u(d));
    }
    return m;
  }
  async function scanGame(src) { // -> [{name, path, replace, deps}] or null if src is not a game folder
    const files = await src.list("mod");
    const mods = [];
    for (const f of files) if (/\.mod$/i.test(f)) { const t = await src.read("mod/" + f); if (t) mods.push(parseMod(t, f)); }
    const hasExe = (await src.list("")).some((n) => /^v2game\.exe$/i.test(n));
    return hasExe || mods.length ? mods : null;
  }
  function layersFor(src, mods, chosen) { // base game, then each chosen mod after its dependencies
    const byName = new Map(mods.map((m) => [m.name, m]));
    const order = [], seen = new Set();
    const add = (m) => { if (!m || seen.has(m.name)) return; seen.add(m.name); for (const d of m.deps) add(byName.get(d)); order.push(m); };
    for (const n of chosen) add(byName.get(n));
    const base = { name: "Victoria II", replace: [], list: (d) => src.list(d), read: (f) => src.read(f), readBin: (f) => src.readBin(f), handle: src.handle };
    return [base, ...order.map((m) => sub(src, m.path, m.name, m.replace))];
  }

  async function reload(progress) {
    data.cultures.clear(); data.groups.clear(); data.religions.clear(); data.poptypes = []; data.loc.clear();
    data.tagColors.clear(); data.cultureColors.clear(); data.religionColors.clear(); data.map = null;
    if (!data.folders.length) return data;
    const pack = packOf();
    progress && progress("cultures");
    const ct = await newest("common/cultures.txt");
    if (!ct && pack) for (const [g, list] of Object.entries(pack.groups)) { data.groups.set(g, list); for (const c of list) data.cultures.set(c, g); }
    if (ct) for (const [g, body] of parse(ct)) {
      if (!Array.isArray(body)) continue;
      const list = [];
      for (const [k, v] of body) if (k && Array.isArray(v) && v.some(([x]) => x === "color" || x === "first_names")) {
        list.push(k); data.cultures.set(k, g);
        const col = v.find(([x]) => x === "color"); if (col && Array.isArray(col[1])) data.cultureColors.set(k, rgb(col[1]));
      }
      data.groups.set(g, list);
    }
    progress && progress("religions");
    const rt = await newest("common/religion.txt");
    if (!rt && pack) for (const [k, g] of Object.entries(pack.religions)) data.religions.set(k, g);
    if (rt) for (const [g, body] of parse(rt)) if (Array.isArray(body)) for (const [k, v] of body) if (k && Array.isArray(v)) {
      data.religions.set(k, g);
      const col = v.find(([x]) => x === "color"); if (col && Array.isArray(col[1])) data.religionColors.set(k, rgb(col[1]));
    }
    progress && progress("pop types");
    data.poptypes = [...(await dirFiles("poptypes", ".txt")).keys()].map((n) => n.replace(/\.txt$/, "")).sort();
    if (!data.poptypes.length && pack) data.poptypes = pack.poptypes.slice();
    progress && progress("localisation");
    const locs = await dirFiles("localisation", ".csv");
    // the game reads localisation files in name order and keeps the first definition of a key
    for (const name of [...locs.keys()].sort()) {
      const t = await locs.get(name).read("localisation/" + name);
      if (!t) continue;
      for (const line of t.split(/\r?\n/)) {
        const s = line.indexOf(";"); if (s <= 0 || line[0] === "#") continue;
        const k = line.slice(0, s); if (data.loc.has(k)) continue;
        const e = line.indexOf(";", s + 1);
        data.loc.set(k, line.slice(s + 1, e < 0 ? undefined : e));
      }
    }
    if (pack) for (const [k, v] of Object.entries(pack.loc)) if (!data.loc.has(k)) data.loc.set(k, v);
    progress && progress("country colors");
    const cl = await newest("common/countries.txt");
    if (cl) {
      for (const [tag, file] of parse(cl)) {
        if (!tag || Array.isArray(file)) continue;
        const t = await newest("common/" + file.replace(/^"|"$/g, ""));
        if (!t) continue;
        const col = parse(t).find(([x]) => x === "color");
        if (col && Array.isArray(col[1])) data.tagColors.set(tag, rgb(col[1]));
      }
    }
    if (pack) {
      for (const [k, v] of Object.entries(pack.tagColors || {})) if (!data.tagColors.has(k)) data.tagColors.set(k, v);
      if (!ct) for (const [k, v] of Object.entries(pack.cultureColors || {})) data.cultureColors.set(k, v);
      if (!rt) for (const [k, v] of Object.entries(pack.religionColors || {})) data.religionColors.set(k, v);
    }
    // the map: provinces.bmp + definition.csv from a folder, or the pack's
    data.mapSource = null;
    if ((await newest("map/definition.csv")) !== null) {
      data.mapSource = { folder: { read: newest, readBin: newestBin }, name: data.folders.map((f) => f.name).join("+") };
    } else if (pack && pack.map) data.mapSource = { pack: pack.map, name: pack.name };
    return data;
  }

  // remember folders between sessions (directory handles only)
  const DB = "v2se", STORE = "folders";
  function idb() {
    return new Promise((res, rej) => { const r = indexedDB.open(DB, 1); r.onupgradeneeded = () => r.result.createObjectStore(STORE); r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); });
  }
  async function remember() {
    try {
      const hs = data.folders.filter((f) => f.handle).map((f) => f.handle);
      const db = await idb(); const tx = db.transaction(STORE, "readwrite"); tx.objectStore(STORE).put(hs, "list");
    } catch {}
  }
  async function recall() {
    try {
      const db = await idb();
      return await new Promise((res) => { const r = db.transaction(STORE).objectStore(STORE).get("list"); r.onsuccess = () => res(r.result || []); r.onerror = () => res([]); });
    } catch { return []; }
  }

  return { data, fromHandle, fromFileList, fromPack, reload, newestBin, scanGame, layersFor, remember, recall,
    name: (k) => data.loc.get(k) || null };
})();
