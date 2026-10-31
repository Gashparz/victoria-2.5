# Generates zz_v2commands.gui / .gfx and localisation/zz_v2commands.csv for TGCConvertPop.dll.
# Native Victoria 2 look: parchment frame (v2cmd_frame.tga, built by make_textures.py from main_bg.dds),
# grey header bars and light table rows (from diplomacy_bg.dds), black fonts, tooltips on every button.
import sys, os
out = sys.argv[1] if len(sys.argv) > 1 else "."
W = 760
TITLE = "vic_22_black"; HEAD = "vic_18_black"; TXT = "Arial12_black"; BTN = "vic_18_black"
sprites = {}   # name -> (texture, w, h, border)
tips = {}      # loc key -> text
def frame(name, w, h):
    sprites[name] = ("v2cmd_frame.tga", w, h, 94)
    return name
def bar(w, h=22):
    n = f"GFX_v2cmd_hdr_{w}x{h}"; sprites[n] = ("v2cmd_header.tga", w, h, 3); return n
def rowbg(w, h=19):
    n = f"GFX_v2cmd_row_{w}x{h}"; sprites[n] = ("v2cmd_row.tga", w, h, 2); return n
def tbox(name, x, y, font, w, h, fmt="left", text=" "):
    return f"""		instantTextBoxType = {{
			name = "{name}"
			position = {{ x = {x} y = {y} }}
			font = "{font}"
			borderSize = {{ x = 0 y = 0 }}
			maxWidth = {w}
			maxHeight = {h}
			text = "{text}"
			format = {fmt}
			fixedsize = yes
		}}
"""
def deco(name, sprite, x, y):
    # background piece (a plain button, the same way the window backgrounds are drawn)
    return f"""		guiButtonType = {{
			name = "{name}"
			quadTextureSprite = "{sprite}"
			position = {{ x = {x} y = {y} }}
			Orientation = "UPPER_LEFT"
		}}
"""
def button(name, x, y, text, tip=None, sprite="GFX_button_standard_144", font=BTN):
    t = ""
    if tip:
        key = "V2CMD_TT_" + name.upper()
        tips[key] = tip
        t = f'\n\t\t\ttooltipText = "{key}"'
    return f"""		guiButtonType = {{
			name = "{name}"
			quadTextureSprite = "{sprite}"
			position = {{ x = {x} y = {y} }}
			buttonText = "{text}"
			buttonFont = "{font}"
			Orientation = "UPPER_LEFT"
			clicksound = click{t}
		}}
"""
def window_head(name, w, h, bgsprite, title_box=None, title=" "):
    s = f"""
	windowType = {{
		name = "{name}"
		backGround = "Background"
		position = {{ x = -{w//2} y = -{h//2} }}
		size = {{ x = {w} y = {h} }}
		moveable = 1
		dontRender = ""
		horizontalBorder = ""
		verticalBorder = ""
		fullScreen = no
		Orientation = "CENTER"

		guiButtonType = {{
			name = "Background"
			quadTextureSprite = "{frame(bgsprite, w, h)}"
		}}
		guiButtonType = {{
			name = "close_button"
			quadTextureSprite = "GFX_main_close_button"
			position = {{ x = {w-58} y = 22 }}
			Orientation = "UPPER_LEFT"
			shortcut = "ESCAPE"
		}}
"""
    if title_box:
        s += tbox(title_box, 0, 26, TITLE, w, 30, "centre", title)
    return s

o = []
add = o.append
add('guiTypes = {\n')
add('''	# Tab in the top bar (created by TGCConvertPop.dll next to Military). Move it by editing position.
	windowType = {
		name = "v2cmd_tab"
		backGround = ""
		position = { x = 1090 y = 0 }
		size = { x = 124 y = 80 }
		moveable = 0
		dontRender = ""
		horizontalBorder = ""
		verticalBorder = ""
		fullScreen = no
		Orientation = "UPPER_LEFT"

		guiButtonType = {
			name = "v2cmd_tab_btn"
			quadTextureSprite = "GFX_topbar_button_military"
			position = { x = 0 y = 5 }
			clicksound = click
			tooltipText = "V2CMD_TT_TAB"
		}
		textBoxType = {
			name = "v2cmd_tab_shadow"
			position = { x = 1 y = 5 }
			font = "vic_18_black_bold"
			borderSize = { x = 0 y = 0 }
			text = "BLOCS"
			maxWidth = 124
			maxHeight = 32
			format = centre
			fixedsize = yes
		}
		textBoxType = {
			name = "v2cmd_tab_text"
			position = { x = 0 y = 4 }
			font = "vic_18_headline"
			borderSize = { x = 0 y = 0 }
			text = "BLOCS"
			maxWidth = 124
			maxHeight = 32
			format = centre
			fixedsize = yes
		}
		instantTextBoxType = {
			name = "v2cmd_tab_sub"
			position = { x = 0 y = 40 }
			font = "Arial12"
			borderSize = { x = 0 y = 0 }
			text = "Victoria 2.5"
			maxWidth = 124
			maxHeight = 16
			format = centre
			fixedsize = yes
		}
	}
''')
tips["V2CMD_TT_TAB"] = "Victoria 2.5: power blocs, treaties and the console command reference."

# ---------------- hub / command reference ----------------
ref = [
 ("H", "WITHOUT THE CONSOLE"),
 ("T", "Blocs: found a bloc, set its identity, see members, cohesion and the bloc map mode."),
 ("T", "Diplomacy: select a country - under the diplomacy window: Propose treaty, Invite to bloc, Power blocs."),
 ("T", "Treaty window: turn on 'Pick on map' and click provinces, or add whole regions with '+ My people' / '+ Their people'."),
 ("H", "POPULATION  (console: press ~)"),
 ("T", "convert_pop <prov> [culture|-|owner] [religion|-|owner] [filters]       list_prov_cultures <prov>"),
 ("T", "migrate_pop <from> <to> <filters>        migrate_all_pops <FROM> <TO> [accepted] [filters]"),
 ("T", "double_pop <prov>      scale_pop <prov> <percent>      scale_country_pop <TAG> <percent> [filters]"),
 ("T", "Filters: culture, religion, pop type (farmers) or pop id. Province ids: showprovinceid, then hover."),
 ("H", "CULTURES AND CORES"),
 ("T", "accept_culture <TAG> [cultures]      unaccept_culture <TAG> <cultures>"),
 ("T", "add_core <TAG> <prov> [more]      remove_core <TAG> <prov> [more]"),
 ("H", "BLOCS AND TREATIES FROM THE CONSOLE"),
 ("T", "bloc_add <name> <TAG..> (forced)   bloc_invite <bloc> <TAG>   bloc_leader <TAG>   bloc_disband <name>"),
 ("T", "treaty <TAG> [give <ids>] [take <ids>]     treaty_propose     treaty_population [TAG]"),
]
y = 112
body = []
for i, (k, t) in enumerate(ref):
    if k == "H":
        y += 8
        body.append(deco(f"v2cmd_hb{i}", bar(W - 70), 35, y))
        body.append(tbox(f"v2cmd_line{i}", 45, y + 2, HEAD, W - 90, 20, "left", t))
        y += 26
    else:
        body.append(tbox(f"v2cmd_line{i}", 45, y, TXT, W - 90, 18, "left", t))
        y += 19
y += 8
body.append(deco("v2cmd_hhb", bar(W - 70), 35, y))
body.append(tbox("v2cmd_htitle", 45, y + 2, HEAD, W - 90, 20, "left", "RECENT TREATIES"))
y += 26
for i in range(5):
    body.append(tbox(f"v2cmd_hist{i}", 45, y, TXT, W - 90, 18))
    y += 19
y += 10
body.append(tbox("v2cmd_foot", 45, y, TXT, W - 90, 18, "left",
                 "Log: the_grand_loader.log next to v2game.exe. Population totals refresh on the next game day."))
H = y + 50
add(window_head("v2cmd_window", W, H, "GFX_v2cmd_bg", "v2cmd_header", "Victoria 2.5"))
add(button("v2cmd_blocs", 150, 66, "Power Blocs", "Open the power bloc panel: members, cohesion, identity, map mode.",
           sprite="GFX_button_standard_177"))
add(button("v2cmd_treaty", W - 150 - 177, 66, "Treaties", "Open the treaty window: exchange provinces or populations with another country.",
           sprite="GFX_button_standard_177"))
o += body
add('\t}\n')

# ---------------- bloc panel ----------------
ROWS = 14
COLS = [(45, 200), (250, 120), (375, 95), (475, 95), (575, 140)]
y0 = 176
BH = y0 + 22 + ROWS * 20 + 150
add(window_head("v2bloc_window", W, BH, "GFX_v2bloc_bg", "v2bloc_header"))
add(button("v2bloc_back", 35, 22, "Back", "Back to the Victoria 2.5 overview.", sprite="GFX_button_standard_144"))
add(tbox("v2bloc_info1", 45, 62, TXT, W - 90, 18))
add(tbox("v2bloc_info2", 45, 82, HEAD, W - 90, 22))
add(tbox("v2bloc_bar", 45, 106, TXT, W - 90, 18))
add(tbox("v2bloc_eff1", 45, 128, TXT, W - 90, 18))
add(tbox("v2bloc_eff2", 45, 146, TXT, W - 90, 18))
add(deco("v2bloc_hbar", bar(W - 70), 35, y0))
for r in range(ROWS + 1):
    yy = y0 + 2 if r == 0 else y0 + 24 + (r - 1) * 20
    if r:
        add(deco(f"v2bloc_rbg{r}", rowbg(W - 70), 35, yy - 2))
    for c, (x, w) in enumerate(COLS):
        add(tbox(f"v2bloc_r{r}c{c}", x, yy, HEAD if r == 0 else TXT, w, 20 if r == 0 else 18))
yb = y0 + 24 + ROWS * 20 + 8
add(button("v2bloc_prev", 35, yb, "< Previous", "Show the previous bloc."))
add(tbox("v2bloc_page", 0, yb + 8, HEAD, W, 22, "centre"))
add(button("v2bloc_next", W - 35 - 144, yb, "Next >", "Show the next bloc."))
yb2 = yb + 40
xs = [35, 35 + (W - 70 - 144) // 3, 35 + 2 * (W - 70 - 144) // 3, W - 35 - 144]
add(button("v2bloc_create", xs[0], yb2, " ", "Found a bloc led by you, or leave the bloc you are in (the most prestigious member takes over)."))
add(tbox("v2bloc_createtext", xs[0] + 4, yb2 + 8, TXT, 136, 18, "centre", "Create bloc"))
add(button("v2bloc_identity", xs[1], yb2, " ", "Leader only: change the bloc identity (Military, Trade, Empire, Religious). Each has its own monthly effects."))
add(tbox("v2bloc_idtext", xs[1] + 4, yb2 + 8, TXT, 136, 18, "centre", "Identity"))
add(button("v2bloc_disband", xs[2], yb2, "Disband", "Leader only: dissolve your bloc. Every member leaves."))
add(button("v2bloc_mapbtn", xs[3], yb2, " ", "Colour the political map by bloc (countries outside blocs turn grey)."))
add(tbox("v2bloc_maptext", xs[3] + 4, yb2 + 8, TXT, 136, 18, "centre", "Bloc map"))
add(tbox("v2bloc_mapstate", 45, yb2 + 44, TXT, W - 90, 18))
add(tbox("v2bloc_hint", 45, yb2 + 64, TXT, W - 90, 18, "left",
         "Tip: in Diplomacy, select a country and press 'Invite to bloc' under the diplomacy window."))
add('\t}\n')

# ---------------- action bar under the Diplomacy window ----------------
# The game's diplomacy window (country_diplomacy) is 1024x632 at x=-512 y=-250 from the screen centre.
# This bar sits right under it; move it with position if a mod resizes that window.
DBW, DBH = 560, 58
tips["V2CMD_TT_V2DIP_TREATY"] = "Exchange provinces or populations with this country. The AI weighs culture, cores, power and relations."
tips["V2CMD_TT_V2DIP_BLOC"] = "Invite this country into your power bloc (or found one). With yesmen on, the AI accepts."
tips["V2CMD_TT_V2DIP_BLOCS"] = "Open the power bloc panel."
add(f"""
	windowType = {{
		name = "v2dip_bar"
		backGround = ""
		position = {{ x = {512 - DBW} y = 384 }}
		size = {{ x = {DBW} y = {DBH} }}
		moveable = 0
		dontRender = ""
		horizontalBorder = ""
		verticalBorder = ""
		fullScreen = no
		Orientation = "CENTER"
""")
add(deco("v2dip_bg", bar(DBW, DBH), 0, 0))
add(tbox("v2dip_info", 12, 6, TXT, DBW - 24, 18))
def thin(name, x, y, text, label_box=None):
    key = "V2CMD_TT_" + name.upper()
    t = f'\n\t\t\ttooltipText = "{key}"' if key in tips else ""
    r = f"""		guiButtonType = {{
			name = "{name}"
			quadTextureSprite = "GFX_button_thin_160"
			position = {{ x = {x} y = {y} }}
			buttonText = "{text}"
			buttonFont = "vic_18_black"
			Orientation = "UPPER_LEFT"
			clicksound = click{t}
		}}
"""
    if label_box:
        r += tbox(label_box, x + 4, y + 4, TXT, 152, 18, "centre")
    return r
add(thin("v2dip_treaty", 12, 28, "Propose treaty"))
add(thin("v2dip_bloc", 200, 28, " ", "v2dip_bloctext"))
add(thin("v2dip_blocs", DBW - 172, 28, "Power blocs"))
add('\t}\n')

# ---------------- treaty window (Victoria 3 style: articles, acceptance, counter-offer) ----------------
TW3, TH = 900, 690
add(window_head("v2treaty_window", TW3, TH, "GFX_v2treaty_bg", "v2tr_header"))
add(tbox("v2t3_sub", 0, 52, HEAD, TW3, 20, "centre"))
CW = (TW3 - 90) // 2
cols = [("l", 35), ("r", 55 + CW)]
for side, x in cols:
    add(deco(f"v2t3_{side}hb", bar(CW), x, 80))
    add(tbox(f"v2t3_{side}head", x + 8, 82, HEAD, CW - 16, 20))
    for r in range(8):
        y = 106 + r * 22
        add(deco(f"v2t3_{side}bg{r}", rowbg(CW, 21), x, y))
        add(tbox(f"v2t3_{side}{r}", x + 8, y + 2, TXT, CW - 80, 18))
        add(tbox(f"v2t3_{side}s{r}", x + CW - 72, y + 2, TXT, 40, 18, "right"))
        key = f"V2CMD_TT_V2T3_{side.upper()}X{r}"
        tips[key] = "Remove this article."
        add(f"""		guiButtonType = {{
			name = "v2t3_{side}x{r}"
			quadTextureSprite = "GFX_close_province"
			position = {{ x = {x + CW - 26} y = {y + 1} }}
			Orientation = "UPPER_LEFT"
			clicksound = click
			tooltipText = "{key}"
		}}
""")
yb = 106 + 8 * 22 + 6
# add-article buttons under each column (thin buttons, 2 per row)
def thinb(name, x, y, text, tip, label=None, w=None):
    key = "V2CMD_TT_" + name.upper(); tips[key] = tip
    sprite = "GFX_button_thin_160" if not w else w
    r = f"""		guiButtonType = {{
			name = "{name}"
			quadTextureSprite = "{sprite}"
			position = {{ x = {x} y = {y} }}
			buttonText = "{text}"
			buttonFont = "vic_18_black"
			Orientation = "UPPER_LEFT"
			clicksound = click
			tooltipText = "{key}"
		}}
"""
    if label: r += tbox(label, x + 4, y + 4, TXT, 152, 18, "centre")
    return r
lx, rx = 35, 55 + CW
add(thinb("v2tr_theirs", lx, yb, "+ Lands of their people", "Offer every province of yours where their people are the majority."))
add(thinb("v2t3_lmoney", lx + 170, yb, "+ Pay 10,000", "Offer money (each click adds 10,000). Money makes almost any deal acceptable."))
add(thinb("v2t3_lpop", lx, yb + 28, " ", "Article: their people living in your country go home to them.", "v2t3_lpop_text"))
add(thinb("v2t3_safe", lx + 170, yb + 28, " ", "Pay for ships, trains and relief: nobody dies on the way (+10 acceptance). Without it about 10% of the people who move die.", "v2t3_safe_text"))
add(thinb("v2tr_mine", rx, yb, "+ Lands of my people", "Ask for every province of theirs where your people are the majority."))
add(thinb("v2t3_rmoney", rx + 170, yb, "+ Demand 10,000", "Ask them for money (each click adds 10,000)."))
add(thinb("v2t3_rpop", rx, yb + 28, " ", "Article: your people living in their country come home to you.", "v2t3_rpop_text"))
add(thinb("v2tr_pick", rx + 170, yb + 28, " ", "While on, every province you click on the map is added to (or removed from) the treaty: yours on the left, theirs on the right.", "v2tr_pick_text"))
# acceptance panel
ya = yb + 66
add(deco("v2t3_ahb", bar(TW3 - 70, 24), 35, ya))
add(tbox("v2t3_accept", 45, ya + 3, HEAD, TW3 - 90, 20, "centre"))
add(tbox("v2t3_bar", 0, ya + 30, HEAD, TW3, 20, "centre"))
add(tbox("v2t3_scale", 0, ya + 50, TXT, TW3, 18, "centre"))
for i in range(8):
    col, row = i % 2, i // 2
    add(tbox(f"v2t3_why{i}", 45 + col * (TW3 // 2 - 20), ya + 74 + row * 19, TXT, TW3 // 2 - 50, 18))
yc = ya + 74 + 4 * 19 + 8
add(tbox("v2t3_countertext", 45, yc + 8, HEAD, TW3 - 300, 20))
add(button("v2t3_counter", TW3 - 35 - 177, yc, "Accept their terms", "Add the money they ask for to your side.", sprite="GFX_button_standard_177"))
yd = yc + 42
add(deco("v2tr_sbar", bar(TW3 - 70, 24), 35, yd))
add(tbox("v2tr_status", 45, yd + 3, TXT, TW3 - 90, 20))
ye = yd + 34
add(tbox("v2t3_money", 45, ye + 8, TXT, 250, 18))
add(button("v2t3_clear", TW3 // 2 - 160, ye, "Clear", "Remove every article."))
add(button("v2t3_propose", TW3 - 35 - 200, ye, "Propose treaty", "Send the treaty. They answer at once. A refusal costs 10 relations.", sprite="GFX_button_standard_200"))
assert ye + 40 <= TH, ye
add('\t}\n')
# ---------------- treaty news toast ----------------
TW, THt = 640, 34
add(f"""
	windowType = {{
		name = "v2toast"
		backGround = ""
		position = {{ x = -{TW//2} y = -380 }}
		size = {{ x = {TW} y = {THt} }}
		moveable = 0
		dontRender = ""
		horizontalBorder = ""
		verticalBorder = ""
		fullScreen = no
		Orientation = "CENTER"
""")
tips["V2CMD_TT_V2TOAST_BG"] = "Treaty news. Click to dismiss; the last ones are listed in the BLOCS tab."
add(f"""		guiButtonType = {{
			name = "v2toast_bg"
			quadTextureSprite = "{bar(TW, THt)}"
			position = {{ x = 0 y = 0 }}
			Orientation = "UPPER_LEFT"
			tooltipText = "V2CMD_TT_V2TOAST_BG"
		}}
""")
add(tbox("v2toast_text", 12, 8, HEAD, TW - 24, 20, "centre"))
add('\t}\n')
# ---------------- request from an AI country ----------------
RW, RH = 680, 330
add(window_head("v2req_window", RW, RH, "GFX_v2req_bg", "v2req_header"))
add(deco("v2req_sbar", bar(RW - 70), 35, 64))
add(tbox("v2req_sub", 45, 66, HEAD, RW - 90, 20, "left"))
for i in range(7):
    add(tbox(f"v2req_l{i}", 45, 96 + i * 20, TXT, RW - 90, 18))
add(button("v2req_accept", RW // 2 - 160, RH - 70, "Accept", "Sign the treaty. Relations +15."))
add(button("v2req_decline", RW // 2 + 16, RH - 70, "Decline", "Refuse. See the last line for the consequences."))
add('\t}\n}\n')

open(os.path.join(out, 'zz_v2commands.gui'), 'w', encoding='cp1252', newline='\r\n').write(''.join(o))
g = ['spriteTypes = {\n\t# TGCConvertPop windows (textures in gfx/interface/v2cmd_*.tga)\n']
for n, (tex, w, h, b) in sprites.items():
    g.append(f'''	corneredTileSpriteType = {{
		name = "{n}"
		size = {{ x = {w} y = {h} }}
		textureFile = "gfx\\\\interface\\\\{tex}"
		borderSize = {{ x = {b} y = {b} }}
	}}
''')
g.append('}\n')
open(os.path.join(out, 'zz_v2commands.gfx'), 'w', encoding='cp1252', newline='\r\n').write(''.join(g))
loc = ''.join(f'{k};{v.replace(";", ",")};;;;;;;;;;;;x\n' for k, v in tips.items())
open(os.path.join(out, 'zz_v2commands.csv'), 'w', encoding='cp1252', newline='\r\n').write(loc)
print("hub", H, "bloc", BH, "treaty", TH, "sprites", len(sprites), "tips", len(tips))
