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
        Panel: top bar tab BLOCS -> "Power Blocs" shows every bloc and its members.
  bloc_map [on|off]   the political map mode shows each bloc in its own colour (others grey);
        also the "Map on/off" button in the bloc panel.

  Power blocs (Victoria 3 style):
  bloc_invite <bloc> <TAG>        the AI accepts or refuses (relations, religion, size, prestige, cohesion)
  bloc_identity <bloc> <military|trade|empire|religious>     bloc_leader <TAG>     bloc_info [bloc]
        military   members join wars together; relations with the leader +2/month
        trade      relations between all members +3/month
        empire     leader +0.5 prestige per member per month
        religious  each month one small pop (<= 3000) per member converts to the leader's religion
  Cohesion 0-100 (relations with the leader, size, shared religion, members at war with each other):
        below 20 (Fractured) only the leader's wars pull the bloc in.
  The bloc panel shows one bloc per page: identity, leader, cohesion bar, effects, members table.
  Without the console: open Diplomacy and select a country. A bar under the diplomacy window shows its bloc and
        relations, with "Propose treaty", "Invite to bloc" / "Form a bloc" (founds "<Your country> Pact" with you
        as leader, then invites) and "Power blocs". With the yesmen cheat on, the AI always accepts.
        The bloc panel also has Create/Leave, Identity, Disband and Bloc map buttons.

Treaties (province exchange, population exchange)
  Diplomacy -> select a country -> "Propose treaty". In the treaty window turn on "Pick on map" and click
  provinces (yours are offered, theirs are asked for), or use "+ My people" / "+ Their people".
  Population swap: only cultures that belong to one side move; cultures both countries count as their own stay.
  No treaty may move more than 30% of a country's people.
  The treaty window shows the AI's chance and every reason:
    province exchange    value of what each side gives (population, its own nation's share, cores),
                         -25 if it would give up land of its own nation, +15 if it gets its own people back,
                         -20 if nothing is offered, power difference, relations, religion, same bloc;
                         a capital is never given.
    population exchange  each side gets its own nationals (primary + accepted cultures) from the other,
                         like Greece and Turkey in 1923: net population, minority of another faith,
                         size of the minority, relations, same bloc.
  Building a treaty: "Pick on map" in the treaty window adds (or removes) every province you click;
        "+ My people" asks for all their provinces where your nation is the majority, "+ Their people"
        offers all yours where theirs is. The population option button cycles: both ways, send theirs home,
        bring yours home, or one culture only (e.g. only french).
  Refusals cost relations (-10 / -5); with the yesmen cheat on, the AI always accepts.
  Console: treaty <TAG> [give <ids>] [take <ids>], treaty_propose, treaty_population [TAG]

Pop merging: the game merges a moving pop into any pop of the same type and religion,
ignoring culture (vanilla bug). migrate_pop uses a fixed check (same type, culture AND religion).
fix_merge_everywhere=1 in mod\TGCConvertPop.ini fixes it for the whole game too.

Province ids: console command "showprovinceid", then hover a province.
Names are script names (polish, catholic, farmers).

Top bar tab "BLOCS" (experimental)
  A tab next to Military opens the Diplomacy+ overview: Power Blocs, Treaties and a command reference.
  Windows use the game's own parchment look; buttons have tooltips; countries, provinces and cultures
  are shown by name.
  Files in the game folder (work with any mod):
    interface\zz_v2commands.gui, interface\zz_v2commands.gfx
    gfx\interface\v2cmd_frame.tga, v2cmd_header.tga, v2cmd_row.tga   (made from main_bg / diplomacy_bg)
    localisation\zz_v2commands.csv   (tooltip texts)
  Move the tab: edit position = { x = 1090 y = 0 } of "v2cmd_tab" in zz_v2commands.gui.
  Turn it off: topbar_tab=0 in mod\TGCConvertPop.ini. The log shows "tab: ..." lines.

AI treaties
  About 80-85% of what happens is real history: the AI signs historical treaties in their real years when
  the world in your game allows it (both countries exist, the giver still owns the land, no war between them):
  Aigun/Peking 1858-60, Danish Gold Coast 1850, Gadsden 1853, Turin 1860 (Savoy, Nice), Ionian Islands 1864,
  Nicobar 1868, Alaska 1867, Dutch Gold Coast 1872, Saint Petersburg 1875 (Sakhalin <-> Kurils), Cyprus 1878,
  Thessaly 1881, Heligoland 1890, the 1898 leases (Kiautschou, Weihaiwei, Port Arthur), Carolines 1899,
  population exchanges 1913 / 1919 / 1923, Danish West Indies 1917, Schleswig 1920, Jubaland 1924, Saar 1935.
  The rest are plausible deals made from the map: border swaps (each side gets its own people back),
  cessions to a much stronger neighbour that has a core and the majority, arrangements inside a bloc,
  population exchanges after conquests. Both sides judge them with the same rules as your treaty window.
  When you are one of the parties, a window asks you: Accept or Decline (closing it = Decline; it expires
  after 30 days). Refusing costs relations and the country remembers it.
  News appear at the top of the screen and in the BLOCS tab under "Recent treaties".
  The treaties are data files: mod\TGCConvertPop\treaties\base.txt (always loaded, works with any mod) plus
  <mod>\treaties\*.txt of every ticked mod. "TGC Treaties" (mod\TGCTreaties) adds the treaties from TGC's own
  events with TGC's tags, provinces and state regions; tick it only together with TGC. A file loaded later
  replaces entries with the same id. The format is explained at the top of base.txt.
  Console: treaty_history  (status of every historical treaty)   treaty_history request  (an AI asks you now)
  ini: ai_treaties=0 turns it off; treaty_ai_years / treaty_request_years set how often.

What is installed in the game folder
  dinput8.dll           The Grand Loader (loads this plugin)
  lua51.dll             V2DLL from Victoria 2 Unofficial Patch 3.05 0.6.2.4
  lua51_real.dll        the game's original lua51.dll (V2DLL forwards to it)
  eventmusic.dll        part of the patch
  v2dll_settings.ini    patch settings (UI extras that need the patch's own files are off)
  Logs\                 v2dll.log
  mod\TGCConvertPop.*   this mod

Logs: the_grand_loader.log (commands) and Logs\v2dll.log (patch), next to v2game.exe.

Uninstall the patch only: delete lua51.dll, eventmusic.dll, v2dll_settings.ini,
then rename lua51_real.dll to lua51.dll.
Uninstall the commands: delete dinput8.dll and mod\TGCConvertPop*.
