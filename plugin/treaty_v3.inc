// ================= Treaty window, Victoria 3 / HOI4 / EU style =================
// One treaty = articles on both sides (provinces, money, population, safe transport). Every article has an
// acceptance score; the total decides like Victoria 3: >= +30 they accept, <= 0 they refuse, in between a chance.
// When they would refuse, a counter-offer shows how much money would make them sign. While the window is
// open the political map shows the treaty: what you give in red, what you get in green.
double g_tr_money_give = 0, g_tr_money_take = 0;
bool g_tr_pop_theirs = false;   // their people living with you go home
bool g_tr_pop_mine = false;     // your people living with them come home
bool g_treaty_map = false;      // political map shows the treaty

struct Article { char text[120]; int score; int kind; uint32_t id; };  // kind: 1 province, 2 money, 3 pop, 4 transport
struct TScore {
    bool possible;
    int score;
    char why[16][110];
    int nwhy;
    char block[120];
    Article left[64], right[64];
    int nl, nr;
    uint64_t pop_get, pop_lose;   // people they receive / lose in the population articles
};
void tw(TScore& t, int v, const char* fmt, ...) {
    if (t.nwhy >= 16) return;
    char b[100];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (v) snprintf(t.why[t.nwhy++], 110, "%s%+d%s  %s", v > 0 ? C_GREEN : C_RED, v, C_END, b);
    else snprintf(t.why[t.nwhy++], 110, "%s", b);
}
int pop_opt_from_flags() { return g_tr_pop_theirs && g_tr_pop_mine ? 0 : g_tr_pop_theirs ? 1 : 2; }
int money_score(double m, uint32_t ai) {
    double d = std::max(0.5 * money_of(ai), 20000.0);
    return (int)lround(30.0 * tanh(m / d));
}
TScore treaty_score(uint32_t me, uint32_t ai, double extra_money) {
    TScore t{};
    t.possible = false;
    uintptr_t cm = country_at((int)me), ca = country_at((int)ai);
    if (!ai || !country_alive(ca)) { snprintf(t.block, sizeof(t.block), "Choose a country in Diplomacy and press 'Propose treaty'."); return t; }
    if (at_war_with(me, ai)) { snprintf(t.block, sizeof(t.block), "You are at war with %s: make peace first.", cn(ai)); return t; }
    int s = 0;
    bool gives_something = false, asks_something = false;
    // provinces you give
    for (int i = 0; i < g_tr_ngive; ++i) {
        uintptr_t p = get_province(g_tr_give[i]);
        if (!p || rd<uint32_t>(p + 0x12C) != me) { snprintf(t.block, sizeof(t.block), "%s is no longer yours.", pn(g_tr_give[i])); return t; }
        if ((int)g_tr_give[i] == rd<int32_t>(cm + 0xBE4)) { snprintf(t.block, sizeof(t.block), "You cannot give away your capital."); return t; }
        int v = (int)lround(6 * log2(1 + province_value(p, ai)));
        bool home = national_share(p, ai) >= 0.5;
        if (home) v += 15;
        Article& a = t.left[t.nl++];
        snprintf(a.text, sizeof(a.text), "Cede %s%s", pn(g_tr_give[i]), home ? "  (their people)" : "");
        a.score = v; a.kind = 1; a.id = g_tr_give[i];
        s += v;
        gives_something = true;
        if (t.nl >= 60) break;
    }
    // provinces they give
    int cap = rd<int32_t>(ca + 0xBE4);
    for (int i = 0; i < g_tr_ntake; ++i) {
        uintptr_t p = get_province(g_tr_take[i]);
        if (!p || rd<uint32_t>(p + 0x12C) != ai) { snprintf(t.block, sizeof(t.block), "%s is no longer theirs.", pn(g_tr_take[i])); return t; }
        if ((int)g_tr_take[i] == cap) { snprintf(t.block, sizeof(t.block), "%s will never give up its capital.", cn(ai)); return t; }
        int v = -(int)lround(6 * log2(1 + province_value(p, ai)));
        bool home = national_share(p, ai) >= 0.5;
        if (home) v -= 25;
        bool yours = national_share(p, me) >= 0.5;
        Article& a = t.right[t.nr++];
        snprintf(a.text, sizeof(a.text), "Cede %s%s", pn(g_tr_take[i]), home ? "  (their homeland)" : yours ? "  (your people)" : "");
        a.score = v; a.kind = 1; a.id = g_tr_take[i];
        s += v;
        asks_something = true;
        if (t.nr >= 60) break;
    }
    // money
    double give = g_tr_money_give + extra_money;
    if (give > 0) {
        if (money_of(me) < give) { snprintf(t.block, sizeof(t.block), "You do not have %.0f in the treasury (you have %.0f).", give, money_of(me)); return t; }
        int v = money_score(give, ai);
        Article& a = t.left[t.nl++];
        snprintf(a.text, sizeof(a.text), "Pay %.0f", give);
        a.score = v; a.kind = 2;
        s += v;
        gives_something = true;
    }
    if (g_tr_money_take > 0) {
        if (money_of(ai) < g_tr_money_take) { snprintf(t.block, sizeof(t.block), "%s does not have %.0f.", cn(ai), g_tr_money_take); return t; }
        int v = -money_score(g_tr_money_take, ai) - 5;
        Article& a = t.right[t.nr++];
        snprintf(a.text, sizeof(a.text), "Pay %.0f", g_tr_money_take);
        a.score = v; a.kind = 2;
        s += v;
        asks_something = true;
    }
    // population
    if (g_tr_pop_theirs || g_tr_pop_mine) {
        draft_push();
        g_tr_popopt = pop_opt_from_flags();
        bool safe = g_tr_safe;
        g_tr_safe = false;
        PopEval pe = eval_population(me, ai);
        g_tr_safe = safe;
        draft_pop();
        t.pop_get = pe.they_get;
        t.pop_lose = pe.they_lose;
        static uint32_t logged_for = 0;
        if (g_tr_pop_mine && pe.they_lose == 0 && logged_for != ai) {  // explain why nobody would come home
            logged_for = ai;
            static uintptr_t provs[2048];
            uintptr_t cul[32]; uint64_t ppl[32]; int nc = 0;
            int np = owned_provinces((int)ai, provs, 2048);
            for (int i = 0; i < np; ++i)
                for_each_pop(provs[i], [&](uintptr_t pop, int) {
                    if (!pop_alive(pop)) return;
                    uintptr_t c = rd<uintptr_t>(pop + 0x6C);
                    int k = 0;
                    while (k < nc && cul[k] != c) ++k;
                    if (k == nc && nc < 32) { cul[nc] = c; ppl[nc++] = 0; }
                    if (k < nc) ppl[k] += rd<uint32_t>(pop + 0x58);
                });
            for (int k = 0; k < nc; ++k)
                logf("population check %s: %s %llu people, yours %d theirs %d group %d (your group %d, theirs %d) -> %s", cn(ai),
                     culture_name(cul[k]), (unsigned long long)ppl[k], national_culture(cm, cul[k]), national_culture(ca, cul[k]),
                     culture_group_of(cul[k]), culture_group_of(rd<uintptr_t>(cm + 0xE64)), culture_group_of(rd<uintptr_t>(ca + 0xE64)),
                     movable_to(cm, ca, cul[k]) ? "comes home" : "stays");
        }
        if (!pe.e.possible) { snprintf(t.block, sizeof(t.block), "Population: %s", pe.e.n ? pe.e.why[pe.e.n - 1] : "nothing to move."); return t; }
        char n1[24], n2[24];
        fmt_people(pe.they_get, n1, sizeof(n1));
        fmt_people(pe.they_lose, n2, sizeof(n2));
        char die1[48] = "", die2[48] = "";
        if (!g_tr_safe && g_migration_loss > 0) {
            char d1[24], d2[24];
            fmt_people(pe.they_get * g_migration_loss / 100, d1, sizeof(d1));
            fmt_people(pe.they_lose * g_migration_loss / 100, d2, sizeof(d2));
            snprintf(die1, sizeof(die1), "  (" C_RED "%s die" C_END ")", d1);
            snprintf(die2, sizeof(die2), "  (" C_RED "%s die" C_END ")", d2);
        }
        if (g_tr_pop_theirs) {
            int v = (int)lround(10 * log2(1 + pe.they_get / 50000.0));
            Article& a = t.left[t.nl++];
            snprintf(a.text, sizeof(a.text), "Send %s of their people home%s", n1, pe.they_get ? die1 : "");
            a.score = v; a.kind = 3;
            s += v;
            gives_something = true;
        }
        if (g_tr_pop_mine) {
            int v = -(int)lround(6 * log2(1 + pe.they_lose / 50000.0));
            Article& a = t.right[t.nr++];
            snprintf(a.text, sizeof(a.text), "Let %s of your people come home%s", n2, pe.they_lose ? die2 : "");
            a.score = v; a.kind = 3;
            s += v;
            asks_something = true;
        }
        if (g_tr_safe) {
            double cost = transport_cost(pe.they_get + pe.they_lose);
            if (money_of(me) < cost + give) { snprintf(t.block, sizeof(t.block), "Safe transport costs %.0f; your treasury has %.0f.", cost, money_of(me)); return t; }
            Article& a = t.left[t.nl++];
            snprintf(a.text, sizeof(a.text), "Safe transport for everyone (%.0f)", cost);
            a.score = 10; a.kind = 4;
            s += 10;
        }
    }
    if (!gives_something && !asks_something) { snprintf(t.block, sizeof(t.block), "The treaty is empty: add articles on either side."); return t; }
    t.possible = true;
    // circumstances
    if (asks_something && !gives_something) {
        int v = prestige_of(cm) >= 2 * std::max(prestige_of(ca), 1) ? -5 : -20;
        s += v;
        tw(t, v, "nothing offered in return%s", v == -5 ? " (but you are far stronger)" : "");
    }
    int pm = prestige_of(cm), pa = prestige_of(ca);
    if (pm >= 2 * std::max(pa, 1)) { s += 10; tw(t, 10, "pressure from a much stronger power"); }
    else if (pa >= 2 * std::max(pm, 1)) { s -= 10; tw(t, -10, "they are much stronger than you"); }
    int rel = relation(ai, me) / 5;
    if (rel) { s += rel; tw(t, rel, "relations %+d", relation(ai, me)); }
    bool same_rel = rd<uintptr_t>(cm + 0xE68) == rd<uintptr_t>(ca + 0xE68);
    s += same_rel ? 5 : -5;
    tw(t, same_rel ? 5 : -5, same_rel ? "same state religion" : "different state religion");
    const char* bm = cached_bloc(me);
    const char* ba = cached_bloc(ai);
    if (bm && ba && _stricmp(bm, ba) == 0) { s += 15; tw(t, 15, "members of the same bloc"); }
    t.score = s;
    return t;
}
int score_chance(const TScore& t) { return !t.possible ? 0 : t.score >= 30 ? 100 : t.score <= 0 ? 0 : t.score * 100 / 30; }
// Money that would make them sign (score >= 30), or -1.
double counter_money(uint32_t me, uint32_t ai, const TScore& t) {
    if (!t.possible || t.score >= 30) return -1;
    double have = money_of(me) - g_tr_money_give;
    for (double m = 5000; m <= have && m <= 5000000; m += m < 50000 ? 5000 : 25000) {
        TScore u = treaty_score(me, ai, m);
        if (u.possible && u.score >= 30) return m;
    }
    return -1;
}

void treaty_execute(uint32_t me, uint32_t ai, const TScore& t) {
    int moved = 0;
    for (int i = 0; i < g_tr_ntake; ++i) moved += transfer_province(g_tr_take[i], me);
    for (int i = 0; i < g_tr_ngive; ++i) moved += transfer_province(g_tr_give[i], ai);
    if (g_tr_money_give > 0) { money_add(me, -g_tr_money_give); money_add(ai, g_tr_money_give); }
    if (g_tr_money_take > 0) { money_add(ai, -g_tr_money_take); money_add(me, g_tr_money_take); }
    uint64_t dead = 0;
    if (g_tr_pop_theirs || g_tr_pop_mine) {
        g_tr_popopt = pop_opt_from_flags();
        if (g_tr_safe) money_add(me, -transport_cost(t.pop_get + t.pop_lose));
        treaty_propose_population_now(me, ai);
        dead = g_last_dead;
    }
    relation_add(me, ai, 10);
    g_bloc_cache_dirty = true;
    bloc_map_redraw();
    char cap[64], dd[24], b[220];
    snprintf(cap, sizeof(cap), "%s", pn((uint32_t)rd<int32_t>(country_at((int)ai) + 0xBE4)));
    fmt_people(dead, dd, sizeof(dd));
    snprintf(b, sizeof(b), "Treaty of %s signed with %s: %d province(s) changed hands%s%s.", cap, cn(ai), moved,
             (g_tr_pop_theirs || g_tr_pop_mine) ? (dead ? ", populations moved, " : ", populations moved safely") : "",
             (g_tr_pop_theirs || g_tr_pop_mine) && dead ? dd : "");
    if ((g_tr_pop_theirs || g_tr_pop_mine) && dead) strncat(b, " died on the way", sizeof(b) - strlen(b) - 1);
    snprintf(g_tr_status, sizeof(g_tr_status), "%s", b);
    toast(b);
    g_tr_ngive = g_tr_ntake = 0;
    g_tr_money_give = g_tr_money_take = 0;
    g_tr_pop_theirs = g_tr_pop_mine = false;
    g_tr_safe = false;
}
void treaty_v3_propose() {
    uint32_t me = player_index(), ai = g_tr_partner;
    TScore t = treaty_score(me, ai, 0);
    if (!t.possible) { snprintf(g_tr_status, sizeof(g_tr_status), "%s", t.block); return; }
    int chance = score_chance(t);
    bool yes = rd<uint8_t>(R(VA_YESMEN)) || (int)(rnd() % 100) < chance;
    logf("treaty with %s: score %d, chance %d%%, %s", cn(ai), t.score, chance, yes ? "signed" : "refused");
    if (yes) { treaty_execute(me, ai, t); return; }
    relation_add(me, ai, -10);
    double m = counter_money(me, ai, treaty_score(me, ai, 0));
    if (m > 0) snprintf(g_tr_status, sizeof(g_tr_status), "%s refused (score %+d). Counter-offer: they would sign for %.0f more. Relations -10.", cn(ai), t.score, m);
    else snprintf(g_tr_status, sizeof(g_tr_status), "%s refused the treaty (score %+d). Relations -10.", cn(ai), t.score);
}

// ---- buttons ----
extern "C" void __cdecl tcp_on_t3_lmoney() { g_tr_money_give += 10000; treaty_panel_refresh(); }
extern "C" void __cdecl tcp_on_t3_rmoney() { g_tr_money_take += 10000; treaty_panel_refresh(); }
extern "C" void __cdecl tcp_on_t3_lpop() { g_tr_pop_theirs = !g_tr_pop_theirs; treaty_panel_refresh(); }
extern "C" void __cdecl tcp_on_t3_rpop() { g_tr_pop_mine = !g_tr_pop_mine; treaty_panel_refresh(); }
extern "C" void __cdecl tcp_on_t3_safe() { g_tr_safe = !g_tr_safe; treaty_panel_refresh(); }
extern "C" void __cdecl tcp_on_t3_propose() { treaty_v3_propose(); treaty_panel_refresh(); }
extern "C" void __cdecl tcp_on_t3_counter() {
    uint32_t me = player_index();
    double m = counter_money(me, g_tr_partner, treaty_score(me, g_tr_partner, 0));
    if (m > 0) { g_tr_money_give += m; snprintf(g_tr_status, sizeof(g_tr_status), "Added %.0f to your side, as they asked.", m); }
    treaty_panel_refresh();
}
extern "C" void __cdecl tcp_on_t3_clear() {
    g_tr_ngive = g_tr_ntake = 0;
    g_tr_money_give = g_tr_money_take = 0;
    g_tr_pop_theirs = g_tr_pop_mine = g_tr_safe = false;
    g_tr_status[0] = 0;
    treaty_panel_refresh();
}
// remove the article in row r of a side
TScore g_t3_last;
void t3_remove(bool left, int r) {
    const Article* arr = left ? g_t3_last.left : g_t3_last.right;
    int n = left ? g_t3_last.nl : g_t3_last.nr;
    if (r >= n) return;
    const Article& a = arr[r];
    if (a.kind == 1) {
        if (left) list_remove(g_tr_give, g_tr_ngive, a.id);
        else list_remove(g_tr_take, g_tr_ntake, a.id);
    } else if (a.kind == 2) {
        if (left) g_tr_money_give = 0; else g_tr_money_take = 0;
    } else if (a.kind == 3) {
        if (left) g_tr_pop_theirs = false; else g_tr_pop_mine = false;
    } else if (a.kind == 4) g_tr_safe = false;
    treaty_panel_refresh();
    bloc_map_redraw();
}
#define T3_X(side, i) extern "C" void __cdecl tcp_on_t3_##side##x##i() { t3_remove(#side[0] == 'l', i); }
T3_X(l, 0) T3_X(l, 1) T3_X(l, 2) T3_X(l, 3) T3_X(l, 4) T3_X(l, 5) T3_X(l, 6) T3_X(l, 7)
T3_X(r, 0) T3_X(r, 1) T3_X(r, 2) T3_X(r, 3) T3_X(r, 4) T3_X(r, 5) T3_X(r, 6) T3_X(r, 7)
#undef T3_X
}  // namespace
#define T3_THUNK(n) "    .globl _tcp_" #n "_thunk\n_tcp_" #n "_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_" #n "\n    popfl\n    popal\n    ret\n"
asm(".text\n" T3_THUNK(t3_lmoney) T3_THUNK(t3_rmoney) T3_THUNK(t3_lpop) T3_THUNK(t3_rpop) T3_THUNK(t3_safe) T3_THUNK(t3_propose)
    T3_THUNK(t3_counter) T3_THUNK(t3_clear)
    T3_THUNK(t3_lx0) T3_THUNK(t3_lx1) T3_THUNK(t3_lx2) T3_THUNK(t3_lx3) T3_THUNK(t3_lx4) T3_THUNK(t3_lx5) T3_THUNK(t3_lx6) T3_THUNK(t3_lx7)
    T3_THUNK(t3_rx0) T3_THUNK(t3_rx1) T3_THUNK(t3_rx2) T3_THUNK(t3_rx3) T3_THUNK(t3_rx4) T3_THUNK(t3_rx5) T3_THUNK(t3_rx6) T3_THUNK(t3_rx7));
#undef T3_THUNK
extern "C" {
void tcp_t3_lmoney_thunk(); void tcp_t3_rmoney_thunk(); void tcp_t3_lpop_thunk(); void tcp_t3_rpop_thunk(); void tcp_t3_safe_thunk();
void tcp_t3_propose_thunk(); void tcp_t3_counter_thunk(); void tcp_t3_clear_thunk();
void tcp_t3_lx0_thunk(); void tcp_t3_lx1_thunk(); void tcp_t3_lx2_thunk(); void tcp_t3_lx3_thunk(); void tcp_t3_lx4_thunk(); void tcp_t3_lx5_thunk(); void tcp_t3_lx6_thunk(); void tcp_t3_lx7_thunk();
void tcp_t3_rx0_thunk(); void tcp_t3_rx1_thunk(); void tcp_t3_rx2_thunk(); void tcp_t3_rx3_thunk(); void tcp_t3_rx4_thunk(); void tcp_t3_rx5_thunk(); void tcp_t3_rx6_thunk(); void tcp_t3_rx7_thunk();
}
namespace {
alignas(8) uint8_t g_t3_glue[32][kGlueSize];
void treaty_v3_subscribe(void* w) {
    struct { const char* name; void (*fn)(); } b[] = {
        {"v2t3_lmoney", tcp_t3_lmoney_thunk}, {"v2t3_rmoney", tcp_t3_rmoney_thunk}, {"v2t3_lpop", tcp_t3_lpop_thunk},
        {"v2t3_rpop", tcp_t3_rpop_thunk}, {"v2t3_safe", tcp_t3_safe_thunk}, {"v2t3_propose", tcp_t3_propose_thunk},
        {"v2t3_counter", tcp_t3_counter_thunk}, {"v2t3_clear", tcp_t3_clear_thunk},
        {"v2t3_lx0", tcp_t3_lx0_thunk}, {"v2t3_lx1", tcp_t3_lx1_thunk}, {"v2t3_lx2", tcp_t3_lx2_thunk}, {"v2t3_lx3", tcp_t3_lx3_thunk},
        {"v2t3_lx4", tcp_t3_lx4_thunk}, {"v2t3_lx5", tcp_t3_lx5_thunk}, {"v2t3_lx6", tcp_t3_lx6_thunk}, {"v2t3_lx7", tcp_t3_lx7_thunk},
        {"v2t3_rx0", tcp_t3_rx0_thunk}, {"v2t3_rx1", tcp_t3_rx1_thunk}, {"v2t3_rx2", tcp_t3_rx2_thunk}, {"v2t3_rx3", tcp_t3_rx3_thunk},
        {"v2t3_rx4", tcp_t3_rx4_thunk}, {"v2t3_rx5", tcp_t3_rx5_thunk}, {"v2t3_rx6", tcp_t3_rx6_thunk}, {"v2t3_rx7", tcp_t3_rx7_thunk},
    };
    for (size_t i = 0; i < sizeof(b) / sizeof(b[0]); ++i) subscribe(w, b[i].name, g_t3_glue[i], b[i].fn);
}
void treaty_v3_refresh() {
    void* w = g_treatywin;
    uint32_t me = player_index(), ai = g_tr_partner;
    TScore t = treaty_score(me, ai, 0);
    g_t3_last = t;
    char b[220], nm[32];
    if (ai) {
        snprintf(b, sizeof(b), "Treaty of %s", pn((uint32_t)rd<int32_t>(country_at((int)ai) + 0xBE4)));
        tb_set(w, "v2tr_header", b);
        snprintf(b, sizeof(b), "%s  and  %s", cn(me), cn(ai));
        tb_set(w, "v2t3_sub", b);
    } else { tb_set(w, "v2tr_header", "Treaty"); tb_set(w, "v2t3_sub", " "); }
    snprintf(b, sizeof(b), "%s GIVES", cn(me));
    for (char* q = b; *q; ++q) *q = (char)toupper((unsigned char)*q);
    tb_set(w, "v2t3_lhead", b);
    snprintf(b, sizeof(b), "%s GIVES", ai ? cn(ai) : "THEY");
    for (char* q = b; *q; ++q) *q = (char)toupper((unsigned char)*q);
    tb_set(w, "v2t3_rhead", b);
    auto rows = [&](const char* side, const Article* arr, int n) {
        for (int r = 0; r < 8; ++r) {
            snprintf(nm, sizeof(nm), "v2t3_%s%d", side, r);
            char sc[24];
            if (r == 7 && n > 8) { snprintf(b, sizeof(b), "... and %d more", n - 7); sc[0] = 0; }
            else if (r < n) { snprintf(b, sizeof(b), "%s", arr[r].text); snprintf(sc, sizeof(sc), "%s%+d" C_END, arr[r].score >= 0 ? C_GREEN : C_RED, arr[r].score); }
            else { snprintf(b, sizeof(b), r == 0 && n == 0 ? "(nothing)" : " "); sc[0] = 0; }
            tb_set(w, nm, b);
            snprintf(nm, sizeof(nm), "v2t3_%ss%d", side, r);
            tb_set(w, nm, sc[0] ? sc : " ");
        }
    };
    rows("l", t.left, t.nl);
    rows("r", t.right, t.nr);
    // acceptance
    if (!ai) snprintf(b, sizeof(b), " ");
    else if (!t.possible) snprintf(b, sizeof(b), C_RED "Cannot be proposed" C_END);
    else if (t.score >= 30) snprintf(b, sizeof(b), "Acceptance %s%+d" C_END "   -   " C_GREEN "they will sign" C_END, C_GREEN, t.score);
    else if (t.score <= 0) snprintf(b, sizeof(b), "Acceptance %s%+d" C_END "   -   " C_RED "they will refuse" C_END, C_RED, t.score);
    else snprintf(b, sizeof(b), "Acceptance %+d   -   %d%% chance they sign", t.score, score_chance(t));
    tb_set(w, "v2t3_accept", b);
    // bar: 0 .. +30 scale, 40 marks
    if (ai && t.possible) {
        char bar[128];
        int k = 0, fill = std::max(0, std::min(40, (t.score + 10) * 40 / 50));
        k += snprintf(bar + k, sizeof(bar) - k, "%s", t.score >= 30 ? C_GREEN : t.score <= 0 ? C_RED : "");
        for (int i = 0; i < 40; ++i) {
            if (i == fill) k += snprintf(bar + k, sizeof(bar) - k, C_END);
            bar[k++] = i == 8 ? '|' : i == 32 ? '|' : (i < fill ? '#' : '.');
        }
        bar[k] = 0;
        tb_set(w, "v2t3_bar", bar);
        tb_set(w, "v2t3_scale", "refuse  0                                  +30  sign");
    } else { tb_set(w, "v2t3_bar", " "); tb_set(w, "v2t3_scale", " "); }
    for (int i = 0; i < 8; ++i) {
        snprintf(nm, sizeof(nm), "v2t3_why%d", i);
        if (!ai) tb_set(w, nm, i == 0 ? "Open Diplomacy, select a country and press 'Propose treaty'." : " ");
        else if (!t.possible) tb_set(w, nm, i == 0 ? t.block : " ");
        else tb_set(w, nm, i < t.nwhy ? t.why[i] : " ");
    }
    // counter offer
    double m = ai && t.possible ? counter_money(me, ai, t) : -1;
    if (m > 0) snprintf(b, sizeof(b), "Counter-offer: they would sign for %.0f more", m);
    else if (ai && t.possible && t.score < 30) snprintf(b, sizeof(b), "No amount of money you have would make them sign.");
    else snprintf(b, sizeof(b), " ");
    tb_set(w, "v2t3_countertext", b);
    // toggles
    tb_set(w, "v2t3_lpop_text", g_tr_pop_theirs ? C_GREEN "Their people go home" C_END : "+ Their people go home");
    tb_set(w, "v2t3_rpop_text", g_tr_pop_mine ? C_GREEN "My people come home" C_END : "+ My people come home");
    tb_set(w, "v2t3_safe_text", g_tr_safe ? C_GREEN "Safe transport: paid" C_END : "+ Pay safe transport");
    tb_set(w, "v2tr_pick_text", g_tr_pick ? C_GREEN "Picking on map: ON" C_END : "Pick provinces on map");
    snprintf(b, sizeof(b), "Treasury: %.0f", money_of(me));
    tb_set(w, "v2t3_money", b);
    tb_set(w, "v2tr_status", g_tr_status[0] ? g_tr_status : " ");
    g_treaty_map = ai != 0;
    bloc_map_redraw();
}
bool treaty_window_open() { return g_treatywin && win_visible(g_treatywin); }
// map colours while the treaty window is open: 0 = leave as is
uint32_t treaty_map_color(uintptr_t prov) {
    uint32_t id = rd<uint32_t>(prov + 0x58);
    if (in_list(g_tr_give, g_tr_ngive, id)) return 0xD04040;   // you give: red
    if (in_list(g_tr_take, g_tr_ntake, id)) return 0x40B050;   // you get: green
    return 0;
}
