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
#include <math.h>
#include <algorithm>
#include <string>
#include <vector>
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

// ---- performance counters: time spent inside this plugin, logged once per game month ----
struct PerfStat {
    const char* name;
    uint64_t ticks, max;
    uint32_t calls;
    DWORD last_warn;
};
enum { P_PALETTE, P_IDLER, P_WARADD, P_DAY_GAME, P_DAY_OURS, P_COLLECT, P_PANEL, P_CACHE, P_COUNT };
PerfStat g_perf[P_COUNT] = {{"bloc map palette", 0, 0, 0, 0}, {"frame hook", 0, 0, 0, 0},       {"war join", 0, 0, 0, 0},
                            {"GAME day (whole)", 0, 0, 0, 0}, {"monthly blocs", 0, 0, 0, 0},  {"collect blocs", 0, 0, 0, 0},
                            {"bloc panel", 0, 0, 0, 0},       {"bloc cache rebuild", 0, 0, 0, 0}};
double g_qpf_ms = 0;
uint64_t qpc() { LARGE_INTEGER v; QueryPerformanceCounter(&v); return (uint64_t)v.QuadPart; }
void logf(const char* fmt, ...);
struct PerfScope {
    int id;
    uint64_t t0;
    explicit PerfScope(int i) : id(i), t0(qpc()) {}
    ~PerfScope() {
        uint64_t d = qpc() - t0;
        PerfStat& p = g_perf[id];
        p.ticks += d;
        ++p.calls;
        if (d > p.max) p.max = d;
        if (g_qpf_ms == 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpf_ms = f.QuadPart / 1000.0; }
        double ms = d / g_qpf_ms;
        if (id != P_DAY_GAME && ms > 30.0 && GetTickCount() - p.last_warn > 10000) {
            p.last_warn = GetTickCount();
            logf("perf: SLOW %s took %.1f ms", p.name, ms);
        }
    }
};
void perf_report() {
    if (g_qpf_ms == 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpf_ms = f.QuadPart / 1000.0; }
    char buf[900];
    int n = snprintf(buf, sizeof(buf), "perf (last month):");
    for (int i = 0; i < P_COUNT; ++i) {
        PerfStat& p = g_perf[i];
        if (!p.calls) continue;
        n += snprintf(buf + n, sizeof(buf) - n, " | %s: %u calls, %.1f ms total, max %.1f ms", p.name, p.calls,
                      p.ticks / g_qpf_ms, p.max / g_qpf_ms);
        if (n >= (int)sizeof(buf) - 1) break;
        p.ticks = p.max = 0;
        p.calls = 0;
    }
    logf("%s", buf);
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

// Localisation: table = 0x009AD510() (the global at 0x0131C99C), text = 0x009AD9D0(EDX = key, push table) -> const char* or 0
// (the lookup behind "Couldn't find localization string").
constexpr uintptr_t VA_LOC_TABLE = 0x009AD510, VA_LOC_FIND = 0x009AD9D0;
constexpr uintptr_t VA_DIPWIN_CTOR = 0x0061B820, VA_DIPWIN_VT = 0x00E07450;
}  // namespace
extern "C" const char* tcp_loc_thunk(uintptr_t fn, uintptr_t table, const char* key);
asm(R"(
    .text
    .globl _tcp_loc_thunk
_tcp_loc_thunk:
    pushl %ebp
    movl  %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl  16(%ebp), %edx
    pushl 12(%ebp)
    call  *8(%ebp)
    popl  %edi
    popl  %esi
    popl  %ebx
    popl  %ebp
    ret
)");
namespace {
// 0x9AD9D0 returns the c_str of the entry's key (a std::string at entry+0). For keys under 16 chars that
// pointer IS the entry; the English text is the next std::string (entry+0x1C, MSVC layout: size +0x10, cap +0x14).
const char* loc(const char* key) {
    if (!key || !*key || strlen(key) >= 16) return nullptr;
    uintptr_t t = reinterpret_cast<uintptr_t (*)()>(R(VA_LOC_TABLE))();
    const char* r = t ? tcp_loc_thunk(R(VA_LOC_FIND), t, key) : nullptr;
    if (!r || strcmp(r, key) != 0) return nullptr;
    uintptr_t e = (uintptr_t)r;
    if (rd<uint32_t>(e + 0x14) != 15) return nullptr;  // key must be stored inline (cap 15)
    uintptr_t v = e + 0x1C;
    uint32_t size = rd<uint32_t>(v + 0x10), cap = rd<uint32_t>(v + 0x14);
    if (size == 0 || size > 300 || cap < size) return nullptr;
    const char* txt = cap >= 16 ? rd<const char*>(v) : reinterpret_cast<const char*>(v);
    return txt && *txt ? txt : nullptr;
}
const char* culture_display(uintptr_t c) {
    const char* k = culture_name(c);
    const char* l = c ? loc(k) : nullptr;
    return l ? l : k;
}
const char* prov_display(uint32_t id, char* buf, size_t n) {
    char key[24];
    snprintf(key, sizeof(key), "PROV%u", id);
    const char* l = loc(key);
    if (l) snprintf(buf, n, "%s", l);
    else snprintf(buf, n, "#%u", id);
    return buf;
}
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
bool g_bloc_cache_dirty = true;
void flag_set(uintptr_t country, const char* name, bool on) {
    if (_strnicmp(name, "bloc", 4) == 0) g_bloc_cache_dirty = true;
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
// Bloc membership of every country, rebuilt at most once per game day (or when this plugin changes a bloc
// flag): walking the flags of ~700 countries is too slow to repeat on every map redraw or war.
uint8_t g_cache_bloc[4096];  // 0 = none, else 1 + index into g_cache_names (sorted)
char g_cache_names[64][32];
int g_cache_n = 0;
uint32_t g_cache_date = 0;
uint32_t world_date() {
    uintptr_t world = rd<uintptr_t>(R(VA_G_WORLD));
    return world ? rd<uint32_t>(world + 0xB0C) : 0;
}
template <typename F> void for_each_tag(F fn);
void bloc_cache_refresh() {
    uint32_t date = world_date();
    if (!g_bloc_cache_dirty && date == g_cache_date) return;
    PerfScope ps(P_CACHE);
    memset(g_cache_bloc, 0, sizeof(g_cache_bloc));
    static char who[4096][32];
    static uint32_t idxs[4096];
    int m = 0;
    g_cache_n = 0;
    for_each_tag([&](uint32_t, uint32_t idx) {
        char b[64];
        if (idx >= 4096 || !country_bloc(country_at((int)idx), b, sizeof(b))) return;
        snprintf(who[m], 32, "%s", b);
        idxs[m++] = idx;
        for (int k = 0; k < g_cache_n; ++k)
            if (_stricmp(g_cache_names[k], b) == 0) return;
        if (g_cache_n < 64) snprintf(g_cache_names[g_cache_n++], 32, "%s", b);
    });
    for (int i = 1; i < g_cache_n; ++i)
        for (int k = i; k > 0 && _stricmp(g_cache_names[k - 1], g_cache_names[k]) > 0; --k) {
            char t[32];
            memcpy(t, g_cache_names[k], 32); memcpy(g_cache_names[k], g_cache_names[k - 1], 32); memcpy(g_cache_names[k - 1], t, 32);
        }
    for (int i = 0; i < m; ++i)
        for (int k = 0; k < g_cache_n; ++k)
            if (_stricmp(g_cache_names[k], who[i]) == 0) { g_cache_bloc[idxs[i]] = (uint8_t)(k + 1); break; }
    g_cache_date = date;
    g_bloc_cache_dirty = false;
}
const char* cached_bloc(uint32_t idx) {
    bloc_cache_refresh();
    return (idx < 4096 && g_cache_bloc[idx]) ? g_cache_names[g_cache_bloc[idx] - 1] : nullptr;
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
#define C_GREEN "\xA7G"
#define C_RED "\xA7R"
#define C_YELLOW ""
#define C_END "\xA7!"
const char* tag_text(uint32_t tag, char* buf) { memcpy(buf, &tag, 3); buf[3] = 0; return buf; }
// "France" for FRA (the country's localised name), else the tag.
const char* name_of(uint32_t idx, char* buf, size_t n) {
    uint32_t lo = 0;
    for_each_tag([&](uint32_t tag, uint32_t i) { if (i == idx) lo = tag; });
    char t[4];
    tag_text(lo, t);
    const char* l = loc(t);
    snprintf(buf, n, "%s", l ? l : t);
    return buf;
}
// Country name in a rotating static buffer (safe for a few uses in one printf).
const char* cn(uint32_t idx) {
    static char b[6][64];
    static int k = 0;
    char* r = b[k++ % 6];
    return name_of(idx, r, 64);
}
const char* pn(uint32_t id) {
    static char b[6][64];
    static int k = 0;
    char* r = b[k++ % 6];
    return prov_display(id, r, 64);
}
// Display name of a bloc: "pact_ger" -> "German Empire Pact", "central_powers" -> "Central Powers".
const char* bloc_display(const char* name, char* buf, size_t n) {
    if (_strnicmp(name, "pact_", 5) == 0 && strlen(name) == 8) {
        char t[4] = {(char)toupper((unsigned char)name[5]), (char)toupper((unsigned char)name[6]), (char)toupper((unsigned char)name[7]), 0};
        const char* l = loc(t);
        snprintf(buf, n, "%s Pact", l ? l : t);
        return buf;
    }
    snprintf(buf, n, "%s", name);
    bool up = true;
    for (char* q = buf; *q; ++q) {
        if (*q == '_') { *q = ' '; up = true; continue; }
        if (up) *q = (char)toupper((unsigned char)*q);
        up = false;
    }
    return buf;
}

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

int bloc_cohesion_for(const char* bloc, uint32_t* leader_out);
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
    uint32_t leader = 0;
    int coh = bloc_cohesion_for(bloc, &leader);
    if (coh < 20 && index != leader) {
        logf("bloc %s: cohesion %d (fractured), only the leader's wars pull the bloc", bloc, coh);
        return;
    }
    int added = 0;
    for_each_tag([&](uint32_t tag, uint32_t idx) {
        if (idx == index || war_has(war, 0x2C, idx) || war_has(war, 0x3C, idx)) return;
        uintptr_t c = country_at((int)idx);
        const char* b2 = cached_bloc(idx);
        if (!b2 || _stricmp(b2, bloc) != 0 || !country_alive(c)) return;
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
    PerfScope ps(P_WARADD);
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
    PerfScope ps(P_WARADD);
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

void bloc_panel_refresh();  // the BLOCURI window, if open
uint32_t player_index();
uint32_t ctag_lo(uint32_t idx);
void bloc_ensure_leader(const char* bloc);
void bloc_panel_subscribe_paging();
int bloc_cohesion_for(const char* bloc, uint32_t* leader_out);
void bloc_map_redraw();     // the bloc map, if on

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
    if (done) bloc_ensure_leader(name);
    bloc_panel_refresh();
    bloc_map_redraw();
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
    bloc_panel_refresh();
    bloc_map_redraw();
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
    bloc_panel_refresh();
    bloc_map_redraw();
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


// ================= bloc map (political map mode recoloured by bloc) =================
// 0x007FCA30 builds the political palette (stdcall, colour map = map+0x20BC): vector of per-province
// entries at +0x1C (index 1..n-1), entry +0 owner colour, +4 controller colour (ARGB), +0xC province.
// Called from 0x0066C4C3 (political map mode, ESI = map view) and 0x00422A32. Both calls are redirected
// here; when the bloc map is on, the original runs and then the colours are replaced.
// Map view: +0xDB4 current mode (1 = political), [+0x24] = 22 "dirty" flags, one per mode.
constexpr uintptr_t VA_POL_PALETTE = 0x007FCA30, VA_POL_CALL_MODE = 0x0066C4C3, VA_POL_CALL_OTHER = 0x00422A32;
bool g_bloc_map = false;
bool g_bloc_map_ok = false;
uintptr_t g_pol_target = 0;
}  // namespace
extern "C" { uintptr_t tcp_mapview = 0; void* tcp_pol_fn = nullptr; }
extern "C" void tcp_pol_mode_thunk();
asm(R"(
    .text
    .globl _tcp_pol_mode_thunk
_tcp_pol_mode_thunk:
    movl  %esi, _tcp_mapview
    jmp   *_tcp_pol_fn
)");
namespace {
const uint32_t kBlocPalette[] = {0xE53935, 0x1E88E5, 0x43A047, 0xFB8C00, 0x8E24AA, 0xFDD835,
                                 0x00ACC1, 0xD81B60, 0x6D4C41, 0xC0CA33, 0x00897B, 0x3949AB};
const char* const kBlocColorNames[] = {"red", "blue", "green", "orange", "purple", "yellow",
                                       "cyan", "pink", "brown", "lime", "teal", "indigo"};
constexpr int kPal = sizeof(kBlocPalette) / sizeof(kBlocPalette[0]);

// Sorted distinct bloc names -> palette index (stable for the same set of blocs).
int bloc_names(char names[][32], int max) {
    bloc_cache_refresh();
    int n = g_cache_n < max ? g_cache_n : max;
    for (int k = 0; k < n; ++k) memcpy(names[k], g_cache_names[k], 32);
    return n;
}

uint32_t grey_of(uint32_t argb) {
    uint32_t r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    uint32_t l = (r * 30 + g * 59 + b * 11) / 100;
    uint32_t v = 60 + l * 35 / 100;
    return (argb & 0xFF000000u) | (v << 16) | (v << 8) | v;
}

void __stdcall pol_palette_hook(uintptr_t cmap) {
    reinterpret_cast<void(__stdcall*)(uintptr_t)>(g_pol_target)(cmap);
    if (!g_bloc_map) return;
    PerfScope ps(P_PALETTE);
    static uint32_t color_of[4096];
    bloc_cache_refresh();
    for (int idx = 0; idx < 4096; ++idx)
        color_of[idx] = g_cache_bloc[idx] ? (kBlocPalette[(g_cache_bloc[idx] - 1) % kPal] | 0x01000000u) : 0;
    uintptr_t* b = rd<uintptr_t*>(cmap + 0x1C);
    uintptr_t* e = rd<uintptr_t*>(cmap + 0x20);
    for (uintptr_t* it = b ? b + 1 : nullptr; it && it < e; ++it) {
        uint32_t* entry = reinterpret_cast<uint32_t*>(*it);
        if (!entry) continue;
        uintptr_t prov = entry[3];
        if (!prov) continue;
        uint32_t owner = rd<uint32_t>(prov + 0x12C), ctrl = rd<uint32_t>(prov + 0x134);
        if (!owner || owner >= 4096) continue;  // sea, wasteland
        uint32_t a = entry[0] & 0xFF000000u;
        uint32_t c1 = color_of[owner] ? (a | (color_of[owner] & 0xFFFFFF)) : grey_of(entry[0]);
        uint32_t c2 = c1;
        if (ctrl && ctrl != owner && ctrl < 4096)
            c2 = color_of[ctrl] ? ((entry[1] & 0xFF000000u) | (color_of[ctrl] & 0xFFFFFF)) : grey_of(entry[1]);
        entry[0] = c1;
        entry[1] = c2;
    }
}

void bloc_map_redraw() {
    if (!tcp_mapview) return;
    uint32_t* dirty = rd<uint32_t*>(tcp_mapview + 0x24);
    if (dirty) dirty[1] = 1;  // the game rebuilds the political palette on its next map update
}

extern "C" ConsoleResult* __cdecl cmd_bloc_map(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (!g_bloc_map_ok) return reply(out, false, "bloc_map: not available (the political map code differs, see the log).");
    const char* a = arg_count(args) >= 1 ? arg_text(args, 0) : nullptr;
    if (a && _stricmp(a, "on") == 0) g_bloc_map = true;
    else if (a && _stricmp(a, "off") == 0) g_bloc_map = false;
    else g_bloc_map = !g_bloc_map;
    bloc_map_redraw();
    bloc_panel_refresh();
    return reply(out, true, g_bloc_map ? "Bloc map ON: the political map mode now shows the blocs (others in grey)."
                                       : "Bloc map OFF: the political map is normal again.");
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

}  // namespace
extern "C" ConsoleResult* __cdecl cmd_treaty(ConsoleResult*, ArgVector*);
extern "C" ConsoleResult* __cdecl cmd_treaty_propose(ConsoleResult*, ArgVector*);
extern "C" ConsoleResult* __cdecl cmd_treaty_population(ConsoleResult*, ArgVector*);
extern "C" ConsoleResult* __cdecl cmd_treaty_history(ConsoleResult*, ArgVector*);
extern "C" ConsoleResult* __cdecl cmd_bloc_identity(ConsoleResult*, ArgVector*);
extern "C" ConsoleResult* __cdecl cmd_bloc_leader(ConsoleResult*, ArgVector*);
extern "C" ConsoleResult* __cdecl cmd_bloc_invite(ConsoleResult*, ArgVector*);
extern "C" ConsoleResult* __cdecl cmd_bloc_info(ConsoleResult*, ArgVector*);
namespace {
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
    {"treaty_history", "treatyhistory", "Lists the historical treaties the AI can sign; 'treaty_history request' makes an AI ask you now.",
     (void*)&cmd_treaty_history, 1, {"<request>"}},
    {"bloc_list", "bloclist", "Lists the blocs and their members.", (void*)&cmd_bloc_list, 1, {"<bloc name>"}},
    {"bloc_map", "blocmap", "Toggles the bloc map: the political map mode shows each bloc in its own colour.",
     (void*)&cmd_bloc_map, 1, {"<on|off>"}},
    {"bloc_identity", "blocidentity", "Sets a bloc's identity: military, trade, empire or religious (each has its own monthly effect).",
     (void*)&cmd_bloc_identity, 2, {"bloc", "identity"}},
    {"bloc_leader", "blocleader", "Makes a member the leader of its bloc.", (void*)&cmd_bloc_leader, 1, {"tag"}},
    {"bloc_invite", "blocinvite", "Invites a country into a bloc; the AI accepts or refuses (bloc_add forces it).",
     (void*)&cmd_bloc_invite, 2, {"bloc", "tag"}},
    {"bloc_info", "blocinfo", "Shows identity, leader, members and cohesion of the blocs.", (void*)&cmd_bloc_info, 1, {"<bloc>"}},
    {"treaty", "treaty", "Opens a treaty with a country: treaty <TAG> [give <ids>] [take <ids>].", (void*)&cmd_treaty, 3,
     {"tag", "<give ids>", "<take ids>"}},
    {"treaty_propose", "treatypropose", "Proposes the province exchange of the open treaty; the AI accepts or refuses.",
     (void*)&cmd_treaty_propose, 0, {}},
    {"treaty_population", "treatypopulation", "Proposes a population exchange (each side gets its own nationals back).",
     (void*)&cmd_treaty_population, 1, {"<tag>"}},
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
alignas(8) uint8_t g_blocs_btn_glue[kGlueSize], g_bloc_close_glue[kGlueSize], g_bloc_back_glue[kGlueSize], g_bloc_map_glue[kGlueSize];
void* g_blocwin = nullptr;  // our "v2bloc_window"

typedef int(__fastcall* TopbarBuildFn)(void*, void*);
typedef void*(__fastcall* TopbarDtorFn)(void*, void*, int);
typedef void(__fastcall* TopbarShowFn)(void*, void*);
TopbarBuildFn g_build_orig = nullptr;
TopbarDtorFn g_dtor_orig = nullptr;
TopbarShowFn g_show_orig = nullptr, g_hide_orig = nullptr;

extern "C" void tcp_tab_click_thunk();
extern "C" void tcp_close_click_thunk();
extern "C" void tcp_blocs_click_thunk();
extern "C" void tcp_bloc_close_thunk();
extern "C" void tcp_bloc_back_thunk();
extern "C" void tcp_bloc_mapbtn_thunk();

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

// ---- text boxes: window vt 0x3C finds one by name; text = std::string at +0xDC, renderer at +0xA0
// (SetText 0x00A5ECC0: ESI = renderer, (const std::string*, bool)) - the game does both everywhere.
constexpr uintptr_t VA_STR_ASSIGN_SUB = 0x00408ED0, VA_TEXT_SET = 0x00A5ECC0;
}  // namespace
extern "C" void tcp_settext_thunk(uintptr_t fn, uintptr_t renderer, const void* str);
asm(R"(
    .text
    .globl _tcp_settext_thunk
_tcp_settext_thunk:
    pushl %ebp
    movl  %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl  12(%ebp), %esi
    pushl $0
    pushl 16(%ebp)
    call  *8(%ebp)
    popl  %edi
    popl  %esi
    popl  %ebx
    popl  %ebp
    ret
)");
namespace {
void tb_set(void* win, const char* name, const char* text) {
    if (!win) return;
    GameString n;
    gs_view(&n, name, strlen(name));
    uint8_t* tb = (uint8_t*)vcall1(win, 0x3C, &n);
    if (!tb) return;
    GameString v;
    gs_view(&v, text, strlen(text));
    typedef void(__fastcall * AssignFn)(void*, void*, const GameString*, uint32_t, uint32_t);
    reinterpret_cast<AssignFn>(R(VA_STR_ASSIGN_SUB))(tb + 0xDC, nullptr, &v, 0, 0xFFFFFFFFu);
    uintptr_t renderer = rd<uintptr_t>((uintptr_t)tb + 0xA0);
    if (renderer) tcp_settext_thunk(R(VA_TEXT_SET), renderer, &v);
}

extern "C" void __cdecl tcp_on_blocs_click() {
    if (!g_blocwin) {
        g_blocwin = gui_create("v2bloc_window");
        if (!g_blocwin) return;
        subscribe(g_blocwin, "close_button", g_bloc_close_glue, &tcp_bloc_close_thunk);
        subscribe(g_blocwin, "v2bloc_back", g_bloc_back_glue, &tcp_bloc_back_thunk);
        if (g_bloc_map_ok) subscribe(g_blocwin, "v2bloc_mapbtn", g_bloc_map_glue, &tcp_bloc_mapbtn_thunk);
        bloc_panel_subscribe_paging();
        logf("tab: bloc window created");
    }
    bloc_panel_refresh();
    win_show(g_blocwin, true);
    if (win_visible(g_cmdwin)) win_show(g_cmdwin, false);
}
extern "C" void __cdecl tcp_on_bloc_mapbtn() {
    g_bloc_map = !g_bloc_map;
    bloc_map_redraw();
    bloc_panel_refresh();
}
extern "C" void __cdecl tcp_on_bloc_close() { win_show(g_blocwin, false); }
extern "C" void __cdecl tcp_on_bloc_back() {
    win_show(g_blocwin, false);
    if (g_cmdwin) win_show(g_cmdwin, true);
}

void treaty_subscribe_cmd(void* win);
void hist_fill(void* win);
extern "C" void __cdecl tcp_on_tab_click() {
    if (!g_cmdwin) {
        g_cmdwin = gui_create("v2cmd_window");
        if (!g_cmdwin) return;
        subscribe(g_cmdwin, "close_button", g_close_glue, &tcp_close_click_thunk);
        subscribe(g_cmdwin, "v2cmd_blocs", g_blocs_btn_glue, &tcp_blocs_click_thunk);
        treaty_subscribe_cmd(g_cmdwin);
        logf("tab: command window created");
        hist_fill(g_cmdwin);
        win_show(g_cmdwin, true);
        if (win_visible(g_blocwin)) win_show(g_blocwin, false);
        return;
    }
    bool show = !win_visible(g_cmdwin);
    if (show) hist_fill(g_cmdwin);
    win_show(g_cmdwin, show);
    if (show && win_visible(g_blocwin)) win_show(g_blocwin, false);
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
    .globl _tcp_blocs_click_thunk
_tcp_blocs_click_thunk:
    pushal
    pushfl
    cld
    call  _tcp_on_blocs_click
    popfl
    popal
    ret
    .globl _tcp_bloc_close_thunk
_tcp_bloc_close_thunk:
    pushal
    pushfl
    cld
    call  _tcp_on_bloc_close
    popfl
    popal
    ret
    .globl _tcp_bloc_mapbtn_thunk
_tcp_bloc_mapbtn_thunk:
    pushal
    pushfl
    cld
    call  _tcp_on_bloc_mapbtn
    popfl
    popal
    ret
    .globl _tcp_bloc_back_thunk
_tcp_bloc_back_thunk:
    pushal
    pushfl
    cld
    call  _tcp_on_bloc_back
    popfl
    popal
    ret
)");

void drop_diplo_window();
void drop_windows() {  // before the topbar rebuilds or dies: our windows go with it
    drop_diplo_window();
    win_destroy(g_blocwin);
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
    logf("tab: COMMANDS added to the topbar (%s, topbar %s)", ok ? "clickable" : "NOT clickable",
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
        if (win_visible(g_blocwin)) win_show(g_blocwin, false);
    }
}

bool write_ptr(uintptr_t addr, uint32_t v) { return write_code(addr, &v, 4); }


// ================= bloc model: leader, identity, cohesion, monthly effects =================
// Flags: "bloc_<name>" membership, "blocleader" on the leader, "blocid_<identity>" on the leader.
// Relations: country+0xBE8[other] -> relation, value at +0x3C (x1000, -200..200). Prestige: country+0xEA0 (x1000).
// Daily hook: the call to DayTick at 0x00683AB1 (stdcall world); monthly work when the month changes.
// Player country index: world+0xB60.
const char* const kIdentities[] = {"military", "trade", "empire", "religious"};
const char* const kIdentitiesOld[] = {"militar", "comercial", "imperiu", "religios"};  // saves from earlier builds
const char* const kIdentityTitles[] = {"Military Treaty", "Trade League", "Sovereign Empire", "Religious Convocation"};
const char* const kIdentityEffects[][2] = {
    {"Members go to war together. Relations with the leader +2 per month.",
     "Cohesion: +5 while the bloc is at war."},
    {"Relations between all members +3 per month.",
     "Cohesion: +10 if every member has positive relations with the leader."},
    {"The leader gains +0.5 prestige per month for every member.",
     "Cohesion: -3 per member instead of -2 (large empires are hard to hold)."},
    {"Monthly, in every member, one small pop (up to 3000) of another faith converts to the leader's religion.",
     "Cohesion: up to +15 for members sharing the leader's religion."},
};
constexpr int kNumIdentities = 4;

struct BlocInfo {
    char name[32];
    uint32_t members[256];
    int n;
    uint32_t leader;
    int identity;
    int cohesion;
};
BlocInfo g_blocs[64];
int g_nblocs = 0;

uint32_t g_member_tag[4096];  // tag chars per country index (filled by collect)

int identity_of_leader(uintptr_t leader_c) {
    int id = 0;
    auto fn = [&](const char* name) {
        if (_strnicmp(name, "blocid_", 7) == 0)
            for (int k = 0; k < kNumIdentities; ++k)
                if (_stricmp(name + 7, kIdentities[k]) == 0 || _stricmp(name + 7, kIdentitiesOld[k]) == 0) id = k;
    };
    if (leader_c) flag_walk(rd<uintptr_t>(country_flags(leader_c) + 4), fn, 0);
    return id;
}
bool has_flag(uintptr_t c, const char* f) {
    uintptr_t e = c ? flag_find(c, f) : 0;
    return e && rd<uint8_t>(e + 0x1C);
}
int prestige_of(uintptr_t c) { return c ? rd<int32_t>(c + 0xEA0) / 1000 : 0; }
int relation(uint32_t a, uint32_t b) {
    uintptr_t ca = country_at((int)a);
    uintptr_t tab = ca ? rd<uintptr_t>(ca + 0xBE8) : 0;
    uintptr_t r = tab ? rd<uintptr_t>(tab + b * 4) : 0;
    return r ? rd<int32_t>(r + 0x3C) / 1000 : 0;
}
void relation_add_one(uint32_t a, uint32_t b, int delta) {
    uintptr_t ca = country_at((int)a);
    uintptr_t tab = ca ? rd<uintptr_t>(ca + 0xBE8) : 0;
    uintptr_t r = tab ? rd<uintptr_t>(tab + b * 4) : 0;
    if (!r) return;
    int32_t v = rd<int32_t>(r + 0x3C) + delta * 1000;
    if (v > 200000) v = 200000;
    if (v < -200000) v = -200000;
    *reinterpret_cast<int32_t*>(r + 0x3C) = v;
}
void relation_add(uint32_t a, uint32_t b, int delta) { relation_add_one(a, b, delta); relation_add_one(b, a, delta); }

// Is country a in any war on the other side of country b?
bool at_war_with(uint32_t a, uint32_t b) {
    uintptr_t world = rd<uintptr_t>(R(VA_G_WORLD));
    if (!world) return false;
    int guard = 0;
    for (uintptr_t node = rd<uintptr_t>(world + 0xB3C); node && guard < 2000; node = rd<uintptr_t>(node + 8), ++guard) {
        uintptr_t war = rd<uintptr_t>(node);
        if (!war) continue;
        if ((war_has(war, 0x2C, a) && war_has(war, 0x3C, b)) || (war_has(war, 0x3C, a) && war_has(war, 0x2C, b))) return true;
    }
    return false;
}
bool in_any_war(uint32_t a) {
    uintptr_t world = rd<uintptr_t>(R(VA_G_WORLD));
    if (!world) return false;
    int guard = 0;
    for (uintptr_t node = rd<uintptr_t>(world + 0xB3C); node && guard < 2000; node = rd<uintptr_t>(node + 8), ++guard) {
        uintptr_t war = rd<uintptr_t>(node);
        if (war && (war_has(war, 0x2C, a) || war_has(war, 0x3C, a))) return true;
    }
    return false;
}

int compute_cohesion(const BlocInfo& b) {
    if (b.n <= 1) return 50;
    uintptr_t lc = country_at((int)b.leader);
    double c = 50;
    int others = 0, rel_sum = 0, same_rel = 0, positive = 0;
    uintptr_t lrel = lc ? rd<uintptr_t>(lc + 0xE68) : 0;
    bool any_war = false;
    for (int i = 0; i < b.n; ++i) {
        uint32_t m = b.members[i];
        any_war |= in_any_war(m);
        if (m == b.leader) continue;
        ++others;
        int r = relation(b.leader, m);
        rel_sum += r;
        positive += r > 0;
        uintptr_t mc = country_at((int)m);
        if (mc && lrel && rd<uintptr_t>(mc + 0xE68) == lrel) ++same_rel;
    }
    if (others) c += (rel_sum / (double)others) / 10.0;            // -20 .. +20
    int per = b.identity == 2 ? 3 : 2;
    if (b.n > 3) c -= per * (b.n - 3);
    double rel_frac = others ? same_rel / (double)others : 1.0;
    c += (b.identity == 3 ? 15 : 10) * rel_frac;
    if (b.identity == 0 && any_war) c += 5;
    if (b.identity == 1 && others && positive == others) c += 10;
    for (int i = 0; i < b.n; ++i)
        for (int k = i + 1; k < b.n; ++k)
            if (at_war_with(b.members[i], b.members[k])) c -= 15;
    if (c < 0) c = 0;
    if (c > 100) c = 100;
    return (int)c;
}
const char* cohesion_level(int c) {
    return c < 20 ? "Fractured" : c < 40 ? "Divided" : c < 60 ? "Stable" : c < 80 ? "Controlled" : "Orchestrated";
}

// Reads every bloc (sorted by name) with members, leader (flag, else highest prestige), identity, cohesion.
int collect_blocs() {
    PerfScope ps(P_COLLECT);
    char names[64][32];
    int nb = bloc_names(names, 64);
    for (int k = 0; k < nb; ++k) {
        memset(&g_blocs[k], 0, sizeof(BlocInfo));
        snprintf(g_blocs[k].name, 32, "%s", names[k]);
    }
    for_each_tag([&](uint32_t tag, uint32_t idx) {
        uintptr_t c = country_at((int)idx);
        const char* b = cached_bloc(idx);
        if (!b) return;
        if (idx < 4096) g_member_tag[idx] = tag;
        for (int k = 0; k < nb; ++k)
            if (_stricmp(g_blocs[k].name, b) == 0 && g_blocs[k].n < 256) {
                BlocInfo& bi = g_blocs[k];
                bi.members[bi.n++] = idx;
                if (country_alive(c) && has_flag(c, "blocleader")) bi.leader = idx;
                break;
            }
    });
    for (int k = 0; k < nb; ++k) {
        BlocInfo& bi = g_blocs[k];
        if (!bi.leader) {  // no flag: the most prestigious living member
            int best = -1;
            for (int i = 0; i < bi.n; ++i) {
                uintptr_t c = country_at((int)bi.members[i]);
                if (country_alive(c) && prestige_of(c) > best) { best = prestige_of(c); bi.leader = bi.members[i]; }
            }
        }
        bi.identity = identity_of_leader(country_at((int)bi.leader));
        bi.cohesion = compute_cohesion(bi);
    }
    g_nblocs = nb;
    return nb;
}
BlocInfo* find_bloc(const char* name) {
    for (int k = 0; k < g_nblocs; ++k)
        if (_stricmp(g_blocs[k].name, name) == 0) return &g_blocs[k];
    return nullptr;
}
int bloc_cohesion_for(const char* bloc, uint32_t* leader_out) {
    collect_blocs();
    BlocInfo* b = find_bloc(bloc);
    if (leader_out) *leader_out = b ? b->leader : 0;
    return b ? b->cohesion : 50;
}
void set_leader(BlocInfo& b, uint32_t idx) {
    for (int i = 0; i < b.n; ++i) flag_set(country_at((int)b.members[i]), "blocleader", false);
    int id = b.identity;
    // identity lives on the leader: move it
    for (int i = 0; i < b.n; ++i)
        for (int k = 0; k < kNumIdentities * 2; ++k) {
            char f[32];
            snprintf(f, sizeof(f), "blocid_%s", k < kNumIdentities ? kIdentities[k] : kIdentitiesOld[k - kNumIdentities]);
            if (has_flag(country_at((int)b.members[i]), f)) flag_set(country_at((int)b.members[i]), f, false);
        }
    uintptr_t c = country_at((int)idx);
    flag_set(c, "blocleader", true);
    char f[32];
    snprintf(f, sizeof(f), "blocid_%s", kIdentities[id]);
    flag_set(c, f, true);
    b.leader = idx;
}
void bloc_ensure_leader(const char* bloc) {
    collect_blocs();
    BlocInfo* b = find_bloc(bloc);
    if (b && b->leader && !has_flag(country_at((int)b->leader), "blocleader")) set_leader(*b, b->leader);
}
const char* tag_of(uint32_t idx, char* buf) {
    uint32_t t = idx < 4096 ? g_member_tag[idx] : 0;
    if (!t) {
        for_each_tag([&](uint32_t tag, uint32_t i) { if (i == idx) t = tag; });
    }
    return tag_text(t, buf);
}

uint32_t g_rng = 0x1234567;
uint32_t rnd() { g_rng = g_rng * 1103515245u + 12345u + GetTickCount(); return (g_rng >> 8) & 0xFFFFFF; }

void bloc_monthly() {
    collect_blocs();
    for (int k = 0; k < g_nblocs; ++k) {
        BlocInfo& b = g_blocs[k];
        if (!b.leader || b.n < 2) continue;
        uintptr_t lc = country_at((int)b.leader);
        if (!b.leader || !country_alive(lc)) continue;
        if (!has_flag(lc, "blocleader")) set_leader(b, b.leader);
        int converted = 0;
        for (int i = 0; i < b.n; ++i) {
            uint32_t m = b.members[i];
            if (m == b.leader || !country_alive(country_at((int)m))) continue;
            switch (b.identity) {
                case 0: relation_add(b.leader, m, 2); break;
                case 1:
                    for (int j = i + 1; j < b.n; ++j)
                        if (country_alive(country_at((int)b.members[j]))) relation_add(m, b.members[j], 3);
                    if (i == 0 || b.members[0] == b.leader) {}
                    break;
                case 2: relation_add(b.leader, m, 1); break;
                case 3: {
                    relation_add(b.leader, m, 2);
                    uintptr_t lrel = rd<uintptr_t>(lc + 0xE68);
                    static uintptr_t provs[2048];
                    int np = owned_provinces((int)m, provs, 2048);
                    uintptr_t best = 0;
                    uint32_t best_size = 3001;
                    for (int p = 0; p < np; ++p)
                        for_each_pop(provs[p], [&](uintptr_t pop, int) {
                            uint32_t sz = rd<uint32_t>(pop + 0x58);
                            if (pop_alive(pop) && rd<uintptr_t>(pop + 0x70) != lrel && sz < best_size) { best = pop; best_size = sz; }
                        });
                    if (best && lrel) { *reinterpret_cast<uintptr_t*>(best + 0x70) = lrel; ++converted; }
                    break;
                }
            }
        }
        if (b.identity == 1) {  // the leader with everyone too
            for (int i = 0; i < b.n; ++i)
                if (b.members[i] != b.leader) relation_add(b.leader, b.members[i], 3);
        }
        if (b.identity == 2) *reinterpret_cast<int32_t*>(lc + 0xEA0) += 500 * (b.n - 1);
        logf("bloc %s (%s): monthly effects, cohesion %d%s", b.name, kIdentities[b.identity], b.cohesion,
             converted ? ", pops converted" : "");
    }
}

// ---- daily hook (call to DayTick at 0x00683AB1) ----
constexpr uintptr_t VA_DAYTICK_CALL = 0x00683AB1, VA_DAYTICK = 0x006859C0, VA_MONTH_LEN = 0x00F1027C;
uintptr_t g_daytick_target = 0;
int g_last_month = -1;
int current_month() {
    uintptr_t world = rd<uintptr_t>(R(VA_G_WORLD));
    if (!world) return -1;
    uint32_t date = rd<uint32_t>(world + 0xB0C);
    if (date < 0x029C55C0u) return -1;
    uint32_t days = (date - 0x029C55C0u) / 24;
    uint32_t doy = days % 365;
    const int32_t* len = reinterpret_cast<const int32_t*>(R(VA_MONTH_LEN));
    int sum = 0;
    for (int m = 0; m < 12; ++m) sum += len[m];
    if (sum != 365) return (int)(doy / 30);
    for (int m = 0; m < 12; ++m) {
        if (doy < (uint32_t)len[m]) return m;
        doy -= len[m];
    }
    return 11;
}
void ai_treaties_monthly();
void __stdcall daytick_hook(uintptr_t world) {
    {
        PerfScope ps(P_DAY_GAME);
        reinterpret_cast<void(__stdcall*)(uintptr_t)>(g_daytick_target)(world);
    }
    int m = current_month();
    if (m >= 0 && g_last_month >= 0 && m != g_last_month) {
        {
            PerfScope ps(P_DAY_OURS);
            bloc_monthly();
            ai_treaties_monthly();
        }
        perf_report();
    }
    if (m != g_last_month) {
        g_last_month = m;
        if (g_blocwin && win_visible(g_blocwin)) bloc_panel_refresh();
    }
}

// ---- commands ----
int identity_from(const char* s) {
    for (int k = 0; k < kNumIdentities; ++k)
        if (_stricmp(s, kIdentities[k]) == 0) return k;
    if (_strnicmp(s, "mil", 3) == 0) return 0;
    if (_strnicmp(s, "com", 3) == 0 || _strnicmp(s, "tra", 3) == 0) return 1;
    if (_strnicmp(s, "imp", 3) == 0 || _strnicmp(s, "emp", 3) == 0 || _strnicmp(s, "sov", 3) == 0) return 2;
    if (_strnicmp(s, "rel", 3) == 0) return 3;
    return -1;
}
extern "C" ConsoleResult* __cdecl cmd_bloc_identity(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (arg_count(args) < 2) return reply(out, false, "Usage: bloc_identity <bloc> <military|trade|empire|religious>");
    collect_blocs();
    BlocInfo* b = find_bloc(arg_text(args, 0));
    if (!b) return reply(out, false, "No bloc called %s.", arg_text(args, 0));
    int id = identity_from(arg_text(args, 1));
    if (id < 0) return reply(out, false, "Identity: military, trade, empire or religious.");
    b->identity = id;
    set_leader(*b, b->leader);
    bloc_panel_refresh();
    return reply(out, true, "Bloc %s is now: %s. %s", b->name, kIdentityTitles[id], kIdentityEffects[id][0]);
}
extern "C" ConsoleResult* __cdecl cmd_bloc_leader(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (arg_count(args) < 1) return reply(out, false, "Usage: bloc_leader <TAG>  (the country must be in a bloc)");
    int ci = country_index(arg_text(args, 0));
    if (ci < 0) return reply(out, false, "Unknown country %s.", arg_text(args, 0));
    char b[64];
    if (!country_bloc(country_at(ci), b, sizeof(b))) return reply(out, false, "%s is in no bloc.", arg_text(args, 0));
    collect_blocs();
    BlocInfo* bi = find_bloc(b);
    if (!bi) return reply(out, false, "Bloc not found.");
    set_leader(*bi, (uint32_t)ci);
    bloc_panel_refresh();
    return reply(out, true, "%s now leads %s.", arg_text(args, 0), b);
}
// Invitation logic shared by the console and the diplomacy button. Returns 1 accepted, 0 refused, -1 not possible.
// With the vanilla "yesmen" cheat on (byte 0x012589A7) the AI always accepts.
constexpr uintptr_t VA_YESMEN = 0x012589A7;
int invite_core(BlocInfo* b, int ci, char* msg, size_t n) {
    const char* tag = cn((uint32_t)ci);
    uintptr_t c = country_at(ci);
    char cur[64], bd[64], cd[64];
    bloc_display(b->name, bd, sizeof(bd));
    if (!country_alive(c)) { snprintf(msg, n, "%s does not exist on the map.", tag); return -1; }
    if (country_bloc(c, cur, sizeof(cur))) { snprintf(msg, n, "%s is already in the %s.", tag, bloc_display(cur, cd, sizeof(cd))); return -1; }
    for (int i = 0; i < b->n; ++i)
        if (at_war_with((uint32_t)ci, b->members[i])) { snprintf(msg, n, "%s refuses: it is at war with a member of the bloc.", tag); return 0; }
    uintptr_t world = rd<uintptr_t>(R(VA_G_WORLD));
    bool player = world && rd<uint32_t>(world + 0xB60) == (uint32_t)ci;
    bool yesmen = rd<uint8_t>(R(VA_YESMEN)) != 0;
    int rel = relation(b->leader, (uint32_t)ci);
    uintptr_t lc = country_at((int)b->leader);
    bool same_religion = lc && rd<uintptr_t>(lc + 0xE68) == rd<uintptr_t>(c + 0xE68);
    int chance = 40 + rel / 5 + (same_religion ? 15 : 0) - 3 * b->n + (prestige_of(c) < prestige_of(lc) ? 10 : -10) + (b->cohesion - 50) / 5;
    if (chance < 5) chance = 5;
    if (chance > 95) chance = 95;
    bool yes = player || yesmen || (int)(rnd() % 100) < chance;
    if (yes) {
        char flag[40];
        snprintf(flag, sizeof(flag), "bloc_%s", b->name);
        flag_set(c, flag, true);
        relation_add(b->leader, (uint32_t)ci, 10);
        bloc_map_redraw();
        snprintf(msg, n, "%s accepted to join the %s%s (chance %d%%).", tag, bd, yesmen ? " [yesmen]" : "", chance);
    } else {
        relation_add(b->leader, (uint32_t)ci, -5);
        snprintf(msg, n, "%s refused to join the %s (chance %d%%, relations -5).", tag, bd, chance);
    }
    logf("bloc invite %s -> %s: chance %d%%, yesmen %d, %s", b->name, tag, chance, yesmen ? 1 : 0, yes ? "accepted" : "refused");
    return yes ? 1 : 0;
}
extern "C" ConsoleResult* __cdecl cmd_bloc_invite(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (arg_count(args) < 2) return reply(out, false, "Usage: bloc_invite <bloc> <TAG>  (the AI decides; bloc_add forces it; yesmen makes it accept)");
    collect_blocs();
    BlocInfo* b = find_bloc(arg_text(args, 0));
    if (!b) return reply(out, false, "No bloc called %s.", arg_text(args, 0));
    int ci = country_index(arg_text(args, 1));
    if (ci < 0) return reply(out, false, "Unknown country %s.", arg_text(args, 1));
    char msg[200];
    int r = invite_core(b, ci, msg, sizeof(msg));
    bloc_panel_refresh();
    return reply(out, r == 1, "%s", msg);
}
extern "C" ConsoleResult* __cdecl cmd_bloc_info(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    collect_blocs();
    if (!g_nblocs) return reply(out, false, "No blocs.");
    Text t;
    char tb[4];
    for (int k = 0; k < g_nblocs; ++k) {
        BlocInfo& b = g_blocs[k];
        if (arg_count(args) >= 1 && _stricmp(arg_text(args, 0), b.name) != 0) continue;
        t.add("%s: %s, leader %s, %d members, cohesion %d (%s)\n", b.name, kIdentityTitles[b.identity], tag_of(b.leader, tb), b.n,
              b.cohesion, cohesion_level(b.cohesion));
    }
    return finish(out, true, t);
}

// ---- the bloc panel: one bloc per page (Victoria 3 style) ----
constexpr int kRows = 14;
int g_bloc_page = 0;
char g_bloc_status[220] = "";
}  // namespace
extern "C" void tcp_bloc_prev_thunk();
extern "C" void tcp_bloc_create_thunk();
extern "C" void tcp_bloc_identity_thunk();
extern "C" void tcp_bloc_disband_thunk();
extern "C" void tcp_bloc_next_thunk();
namespace {
uint64_t country_population(uint32_t idx) {
    static uintptr_t provs[2048];
    int np = owned_provinces((int)idx, provs, 2048);
    uint64_t sum = 0;
    for (int p = 0; p < np; ++p)
        for_each_pop(provs[p], [&](uintptr_t pop, int) { if (pop_alive(pop)) sum += rd<uint32_t>(pop + 0x58); });
    return sum;
}
int country_province_count(uint32_t idx) {
    uintptr_t c = country_at((int)idx);
    return c ? (int)((rd<uintptr_t>(c + 0x9DC) - rd<uintptr_t>(c + 0x9D8)) / 4) : 0;
}
void fmt_people(uint64_t n, char* out, size_t sz) {
    if (n >= 1000000) snprintf(out, sz, "%.2fM", n / 1e6);
    else if (n >= 1000) snprintf(out, sz, "%.0fk", n / 1e3);
    else snprintf(out, sz, "%llu", (unsigned long long)n);
}
void bloc_panel_refresh() {
    if (!g_blocwin) return;
    PerfScope ps(P_PANEL);
    collect_blocs();
    tb_set(g_blocwin, "v2bloc_mapstate", g_bloc_status[0] ? g_bloc_status : " ");
    tb_set(g_blocwin, "v2bloc_maptext", g_bloc_map ? "Bloc map: ON" : "Bloc map: off");
    auto row = [&](int r, const char* a, const char* b, const char* c, const char* d, const char* e) {
        char n[24];
        const char* v[5] = {a, b, c, d, e};
        for (int col = 0; col < 5; ++col) {
            snprintf(n, sizeof(n), "v2bloc_r%dc%d", r, col);
            tb_set(g_blocwin, n, v[col] && v[col][0] ? v[col] : " ");
        }
    };
    uint32_t me = player_index();
    const char* mine = cached_bloc(me);
    tb_set(g_blocwin, "v2bloc_createtext", mine ? "Leave bloc" : "Create bloc");
    if (!g_nblocs) {
        tb_set(g_blocwin, "v2bloc_header", "Power Blocs");
        tb_set(g_blocwin, "v2bloc_info1", "There are no power blocs yet.");
        tb_set(g_blocwin, "v2bloc_info2", "Press 'Create bloc' to found one with you as its leader.");
        tb_set(g_blocwin, "v2bloc_bar", "Or open Diplomacy, select a country and press 'Form a bloc' below the diplomacy window.");
        tb_set(g_blocwin, "v2bloc_eff1", " ");
        tb_set(g_blocwin, "v2bloc_eff2", " ");
        tb_set(g_blocwin, "v2bloc_page", " ");
        tb_set(g_blocwin, "v2bloc_idtext", "Identity");
        for (int r = 0; r <= kRows; ++r) row(r, "", "", "", "", "");
        return;
    }
    if (g_bloc_page >= g_nblocs) g_bloc_page = 0;
    if (g_bloc_page < 0) g_bloc_page = g_nblocs - 1;
    BlocInfo& b = g_blocs[g_bloc_page];
    char title[96], buf[240], ln[64];
    tb_set(g_blocwin, "v2bloc_header", bloc_display(b.name, title, sizeof(title)));
    snprintf(buf, sizeof(buf), "%s   -   Leader: %s   -   %d members   -   map colour: %s", kIdentityTitles[b.identity],
             name_of(b.leader, ln, sizeof(ln)), b.n, kBlocColorNames[g_bloc_page % kPal]);
    tb_set(g_blocwin, "v2bloc_info1", buf);
    snprintf(buf, sizeof(buf), "Cohesion: %s%d / 100  -  %s" C_END "%s", b.cohesion >= 40 ? C_GREEN : C_RED, b.cohesion,
             cohesion_level(b.cohesion), b.cohesion < 20 ? "  (only the leader's wars pull the bloc in)" : "");
    tb_set(g_blocwin, "v2bloc_info2", buf);
    char bar[96];
    int fill = b.cohesion / 2, k = 0;
    k += snprintf(bar + k, sizeof(bar) - k, "%s", b.cohesion >= 40 ? C_GREEN : C_RED);
    for (int i = 0; i < 50; ++i) {
        if (i == fill) k += snprintf(bar + k, sizeof(bar) - k, C_END);
        bar[k++] = i < fill ? '|' : '.';
    }
    bar[k] = 0;
    tb_set(g_blocwin, "v2bloc_bar", bar);
    tb_set(g_blocwin, "v2bloc_eff1", kIdentityEffects[b.identity][0]);
    tb_set(g_blocwin, "v2bloc_eff2", kIdentityEffects[b.identity][1]);
    snprintf(buf, sizeof(buf), "Bloc %d of %d", g_bloc_page + 1, g_nblocs);
    tb_set(g_blocwin, "v2bloc_page", buf);
    snprintf(buf, sizeof(buf), "Identity: %s", kIdentities[b.identity]);
    tb_set(g_blocwin, "v2bloc_idtext", buf);
    row(0, "Country", "Population", "Prestige", "Provinces", "Relations");
    uint32_t order[256];
    int n = 0;
    order[n++] = b.leader;
    for (int i = 0; i < b.n; ++i)
        if (b.members[i] != b.leader) order[n++] = b.members[i];
    for (int i = 2; i < n; ++i)
        for (int q = i; q > 1 && prestige_of(country_at((int)order[q])) > prestige_of(country_at((int)order[q - 1])); --q) {
            uint32_t t = order[q]; order[q] = order[q - 1]; order[q - 1] = t;
        }
    for (int r = 1; r <= kRows; ++r) {
        int i = r - 1;
        if (i >= n || (r == kRows && n > kRows)) {
            if (r == kRows && n > kRows) { snprintf(buf, sizeof(buf), "... and %d more", n - kRows + 1); row(r, buf, "", "", "", ""); }
            else row(r, "", "", "", "", "");
            continue;
        }
        uint32_t m = order[i];
        uintptr_t c = country_at((int)m);
        char name[96], pop[32], pres[16], provs[16], rel[32];
        snprintf(name, sizeof(name), "%s%s%s%s", m == me ? C_GREEN : "", name_of(m, ln, sizeof(ln)),
                 m == b.leader ? "  (leader)" : country_alive(c) ? "" : "  (annexed)", m == me ? C_END : "");
        fmt_people(country_population(m), pop, sizeof(pop));
        snprintf(pres, sizeof(pres), "%d", prestige_of(c));
        snprintf(provs, sizeof(provs), "%d", country_province_count(m));
        if (m == b.leader) snprintf(rel, sizeof(rel), "-");
        else {
            int rv = relation(b.leader, m);
            snprintf(rel, sizeof(rel), "%s%+d" C_END, rv >= 0 ? C_GREEN : C_RED, rv);
        }
        row(r, name, pop, pres, provs, rel);
    }
}
// ---- bloc actions without the console ----
void bloc_action_create_or_leave() {
    uint32_t me = player_index();
    char t[4], buf[64];
    const char* mine = cached_bloc(me);
    uintptr_t c = country_at((int)me);
    if (!mine) {
        char name[16], flag[24];
        tag_text(ctag_lo(me), t);
        snprintf(name, sizeof(name), "pact_%c%c%c", tolower(t[0]), tolower(t[1]), tolower(t[2]));
        snprintf(flag, sizeof(flag), "bloc_%s", name);
        flag_set(c, flag, true);
        bloc_ensure_leader(name);
        collect_blocs();
        for (int k = 0; k < g_nblocs; ++k)
            if (_stricmp(g_blocs[k].name, name) == 0) g_bloc_page = k;
        snprintf(g_bloc_status, sizeof(g_bloc_status), "You founded the %s. Click countries on the map to invite them.", bloc_display(name, buf, sizeof(buf)));
    } else {
        char flag[80], keep[40];
        snprintf(keep, sizeof(keep), "%s", mine);
        snprintf(flag, sizeof(flag), "bloc_%s", keep);
        bool leader = has_flag(c, "blocleader");
        flag_set(c, flag, false);
        flag_set(c, "blocleader", false);
        if (leader) bloc_ensure_leader(keep);  // the most prestigious member takes over
        snprintf(g_bloc_status, sizeof(g_bloc_status), "You left the %s.", bloc_display(keep, buf, sizeof(buf)));
    }
    bloc_map_redraw();
    bloc_panel_refresh();
}
BlocInfo* my_led_bloc() {
    uint32_t me = player_index();
    collect_blocs();
    for (int k = 0; k < g_nblocs; ++k)
        if (g_blocs[k].leader == me) return &g_blocs[k];
    return nullptr;
}
void bloc_action_identity() {
    BlocInfo* b = my_led_bloc();
    if (!b) { snprintf(g_bloc_status, sizeof(g_bloc_status), "Only a bloc leader can change the identity."); bloc_panel_refresh(); return; }
    b->identity = (b->identity + 1) % kNumIdentities;
    set_leader(*b, b->leader);
    for (int k = 0; k < g_nblocs; ++k)
        if (&g_blocs[k] == b) g_bloc_page = k;
    snprintf(g_bloc_status, sizeof(g_bloc_status), "Identity is now %s.", kIdentityTitles[b->identity]);
    bloc_panel_refresh();
}
void bloc_action_disband() {
    BlocInfo* b = my_led_bloc();
    char buf[64];
    if (!b) { snprintf(g_bloc_status, sizeof(g_bloc_status), "Only the leader can disband a bloc."); bloc_panel_refresh(); return; }
    char name[40], flag[48];
    snprintf(name, sizeof(name), "%s", b->name);
    snprintf(flag, sizeof(flag), "bloc_%s", name);
    for_each_tag([&](uint32_t, uint32_t idx) {
        uintptr_t c = country_at((int)idx);
        if (c && has_flag(c, flag)) { flag_set(c, flag, false); flag_set(c, "blocleader", false); }
    });
    snprintf(g_bloc_status, sizeof(g_bloc_status), "The %s was disbanded.", bloc_display(name, buf, sizeof(buf)));
    bloc_map_redraw();
    bloc_panel_refresh();
}
extern "C" void __cdecl tcp_on_bloc_create() { bloc_action_create_or_leave(); }
extern "C" void __cdecl tcp_on_bloc_identity() { bloc_action_identity(); }
extern "C" void __cdecl tcp_on_bloc_disband() { bloc_action_disband(); }
asm(".text\n"
    "    .globl _tcp_bloc_create_thunk\n_tcp_bloc_create_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_bloc_create\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_bloc_identity_thunk\n_tcp_bloc_identity_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_bloc_identity\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_bloc_disband_thunk\n_tcp_bloc_disband_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_bloc_disband\n    popfl\n    popal\n    ret\n");
extern "C" void __cdecl tcp_on_bloc_prev() { --g_bloc_page; bloc_panel_refresh(); }
extern "C" void __cdecl tcp_on_bloc_next() { ++g_bloc_page; bloc_panel_refresh(); }
asm(R"(
    .text
    .globl _tcp_bloc_prev_thunk
_tcp_bloc_prev_thunk:
    pushal
    pushfl
    cld
    call  _tcp_on_bloc_prev
    popfl
    popal
    ret
    .globl _tcp_bloc_next_thunk
_tcp_bloc_next_thunk:
    pushal
    pushfl
    cld
    call  _tcp_on_bloc_next
    popfl
    popal
    ret
)");
alignas(8) uint8_t g_bloc_prev_glue[kGlueSize], g_bloc_next_glue[kGlueSize], g_bloc_create_glue[kGlueSize],
    g_bloc_identity_glue[kGlueSize], g_bloc_disband_glue[kGlueSize];
void bloc_panel_subscribe_paging() {
    subscribe(g_blocwin, "v2bloc_prev", g_bloc_prev_glue, &tcp_bloc_prev_thunk);
    subscribe(g_blocwin, "v2bloc_next", g_bloc_next_glue, &tcp_bloc_next_thunk);
    subscribe(g_blocwin, "v2bloc_create", g_bloc_create_glue, &tcp_bloc_create_thunk);
    subscribe(g_blocwin, "v2bloc_identity", g_bloc_identity_glue, &tcp_bloc_identity_thunk);
    subscribe(g_blocwin, "v2bloc_disband", g_bloc_disband_glue, &tcp_bloc_disband_thunk);
}

// ---- "Invita <TAG> in bloc" button next to the province window ----
// world+0xB24 -> vt 0x124 () = the province/country view shown when you click on the map: +0x20 = window
// (+0x67 visible), +0x24 -> selected country CTag at +0x128 / +0x12C (read the same way by the diplomatic map
// mode at 0x007FCD09). Visibility and label are synced from CInGameIdler_Update (vtable slot 0x00E0A350).
constexpr uintptr_t VA_IDLER_SLOT = 0x00E0A350, VA_IDLER_UPDATE = 0x00654D80;
typedef int(__fastcall* IdlerFn)(void*, void*, uint32_t);
IdlerFn g_idler_orig = nullptr;
void* g_diplowin = nullptr;
}  // namespace
extern "C" void tcp_diplo_invite_thunk();
namespace {
alignas(8) uint8_t g_diplo_glue[kGlueSize];

uintptr_t diplomacy_view() {
    uintptr_t world = rd<uintptr_t>(R(VA_G_WORLD));
    uintptr_t map = world ? rd<uintptr_t>(world + 0xB24) : 0;
    if (!map) return 0;
    return (uintptr_t)vcall0((void*)map, 0x124);
}
bool diplomacy_open(uint32_t* selected) {
    uintptr_t v = diplomacy_view();
    uintptr_t w = v ? rd<uintptr_t>(v + 0x20) : 0;
    if (!w || !rd<uint8_t>(w + 0x67)) return false;
    uintptr_t t = rd<uintptr_t>(v + 0x24);
    if (selected) *selected = t ? rd<uint32_t>(t + 0x12C) : 0;
    return true;
}
uint32_t player_index() {
    uintptr_t world = rd<uintptr_t>(R(VA_G_WORLD));
    return world ? rd<uint32_t>(world + 0xB60) : 0;
}

uint32_t g_dip_sel = 0;  // country selected in the Diplomacy window
extern "C" void __cdecl tcp_on_diplo_invite() {
    uint32_t sel = g_dip_sel;
    uint32_t me = player_index();
    char bloc[64];
    collect_blocs();
    if (sel && sel != me && !country_bloc(country_at((int)me), bloc, sizeof(bloc))) {
        bloc_action_create_or_leave();  // found "<Your country> Pact" with you as leader
        collect_blocs();
        country_bloc(country_at((int)me), bloc, sizeof(bloc));
    }
    if (!country_bloc(country_at((int)me), bloc, sizeof(bloc))) {
        snprintf(g_bloc_status, sizeof(g_bloc_status), "Could not create a bloc.");
    } else if (!sel || sel == me) {
        snprintf(g_bloc_status, sizeof(g_bloc_status), "Click another country on the map.");
    } else {
        BlocInfo* b = find_bloc(bloc);
        if (b && b->leader != me) {
            char tb[4];
            snprintf(g_bloc_status, sizeof(g_bloc_status), "Only the bloc leader (%s) can invite countries.", cn(b->leader)); (void)tb;
        } else if (b) {
            invite_core(b, (int)sel, g_bloc_status, sizeof(g_bloc_status));
        }
    }
    logf("bloc button: target %s -> %s", sel ? cn(sel) : "none", g_bloc_status);
    // show the result in the bloc panel, on the player's bloc
    collect_blocs();
    for (int k = 0; k < g_nblocs; ++k)
        if (_stricmp(g_blocs[k].name, bloc) == 0) g_bloc_page = k;
    tcp_on_blocs_click();
}
asm(R"(
    .text
    .globl _tcp_diplo_invite_thunk
_tcp_diplo_invite_thunk:
    pushal
    pushfl
    cld
    call  _tcp_on_diplo_invite
    popfl
    popal
    ret
)");

void treaty_frame_sync(bool open, uintptr_t prov);
uintptr_t selected_province();
void dipbar_sync();
void ai_treaties_frame();
int __fastcall idler_hook(void* self, void* edx, uint32_t a) {
    int r = g_idler_orig(self, edx, a);
    if (!g_topbar || !g_layer || !g_blocs_ok) return r;
    static int skip = 0;
    if (++skip < 6) return r;  // ~10 checks a second is plenty
    skip = 0;
    PerfScope ps(P_IDLER);
    uint32_t sel = 0;
    bool open = diplomacy_open(&sel);
    treaty_frame_sync(open, open ? selected_province() : 0);
    dipbar_sync();
    ai_treaties_frame();
    return r;
}
void install_diplo_button() {
    uint32_t* slot = (uint32_t*)R(VA_IDLER_SLOT);
    if (*slot != R(VA_IDLER_UPDATE)) { logf("diplomacy button: idler slot differs (%08X); button off", *slot); return; }
    g_idler_orig = (IdlerFn)R(VA_IDLER_UPDATE);
    uint32_t v = (uint32_t)(uintptr_t)&idler_hook;
    logf(write_code((uintptr_t)slot, &v, 4) ? "diplomacy button: frame hook installed" : "diplomacy button: VirtualProtect failed");
}

// ================= treaties: province exchange and population exchange =================
// Province transfer: the secede_province effect (CSecedeProvinceEffect::Execute 0x00887420, thiscall
// (effect, scope, unused)); effect +0x20/+0x24 = new owner CTag, scope +0x08/+0x0C = current owner CTag,
// +0x10 = province id. It moves the province, its state membership, pops' jobs and units like the event does.
// Country capital province id: country+0xBE4.
constexpr uintptr_t VA_SECEDE_EXEC = 0x00887420;
uint32_t g_tr_partner = 0;
constexpr int kTrMax = 300;
uint32_t g_tr_give[kTrMax], g_tr_take[kTrMax];
bool g_tr_pick = false;
int g_tr_popopt = 0;  // 0 both ways, 1 only theirs go home, 2 only yours come home, 3+ one culture
uintptr_t g_tr_popcult[64];
int g_tr_npopcult = 0;
int g_tr_ngive = 0, g_tr_ntake = 0;
char g_tr_status[240] = "";
void* g_treatywin = nullptr;
void* g_traddwin = nullptr;
}  // namespace
extern "C" void tcp_tr_add_thunk();
extern "C" void tcp_tr_close_thunk();
extern "C" void tcp_tr_prov_thunk();
extern "C" void tcp_tr_pop_thunk();
extern "C" void tcp_tr_clear_thunk();
extern "C" void tcp_tr_open_thunk();
extern "C" void tcp_tr_pick_thunk();
extern "C" void tcp_tr_mine_thunk();
extern "C" void tcp_tr_theirs_thunk();
extern "C" void tcp_tr_popopt_thunk();
extern "C" ConsoleResult* __cdecl cmd_migrate_all_pops(ConsoleResult* out, ArgVector* args);
namespace {
alignas(8) uint8_t g_tr_add_glue[kGlueSize], g_tr_close_glue[kGlueSize], g_tr_prov_glue[kGlueSize], g_tr_pop_glue[kGlueSize],
    g_tr_clear_glue[kGlueSize], g_tr_open_glue[kGlueSize], g_tr_pick_glue[kGlueSize], g_tr_mine_glue[kGlueSize],
    g_tr_theirs_glue[kGlueSize], g_tr_popopt_glue[kGlueSize];

uint32_t ctag_lo(uint32_t idx) {
    uint32_t lo = 0;
    for_each_tag([&](uint32_t tag, uint32_t i) { if (i == idx) lo = tag; });
    return lo;
}
const char* tag3(uint32_t idx, char* buf) { return tag_text(ctag_lo(idx), buf); }
bool national_culture(uintptr_t country, uintptr_t culture) {
    if (!country || !culture) return false;
    if (rd<uintptr_t>(country + 0xE64) == culture) return true;
    int guard = 0;
    for (uintptr_t node = rd<uintptr_t>(country + 0xE54); node && guard < 500; node = rd<uintptr_t>(node + 8), ++guard)
        if (rd<uintptr_t>(node) == culture) return true;
    return false;
}
struct ProvStats { uint64_t people, national; uintptr_t top_culture; uint64_t top_people; };
ProvStats prov_stats(uintptr_t prov, uintptr_t nation) {
    ProvStats st{0, 0, 0, 0};
    uintptr_t cul[64];
    uint64_t ppl[64];
    int nc = 0;
    for_each_pop(prov, [&](uintptr_t pop, int) {
        if (!pop_alive(pop)) return;
        uint32_t sz = rd<uint32_t>(pop + 0x58);
        uintptr_t c = rd<uintptr_t>(pop + 0x6C);
        st.people += sz;
        if (national_culture(nation, c)) st.national += sz;
        int k = 0;
        while (k < nc && cul[k] != c) ++k;
        if (k == nc && nc < 64) { cul[nc] = c; ppl[nc++] = 0; }
        if (k < nc) ppl[k] += sz;
    });
    for (int k = 0; k < nc; ++k)
        if (ppl[k] > st.top_people) { st.top_people = ppl[k]; st.top_culture = cul[k]; }
    return st;
}
// What a province is worth to a country: population, how much of it is the country's own nation, cores.
double province_value(uintptr_t prov, uint32_t country_idx) {
    ProvStats st = prov_stats(prov, country_at((int)country_idx));
    double share = st.people ? (double)st.national / st.people : 0;
    double base = 1.0 + std::min(st.people / 50000.0, 20.0);
    return base * (1.0 + 2.0 * share) * (province_has_core(prov, country_idx) ? 2.5 : 1.0);
}
double national_share(uintptr_t prov, uint32_t country_idx) {
    ProvStats st = prov_stats(prov, country_at((int)country_idx));
    return st.people ? (double)st.national / st.people : 0;
}

struct Eval {
    int chance;
    bool possible;
    char why[10][110];
    int n;
    void add(int v, const char* fmt, ...) {
        if (n >= 10) return;
        char b[100];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(b, sizeof(b), fmt, ap);
        va_end(ap);
        if (v) snprintf(why[n++], 110, "%s%+d%s  %s", v > 0 ? C_GREEN : C_RED, v, C_END, b);
        else snprintf(why[n++], 110, "%s", b);
    }
};
int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

Eval eval_provinces(uint32_t me, uint32_t ai) {
    Eval e{};
    e.possible = false;
    uintptr_t cm = country_at((int)me), ca = country_at((int)ai);
    if (!ai || !country_alive(ca)) { e.add(0, "No treaty partner: click a foreign province and add it."); return e; }
    if (!g_tr_ngive && !g_tr_ntake) { e.add(0, "The treaty is empty: add provinces from the province window."); return e; }
    if (at_war_with(me, ai)) { e.add(0, "You are at war: make peace first."); return e; }
    int cap = rd<int32_t>(ca + 0xBE4);
    double gain = 0, loss = 0;
    bool gives_home = false, gets_home = false;
    for (int i = 0; i < g_tr_ntake; ++i) {
        uintptr_t p = get_province(g_tr_take[i]);
        if (!p || rd<uint32_t>(p + 0x12C) != ai) { e.add(0, "Province %u is no longer theirs.", g_tr_take[i]); return e; }
        if ((int)g_tr_take[i] == cap) { e.add(0, "They will never give up their capital (%u).", g_tr_take[i]); return e; }
        loss += province_value(p, ai);
        if (national_share(p, ai) >= 0.5) gives_home = true;
    }
    for (int i = 0; i < g_tr_ngive; ++i) {
        uintptr_t p = get_province(g_tr_give[i]);
        if (!p || rd<uint32_t>(p + 0x12C) != me) { e.add(0, "Province %u is no longer yours.", g_tr_give[i]); return e; }
        if ((int)g_tr_give[i] == rd<int32_t>(cm + 0xBE4)) { e.add(0, "You cannot give away your capital."); return e; }
        gain += 0.8 * province_value(p, ai);
        if (national_share(p, ai) >= 0.5) gets_home = true;
    }
    e.possible = true;
    double ratio = loss > 0 ? (gain - loss) / loss : (gain > 0 ? 1.0 : 0.0);
    int c = 50 + (int)(45 * tanh(1.5 * ratio));
    e.add(c - 50, "balance of the exchange (they value %.0f received vs %.0f given)", gain, loss);
    if (gives_home && !gets_home) { c -= 25; e.add(-25, "they would give up land of their own nation"); }
    if (gets_home) { c += 15; e.add(15, "they get back land of their own people"); }
    int pm = prestige_of(cm), pa = prestige_of(ca);
    if (!g_tr_ngive) {
        int v = pm >= 2 * std::max(pa, 1) ? -5 : -20;
        c += v;
        e.add(v, "nothing offered in return%s", v == -5 ? " (but you are far stronger)" : "");
    }
    if (pm >= 2 * std::max(pa, 1)) { c += 10; e.add(10, "pressure from a much stronger power"); }
    else if (pa >= 2 * std::max(pm, 1)) { c -= 10; e.add(-10, "they are much stronger than you"); }
    int rel = relation(ai, me);
    if (rel / 5) { c += rel / 5; e.add(rel / 5, "relations %+d", rel); }
    bool same_rel = rd<uintptr_t>(cm + 0xE68) == rd<uintptr_t>(ca + 0xE68);
    c += same_rel ? 10 : -10;
    e.add(same_rel ? 10 : -10, same_rel ? "same state religion" : "different state religion");
    const char* bm = cached_bloc(me);
    const char* ba = cached_bloc(ai);
    if (bm && ba && _stricmp(bm, ba) == 0) { c += 15; e.add(15, "members of the same bloc"); }
    e.chance = clampi(c, 3, 95);
    return e;
}

struct PopEval {
    Eval e;
    uint64_t they_get, they_lose, shared;
};
// A pop of culture c may be sent to `target` only if c is target's (primary or accepted) and NOT the
// source's. Cultures both countries count as their own (e.g. German in Germany and in Austria-Hungary) stay.
bool movable_to(uintptr_t target, uintptr_t source, uintptr_t c) {
    return c && national_culture(target, c) && !national_culture(source, c);
}
// Candidate cultures for the "one culture" options: their nationals living with you, yours living with them.
void popcult_refresh(uint32_t me, uint32_t ai) {
    g_tr_npopcult = 0;
    uintptr_t cm = country_at((int)me), ca = country_at((int)ai);
    if (!ca) return;
    static uintptr_t provs[2048];
    auto scan = [&](uint32_t owner, uintptr_t nation) {
        int np = owned_provinces((int)owner, provs, 2048);
        for (int i = 0; i < np; ++i)
            for_each_pop(provs[i], [&](uintptr_t pop, int) {
                uintptr_t c = rd<uintptr_t>(pop + 0x6C);
                if (!pop_alive(pop) || !movable_to(nation, owner == me ? cm : ca, c)) return;
                for (int k = 0; k < g_tr_npopcult; ++k)
                    if (g_tr_popcult[k] == c) return;
                if (g_tr_npopcult < 64) g_tr_popcult[g_tr_npopcult++] = c;
            });
    };
    scan(me, ca);
    scan(ai, cm);
}
uintptr_t popopt_culture() { return g_tr_popopt >= 3 && g_tr_popopt - 3 < g_tr_npopcult ? g_tr_popcult[g_tr_popopt - 3] : 0; }
void popopt_label(char* out, size_t n) {
    uintptr_t c = popopt_culture();
    if (c) snprintf(out, n, "Only %s", culture_display(c));
    else snprintf(out, n, "%s", g_tr_popopt == 1 ? "Send theirs home" : g_tr_popopt == 2 ? "Bring yours home" : "Both ways");
}
PopEval eval_population(uint32_t me, uint32_t ai) {
    PopEval r{};
    Eval& e = r.e;
    uintptr_t cm = country_at((int)me), ca = country_at((int)ai);
    if (!ai || !country_alive(ca)) { e.add(0, "No treaty partner."); return r; }
    if (at_war_with(me, ai)) { e.add(0, "You are at war: make peace first."); return r; }
    uintptr_t only = popopt_culture();
    if (only && national_culture(ca, only) && national_culture(cm, only)) {
        e.add(0, "%s is a culture of both countries: it stays where it is.", culture_display(only));
        return r;
    }
    bool to_them = g_tr_popopt != 2 && (!only || national_culture(ca, only));
    bool to_you = g_tr_popopt != 1 && (!only || national_culture(cm, only));
    uint64_t my_total = 0;
    static uintptr_t provs[2048];
    uint64_t foreign_faith = 0, ai_total = 0;
    uintptr_t ai_rel = rd<uintptr_t>(ca + 0xE68);
    int np = owned_provinces((int)me, provs, 2048);
    for (int i = 0; i < np && to_them; ++i)
        for_each_pop(provs[i], [&](uintptr_t pop, int) {
            if (!pop_alive(pop)) return;
            uint32_t sz = rd<uint32_t>(pop + 0x58);
            my_total += sz;
            uintptr_t c = rd<uintptr_t>(pop + 0x6C);
            if (only ? c == only : movable_to(ca, cm, c)) r.they_get += sz;
            else if (!only && national_culture(ca, c) && national_culture(cm, c)) r.shared += sz;
        });
    np = owned_provinces((int)ai, provs, 2048);
    for (int i = 0; i < np; ++i)
        for_each_pop(provs[i], [&](uintptr_t pop, int) {
            if (!pop_alive(pop)) return;
            uint32_t sz = rd<uint32_t>(pop + 0x58);
            ai_total += sz;
            uintptr_t c = rd<uintptr_t>(pop + 0x6C);
            if (to_you && (only ? c == only : movable_to(cm, ca, c))) {
                r.they_lose += sz;
                if (rd<uintptr_t>(pop + 0x70) != ai_rel) foreign_faith += sz;
            }
        });
    if (r.shared) {
        char sh[24];
        fmt_people(r.shared, sh, sizeof(sh));
        e.add(0, "%s of your people share a culture with them and stay.", sh);
    }
    if (!r.they_get && !r.they_lose) { e.add(0, "Nobody to move with this option."); return r; }
    // safety: no treaty may uproot more than 30%% of either country
    if ((my_total && r.they_get * 10 > my_total * 3) || (ai_total && r.they_lose * 10 > ai_total * 3)) {
        Eval keep = e;
        e = Eval{};
        e.add(0, "Too large: it would move more than 30%% of a country's people.");
        for (int i = 0; i < keep.n; ++i) e.add(0, "%s", keep.why[i]);
        return r;
    }
    e.possible = true;
    int c = 30;
    e.add(0, "base 30");
    double big = (double)std::max(r.they_get, r.they_lose);
    int net = (int)(25 * tanh(((double)r.they_get - (double)r.they_lose) / big));
    c += net;
    e.add(net, "net population for them");
    int faith = r.they_lose ? (int)(20.0 * foreign_faith / r.they_lose) : 0;
    if (faith) { c += faith; e.add(faith, "the minority they send away has another faith"); }
    if (ai_total && r.they_lose * 20 >= ai_total) { c += 10; e.add(10, "the minority is large (5%% or more of them)"); }
    if (ai_total && r.they_get * 20 >= ai_total) { c -= 10; e.add(-10, "too many refugees to settle at once"); }
    int rel = relation(ai, me) / 10;
    if (rel) { c += rel; e.add(rel, "relations"); }
    const char* bm = cached_bloc(me);
    const char* ba = cached_bloc(ai);
    if (bm && ba && _stricmp(bm, ba) == 0) { c += 10; e.add(10, "members of the same bloc"); }
    e.chance = clampi(c, 3, 95);
    return r;
}

bool transfer_province(uint32_t prov_id, uint32_t to_idx) {
    uintptr_t p = get_province(prov_id);
    if (!p) return false;
    uint32_t owner = rd<uint32_t>(p + 0x12C);
    if (owner == to_idx) return true;
    uint32_t eff[32] = {0}, scope[32] = {0};
    eff[0x20 / 4] = ctag_lo(to_idx);
    eff[0x24 / 4] = to_idx;
    scope[0x08 / 4] = ctag_lo(owner);
    scope[0x0C / 4] = owner;
    scope[0x10 / 4] = prov_id;
    typedef void(__fastcall * ExecFn)(void*, void*, void*, uint32_t);
    reinterpret_cast<ExecFn>(R(VA_SECEDE_EXEC))(eff, nullptr, scope, 0);
    return rd<uint32_t>(p + 0x12C) == to_idx;
}

void run_migrate_all(uint32_t from, uint32_t to, const char* what = "accepted") {
    char a[4], b[4];
    tag3(from, a);
    tag3(to, b);
    alignas(8) uint8_t toks[3][kTokenSize];
    memset(toks, 0, sizeof(toks));
    gs_view(reinterpret_cast<GameString*>(toks[0]), a, 3);
    gs_view(reinterpret_cast<GameString*>(toks[1]), b, 3);
    gs_view(reinterpret_cast<GameString*>(toks[2]), what, strlen(what));
    ArgVector av{toks[0], toks[0] + 3 * kTokenSize, toks[0] + 3 * kTokenSize};
    ConsoleResult res;
    cmd_migrate_all_pops(&res, &av);
}

void treaty_panel_refresh();
void treaty_clear() { g_tr_partner = 0; g_tr_ngive = g_tr_ntake = 0; g_tr_popopt = 0; g_tr_npopcult = 0; }
bool in_list(uint32_t* l, int n, uint32_t v) { for (int i = 0; i < n; ++i) if (l[i] == v) return true; return false; }
void list_remove(uint32_t* l, int& n, uint32_t v) {
    for (int i = 0; i < n; ++i)
        if (l[i] == v) { l[i] = l[--n]; return; }
}
// Adds (or removes again) a province to the treaty. Foreign provinces set the partner.
void treaty_toggle(uint32_t prov_id) {
    uint32_t me = player_index();
    uintptr_t p = get_province(prov_id);
    if (!p) return;
    uint32_t owner = rd<uint32_t>(p + 0x12C);
    char t[4];
    if (!owner) { snprintf(g_tr_status, sizeof(g_tr_status), "Province %u has no owner.", prov_id); return; }
    if (owner == me) {
        if (in_list(g_tr_give, g_tr_ngive, prov_id)) list_remove(g_tr_give, g_tr_ngive, prov_id);
        else if (g_tr_ngive < kTrMax) g_tr_give[g_tr_ngive++] = prov_id;
        snprintf(g_tr_status, sizeof(g_tr_status), "Province %u %s what you offer.", prov_id,
                 in_list(g_tr_give, g_tr_ngive, prov_id) ? "added to" : "removed from");
        return;
    }
    if (g_tr_partner && owner != g_tr_partner) {
        g_tr_ntake = 0;
        g_tr_popopt = 0;
        snprintf(g_tr_status, sizeof(g_tr_status), "New partner %s: their earlier provinces were removed.", cn(owner));
    }
    g_tr_partner = owner;
    if (in_list(g_tr_take, g_tr_ntake, prov_id)) list_remove(g_tr_take, g_tr_ntake, prov_id);
    else if (g_tr_ntake < kTrMax) g_tr_take[g_tr_ntake++] = prov_id;
    if (!g_tr_status[0] || strncmp(g_tr_status, "New partner", 11) != 0)
        snprintf(g_tr_status, sizeof(g_tr_status), "Province %u %s what you ask from %s.", prov_id,
                 in_list(g_tr_take, g_tr_ntake, prov_id) ? "added to" : "removed from", cn(owner));
}

bool roll(int chance) {
    if (rd<uint8_t>(R(VA_YESMEN))) return true;
    return (int)(rnd() % 100) < chance;
}
void treaty_propose_provinces() {
    uint32_t me = player_index(), ai = g_tr_partner;
    Eval e = eval_provinces(me, ai);
    char t[4];
    if (!e.possible) { snprintf(g_tr_status, sizeof(g_tr_status), "%s", e.n ? e.why[0] : "Not possible."); return; }
    bool yes = roll(e.chance);
    logf("treaty provinces with %s: chance %d%%, %s", tag3(ai, t), e.chance, yes ? "accepted" : "refused");
    if (!yes) {
        relation_add(me, ai, -10);
        snprintf(g_tr_status, sizeof(g_tr_status), "%s refused the exchange (chance %d%%). Relations -10.", cn(ai), e.chance);
        return;
    }
    int moved = 0;
    for (int i = 0; i < g_tr_ntake; ++i) moved += transfer_province(g_tr_take[i], me);
    for (int i = 0; i < g_tr_ngive; ++i) moved += transfer_province(g_tr_give[i], ai);
    relation_add(me, ai, 10);
    snprintf(g_tr_status, sizeof(g_tr_status), "%s accepted%s: %d province(s) changed hands. Relations +10.", cn(ai),
             rd<uint8_t>(R(VA_YESMEN)) ? " [yesmen]" : "", moved);
    treaty_clear();
    g_tr_partner = ai;
    g_bloc_cache_dirty = true;
    bloc_map_redraw();
}
void treaty_propose_population() {
    uint32_t me = player_index(), ai = g_tr_partner;
    char t[4];
    PopEval pe = eval_population(me, ai);
    if (!pe.e.possible) { snprintf(g_tr_status, sizeof(g_tr_status), "%s", pe.e.n ? pe.e.why[0] : "Not possible."); return; }
    if (!g_merge_fix_ok) { snprintf(g_tr_status, sizeof(g_tr_status), "Population exchange disabled (merge fix missing)."); return; }
    bool yes = roll(pe.e.chance);
    logf("treaty population with %s: chance %d%%, %s", tag3(ai, t), pe.e.chance, yes ? "accepted" : "refused");
    if (!yes) {
        relation_add(me, ai, -5);
        snprintf(g_tr_status, sizeof(g_tr_status), "%s refused the population exchange (chance %d%%). Relations -5.", cn(ai), pe.e.chance);
        return;
    }
    uintptr_t only = popopt_culture();
    uintptr_t cm = country_at((int)me), ca = country_at((int)ai);
    if (only) {
        if (national_culture(cm, only)) run_migrate_all(ai, me, culture_name(only));
        else run_migrate_all(me, ai, culture_name(only));
    } else {
        // culture by culture, and only cultures that belong to one side (shared ones stay)
        popcult_refresh(me, ai);
        for (int k = 0; k < g_tr_npopcult; ++k) {
            uintptr_t c = g_tr_popcult[k];
            if (g_tr_popopt != 1 && movable_to(cm, ca, c)) run_migrate_all(ai, me, culture_name(c));
            if (g_tr_popopt != 2 && movable_to(ca, cm, c)) run_migrate_all(me, ai, culture_name(c));
        }
        popcult_refresh(me, ai);
    }
    snprintf(g_tr_status, sizeof(g_tr_status), "%s accepted%s: about %llu people came to you, %llu went to them.", cn(ai),
             rd<uint8_t>(R(VA_YESMEN)) ? " [yesmen]" : "", (unsigned long long)pe.they_lose, (unsigned long long)pe.they_get);
}

// ---- the treaty panel ----
constexpr int kTrLines = 24;
// Province line for the two columns: "Paris  1.21M  mostly North German  (their core)"
void prov_row(char* out, size_t n, uint32_t id, uint32_t me, uint32_t ai, uint32_t nation) {
    uintptr_t p = get_province(id);
    if (!p) { snprintf(out, n, "%s", pn(id)); return; }
    ProvStats st = prov_stats(p, country_at((int)nation));
    char ppl[24];
    fmt_people(st.people, ppl, sizeof(ppl));
    const char* core = province_has_core(p, ai) && province_has_core(p, me) ? "  (both cores)"
                     : province_has_core(p, ai) ? "  (their core)" : province_has_core(p, me) ? "  (your core)" : "";
    snprintf(out, n, "%s   %s, %s%s", pn(id), ppl, culture_display(st.top_culture), core);
}
constexpr int kTrRows = 8, kTrWhy = 5, kTrPopWhy = 3;
void treaty_panel_refresh() {
    if (!g_treatywin) return;
    uint32_t me = player_index(), ai = g_tr_partner;
    char buf[200], nm[24];
    if (ai) snprintf(buf, sizeof(buf), "Treaty with %s", cn(ai));
    else snprintf(buf, sizeof(buf), "Treaty");
    tb_set(g_treatywin, "v2tr_header", buf);
    auto column = [&](const char* prefix, const char* head, uint32_t* l, int cnt) {
        uint64_t total = 0;
        for (int i = 0; i < cnt; ++i)
            if (uintptr_t p = get_province(l[i])) total += prov_stats(p, 0).people;
        char ppl[24];
        fmt_people(total, ppl, sizeof(ppl));
        if (cnt) snprintf(buf, sizeof(buf), "%s  -  %d province%s, %s people", head, cnt, cnt == 1 ? "" : "s", ppl);
        else snprintf(buf, sizeof(buf), "%s", head);
        snprintf(nm, sizeof(nm), "%sh", prefix);
        tb_set(g_treatywin, nm, buf);
        for (int r = 0; r < kTrRows; ++r) {
            snprintf(nm, sizeof(nm), "%s%d", prefix, r);
            if (r == kTrRows - 1 && cnt > kTrRows) snprintf(buf, sizeof(buf), "... and %d more", cnt - kTrRows + 1);
            else if (r < cnt) prov_row(buf, sizeof(buf), l[r], me, ai, ai);
            else if (r == 0) snprintf(buf, sizeof(buf), "(nothing)");
            else buf[0] = 0;
            tb_set(g_treatywin, nm, buf[0] ? buf : " ");
        }
    };
    column("v2tr_g", "YOU GIVE", g_tr_give, g_tr_ngive);
    char th[64];
    snprintf(th, sizeof(th), "%s GIVES", ai ? cn(ai) : "THEY");
    for (char* q = th; *q; ++q) *q = (char)toupper((unsigned char)*q);
    column("v2tr_t", th, g_tr_take, g_tr_ntake);
    // province exchange
    Eval e = eval_provinces(me, ai);
    if (!ai) snprintf(buf, sizeof(buf), " ");
    else if (e.possible) snprintf(buf, sizeof(buf), "Chance: %s%d%%" C_END, e.chance >= 50 ? C_GREEN : C_RED, e.chance);
    else snprintf(buf, sizeof(buf), C_RED "Not possible" C_END);
    tb_set(g_treatywin, "v2tr_pchance", buf);
    for (int i = 0; i < kTrWhy; ++i) {
        snprintf(nm, sizeof(nm), "v2tr_pw%d", i);
        if (!ai && i == 0) tb_set(g_treatywin, nm, "Open Diplomacy, select a country and press 'Propose treaty'.");
        else if (!ai && i == 1) tb_set(g_treatywin, nm, "Then turn on 'Pick on map' and click provinces: yours are offered, theirs are asked for.");
        else if (i == kTrWhy - 1 && e.n > kTrWhy) { snprintf(buf, sizeof(buf), "... %d more reasons", e.n - kTrWhy + 1); tb_set(g_treatywin, nm, buf); }
        else tb_set(g_treatywin, nm, ai && i < e.n ? e.why[i] : " ");
    }
    // population swap
    PopEval pe{};
    if (ai) pe = eval_population(me, ai);
    char opt[64];
    popopt_label(opt, sizeof(opt));
    tb_set(g_treatywin, "v2tr_popopt_text", opt);
    if (!ai) snprintf(buf, sizeof(buf), " ");
    else if (pe.e.possible) snprintf(buf, sizeof(buf), "Chance: %s%d%%" C_END, pe.e.chance >= 50 ? C_GREEN : C_RED, pe.e.chance);
    else snprintf(buf, sizeof(buf), C_RED "Not possible" C_END);
    tb_set(g_treatywin, "v2tr_popchance", buf);
    if (ai) {
        char a2[24], b2[24];
        fmt_people(pe.they_lose, a2, sizeof(a2));
        fmt_people(pe.they_get, b2, sizeof(b2));
        snprintf(buf, sizeof(buf), "You receive " C_GREEN "%s" C_END " of your people,  %s receives " C_GREEN "%s" C_END " of theirs.", a2, cn(ai), b2);
    } else snprintf(buf, sizeof(buf), " ");
    tb_set(g_treatywin, "v2tr_popsum", buf);
    for (int i = 0; i < kTrPopWhy; ++i) {
        snprintf(nm, sizeof(nm), "v2tr_ow%d", i);
        tb_set(g_treatywin, nm, ai && i < pe.e.n ? pe.e.why[i] : " ");
    }
    tb_set(g_treatywin, "v2tr_status", g_tr_status[0] ? g_tr_status : " ");
    tb_set(g_treatywin, "v2tr_pick_text", g_tr_pick ? C_GREEN "Pick on map: ON" C_END : "Pick on map: off");
}
void treaty_open() {
    if (!g_treatywin) {
        g_treatywin = gui_create("v2treaty_window");
        if (!g_treatywin) return;
        subscribe(g_treatywin, "close_button", g_tr_close_glue, &tcp_tr_close_thunk);
        subscribe(g_treatywin, "v2tr_prov", g_tr_prov_glue, &tcp_tr_prov_thunk);
        subscribe(g_treatywin, "v2tr_pop", g_tr_pop_glue, &tcp_tr_pop_thunk);
        subscribe(g_treatywin, "v2tr_clear", g_tr_clear_glue, &tcp_tr_clear_thunk);
        subscribe(g_treatywin, "v2tr_pick", g_tr_pick_glue, &tcp_tr_pick_thunk);
        subscribe(g_treatywin, "v2tr_mine", g_tr_mine_glue, &tcp_tr_mine_thunk);
        subscribe(g_treatywin, "v2tr_theirs", g_tr_theirs_glue, &tcp_tr_theirs_thunk);
        subscribe(g_treatywin, "v2tr_popopt", g_tr_popopt_glue, &tcp_tr_popopt_thunk);
        logf("treaty: window created");
    }
    treaty_panel_refresh();
    win_show(g_treatywin, true);
}
uintptr_t selected_province() {
    uintptr_t v = diplomacy_view();
    uintptr_t w = v ? rd<uintptr_t>(v + 0x20) : 0;
    if (!w || !rd<uint8_t>(w + 0x67)) return 0;
    return rd<uintptr_t>(v + 0x24);
}
extern "C" void __cdecl tcp_on_tr_add() {
    uintptr_t p = selected_province();
    if (p) treaty_toggle(rd<uint32_t>(p + 0x58));
    treaty_open();
}
extern "C" void __cdecl tcp_on_tr_close() { win_show(g_treatywin, false); }
extern "C" void __cdecl tcp_on_tr_prov() { treaty_propose_provinces(); treaty_panel_refresh(); }
extern "C" void __cdecl tcp_on_tr_pop() { treaty_propose_population(); treaty_panel_refresh(); }
extern "C" void __cdecl tcp_on_tr_clear() { treaty_clear(); snprintf(g_tr_status, sizeof(g_tr_status), "Treaty cleared."); treaty_panel_refresh(); }
extern "C" void __cdecl tcp_on_tr_open() { treaty_open(); }
extern "C" void __cdecl tcp_on_tr_pick() {
    g_tr_pick = !g_tr_pick;
    snprintf(g_tr_status, sizeof(g_tr_status), g_tr_pick ? "Pick mode ON: every province you click on the map is added (or removed)."
                                                         : "Pick mode off.");
    treaty_panel_refresh();
}
// Adds every province of `owner` where `nation`'s people are the majority.
int add_majority(uint32_t owner, uint32_t nation, uint32_t* list, int& cnt) {
    static uintptr_t provs[2048];
    int np = owned_provinces((int)owner, provs, 2048), added = 0;
    int cap = rd<int32_t>(country_at((int)owner) + 0xBE4);
    for (int i = 0; i < np && cnt < kTrMax; ++i) {
        uint32_t id = rd<uint32_t>(provs[i] + 0x58);
        if ((int)id == cap || in_list(list, cnt, id)) continue;
        if (national_share(provs[i], nation) >= 0.5) { list[cnt++] = id; ++added; }
    }
    return added;
}
extern "C" void __cdecl tcp_on_tr_mine() {  // their land where my people live
    if (!g_tr_partner) { snprintf(g_tr_status, sizeof(g_tr_status), "Choose a partner first: add one of their provinces."); }
    else {
        char t[4];
        int a = add_majority(g_tr_partner, player_index(), g_tr_take, g_tr_ntake);
        snprintf(g_tr_status, sizeof(g_tr_status), "Asked for %d province(s) of %s where your people are the majority.", a, cn(g_tr_partner));
    }
    treaty_panel_refresh();
}
extern "C" void __cdecl tcp_on_tr_theirs() {  // my land where their people live
    if (!g_tr_partner) { snprintf(g_tr_status, sizeof(g_tr_status), "Choose a partner first: add one of their provinces."); }
    else {
        char t[4];
        int a = add_majority(player_index(), g_tr_partner, g_tr_give, g_tr_ngive);
        snprintf(g_tr_status, sizeof(g_tr_status), "Offered %d province(s) of yours where %s's people are the majority.", a, cn(g_tr_partner));
    }
    treaty_panel_refresh();
}
extern "C" void __cdecl tcp_on_tr_popopt() {
    popcult_refresh(player_index(), g_tr_partner);
    g_tr_popopt = (g_tr_popopt + 1) % (3 + g_tr_npopcult);
    treaty_panel_refresh();
}
#define TCP_THUNK(n) "    .globl _" #n "_thunk\n_" #n "_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_" #n "\n    popfl\n    popal\n    ret\n"
asm(".text\n"
    "    .globl _tcp_tr_add_thunk\n_tcp_tr_add_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_tr_add\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_tr_close_thunk\n_tcp_tr_close_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_tr_close\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_tr_prov_thunk\n_tcp_tr_prov_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_tr_prov\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_tr_pop_thunk\n_tcp_tr_pop_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_tr_pop\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_tr_clear_thunk\n_tcp_tr_clear_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_tr_clear\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_tr_open_thunk\n_tcp_tr_open_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_tr_open\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_tr_pick_thunk\n_tcp_tr_pick_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_tr_pick\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_tr_mine_thunk\n_tcp_tr_mine_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_tr_mine\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_tr_theirs_thunk\n_tcp_tr_theirs_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_tr_theirs\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_tr_popopt_thunk\n_tcp_tr_popopt_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_tr_popopt\n    popfl\n    popal\n    ret\n");
#undef TCP_THUNK

void treaty_subscribe_cmd(void* win) { subscribe(win, "v2cmd_treaty", g_tr_open_glue, &tcp_tr_open_thunk); }

// per-frame: the "Treaty" button next to the province window
void treaty_frame_sync(bool open, uintptr_t prov) {
    uint32_t me = player_index();
    static uint32_t last_pick = 0;
    if (!open || !prov) last_pick = 0;
    else if (g_tr_pick && g_treatywin && win_visible(g_treatywin)) {
        uint32_t id = rd<uint32_t>(prov + 0x58);
        if (id != last_pick) {
            last_pick = id;
            treaty_toggle(id);
            treaty_panel_refresh();
        }
    }
    (void)me;
}


// ================= Diplomacy window integration =================
// CDiplomacyWindow (vtable 0x00E07450) is built by 0x0061B820 (stdcall-like: [esp+4] = this). We detour its
// prologue (55 8B EC 6A FF) to remember the object. Its gui window is at +0x4C (vt 7 = IsVisible reads
// [this+0x4C]+0x67). The selected country is a CTag inside the object (+0x388/+0x38C).
}  // namespace
extern "C" { uintptr_t g_dipobj = 0; uintptr_t g_dipctor_cont = 0; }
extern "C" void tcp_dip_treaty_thunk();
extern "C" void tcp_dip_bloc_thunk();
extern "C" void tcp_dip_blocs_thunk();
extern "C" void tcp_dipctor_hook();
namespace {

void* g_dipbar = nullptr;
alignas(8) uint8_t g_dip_treaty_glue[kGlueSize], g_dip_bloc_glue[kGlueSize], g_dip_blocs_glue[kGlueSize];
bool dip_visible() {
    if (!g_dipobj || IsBadReadPtr((void*)g_dipobj, 0x60) || rd<uintptr_t>(g_dipobj) != R(VA_DIPWIN_VT)) return false;
    uintptr_t gui = rd<uintptr_t>(g_dipobj + 0x4C);
    return gui && rd<uint8_t>(gui + 0x67) != 0;
}
// The window keeps the selected country as a CTag: lo at +0x388, index at +0x38C (see 0x0062761D, which also
// falls back to the player when the selected country owns no provinces). Works with any mod or language.
uint32_t dip_selected() {
    uint32_t idx = rd<uint32_t>(g_dipobj + 0x38C);
    uintptr_t c = country_at((int)idx);
    return c && country_alive(c) ? idx : 0;
}
// Our windows live on a lower layer than the diplomacy screen, so close it before opening one of ours.
void dip_hide() {
    if (!dip_visible()) return;
    win_show((void*)rd<uintptr_t>(g_dipobj + 0x4C), false);
    if (g_dipbar) win_show(g_dipbar, false);
}
extern "C" void __cdecl tcp_on_dip_treaty() {
    uint32_t sel = g_dip_sel;
    dip_hide();
    if (!sel || sel == player_index()) return;
    if (g_tr_partner != sel) {
        treaty_clear();
        g_tr_partner = sel;
        snprintf(g_tr_status, sizeof(g_tr_status), "Turn on 'Pick on map' and click provinces, or use '+ My people' / '+ Their people'.");
    }
    popcult_refresh(player_index(), sel);
    treaty_open();
}
extern "C" void __cdecl tcp_on_dip_bloc() {
    uint32_t sel = g_dip_sel, me = player_index();
    if (!sel || sel == me) return;
    dip_hide();
    const char* theirs = cached_bloc(sel);
    const char* mine = cached_bloc(me);
    if (theirs) {  // show that bloc
        collect_blocs();
        for (int k = 0; k < g_nblocs; ++k)
            if (_stricmp(g_blocs[k].name, theirs) == 0) g_bloc_page = k;
        if (!(mine && _stricmp(mine, theirs) == 0)) g_bloc_status[0] = 0;
        tcp_on_blocs_click();
        return;
    }
    tcp_on_diplo_invite();
}
extern "C" void __cdecl tcp_on_dip_blocs() { dip_hide(); tcp_on_blocs_click(); }
}  // namespace
asm(".text\n"
    "    .globl _tcp_dip_treaty_thunk\n_tcp_dip_treaty_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_dip_treaty\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_dip_bloc_thunk\n_tcp_dip_bloc_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_dip_bloc\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_dip_blocs_thunk\n_tcp_dip_blocs_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_dip_blocs\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_toast_thunk\n_tcp_toast_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_toast\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_req_acc_thunk\n_tcp_req_acc_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_req_acc\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_req_dec_thunk\n_tcp_req_dec_thunk:\n    pushal\n    pushfl\n    cld\n    call  _tcp_on_req_dec\n    popfl\n    popal\n    ret\n"
    "    .globl _tcp_dipctor_hook\n_tcp_dipctor_hook:\n    movl  4(%esp), %eax\n    movl  %eax, _g_dipobj\n    pushl %ebp\n    movl  %esp, %ebp\n    pushl $-1\n    jmp   *_g_dipctor_cont\n");
namespace {

void dipbar_sync() {
    bool open = dip_visible();
    uint32_t me = player_index();
    uint32_t sel = open ? dip_selected() : 0;
    g_dip_sel = sel;
    bool want = open && sel && sel != me;
    if (want && !g_dipbar) {
        g_dipbar = gui_create("v2dip_bar");
        if (g_dipbar) {
            subscribe(g_dipbar, "v2dip_treaty", g_dip_treaty_glue, &tcp_dip_treaty_thunk);
            subscribe(g_dipbar, "v2dip_bloc", g_dip_bloc_glue, &tcp_dip_bloc_thunk);
            subscribe(g_dipbar, "v2dip_blocs", g_dip_blocs_glue, &tcp_dip_blocs_thunk);
            logf("diplomacy: action bar created");
        }
    }
    if (!g_dipbar) return;
    if (want) {
        static uint32_t last_sel = 0;
        static char last_key[160] = "";
        const char* theirs = cached_bloc(sel);
        const char* mine = cached_bloc(me);
        char bd[64], label[64], info[200], key[160];
        if (theirs && mine && _stricmp(theirs, mine) == 0) snprintf(label, sizeof(label), "In your bloc");
        else if (theirs) snprintf(label, sizeof(label), "View %s", bloc_display(theirs, bd, sizeof(bd)));
        else if (!mine) snprintf(label, sizeof(label), "Form a bloc");
        else snprintf(label, sizeof(label), "Invite to bloc");
        int rel = relation(me, sel);
        snprintf(info, sizeof(info), "%s   -   Bloc: %s   -   Relations: %s%+d" C_END, cn(sel),
                 theirs ? bloc_display(theirs, bd, sizeof(bd)) : "none", rel >= 0 ? C_GREEN : C_RED, rel);
        snprintf(key, sizeof(key), "%u|%s|%s", sel, label, info);
        if (sel != last_sel || strcmp(key, last_key) != 0) {
            tb_set(g_dipbar, "v2dip_bloctext", label);
            tb_set(g_dipbar, "v2dip_info", info);
            last_sel = sel;
            snprintf(last_key, sizeof(last_key), "%s", key);
        }
    }
    if (win_visible(g_dipbar) != want) win_show(g_dipbar, want);
}
void install_dipwin_capture() {
    uint8_t* site = (uint8_t*)R(VA_DIPWIN_CTOR);
    const uint8_t expect[5] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};
    if (memcmp(site, expect, 5) != 0) { logf("diplomacy: window constructor differs; diplomacy buttons off"); return; }
    g_dipctor_cont = R(VA_DIPWIN_CTOR) + 5;
    uint8_t jmp[5] = {0xE9};
    int32_t rel = (int32_t)((uintptr_t)&tcp_dipctor_hook - (R(VA_DIPWIN_CTOR) + 5));
    memcpy(jmp + 1, &rel, 4);
    logf(write_code(R(VA_DIPWIN_CTOR), jmp, 5) ? "diplomacy: window hook installed" : "diplomacy: VirtualProtect failed");
}

#include "ai_treaties.inc"

// CSecedeProvinceEffect::Execute (vtable slot 0x00E2B004 -> 0x00887420, thiscall (scope, x), ret 8). Hooked to
// recognise historical treaties carried out by the game's own decisions and events. transfer_province calls
// the function directly, so the plugin's own transfers are not seen here.
constexpr uintptr_t VA_SECEDE_SLOT = 0x00E2B004;
typedef void(__fastcall* SecedeFn)(void*, void*, void*, uint32_t);
SecedeFn g_secede_orig = nullptr;
void __fastcall secede_hook(void* eff, void* edx, void* scope, uint32_t x) {
    uint32_t id = scope ? rd<uint32_t>((uintptr_t)scope + 0x10) : 0;
    uintptr_t p = id ? get_province(id) : 0;
    uint32_t before = p ? rd<uint32_t>(p + 0x12C) : 0;
    g_secede_orig(eff, edx, scope, x);
    uint32_t after = p ? rd<uint32_t>(p + 0x12C) : 0;
    if (p && before && after && before != after && !g_hist.empty()) native_secede(id, before, after);
}
void install_secede_watch() {
    uint32_t* slot = (uint32_t*)R(VA_SECEDE_SLOT);
    if (*slot != R(VA_SECEDE_EXEC)) { logf("treaties: secede effect slot differs (%08X); game treaty events not recognised", *slot); return; }
    g_secede_orig = (SecedeFn)R(VA_SECEDE_EXEC);
    uint32_t v = (uint32_t)(uintptr_t)&secede_hook;
    logf(write_code((uintptr_t)slot, &v, 4) ? "treaties: watching the game's own treaty events" : "treaties: VirtualProtect failed");
}
// ---- console ----
extern "C" ConsoleResult* __cdecl cmd_treaty(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (arg_count(args) < 1)
        return reply(out, false, "Usage: treaty <TAG> [give <province ids>] [take <province ids>]  (opens the treaty window)");
    int ci = country_index(arg_text(args, 0));
    if (ci < 0) return reply(out, false, "Unknown country %s.", arg_text(args, 0));
    treaty_clear();
    g_tr_partner = (uint32_t)ci;
    int mode = 0;
    for (int i = 1; i < arg_count(args); ++i) {
        const char* a = arg_text(args, i);
        uint32_t id;
        if (_stricmp(a, "give") == 0) mode = 1;
        else if (_stricmp(a, "take") == 0) mode = 2;
        else if (parse_uint(a, &id) && mode == 1 && g_tr_ngive < kTrMax) g_tr_give[g_tr_ngive++] = id;
        else if (parse_uint(a, &id) && mode == 2 && g_tr_ntake < kTrMax) g_tr_take[g_tr_ntake++] = id;
    }
    g_tr_status[0] = 0;
    treaty_open();
    Eval e = eval_provinces(player_index(), g_tr_partner);
    PopEval pe = eval_population(player_index(), g_tr_partner);
    return reply(out, true, "Treaty with %s: province exchange %s%d%%, population exchange %s%d%%. Use the treaty window, or treaty_propose / treaty_population.",
                 arg_text(args, 0), e.possible ? "" : "impossible ", e.chance, pe.e.possible ? "" : "impossible ", pe.e.chance);
}
extern "C" ConsoleResult* __cdecl cmd_treaty_propose(ConsoleResult* out, ArgVector*) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    treaty_propose_provinces();
    treaty_panel_refresh();
    return reply(out, true, "%s", g_tr_status);
}
extern "C" ConsoleResult* __cdecl cmd_treaty_population(ConsoleResult* out, ArgVector* args) {
    init_result(out);
    if (ConsoleResult* g = gate(out)) return g;
    if (arg_count(args) >= 1) {
        int ci = country_index(arg_text(args, 0));
        if (ci < 0) return reply(out, false, "Unknown country %s.", arg_text(args, 0));
        g_tr_partner = (uint32_t)ci;
    }
    treaty_propose_population();
    treaty_panel_refresh();
    return reply(out, true, "%s", g_tr_status);
}

void drop_diplo_window() { win_destroy(g_diplowin); win_destroy(g_traddwin); win_destroy(g_treatywin); win_destroy(g_dipbar); win_destroy(g_toast); win_destroy(g_reqwin); }

void install_bloc_tick() {
    uintptr_t site = R(VA_DAYTICK_CALL);
    if (*(uint8_t*)site != 0xE8) { logf("bloc tick: call at 0x683AB1 not found; monthly bloc effects off"); return; }
    g_daytick_target = site + 5 + *(int32_t*)(site + 1);
    if (g_daytick_target != R(VA_DAYTICK)) logf("bloc tick: DayTick call already redirected by another DLL, chaining it");
    int32_t rel = (int32_t)((uintptr_t)&daytick_hook - (site + 5));
    logf(write_code(site + 1, &rel, 4) ? "bloc tick: monthly bloc effects installed" : "bloc tick: VirtualProtect failed");
}

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


void install_bloc_map() {
    const uintptr_t sites[2] = {R(VA_POL_CALL_MODE), R(VA_POL_CALL_OTHER)};
    for (uintptr_t site : sites) {
        if (*(uint8_t*)site != 0xE8 || site + 5 + *(int32_t*)(site + 1) != R(VA_POL_PALETTE)) {
            logf("bloc map: call at %08X differs; bloc map disabled", (unsigned)site);
            return;
        }
    }
    g_pol_target = R(VA_POL_PALETTE);
    tcp_pol_fn = (void*)&pol_palette_hook;
    int32_t r1 = (int32_t)((uintptr_t)&tcp_pol_mode_thunk - (sites[0] + 5));
    int32_t r2 = (int32_t)((uintptr_t)&pol_palette_hook - (sites[1] + 5));
    g_bloc_map_ok = write_code(sites[0] + 1, &r1, 4) && write_code(sites[1] + 1, &r2, 4);
    logf(g_bloc_map_ok ? "bloc map: political palette hooked" : "bloc map: VirtualProtect failed");
}

void read_settings() {
    g_require_mod = GetPrivateProfileIntW(L"convert_pop", L"require_mod", 0, g_host->config_file) != 0;
    g_fix_merge_everywhere = GetPrivateProfileIntW(L"convert_pop", L"fix_merge_everywhere", 0, g_host->config_file) != 0;
    g_tab_enabled = GetPrivateProfileIntW(L"convert_pop", L"topbar_tab", 1, g_host->config_file) != 0;
    g_blocs_enabled = GetPrivateProfileIntW(L"convert_pop", L"blocs", 1, g_host->config_file) != 0;
    g_ai_treaties = GetPrivateProfileIntW(L"convert_pop", L"ai_treaties", 1, g_host->config_file) != 0;
    g_dyn_years = GetPrivateProfileIntW(L"convert_pop", L"treaty_ai_years", 10, g_host->config_file);
    g_req_years = GetPrivateProfileIntW(L"convert_pop", L"treaty_request_years", 6, g_host->config_file);
    if (g_ai_treaties) { load_treaties(); install_secede_watch(); }
    const char* cmd = GetCommandLineA();
    char lower[4096];
    size_t n = cmd ? strlen(cmd) : 0;
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (size_t i = 0; i < n; ++i) lower[i] = (char)tolower((unsigned char)cmd[i]);
    lower[n] = 0;
    g_mod_active = strstr(lower, "tgcconvertpop") != nullptr;
    logf("TGC Convert Pop build 2026-10-27e (treaties with a game decision/event run natively; plugin recognises them)");
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
    if (g_blocs_ok) install_bloc_map();
    if (g_blocs_ok) install_bloc_tick();
    if (g_blocs_ok && g_tab_enabled) {
        install_dipwin_capture();
        install_diplo_button();
    }
    return install_commands() ? 0 : 2;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
