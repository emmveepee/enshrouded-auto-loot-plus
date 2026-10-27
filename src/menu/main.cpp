// Auto Loot Plus: native F8 menu and loose PickupItem collection only.
// Corpse/container/harvest requests belong exclusively to the guarded client
// helper. Do not append corpse queries, bypass the vanilla no-PickupItem skip,
// synthesize AcceptOfferedInteractionEvent, or hook the native loot-all block.
// Runtime patch sites use checked signatures, never fixed game RVAs.

#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// dinput8 proxy
// ---------------------------------------------------------------------------

static HMODULE g_real = nullptr;

static HMODULE RealDinput8()
{
    if (!g_real) {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);
        wcscat_s(path, L"\\dinput8.dll");
        g_real = LoadLibraryW(path);
    }
    return g_real;
}

template <typename T>
static T RealProc(const char* name)
{
    HMODULE m = RealDinput8();
    return m ? reinterpret_cast<T>(GetProcAddress(m, name)) : nullptr;
}

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer)
{
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    auto fn = RealProc<Fn>("DirectInput8Create");
    return fn ? fn(inst, version, riid, out, outer) : E_FAIL;
}
extern "C" HRESULT WINAPI Proxy_DllCanUnloadNow() { return S_FALSE; }
extern "C" HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out)
{
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    auto fn = RealProc<Fn>("DllGetClassObject");
    return fn ? fn(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI Proxy_DllRegisterServer() { return E_NOTIMPL; }
extern "C" HRESULT WINAPI Proxy_DllUnregisterServer() { return E_NOTIMPL; }
extern "C" LPCVOID WINAPI Proxy_GetdfDIJoystick()
{
    using Fn = LPCVOID(WINAPI*)();
    auto fn = RealProc<Fn>("GetdfDIJoystick");
    return fn ? fn() : nullptr;
}

// ---------------------------------------------------------------------------
// Logging and config
// ---------------------------------------------------------------------------

static std::wstring g_gameDir;

void Log(const char* fmt, ...)
{
    std::wstring path = g_gameDir + L"\\auto-loot-plus.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"a") != 0 || !f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

struct Config {
    bool enabled = true;
    float radius = 15.0f;
    bool autoLoot = true;
    bool corpses = true;
    bool debugLog = false;
    bool ignoreLineOfSight = true;
    bool instantLoot = true;
};
static Config g_cfg;

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static bool ParseBool(const std::string& v, bool fallback)
{
    std::string s;
    for (char c : v) s += (char)tolower((unsigned char)c);
    if (s == "true" || s == "1" || s == "yes" || s == "on") return true;
    if (s == "false" || s == "0" || s == "no" || s == "off") return false;
    return fallback;
}

static void LoadConfig()
{
    std::wstring path = g_gameDir + L"\\mods\\auto-loot-plus\\config.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") != 0 || !f) {
        Log("config.txt nicht gefunden – Standardwerte");
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string s = Trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(s.substr(0, eq));
        std::string val = Trim(s.substr(eq + 1));
        size_t comment = val.find_first_of("#;");
        if (comment != std::string::npos) val = Trim(val.substr(0, comment));
        if (key == "enabled") g_cfg.enabled = ParseBool(val, g_cfg.enabled);
        else if (key == "radiusMeters") g_cfg.radius = (float)atof(val.c_str());
        else if (key == "autoLoot") g_cfg.autoLoot = ParseBool(val, g_cfg.autoLoot);
        else if (key == "lootEnemyCorpses") g_cfg.corpses = ParseBool(val, g_cfg.corpses);
        else if (key == "debugLog") g_cfg.debugLog = ParseBool(val, g_cfg.debugLog);
        else if (key == "requireLineOfSight") g_cfg.ignoreLineOfSight = !ParseBool(val, !g_cfg.ignoreLineOfSight);
        else if (key == "instantLoot") g_cfg.instantLoot = ParseBool(val, g_cfg.instantLoot);
    }
    fclose(f);
    if (g_cfg.radius < 1.0f) g_cfg.radius = 1.0f;
    if (g_cfg.radius > 50.0f) g_cfg.radius = 50.0f;
}

// ---------------------------------------------------------------------------
// Pattern scanning
// ---------------------------------------------------------------------------

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
};

static Pattern ParsePattern(const char* text)
{
    Pattern p;
    for (const char* c = text; *c;) {
        if (*c == ' ') { ++c; continue; }
        if (*c == '?') {
            p.bytes.push_back(0);
            p.mask.push_back(false);
            c += (c[1] == '?') ? 2 : 1;
        } else {
            p.bytes.push_back((uint8_t)strtoul(std::string(c, 2).c_str(), nullptr, 16));
            p.mask.push_back(true);
            c += 2;
        }
    }
    return p;
}

static uint8_t* g_textStart = nullptr;
static size_t g_textSize = 0;

static bool FindText()
{
    auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (memcmp(sec->Name, ".text", 5) == 0) {
            g_textStart = base + sec->VirtualAddress;
            g_textSize = sec->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

// Returns the unique match in [start, start+size), or nullptr (count of matches in *count)
static uint8_t* FindUniqueIn(uint8_t* start, size_t size, const char* text, int* count)
{
    Pattern p = ParsePattern(text);
    uint8_t* found = nullptr;
    *count = 0;
    const size_t n = p.bytes.size();
    for (size_t i = 0; i + n <= size; ++i) {
        uint8_t* at = start + i;
        if (at[0] != p.bytes[0]) continue;
        bool ok = true;
        for (size_t k = 1; k < n; ++k) {
            if (p.mask[k] && at[k] != p.bytes[k]) { ok = false; break; }
        }
        if (ok) {
            if (++*count == 1) found = at;
        }
    }
    return *count == 1 ? found : nullptr;
}

static uint8_t* FindUnique(const char* text, int* count)
{
    return FindUniqueIn(g_textStart, g_textSize, text, count);
}

// First match (for patterns that legitimately occur more than once)
static uint8_t* FindFirst(const char* text, int* count)
{
    Pattern p = ParsePattern(text);
    *count = 0;
    const size_t n = p.bytes.size();
    for (size_t i = 0; i + n <= g_textSize; ++i) {
        bool ok = true;
        for (size_t k = 0; k < n; ++k)
            if (p.mask[k] && g_textStart[i + k] != p.bytes[k]) { ok = false; break; }
        if (ok) { *count = 1; return g_textStart + i; }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Patch helpers
// ---------------------------------------------------------------------------

static void WriteCode(void* at, const void* data, size_t size)
{
    DWORD old;
    VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old);
    memcpy(at, data, size);
    VirtualProtect(at, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, size);
}

static int32_t Rel32(const uint8_t* from, const void* to)
{
    return (int32_t)(reinterpret_cast<const uint8_t*>(to) - from);
}

static bool InRel32(const uint8_t* from, const void* to)
{
    int64_t d = reinterpret_cast<const uint8_t*>(to) - from;
    return d > INT32_MIN && d < INT32_MAX;
}

// Memory within +-2 GB of the game code for floats and the jump trampoline
static uint8_t* AllocNear(uint8_t* target, size_t size)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    uintptr_t start = (reinterpret_cast<uintptr_t>(target) & ~(gran - 1));
    for (uintptr_t delta = gran; delta < 0x70000000; delta += gran) {
        for (int dir = -1; dir <= 1; dir += 2) {
            uintptr_t addr = dir < 0 ? start - delta : start + delta;
            void* p = VirtualAlloc(reinterpret_cast<void*>(addr), size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (p) return static_cast<uint8_t*>(p);
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Game knowledge (Enshrouded 1.0, build 1269579)
// ---------------------------------------------------------------------------

static const uint32_t kHashPickupItem = 0x08775A90;            // keen::ecs::PickupItem
static const uint32_t kHashLootedCorpse = 0x761CCA43;          // keen::ecs::DisableColliderModeChanges

// Magnet function: box half-extent load "movss xmm5,[rip+X]" (7.0)
static const char* kSigBox =
    "88 54 24 10 48 89 4C 24 08 55 53 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 48 8B 39 48 8B D9 48 8B CF "
    "48 89 7C 24 30 E8 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ?? 48 89 B4 24 ?? ?? ?? ?? 48 8D 54 24 48 48 8B CF "
    "44 0F 29 94 24 ?? ?? ?? ?? E8 ?? ?? ?? ?? F3 0F 10 2D";
static const size_t kSigBoxDisp = 0x58;

// Pickup radius (1.75 m) and skill radius (3.5 m): movss xmm12 / xmm13,[rip+X]
static const char* kSigRadii =
    "44 0F 29 9C 24 ?? ?? ?? ?? 45 0F 57 DB 44 0F 29 A4 24 ?? ?? ?? ?? "
    "F3 44 0F 10 25 ?? ?? ?? ?? 44 0F 29 AC 24 ?? ?? ?? ?? F3 44 0F 10 2D ?? ?? ?? ?? 44 0F 29 B4 24 ?? ?? ?? ?? "
    "F3 44 0F 10 35 ?? ?? ?? ?? 44 0F 29 BC 24";
static const size_t kSigRadiiA = 0x1B;
static const size_t kSigRadiiB = 0x2D;
static const size_t kSigRadiiNear = 0x3F;   // xmm14 = 0.5625 (0.75 m squared): closer = loot at once, else pull

// Max squared distance (12.25): comiss xmm7,[rip+X]
static const char* kSigMaxDist =
    "F3 41 0F 59 FA F3 41 0F 59 C2 F3 0F 59 FF F3 0F 59 C0 F3 0F 58 F9 F3 0F 58 F8 0F 2F 3D ?? ?? ?? ?? 0F 87";
static const size_t kSigMaxDistDisp = 0x1D;

// Line of sight: "mov rdx,[rbx+458h]; mov rcx,[rsp+50h]; call HasAreaLoot; test al,al; jne <skip raycast>"
// Turning the jne into jmp skips the visibility raycast for every target.
// (preceded by "ja" of the max-distance check; the same sequence after a "jbe" must stay untouched)
static const char* kSigSight = "0F 87 ?? ?? ?? ?? 48 8B 93 58 04 00 00 48 8B 4C 24 50 E8 ?? ?? ?? ?? 84 C0 0F 85";
static const size_t kSigSightJne = 0x19;

// System area_loot (pulls marked items to the player): drops them again beyond 1.75 / 3.5 m
static const char* kSigPull =
    "66 41 85 84 24 B0 05 00 00 74 ?? B2 08 49 8B CF E8 ?? ?? ?? ?? 48 8B 55 ?? 84 C0 75 ?? "
    "F3 44 0F 10 05 ?? ?? ?? ?? EB ?? F3 44 0F 10 05 ?? ?? ?? ?? F3 0F 10 05";
static const size_t kSigPullA = 0x22;
static const size_t kSigPullB = 0x2D;

// Hold-E gate: "test byte [rdx+34h],80h; jne +3; xor al,al; ret"
static const char* kSigGate = "F6 42 34 80 75 03 32 C0 C3 B8 00 10 00 00 66 85";
static const size_t kSigGateJne = 0x04;

// Quest items stay in chests when we loot automatically (list: mods\auto-loot-plus\quest_items.txt)
static std::vector<uint32_t> g_questItems;
static volatile LONG64 g_statQuestKept = 0;

static void LoadQuestItems()
{
    std::wstring path = g_gameDir + L"\\mods\\auto-loot-plus\\quest_items.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") != 0 || !f) { Log("quest_items.txt fehlt"); return; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] < '0' || line[0] > '9') continue;
        g_questItems.push_back((uint32_t)strtoul(line, nullptr, 10));
    }
    fclose(f);
    std::sort(g_questItems.begin(), g_questItems.end());
    Log("Questgegenstaende: %zu", g_questItems.size());
}

// Diagnostics (debugLog): which items our automatic "loot all" moves, by item id
struct ItemCount { uint32_t id; uint32_t count; };
static ItemCount g_lootedItems[64];
static SRWLOCK g_lootedLock = SRWLOCK_INIT;

static void CountLootedItem(uint32_t id)
{
    AcquireSRWLockExclusive(&g_lootedLock);
    for (auto& c : g_lootedItems) {
        if (c.id == id) { ++c.count; break; }
        if (c.count == 0) { c = { id, 1 }; break; }
    }
    ReleaseSRWLockExclusive(&g_lootedLock);
}

static bool __fastcall IsQuestItem(uint32_t id)
{
    bool quest = std::binary_search(g_questItems.begin(), g_questItems.end(), id);
    if (quest) InterlockedIncrement64(&g_statQuestKept);
    else if (g_cfg.debugLog) CountLootedItem(id);
    return quest;
}

// immediate_loot_action, per inventory slot: "xor r13d,r13d; cmp [rdi],r13d; je <next slot>"
static const char* kSigSlotLoop = "45 33 ED 44 39 2F 0F 84 ?? ?? ?? ?? 48 8D 8D ?? ?? ?? ?? E8";
static const size_t kSlotLoopSite = 0x03;          // cmp [rdi],r13d (3 bytes) + je rel32 (6 bytes)
static const size_t kEventBypassFromLootAll = 0x64; // r14 = &event.lootAll, +0x64 = &event.bypassInteraction

// Gate "is the magnet active for this player": vanilla = hold-E bit && (flag 0x1000 || check(r8, 8))
using GateCheckFn = bool(__fastcall*)(void*, uint8_t);
static GateCheckFn g_gateCheck = nullptr;

// A game window is open (chest, crafting station, dialogue): the game shows the mouse cursor, during
// play it is hidden. While a window is open the magnet stays as in the vanilla game (E cannot be held
// in a window, so: off). An accepted interaction of ours would otherwise rebind or close that window
// (reports after 1.0.2: "crafting window closes on its own", "chest shows a row of empty slots").
// The pointer is replaceable for the test.
static bool CursorShown()
{
    CURSORINFO ci = { sizeof ci };
    return GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) != 0;
}
static bool (*g_cursorShown)() = &CursorShown;
static volatile LONG64 g_statGate = 0, g_statGateHeld = 0, g_statGateVanilla = 0, g_statGatePaused = 0;
extern uint64_t g_lastCursorRecenter;   // overlay.cpp: last SetCursorPos of the game (diagnostics)

static bool __fastcall GateHook(uint8_t* state, uint8_t* input, void* actor)
{
    InterlockedIncrement64(&g_statGate);
    bool held = (input[0x34] & 0x80) != 0;
    bool vanilla = held && ((*reinterpret_cast<uint16_t*>(state + 0x5B0) & 0x1000) || g_gateCheck(actor, 8));
    if (held) InterlockedIncrement64(&g_statGateHeld);
    if (vanilla) InterlockedIncrement64(&g_statGateVanilla);
    if (!g_cfg.autoLoot) return vanilla;
    bool window = g_cursorShown();
    static bool lastWindow = false;
    static LONG windowLogs = 0;
    if (window != lastWindow) {
        lastWindow = window;
        if (g_cfg.debugLog && InterlockedIncrement(&windowLogs) <= 60)
            Log("Mauszeiger %s – Magnet %s (letzte Neuzentrierung vor %llu ms)", window ? "sichtbar" : "versteckt",
                window ? "wie Original" : "automatisch", (unsigned long long)(GetTickCount64() - g_lastCursorRecenter));
    }
    if (window) { InterlockedIncrement64(&g_statGatePaused); return vanilla; }
    return true;
}

// ---------------------------------------------------------------------------
// Patching
// ---------------------------------------------------------------------------

struct NearData {
    float boxHalf;
    float maxDist2;
    float radiusA;
    float radiusB;
    float nearDist2;        // closer than this = loot at once, else pull (instantLoot: whole radius)
    uint8_t ignoreSight;    // read by sightStub: 1 = skip the visibility raycast
    uint8_t questStub[128];
    uint8_t sightStub[48];
};

static NearData* g_near = nullptr;
static float g_vanillaNearDist2 = 0.5625f;   // read from the game before patching

// Small code emitter for the stubs
struct Emit {
    uint8_t* p;
    void bytes(std::initializer_list<uint8_t> b) { for (uint8_t x : b) *p++ = x; }
    void imm64(uint64_t v) { memcpy(p, &v, 8); p += 8; }
    void rel32To(const void* target) { int32_t r = Rel32(p + 4, target); memcpy(p, &r, 4); p += 4; }
};

// Slot loop hook: skip empty slots (vanilla) and, for our automatic loot events only
// (bypassInteraction set), quest items.
static bool PatchQuestFilter()
{
    int count = 0;
    uint8_t* m = FindUnique(kSigSlotLoop, &count);
    if (!m) { Log("Muster 'slotLoop' gefunden: %d mal – Questfilter aus", count); return false; }
    uint8_t* site = m + kSlotLoopSite;
    uint8_t* skip = site + 9 + *reinterpret_cast<int32_t*>(site + 5);   // je target (next slot)
    uint8_t* cont = site + 9;

    Emit e{ g_near->questStub };
    e.bytes({ 0x8B, 0x07 });                         // mov eax,[rdi]
    e.bytes({ 0x85, 0xC0 });                         // test eax,eax
    e.bytes({ 0x0F, 0x84 }); e.rel32To(skip);        // jz skip
    e.bytes({ 0x41, 0x80, 0x7E, (uint8_t)kEventBypassFromLootAll, 0x00 });  // cmp byte [r14+64h],0
    e.bytes({ 0x0F, 0x84 }); e.rel32To(cont);        // je cont (vanilla event: no filter)
    e.bytes({ 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x50 }); // push rcx..r11,rax
    e.bytes({ 0x8B, 0xC8 });                         // mov ecx,eax
    e.bytes({ 0x48, 0x83, 0xEC, 0x28 });             // sub rsp,28h
    e.bytes({ 0x48, 0xB8 }); e.imm64(reinterpret_cast<uint64_t>(&IsQuestItem));
    e.bytes({ 0xFF, 0xD0 });                         // call rax
    e.bytes({ 0x48, 0x83, 0xC4, 0x28 });             // add rsp,28h
    e.bytes({ 0x84, 0xC0 });                         // test al,al
    e.bytes({ 0x58, 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59 }); // pop (flags kept)
    e.bytes({ 0x0F, 0x85 }); e.rel32To(skip);        // jnz skip (quest item stays)
    e.bytes({ 0xE9 }); e.rel32To(cont);              // jmp cont
    FlushInstructionCache(GetCurrentProcess(), g_near->questStub, e.p - g_near->questStub);

    uint8_t jmp[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
    int32_t rel = Rel32(site + 5, g_near->questStub);
    memcpy(jmp + 1, &rel, 4);
    WriteCode(site, jmp, sizeof jmp);
    return true;
}

// Line of sight: the "jne <skip raycast>" (0F 85 rel32) jumps to sightStub, which keeps the
// vanilla jump and additionally skips the raycast while g_near->ignoreSight is set.
static void BuildSightStub(uint8_t* jne)
{
    uint8_t* skip = jne + 6 + *reinterpret_cast<int32_t*>(jne + 2);
    Emit e{ g_near->sightStub };
    e.bytes({ 0x0F, 0x85 }); e.rel32To(skip);        // jne skip (vanilla)
    e.bytes({ 0x9C });                               // pushfq
    e.bytes({ 0x80, 0x3D });                         // cmp byte [rip+disp32],0
    int32_t flagRel = Rel32(e.p + 5, &g_near->ignoreSight);   // relative to the end (disp32 + imm8)
    memcpy(e.p, &flagRel, 4); e.p += 4;
    e.bytes({ 0x00 });
    e.bytes({ 0x74, 0x06 });                         // je vanilla
    e.bytes({ 0x9D });                               // popfq
    e.bytes({ 0xE9 }); e.rel32To(skip);              // jmp skip
    e.bytes({ 0x9D });                               // vanilla: popfq
    e.bytes({ 0xE9 }); e.rel32To(jne + 6);           // jmp back (raycast)
    FlushInstructionCache(GetCurrentProcess(), g_near->sightStub, e.p - g_near->sightStub);

    uint8_t jmp[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
    int32_t rel = Rel32(jne + 5, g_near->sightStub);
    memcpy(jmp + 1, &rel, 4);
    WriteCode(jne, jmp, sizeof jmp);
}

static void WriteJumpAbs(uint8_t* at, const void* target)   // mov rax, imm64; jmp rax (12 bytes)
{
    uint8_t code[12] = { 0x48, 0xB8 };
    uint64_t t = reinterpret_cast<uint64_t>(target);
    memcpy(code + 2, &t, 8);
    code[10] = 0xFF; code[11] = 0xE0;
    WriteCode(at, code, sizeof code);
}

static void ApplyRadius()
{
    float r = g_cfg.radius;
    g_near->boxHalf = r > 7.0f ? r : 7.0f;
    g_near->maxDist2 = r * r;
    g_near->radiusA = r;
    g_near->radiusB = r;
    g_near->nearDist2 = g_cfg.instantLoot ? r * r : g_vanillaNearDist2;
    g_near->ignoreSight = g_cfg.ignoreLineOfSight ? 1 : 0;
}

// Called by the in-game menu (overlay.cpp) for settings that take effect at once.
void ApplyLiveSetting(const std::string& key, const std::string& value)
{
    if (key == "autoLoot") {
        g_cfg.autoLoot = ParseBool(value, g_cfg.autoLoot);
    } else if (key == "radiusMeters") {
        float r = (float)atof(value.c_str());
        g_cfg.radius = r < 1.0f ? 1.0f : (r > 50.0f ? 50.0f : r);
    } else if (key == "instantLoot") {
        g_cfg.instantLoot = ParseBool(value, g_cfg.instantLoot);
    } else if (key == "requireLineOfSight") {
        g_cfg.ignoreLineOfSight = !ParseBool(value, !g_cfg.ignoreLineOfSight);
    } else {
        return;
    }
    if (g_near) ApplyRadius();
}

const std::wstring& GameDirectory() { return g_gameDir; }

static void PatchDisp(uint8_t* dispAt, const void* newTarget)
{
    // rip-relative disp32 is relative to the end of the instruction; all our sites end right after the disp
    int32_t disp = Rel32(dispAt + 4, newTarget);
    WriteCode(dispAt, &disp, 4);
}

static bool Patch()
{
    // inMagnet: searched only inside the magnet function (starts at the "box" site)
    struct Site { const char* name; const char* sig; bool inMagnet; uint8_t* at; };
    Site sites[] = {
        { "box", kSigBox, false, nullptr },
        { "radii", kSigRadii, true, nullptr },
        { "maxDist", kSigMaxDist, true, nullptr },
        { "gate", kSigGate, false, nullptr },
        { "pull", kSigPull, false, nullptr },
        { "sight", kSigSight, true, nullptr },
    };
    const size_t kMagnetSize = 0x1000;
    bool ok = true;
    for (auto& s : sites) {
        int count = 0;
        if (s.inMagnet) {
            if (!sites[0].at) { ok = false; continue; }
            s.at = FindUniqueIn(sites[0].at, kMagnetSize, s.sig, &count);
        } else {
            s.at = FindUnique(s.sig, &count);
        }
        if (!s.at) {
            Log("Muster '%s' gefunden: %d mal (erwartet 1)", s.name, count);
            ok = false;
        }
    }
    if (!ok) return false;

    g_near = reinterpret_cast<NearData*>(AllocNear(g_textStart, sizeof(NearData)));
    if (!g_near || !InRel32(g_textStart, g_near) || !InRel32(g_textStart + g_textSize, g_near)) {
        Log("Kein Speicher in Reichweite des Spielcodes");
        return false;
    }
    uint8_t* nearDisp = sites[1].at + kSigRadiiNear;
    g_vanillaNearDist2 = *reinterpret_cast<float*>(nearDisp + 4 + *reinterpret_cast<int32_t*>(nearDisp));
    if (g_cfg.debugLog) Log("Nahbereich Original: %.4f (erwartet 0.5625)", g_vanillaNearDist2);
    ApplyRadius();

    // Radius
    PatchDisp(sites[0].at + kSigBoxDisp, &g_near->boxHalf);
    PatchDisp(sites[1].at + kSigRadiiA, &g_near->radiusA);
    PatchDisp(sites[1].at + kSigRadiiB, &g_near->radiusB);
    PatchDisp(sites[2].at + kSigMaxDistDisp, &g_near->maxDist2);
    // Instant loot: everything inside the radius is looted directly instead of being pulled.
    // Static things (mushrooms, fireflies in bushes) cannot be pulled and would never arrive.
    // Always our own float, so the menu can switch it while the game runs.
    PatchDisp(nearDisp, &g_near->nearDist2);
    PatchDisp(sites[4].at + kSigPullA, &g_near->radiusA);
    PatchDisp(sites[4].at + kSigPullB, &g_near->radiusB);

    BuildSightStub(sites[5].at + kSigSightJne);

    // Only native PickupItem collection is modified. The vanilla entity query,
    // no-PickupItem skip and loot-all block remain untouched. Corpse/harvest
    // interactions belong exclusively to the guarded client helper.
    LoadQuestItems();
    if (!g_questItems.empty()) PatchQuestFilter();

    // Gate: replaced by GateHook (automatic mode + diagnostics). The vanilla helper is the
    // "jmp rel32" at the end of the gate: "B2 08 49 8B C8 E9 rel32" at +0x1A.
    uint8_t* gate = sites[3].at;
    static const uint8_t tail[] = { 0xB2, 0x08, 0x49, 0x8B, 0xC8, 0xE9 };
    if (memcmp(gate + 0x1A, tail, sizeof tail) != 0) {
        Log("Gate-Ende passt nicht – Auto-Modus nicht aktiv");
    } else {
        uint8_t* jmp = gate + 0x1F;
        g_gateCheck = reinterpret_cast<GateCheckFn>(jmp + 5 + *reinterpret_cast<int32_t*>(jmp + 1));
        WriteJumpAbs(gate, &GateHook);
    }
    (void)kSigGateJne;
    return true;
}

static DWORD WINAPI Worker(LPVOID)
{
    LoadConfig();
    Log("Auto Loot Plus native 1.0.3 – radius=%.1f autoLoot=%d corpses=%d instant=%d ignoreSight=%d", g_cfg.radius,
        g_cfg.autoLoot, g_cfg.corpses, g_cfg.instantLoot, g_cfg.ignoreLineOfSight);
    if (!g_cfg.enabled) { Log("deaktiviert (config.txt)"); return 0; }
    if (!FindText()) { Log(".text nicht gefunden"); return 0; }

    // The executable is wrapped (SteamStub); wait until the real code is readable.
    for (int i = 0; i < 240; ++i) {
        int count = 0;
        if (FindUnique(kSigGate, &count)) break;
        Sleep(250);
    }
    if (!Patch()) { Log("Nichts geändert (Spielversion passt nicht?)"); return 0; }
    Log("Patches angewendet");

    LONG64 lastGate = -1;
    for (;;) {
        Sleep(5000);
        LONG64 calls = g_statGate;
        if (!g_cfg.debugLog || calls == lastGate) continue;
        lastGate = calls;
        Log("Native loose-pickup gate calls=%lld; corpse/harvest owner=guarded client helper", calls);
        char items[64 * 24] = {};
        size_t used = 0;
        AcquireSRWLockExclusive(&g_lootedLock);
        for (auto& c : g_lootedItems) {
            if (c.count == 0) break;
            used += sprintf_s(items + used, sizeof items - used, " %u×%u", c.id, c.count);
            c = { 0, 0 };
        }
        ReleaseSRWLockExclusive(&g_lootedLock);
        if (used) Log("Alles-nehmen-Gegenstände (Nummer×Anzahl):%s", items);
    }
}

void Overlay_Install();   // overlay.cpp

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        g_gameDir = exe;
        g_gameDir = g_gameDir.substr(0, g_gameDir.find_last_of(L"\\/"));
        // only act inside the game client
        std::wstring name = exe;
        name = name.substr(name.find_last_of(L"\\/") + 1);
        if (_wcsicmp(name.c_str(), L"enshrouded.exe") == 0) {
            Overlay_Install();   // must run before the game loads Vulkan
            CloseHandle(CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr));
        }
    }
    return TRUE;
}
