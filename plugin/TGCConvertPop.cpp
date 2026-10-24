// Console Commands (any mod; formerly TGC Convert Pop) - a plugin for The Grand Loader (Victoria II 3.04, v2game.exe SHA-256 62d48c20...).
// Works alongside V2DLL / Victoria 2 Unofficial Patch 3.05 (V2DLL is loaded as lua51.dll, this as a
// Grand Loader plugin through dinput8.dll; none of the sites below are touched by V2DLL).
//
// Console commands
//   convert_pop <province> [culture|-|owner] [religion|-|owner]
//       Rewrites culture and/or religion of every pop in a province (default: owner's culture + religion).
//   list_prov_cultures <province>
//       Lists every pop of a province: internal pop id, pop type, culture, size; then totals per culture.
//   list_prov_religions <province>
//       Same, with religions.
//   migrate_pop <from province> <to province> <filter> [filter...]
//       Moves the pops of <from> that match ALL filters to <to>. A filter is a pop id (number from the
//       list commands), a culture, a religion or a pop type name, e.g.
//         migrate_pop 123 555 polish
//         migrate_pop 123 555 polish catholic
//         migrate_pop 123 555 polish farmers
//         migrate_pop 123 555 48211
//       Uses the game's own move routine (the one behind the script effect move_pop), so pops are
//       merged with an identical pop in the destination exactly like move_pop does.
//
// Wiring (3.04 listing VAs, rebased at runtime for ASLR):
//   * Console list: FUN_00423990 pushes pointers to 60 static 0x4C-byte entries (0x00F09740..0x00F0A910)
//     into a vector. We copy them into our own table, append our entries and retarget the loop.
//     Entry: +0x00 u8 enabled, +0x04 name, +0x08 alias count, +0x0C up to 3 inline alias pointers,
//     +0x18 description, +0x1C handler, +0x20 argc (help only), +0x24 arg names[10].
//     Handler: Result* __cdecl fn(Result*, vector<Token>* args); args exclude the name; Token stride
//     0x1C with an MSVC std::string at +0; Result = { u8 ok; std::string text; }.
//   * Pop: +0x0C id (tag +0x08 == 0x2E), +0x58 size, +0x64 province, +0x6C culture, +0x70 religion,
//     +0x27C next; vtable +0x20 = is-alive (save writer 0x00956060, move_pop Execute 0x00897140).
//   * Province: +0x58 id, +0x12C owner index, +0x194 per-pop-type lists {head,tail,count,?}.
//   * Country: +0xE64 primary culture, +0xE68 state religion.
//   * Culture name +0x18, religion name +0x10, pop type name +0x08 (std::string).
//   * Pop_MoveToProvince 0x004B8E80: __stdcall(Province* dest, Pop* pop, bool 0).

#include <windows.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tgl/plugin_api.h"

namespace {

const TGL_HostApi* g_host = nullptr;
uintptr_t g_base = 0;
bool g_require_mod = true;
bool g_mod_active = false;
bool g_fix_merge_everywhere = false;
bool g_merge_fix_ok = false;
extern "C" { volatile uint8_t tcp_strict_merge = 0; uintptr_t tcp_merge_found = 0, tcp_merge_next = 0; }

constexpr uintptr_t kImageBase = 0x00400000;
inline uintptr_t R(uintptr_t va) { return g_base + (va - kImageBase); }

void logf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_host && g_host->log) g_host->log(g_host, buf);
}

// ---- game addresses (3.04 VAs) ----
constexpr uintptr_t VA_TABLE_BEGIN = 0x00F09740;
constexpr uintptr_t VA_TABLE_END = 0x00F0A910;
constexpr int kVanillaCount = 60;
constexpr size_t kEntrySize = 0x4C;

constexpr uintptr_t VA_LOOP_RESERVE = 0x004239C2;  // 6A 3C          push 0x3C
constexpr uintptr_t VA_LOOP_START = 0x004239D0;    // BB imm32       mov ebx, table
constexpr uintptr_t VA_LOOP_LAST = 0x004239D5;     // B8 3B 00 00 00 mov eax, 0x3B
constexpr uintptr_t VA_LOOP_END = 0x00423A3B;      // 81 FB imm32    cmp ebx, table_end

constexpr uintptr_t VA_STRING_ASSIGN = 0x00409350;   // thiscall std::string::assign(const char*, size_t)
constexpr uintptr_t VA_CULTURE_FIND = 0x004A2F70;    // EAX=map, push std::string*, ret 4
constexpr uintptr_t VA_RELIGION_FIND = 0x005677B0;   // EAX=map, push std::string*, ret 4
constexpr uintptr_t VA_POP_MOVE = 0x004B8E80;        // stdcall (Province*, Pop*, bool)
constexpr uintptr_t VA_G_WORLD = 0x012588E8;
constexpr uintptr_t VA_G_COUNTRIES = 0x012587E4;
constexpr uintptr_t VA_G_CULTURE_MAP = 0x012587E8;
constexpr uintptr_t VA_G_RELIGION_MAP = 0x0125EA24;
constexpr uintptr_t VA_G_POPTYPE_DB = 0x0131BB34;

// ---- MSVC 2010 std::string as the game lays it out (0x18 bytes) ----
struct GameString {
    union {
        char buf[16];
        char* ptr;
    };
    uint32_t size;
    uint32_t cap;
};
static_assert(sizeof(GameString) == 0x18, "layout");

const char* gs_cstr(const GameString* s) { return s->cap >= 16 ? s->ptr : s->buf; }

void gs_view(GameString* s, const char* text, size_t n) {  // read-only, never freed by the game
    memset(s, 0, sizeof(*s));
    s->size = (uint32_t)n;
    if (n < 16) {
        memcpy(s->buf, text, n);
        s->buf[n] = 0;
        s->cap = 15;
    } else {
        s->ptr = const_cast<char*>(text);
        s->cap = (uint32_t)n;
    }
}

struct ConsoleResult {
    uint8_t ok;
    uint8_t pad[3];
    GameString text;
};

struct ArgVector {
    uint8_t* begin;
    uint8_t* end;
    uint8_t* cap;
};
constexpr size_t kTokenSize = 0x1C;

// ---- thunks for the game's calling conventions (callee-saved registers kept) ----
extern "C" void* tcp_lookup_thunk(uintptr_t fn, void* map, const GameString* name);
extern "C" void tcp_assign_thunk(uintptr_t fn, GameString* self, const char* text, size_t n);
extern "C" uint32_t tcp_vcall0_thunk(uintptr_t fn, void* self);
extern "C" void tcp_move_thunk(uintptr_t fn, void* dest, void* pop);

asm(R"(
    .text
    .globl _tcp_lookup_thunk
_tcp_lookup_thunk:
    pushl %ebp
    movl  %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl  12(%ebp), %eax
    pushl 16(%ebp)
    call  *8(%ebp)
    popl  %edi
    popl  %esi
    popl  %ebx
    popl  %ebp
    ret

    .globl _tcp_assign_thunk
_tcp_assign_thunk:
    pushl %ebp
    movl  %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl  12(%ebp), %ecx
    pushl 20(%ebp)
    pushl 16(%ebp)
    call  *8(%ebp)
    popl  %edi
    popl  %esi
    popl  %ebx
    popl  %ebp
    ret

    .globl _tcp_vcall0_thunk
_tcp_vcall0_thunk:
    pushl %ebp
    movl  %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl  12(%ebp), %ecx
    call  *8(%ebp)
    movzbl %al, %eax
    popl  %edi
    popl  %esi
    popl  %ebx
    popl  %ebp
    ret

    .globl _tcp_move_thunk
_tcp_move_thunk:
    pushl %ebp
    movl  %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl $0
    pushl 16(%ebp)
    pushl 12(%ebp)
    call  *8(%ebp)
    popl  %edi
    popl  %esi
    popl  %ebx
    popl  %ebp
    ret

    .globl _tcp_esi_call_thunk
_tcp_esi_call_thunk:
    pushl %ebp
    movl  %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl  12(%ebp), %esi
    pushl 16(%ebp)
    call  *8(%ebp)
    popl  %edi
    popl  %esi
    popl  %ebx
    popl  %ebp
    ret

    # Replaces 0x004BCB2E: mov ecx,[edx+0x6C] / cmp ecx,ecx / je found  (vanilla never compares cultures).
    # EAX = candidate pop in the destination, EDX = moving pop.
    .globl _tcp_merge_cave
_tcp_merge_cave:
    movl  0x6c(%edx), %ecx
    cmpb  $0, _tcp_strict_merge
    je    1f
    cmpl  0x6c(%eax), %ecx
    jne   2f
1:  jmp   *_tcp_merge_found
2:  jmp   *_tcp_merge_next
)");
extern "C" void tcp_merge_cave();
extern "C" void tcp_setsize_thunk(uintptr_t fn, uintptr_t pop, uint32_t size);
asm(R"(
    .text
    .globl _tcp_setsize_thunk
_tcp_setsize_thunk:
    pushl %ebp
    movl  %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl  12(%ebp), %esi
    movl  16(%ebp), %eax
    call  *8(%ebp)
    popl  %edi
    popl  %esi
    popl  %ebx
    popl  %ebp
    ret
)");
extern "C" uint32_t tcp_esi_call_thunk(uintptr_t fn, uintptr_t esi, uintptr_t arg);

// ---- text output ----
constexpr size_t kOutMax = 6000;
struct Text {
    char buf[kOutMax];
    size_t n = 0;
    bool truncated = false;
    void add(const char* fmt, ...) {
        if (truncated) return;
        va_list ap;
        va_start(ap, fmt);
        int w = vsnprintf(buf + n, kOutMax - n, fmt, ap);
        va_end(ap);
        if (w < 0 || n + (size_t)w >= kOutMax - 64) {
            buf[n] = 0;
            truncated = true;
            return;
        }
        n += (size_t)w;
    }
};

ConsoleResult* finish(ConsoleResult* out, bool ok, Text& t) {
    if (t.truncated) {
        int w = snprintf(t.buf + t.n, kOutMax - t.n, "\n... (list cut, see the_grand_loader.log)");
        if (w > 0) t.n += (size_t)w;
    }
    out->ok = ok ? 1 : 0;
    tcp_assign_thunk(R(VA_STRING_ASSIGN), &out->text, t.buf, t.n);
    return out;
}

ConsoleResult* reply(ConsoleResult* out, bool ok, const char* fmt, ...) {
    static Text t;
    t.n = 0;
    t.truncated = false;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(t.buf, 512, fmt, ap);
    va_end(ap);
    t.n = w < 0 ? 0 : (w >= 512 ? 511 : (size_t)w);
    if (!ok) logf("refused: %s", t.buf);
    return finish(out, ok, t);
}

void init_result(ConsoleResult* out) {
    out->ok = 0;
    memset(&out->text, 0, sizeof(out->text));
    out->text.cap = 15;
}

template <typename T>
T rd(uintptr_t addr) {
    return *reinterpret_cast<T*>(addr);
}

int arg_count(const ArgVector* args) {
    if (!args || !args->begin || args->end < args->begin) return 0;
    return (int)((args->end - args->begin) / kTokenSize);
}
const char* arg_text(const ArgVector* args, int i) {
    return gs_cstr(reinterpret_cast<const GameString*>(args->begin + i * kTokenSize));
}

const char* culture_name(uintptr_t c) { return c ? gs_cstr(reinterpret_cast<const GameString*>(c + 0x18)) : "?"; }
const char* religion_name(uintptr_t r) { return r ? gs_cstr(reinterpret_cast<const GameString*>(r + 0x10)) : "?"; }
const char* poptype_name(uintptr_t p) { return p ? gs_cstr(reinterpret_cast<const GameString*>(p + 0x08)) : "?"; }

uintptr_t find_in_map(uintptr_t fn_va, uintptr_t map_ptr_va, const char* name) {
    void* map = rd<void*>(R(map_ptr_va));
    if (!map || !name || !*name) return 0;
    GameString s;
    gs_view(&s, name, strlen(name));
    return (uintptr_t)tcp_lookup_thunk(R(fn_va), map, &s);
}
uintptr_t find_culture(const char* n) { return find_in_map(VA_CULTURE_FIND, VA_G_CULTURE_MAP, n); }
uintptr_t find_religion(const char* n) { return find_in_map(VA_RELIGION_FIND, VA_G_RELIGION_MAP, n); }

// Pop types: vector of PopType* at db+0x0C..+0x10, same order as the province pop lists.
int poptype_count() {
    uintptr_t db = rd<uintptr_t>(R(VA_G_POPTYPE_DB));
    return db ? (int)((rd<uintptr_t>(db + 0x10) - rd<uintptr_t>(db + 0x0C)) / 4) : 0;
}
uintptr_t poptype_at(int i) {
    uintptr_t db = rd<uintptr_t>(R(VA_G_POPTYPE_DB));
    return db ? rd<uintptr_t*>(db + 0x0C)[i] : 0;
}
int find_poptype(const char* name) {
    int n = poptype_count();
    for (int i = 0; i < n; ++i)
        if (_stricmp(poptype_name(poptype_at(i)), name) == 0) return i;
    return -1;
}

bool parse_uint(const char* s, uint32_t* out) {
    if (!s || !*s) return false;
    uint64_t v = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10 + (uint32_t)(*s - '0');
        if (v > 0x7FFFFFFF) return false;
    }
    *out = (uint32_t)v;
    return true;
}

uintptr_t get_province(uint32_t id) {
    uintptr_t world = rd<uintptr_t>(R(VA_G_WORLD));
    if (!world) return 0;
    uintptr_t* b = rd<uintptr_t*>(world + 0xACC);
    uintptr_t* e = rd<uintptr_t*>(world + 0xAD0);
    if (!b || e <= b || id == 0 || id >= (uint32_t)(e - b)) return 0;
    uintptr_t p = b[id];
    return (p && rd<uint32_t>(p + 0x58) == id) ? p : 0;
}

uintptr_t province_owner(uintptr_t prov) {
    uint32_t owner = rd<uint32_t>(prov + 0x12C);
    if (!owner) return 0;
    uintptr_t mgr = rd<uintptr_t>(R(VA_G_COUNTRIES));
    uintptr_t* cb = mgr ? rd<uintptr_t*>(mgr + 4) : nullptr;
    return cb ? cb[owner] : 0;
}

uint32_t pop_id(uintptr_t pop) { return rd<uint32_t>(pop + 0x0C); }
bool pop_alive(uintptr_t pop) {
    uintptr_t vt = rd<uintptr_t>(pop);
    return vt && tcp_vcall0_thunk(rd<uintptr_t>(vt + 0x20), (void*)pop) != 0;
}

// Calls fn(pop, type index) for every pop of the province. Returns false if the province has no lists.
template <typename F>
bool for_each_pop(uintptr_t prov, F fn) {
    uintptr_t lists = rd<uintptr_t>(prov + 0x194);
    int types = poptype_count();
    if (!lists || types <= 0) return false;
    for (int t = 0; t < types; ++t) {
        int guard = 0;
        for (uintptr_t pop = rd<uintptr_t>(lists + t * 0x10); pop && guard < 100000;
             pop = rd<uintptr_t>(pop + 0x27C), ++guard)
            fn(pop, t);
    }
    return true;
}

ConsoleResult* gate(ConsoleResult* out) {
    if (g_require_mod && !g_mod_active)
        return reply(out, false, "Tick 'Console Commands (any mod)' in the launcher, or set require_mod=0 in mod\\TGCConvertPop.ini.");
    if (!rd<uintptr_t>(R(VA_G_WORLD))) return reply(out, false, "No game loaded.");
    return nullptr;
}

bool is_keep(const char* s) { return s && strcmp(s, "-") == 0; }
bool is_owner(const char* s) { return s && _stricmp(s, "owner") == 0; }

// ================= filters (culture / religion / pop type / pop id; all must match) =================
struct Filter {
    uintptr_t culture = 0, religion = 0;
    int type = -1;
    uint32_t ids[16];
    int n_ids = 0;
    bool accepted = false;        // keyword "accepted" (migrate_all_pops only)
    uintptr_t set[128];           // if n_set > 0: culture must be one of these (replaces 'culture')
    int n_set = 0;
    bool any() const { return culture || religion || type >= 0 || n_ids || n_set; }
    bool match(uintptr_t pop, int t) const {
        if (type >= 0 && t != type) return false;
        if (n_set) {
            uintptr_t c = rd<uintptr_t>(pop + 0x6C);
            bool hit = false;
            for (int k = 0; k < n_set && !hit; ++k) hit = set[k] == c;
            if (!hit) return false;
        } else if (culture && rd<uintptr_t>(pop + 0x6C) != culture) return false;
        if (religion && rd<uintptr_t>(pop + 0x70) != religion) return false;
        if (n_ids) {
            uint32_t pid = pop_id(pop);
            for (int k = 0; k < n_ids; ++k)
                if (ids[k] == pid) return true;
            return false;
        }
        return true;
    }
};

// Parses args[first..]; returns the offending token or nullptr.
const char* parse_filters(const ArgVector* args, int first, Filter* f) {
    for (int i = first; i < arg_count(args); ++i) {
        const char* a = arg_text(args, i);
        uint32_t num = 0;
        int ty;
        uintptr_t x;
        if (_stricmp(a, "accepted") == 0) {
            f->accepted = true;
        } else if (parse_uint(a, &num)) {
            if (f->n_ids < 16) f->ids[f->n_ids++] = num;
        } else if ((ty = find_poptype(a)) >= 0) {
            f->type = ty;
        } else if ((x = find_culture(a)) != 0) {
            f->culture = x;
        } else if ((x = find_religion(a)) != 0) {
            f->religion = x;
        } else {
            return a;
        }
    }
    return nullptr;
}

// ================= convert_pop =================
extern "C" ConsoleResult* __cdecl cmd_convert_pop(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    int argc = arg_count(args);
    if (argc < 1) return reply(out, false, "Usage: convert_pop <province id> [culture|-|owner] [religion|-|owner] [filters: culture/religion/pop type/pop id]");

    uint32_t id = 0;
    if (!parse_uint(arg_text(args, 0), &id)) return reply(out, false, "convert_pop: '%s' is not a province id.", arg_text(args, 0));
    uintptr_t prov = get_province(id);
    if (!prov) return reply(out, false, "convert_pop: province %u does not exist.", id);
    uintptr_t country = province_owner(prov);

    const char* a_cul = argc >= 2 ? arg_text(args, 1) : nullptr;
    const char* a_rel = argc >= 3 ? arg_text(args, 2) : nullptr;
    bool both_owner = argc == 1;

    uintptr_t culture = 0, religion = 0;
    if (both_owner || is_owner(a_cul)) {
        if (!country) return reply(out, false, "convert_pop: province %u has no owner.", id);
        culture = rd<uintptr_t>(country + 0xE64);
    } else if (a_cul && !is_keep(a_cul)) {
        culture = find_culture(a_cul);
        if (!culture) return reply(out, false, "convert_pop: unknown culture '%s'.", a_cul);
    }
    if (both_owner || is_owner(a_rel)) {
        if (!country) return reply(out, false, "convert_pop: province %u has no owner.", id);
        religion = rd<uintptr_t>(country + 0xE68);
    } else if (a_rel && !is_keep(a_rel)) {
        religion = find_religion(a_rel);
        if (!religion) return reply(out, false, "convert_pop: unknown religion '%s'.", a_rel);
    }
    if (!culture && !religion) return reply(out, false, "convert_pop: nothing to change.");
    Filter filter;
    if (const char* bad = parse_filters(args, 3, &filter))
        return reply(out, false, "convert_pop: filter '%s' is not a culture, religion, pop type or pop id.", bad);
    if (filter.accepted) return reply(out, false, "convert_pop: 'accepted' only works with migrate_all_pops.");

    int pops = 0, changed = 0;
    uint64_t people = 0;
    if (!for_each_pop(prov, [&](uintptr_t pop, int t) {
            if (!filter.match(pop, t)) return;
            ++pops;
            people += rd<uint32_t>(pop + 0x58);
            bool c = false;
            if (culture && rd<uintptr_t>(pop + 0x6C) != culture) { *reinterpret_cast<uintptr_t*>(pop + 0x6C) = culture; c = true; }
            if (religion && rd<uintptr_t>(pop + 0x70) != religion) { *reinterpret_cast<uintptr_t*>(pop + 0x70) = religion; c = true; }
            if (c) ++changed;
        }))
        return reply(out, false, "convert_pop: province %u has no pops.", id);

    logf("convert_pop %u: %d/%d pops (culture %s, religion %s)", id, changed, pops,
         culture ? culture_name(culture) : "kept", religion ? religion_name(religion) : "kept");
    return reply(out, true, "Province %u: converted %d of %d %spops (%llu people) to %s / %s.", id, changed, pops, filter.any() ? "matching " : "",
                 (unsigned long long)people, culture ? culture_name(culture) : "(culture kept)",
                 religion ? religion_name(religion) : "(religion kept)");
}


// ================= double_pop / scale_pop =================
// Pop_SetSize 0x00955FD0: ESI = pop, EAX = new size (clamped 0..2,000,000 by the game; it keeps the
// previous size at +0x5C for the growth display and fixes employment when a pop shrinks).
constexpr uintptr_t VA_POP_SETSIZE = 0x00955FD0;

void scale_pops(const uintptr_t* pops, int n, uint32_t pct, uint64_t* before, uint64_t* after, int* capped) {
    for (int i = 0; i < n; ++i) {
        uint32_t size = rd<uint32_t>(pops[i] + 0x58);
        uint64_t want = (uint64_t)size * pct / 100;
        if (want > 2000000) { want = 2000000; ++*capped; }
        *before += size;
        tcp_setsize_thunk(R(VA_POP_SETSIZE), pops[i], (uint32_t)want);
        *after += rd<uint32_t>(pops[i] + 0x58);
    }
}

ConsoleResult* scale_cmd(ConsoleResult* out, ArgVector* args, bool is_double) {
    const char* cmd = is_double ? "double_pop" : "scale_pop";
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    int argc = arg_count(args);
    int need = is_double ? 1 : 2;
    if (argc < need)
        return reply(out, false, is_double ? "Usage: double_pop <province id> [filters: culture/religion/pop type/pop id]"
                                           : "Usage: scale_pop <province id> <percent, 200 = double, 50 = half> [filters]");
    uint32_t id = 0, pct = 200;
    if (!parse_uint(arg_text(args, 0), &id)) return reply(out, false, "%s: '%s' is not a province id.", cmd, arg_text(args, 0));
    if (!is_double && (!parse_uint(arg_text(args, 1), &pct) || pct < 1 || pct > 1000))
        return reply(out, false, "scale_pop: percent must be 1..1000 (200 = double, 50 = half).");
    uintptr_t prov = get_province(id);
    if (!prov) return reply(out, false, "%s: province %u does not exist.", cmd, id);
    Filter filter;
    if (const char* bad = parse_filters(args, need, &filter))
        return reply(out, false, "%s: filter '%s' is not a culture, religion, pop type or pop id.", cmd, bad);
    if (filter.accepted) return reply(out, false, "%s: 'accepted' only works with migrate_all_pops.", cmd);

    uintptr_t pops[4096];
    int n = 0;
    if (!for_each_pop(prov, [&](uintptr_t pop, int t) {
            if (n < 4096 && pop_alive(pop) && filter.match(pop, t)) pops[n++] = pop;
        }))
        return reply(out, false, "%s: province %u has no pops.", cmd, id);
    if (!n) return reply(out, false, "%s: no %spops in province %u.", cmd, filter.any() ? "matching " : "", id);

    uint64_t before = 0, after = 0;
    int capped = 0;
    scale_pops(pops, n, pct, &before, &after, &capped);
    logf("%s %u (%u%%): %d pops, %llu -> %llu", cmd, id, pct, n, (unsigned long long)before, (unsigned long long)after);
    return reply(out, true, "Province %u: %d %spops x%u%%: %llu -> %llu people.%s", id, n, filter.any() ? "matching " : "", pct,
                 (unsigned long long)before, (unsigned long long)after,
                 capped ? " (some pops hit the game's 2,000,000 limit)" : "");
}
extern "C" ConsoleResult* __cdecl cmd_double_pop(ConsoleResult* out, ArgVector* args) { return scale_cmd(out, args, true); }
extern "C" ConsoleResult* __cdecl cmd_scale_pop(ConsoleResult* out, ArgVector* args) { return scale_cmd(out, args, false); }

// ================= list_prov_cultures / list_prov_religions =================
struct Tally {
    uintptr_t key;
    int pops;
    uint64_t people;
};

ConsoleResult* list_province(ConsoleResult* out, ArgVector* args, bool religions) {
    const char* cmd = religions ? "list_prov_religions" : "list_prov_cultures";
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    uint32_t id = 0;
    if (arg_count(args) < 1 || !parse_uint(arg_text(args, 0), &id)) return reply(out, false, "Usage: %s <province id>", cmd);
    uintptr_t prov = get_province(id);
    if (!prov) return reply(out, false, "%s: province %u does not exist.", cmd, id);

    static Text t;
    t.n = 0;
    t.truncated = false;
    static Tally tally[256];
    int ntally = 0, pops = 0;
    uint64_t people = 0;

    // Pass 1: totals.
    for_each_pop(prov, [&](uintptr_t pop, int) {
        uintptr_t key = rd<uintptr_t>(pop + (religions ? 0x70 : 0x6C));
        uint32_t size = rd<uint32_t>(pop + 0x58);
        ++pops;
        people += size;
        int i = 0;
        while (i < ntally && tally[i].key != key) ++i;
        if (i == ntally && ntally < 256) tally[ntally++] = {key, 0, 0};
        if (i < 256) { tally[i].pops++; tally[i].people += size; }
    });

    t.add("Province %u: %d pops, %llu people.\n", id, pops, (unsigned long long)people);
    for (int i = 0; i < ntally; ++i)
        t.add("  %s: %d pops, %llu people (%.1f%%)\n", religions ? religion_name(tally[i].key) : culture_name(tally[i].key),
              tally[i].pops, (unsigned long long)tally[i].people, people ? 100.0 * (double)tally[i].people / (double)people : 0.0);
    t.add("pop id | type | %s | size\n", religions ? "religion" : "culture");

    // Pass 2: every pop.
    logf("%s %u:", cmd, id);
    for_each_pop(prov, [&](uintptr_t pop, int type) {
        uintptr_t key = rd<uintptr_t>(pop + (religions ? 0x70 : 0x6C));
        const char* name = religions ? religion_name(key) : culture_name(key);
        t.add("%u | %s | %s | %u\n", pop_id(pop), poptype_name(poptype_at(type)), name, rd<uint32_t>(pop + 0x58));
        logf("  %u %s %s/%s %u", pop_id(pop), poptype_name(poptype_at(type)), culture_name(rd<uintptr_t>(pop + 0x6C)),
             religion_name(rd<uintptr_t>(pop + 0x70)), rd<uint32_t>(pop + 0x58));
    });
    if (t.n && t.buf[t.n - 1] == '\n') t.buf[--t.n] = 0;
    return finish(out, true, t);
}

extern "C" ConsoleResult* __cdecl cmd_list_cultures(ConsoleResult* out, ArgVector* args) { return list_province(out, args, false); }
extern "C" ConsoleResult* __cdecl cmd_list_religions(ConsoleResult* out, ArgVector* args) { return list_province(out, args, true); }

// ================= migrate_pop =================
extern "C" ConsoleResult* __cdecl cmd_migrate_pop(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    int argc = arg_count(args);
    if (argc < 3)
        return reply(out, false, "Usage: migrate_pop <from province> <to province> <culture|religion|pop type|pop id> [more filters]");

    uint32_t from_id = 0, to_id = 0;
    if (!parse_uint(arg_text(args, 0), &from_id) || !parse_uint(arg_text(args, 1), &to_id))
        return reply(out, false, "migrate_pop: the first two arguments must be province ids.");
    if (from_id == to_id) return reply(out, false, "migrate_pop: source and destination are the same province.");
    uintptr_t from = get_province(from_id), to = get_province(to_id);
    if (!from) return reply(out, false, "migrate_pop: province %u does not exist.", from_id);
    if (!to) return reply(out, false, "migrate_pop: province %u does not exist.", to_id);
    if (!rd<uintptr_t>(to + 0x194)) return reply(out, false, "migrate_pop: province %u cannot hold pops (sea or wasteland?).", to_id);

    if (!g_merge_fix_ok) return reply(out, false, "migrate_pop: disabled, the pop merge fix could not be installed (see the_grand_loader.log).");
    Filter filter;
    if (const char* bad = parse_filters(args, 2, &filter))
        return reply(out, false, "migrate_pop: '%s' is not a culture, religion, pop type or pop id.", bad);
    if (filter.accepted) return reply(out, false, "migrate_pop: 'accepted' only works with migrate_all_pops.");

    // Collect first: moving changes the lists we walk.
    static uintptr_t picked[4096];
    int npicked = 0;
    uint64_t people = 0;
    for_each_pop(from, [&](uintptr_t pop, int type) {
        if (!filter.match(pop, type)) return;
        if (npicked < 4096) picked[npicked++] = pop;
    });
    if (npicked == 0) return reply(out, false, "migrate_pop: no pop in province %u matches.", from_id);

    int moved = 0;
    tcp_strict_merge = 1;  // merge only into a pop of the same type, culture AND religion
    for (int i = 0; i < npicked; ++i) {
        uintptr_t pop = picked[i];
        if (!pop_alive(pop) || rd<uintptr_t>(pop + 0x64) != from) continue;
        uint32_t size = rd<uint32_t>(pop + 0x58);
        tcp_move_thunk(R(VA_POP_MOVE), (void*)to, (void*)pop);
        ++moved;
        people += size;
    }
    tcp_strict_merge = g_fix_merge_everywhere ? 1 : 0;
    logf("migrate_pop %u -> %u: %d pops, %llu people", from_id, to_id, moved, (unsigned long long)people);
    return reply(out, moved > 0, "Moved %d pops (%llu people) from province %u to %u.%s", moved,
                 (unsigned long long)people, from_id, to_id,
                 moved ? " Pops merge only with a pop of the same type, culture and religion." : "");
}

// ================= migrate_all_pops =================
// migrate_all_pops <FROM tag> <TO tag> [filters]
//   Takes every pop owned by FROM whose culture is TO's primary culture (or the filters given) and
//   spreads them over TO's provinces so every province receives about the same number of people
//   (whole pops, biggest first, each to the province that has received the least so far).
constexpr uintptr_t VA_COUNTRY_BY_TAG = 0x0055C6C0;  // stdcall (mgr, out {tag, index}*, const char* tag)
typedef void(__stdcall* CountryByTagFn)(void* mgr, uint32_t* out, const char* tag);

int country_index(const char* tag_in) {
    char tag[4] = {0};
    if (!tag_in || strlen(tag_in) != 3) return -1;
    for (int i = 0; i < 3; ++i) tag[i] = (char)toupper((unsigned char)tag_in[i]);
    void* mgr = rd<void*>(R(VA_G_COUNTRIES));
    if (!mgr) return -1;
    uint32_t out[2] = {0, 0};
    reinterpret_cast<CountryByTagFn>(R(VA_COUNTRY_BY_TAG))(mgr, out, tag);
    return out[1] ? (int)out[1] : -1;
}
uintptr_t country_at(int index) {
    uintptr_t mgr = rd<uintptr_t>(R(VA_G_COUNTRIES));
    return mgr ? rd<uintptr_t*>(mgr + 4)[index] : 0;
}
// Owned provinces: vector of province ids at country+0x9D8/+0x9DC (checked against province owner).
int owned_provinces(int index, uintptr_t* out, int max) {
    uintptr_t c = country_at(index);
    if (!c) return 0;
    uint32_t* b = rd<uint32_t*>(c + 0x9D8);
    uint32_t* e = rd<uint32_t*>(c + 0x9DC);
    int n = 0;
    for (uint32_t* it = b; b && it < e && n < max; ++it) {
        uintptr_t p = get_province(*it);
        if (p && rd<uint32_t>(p + 0x12C) == (uint32_t)index && rd<uintptr_t>(p + 0x194)) out[n++] = p;
    }
    return n;
}

extern "C" ConsoleResult* __cdecl cmd_migrate_all_pops(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (!g_merge_fix_ok) return reply(out, false, "migrate_all_pops: disabled, the pop merge fix could not be installed.");
    if (arg_count(args) < 2)
        return reply(out, false, "Usage: migrate_all_pops <FROM tag> <TO tag> [accepted] [culture|religion|pop type filters]  e.g. migrate_all_pops GER POL accepted");

    const char* from_tag = arg_text(args, 0);
    const char* to_tag = arg_text(args, 1);
    int to_i = country_index(to_tag), from_i = country_index(from_tag);
    if (to_i < 0) return reply(out, false, "migrate_all_pops: unknown country '%s'.", to_tag);
    if (from_i < 0) return reply(out, false, "migrate_all_pops: unknown country '%s'.", from_tag);
    if (to_i == from_i) return reply(out, false, "migrate_all_pops: the two countries are the same.");

    Filter filter;
    if (const char* bad = parse_filters(args, 2, &filter))
        return reply(out, false, "migrate_all_pops: '%s' is not a culture, religion or pop type.", bad);
    if (filter.n_ids) return reply(out, false, "migrate_all_pops: use migrate_pop for single pop ids.");
    uintptr_t to_country = country_at(to_i);
    if (!filter.culture) filter.culture = rd<uintptr_t>(to_country + 0xE64);
    if (!filter.culture) return reply(out, false, "migrate_all_pops: %s has no primary culture.", to_tag);
    char label[256];
    snprintf(label, sizeof(label), "%s", culture_name(filter.culture));
    if (filter.accepted) {
        // The given (or primary) culture plus TO's accepted cultures: list {value, prev, next, flag} at +0xE54.
        filter.set[filter.n_set++] = filter.culture;
        int guard = 0;
        for (uintptr_t node = rd<uintptr_t>(to_country + 0xE54); node && guard < 1000 && filter.n_set < 128;
             node = rd<uintptr_t>(node + 8), ++guard) {
            uintptr_t c = rd<uintptr_t>(node);
            bool dup = false;
            for (int k = 0; k < filter.n_set; ++k) dup |= filter.set[k] == c;
            if (c && !dup) {
                filter.set[filter.n_set++] = c;
                size_t l = strlen(label);
                snprintf(label + l, sizeof(label) - l, "+%s", culture_name(c));
            }
        }
    }

    static uintptr_t dest[4096], src[4096];
    int ndest = owned_provinces(to_i, dest, 4096);
    int nsrc = owned_provinces(from_i, src, 4096);
    if (ndest == 0) return reply(out, false, "migrate_all_pops: %s owns no provinces.", to_tag);
    if (nsrc == 0) return reply(out, false, "migrate_all_pops: %s owns no provinces.", from_tag);

    // Collect.
    static uintptr_t picked[16384];
    static uint32_t psize[16384];
    int npicked = 0;
    bool overflow = false;
    for (int i = 0; i < nsrc; ++i)
        for_each_pop(src[i], [&](uintptr_t pop, int type) {
            if (!filter.match(pop, type)) return;
            if (npicked < 16384) { picked[npicked] = pop; psize[npicked] = rd<uint32_t>(pop + 0x58); ++npicked; }
            else overflow = true;
        });
    if (npicked == 0)
        return reply(out, false, "migrate_all_pops: no %s pops in %s's %d provinces.", label, from_tag, nsrc);

    // Biggest first (insertion sort is fine for a few thousand pops).
    for (int i = 1; i < npicked; ++i) {
        uintptr_t p = picked[i];
        uint32_t sz = psize[i];
        int j = i - 1;
        while (j >= 0 && psize[j] < sz) { picked[j + 1] = picked[j]; psize[j + 1] = psize[j]; --j; }
        picked[j + 1] = p;
        psize[j + 1] = sz;
    }

    static uint64_t received[4096];
    for (int i = 0; i < ndest; ++i) received[i] = 0;
    int moved = 0;
    uint64_t people = 0;
    tcp_strict_merge = 1;
    for (int i = 0; i < npicked; ++i) {
        uintptr_t pop = picked[i];
        if (!pop_alive(pop)) continue;
        int best = 0;
        for (int d = 1; d < ndest; ++d)
            if (received[d] < received[best]) best = d;
        tcp_move_thunk(R(VA_POP_MOVE), (void*)dest[best], (void*)pop);
        received[best] += psize[i];
        people += psize[i];
        ++moved;
    }
    tcp_strict_merge = g_fix_merge_everywhere ? 1 : 0;

    uint64_t lo = received[0], hi = received[0];
    for (int i = 1; i < ndest; ++i) { if (received[i] < lo) lo = received[i]; if (received[i] > hi) hi = received[i]; }
    logf("migrate_all_pops %s <- %s: %d %s pops, %llu people over %d provinces (min %llu, max %llu)", to_tag, from_tag, moved,
         label, (unsigned long long)people, ndest, (unsigned long long)lo, (unsigned long long)hi);
    return reply(out, true,
                 "Moved %d %s pops (%llu people) from %s (%d provinces) to %s (%d provinces): %llu to %llu people per province.%s",
                 moved, label, (unsigned long long)people, from_tag, nsrc, to_tag, ndest,
                 (unsigned long long)lo, (unsigned long long)hi, overflow ? " (over 16384 pops: run it again for the rest)" : "");
}

// ================= accept_culture / unaccept_culture =================
// Country accepted cultures: list {culture, prev, next, flag} head/tail/count at +0xE54/+0xE58/+0xE5C.
// Add  = Country_AddAcceptedCulture 0x00522310 (usercall ESI = country, push culture, ret 4)
// Drop = List_Remove 0x005F2AE0 (usercall ESI = &country->list, push node, ret 4), as remove_accepted_culture does.
constexpr uintptr_t VA_ADD_ACCEPTED = 0x00522310;
constexpr uintptr_t VA_LIST_REMOVE = 0x005F2AE0;

void accepted_list_text(uintptr_t country, char* buf, size_t n) {
    buf[0] = 0;
    size_t len = 0;
    int guard = 0;
    for (uintptr_t node = rd<uintptr_t>(country + 0xE54); node && guard < 1000; node = rd<uintptr_t>(node + 8), ++guard) {
        int w = snprintf(buf + len, n - len, "%s%s", len ? ", " : "", culture_name(rd<uintptr_t>(node)));
        if (w < 0 || len + (size_t)w >= n) break;
        len += (size_t)w;
    }
    if (!len) snprintf(buf, n, "(none)");
}

ConsoleResult* culture_cmd(ConsoleResult* out, ArgVector* args, bool add) {
    const char* cmd = add ? "accept_culture" : "unaccept_culture";
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    int argc = arg_count(args);
    if (argc < 1) return reply(out, false, "Usage: %s <TAG> <culture> [more cultures]  (only the TAG lists them)", cmd);
    const char* tag = arg_text(args, 0);
    int ci = country_index(tag);
    if (ci < 0) return reply(out, false, "%s: unknown country '%s'.", cmd, tag);
    uintptr_t country = country_at(ci);
    if (!country) return reply(out, false, "%s: country '%s' not loaded.", cmd, tag);

    static char before[2048], after[2048];
    accepted_list_text(country, before, sizeof(before));
    uintptr_t primary = rd<uintptr_t>(country + 0xE64);
    if (argc == 1)
        return reply(out, true, "%s: primary %s; accepted: %s", tag, culture_name(primary), before);

    int changed = 0;
    for (int i = 1; i < argc; ++i) {
        const char* name = arg_text(args, i);
        uintptr_t c = find_culture(name);
        if (!c) return reply(out, false, "%s: unknown culture '%s' (nothing after it was changed).", cmd, name);
        uintptr_t node = 0;
        int guard = 0;
        for (uintptr_t n = rd<uintptr_t>(country + 0xE54); n && guard < 1000; n = rd<uintptr_t>(n + 8), ++guard)
            if (rd<uintptr_t>(n) == c) { node = n; break; }
        if (add) {
            if (c == primary || node) continue;
            tcp_esi_call_thunk(R(VA_ADD_ACCEPTED), country, c);
            ++changed;
        } else {
            if (!node) continue;
            tcp_esi_call_thunk(R(VA_LIST_REMOVE), country + 0xE54, node);
            ++changed;
        }
    }
    accepted_list_text(country, after, sizeof(after));
    logf("%s %s: %d change(s); accepted now: %s", cmd, tag, changed, after);
    return reply(out, true, "%s: %d change(s). Accepted cultures now: %s", tag, changed, after);
}
extern "C" ConsoleResult* __cdecl cmd_accept_culture(ConsoleResult* out, ArgVector* args) { return culture_cmd(out, args, true); }
extern "C" ConsoleResult* __cdecl cmd_unaccept_culture(ConsoleResult* out, ArgVector* args) { return culture_cmd(out, args, false); }


// ================= scale_country_pop =================
extern "C" ConsoleResult* __cdecl cmd_scale_country_pop(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (arg_count(args) < 2)
        return reply(out, false, "Usage: scale_country_pop <TAG> <percent, 200 = double> [filters: culture/religion/pop type]  e.g. scale_country_pop SER 200 serb");
    const char* tag = arg_text(args, 0);
    int ci = country_index(tag);
    if (ci < 0) return reply(out, false, "scale_country_pop: unknown country '%s'.", tag);
    uint32_t pct = 0;
    if (!parse_uint(arg_text(args, 1), &pct) || pct < 1 || pct > 1000)
        return reply(out, false, "scale_country_pop: percent must be 1..1000 (200 = double, 50 = half).");
    Filter filter;
    if (const char* bad = parse_filters(args, 2, &filter))
        return reply(out, false, "scale_country_pop: '%s' is not a culture, religion or pop type.", bad);
    if (filter.accepted || filter.n_ids) return reply(out, false, "scale_country_pop: filters are culture, religion and pop type.");

    static uintptr_t provs[4096];
    int np = owned_provinces(ci, provs, 4096);
    if (!np) return reply(out, false, "scale_country_pop: %s owns no provinces.", tag);
    static uintptr_t pops[65536];
    int n = 0;
    for (int i = 0; i < np; ++i)
        for_each_pop(provs[i], [&](uintptr_t pop, int t) {
            if (n < 65536 && pop_alive(pop) && filter.match(pop, t)) pops[n++] = pop;
        });
    if (!n) return reply(out, false, "scale_country_pop: no %spops in %s's %d provinces.", filter.any() ? "matching " : "", tag, np);
    uint64_t before = 0, after = 0;
    int capped = 0;
    scale_pops(pops, n, pct, &before, &after, &capped);
    logf("scale_country_pop %s %u%%: %d pops in %d provinces, %llu -> %llu", tag, pct, n, np, (unsigned long long)before,
         (unsigned long long)after);
    return reply(out, true, "%s: %d %spops in %d provinces x%u%%: %llu -> %llu people.%s", tag, n, filter.any() ? "matching " : "", np, pct,
                 (unsigned long long)before, (unsigned long long)after, capped ? " (some pops hit the game's 2,000,000 limit)" : "");
}

// ================= add_core / remove_core =================
// Same calls as the add_core / remove_core script effects (0x0087E760 / 0x0087F510):
// __stdcall fn(Province*, CTag* {tag, country index}, bool history) then country+0x1434 = 1 (refresh).
// Cores of a province: vector of CTag (8 bytes, index at +4) at province +0x148/+0x14C.
constexpr uintptr_t VA_ADD_CORE = 0x004B61A0, VA_REMOVE_CORE = 0x004B6290;
typedef void(__stdcall* CoreFn)(uintptr_t prov, uint32_t* tag, uint32_t history);

bool province_has_core(uintptr_t prov, uint32_t index) {
    uint32_t* b = rd<uint32_t*>(prov + 0x148);
    uint32_t* e = rd<uint32_t*>(prov + 0x14C);
    for (uint32_t* it = b; b && it < e; it += 2)
        if (it[1] == index) return true;
    return false;
}

ConsoleResult* core_cmd(ConsoleResult* out, ArgVector* args, bool add) {
    const char* cmd = add ? "add_core" : "remove_core";
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (arg_count(args) < 2) return reply(out, false, "Usage: %s <TAG> <province id> [more province ids]", cmd);
    char tag[4] = {0};
    const char* tag_in = arg_text(args, 0);
    if (strlen(tag_in) != 3) return reply(out, false, "%s: '%s' is not a country tag.", cmd, tag_in);
    for (int i = 0; i < 3; ++i) tag[i] = (char)toupper((unsigned char)tag_in[i]);
    void* mgr = rd<void*>(R(VA_G_COUNTRIES));
    uint32_t ctag[2] = {0, 0};
    if (mgr) reinterpret_cast<CountryByTagFn>(R(VA_COUNTRY_BY_TAG))(mgr, ctag, tag);
    if (!ctag[1]) return reply(out, false, "%s: unknown country '%s'.", cmd, tag);
    CoreFn fn = reinterpret_cast<CoreFn>(R(add ? VA_ADD_CORE : VA_REMOVE_CORE));

    Text t;
    int done = 0, skipped = 0;
    for (int i = 1; i < arg_count(args); ++i) {
        uint32_t id = 0;
        uintptr_t prov = parse_uint(arg_text(args, i), &id) ? get_province(id) : 0;
        if (!prov) { t.add("'%s' is not a province id.\n", arg_text(args, i)); ++skipped; continue; }
        if (province_has_core(prov, ctag[1]) == add) {
            t.add("%u: %s %s a core there.\n", id, tag, add ? "already has" : "has no");
            ++skipped;
            continue;
        }
        uint32_t c[2] = {ctag[0], ctag[1]};
        fn(prov, c, 1);
        ++done;
        t.add("%u: %s core %s.\n", id, tag, add ? "added" : "removed");
    }
    if (done) {
        uintptr_t country = country_at((int)ctag[1]);
        if (country) *reinterpret_cast<uint8_t*>(country + 0x1434) = 1;
    }
    logf("%s %s: %d changed, %d skipped", cmd, tag, done, skipped);
    t.add("%s %s: %d province(s) changed.", cmd, tag, done);
    return finish(out, done > 0, t);
}
extern "C" ConsoleResult* __cdecl cmd_add_core(ConsoleResult* out, ArgVector* args) { return core_cmd(out, args, true); }
extern "C" ConsoleResult* __cdecl cmd_remove_core(ConsoleResult* out, ArgVector* args) { return core_cmd(out, args, false); }


// ================= blocs (mega alliances) =================
// A bloc is a set of countries carrying the country flag "bloc_<name>" (so it is saved in the save game and
// visible to events through has_country_flag). Whenever the game adds a bloc member to a war side with the
// "real join" flag (declaration, its allies, call-ally accepts), every other living member of that bloc is
// added to the same side right after, unless it is already in that war. Loading a save or crisis/rebel wars
// use the flag 0 and are left alone. The game's own alliances are untouched.
//
// War: +0x2C attackers, +0x3C defenders (vectors of CTag {char tag[4], int index}).
// War_AddAttacker 0x00934280 / War_AddDefender 0x00935390: __stdcall (War*, bool join, bool b, CTag by value).
// Country flags: CFlags at country+0x1B0; vt+8 find(const char*) -> entry (GStr name at +0, bool set +0x1C);
// 0x0043C450 sets a flag (ESI = flags, EAX = name). Flags live in a ternary trie: root at flags+4, node =
// {entry, char, lo, hi, eq}.
constexpr uintptr_t VA_WAR_ADD_ATT = 0x00934280, VA_WAR_ADD_DEF = 0x00935390, VA_FLAG_SET = 0x0043C450;
typedef uint32_t(__stdcall* WarAddFn)(uintptr_t war, uint32_t join, uint32_t b, uint32_t tag, uint32_t index);
WarAddFn g_add_att = nullptr, g_add_def = nullptr;  // trampolines to the originals
bool g_blocs_ok = false;
}  // namespace
extern "C" void tcp_flagset_thunk(uintptr_t fn, uintptr_t flags, const char* name);
asm(R"(
    .text
    .globl _tcp_flagset_thunk
_tcp_flagset_thunk:
    pushl %ebp
    movl  %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl  12(%ebp), %esi
    movl  16(%ebp), %eax
    call  *8(%ebp)
    popl  %edi
    popl  %esi
    popl  %ebx
    popl  %ebp
    ret
)");
namespace {

uintptr_t country_flags(uintptr_t country) { return country + 0x1B0; }
uintptr_t flag_find(uintptr_t country, const char* name) {
    uintptr_t f = country_flags(country);
    typedef uintptr_t(__fastcall * FindFn)(uintptr_t, void*, const char*);
    FindFn fn = reinterpret_cast<FindFn>(rd<uintptr_t>(rd<uintptr_t>(f) + 8));
    return fn(f, nullptr, name);
}
void flag_set(uintptr_t country, const char* name, bool on) {
    uintptr_t e = flag_find(country, name);
    if (e) { *reinterpret_cast<uint8_t*>(e + 0x1C) = on ? 1 : 0; return; }
    if (on) tcp_flagset_thunk(R(VA_FLAG_SET), country_flags(country), name);
}
// Calls fn(name) for every flag that is set.
template <typename F>
void flag_walk(uintptr_t node, F& fn, int depth) {
    if (!node || depth > 200) return;
    uintptr_t e = rd<uintptr_t>(node);
    if (e && rd<uint8_t>(e + 0x1C)) fn(gs_cstr(reinterpret_cast<const GameString*>(e)));
    flag_walk(rd<uintptr_t>(node + 8), fn, depth + 1);
    flag_walk(rd<uintptr_t>(node + 0xC), fn, depth + 1);
    flag_walk(rd<uintptr_t>(node + 0x10), fn, depth + 1);
}
// The bloc of a country ("" if none), copied into out.
bool country_bloc(uintptr_t country, char* out, size_t n) {
    out[0] = 0;
    if (!country) return false;
    auto fn = [&](const char* name) {
        if (!out[0] && _strnicmp(name, "bloc_", 5) == 0 && name[5]) snprintf(out, n, "%s", name + 5);
    };
    flag_walk(rd<uintptr_t>(country_flags(country) + 4), fn, 0);
    return out[0] != 0;
}
bool country_alive(uintptr_t country) { return country && rd<uintptr_t>(country + 0x9DC) > rd<uintptr_t>(country + 0x9D8); }

// Every real country: {tag chars, index} from the tag hash table (64 buckets of CTag vectors at mgr+0x14).
template <typename F>
void for_each_tag(F fn) {
    uintptr_t mgr = rd<uintptr_t>(R(VA_G_COUNTRIES));
    if (!mgr) return;
    for (int b = 0; b < 64; ++b) {
        uint32_t* it = rd<uint32_t*>(mgr + 0x14 + b * 0x10);
        uint32_t* end = rd<uint32_t*>(mgr + 0x18 + b * 0x10);
        for (; it && it < end; it += 2)
            if (it[1]) fn(it[0], it[1]);
    }
}
const char* tag_text(uint32_t tag, char* buf) { memcpy(buf, &tag, 3); buf[3] = 0; return buf; }

bool war_has(uintptr_t war, int side_off, uint32_t index) {
    uint32_t* b = rd<uint32_t*>(war + side_off);
    uint32_t* e = rd<uint32_t*>(war + side_off + 4);
    for (uint32_t* it = b; b && it < e; it += 2)
        if (it[1] == index) return true;
    return false;
}
uint32_t war_leader(uintptr_t war, int side_off) {
    uint32_t* b = rd<uint32_t*>(war + side_off);
    uint32_t* e = rd<uint32_t*>(war + side_off + 4);
    return (b && b < e) ? b[1] : 0;
}

// Adds the bloc mates of `index` to the given side of `war`.
void bloc_pull(uintptr_t war, bool attackers, uint32_t index) {
    char bloc[64];
    if (!country_bloc(country_at((int)index), bloc, sizeof(bloc))) return;
    // members of the same bloc fighting each other: the bloc stays out
    char other[64];
    uint32_t enemy = war_leader(war, attackers ? 0x3C : 0x2C);
    if (enemy && country_bloc(country_at((int)enemy), other, sizeof(other)) && _stricmp(other, bloc) == 0) {
        logf("bloc %s: its members lead both sides of a war, nobody pulled in", bloc);
        return;
    }
    int added = 0;
    for_each_tag([&](uint32_t tag, uint32_t idx) {
        if (idx == index || war_has(war, 0x2C, idx) || war_has(war, 0x3C, idx)) return;
        uintptr_t c = country_at((int)idx);
        char b2[64];
        if (!country_alive(c) || !country_bloc(c, b2, sizeof(b2)) || _stricmp(b2, bloc) != 0) return;
        (attackers ? g_add_att : g_add_def)(war, 1, 0, tag, idx);
        char t[4];
        logf("bloc %s: %s joins the war on the %s side", bloc, tag_text(tag, t), attackers ? "attacker" : "defender");
        ++added;
    });
    (void)added;
}

// A declaration adds the attacker before the defender exists: remember it until the defender is added.
struct Pending { uintptr_t war; uint32_t index; };
Pending g_pending[16];
int g_npending = 0;

uint32_t __stdcall war_add_att_hook(uintptr_t war, uint32_t join, uint32_t b, uint32_t tag, uint32_t index) {
    if (war_has(war, 0x3C, index)) return 0;  // never on both sides
    uint32_t r = g_add_att(war, join, b, tag, index);
    if (!(join & 0xFF)) return r;
    if (!war_leader(war, 0x3C)) {
        if (g_npending < 16) g_pending[g_npending++] = {war, index};
        return r;
    }
    bloc_pull(war, true, index);
    return r;
}
uint32_t __stdcall war_add_def_hook(uintptr_t war, uint32_t join, uint32_t b, uint32_t tag, uint32_t index) {
    if (war_has(war, 0x2C, index)) return 0;
    uint32_t r = g_add_def(war, join, b, tag, index);
    if (!(join & 0xFF)) return r;
    for (int i = 0; i < g_npending;) {
        if (g_pending[i].war == war) {
            uint32_t att = g_pending[i].index;
            g_pending[i] = g_pending[--g_npending];
            bloc_pull(war, true, att);
        } else {
            ++i;
        }
    }
    bloc_pull(war, false, index);
    return r;
}

bool valid_bloc_name(const char* s) {
    size_t n = strlen(s);
    if (n < 1 || n > 24) return false;
    for (size_t i = 0; i < n; ++i)
        if (!isalnum((unsigned char)s[i]) && s[i] != '_') return false;
    return true;
}
void lower(char* s) { for (; *s; ++s) *s = (char)tolower((unsigned char)*s); }

extern "C" ConsoleResult* __cdecl cmd_bloc_add(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (!g_blocs_ok) return reply(out, false, "bloc_add: disabled, the war hooks could not be installed (see the log).");
    if (arg_count(args) < 2) return reply(out, false, "Usage: bloc_add <bloc name> <TAG> [more tags]  e.g. bloc_add central_powers GER AUS TUR");
    char name[32];
    snprintf(name, sizeof(name), "%s", arg_text(args, 0));
    lower(name);
    if (!valid_bloc_name(name)) return reply(out, false, "bloc_add: the name may use letters, digits and _ (max 24).");
    char flag[40];
    snprintf(flag, sizeof(flag), "bloc_%s", name);
    Text t;
    int done = 0;
    for (int i = 1; i < arg_count(args); ++i) {
        const char* tag = arg_text(args, i);
        int ci = country_index(tag);
        if (ci < 0) { t.add("%s: unknown country.\n", tag); continue; }
        uintptr_t c = country_at(ci);
        char cur[64];
        if (country_bloc(c, cur, sizeof(cur))) {
            if (_stricmp(cur, name) == 0) t.add("%s is already in %s.\n", tag, name);
            else t.add("%s is in bloc %s: bloc_remove %s first.\n", tag, cur, tag);
            continue;
        }
        flag_set(c, flag, true);
        ++done;
        t.add("%s joins %s.\n", tag, name);
    }
    logf("bloc_add %s: %d countries", name, done);
    t.add("Bloc %s: %d added. In a war every living member joins the side of the member that is in it.", name, done);
    return finish(out, done > 0, t);
}

extern "C" ConsoleResult* __cdecl cmd_bloc_remove(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (arg_count(args) < 1) return reply(out, false, "Usage: bloc_remove <TAG> [more tags]");
    Text t;
    int done = 0;
    for (int i = 0; i < arg_count(args); ++i) {
        const char* tag = arg_text(args, i);
        int ci = country_index(tag);
        if (ci < 0) { t.add("%s: unknown country.\n", tag); continue; }
        uintptr_t c = country_at(ci);
        char cur[64], flag[80];
        if (!country_bloc(c, cur, sizeof(cur))) { t.add("%s is in no bloc.\n", tag); continue; }
        snprintf(flag, sizeof(flag), "bloc_%s", cur);
        flag_set(c, flag, false);
        ++done;
        t.add("%s left %s.\n", tag, cur);
    }
    return finish(out, done > 0, t);
}

extern "C" ConsoleResult* __cdecl cmd_bloc_disband(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (arg_count(args) < 1) return reply(out, false, "Usage: bloc_disband <bloc name>");
    char name[32], flag[40];
    snprintf(name, sizeof(name), "%s", arg_text(args, 0));
    lower(name);
    snprintf(flag, sizeof(flag), "bloc_%s", name);
    int n = 0;
    for_each_tag([&](uint32_t, uint32_t idx) {
        uintptr_t c = country_at((int)idx);
        uintptr_t e = c ? flag_find(c, flag) : 0;
        if (e && rd<uint8_t>(e + 0x1C)) { *reinterpret_cast<uint8_t*>(e + 0x1C) = 0; ++n; }
    });
    return reply(out, n > 0, n ? "Bloc %s disbanded (%d members)." : "No bloc called %s.", name, n);
}

extern "C" ConsoleResult* __cdecl cmd_bloc_list(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    const char* only = arg_count(args) >= 1 ? arg_text(args, 0) : nullptr;
    struct Entry { char bloc[32]; char tag[4]; bool alive; };
    static Entry list[1024];
    int n = 0;
    for_each_tag([&](uint32_t tag, uint32_t idx) {
        uintptr_t c = country_at((int)idx);
        char b[64];
        if (n < 1024 && country_bloc(c, b, sizeof(b)) && (!only || _stricmp(only, b) == 0)) {
            snprintf(list[n].bloc, sizeof(list[n].bloc), "%s", b);
            tag_text(tag, list[n].tag);
            list[n].alive = country_alive(c);
            ++n;
        }
    });
    if (!n) return reply(out, false, only ? "No bloc called %s." : "No blocs. Create one: bloc_add <name> <TAG> [tags]", only);
    Text t;
    bool done[1024] = {false};
    for (int i = 0; i < n; ++i) {
        if (done[i]) continue;
        int cnt = 0;
        for (int k = i; k < n; ++k) cnt += _stricmp(list[k].bloc, list[i].bloc) == 0;
        t.add("%s (%d): ", list[i].bloc, cnt);
        for (int k = i; k < n; ++k)
            if (!done[k] && _stricmp(list[k].bloc, list[i].bloc) == 0) {
                done[k] = true;
                t.add("%s%s ", list[k].tag, list[k].alive ? "" : "(dead)");
            }
        t.add("\n");
    }
    return finish(out, true, t);
}

// ================= console table extension =================
struct NewCommand {
    const char* name;
    const char* alias;
    const char* desc;
    void* fn;
    int argc;
    const char* args[4];
};

const NewCommand kCommands[] = {
    {"convert_pop", "convertpop",
     "Converts every pop in a province to the owner's culture and religion (or to the ones given).",
     (void*)&cmd_convert_pop, 3, {"province id", "<culture|-|owner>", "<religion|-|owner>"}},
    {"list_prov_cultures", "listprovcultures", "Lists the pops of a province with their internal id, type, culture and size.",
     (void*)&cmd_list_cultures, 1, {"province id"}},
    {"list_prov_religions", "listprovreligions", "Lists the pops of a province with their internal id, type, religion and size.",
     (void*)&cmd_list_religions, 1, {"province id"}},
    {"migrate_pop", "migratepop",
     "Moves the pops of a province that match the filters (culture, religion, pop type or pop id) to another province.",
     (void*)&cmd_migrate_pop, 4, {"from province", "to province", "culture|religion|type|pop id", "<more filters>"}},
    {"migrate_all_pops", "migrateallpops",
     "Moves every pop of the second country's primary culture living in the first country into the second country, spread evenly.",
     (void*)&cmd_migrate_all_pops, 3, {"FROM tag", "TO tag", "<filters>"}},
    {"accept_culture", "acceptculture", "Adds accepted cultures to a country (only the tag: lists them).",
     (void*)&cmd_accept_culture, 2, {"country tag", "<cultures>"}},
    {"unaccept_culture", "unacceptculture", "Removes accepted cultures from a country.",
     (void*)&cmd_unaccept_culture, 2, {"country tag", "cultures"}},
    {"double_pop", "doublepop", "Doubles the size of the pops of a province (optionally only the ones matching the filters).",
     (void*)&cmd_double_pop, 2, {"province id", "<filters>"}},
    {"scale_pop", "scalepop", "Multiplies the size of the pops of a province by a percentage (200 = double, 50 = half).",
     (void*)&cmd_scale_pop, 3, {"province id", "percent", "<filters>"}},
    {"scale_country_pop", "scalecountrypop", "Multiplies the pops of every province a country owns (optionally one culture/religion/type) by a percentage.",
     (void*)&cmd_scale_country_pop, 3, {"country tag", "percent", "<filters>"}},
    {"add_core", "addcore", "Adds a country's core to one or more provinces.", (void*)&cmd_add_core, 2, {"country tag", "province ids"}},
    {"bloc_add", "blocadd", "Puts countries into a bloc (mega alliance): in a war, all living members join the side of the member in it.",
     (void*)&cmd_bloc_add, 2, {"bloc name", "tags"}},
    {"bloc_remove", "blocremove", "Takes countries out of their bloc.", (void*)&cmd_bloc_remove, 1, {"tags"}},
    {"bloc_list", "bloclist", "Lists the blocs and their members.", (void*)&cmd_bloc_list, 1, {"<bloc name>"}},
    {"bloc_disband", "blocdisband", "Removes every member of a bloc.", (void*)&cmd_bloc_disband, 1, {"bloc name"}},
    {"remove_core", "removecore", "Removes a country's core from one or more provinces.", (void*)&cmd_remove_core, 2, {"country tag", "province ids"}},
};
constexpr int kNew = sizeof(kCommands) / sizeof(kCommands[0]);
constexpr int kTotal = kVanillaCount + kNew;
static_assert(kTotal < 0x80, "push imm8");

alignas(16) uint8_t g_table[kTotal * kEntrySize];

bool bytes_equal(uintptr_t addr, const uint8_t* expect, size_t n) { return memcmp((void*)addr, expect, n) == 0; }

bool write_code(uintptr_t addr, const void* src, size_t n) {
    DWORD old;
    if (!VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)addr, src, n);
    VirtualProtect((void*)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
    return true;
}

bool install_commands() {
    const uintptr_t start = R(VA_LOOP_START), last = R(VA_LOOP_LAST), end = R(VA_LOOP_END), res = R(VA_LOOP_RESERVE);
    const uint32_t table_rt = (uint32_t)R(VA_TABLE_BEGIN), table_end_rt = (uint32_t)R(VA_TABLE_END);

    uint8_t e_start[5] = {0xBB};
    memcpy(e_start + 1, &table_rt, 4);
    const uint8_t e_last[5] = {0xB8, 0x3B, 0x00, 0x00, 0x00};
    uint8_t e_end[6] = {0x81, 0xFB};
    memcpy(e_end + 2, &table_end_rt, 4);
    const uint8_t e_res[2] = {0x6A, 0x3C};

    if (!bytes_equal(start, e_start, 5) || !bytes_equal(last, e_last, 5) || !bytes_equal(end, e_end, 6) ||
        !bytes_equal(res, e_res, 2)) {
        logf("console list loop does not match v2game.exe 3.04 (or another DLL patched it); commands NOT added");
        return false;
    }

    memcpy(g_table, (void*)R(VA_TABLE_BEGIN), kVanillaCount * kEntrySize);
    for (int i = 0; i < kNew; ++i) {
        const NewCommand& c = kCommands[i];
        uint8_t* e = g_table + (kVanillaCount + i) * kEntrySize;
        memset(e, 0, kEntrySize);
        e[0] = 1;  // available outside developer mode
        *reinterpret_cast<const char**>(e + 0x04) = c.name;
        *reinterpret_cast<uint32_t*>(e + 0x08) = 1;
        *reinterpret_cast<const char**>(e + 0x0C) = c.alias;
        *reinterpret_cast<const char**>(e + 0x18) = c.desc;
        *reinterpret_cast<void**>(e + 0x1C) = c.fn;
        *reinterpret_cast<uint32_t*>(e + 0x20) = (uint32_t)c.argc;
        for (int k = 0; k < c.argc && k < 4; ++k) *reinterpret_cast<const char**>(e + 0x24 + 4 * k) = c.args[k];
    }

    const uint32_t new_begin = (uint32_t)(uintptr_t)g_table;
    const uint32_t new_end = (uint32_t)(uintptr_t)(g_table + kTotal * kEntrySize);
    const uint8_t n_last[5] = {0xB8, (uint8_t)(kTotal - 1), 0, 0, 0};
    const uint8_t n_res[2] = {0x6A, (uint8_t)kTotal};

    bool ok = write_code(start + 1, &new_begin, 4) && write_code(end + 2, &new_end, 4) &&
              write_code(last, n_last, 5) && write_code(res, n_res, 2);
    if (ok)
        for (int i = 0; i < kNew; ++i) logf("console command '%s' registered", kCommands[i].name);
    else
        logf("VirtualProtect failed; commands NOT added");
    return ok;
}

bool install_merge_fix() {
    const uintptr_t site = R(0x004BCB2E);
    const uint8_t expect[7] = {0x8B, 0x4A, 0x6C, 0x3B, 0xC9, 0x74, 0x47};
    if (!bytes_equal(site, expect, 7)) {
        logf("pop merge site 0x4BCB2E differs (another DLL patched it?); merge fix NOT installed, migrate_pop disabled");
        return false;
    }
    tcp_merge_found = R(0x004BCB7C);
    tcp_merge_next = R(0x004BCB35);
    tcp_strict_merge = g_fix_merge_everywhere ? 1 : 0;
    uint8_t patch[7] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    int32_t rel = (int32_t)((uintptr_t)&tcp_merge_cave - (site + 5));
    memcpy(patch + 1, &rel, 4);
    bool ok = write_code(site, patch, 7);
    logf(ok ? "pop merge culture check fixed (%s)" : "pop merge fix: VirtualProtect failed",
         g_fix_merge_everywhere ? "everywhere" : "for migrate_pop only");
    return ok;
}

// ================= topbar tab "COMENZI" =================
// The topbar class (vtable 0x00E113D0): [0] build 0x007129A0 (also called from its constructor at 0x0070D090),
// [1] deleting dtor 0x0070D0C0, [2] show 0x0070D300, [3] hide 0x007B3660. Its window (from topbar.gui) is at +0x20,
// +0x24 is the owner that gives the gui factory (vt 0x34) and the window layer (vt 0x3C). A window: vt 0x34 finds a
// child by name, vt 0x50 destroys it, (window+0x18) vt 0x34/0x38 show/hide, byte +0x67 = visible.
// A button's observable is at +0x54 (vt 4 = AddObserver). Factory [0x01258A80]: vt 0x2C create(name, layer),
// +0x34 = gui type map (vt 4 = find, returns 0 when the type is missing).
constexpr uintptr_t VA_TOPBAR_BUILD = 0x007129A0, VA_TOPBAR_BUILD_CALL = 0x0070D090, VA_TOPBAR_VT = 0x00E113D0;
constexpr uintptr_t VA_TOPBAR_DTOR = 0x0070D0C0, VA_TOPBAR_SHOW = 0x0070D300, VA_TOPBAR_HIDE = 0x007B3660;
constexpr uintptr_t VA_GUI_FACTORY = 0x01258A80;
constexpr size_t kGlueSize = 44;
const uint8_t kGlueClickStub[13] = {0x8B, 0x41, 0x08, 0x85, 0xC0, 0x74, 0x05, 0x8B, 0x49, 0x04, 0xFF, 0xE0, 0xC3};

typedef void* (__fastcall* VFn0)(void*, void*);
typedef void* (__fastcall* VFn1)(void*, void*, void*);
typedef void* (__fastcall* VFn2)(void*, void*, void*, void*);
inline void* vslot(void* obj, int off) { return (*reinterpret_cast<void***>(obj))[off / 4]; }
inline void* vcall0(void* o, int off) { return ((VFn0)vslot(o, off))(o, nullptr); }
inline void* vcall1(void* o, int off, void* a) { return ((VFn1)vslot(o, off))(o, nullptr, a); }
inline void* vcall2(void* o, int off, void* a, void* b) { return ((VFn2)vslot(o, off))(o, nullptr, a, b); }

bool g_tab_enabled = true;
bool g_blocs_enabled = true;
void* g_topbar = nullptr;   // the live topbar object
void* g_tab = nullptr;      // our "v2cmd_tab" window
void* g_cmdwin = nullptr;   // our "v2cmd_window"
void* g_layer = nullptr;
alignas(8) uint8_t g_tab_glue[kGlueSize];
alignas(8) uint8_t g_close_glue[kGlueSize];

typedef int(__fastcall* TopbarBuildFn)(void*, void*);
typedef void*(__fastcall* TopbarDtorFn)(void*, void*, int);
typedef void(__fastcall* TopbarShowFn)(void*, void*);
TopbarBuildFn g_build_orig = nullptr;
TopbarDtorFn g_dtor_orig = nullptr;
TopbarShowFn g_show_orig = nullptr, g_hide_orig = nullptr;

extern "C" void tcp_tab_click_thunk();
extern "C" void tcp_close_click_thunk();

void win_show(void* w, bool on) {
    if (!w) return;
    void* sub = (uint8_t*)w + 0x18;
    vcall0(sub, on ? 0x34 : 0x38);
}
bool win_visible(void* w) { return w && *((uint8_t*)w + 0x67) != 0; }
void win_destroy(void*& w) {
    if (!w) return;
    void* x = w;
    w = nullptr;
    vcall0(x, 0x50);
}

void* gui_create(const char* type) {
    void* factory = *(void**)R(VA_GUI_FACTORY);
    if (!factory || !g_layer) return nullptr;
    GameString s;
    gs_view(&s, type, strlen(type));
    void* types = (uint8_t*)factory + 0x34;
    if (!vcall1(types, 4, &s)) {
        logf("tab: gui type '%s' not found (interface\\zz_v2commands.gui missing or replaced by a mod)", type);
        return nullptr;
    }
    return vcall2(factory, 0x2C, &s, g_layer);
}

bool subscribe(void* window, const char* button, uint8_t* glue, void (*thunk)()) {
    if (!window || !g_topbar) return false;
    uint8_t* src = (uint8_t*)g_topbar + 0x218;  // a live CButtonObserverGlue of the topbar
    void** vt = *(void***)src;
    if (!vt || IsBadReadPtr(vt, 12) || IsBadReadPtr(vt[2], sizeof(kGlueClickStub)) ||
        memcmp(vt[2], kGlueClickStub, sizeof(kGlueClickStub)) != 0) {
        logf("tab: topbar+0x218 is not a button glue; '%s' not subscribed", button);
        return false;
    }
    GameString s;
    gs_view(&s, button, strlen(button));
    void* b = vcall1(window, 0x34, &s);
    if (!b) {
        logf("tab: button '%s' not found", button);
        return false;
    }
    memset(glue, 0, kGlueSize);
    *(void**)(glue + 0) = vt;
    *(void**)(glue + 4) = *(void**)(src + 4);
    *(void**)(glue + 8) = (void*)thunk;
    vcall1((uint8_t*)b + 0x54, 4, glue);
    return true;
}

extern "C" void __cdecl tcp_on_tab_click() {
    if (!g_cmdwin) {
        g_cmdwin = gui_create("v2cmd_window");
        if (!g_cmdwin) return;
        subscribe(g_cmdwin, "close_button", g_close_glue, &tcp_close_click_thunk);
        logf("tab: command window created");
        win_show(g_cmdwin, true);
        return;
    }
    win_show(g_cmdwin, !win_visible(g_cmdwin));
}
extern "C" void __cdecl tcp_on_close_click() { win_show(g_cmdwin, false); }

asm(R"(
    .text
    .globl _tcp_tab_click_thunk
_tcp_tab_click_thunk:
    pushal
    pushfl
    cld
    call  _tcp_on_tab_click
    popfl
    popal
    ret
    .globl _tcp_close_click_thunk
_tcp_close_click_thunk:
    pushal
    pushfl
    cld
    call  _tcp_on_close_click
    popfl
    popal
    ret
)");

void drop_windows() {  // before the topbar rebuilds or dies: our windows go with it
    win_destroy(g_cmdwin);
    win_destroy(g_tab);
}

int __fastcall topbar_build_hook(void* self, void* edx) {
    if (self == g_topbar) drop_windows();
    int r = g_build_orig(self, edx);
    g_topbar = self;
    void* owner = *(void**)((uint8_t*)self + 0x24);
    void* tbwin = *(void**)((uint8_t*)self + 0x20);
    if (!owner || !tbwin) return r;
    g_layer = vcall0(owner, 0x3C);
    g_tab = gui_create("v2cmd_tab");
    if (!g_tab) return r;
    bool ok = subscribe(g_tab, "v2cmd_tab_btn", g_tab_glue, &tcp_tab_click_thunk);
    win_show(g_tab, win_visible(tbwin));
    logf("tab: COMENZI added to the topbar (%s, topbar %s)", ok ? "clickable" : "NOT clickable",
         win_visible(tbwin) ? "visible" : "hidden");
    return r;
}
void* __fastcall topbar_dtor_hook(void* self, void* edx, int flags) {
    if (self == g_topbar) {
        drop_windows();
        g_topbar = nullptr;
    }
    return g_dtor_orig(self, edx, flags);
}
void __fastcall topbar_show_hook(void* self, void* edx) {
    g_show_orig(self, edx);
    if (self == g_topbar) win_show(g_tab, true);
}
void __fastcall topbar_hide_hook(void* self, void* edx) {
    g_hide_orig(self, edx);
    if (self == g_topbar) {
        win_show(g_tab, false);
        if (win_visible(g_cmdwin)) win_show(g_cmdwin, false);
    }
}

bool write_ptr(uintptr_t addr, uint32_t v) { return write_code(addr, &v, 4); }

void install_topbar_tab() {
    if (!g_tab_enabled) { logf("tab: disabled (topbar_tab=0)"); return; }
    const uintptr_t call = R(VA_TOPBAR_BUILD_CALL), vt = R(VA_TOPBAR_VT);
    const uint32_t* slots = (const uint32_t*)vt;
    int32_t rel = *(int32_t*)(call + 1);
    if (*(uint8_t*)call != 0xE8 || call + 5 + rel != R(VA_TOPBAR_BUILD) || slots[0] != R(VA_TOPBAR_BUILD) ||
        slots[1] != R(VA_TOPBAR_DTOR) || slots[2] != R(VA_TOPBAR_SHOW) || slots[3] != R(VA_TOPBAR_HIDE)) {
        logf("tab: topbar code differs (another DLL hooked it, e.g. V2DLL PLAYER_BUTTONS=1); tab NOT added");
        return;
    }
    g_build_orig = (TopbarBuildFn)R(VA_TOPBAR_BUILD);
    g_dtor_orig = (TopbarDtorFn)R(VA_TOPBAR_DTOR);
    g_show_orig = (TopbarShowFn)R(VA_TOPBAR_SHOW);
    g_hide_orig = (TopbarShowFn)R(VA_TOPBAR_HIDE);
    int32_t nrel = (int32_t)((uintptr_t)&topbar_build_hook - (call + 5));
    bool ok = write_code(call + 1, &nrel, 4) && write_ptr(vt, (uint32_t)(uintptr_t)&topbar_build_hook) &&
              write_ptr(vt + 4, (uint32_t)(uintptr_t)&topbar_dtor_hook) &&
              write_ptr(vt + 8, (uint32_t)(uintptr_t)&topbar_show_hook) &&
              write_ptr(vt + 12, (uint32_t)(uintptr_t)&topbar_hide_hook);
    logf(ok ? "tab: topbar hooks installed" : "tab: VirtualProtect failed");
}


// 9-byte prologue of both War_Add functions: push ebp / mov ebp,esp / mov eax,fs:[0]
void* make_detour(uintptr_t target, void* hook) {
    const uint8_t expect[9] = {0x55, 0x8B, 0xEC, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00};
    if (memcmp((void*)target, expect, 9) != 0) return nullptr;
    uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return nullptr;
    memcpy(tramp, expect, 9);
    tramp[9] = 0xE9;
    int32_t back = (int32_t)((target + 9) - ((uintptr_t)tramp + 14));
    memcpy(tramp + 10, &back, 4);
    uint8_t patch[9] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
    int32_t rel = (int32_t)((uintptr_t)hook - (target + 5));
    memcpy(patch + 1, &rel, 4);
    if (!write_code(target, patch, 9)) return nullptr;
    return tramp;
}
void install_blocs() {
    if (!g_blocs_enabled) { logf("blocs: disabled (blocs=0)"); return; }
    const uint8_t expect[9] = {0x55, 0x8B, 0xEC, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00};
    if (memcmp((void*)R(VA_WAR_ADD_ATT), expect, 9) != 0 || memcmp((void*)R(VA_WAR_ADD_DEF), expect, 9) != 0) {
        logf("blocs: war functions differ (another DLL hooked them?); blocs disabled");
        return;
    }
    g_add_att = (WarAddFn)make_detour(R(VA_WAR_ADD_ATT), (void*)&war_add_att_hook);
    g_add_def = g_add_att ? (WarAddFn)make_detour(R(VA_WAR_ADD_DEF), (void*)&war_add_def_hook) : nullptr;
    g_blocs_ok = g_add_att && g_add_def;
    logf(g_blocs_ok ? "blocs: war hooks installed" : "blocs: hook install FAILED");
}

void read_settings() {
    g_require_mod = GetPrivateProfileIntW(L"convert_pop", L"require_mod", 0, g_host->config_file) != 0;
    g_fix_merge_everywhere = GetPrivateProfileIntW(L"convert_pop", L"fix_merge_everywhere", 0, g_host->config_file) != 0;
    g_tab_enabled = GetPrivateProfileIntW(L"convert_pop", L"topbar_tab", 1, g_host->config_file) != 0;
    g_blocs_enabled = GetPrivateProfileIntW(L"convert_pop", L"blocs", 1, g_host->config_file) != 0;
    const char* cmd = GetCommandLineA();
    char lower[4096];
    size_t n = cmd ? strlen(cmd) : 0;
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (size_t i = 0; i < n; ++i) lower[i] = (char)tolower((unsigned char)cmd[i]);
    lower[n] = 0;
    g_mod_active = strstr(lower, "tgcconvertpop") != nullptr;
    logf("TGC Convert Pop build 2026-10-24 (blocs)");
    logf("command line: %s", cmd ? cmd : "(none)");
    logf("mod %s, require_mod=%d, V2DLL %s", g_mod_active ? "enabled" : "not enabled", g_require_mod ? 1 : 0,
         GetModuleHandleA("lua51_real.dll") || GetFileAttributesA("lua51_real.dll") != INVALID_FILE_ATTRIBUTES ? "present" : "not found");
}

}  // namespace

extern "C" __declspec(dllexport) int TGL_PluginInit(const TGL_HostApi* host) {
    if (!host || host->api_version != TGL_PLUGIN_API_VERSION) return 1;
    g_host = host;
    g_base = (uintptr_t)host->exe_base;
    read_settings();
    g_merge_fix_ok = install_merge_fix();
    install_topbar_tab();
    install_blocs();
    return install_commands() ? 0 : 2;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
