// Victoria 2 launcher that remembers the selected mods (replaces victoria2.exe; the original is kept as
// victoria2_original.exe). Works for any mod set, vanilla included. Selection and order are saved in
// launcher\mods_selected.txt next to the game.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <olectl.h>
#include <ocidl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>

struct Mod { std::wstring file, name; bool on = false; };
static std::vector<Mod> g_mods;
static std::wstring g_dir;
static HWND g_list, g_status, g_auto, g_wnd;
static IPicture* g_pic = nullptr;
static HFONT g_font, g_big;
static bool g_autostart = false;

enum { ID_LIST = 100, ID_UP, ID_DOWN, ID_NONE, ID_PLAY, ID_ORIG, ID_FOLDER, ID_AUTO };

static std::wstring path(const std::wstring& rel) { return g_dir + L"\\" + rel; }
static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(1252, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(1252, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(1252, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(1252, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
// name = "..." from a .mod file
static std::wstring mod_name(const std::wstring& file) {
    FILE* f = _wfopen(path(L"mod\\" + file).c_str(), L"rb");
    std::wstring r = file.substr(0, file.size() - 4);
    if (!f) return r;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t' || *p == '\xEF' || *p == '\xBB' || *p == '\xBF') ++p;
        if (_strnicmp(p, "name", 4) != 0) continue;
        p += 4;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p != '=') continue;
        char* q1 = strchr(p, '"');
        char* q2 = q1 ? strchr(q1 + 1, '"') : nullptr;
        if (q1 && q2) { r = widen(std::string(q1 + 1, q2)); break; }
    }
    fclose(f);
    return r;
}
static void load() {
    std::vector<Mod> found;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path(L"mod\\*.mod").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) found.push_back({fd.cFileName, mod_name(fd.cFileName), false});
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(found.begin(), found.end(), [](const Mod& a, const Mod& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    // saved order first (lines "1 file.mod" / "0 file.mod"), then any new mods
    FILE* f = _wfopen(path(L"launcher\\mods_selected.txt").c_str(), L"rb");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
            if (!strncmp(line, "autostart=", 10)) { g_autostart = line[10] == '1'; continue; }
            if (n < 3 || (line[0] != '0' && line[0] != '1') || line[1] != ' ') continue;
            std::wstring file = widen(line + 2);
            for (size_t i = 0; i < found.size(); ++i)
                if (_wcsicmp(found[i].file.c_str(), file.c_str()) == 0) {
                    found[i].on = line[0] == '1';
                    g_mods.push_back(found[i]);
                    found.erase(found.begin() + i);
                    break;
                }
        }
        fclose(f);
    }
    for (auto& m : found) g_mods.push_back(m);
}
static void save() {
    FILE* f = _wfopen(path(L"launcher\\mods_selected.txt").c_str(), L"wb");
    if (!f) return;
    fprintf(f, "# Victoria 2 launcher: mods in load order, 1 = selected\r\nautostart=%d\r\n", g_autostart ? 1 : 0);
    for (auto& m : g_mods) fprintf(f, "%d %s\r\n", m.on ? 1 : 0, narrow(m.file).c_str());
    fclose(f);
}
static void status() {
    int n = 0;
    for (auto& m : g_mods) n += m.on;
    wchar_t b[200];
    if (n) swprintf(b, 200, L"%d mod%s selected - loaded top to bottom. Your choice is remembered.", n, n == 1 ? L"" : L"s");
    else swprintf(b, 200, L"No mod selected: vanilla Victoria 2.");
    SetWindowTextW(g_status, b);
}
static bool g_filling = false;
static void fill(int sel) {
    g_filling = true;
    ListView_DeleteAllItems(g_list);
    for (size_t i = 0; i < g_mods.size(); ++i) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = (int)i;
        it.pszText = (LPWSTR)g_mods[i].name.c_str();
        ListView_InsertItem(g_list, &it);
        ListView_SetCheckState(g_list, (int)i, g_mods[i].on);
    }
    if (sel >= 0) {
        ListView_SetItemState(g_list, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_list, sel, FALSE);
    }
    g_filling = false;
    status();
}
static void launch_game() {
    save();
    std::wstring cmd = L"\"v2game.exe\" ";
    for (auto& m : g_mods)
        if (m.on) cmd += L" -mod=mod/" + m.file;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring exe = path(L"v2game.exe");
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, g_dir.c_str(), &si, &pi)) {
        MessageBoxW(g_wnd, L"Could not start v2game.exe.", L"Victoria 2", MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    PostQuitMessage(0);
}
static void load_picture() {
    HANDLE f = CreateFileW(path(L"load_bg.jpg").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(f, nullptr), rd = 0;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sz);
    ReadFile(f, GlobalLock(g), sz, &rd, nullptr);
    GlobalUnlock(g);
    CloseHandle(f);
    IStream* s = nullptr;
    if (CreateStreamOnHGlobal(g, TRUE, &s) == S_OK) {
        OleLoadPicture(s, sz, FALSE, IID_IPicture, (void**)&g_pic);
        s->Release();
    }
}
static HWND mk(const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}
static void move(int d) {
    int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    int j = i + d;
    if (i < 0 || j < 0 || j >= (int)g_mods.size()) return;
    std::swap(g_mods[i], g_mods[j]);
    fill(j);
    save();
}
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        g_wnd = h;
        mk(L"STATIC", L"Mods  (checked = loaded, top to bottom)", 0, 724, 12, 280, 18, 0, g_font);
        g_list = mk(WC_LISTVIEWW, L"", LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER, 724, 32, 280, 290, ID_LIST, g_font);
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        LVCOLUMNW c{};
        c.mask = LVCF_WIDTH;
        c.cx = 255;
        ListView_InsertColumn(g_list, 0, &c);
        mk(L"BUTTON", L"Move up", BS_PUSHBUTTON, 724, 328, 90, 26, ID_UP, g_font);
        mk(L"BUTTON", L"Move down", BS_PUSHBUTTON, 819, 328, 90, 26, ID_DOWN, g_font);
        mk(L"BUTTON", L"Clear", BS_PUSHBUTTON, 914, 328, 90, 26, ID_NONE, g_font);
        g_auto = mk(L"BUTTON", L"Next time start directly (hold Shift for this window)", BS_AUTOCHECKBOX | BS_MULTILINE, 724, 360, 280, 34, ID_AUTO, g_font);
        SendMessageW(g_auto, BM_SETCHECK, g_autostart ? BST_CHECKED : BST_UNCHECKED, 0);
        mk(L"BUTTON", L"Start Victoria 2", BS_DEFPUSHBUTTON, 724, 400, 280, 48, ID_PLAY, g_big);
        mk(L"BUTTON", L"Original launcher", BS_PUSHBUTTON, 12, 420, 140, 28, ID_ORIG, g_font);
        mk(L"BUTTON", L"Open mod folder", BS_PUSHBUTTON, 158, 420, 140, 28, ID_FOLDER, g_font);
        g_status = mk(L"STATIC", L"", 0, 310, 426, 400, 18, 0, g_font);
        fill(-1);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (g_pic) {
            OLE_XSIZE_HIMETRIC w;
            OLE_YSIZE_HIMETRIC ht;
            g_pic->get_Width(&w);
            g_pic->get_Height(&ht);
            g_pic->Render(dc, 12, 12, 700, 400, 0, ht, w, -ht, nullptr);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)lp;
        if (n->idFrom == ID_LIST && n->code == LVN_ITEMCHANGED && !g_filling) {
            NMLISTVIEW* v = (NMLISTVIEW*)lp;
            if (v->iItem >= 0 && (v->uChanged & LVIF_STATE) && ((v->uNewState ^ v->uOldState) & LVIS_STATEIMAGEMASK)) {
                g_mods[v->iItem].on = ListView_GetCheckState(g_list, v->iItem) != 0;
                save();
                status();
            }
        }
        if (n->idFrom == ID_LIST && n->code == NM_DBLCLK) launch_game();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_UP: move(-1); break;
        case ID_DOWN: move(1); break;
        case ID_NONE: for (auto& x : g_mods) x.on = false; fill(-1); save(); break;
        case ID_AUTO: g_autostart = SendMessageW(g_auto, BM_GETCHECK, 0, 0) == BST_CHECKED; save(); break;
        case ID_PLAY: launch_game(); break;
        case ID_FOLDER: ShellExecuteW(h, L"open", path(L"mod").c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case ID_ORIG: {
            save();
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            std::wstring exe = path(L"victoria2_original.exe");
            wchar_t cmd[] = L"victoria2_original.exe";
            if (CreateProcessW(exe.c_str(), cmd, nullptr, nullptr, FALSE, 0, nullptr, g_dir.c_str(), &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                PostQuitMessage(0);
            } else MessageBoxW(h, L"victoria2_original.exe was not found.", L"Victoria 2", MB_ICONERROR);
            break;
        }
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_dir = exe;
    g_dir = g_dir.substr(0, g_dir.find_last_of(L"\\/"));
    SetCurrentDirectoryW(g_dir.c_str());
    load();
    if (g_autostart && !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
        g_wnd = nullptr;
        launch_game();
        return 0;
    }
    CoInitialize(nullptr);
    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&ic);
    g_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_big = CreateFontW(-18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Georgia");
    load_picture();
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"V2Launcher";
    RegisterClassW(&wc);
    RECT r{0, 0, 1016, 460};
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, st, FALSE);
    int w = r.right - r.left, ht = r.bottom - r.top;
    HWND h = CreateWindowW(L"V2Launcher", L"Victoria 2", st, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                           (GetSystemMetrics(SM_CYSCREEN) - ht) / 2, w, ht, nullptr, nullptr, inst, nullptr);
    ShowWindow(h, show);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (g_pic) g_pic->Release();
    return 0;
}
