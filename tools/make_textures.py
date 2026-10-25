# Builds the parchment textures for the TGCConvertPop windows from the game's own art.
# usage: python make_textures.py <game>/gfx/interface <out dir>
import sys, os
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
m = Image.open(os.path.join(src, "main_bg.dds")).convert("RGBA")
# gold frame of main_bg runs about x 76..1087, y 70..700; keep 12px of shadow around it
L, T, R, B = 64, 58, 1100, 713
crop = m.crop((L, T, R, B))
cw, ch = crop.size
# remove the vignette inside the frame so the stretched edges match the centre
import numpy as np
a = np.asarray(crop).astype(np.float64)
lum = a[..., :3].mean(axis=2)
yy, xx = np.mgrid[0:ch, 0:cw]
inner = (xx > 24) & (xx < cw - 24) & (yy > 24) & (yy < ch - 24)
fit = inner & ~(((xx < 80) | (xx > cw - 80)) & ((yy < 80) | (yy > ch - 80)))
X = np.stack([np.ones(fit.sum()), xx[fit], yy[fit], xx[fit] ** 2, yy[fit] ** 2, xx[fit] * yy[fit]], 1)
coef, *_ = np.linalg.lstsq(X, lum[fit], rcond=None)
full = coef[0] + coef[1] * xx + coef[2] * yy + coef[3] * xx ** 2 + coef[4] * yy ** 2 + coef[5] * xx * yy
target = full[ch // 2, cw // 2]
f = np.where(xx * 0 + 1, 1.0, 1.0)
# ramp the correction in from the frame so the gold border keeps its shading
dist = np.minimum(np.minimum(xx, cw - 1 - xx), np.minimum(yy, ch - 1 - yy))
w = np.clip((dist - 16) / 10.0, 0, 1)
f = 1 + w * (target / full - 1)
a[..., :3] = np.clip(a[..., :3] * f[..., None], 0, 255)
crop = Image.fromarray(a.astype(np.uint8), "RGBA")
C = 64          # corner size (holds the gold ornaments)
S = 320         # output texture size
mid = S - 2 * C
t = Image.new("RGBA", (S, S))
# corners
t.paste(crop.crop((0, 0, C, C)), (0, 0))
t.paste(crop.crop((cw - C, 0, cw, C)), (S - C, 0))
t.paste(crop.crop((0, ch - C, C, ch)), (0, S - C))
t.paste(crop.crop((cw - C, ch - C, cw, ch)), (S - C, S - C))
# edges, taken from the middle of each side
mx, my = cw // 2 - mid // 2, ch // 2 - mid // 2
t.paste(crop.crop((mx, 0, mx + mid, C)), (C, 0))
t.paste(crop.crop((mx, ch - C, mx + mid, ch)), (C, S - C))
t.paste(crop.crop((0, my, C, my + mid)), (0, C))
t.paste(crop.crop((cw - C, my, cw, my + mid)), (S - C, C))
# parchment centre
t.paste(crop.crop((mx, my, mx + mid, my + mid)), (C, C))
# inside the frame use one piece of centre parchment everywhere (keeps the gold corner ornaments)
ta = np.asarray(t).astype(np.float64)
pa = np.asarray(crop.crop((cw // 2 - S // 2, ch // 2 - S // 2, cw // 2 + S // 2, ch // 2 + S // 2))).astype(np.float64)
pm = pa[S // 4:3 * S // 4, S // 4:3 * S // 4, :3].reshape(-1, 3).mean(0)
ty, tx = np.mgrid[0:S, 0:S]
td = np.minimum(np.minimum(tx, S - 1 - tx), np.minimum(ty, S - 1 - ty))
corner = ((tx < C) | (tx > S - 1 - C)) & ((ty < C) | (ty > S - 1 - C))
diff = np.abs(ta[..., :3] - pm).sum(2)
ornament = corner & (diff > 60)
repl = (td >= 22) & ~ornament
ta[repl] = pa[repl]
t = Image.fromarray(ta.astype(np.uint8), "RGBA")
t.save(os.path.join(out, "v2cmd_frame.tga"))
d = Image.open(os.path.join(src, "diplomacy_bg.dds")).convert("RGBA")
d.crop((662, 101, 975, 118)).save(os.path.join(out, "v2cmd_header.tga"))
d.crop((662, 118, 975, 135)).save(os.path.join(out, "v2cmd_row.tga"))
print("ok", t.size)
