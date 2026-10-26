// ================= AI treaties: historical treaties, plausible AI-AI deals, AI requests to the player =================
// Historical treaties (about 80-85% of what happens) fire inside their real date window when the simulated
// world allows it (both countries exist, the giver still owns the provinces, no war between them). The rest
// are plausible deals generated from the map: border swaps of each other's people, cessions to a much stronger
// neighbour with a core and a majority, intra-bloc rearrangements and population exchanges after conquests.
// Both sides judge with the same rules as the player's treaty window (eval_provinces / eval_population).
// State lives in country flags, so it is saved with the game: htreaty_<id> (done), trdyn_<year> (last dynamic
// treaty), trreq_<year> (asked the player), trgrudge (the player refused).

struct HistTreaty {
    const char* id;
    int y0, y1;            // year window (inclusive)
    int kind;              // 0 cession FROM->TO, 1 swap (provs FROM->TO, back TO->FROM), 2 population exchange
    const char* from[4];   // giver candidates (first that owns a listed province)
    const char* to[3];     // receiver candidates (first alive)
    uint16_t provs[16];
    uint16_t back[8];
    uint16_t to_owns[4];   // optional: TO must own one of these (0 = no condition)
    int majority;          // 1: a province moves only if the receiver's nation is its majority (plebiscites)
    int monthly;           // chance per month inside the window, percent
    const char* title;
    const char* why;
};
const HistTreaty kHist[] = {
    {"aigun", 1857, 1862, 0, {"QNG", "CHI", "MCK"}, {"RUS"}, {1081, 1082, 1083, 1084, 1085}, {}, {}, 0, 12,
     "Treaties of Aigun and Peking", "China cedes the lands north of the Amur and east of the Ussuri to Russia."},
    {"danish_gold_coast", 1849, 1852, 0, {"DEN"}, {"ENG"}, {1907}, {}, {}, 0, 15,
     "Sale of the Danish Gold Coast", "Denmark sells its forts on the Gold Coast to Britain."},
    {"gadsden", 1852, 1856, 0, {"MEX"}, {"USA"}, {102, 2140}, {}, {103, 104, 101}, 0, 15,
     "Gadsden Purchase", "Mexico sells the Mesilla valley and southern Arizona to the United States."},
    {"turin", 1859, 1862, 0, {"SAR", "ITA"}, {"FRA"}, {472, 466}, {}, {}, 0, 15,
     "Treaty of Turin", "Sardinia cedes Savoy and Nice to France for its help against Austria."},
    {"ionian", 1863, 1866, 0, {"ION", "ENG"}, {"GRE"}, {826, 827}, {}, {}, 0, 15,
     "Union of the Ionian Islands", "Britain hands the Ionian Islands to Greece."},
    {"nicobar", 1867, 1870, 0, {"DEN"}, {"ENG"}, {3264}, {}, {}, 0, 12,
     "Sale of the Nicobar Islands", "Denmark sells the Nicobar Islands to Britain."},
    {"alaska", 1865, 1871, 0, {"RUS"}, {"USA"}, {1, 2, 3, 4, 5, 2599, 3317, 3390, 3391, 3395, 3408, 3409}, {}, {}, 0, 12,
     "Alaska Purchase", "Russia sells Alaska to the United States for 7.2 million dollars."},
    {"dutch_gold_coast", 1870, 1873, 0, {"NET"}, {"ENG"}, {1909}, {}, {}, 0, 15,
     "Anglo-Dutch Gold Coast Treaty", "The Netherlands cede their Gold Coast forts to Britain."},
    {"st_petersburg_1875", 1874, 1877, 1, {"JAP"}, {"RUS"}, {1086, 1087}, {1088, 1089}, {}, 0, 15,
     "Treaty of Saint Petersburg", "Japan gives up Sakhalin to Russia and receives the Kuril Islands."},
    {"cyprus", 1877, 1880, 0, {"TUR"}, {"ENG"}, {855, 856, 857}, {}, {}, 0, 15,
     "Cyprus Convention", "The Ottoman Empire hands Cyprus to British administration."},
    {"thessaly", 1880, 1883, 0, {"TUR"}, {"GRE"}, {831, 832, 825}, {}, {}, 0, 12,
     "Convention of Constantinople", "The Ottomans cede Thessaly and Arta to Greece under great power pressure."},
    {"heligoland", 1889, 1892, 0, {"ENG"}, {"GER", "PRU"}, {533}, {}, {}, 0, 15,
     "Heligoland-Zanzibar Treaty", "Britain gives Heligoland to Germany in exchange for German claims in East Africa."},
    {"kiautschou", 1897, 1900, 0, {"QNG", "CHI"}, {"GER", "PRU"}, {1566}, {}, {}, 0, 15,
     "Kiautschou Lease", "China leases Kiautschou Bay (Tsingtao) to Germany."},
    {"weihaiwei", 1897, 1900, 0, {"QNG", "CHI"}, {"ENG"}, {1569}, {}, {}, 0, 15,
     "Weihaiwei Lease", "China leases Weihaiwei to Britain."},
    {"port_arthur", 1897, 1900, 0, {"QNG", "CHI", "MCK"}, {"RUS"}, {1481}, {}, {}, 0, 15,
     "Port Arthur Lease", "China leases Port Arthur to Russia."},
    {"carolines", 1898, 1901, 0, {"SPA"}, {"GER", "PRU"}, {2521, 2518, 2519, 2579, 610}, {}, {}, 0, 15,
     "German-Spanish Treaty", "Spain sells the Caroline, Northern Mariana and Palau islands to Germany."},
    {"bul_tur_1913", 1913, 1915, 2, {"TUR"}, {"BUL"}, {}, {}, {}, 0, 15,
     "Ottoman-Bulgarian Convention", "Bulgaria and the Ottoman Empire exchange their minorities along the border."},
    {"danish_west_indies", 1916, 1918, 0, {"DEN"}, {"USA"}, {2223}, {}, {}, 0, 15,
     "Sale of the Danish West Indies", "Denmark sells its Caribbean islands to the United States."},
    {"schleswig", 1919, 1922, 0, {"GER", "PRU", "SWH"}, {"DEN"}, {371, 370}, {}, {}, 1, 20,
     "Schleswig Plebiscites", "Northern Schleswig votes to join Denmark; the south stays German."},
    {"gre_bul_1919", 1919, 1921, 2, {"BUL"}, {"GRE"}, {}, {}, {}, 0, 15,
     "Convention of Neuilly", "Greece and Bulgaria agree on a reciprocal emigration of their minorities."},
    {"lausanne", 1922, 1925, 2, {"TUR"}, {"GRE"}, {}, {}, {}, 0, 20,
     "Lausanne Population Exchange", "Greece and Turkey exchange their Orthodox and Muslim populations."},
    {"jubaland", 1924, 1926, 0, {"ENG"}, {"ITA"}, {1870}, {}, {}, 0, 15,
     "Cession of Jubaland", "Britain cedes Jubaland to Italian Somaliland."},
    {"saar", 1934, 1936, 0, {"FRA"}, {"GER"}, {573}, {}, {}, 1, 25,
     "Saar Plebiscite", "The Saar votes to return to Germany."},
};
constexpr int kNumHist = sizeof(kHist) / sizeof(kHist[0]);

struct GameDate { int year, month, day; };
GameDate game_date() {
    int d = (int)(world_date() / 24) - 1825000;
    GameDate g{d / 365, 1, 1};
    int doy = d % 365;
    static const int ml[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int m = 0;
    while (m < 11 && doy >= ml[m]) doy -= ml[m++];
    g.month = m + 1;
    g.day = doy + 1;
    return g;
}
int alive_index(const char* tag) {
    int i = country_index(tag);
    return i > 0 && country_alive(country_at(i)) ? i : -1;
}
// largest N in flags "<prefix>N" on a country (0 if none)
int flag_year(uintptr_t c, const char* prefix) {
    int best = 0;
    size_t pl = strlen(prefix);
    auto fn = [&](const char* name) {
        if (_strnicmp(name, prefix, pl) == 0) { int y = atoi(name + pl); if (y > best) best = y; }
    };
    flag_walk(rd<uintptr_t>(country_flags(c) + 4), fn, 0);
    return best;
}
void flag_set_year(uintptr_t c, const char* prefix, int year) {
    char f[48];
    snprintf(f, sizeof(f), "%s%d", prefix, year);
    flag_set(c, f, true);
}

// ---- history (for the toast and the overview window) ----
char g_tr_hist[8][200];
int g_tr_nhist = 0;
void* g_toast = nullptr;
int g_toast_ticks = 0;
bool g_toast_pending = false, g_req_pending = false;
alignas(8) uint8_t g_toast_glue[kGlueSize], g_req_acc_glue[kGlueSize], g_req_dec_glue[kGlueSize], g_req_close_glue[kGlueSize];
}  // namespace
extern "C" void tcp_toast_thunk();
extern "C" void tcp_req_acc_thunk();
extern "C" void tcp_req_dec_thunk();
namespace {
void toast(const char* text) {
    GameDate g = game_date();
    memmove(g_tr_hist[1], g_tr_hist[0], sizeof(g_tr_hist) - sizeof(g_tr_hist[0]));
    snprintf(g_tr_hist[0], sizeof(g_tr_hist[0]), "%d: %s", g.year, text);
    if (g_tr_nhist < 8) ++g_tr_nhist;
    logf("treaty news: %s", g_tr_hist[0]);
    g_toast_pending = true;  // shown from the frame hook (UI thread); this may run in the day tick
}
void toast_show() {
    g_toast_pending = false;
    if (!g_layer) return;
    if (!g_toast) {
        g_toast = gui_create("v2toast");
        if (g_toast) subscribe(g_toast, "v2toast_bg", g_toast_glue, &tcp_toast_thunk);
    }
    if (!g_toast) return;
    tb_set(g_toast, "v2toast_text", g_tr_hist[0]);
    win_show(g_toast, true);
    g_toast_ticks = 120;  // ~12 s of idler checks
}
extern "C" void __cdecl tcp_on_toast() { win_show(g_toast, false); g_toast_ticks = 0; }
void hist_fill(void* win) {
    for (int i = 0; i < 5; ++i) {
        char nm[24];
        snprintf(nm, sizeof(nm), "v2cmd_hist%d", i);
        tb_set(win, nm, i < g_tr_nhist ? g_tr_hist[i] : (i == 0 ? "No treaties signed yet this session." : " "));
    }
}

// ---- running a deal with the treaty window's machinery, without touching the player's draft ----
struct TrDraft {
    uint32_t partner, give[kTrMax], take[kTrMax];
    int ngive, ntake, popopt, npopcult;
    uintptr_t popcult[64];
    char status[240];
};
TrDraft g_draft_save;
void draft_push() {
    TrDraft& d = g_draft_save;
    d.partner = g_tr_partner; d.ngive = g_tr_ngive; d.ntake = g_tr_ntake; d.popopt = g_tr_popopt; d.npopcult = g_tr_npopcult;
    memcpy(d.give, g_tr_give, sizeof(d.give)); memcpy(d.take, g_tr_take, sizeof(d.take));
    memcpy(d.popcult, g_tr_popcult, sizeof(d.popcult));
    memcpy(d.status, g_tr_status, sizeof(d.status));
}
void draft_pop() {
    TrDraft& d = g_draft_save;
    g_tr_partner = d.partner; g_tr_ngive = d.ngive; g_tr_ntake = d.ntake; g_tr_popopt = d.popopt; g_tr_npopcult = d.npopcult;
    memcpy(g_tr_give, d.give, sizeof(d.give)); memcpy(g_tr_take, d.take, sizeof(d.take));
    memcpy(g_tr_popcult, d.popcult, sizeof(d.popcult));
    memcpy(g_tr_status, d.status, sizeof(d.status));
}
// Chance that `judge` accepts: proposer gives `give` and takes `take` (from judge).
Eval judge_provinces(uint32_t proposer, uint32_t judge, const uint32_t* give, int ng, const uint32_t* take, int nt) {
    draft_push();
    g_tr_ngive = std::min(ng, kTrMax); g_tr_ntake = std::min(nt, kTrMax);
    memcpy(g_tr_give, give, g_tr_ngive * 4); memcpy(g_tr_take, take, g_tr_ntake * 4);
    Eval e = eval_provinces(proposer, judge);
    draft_pop();
    return e;
}
PopEval judge_population(uint32_t proposer, uint32_t judge) {
    draft_push();
    g_tr_popopt = 0;
    PopEval pe = eval_population(proposer, judge);
    draft_pop();
    return pe;
}
int do_pop_swap(uint32_t a, uint32_t b) {
    if (!g_merge_fix_ok) return 0;
    draft_push();
    uintptr_t ca = country_at((int)a), cb = country_at((int)b);
    popcult_refresh(a, b);
    int n = 0;
    for (int k = 0; k < g_tr_npopcult; ++k) {
        uintptr_t c = g_tr_popcult[k];
        if (movable_to(ca, cb, c)) { run_migrate_all(b, a, culture_name(c)); ++n; }
        if (movable_to(cb, ca, c)) { run_migrate_all(a, b, culture_name(c)); ++n; }
    }
    draft_pop();
    return n;
}
int transfer_list(const uint32_t* l, int n, uint32_t to) {
    int moved = 0;
    for (int i = 0; i < n; ++i) moved += transfer_province(l[i], to);
    g_bloc_cache_dirty = true;
    bloc_map_redraw();
    return moved;
}
void list_names(const uint32_t* l, int n, char* out, size_t sz) {
    out[0] = 0;
    for (int i = 0; i < n && i < 4; ++i) {
        size_t k = strlen(out);
        snprintf(out + k, sz - k, "%s%s", i ? ", " : "", pn(l[i]));
    }
    if (n > 4) { size_t k = strlen(out); snprintf(out + k, sz - k, " and %d more", n - 4); }
}

// ---- requests to the player ----
struct Request {
    bool active;
    uint32_t ai;                // the other country
    int kind;                   // 0 provinces, 2 population exchange
    uint32_t player_gives[16], player_gets[16];
    int ng, nt;
    char title[80], why[220];
    int penalty;                // relations lost on refusal
    const char* hist;           // historical id or nullptr
    uint32_t expires;           // world date
};
Request g_req{};
void* g_reqwin = nullptr;
void req_show() {
    if (!g_reqwin) {
        g_reqwin = gui_create("v2req_window");
        if (!g_reqwin) return;
        subscribe(g_reqwin, "v2req_accept", g_req_acc_glue, &tcp_req_acc_thunk);
        subscribe(g_reqwin, "v2req_decline", g_req_dec_glue, &tcp_req_dec_thunk);
        subscribe(g_reqwin, "close_button", g_req_close_glue, &tcp_req_dec_thunk);  // closing = declining
    }
    char b[260], names[160];
    snprintf(b, sizeof(b), "Proposal from %s", cn(g_req.ai));
    tb_set(g_reqwin, "v2req_header", b);
    tb_set(g_reqwin, "v2req_sub", g_req.title);
    int n = 0;
    auto line = [&](const char* s) { char nm[16]; snprintf(nm, sizeof(nm), "v2req_l%d", n++); tb_set(g_reqwin, nm, s); };
    if (g_req.kind == 2) {
        PopEval pe = judge_population(g_req.ai, player_index());
        char a2[24], b2[24];
        fmt_people(pe.they_lose, a2, sizeof(a2));
        fmt_people(pe.they_get, b2, sizeof(b2));
        snprintf(b, sizeof(b), "An exchange of minorities: %s of your people come home, %s of theirs leave you.", a2, b2);
        line(b);
        line("Only cultures that belong to one side move; shared cultures stay.");
    } else {
        if (g_req.ng) { list_names(g_req.player_gives, g_req.ng, names, sizeof(names)); snprintf(b, sizeof(b), "You give:  %s", names); line(b); }
        if (g_req.nt) { list_names(g_req.player_gets, g_req.nt, names, sizeof(names)); snprintf(b, sizeof(b), "You receive:  %s", names); line(b); }
        if (!g_req.nt) line("They offer nothing in return.");
    }
    line(" ");
    line(g_req.why);
    snprintf(b, sizeof(b), "If you refuse: relations %s-%d" C_END "%s", C_RED, g_req.penalty,
             g_req.hist ? "." : ", and they will remember it.");
    line(g_req.penalty ? b : "If you refuse: nothing happens.");
    while (n < 7) line(" ");
    win_show(g_reqwin, true);
}
void req_open(const Request& r) {
    if (g_req.active) return;
    g_req = r;
    g_req.active = true;
    g_req.expires = world_date() + 30 * 24;
    flag_set_year(country_at((int)r.ai), "trreq_", game_date().year);
    logf("treaty request from %s: %s", cn(r.ai), r.title);
    g_req_pending = true;
}
void req_close(bool accepted) {
    if (!g_req.active) return;
    uint32_t me = player_index(), ai = g_req.ai;
    char b[200];
    if (accepted) {
        if (g_req.kind == 2) do_pop_swap(me, ai);
        else {
            transfer_list(g_req.player_gives, g_req.ng, ai);
            transfer_list(g_req.player_gets, g_req.nt, me);
        }
        relation_add(me, ai, 15);
        snprintf(b, sizeof(b), "%s: you accepted the proposal of %s.", g_req.title, cn(ai));
    } else {
        if (g_req.penalty) relation_add(me, ai, -g_req.penalty);
        if (!g_req.hist) flag_set(country_at((int)ai), "trgrudge", true);
        snprintf(b, sizeof(b), "%s: you refused %s.", g_req.title, cn(ai));
    }
    if (g_req.hist) {
        char f[48];
        snprintf(f, sizeof(f), "htreaty_%s", g_req.hist);
        flag_set(country_at((int)ai), f, true);
        flag_set(country_at((int)me), f, true);
    }
    g_req.active = false;
    win_show(g_reqwin, false);
    toast(b);
}
extern "C" void __cdecl tcp_on_req_acc() { req_close(true); }
extern "C" void __cdecl tcp_on_req_dec() { req_close(false); }

// ---- historical treaties ----
bool hist_done(const HistTreaty& h, uint32_t a, uint32_t b) {
    char f[48];
    snprintf(f, sizeof(f), "htreaty_%s", h.id);
    return has_flag(country_at((int)a), f) || has_flag(country_at((int)b), f);
}
void hist_mark(const HistTreaty& h, uint32_t a, uint32_t b) {
    char f[48];
    snprintf(f, sizeof(f), "htreaty_%s", h.id);
    flag_set(country_at((int)a), f, true);
    flag_set(country_at((int)b), f, true);
}
// Returns 1 when it fired (or a request to the player was opened).
int hist_try(const HistTreaty& h, int year) {
    if (year < h.y0 || year > h.y1) return 0;
    int to = -1;
    for (const char* t : h.to) if (t && to < 0) to = alive_index(t);
    if (to < 0) return 0;
    int from = -1;
    uint32_t give[16], back[8];
    int ng = 0, nb = 0;
    for (const char* t : h.from) {
        if (!t || from >= 0) continue;
        int f = alive_index(t);
        if (f < 0 || f == to) continue;
        if (h.kind == 2) { from = f; break; }
        uintptr_t fc = country_at(f);
        int cap = rd<int32_t>(fc + 0xBE4);
        for (uint16_t id : h.provs) {
            if (!id) break;
            uintptr_t p = get_province(id);
            if (!p || rd<uint32_t>(p + 0x12C) != (uint32_t)f || (int)id == cap) continue;
            if (h.majority && national_share(p, (uint32_t)to) < 0.5) continue;
            give[ng++] = id;
        }
        if (ng) from = f;
    }
    if (from < 0) return 0;
    if (hist_done(h, (uint32_t)from, (uint32_t)to)) return 0;
    if (at_war_with((uint32_t)from, (uint32_t)to)) return 0;
    if (h.to_owns[0]) {
        bool ok = false;
        for (uint16_t id : h.to_owns) if (id) { uintptr_t p = get_province(id); ok |= p && rd<uint32_t>(p + 0x12C) == (uint32_t)to; }
        if (!ok) return 0;
    }
    if (h.kind == 1)
        for (uint16_t id : h.back) {
            if (!id) break;
            uintptr_t p = get_province(id);
            if (p && rd<uint32_t>(p + 0x12C) == (uint32_t)to) back[nb++] = id;
        }
    if (relation((uint32_t)from, (uint32_t)to) < -150) return 0;  // bitter enemies do not sign
    if ((int)(rnd() % 100) >= h.monthly) return 0;
    uint32_t me = player_index();
    if (h.kind == 2) {
        PopEval pe = judge_population((uint32_t)to, (uint32_t)from);
        if (!pe.e.possible) { hist_mark(h, (uint32_t)from, (uint32_t)to); return 0; }
    }
    if ((uint32_t)from == me || (uint32_t)to == me) {
        if (g_req.active) return 0;
        Request r{};
        r.ai = (uint32_t)from == me ? (uint32_t)to : (uint32_t)from;
        r.kind = h.kind == 2 ? 2 : 0;
        if ((uint32_t)from == me) { memcpy(r.player_gives, give, ng * 4); r.ng = ng; memcpy(r.player_gets, back, nb * 4); r.nt = nb; }
        else { memcpy(r.player_gets, give, ng * 4); r.nt = ng; memcpy(r.player_gives, back, nb * 4); r.ng = nb; }
        snprintf(r.title, sizeof(r.title), "%s", h.title);
        snprintf(r.why, sizeof(r.why), "History: %s", h.why);
        r.penalty = (uint32_t)from == me && h.kind != 2 ? 20 : 0;
        r.hist = h.id;
        req_open(r);
        return 1;
    }
    char b[220];
    if (h.kind == 2) {
        do_pop_swap((uint32_t)to, (uint32_t)from);
        snprintf(b, sizeof(b), "%s: %s and %s exchange their minorities.", h.title, cn((uint32_t)from), cn((uint32_t)to));
    } else {
        transfer_list(give, ng, (uint32_t)to);
        if (nb) transfer_list(back, nb, (uint32_t)from);
        char names[160];
        list_names(give, ng, names, sizeof(names));
        snprintf(b, sizeof(b), "%s: %s cedes %s to %s.", h.title, cn((uint32_t)from), names, cn((uint32_t)to));
    }
    relation_add((uint32_t)from, (uint32_t)to, 10);
    hist_mark(h, (uint32_t)from, (uint32_t)to);
    toast(b);
    return 1;
}

// ---- plausible deals generated from the map ----
int g_dyn_years = 10;   // one AI-AI deal every ~N years worldwide (ini: treaty_ai_years)
int g_req_years = 6;    // one request to the player every ~N years at most (ini: treaty_request_years)
bool g_ai_treaties = true;
bool dyn_cooldown(uint32_t idx, int year, int years) {
    int y = flag_year(country_at((int)idx), "trdyn_");
    return y && year - y < years;
}
// Finds an irredenta of `owner`: a province (not capital) whose majority is the nation of another country
// that has a core on it, while `owner` has none. Returns the claimant or -1.
int find_irredenta(uint32_t owner, uint32_t* prov_out) {
    static uintptr_t provs[2048];
    int np = owned_provinces((int)owner, provs, 2048);
    if (np < 2) return -1;
    uintptr_t oc = country_at((int)owner);
    int cap = rd<int32_t>(oc + 0xBE4);
    int start = np ? (int)(rnd() % (uint32_t)np) : 0;
    for (int k = 0; k < np; ++k) {
        uintptr_t p = provs[(start + k) % np];
        uint32_t id = rd<uint32_t>(p + 0x58);
        if ((int)id == cap || province_has_core(p, owner)) continue;
        ProvStats st = prov_stats(p, 0);
        if (!st.top_culture || st.top_people * 2 < st.people || national_culture(oc, st.top_culture)) continue;
        uint32_t* b = rd<uint32_t*>(p + 0x148);
        uint32_t* e = rd<uint32_t*>(p + 0x14C);
        for (uint32_t* it = b; b && it < e; it += 2) {
            uint32_t c = it[1];
            if (c == owner || !country_alive(country_at((int)c))) continue;
            if (national_culture(country_at((int)c), st.top_culture)) { *prov_out = id; return (int)c; }
        }
    }
    return -1;
}
int dynamic_try(int year) {
    // pick a random living country that owns land
    static uint32_t list[1024];
    int n = 0;
    for_each_tag([&](uint32_t, uint32_t idx) {
        if (n < 1024 && country_alive(country_at((int)idx))) list[n++] = idx;
    });
    if (!n) return 0;
    uint32_t me = player_index();
    for (int tries = 0; tries < 12; ++tries) {
        uint32_t a = list[rnd() % (uint32_t)n];   // owner of the irredenta
        uint32_t p = 0;
        int bi = find_irredenta(a, &p);
        if (bi < 0) continue;
        uint32_t b = (uint32_t)bi;                // claimant
        if (at_war_with(a, b) || dyn_cooldown(a, year, 5) || dyn_cooldown(b, year, 5)) continue;
        if ((a == me || b == me) && (g_req.active || flag_year(country_at((int)(a == me ? b : a)), "trreq_") + g_req_years > year)) continue;
        // does the claimant hold land of a's people to give back? -> border swap
        uint32_t q = 0;
        int back = find_irredenta(b, &q);
        bool swap = back == (int)a;
        uintptr_t ca = country_at((int)a), cb = country_at((int)b);
        const char* ba = cached_bloc(a);
        const char* bb = cached_bloc(b);
        bool same_bloc = ba && bb && _stricmp(ba, bb) == 0;
        bool stronger = prestige_of(cb) >= 3 * std::max(prestige_of(ca), 1);
        // a conquered province full of b's people with no core of a: maybe a population exchange instead
        bool pop = !swap && !stronger && !same_bloc && (rnd() % 3 == 0);
        uint32_t give[1] = {q}, take[1] = {p};
        const char* kind;
        if (swap) kind = "Border rectification";
        else if (same_bloc) kind = "Bloc arrangement";
        else if (stronger) kind = "Cession under pressure";
        else if (pop) kind = "Population exchange";
        else continue;
        if (a == me || b == me) {  // the AI asks the player (or offers a swap)
            uint32_t ai = a == me ? b : a;
            Request r{};
            r.ai = ai;
            r.kind = pop ? 2 : 0;
            if (a == me) { r.player_gives[0] = p; r.ng = 1; if (swap) { r.player_gets[0] = q; r.nt = 1; } }
            else { r.player_gets[0] = p; r.nt = 1; if (swap) { r.player_gives[0] = q; r.ng = 1; } }
            if (a != me && !swap && !pop) continue;  // the AI does not give land away for nothing
            bool grudge = has_flag(country_at((int)ai), "trgrudge");
            snprintf(r.title, sizeof(r.title), "%s%s", kind, grudge ? " (asked again)" : "");
            ProvStats st = prov_stats(get_province(p), 0);
            snprintf(r.why, sizeof(r.why), "%s is mostly %s and a core of %s.%s", pn(p), culture_display(st.top_culture), cn(b),
                     same_bloc ? " Your bloc leader backs the request." : stronger ? " They are far stronger than you." : "");
            r.penalty = pop ? 5 : grudge ? 35 : 20;
            req_open(r);
            return 1;
        }
        char bf[220];
        if (pop) {
            PopEval pe = judge_population(b, a);
            if (!pe.e.possible || (int)(rnd() % 100) >= pe.e.chance) continue;
            do_pop_swap(b, a);
            snprintf(bf, sizeof(bf), "%s: %s and %s exchange their minorities.", kind, cn(a), cn(b));
        } else {
            Eval e = judge_provinces(b, a, give, swap ? 1 : 0, take, 1);
            if (!e.possible) continue;
            int chance = e.chance + (same_bloc ? 10 : 0);
            if ((int)(rnd() % 100) >= chance) { relation_add(a, b, -5); continue; }
            transfer_province(p, b);
            if (swap) transfer_province(q, a);
            g_bloc_cache_dirty = true;
            bloc_map_redraw();
            if (swap) snprintf(bf, sizeof(bf), "%s: %s gives %s to %s and receives %s.", kind, cn(a), pn(p), cn(b), pn(q));
            else snprintf(bf, sizeof(bf), "%s: %s cedes %s to %s.", kind, cn(a), pn(p), cn(b));
        }
        relation_add(a, b, 10);
        flag_set_year(ca, "trdyn_", year);
        flag_set_year(cb, "trdyn_", year);
        toast(bf);
        return 1;
    }
    return 0;
}
// An AI country with a core and the majority in one of the player's provinces asks for it.
bool player_request_try(int year, bool force) {
    {
        uint32_t me = player_index(), p = 0;
        int claimant = find_irredenta(me, &p);
        if (claimant > 0 && (force || flag_year(country_at(claimant), "trreq_") + g_req_years <= year) && !at_war_with(me, (uint32_t)claimant)) {
            uint32_t c = (uint32_t)claimant;
            const char* bm = cached_bloc(me);
            const char* bc = cached_bloc(c);
            bool same_bloc = bm && bc && _stricmp(bm, bc) == 0;
            bool stronger = prestige_of(country_at(claimant)) >= 2 * std::max(prestige_of(country_at((int)me)), 1);
            if (force || stronger || same_bloc || relation(me, c) > 50) {
                Request r{};
                r.ai = c;
                r.player_gives[0] = p;
                r.ng = 1;
                bool grudge = has_flag(country_at(claimant), "trgrudge");
                snprintf(r.title, sizeof(r.title), "%s%s", same_bloc ? "Bloc arrangement" : stronger ? "Cession under pressure" : "Friendly request",
                         grudge ? " (asked again)" : "");
                ProvStats st = prov_stats(get_province(p), 0);
                snprintf(r.why, sizeof(r.why), "%s is mostly %s and a core of %s; you have no core there.", pn(p), culture_display(st.top_culture), cn(c));
                r.penalty = grudge ? 35 : 20;
                req_open(r);
            }
        }
    }
    return g_req.active;
}
void ai_treaties_monthly() {
    if (!g_ai_treaties || !rd<uintptr_t>(R(VA_G_WORLD))) return;
    GameDate g = game_date();
    static bool logged = false;
    if (!logged) { logf("ai treaties: game date %d-%02d-%02d (raw %u)", g.year, g.month, g.day, world_date()); logged = true; }
    int fired = 0;
    for (int i = 0; i < kNumHist && fired < 2; ++i) fired += hist_try(kHist[i], g.year);
    // dynamic deals: about one every g_dyn_years years worldwide, at most one per month
    if (!fired && (int)(rnd() % (uint32_t)(12 * std::max(g_dyn_years, 1))) == 0) dynamic_try(g.year);
    // the player is asked separately, rarely
    else if (!fired && !g_req.active && (int)(rnd() % (uint32_t)(12 * std::max(g_req_years, 1))) == 0) player_request_try(g.year, false);
}
// per idler check (~10/s): toast timer, request expiry
void ai_treaties_frame() {
    if (g_toast_pending) toast_show();
    if (g_req_pending && g_req.active) { g_req_pending = false; req_show(); }
    if (g_toast_ticks > 0 && --g_toast_ticks == 0) win_show(g_toast, false);
    if (g_req.active && world_date() >= g_req.expires) {
        logf("treaty request expired: %s", g_req.title);
        req_close(false);
    }
}

}  // namespace
extern "C" ConsoleResult* __cdecl cmd_treaty_history(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    GameDate gd = game_date();
    if (arg_count(args) >= 1 && _stricmp(arg_text(args, 0), "request") == 0) {
        if (g_req.active) return reply(out, false, "A request is already open.");
        return player_request_try(gd.year, true) ? reply(out, true, "Request opened.")
                                                 : reply(out, false, "No country has a core and the majority in one of your provinces.");
    }
    Text t;
    t.add("Historical treaties (now %d-%02d):\n", gd.year, gd.month);
    for (int i = 0; i < kNumHist; ++i) {
        const HistTreaty& h = kHist[i];
        int to = -1, from = -1;
        for (const char* x : h.to) if (x && to < 0) to = alive_index(x);
        for (const char* x : h.from) if (x && from < 0) from = alive_index(x);
        const char* st;
        if (to >= 0 && from >= 0 && hist_done(h, (uint32_t)from, (uint32_t)to)) st = "done";
        else if (gd.year > h.y1) st = "missed";
        else if (gd.year < h.y0) st = "future";
        else if (to < 0 || from < 0) st = "countries missing";
        else st = "possible now";
        t.add("%d-%d %s: %s\n", h.y0, h.y1, h.title, st);
    }
    return finish(out, true, t);
}
namespace {
