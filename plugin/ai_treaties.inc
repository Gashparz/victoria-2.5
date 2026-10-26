// ================= AI treaties: historical treaties, plausible AI-AI deals, AI requests to the player =================
// Historical treaties (about 80-85% of what happens) fire inside their real date window when the simulated
// world allows it (both countries exist, the giver still owns the provinces, no war between them). The rest
// are plausible deals generated from the map: border swaps of each other's people, cessions to a much stronger
// neighbour with a core and a majority, intra-bloc rearrangements and population exchanges after conquests.
// Both sides judge with the same rules as the player's treaty window (eval_provinces / eval_population).
// State lives in country flags, so it is saved with the game: htreaty_<id> (done), trdyn_<year> (last dynamic
// treaty), trreq_<year> (asked the player), trgrudge (the player refused).

// Treaty data is read from text files: mod\TGCConvertPop\treaties\*.txt (always) and <mod path>\treaties\*.txt
// of every mod on the command line (e.g. the "TGC Treaties" mod). Same id later = replaces the earlier entry.
struct HistTreaty {
    std::string id, title, why, source;
    int y0 = 0, y1 = 0, kind = 0, majority = 0, monthly = 15, mtth = 0, accept = 100;
    std::vector<std::string> from, to, cores_of, back_cores_of, native;
    bool native_found = false;   // the game's own decision/event exists: it fires with its own conditions
    std::vector<uint32_t> provs, back, to_owns;   // regions already expanded into ids
};
std::vector<HistTreaty> g_hist;
std::vector<std::string> g_treaty_files;
// state regions of the active map: name -> province ids
std::vector<std::pair<std::string, std::vector<uint32_t>>> g_regions;

struct Tok { std::vector<std::string> t; size_t i = 0; bool eof() const { return i >= t.size(); } const std::string& next() { static std::string e; return i < t.size() ? t[i++] : e; } const std::string& peek() { static std::string e; return i < t.size() ? t[i] : e; } };
bool read_file(const char* path, std::string& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}
void tokenize(const std::string& s, Tok& tk) {
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (c == '#') { while (i < n && s[i] != '\n') ++i; continue; }
        if (isspace((unsigned char)c)) { ++i; continue; }
        if (c == '{' || c == '}' || c == '=') { tk.t.push_back(std::string(1, c)); ++i; continue; }
        if (c == '"') {
            size_t j = s.find('"', i + 1);
            if (j == std::string::npos) j = n;
            tk.t.push_back(s.substr(i + 1, j - i - 1));
            i = j + 1;
            continue;
        }
        size_t j = i;
        while (j < n && !isspace((unsigned char)s[j]) && s[j] != '{' && s[j] != '}' && s[j] != '=' && s[j] != '#') ++j;
        tk.t.push_back(s.substr(i, j - i));
        i = j;
    }
}
std::vector<std::string> read_list(Tok& tk) {  // after "=": either { a b c } or a single word
    std::vector<std::string> v;
    if (tk.peek() != "{") { v.push_back(tk.next()); return v; }
    tk.next();
    while (!tk.eof() && tk.peek() != "}") v.push_back(tk.next());
    tk.next();
    return v;
}
void skip_value(Tok& tk) {
    if (tk.peek() != "{") { tk.next(); return; }
    int d = 0;
    do { const std::string& x = tk.next(); if (x == "{") ++d; else if (x == "}") --d; } while (d > 0 && !tk.eof());
}
void load_regions(const char* path) {
    std::string s;
    if (!read_file(path, s)) return;
    g_regions.clear();
    Tok tk;
    tokenize(s, tk);
    while (!tk.eof()) {
        std::string name = tk.next();
        if (tk.peek() != "=") continue;
        tk.next();
        if (tk.peek() != "{") { tk.next(); continue; }
        std::vector<uint32_t> ids;
        for (auto& x : read_list(tk)) if (isdigit((unsigned char)x[0])) ids.push_back((uint32_t)atoi(x.c_str()));
        g_regions.push_back({name, ids});
    }
    logf("treaties: %d state regions from %s", (int)g_regions.size(), path);
}
void add_ids(std::vector<uint32_t>& to, const std::vector<std::string>& l, bool regions, int* missing) {
    for (auto& x : l) {
        if (!regions) { if (isdigit((unsigned char)x[0])) to.push_back((uint32_t)atoi(x.c_str())); continue; }
        bool found = false;
        for (auto& r : g_regions)
            if (_stricmp(r.first.c_str(), x.c_str()) == 0) { to.insert(to.end(), r.second.begin(), r.second.end()); found = true; break; }
        if (!found && missing) ++*missing;
    }
}
int load_treaty_file(const char* path) {
    std::string s;
    if (!read_file(path, s)) return -1;
    Tok tk;
    tokenize(s, tk);
    int n = 0, missing = 0;
    while (!tk.eof()) {
        std::string key = tk.next();
        if (tk.peek() != "=") continue;
        tk.next();
        if (key != "treaty") { skip_value(tk); continue; }
        if (tk.next() != "{") continue;
        HistTreaty h;
        while (!tk.eof() && tk.peek() != "}") {
            std::string k = tk.next();
            if (tk.peek() != "=") continue;
            tk.next();
            std::vector<std::string> v = read_list(tk);
            std::string one = v.empty() ? "" : v[0];
            if (k == "id") h.id = one;
            else if (k == "title") h.title = one;
            else if (k == "why") h.why = one;
            else if (k == "source") h.source = one;
            else if (k == "years" && v.size() >= 2) { h.y0 = atoi(v[0].c_str()); h.y1 = atoi(v[1].c_str()); }
            else if (k == "kind") h.kind = one == "swap" ? 1 : one == "population" ? 2 : 0;
            else if (k == "from") h.from = v;
            else if (k == "to") h.to = v;
            else if (k == "provinces") add_ids(h.provs, v, false, nullptr);
            else if (k == "regions") add_ids(h.provs, v, true, &missing);
            else if (k == "cores_of") h.cores_of = v;
            else if (k == "back") add_ids(h.back, v, false, nullptr);
            else if (k == "back_regions") add_ids(h.back, v, true, &missing);
            else if (k == "back_cores_of") h.back_cores_of = v;
            else if (k == "to_owns") add_ids(h.to_owns, v, false, nullptr);
            else if (k == "majority") h.majority = one == "yes" || one == "1";
            else if (k == "chance") h.monthly = atoi(one.c_str());
            else if (k == "mtth") h.mtth = atoi(one.c_str());
            else if (k == "accept") h.accept = atoi(one.c_str());
            else if (k == "native") h.native = v;
        }
        tk.next();
        if (h.id.empty() || h.from.empty() || h.to.empty() || !h.y0) continue;
        if (h.title.empty()) h.title = h.id;
        bool replaced = false;
        for (auto& o : g_hist) if (o.id == h.id) { o = h; replaced = true; }
        if (!replaced) g_hist.push_back(h);
        ++n;
    }
    logf("treaties: %d from %s%s", n, path, missing ? " (some regions not on this map: skipped)" : "");
    g_treaty_files.push_back(path);
    return n;
}
void load_dir(const std::string& dir) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.txt").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::string> names;
    do names.push_back(fd.cFileName); while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    for (auto& n : names) load_treaty_file((dir + "\\" + n).c_str());
}
// mod paths from the command line, in order: -mod=mod/TGC.mod -> "mod/TGC"
std::vector<std::string> mod_paths() {
    std::vector<std::string> out;
    const char* cl = GetCommandLineA();
    for (const char* p = strstr(cl, "-mod="); p; p = strstr(p + 5, "-mod=")) {
        const char* e = strstr(p + 5, ".mod");
        if (!e) break;
        std::string file(p + 5, e + 4);
        std::string s;
        if (!read_file(file.c_str(), s)) continue;
        Tok tk;
        tokenize(s, tk);
        while (!tk.eof()) {
            std::string k = tk.next();
            if (k == "path" && tk.peek() == "=") { tk.next(); out.push_back(tk.next()); break; }
        }
    }
    return out;
}
void load_treaties() {
    g_hist.clear();
    g_treaty_files.clear();
    std::vector<std::string> mods = mod_paths();
    std::string region = "map\\region.txt";
    for (auto& m : mods) {
        std::string r = m + "\\map\\region.txt";
        if (GetFileAttributesA(r.c_str()) != INVALID_FILE_ATTRIBUTES) region = r;
    }
    load_regions(region.c_str());
    load_dir("mod\\TGCConvertPop\\treaties");
    for (auto& m : mods) if (_stricmp(m.c_str(), "mod/TGCConvertPop") != 0) load_dir(m + "\\treaties");
    // "native = { decisions/ENG.txt:webster_ashburton_treaty events/RUSFlavor.txt:32509 }": if the active game
    // files contain that decision or event, the game runs the treaty itself (same conditions, same odds);
    // the plugin only recognises it when it happens.
    int nn = 0;
    for (auto& h : g_hist) {
        for (auto& ref : h.native) {
            size_t c = ref.rfind(':');
            if (c == std::string::npos || h.native_found) continue;
            std::string rel = ref.substr(0, c), what = ref.substr(c + 1), text;
            for (auto& r : rel) if (r == '/') r = '\\';
            bool got = false;
            for (int i = (int)mods.size() - 1; i >= 0 && !got; --i) got = read_file((mods[i] + "\\" + rel).c_str(), text);
            if (!got) got = read_file(rel.c_str(), text);
            if (!got) continue;
            bool id = !what.empty() && isdigit((unsigned char)what[0]);
            size_t pos = 0;
            while ((pos = text.find(what, pos)) != std::string::npos) {
                bool ok;
                if (id) {  // "id = 32509"
                    size_t q = pos;
                    while (q > 0 && (text[q - 1] == ' ' || text[q - 1] == '\t' || text[q - 1] == '=')) --q;
                    ok = q >= 2 && text.compare(q - 2, 2, "id") == 0 && !isdigit((unsigned char)text[pos + what.size()]);
                } else ok = (pos == 0 || !(isalnum((unsigned char)text[pos - 1]) || text[pos - 1] == '_')) &&
                            !(isalnum((unsigned char)text[pos + what.size()]) || text[pos + what.size()] == '_');
                if (ok) { h.native_found = true; break; }
                pos += what.size();
            }
        }
        nn += h.native_found;
    }
    logf("treaties: %d historical treaties loaded, %d run by the game's own decisions/events", (int)g_hist.size(), nn);
}
// ---- culture groups (common/cultures.txt of the active mod) ----
std::vector<std::pair<std::string, int>> g_culture_group;   // culture name -> group index
std::vector<std::string> g_group_names;
std::vector<std::pair<uintptr_t, int>> g_group_cache;
void load_culture_groups() {
    std::vector<std::string> mods = mod_paths();
    std::string path = "common\\cultures.txt", s;
    for (int i = (int)mods.size() - 1; i >= 0; --i) {
        std::string p = mods[i] + "\\common\\cultures.txt";
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) { path = p; break; }
    }
    if (!read_file(path.c_str(), s)) { logf("cultures: %s not readable", path.c_str()); return; }
    Tok tk;
    tokenize(s, tk);
    g_culture_group.clear(); g_group_names.clear(); g_group_cache.clear();
    while (!tk.eof()) {
        std::string group = tk.next();
        if (tk.peek() != "=") continue;
        tk.next();
        if (tk.peek() != "{") { tk.next(); continue; }
        tk.next();
        int gi = (int)g_group_names.size();
        g_group_names.push_back(group);
        while (!tk.eof() && tk.peek() != "}") {
            std::string k = tk.next();
            if (tk.peek() != "=") continue;
            tk.next();
            if (tk.peek() != "{") { tk.next(); continue; }
            // a block inside a group: a culture if it has a colour
            size_t start = tk.i;
            skip_value(tk);
            bool culture = false;
            for (size_t j = start; j < tk.i; ++j) if (tk.t[j] == "color") { culture = true; break; }
            if (culture) g_culture_group.push_back({k, gi});
        }
        tk.next();
    }
    logf("cultures: %d cultures in %d groups from %s", (int)g_culture_group.size(), (int)g_group_names.size(), path.c_str());
}
int culture_group_of(uintptr_t c) {
    if (!c) return -1;
    for (auto& e : g_group_cache) if (e.first == c) return e.second;
    const char* n = culture_name(c);
    int g = -1;
    for (auto& e : g_culture_group) if (e.first == n) { g = e.second; break; }
    if (g_group_cache.size() < 4096) g_group_cache.push_back({c, g});
    return g;
}
// provinces a country owns among `ids` plus those that are cores of `cores`
void pick_provinces(uint32_t owner, const std::vector<uint32_t>& ids, const std::vector<std::string>& cores, bool majority,
                    uint32_t nation, uint32_t* out, int& n, int max) {
    uintptr_t oc = country_at((int)owner);
    int cap = rd<int32_t>(oc + 0xBE4);
    auto take = [&](uintptr_t p, uint32_t id) {
        if (n >= max || (int)id == cap) return;
        for (int i = 0; i < n; ++i) if (out[i] == id) return;
        if (majority && national_share(p, nation) < 0.5) return;
        out[n++] = id;
    };
    for (uint32_t id : ids) {
        uintptr_t p = get_province(id);
        if (p && rd<uint32_t>(p + 0x12C) == owner) take(p, id);
    }
    if (!cores.empty()) {
        std::vector<int> idx;
        for (auto& t : cores) { int i = country_index(t.c_str()); if (i > 0) idx.push_back(i); }
        static uintptr_t provs[2048];
        int np = owned_provinces((int)owner, provs, 2048);
        for (int i = 0; i < np; ++i)
            for (int c : idx)
                if (province_has_core(provs[i], (uint32_t)c)) { take(provs[i], rd<uint32_t>(provs[i] + 0x58)); break; }
    }
}

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
    // the richer side pays for safe transport when it can easily afford it; otherwise people die on the way
    PopEval pe = judge_population(a, b);
    double cost = transport_cost(pe.they_get + pe.they_lose);
    uint32_t payer = money_of(a) >= money_of(b) ? a : b;
    int loss = g_migration_loss;
    if (money_of(payer) >= 3 * cost) { money_add(payer, -cost); loss = 0; }
    uint64_t dead = 0;
    for (int k = 0; k < g_tr_npopcult; ++k) {
        uintptr_t c = g_tr_popcult[k];
        if (movable_to(ca, cb, c)) { dead += migration_deaths(b, c, loss); run_migrate_all(b, a, culture_name(c)); ++n; }
        if (movable_to(cb, ca, c)) { dead += migration_deaths(a, c, loss); run_migrate_all(a, b, culture_name(c)); ++n; }
    }
    g_last_dead = dead;
    logf("population exchange %s <-> %s: %s, %llu died", cn(a), cn(b), loss ? "no transport aid" : "safe transport paid", (unsigned long long)dead);
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
        if (g_req.kind == 2) do_pop_swap(me, ai);  // the richer side pays for transport if it can
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
    snprintf(f, sizeof(f), "htreaty_%s", h.id.c_str());
    return has_flag(country_at((int)a), f) || has_flag(country_at((int)b), f);
}
void hist_mark(const HistTreaty& h, uint32_t a, uint32_t b) {
    char f[48];
    snprintf(f, sizeof(f), "htreaty_%s", h.id.c_str());
    flag_set(country_at((int)a), f, true);
    flag_set(country_at((int)b), f, true);
}
// Returns 1 when it fired (or a request to the player was opened).
int hist_try(const HistTreaty& h, int year) {
    if (h.native_found || year < h.y0 || year > h.y1) return 0;
    int to = -1;
    for (auto& t : h.to) if (to < 0) to = alive_index(t.c_str());
    if (to < 0) return 0;
    int from = -1;
    uint32_t give[64], back[64];
    int ng = 0, nb = 0;
    for (auto& t : h.from) {
        if (from >= 0) break;
        int f = alive_index(t.c_str());
        if (f < 0 || f == to) continue;
        if (h.kind == 2) { from = f; break; }
        ng = 0;
        pick_provinces((uint32_t)f, h.provs, h.cores_of, h.majority, (uint32_t)to, give, ng, 64);
        if (ng) from = f;
    }
    if (from < 0) return 0;
    if (hist_done(h, (uint32_t)from, (uint32_t)to)) return 0;
    if (at_war_with((uint32_t)from, (uint32_t)to)) return 0;
    if (!h.to_owns.empty()) {
        bool ok = false;
        for (uint32_t id : h.to_owns) { uintptr_t p = get_province(id); ok |= p && rd<uint32_t>(p + 0x12C) == (uint32_t)to; }
        if (!ok) return 0;
    }
    if (h.kind == 1) pick_provinces((uint32_t)to, h.back, h.back_cores_of, false, (uint32_t)from, back, nb, 64);
    if (relation((uint32_t)from, (uint32_t)to) < -150) return 0;  // bitter enemies do not sign
    // same odds as the original: a decision the AI takes once allowed (chance per month) or an event's
    // mean time to happen (half of the time it has fired after mtth months)
    double p = h.mtth > 0 ? 1.0 - pow(0.5, 1.0 / h.mtth) : h.monthly / 100.0;
    if ((rnd() % 100000) >= (uint32_t)(p * 100000)) return 0;
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
        ng = std::min(ng, 16); nb = std::min(nb, 16);
        if ((uint32_t)from == me) { memcpy(r.player_gives, give, ng * 4); r.ng = ng; memcpy(r.player_gets, back, nb * 4); r.nt = nb; }
        else { memcpy(r.player_gets, give, ng * 4); r.nt = ng; memcpy(r.player_gives, back, nb * 4); r.ng = nb; }
        snprintf(r.title, sizeof(r.title), "%s", h.title.c_str());
        snprintf(r.why, sizeof(r.why), "History: %s", h.why.c_str());
        r.penalty = (uint32_t)from == me && h.kind != 2 ? 20 : 0;
        r.hist = h.id.c_str();
        req_open(r);
        return 1;
    }
    char b[220];
    // the AI side answers with the original event's odds (ai_chance of its options)
    if ((int)(rnd() % 100) >= h.accept) {
        hist_mark(h, (uint32_t)from, (uint32_t)to);
        snprintf(b, sizeof(b), "%s: %s turned it down.", h.title.c_str(), cn((uint32_t)from));
        logf("treaty %s refused (accept %d%%)", h.id.c_str(), h.accept);
        toast(b);
        return 1;
    }
    if (h.kind == 2) {
        do_pop_swap((uint32_t)to, (uint32_t)from);
        snprintf(b, sizeof(b), "%s: %s and %s exchange their minorities.", h.title.c_str(), cn((uint32_t)from), cn((uint32_t)to));
    } else {
        transfer_list(give, ng, (uint32_t)to);
        if (nb) transfer_list(back, nb, (uint32_t)from);
        char names[160];
        list_names(give, ng, names, sizeof(names));
        if (nb) {
            char back_names[160];
            list_names(back, nb, back_names, sizeof(back_names));
            snprintf(b, sizeof(b), "%s: %s cedes %s to %s and receives %s.", h.title.c_str(), cn((uint32_t)from), names, cn((uint32_t)to), back_names);
        } else snprintf(b, sizeof(b), "%s: %s cedes %s to %s.", h.title.c_str(), cn((uint32_t)from), names, cn((uint32_t)to));
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
    for (size_t i = 0; i < g_hist.size() && fired < 2; ++i) fired += hist_try(g_hist[i], g.year);
    // dynamic deals: about one every g_dyn_years years worldwide, at most one per month
    if (!fired && (int)(rnd() % (uint32_t)(12 * std::max(g_dyn_years, 1))) == 0) dynamic_try(g.year);
    // the player is asked separately, rarely
    else if (!fired && !g_req.active && (int)(rnd() % (uint32_t)(12 * std::max(g_req_years, 1))) == 0) player_request_try(g.year, false);
}
// ---- the game's own treaty decisions/events: recognised when their secede_province runs ----
struct NativeHit { int entry; uint32_t from, to; uint32_t provs[16]; int n; };
NativeHit g_native_hits[8];
int g_native_n = 0;
bool tag_in(const std::vector<std::string>& l, uint32_t idx) {
    for (auto& t : l) if (country_index(t.c_str()) == (int)idx) return true;
    return false;
}
bool entry_has_province(const HistTreaty& h, uintptr_t p, uint32_t id, bool back) {
    const std::vector<uint32_t>& ids = back ? h.back : h.provs;
    for (uint32_t x : ids) if (x == id) return true;
    for (auto& t : back ? h.back_cores_of : h.cores_of) {
        int c = country_index(t.c_str());
        if (c > 0 && province_has_core(p, (uint32_t)c)) return true;
    }
    return false;
}
void native_secede(uint32_t id, uint32_t before, uint32_t after) {
    uintptr_t p = get_province(id);
    for (size_t i = 0; i < g_hist.size(); ++i) {
        const HistTreaty& h = g_hist[i];
        bool fwd = tag_in(h.from, before) && tag_in(h.to, after) && entry_has_province(h, p, id, false);
        bool rev = h.kind == 1 && tag_in(h.to, before) && tag_in(h.from, after) && entry_has_province(h, p, id, true);
        if (!fwd && !rev) continue;
        hist_mark(h, before, after);
        int k = 0;
        while (k < g_native_n && g_native_hits[k].entry != (int)i) ++k;
        if (k == g_native_n) {
            if (g_native_n >= 8) return;
            g_native_hits[k] = NativeHit{(int)i, fwd ? before : after, fwd ? after : before, {}, 0};
            ++g_native_n;
        }
        NativeHit& nh = g_native_hits[k];
        if (fwd && nh.n < 16) nh.provs[nh.n++] = id;
        logf("treaty %s: the game's own event moved %s (%s -> %s)", h.id.c_str(), pn(id), cn(before), cn(after));
        return;
    }
}
void native_flush() {
    for (int k = 0; k < g_native_n; ++k) {
        NativeHit& nh = g_native_hits[k];
        const HistTreaty& h = g_hist[nh.entry];
        char names[160], b[240];
        list_names(nh.provs, nh.n, names, sizeof(names));
        if (nh.n) snprintf(b, sizeof(b), "%s: %s cedes %s to %s.", h.title.c_str(), cn(nh.from), names, cn(nh.to));
        else snprintf(b, sizeof(b), "%s signed by %s and %s.", h.title.c_str(), cn(nh.from), cn(nh.to));
        toast(b);
    }
    g_native_n = 0;
}
// per idler check (~10/s): toast timer, request expiry
void ai_treaties_frame() {
    if (g_native_n) native_flush();
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
    t.add("%d historical treaties (now %d-%02d) from %d file(s). Your treasury as the plugin reads it: %.0f\n", (int)g_hist.size(), gd.year, gd.month,
          (int)g_treaty_files.size(), money_of(player_index()));
    for (size_t i = 0; i < g_hist.size(); ++i) {
        const HistTreaty& h = g_hist[i];
        int to = -1, from = -1;
        for (auto& x : h.to) if (to < 0) to = alive_index(x.c_str());
        for (auto& x : h.from) if (from < 0) from = alive_index(x.c_str());
        const char* st;
        if (to >= 0 && from >= 0 && hist_done(h, (uint32_t)from, (uint32_t)to)) st = "done";
        else if (gd.year > h.y1) st = "missed";
        else if (gd.year < h.y0) st = "future";
        else if (to < 0 || from < 0) st = "countries missing";
        else st = "possible now";
        if (h.native_found && strcmp(st, "done") != 0) st = "run by the game's own decision/event (same conditions)";
        if (h.mtth) t.add("%d-%d %s: %s  (mtth %d months, AI accepts %d%%)\n", h.y0, h.y1, h.title.c_str(), st, h.mtth, h.accept);
        else t.add("%d-%d %s: %s  (%d%%/month, AI accepts %d%%)\n", h.y0, h.y1, h.title.c_str(), st, h.monthly, h.accept);
    }
    return finish(out, true, t);
}
namespace {
