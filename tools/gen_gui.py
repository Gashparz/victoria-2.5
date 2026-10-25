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
			text = "and treaties"
			maxWidth = 124
			maxHeight = 16
			format = centre
			fixedsize = yes
		}
	}
''')
tips["V2CMD_TT_TAB"] = "Power blocs, treaties and the console command reference."

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
y += 10
body.append(tbox("v2cmd_foot", 45, y, TXT, W - 90, 18, "left",
                 "Log: the_grand_loader.log next to v2game.exe. Population totals refresh on the next game day."))
H = y + 50
add(window_head("v2cmd_window", W, H, "GFX_v2cmd_bg", "v2cmd_header", "Diplomacy+"))
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
add(button("v2bloc_back", 35, 22, "Back", "Back to the Diplomacy+ overview.", sprite="GFX_button_standard_144"))
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

# ---------------- treaty window ----------------
TH = 612
add(window_head("v2treaty_window", W, TH, "GFX_v2treaty_bg", "v2tr_header"))
CW = (W - 80) // 2          # column width
cols = [("v2tr_g", 35), ("v2tr_t", 45 + CW)]
for pre, x in cols:
    add(deco(pre + "hb", bar(CW), x, 64))
    add(tbox(pre + "h", x + 8, 66, HEAD, CW - 16, 20))
    for r in range(8):
        y = 90 + r * 20
        add(deco(f"{pre}bg{r}", rowbg(CW), x, y))
        add(tbox(f"{pre}{r}", x + 8, y + 2, TXT, CW - 16, 18))
y = 90 + 8 * 20 + 8
xs = [35, 35 + (W - 70 - 144) // 3, 35 + 2 * (W - 70 - 144) // 3, W - 35 - 144]
add(button("v2tr_pick", xs[0], y, " ", "While on, every province you click on the map is added to (or removed from) the treaty."))
add(tbox("v2tr_pick_text", xs[0] + 4, y + 8, TXT, 136, 18, "centre"))
add(button("v2tr_mine", xs[1], y, "+ My people", "Ask for every province of theirs where your own culture is the majority."))
add(button("v2tr_theirs", xs[2], y, "+ Their people", "Offer every province of yours where their culture is the majority."))
add(button("v2tr_clear", xs[3], y, "Clear", "Empty both lists."))
# province exchange section
y += 46
add(deco("v2tr_phb", bar(W - 70), 35, y))
add(tbox("v2tr_ptitle", 45, y + 2, HEAD, 300, 20, "left", "PROVINCE EXCHANGE"))
add(tbox("v2tr_pchance", W - 245, y + 2, HEAD, 200, 20, "right"))
for i in range(5):
    add(tbox(f"v2tr_pw{i}", 45, y + 28 + i * 18, TXT, W - 260, 18))
add(button("v2tr_prov", W - 35 - 144, y + 34, "Propose", "Send the province exchange. A refusal costs 10 relations."))
y += 28 + 5 * 18 + 10
# population section
add(deco("v2tr_ohb", bar(W - 70), 35, y))
add(tbox("v2tr_otitle", 45, y + 2, HEAD, 300, 20, "left", "POPULATION SWAP"))
add(tbox("v2tr_popchance", W - 245, y + 2, HEAD, 200, 20, "right"))
add(tbox("v2tr_popsum", 45, y + 28, TXT, W - 260, 18))
for i in range(3):
    add(tbox(f"v2tr_ow{i}", 45, y + 48 + i * 18, TXT, W - 260, 18))
add(button("v2tr_popopt", W - 35 - 144, y + 28, " ", "Which pops move: both ways, only theirs, only yours, or a single culture (click to cycle)."))
add(tbox("v2tr_popopt_text", W - 35 - 140, y + 36, TXT, 136, 18, "centre"))
add(button("v2tr_pop", W - 35 - 144, y + 66, "Swap", "Each side receives its own people from the other. Shared cultures stay. A refusal costs 5 relations."))
y += 48 + 3 * 18 + 12
add(deco("v2tr_sbar", bar(W - 70, 24), 35, y))
add(tbox("v2tr_status", 45, y + 2, TXT, W - 90, 20))
assert y + 24 + 30 <= TH, y
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
