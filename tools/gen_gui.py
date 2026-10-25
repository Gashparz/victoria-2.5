# Generates zz_v2commands.gui/.gfx (the COMMANDS tab, its window, the bloc panel and the invite button).
import sys, os
out = sys.argv[1] if len(sys.argv) > 1 else "."
W = 720
H_ = "vic_18"; L = "Arial12"
lines = [
 (H_, "Open the console with ~ and type a command. Province ids: showprovinceid, then hover a province."),
 None,
 (H_, "POPULATION"),
 (L, "convert_pop <prov> [culture|-|owner] [religion|-|owner] [filters]     change culture/religion"),
 (L, "list_prov_cultures <prov>   /   list_prov_religions <prov>      pops: id | type | culture/religion | size"),
 (L, "migrate_pop <from> <to> <filters>        move matching pops      e.g. migrate_pop 123 555 polish catholic"),
 (L, "migrate_all_pops <FROM> <TO> [accepted] [filters]    TO's culture living in FROM, spread evenly"),
 (L, "      e.g. migrate_all_pops GER POL      migrate_all_pops GER POL accepted"),
 (L, "double_pop <prov> [filters]      scale_pop <prov> <percent> [filters]      (200 = double, 50 = half)"),
 (L, "scale_country_pop <TAG> <percent> [filters]     whole country      e.g. scale_country_pop SER 200 serb"),
 (L, "Filter = culture, religion, pop type (farmers) or pop id. A pop must match every filter."),
 None,
 (H_, "CULTURES AND CORES"),
 (L, "accept_culture <TAG> [cultures]     unaccept_culture <TAG> <cultures>      (TAG alone = list)"),
 (L, "add_core <TAG> <prov> [more]      remove_core <TAG> <prov> [more]      e.g. add_core SER 652 653"),
 None,
 (H_, "POWER BLOCS (Victoria 3 style)"),
 (L, "bloc_add <name> <TAG> [more tags]  (forced)      bloc_invite <bloc> <TAG>  (the AI accepts or refuses)"),
 (L, "bloc_identity <bloc> military|trade|empire|religious      bloc_leader <TAG>      bloc_info [bloc]"),
 (L, "bloc_remove <TAG>     bloc_list     bloc_disband <name>     bloc_map [on|off]  (political map by bloc)"),
 (L, "In a war, members join the side of the member involved. The Blocs panel shows cohesion and identity effects."),
 (L, "Click a country on the map: an 'Invite ... to bloc' button appears next to the province window (leader only)."),
 None,
 (H_, "TREATIES"),
 (L, "Click a province, press 'Treaty: ...' next to the province window to offer it or ask for it, then open Treaties."),
 (L, "treaty <TAG> [give <ids>] [take <ids>]     treaty_propose     treaty_population [TAG]   (each side gets its own people)"),
 None,
 (L, "Log: the_grand_loader.log next to v2game.exe. Population totals refresh on the next game day."),
]
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
		}
		textBoxType = {
			name = "v2cmd_tab_shadow"
			position = { x = 1 y = 5 }
			font = "vic_18_black_bold"
			borderSize = { x = 0 y = 0 }
			text = "COMMANDS"
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
			text = "COMMANDS"
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
			text = "console"
			maxWidth = 124
			maxHeight = 16
			format = centre
			fixedsize = yes
		}
	}
''')
body = []
y = 58
for i, ln in enumerate(lines):
    if ln is None:
        y += 10; continue
    f, t = ln
    h = 22 if f == H_ else 18
    body.append(f'''		instantTextBoxType = {{
			name = "v2cmd_line{i}"
			position = {{ x = 30 y = {y} }}
			font = "{f}"
			borderSize = {{ x = 0 y = 0 }}
			maxWidth = {W-60}
			maxHeight = {h}
			text = "{t}"
			format = left
			fixedsize = yes
		}}
''')
    y += h + 2
H = y + 24
add(f'''
	# Window opened by the tab
	windowType = {{
		name = "v2cmd_window"
		backGround = "Background"
		position = {{ x = -{W//2} y = -{H//2} }}
		size = {{ x = {W} y = {H} }}
		moveable = 1
		dontRender = ""
		horizontalBorder = ""
		verticalBorder = ""
		fullScreen = no
		Orientation = "CENTER"

		guiButtonType = {{
			name = "Background"
			quadTextureSprite = "GFX_v2cmd_bg"
		}}
		guiButtonType = {{
			name = "close_button"
			quadTextureSprite = "GFX_main_close_button"
			position = {{ x = {W-44} y = 12 }}
			Orientation = "UPPER_LEFT"
			shortcut = "ESCAPE"
		}}
		guiButtonType = {{
			name = "v2cmd_blocs"
			quadTextureSprite = "GFX_button_standard_144"
			position = {{ x = 22 y = 12 }}
			buttonText = "Blocs"
			buttonFont = "vic_18_black"
			Orientation = "UPPER_LEFT"
		}}
		guiButtonType = {{
			name = "v2cmd_treaty"
			quadTextureSprite = "GFX_button_standard_144"
			position = {{ x = {W-200} y = 12 }}
			buttonText = "Treaties"
			buttonFont = "vic_18_black"
			Orientation = "UPPER_LEFT"
		}}
		instantTextBoxType = {{
			name = "v2cmd_header"
			position = {{ x = 0 y = 16 }}
			font = "vic_22"
			borderSize = {{ x = 0 y = 0 }}
			maxWidth = {W}
			maxHeight = 30
			text = "Console commands"
			format = centre
			fixedsize = yes
		}}
''')
o += body
add('\t}\n')
# ---- bloc panel (one bloc per page, filled by TGCConvertPop.dll)
ROWS = 16
COLS = [(30, 175), (210, 125), (340, 95), (440, 95), (540, 150)]
BH = 182 + (ROWS) * 18 + 100
def tbox(name, x, y, font, w, h, fmt="left"):
    return f"""		instantTextBoxType = {{
			name = "{name}"
			position = {{ x = {x} y = {y} }}
			font = "{font}"
			borderSize = {{ x = 0 y = 0 }}
			maxWidth = {w}
			maxHeight = {h}
			text = " "
			format = {fmt}
			fixedsize = yes
		}}
"""
def button(name, sprite, x, y, text):
    return f"""		guiButtonType = {{
			name = "{name}"
			quadTextureSprite = "{sprite}"
			position = {{ x = {x} y = {y} }}
			buttonText = "{text}"
			buttonFont = "vic_18_black"
			Orientation = "UPPER_LEFT"
		}}
"""
add(f"""
	# Bloc panel (filled by TGCConvertPop.dll)
	windowType = {{
		name = "v2bloc_window"
		backGround = "Background"
		position = {{ x = -{W//2} y = -{BH//2} }}
		size = {{ x = {W} y = {BH} }}
		moveable = 1
		dontRender = ""
		horizontalBorder = ""
		verticalBorder = ""
		fullScreen = no
		Orientation = "CENTER"

		guiButtonType = {{
			name = "Background"
			quadTextureSprite = "GFX_v2bloc_bg"
		}}
		guiButtonType = {{
			name = "close_button"
			quadTextureSprite = "GFX_main_close_button"
			position = {{ x = {W-44} y = 12 }}
			Orientation = "UPPER_LEFT"
			shortcut = "ESCAPE"
		}}
""")
add(button("v2bloc_back", "GFX_button_standard_144", 22, 12, "Commands"))
add(button("v2bloc_mapbtn", "GFX_button_standard_144", W - 200, 12, "Map on/off"))
add(tbox("v2bloc_header", 0, 16, "vic_22", W, 30, "centre"))
add(tbox("v2bloc_info1", 30, 52, "Arial12", W - 60, 18))
add(tbox("v2bloc_info2", 30, 72, "vic_18", W - 60, 22))
add(tbox("v2bloc_bar", 30, 96, "Arial12", W - 60, 18))
add(tbox("v2bloc_eff1", 30, 118, "Arial12", W - 60, 18))
add(tbox("v2bloc_eff2", 30, 136, "Arial12", W - 60, 18))
for r in range(ROWS + 1):
    y = 162 if r == 0 else 184 + (r - 1) * 18
    for c, (x, w) in enumerate(COLS):
        add(tbox(f"v2bloc_r{r}c{c}", x, y, "vic_18" if r == 0 else "Arial12", w, 22 if r == 0 else 18))
yb = 184 + ROWS * 18 + 10
add(button("v2bloc_prev", "GFX_button_standard_144", 22, yb, "< Previous"))
add(tbox("v2bloc_page", 0, yb + 8, "vic_18", W, 22, "centre"))
add(button("v2bloc_next", "GFX_button_standard_144", W - 22 - 144, yb, "Next >"))
add(tbox("v2bloc_mapstate", 30, yb + 44, "Arial12", W - 60, 18))
add('''	}

	# "Invite <TAG> to bloc" button, shown by TGCConvertPop.dll to the right of the province window when you
	# click a country on the map and you are in a bloc. Move it with position (from the lower left corner).
	windowType = {
		name = "v2bloc_diplo"
		backGround = ""
		position = { x = 386 y = -560 }
		size = { x = 180 y = 36 }
		moveable = 0
		dontRender = ""
		horizontalBorder = ""
		verticalBorder = ""
		fullScreen = no
		Orientation = "LOWER_LEFT"

		guiButtonType = {
			name = "v2bloc_invite"
			position = { x = 0 y = 0 }
			quadTextureSprite = "GFX_button_standard_177"
			buttonText = " "
			buttonFont = "vic_18_black"
			clicksound = click
		}
		instantTextBoxType = {
			name = "v2bloc_invite_text"
			position = { x = 4 y = 8 }
			font = "vic_18_black"
			borderSize = { x = 0 y = 0 }
			text = "Invite to bloc"
			maxWidth = 170
			maxHeight = 20
			format = centre
			fixedsize = yes
		}
''')
add('''	}

	# "Treaty: ..." button, shown by TGCConvertPop.dll below the bloc invite button when a province is selected.
	windowType = {
		name = "v2treaty_add"
		backGround = ""
		position = { x = 386 y = -520 }
		size = { x = 180 y = 36 }
		moveable = 0
		dontRender = ""
		horizontalBorder = ""
		verticalBorder = ""
		fullScreen = no
		Orientation = "LOWER_LEFT"

		guiButtonType = {
			name = "v2tr_addbtn"
			position = { x = 0 y = 0 }
			quadTextureSprite = "GFX_button_standard_177"
			buttonText = " "
			buttonFont = "vic_18_black"
			clicksound = click
		}
		instantTextBoxType = {
			name = "v2tr_addtext"
			position = { x = 4 y = 8 }
			font = "vic_18_black"
			borderSize = { x = 0 y = 0 }
			text = "Treaty"
			maxWidth = 170
			maxHeight = 20
			format = centre
			fixedsize = yes
		}
	}
''')
TL = 24
TH = 60 + TL * 18 + 90
add(f'''
	# Treaty window (filled by TGCConvertPop.dll)
	windowType = {{
		name = "v2treaty_window"
		backGround = "Background"
		position = {{ x = -{W//2} y = -{TH//2} }}
		size = {{ x = {W} y = {TH} }}
		moveable = 1
		dontRender = ""
		horizontalBorder = ""
		verticalBorder = ""
		fullScreen = no
		Orientation = "CENTER"

		guiButtonType = {{
			name = "Background"
			quadTextureSprite = "GFX_v2treaty_bg"
		}}
		guiButtonType = {{
			name = "close_button"
			quadTextureSprite = "GFX_main_close_button"
			position = {{ x = {W-44} y = 12 }}
			Orientation = "UPPER_LEFT"
			shortcut = "ESCAPE"
		}}
''')
add(tbox("v2tr_header", 0, 16, "vic_22", W, 30, "centre"))
for i in range(TL):
    add(tbox(f"v2tr_l{i}", 30, 56 + i * 18, "Arial12", W - 60, 18))
yb = 56 + TL * 18 + 8
add(tbox("v2tr_status", 30, yb, "vic_18", W - 60, 22))
add(button("v2tr_prov", "GFX_button_standard_144", 30, yb + 30, "Propose exchange"))
add(button("v2tr_pop", "GFX_button_standard_144", 200, yb + 30, "Population swap"))
add(button("v2tr_clear", "GFX_button_standard_144", W - 30 - 144, yb + 30, "Clear"))
add('\t}\n}\n')
open(os.path.join(out, 'zz_v2commands.gui'), 'w', encoding='cp1252', newline='\r\n').write(''.join(o))
open(os.path.join(out, 'zz_v2commands.gfx'), 'w', encoding='cp1252', newline='\r\n').write(f'''spriteTypes = {{
	# background of v2cmd_window (TGCConvertPop)
	corneredTileSpriteType = {{
		name = "GFX_v2cmd_bg"
		size = {{ x = {W} y = {H} }}
		textureFile = "gfx\\\\interface\\\\tiles_dialog_type2.tga"
		borderSize = {{ x = 94 y = 94 }}
	}}
	corneredTileSpriteType = {{
		name = "GFX_v2treaty_bg"
		size = {{ x = {W} y = {60 + 24 * 18 + 90} }}
		textureFile = "gfx\\\\interface\\\\tiles_dialog_type2.tga"
		borderSize = {{ x = 94 y = 94 }}
	}}
	corneredTileSpriteType = {{
		name = "GFX_v2bloc_bg"
		size = {{ x = {W} y = {BH} }}
		textureFile = "gfx\\\\interface\\\\tiles_dialog_type2.tga"
		borderSize = {{ x = 94 y = 94 }}
	}}
}}
''')
print("window height", H)
