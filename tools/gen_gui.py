# Generates zz_v2commands.gui/.gfx (the COMENZI tab and its window).
import sys, os
out = sys.argv[1] if len(sys.argv) > 1 else "."
W = 720
H_ = "vic_18"; L = "Arial12"
lines = [
 (H_, "Deschide consola cu tasta ~ si scrie comanda. Id provincie: showprovinceid, apoi mouse pe provincie."),
 None,
 (H_, "POPULATIE"),
 (L, "convert_pop <prov> [cultura|-|owner] [religie|-|owner] [filtre]     schimba cultura/religia"),
 (L, "list_prov_cultures <prov>   /   list_prov_religions <prov>      pop-uri: id | tip | cultura/religie | marime"),
 (L, "migrate_pop <din> <in> <filtre>        muta pop-urile potrivite      ex: migrate_pop 123 555 polish catholic"),
 (L, "migrate_all_pops <DIN> <SPRE> [accepted] [filtre]    cultura lui SPRE din tara DIN, impartita egal"),
 (L, "      ex: migrate_all_pops GER POL      migrate_all_pops GER POL accepted"),
 (L, "double_pop <prov> [filtre]      scale_pop <prov> <procent> [filtre]      (200 = dublu, 50 = jumatate)"),
 (L, "scale_country_pop <TAG> <procent> [filtre]     toata tara      ex: scale_country_pop SER 200 serb"),
 (L, "Filtru = cultura, religie, tip de pop (farmers) sau id de pop. Toate filtrele trebuie sa se potriveasca."),
 None,
 (H_, "CULTURI SI CORE-URI"),
 (L, "accept_culture <TAG> [culturi]     unaccept_culture <TAG> <culturi>      (doar TAG = lista)"),
 (L, "add_core <TAG> <prov> [alte prov]      remove_core <TAG> <prov> [alte prov]      ex: add_core SER 652 653"),
 None,
 (H_, "BLOCURI (mega aliante)"),
 (L, "bloc_add <nume> <TAG> [alte tag-uri]       ex: bloc_add puterile_centrale GER AUS TUR BUL"),
 (L, "bloc_remove <TAG>     bloc_list [nume]     bloc_disband <nume>"),
 (L, "Intr-un razboi, toti membrii vii intra de partea membrului implicat. Blocul se pastreaza in salvare."),
 None,
 (L, "Jurnal: the_grand_loader.log langa v2game.exe. Totalurile de populatie se actualizeaza a doua zi."),
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
			text = "COMENZI"
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
			text = "COMENZI"
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
			text = "consola"
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
		instantTextBoxType = {{
			name = "v2cmd_header"
			position = {{ x = 0 y = 16 }}
			font = "vic_22"
			borderSize = {{ x = 0 y = 0 }}
			maxWidth = {W}
			maxHeight = 30
			text = "Comenzi de consola"
			format = centre
			fixedsize = yes
		}}
''')
o += body
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
}}
''')
print("window height", H)
