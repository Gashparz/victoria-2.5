Console Commands  (Victoria II 3.04 Steam, any mod: TGC, GFM, vanilla; Unofficial Patch 3.05 engine)
================================================================================
Console commands (single player)

  convert_pop <prov>                        owner's culture + state religion
  convert_pop <prov> <culture> [religion]   "-" keeps a value, "owner" uses the owner's
  convert_pop <prov> <culture> <religion> <filters...>   only the matching pops
        convert_pop 652 serb - 24594          pop 24594 becomes serb, religion kept
        convert_pop 652 serb - bulgarian farmers
  list_prov_cultures <prov>                 pop id | type | culture | size, plus totals
  list_prov_religions <prov>                pop id | type | religion | size, plus totals
  migrate_pop <from> <to> <filter> [...]    moves the pops of <from> matching ALL filters
        filter = culture, religion, pop type or pop id (from the list commands)
        migrate_pop 123 555 polish
        migrate_pop 123 555 polish catholic farmers
        migrate_pop 123 555 48211 48212

  migrate_all_pops <FROM> <TO> [filters]   all pops of TO's primary culture living in FROM
        go to TO's provinces, spread evenly by people (whole pops, biggest first)
        migrate_all_pops GER POL
        migrate_all_pops GER POL catholic farmers   only Polish Catholic farmers
        migrate_all_pops GER POL kashubian           another culture instead of POL's primary
        migrate_all_pops GER POL accepted            POL's primary + all POL's accepted cultures

  accept_culture <TAG> [cultures...]        adds accepted cultures (only the TAG: lists them)
  unaccept_culture <TAG> <cultures...>      removes accepted cultures
        accept_culture POL kashubian silesian

  double_pop <prov> [filters]               doubles the pops of a province (only the matching ones with filters)
  scale_pop <prov> <percent> [filters]      200 = double, 50 = half, 150 = +50%
        double_pop 652 serb       scale_pop 652 150 farmers
        (the game caps one pop at 2,000,000 people)

  scale_country_pop <TAG> <percent> [filters]   every province the country owns
        scale_country_pop SER 200 serb
  add_core <TAG> <prov> [more]              adds the country's core      add_core SER 652 653
  remove_core <TAG> <prov> [more]           removes it                   remove_core TUR 652

  bloc_add <name> <TAG> [more tags]         puts countries into a bloc (mega alliance)
        bloc_add central_powers GER AUS TUR BUL
  bloc_remove <TAG> [more]   bloc_list [name]   bloc_disband <name>
        When a bloc member enters a war (declares, is attacked, or joins as an ally), every other
        living member joins the same side. Stored as the country flag bloc_<name>, so it is saved
        in the save game. Normal alliances are untouched. Crisis and rebel wars are not affected.
        Panel: top bar tab COMENZI -> "Blocuri" button shows every bloc and its members.
  bloc_map [on|off]   the political map mode shows each bloc in its own colour (others grey);
        also the "Harta on/off" button in the bloc panel.

  Power blocs (Victoria 3 style):
  bloc_invite <bloc> <TAG>        the AI accepts or refuses (relations, religion, size, prestige, cohesion)
  bloc_identity <bloc> <militar|comercial|imperiu|religios>     bloc_leader <TAG>     bloc_info [bloc]
        militar    members join wars together; relations with the leader +2/month
        comercial  relations between all members +3/month
        imperiu    leader +0.5 prestige per member per month
        religios   each month one small pop (<= 3000) per member converts to the leader's religion
  Cohesion 0-100 (relations with the leader, size, shared religion, members at war with each other):
        below 20 (Fracturat) only the leader's wars pull the bloc in.
  The bloc panel shows one bloc per page: identity, leader, cohesion bar, effects, members table.
  Invite button: click a country on the map; next to the province window an "Invita <TAG> in bloc" button
        appears (when you lead a bloc). With the yesmen cheat on, the AI always accepts. The result shows in the
        bloc panel, which opens on your bloc.

Pop merging: the game merges a moving pop into any pop of the same type and religion,
ignoring culture (vanilla bug). migrate_pop uses a fixed check (same type, culture AND religion).
fix_merge_everywhere=1 in mod\TGCConvertPop.ini fixes it for the whole game too.

Province ids: console command "showprovinceid", then hover a province.
Names are script names (polish, catholic, farmers).

Top bar tab "COMENZI" (experimental)
  A tab next to Military opens a window with this command list (ESC or X closes it).
  Files: interface\zz_v2commands.gui and interface\zz_v2commands.gfx in the game folder (work with any mod).
  Move the tab: edit position = { x = 1090 y = 0 } of "v2cmd_tab" in zz_v2commands.gui.
  Turn it off: topbar_tab=0 in mod\TGCConvertPop.ini. The log shows "tab: ..." lines.

Requirements: The Grand Loader (dinput8.dll) in the game folder - see INSTALL.md.
Optional: Victoria 2 Unofficial Patch 3.05 (V2DLL); keep PLAYER_BUTTONS=0 for the top bar tab.

Logs: the_grand_loader.log next to v2game.exe.

Uninstall: delete mod\TGCConvertPop* and interface\zz_v2commands.*
Source, issues: see the project's GitHub page. License: MIT.
