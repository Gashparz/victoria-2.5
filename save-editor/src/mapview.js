// Province map: decodes the province id raster, paints it by a per-province colour table, pan/zoom/hover/click.
"use strict";
const MapView = (() => {
  let geo = null;      // {w, h, ids: Uint16Array, sea: Set, maxId, bbox: Int32Array(4*(maxId+1))}
  let loading = null;

  async function inflate(bytes) {
    const ds = new DecompressionStream("deflate");
    const out = await new Response(new Blob([bytes]).stream().pipeThrough(ds)).arrayBuffer();
    return new Uint8Array(out);
  }
  function b64(s) { const bin = atob(s); const u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i); return u; }

  async function fromPack(m) {
    const raw = await inflate(b64(m.ids));
    const ids = new Uint16Array(raw.buffer, raw.byteOffset, raw.byteLength / 2);
    return { w: m.w, h: m.h, ids, sea: new Set(m.sea) };
  }
  async function fromFolder(f) {
    const def = await f.read("map/definition.csv");
    const bmp = await f.readBin("map/provinces.bmp");
    if (!def || !bmp) throw new Error("map/provinces.bmp or definition.csv is missing");
    const dv = new DataView(bmp.buffer, bmp.byteOffset, bmp.byteLength);
    const off = dv.getUint32(10, true), w = dv.getInt32(18, true), hh = dv.getInt32(22, true), bpp = dv.getUint16(28, true);
    if (bpp !== 24) throw new Error("provinces.bmp must be 24-bit");
    const h = Math.abs(hh), bottomUp = hh > 0, stride = (w * 3 + 3) & ~3;
    const lut = new Map();
    for (const line of def.split(/\r?\n/)) {
      const p = line.split(";");
      if (p.length < 4 || !/^\d+$/.test(p[0]) || !/^\d+$/.test(p[1].trim())) continue;
      lut.set((+p[1] << 16) | (+p[2] << 8) | +p[3], +p[0]);
    }
    const ids = new Uint16Array(w * h);
    for (let y = 0; y < h; y++) {
      const row = off + (bottomUp ? y : h - 1 - y) * stride; // Victoria II reads the rows as stored
      let last = -1, lid = 0;
      for (let x = 0; x < w; x++) {
        const i = row + x * 3;
        const key = (bmp[i + 2] << 16) | (bmp[i + 1] << 8) | bmp[i];
        if (key !== last) { last = key; lid = lut.get(key) || 0; }
        ids[y * w + x] = lid;
      }
    }
    let sea = new Set();
    const dm = await f.read("map/default.map");
    if (dm) { const m = dm.match(/sea_starts\s*=\s*\{([^}]*)\}/); if (m) sea = new Set(m[1].split(/\s+/).filter(Boolean).map(Number)); }
    return { w, h, ids, sea };
  }
  function finish(g) {
    let maxId = 0;
    for (let i = 0; i < g.ids.length; i++) if (g.ids[i] > maxId) maxId = g.ids[i];
    const bbox = new Int32Array(4 * (maxId + 1)).fill(-1);
    const { w, h, ids } = g;
    for (let y = 0; y < h; y++) for (let x = 0, i = y * w; x < w; x++, i++) {
      const b = ids[i] * 4;
      if (bbox[b] < 0) { bbox[b] = x; bbox[b + 1] = y; bbox[b + 2] = x; bbox[b + 3] = y; }
      else { if (x < bbox[b]) bbox[b] = x; if (x > bbox[b + 2]) bbox[b + 2] = x; if (y > bbox[b + 3]) bbox[b + 3] = y; }
    }
    g.maxId = maxId; g.bbox = bbox;
    return g;
  }
  async function load(src) {
    if (!src) throw new Error("No map data (choose the game folder in the Mod data tab)");
    if (geo && geo.src === src) return geo;
    if (loading && loading.src === src) return loading.p;
    const p = (async () => { const g = finish(src.pack ? await fromPack(src.pack) : await fromFolder(src.folder)); g.src = src; geo = g; return g; })();
    loading = { src, p };
    return p;
  }

  // ---- view ----
  class View {
    constructor(host, opts) {
      this.g = geo; this.opts = opts;
      host.innerHTML = "";
      this.cv = document.createElement("canvas");
      this.cv.className = "mapcv";
      host.appendChild(this.cv);
      this.tip = document.createElement("div"); this.tip.className = "maptip"; host.appendChild(this.tip);
      this.off = document.createElement("canvas"); this.off.width = this.g.w; this.off.height = this.g.h;
      this.sel = null; this.hover = 0;
      const st = opts.state || {};
      this.scale = st.scale || 0; this.cx = st.cx ?? this.g.w / 2; this.cy = st.cy ?? this.g.h / 2;
      this.resize();
      this.ro = new ResizeObserver(() => { this.resize(); this.draw(); }); this.ro.observe(host);
      this.bind();
    }
    destroy() { this.ro.disconnect(); }
    state() { return { scale: this.scale, cx: this.cx, cy: this.cy }; }
    resize() {
      const r = this.cv.parentElement.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      this.cw = Math.max(200, r.width); this.ch = Math.max(200, r.height);
      this.cv.width = Math.round(this.cw * dpr); this.cv.height = Math.round(this.ch * dpr);
      this.cv.style.width = this.cw + "px"; this.cv.style.height = this.ch + "px";
      this.dpr = dpr;
      const fit = Math.min(this.cw / this.g.w, this.ch / this.g.h);
      this.minScale = fit;
      if (!this.scale || this.scale < fit) this.scale = fit;
    }
    // colours: Uint32Array (ABGR little-endian) per province id; border: owner key array to darken country borders
    paint(colors, groupKey) {
      const { w, h, ids } = this.g;
      const ctx = this.off.getContext("2d");
      const img = ctx.createImageData(w, h);
      const px = new Uint32Array(img.data.buffer);
      const B1 = 0xff3a3a3a, B2 = 0xff101010;
      for (let y = 0; y < h; y++) {
        const r = y * w;
        for (let x = 0; x < w; x++) {
          const i = r + x, id = ids[i];
          let c = colors[id];
          const rt = x + 1 < w ? ids[i + 1] : id, dn = y + 1 < h ? ids[i + w] : id;
          if (rt !== id || dn !== id) {
            const sea = this.g.sea.has(id);
            const other = rt !== id ? rt : dn;
            if (!(sea && this.g.sea.has(other))) {
              if (groupKey && groupKey[id] !== groupKey[other]) c = B2;
              else if (!sea) c = ((c >>> 1) & 0x7f7f7f7f) | 0xff000000; // darker same-owner border
            }
          }
          px[i] = c;
        }
      }
      ctx.putImageData(img, 0, 0);
      this.draw();
    }
    toMap(ex, ey) { return [this.cx + (ex - this.cw / 2) / this.scale, this.cy + (ey - this.ch / 2) / this.scale]; }
    idAt(mx, my) { const x = Math.floor(mx), y = Math.floor(my); if (x < 0 || y < 0 || x >= this.g.w || y >= this.g.h) return 0; return this.g.ids[y * this.g.w + x]; }
    clamp() {
      const hw = this.cw / 2 / this.scale, hh = this.ch / 2 / this.scale;
      this.cx = Math.min(Math.max(this.cx, Math.min(hw, this.g.w / 2)), Math.max(this.g.w - hw, this.g.w / 2));
      this.cy = Math.min(Math.max(this.cy, Math.min(hh, this.g.h / 2)), Math.max(this.g.h - hh, this.g.h / 2));
    }
    draw() {
      this.clamp();
      const ctx = this.cv.getContext("2d");
      ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
      ctx.fillStyle = getComputedStyle(document.body).getPropertyValue("--sea") || "#2b4a66";
      ctx.fillRect(0, 0, this.cw, this.ch);
      ctx.imageSmoothingEnabled = this.scale < 1;
      const sx = this.cx - this.cw / 2 / this.scale, sy = this.cy - this.ch / 2 / this.scale;
      ctx.drawImage(this.off, sx, sy, this.cw / this.scale, this.ch / this.scale, 0, 0, this.cw, this.ch);
      if (this.selMask) {
        const [x0, y0, mw, mh] = this.selBox;
        ctx.drawImage(this.selMask, (x0 - sx) * this.scale, (y0 - sy) * this.scale, mw * this.scale, mh * this.scale);
      }
    }
    select(id) {
      this.sel = id; this.selMask = null;
      if (id && this.g.bbox[id * 4] >= 0) {
        const b = this.g.bbox, x0 = b[id * 4], y0 = b[id * 4 + 1], x1 = b[id * 4 + 2], y1 = b[id * 4 + 3];
        const mw = x1 - x0 + 1, mh = y1 - y0 + 1;
        const c = document.createElement("canvas"); c.width = mw; c.height = mh;
        const cx = c.getContext("2d"); const im = cx.createImageData(mw, mh); const p = new Uint32Array(im.data.buffer);
        const { w, ids } = this.g;
        for (let y = 0; y < mh; y++) for (let x = 0; x < mw; x++) {
          const i = (y0 + y) * w + x0 + x;
          if (ids[i] !== id) continue;
          const edge = x === 0 || y === 0 || x === mw - 1 || y === mh - 1 || ids[i - 1] !== id || ids[i + 1] !== id || ids[i - w] !== id || ids[i + w] !== id;
          p[y * mw + x] = edge ? 0xffffffff : 0x55ffffff;
        }
        cx.putImageData(im, 0, 0);
        this.selMask = c; this.selBox = [x0, y0, mw, mh];
      }
      this.draw();
    }
    centerOn(id, zoom) {
      const b = this.g.bbox; if (!id || b[id * 4] < 0) return;
      this.cx = (b[id * 4] + b[id * 4 + 2]) / 2; this.cy = (b[id * 4 + 1] + b[id * 4 + 3]) / 2;
      if (zoom) this.scale = Math.max(this.scale, Math.min(2.5, 0.35 * Math.min(this.cw, this.ch) / Math.max(40, b[id * 4 + 2] - b[id * 4], b[id * 4 + 3] - b[id * 4 + 1])));
      this.draw();
    }
    bind() {
      const cv = this.cv;
      let drag = null, moved = false;
      cv.addEventListener("wheel", (e) => {
        e.preventDefault();
        const r = cv.getBoundingClientRect(), ex = e.clientX - r.left, ey = e.clientY - r.top;
        const [mx, my] = this.toMap(ex, ey);
        const f = Math.exp(-e.deltaY * 0.0015);
        this.scale = Math.min(12, Math.max(this.minScale, this.scale * f));
        this.cx = mx - (ex - this.cw / 2) / this.scale; this.cy = my - (ey - this.ch / 2) / this.scale;
        this.draw();
      }, { passive: false });
      cv.addEventListener("pointerdown", (e) => { drag = { x: e.clientX, y: e.clientY, cx: this.cx, cy: this.cy }; moved = false; cv.setPointerCapture(e.pointerId); });
      cv.addEventListener("pointermove", (e) => {
        const r = cv.getBoundingClientRect(), ex = e.clientX - r.left, ey = e.clientY - r.top;
        if (drag) {
          const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
          if (Math.abs(dx) + Math.abs(dy) > 3) moved = true;
          this.cx = drag.cx - dx / this.scale; this.cy = drag.cy - dy / this.scale; this.draw();
          this.tip.style.display = "none";
          return;
        }
        const id = this.idAt(...this.toMap(ex, ey));
        if (id !== this.hover) { this.hover = id; this.tip.innerHTML = id ? this.opts.tooltip(id) : ""; }
        this.tip.style.display = id && this.tip.innerHTML ? "block" : "none";
        const tx = Math.min(ex + 14, this.cw - this.tip.offsetWidth - 4), ty = Math.min(ey + 14, this.ch - this.tip.offsetHeight - 4);
        this.tip.style.left = tx + "px"; this.tip.style.top = ty + "px";
      });
      cv.addEventListener("pointerup", (e) => {
        const wasDrag = drag && moved; drag = null;
        if (wasDrag) { this.opts.onMove && this.opts.onMove(this.state()); return; }
        const r = cv.getBoundingClientRect();
        const id = this.idAt(...this.toMap(e.clientX - r.left, e.clientY - r.top));
        if (id) this.opts.onClick(id, e);
      });
      cv.addEventListener("pointerleave", () => { this.tip.style.display = "none"; this.hover = 0; });
    }
  }

  return { load, View, get geo() { return geo; } };
})();
