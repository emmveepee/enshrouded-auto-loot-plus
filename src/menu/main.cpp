// Auto Loot Plus – native part (dinput8.dll proxy)
//
// Patches the player loot magnet of Enshrouded 1.0 (the code behind "hold E"):
//   * radius: the four distance constants of the magnet point to our own floats
//   * corpses: the entity query runs a second time for looted-enemy corpses
//     (component DisableColliderModeChanges) and targets without PickupItem jump
//     straight to the game's own "loot all" event (LootInteractionEvent)
//   * the interaction of such a target is only accepted when it carries AreaLootOverride (set by
//     mod.lua on every target the settings allow). The entity query is coarse: it also returns other
//     entities near the wanted ones (bushes, heaps ...), which must not be harvested.
//   * autoLoot: the "is hold-E pressed" gate is skipped
// autoLoot, radius, instantLoot and line of sight can be switched while the game runs.
// Every site is located by byte pattern. If any pattern is missing or ambiguous,
// nothing is patched (all-or-nothing) and the reason is written to the log.

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

// Entity query: "mov eax,[PickupItem hash]; ... call Query"
static const char* kSigQuery =
    "48 89 85 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? 89 44 24 20 48 89 B5 ?? ?? ?? ?? 48 C7 85 ?? ?? ?? ?? 20 00 00 00 E8";
static const size_t kSigQueryCall = 0x23;

// First check of the magnet for every entity: "mov r8,[rbx+X]; lea rcx,[rbp+..]; mov rdx,rax;
// call GetComponent; mov rax,[rax]; mov [rsp+38h],rax; test rax,rax; jne; ...; cmp byte [rax],0;
// jne <skip>" = AreaLootOverride (exclude = true skips the entity). X is the context offset of its
// accessor; OnLootAllTarget reads the same component.
static const char* kSigAreaLoot =
    "4C 8B 83 ?? ?? ?? ?? 48 8D 8D ?? ?? ?? ?? 48 8B D0 E8 ?? ?? ?? ?? 48 8B 00 48 89 44 24 38 48 85 C0 "
    "75 07 48 89 44 24 38 EB ?? 80 38 00 0F 85";
static size_t g_ctxAreaLoot = 0;   // 0 = not found: accepts are not restricted (as in 1.0.0)

// "no PickupItem -> skip": je rel32 that we retarget to the loot-all event block
static const char* kSigNoPickup =
    "48 8B 08 48 85 C9 0F 84 ?? ?? ?? ?? 48 8D 95 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? "
    "44 0F 2F F7 0F 86 ?? ?? ?? ?? 48 8B 83";
static const size_t kSigNoPickupJe = 0x06;
static const size_t kSigNoPickupLootAll = 0x2A;

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

// Context offsets of player_control_action_sequence (component accessors / event outputs)
static const size_t kCtxOfferRuntime = 0x488;   // InteractionOfferRuntime (random-access read)
static const size_t kCtxAcceptEvent = 0x530;    // AcceptOfferedInteractionEvent output
static const size_t kOfferIdOffset = 40;        // InteractionOfferRuntime.offerId
static const size_t kOfferDisabledOffset = 46;  // InteractionOfferRuntime.isDisabled

// Header site in the vanilla AcceptOffered emitter: "lea rdx,[rbp+67h]; mov rcx,r12; call EventHeader"
static const char* kSigHeader = "48 8D 55 67 49 8B CC E8 ?? ?? ?? ?? 48 8D 55 67 48 8B 08 48 89 0B 49 8B CC E8";
// Vanilla helper allocating an AcceptOfferedInteractionEvent from an output accessor
static const char* kSigAllocAccept =
    "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 E8 ?? ?? ?? ?? 48 8B CF 0F B7 D8 E8 ?? ?? ?? ?? 44 0F B7 CB BA B2 04 E4 82";

using GetComponentFn = void**(__fastcall*)(void* out, void* entity, void* accessor);
using GetIdFn = uint32_t*(__fastcall*)(void* world, void* tmp);
using GetHeaderFn = uint64_t*(__fastcall*)(void* world, void* tmp);
using AllocEventFn = uint8_t*(__fastcall*)(void* accessor);
static GetComponentFn g_getComponent = nullptr;
static GetIdFn g_getGuest = nullptr;
static GetHeaderFn g_getHeader = nullptr;
static AllocEventFn g_allocAccept = nullptr;

static volatile LONG64 g_statLootAll = 0, g_statAccepts = 0;

// "Interaction accepted" once per target (corpse/bush/chest) and visit. Up to 1.0.2 a target was
// accepted up to 3 times per 20 s (every 1.5 s) so that a refused attempt got new chances. But many
// targets stay in range after the accept (corpses, chests that keep a quest item, plants that are
// still being harvested), and every further accept did harm: a plant was harvested once more per
// accept (3 fibers instead of 1), and while a crafting or chest window was open the window was bound
// to the newly accepted target (window closed "twice in a row, as on a timer", or showed the plant's
// empty single slot).
// Now: the game answers a processed accept with a new offer (the offer id changes; seen in every
// logged repeat). While the offer id stays the same nothing happened, and the accept is repeated
// after kRetryMs, at most kMaxAccepts times. Once the id changed, the target is done for this visit.
// A new visit starts when the target was out of range for kReArmMs; the magnet reports every target
// in range each frame, so lastSeen stays fresh.
struct SeenTarget { uint32_t host; uint32_t offerId; uint32_t accepts; uint64_t lastSeen; uint64_t acceptTick; };
static const size_t kSeenTargets = 1024;
static SeenTarget g_seen[kSeenTargets];
static SRWLOCK g_seenLock = SRWLOCK_INIT;
static const uint64_t kReArmMs = 10000;
static const uint64_t kRetryMs = 3000;
static const uint32_t kMaxAccepts = 3;
static uint64_t g_clock = 0;   // tests only: fixed clock instead of GetTickCount64

static uint64_t Now() { return g_clock ? g_clock : GetTickCount64(); }

// Called for every released target the magnet reports (each frame). offerId = its current offer,
// nullptr = no usable offer right now (the target stays pending). Returns true when an accept is due.
static bool TargetPending(uint32_t host, const uint32_t* offerId)
{
    uint64_t now = Now();
    AcquireSRWLockExclusive(&g_seenLock);
    SeenTarget* s = nullptr;
    SeenTarget* oldest = &g_seen[0];
    for (auto& t : g_seen) {
        if (t.host == host && t.lastSeen != 0) { s = &t; break; }
        if (t.lastSeen < oldest->lastSeen) oldest = &t;
    }
    if (!s) { s = oldest; *s = { host, 0, 0, 0, 0 }; }   // table full: the longest unseen is forgotten
    if (now - s->lastSeen > kReArmMs) s->accepts = 0;    // new visit (also a new entry)
    s->lastSeen = now;
    bool due = false;
    if (offerId && s->accepts < kMaxAccepts) {
        if (s->accepts == 0) due = true;
        else if (*offerId != s->offerId) s->accepts = kMaxAccepts;   // the game reacted: done
        else due = now - s->acceptTick > kRetryMs;                   // nothing happened: try again
    }
    ReleaseSRWLockExclusive(&g_seenLock);
    return due;
}

// Called right after the accept event for this target was sent.
static void TargetAccepted(uint32_t host, uint32_t offerId)
{
    AcquireSRWLockExclusive(&g_seenLock);
    for (auto& t : g_seen) {
        if (t.host == host && t.lastSeen != 0) { t.offerId = offerId; ++t.accepts; t.acceptTick = Now(); break; }
    }
    ReleaseSRWLockExclusive(&g_seenLock);
}

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

static volatile LONG64 g_statNotReleased = 0;
static volatile LONG64 g_statNoOffer = 0, g_statOfferDisabled = 0, g_statThrottled = 0, g_statNoHelpers = 0;
static volatile LONG g_acceptLogs = 0;   // debugLog: the first accepts are logged one by one
static volatile LONG64 g_statSkipped = 0;

// Released = carries AreaLootOverride with exclude=false (mod.lua sets it on everything the settings allow)
static bool IsReleased(uint8_t* ctx, void* entity)
{
    uint8_t tmpArea[64] = {};
    void** area = g_getComponent(tmpArea, entity, *reinterpret_cast<void**>(ctx + g_ctxAreaLoot));
    return area && *area && !*static_cast<uint8_t*>(*area);
}

// Gate in front of the loot-all block for targets without PickupItem (corpses, bushes, chests and the
// neighbours the coarse query returns). Only released targets go on; all others take the game's own
// "next entity" jump as in the original. Up to 1.0.1 they went on into "loot all" without the accepted
// interaction, so the game emptied objects mod.lua never released – boss loot lost its key that way.
static bool __fastcall OnNoPickupTarget(uint8_t* ctx, uint8_t* gameRsp)
{
    if (!g_ctxAreaLoot || !g_getComponent) return true;   // check not available: as in 1.0.0
    void* entity = *reinterpret_cast<void**>(gameRsp + 0x50);
    if (IsReleased(ctx, entity)) return true;
    InterlockedIncrement64(&g_statSkipped);
    return false;
}

// Called from the magnet for a target without PickupItem (corpse, bush) right before the game
// sends its "loot all" event. Corpses and bushes roll their loot only when an interaction is
// accepted, so we first send the same AcceptOfferedInteractionEvent that pressing E sends.
// Only for targets that carry AreaLootOverride: mod.lua sets it on everything the settings allow.
static void __fastcall OnLootAllTarget(uint8_t* ctx, uint8_t* gameRsp, uint32_t host)
{
    InterlockedIncrement64(&g_statLootAll);
    void* world = *reinterpret_cast<void**>(gameRsp + 0x30);
    if (!g_getComponent || !g_getGuest || !g_allocAccept) { InterlockedIncrement64(&g_statNoHelpers); return; }

    void* entity = *reinterpret_cast<void**>(gameRsp + 0x50);
    if (g_ctxAreaLoot && !IsReleased(ctx, entity)) {
        InterlockedIncrement64(&g_statNotReleased);
        return;
    }
    uint8_t tmp[64] = {};
    void** slot = g_getComponent(tmp, entity, *reinterpret_cast<void**>(ctx + kCtxOfferRuntime));
    uint8_t* offer = slot ? static_cast<uint8_t*>(*slot) : nullptr;
    bool usable = offer && !offer[kOfferDisabledOffset];
    uint32_t offerId = usable ? *reinterpret_cast<uint32_t*>(offer + kOfferIdOffset) : 0;
    bool due = TargetPending(host, usable ? &offerId : nullptr);
    if (!offer) { InterlockedIncrement64(&g_statNoOffer); return; }
    if (!usable) { InterlockedIncrement64(&g_statOfferDisabled); return; }
    if (!due) { InterlockedIncrement64(&g_statThrottled); return; }

    uint8_t* ev = g_allocAccept(*reinterpret_cast<void**>(ctx + kCtxAcceptEvent));
    if (!ev) return;
    TargetAccepted(host, offerId);
    uint8_t tmp2[64] = {};
    if (g_getHeader) *reinterpret_cast<uint64_t*>(ev) = *g_getHeader(world, tmp2);
    uint8_t tmp3[64] = {};
    *reinterpret_cast<uint32_t*>(ev + 0x08) = *g_getGuest(world, tmp3);   // guest = player
    *reinterpret_cast<uint32_t*>(ev + 0x0C) = host;                         // host = corpse/bush
    *reinterpret_cast<uint32_t*>(ev + 0x10) = *reinterpret_cast<uint32_t*>(offer + kOfferIdOffset);
    InterlockedIncrement64(&g_statAccepts);
    if (g_cfg.debugLog && InterlockedIncrement(&g_acceptLogs) <= 40)
        Log("Angenommen: Ziel=%u Angebot=%u offered=%u Gast=%u", host, *reinterpret_cast<uint32_t*>(offer + kOfferIdOffset),
            offer[45], *reinterpret_cast<uint32_t*>(ev + 0x08));
}

// ---------------------------------------------------------------------------
// Query hook
// ---------------------------------------------------------------------------

using QueryFn = void*(__fastcall*)(void*, void*, void*, void*, uint32_t, uint8_t);
static QueryFn g_origQuery = nullptr;

// Diagnostics, written to the log every few seconds
static volatile LONG64 g_statQueries = 0, g_statPickups = 0, g_statCorpses = 0;
static volatile LONG64 g_statGate = 0, g_statGateHeld = 0, g_statGateVanilla = 0;

static void* __fastcall QueryHook(void* world, void* result, void* grid, void* box, uint32_t hash, uint8_t flag)
{
    // result list: { entries*, count, capacity, grow-callback } – the query appends
    auto count = reinterpret_cast<volatile uint64_t*>(static_cast<uint8_t*>(result) + 8);
    uint64_t before = *count;
    void* r = g_origQuery(world, result, grid, box, hash, flag);
    if (hash != kHashPickupItem) return r;
    uint64_t mid = *count;
    // corpses, chests, bushes, crops (mod.lua decides with AreaLootOverride what may be accepted)
    g_origQuery(world, result, grid, box, kHashLootedCorpse, flag);
    InterlockedIncrement64(&g_statQueries);
    InterlockedAdd64(&g_statPickups, (LONG64)(mid - before));
    InterlockedAdd64(&g_statCorpses, (LONG64)(*count - mid));
    return r;
}

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
static volatile LONG64 g_statGatePaused = 0;
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
    uint8_t trampoline[16];
    uint8_t lootAllStub[64];
    uint8_t gateStub[64];
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

// "je <skip>" (0F 84 rel32) of the no-PickupItem check -> gate stub: OnNoPickupTarget(ctx = rbx,
// gameRsp = rsp); released -> loot-all block, else the original skip target
static void BuildGateStub(uint8_t* je, uint8_t* lootAll)
{
    uint8_t* skip = je + 6 + *reinterpret_cast<int32_t*>(je + 2);
    Emit g{ g_near->gateStub };
    g.bytes({ 0x48, 0x8B, 0xCB,                       // mov rcx, rbx
              0x48, 0x8B, 0xD4,                       // mov rdx, rsp
              0x48, 0x83, 0xEC, 0x20,                 // sub rsp, 20h
              0x48, 0xB8 });                          // mov rax, imm64
    g.imm64(reinterpret_cast<uint64_t>(&OnNoPickupTarget));
    g.bytes({ 0xFF, 0xD0,                             // call rax
              0x48, 0x83, 0xC4, 0x20,                 // add rsp, 20h
              0x84, 0xC0,                             // test al, al
              0x0F, 0x84 });                          // jz skip
    g.rel32To(skip);
    g.bytes({ 0xE9 });                                // jmp loot-all block
    g.rel32To(lootAll);
    FlushInstructionCache(GetCurrentProcess(), g_near->gateStub, g.p - g_near->gateStub);
    int32_t jeRel = Rel32(je + 6, g_near->gateStub);
    WriteCode(je + 2, &jeRel, 4);
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
        { "query", kSigQuery, true, nullptr },
        { "noPickup", kSigNoPickup, true, nullptr },
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
    PatchDisp(sites[6].at + kSigPullA, &g_near->radiusA);
    PatchDisp(sites[6].at + kSigPullB, &g_near->radiusB);

    BuildSightStub(sites[7].at + kSigSightJne);

    // Loot-all targets (chests, bushes, crops, corpses): extra queries + jump to the loot-all event
    // when there is no PickupItem. Always installed; QueryHook asks for corpses only while enabled.
    {
        uint8_t* call = sites[3].at + kSigQueryCall;
        g_origQuery = reinterpret_cast<QueryFn>(call + 5 + *reinterpret_cast<int32_t*>(call + 1));
        WriteJumpAbs(g_near->trampoline, &QueryHook);
        int32_t rel = Rel32(call + 5, g_near->trampoline);
        WriteCode(call + 1, &rel, 4);

        // Game helpers, taken from call sites next to the verified patterns
        uint8_t* getCall = sites[4].at - 5;               // call GetComponent (PickupItem lookup)
        uint8_t* guestCall = sites[4].at + 0x7A;          // call GetCurrentEntity (loot-all block)
        if (getCall[0] == 0xE8 && guestCall[0] == 0xE8) {
            g_getComponent = reinterpret_cast<GetComponentFn>(getCall + 5 + *reinterpret_cast<int32_t*>(getCall + 1));
            g_getGuest = reinterpret_cast<GetIdFn>(guestCall + 5 + *reinterpret_cast<int32_t*>(guestCall + 1));
        }
        // The header pattern occurs twice in a row in the vanilla emitter; both call the same function.
        int n1 = 0, n2 = 0;
        uint8_t* header = FindFirst(kSigHeader, &n1);
        if (header) g_getHeader = reinterpret_cast<GetHeaderFn>(header + 12 + *reinterpret_cast<int32_t*>(header + 8));
        g_allocAccept = reinterpret_cast<AllocEventFn>(FindUnique(kSigAllocAccept, &n2));
        if (g_cfg.debugLog)
            Log("Helfer: GetComponent=%p Guest=%p Header=%p AllocAccept=%p", (void*)g_getComponent,
                (void*)g_getGuest, (void*)g_getHeader, (void*)g_allocAccept);

        // Every target that reaches the loot-all event block (pickups with an inventory like fireflies,
        // corpses, bushes, chests) first gets OnLootAllTarget(ctx=rbx, gameRsp=rsp, host=edi).
        // The block starts with "mov rax,[rbx+548h]" (7 bytes); it is replaced by a jmp to the stub,
        // which runs the call, the displaced instruction and jumps back.
        uint8_t* lootAll = sites[4].at + kSigNoPickupLootAll;
        static const uint8_t kBlockEntry[] = { 0x48, 0x8B, 0x83, 0x48, 0x05, 0x00, 0x00 };
        if (memcmp(lootAll, kBlockEntry, sizeof kBlockEntry) != 0) {
            Log("Loot-all-Block passt nicht – Leichen/Büsche aus");
            return false;
        }
        uint8_t* s = g_near->lootAllStub;
        size_t k = 0;
        const uint8_t pre[] = { 0x48, 0x8B, 0xCB,             // mov rcx, rbx
                                0x48, 0x8B, 0xD4,             // mov rdx, rsp
                                0x44, 0x8B, 0xC7,             // mov r8d, edi
                                0x48, 0x83, 0xEC, 0x20,       // sub rsp, 20h
                                0x48, 0xB8 };                 // mov rax, imm64
        memcpy(s, pre, sizeof pre); k = sizeof pre;
        uint64_t fn = reinterpret_cast<uint64_t>(&OnLootAllTarget);
        memcpy(s + k, &fn, 8); k += 8;
        const uint8_t post[] = { 0xFF, 0xD0,                  // call rax
                                 0x48, 0x83, 0xC4, 0x20 };    // add rsp, 20h
        memcpy(s + k, post, sizeof post); k += sizeof post;
        memcpy(s + k, kBlockEntry, sizeof kBlockEntry); k += sizeof kBlockEntry;   // mov rax,[rbx+548h]
        s[k++] = 0xE9;                                        // jmp back behind the displaced instruction
        int32_t backRel = Rel32(s + k + 4, lootAll + sizeof kBlockEntry);
        memcpy(s + k, &backRel, 4); k += 4;
        FlushInstructionCache(GetCurrentProcess(), s, k);

        uint8_t entryJmp[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
        int32_t toStub = Rel32(lootAll + 5, s);
        memcpy(entryJmp + 1, &toStub, 4);
        WriteCode(lootAll, entryJmp, sizeof entryJmp);

        // Targets without PickupItem (corpses, bushes, chests) go through the release check into the
        // loot-all block; not released -> the original jump target ("next entity", only rax and rbp
        // are used there, rbx = context as here)
        BuildGateStub(sites[4].at + kSigNoPickupJe, lootAll);

        // Accessor of AreaLootOverride: the magnet's own first check, right behind the query call
        int areaCount = 0;
        uint8_t* areaSite = FindUnique(kSigAreaLoot, &areaCount);
        if (areaSite && areaSite > call && areaSite < call + 0x200) {
            g_ctxAreaLoot = *reinterpret_cast<uint32_t*>(areaSite + 3);
            Log("Freigabe-Prüfung aktiv (AreaLootOverride, Kontext +%zX)", g_ctxAreaLoot);
        } else {
            Log("Muster 'areaLoot' gefunden: %d mal – Freigabe-Prüfung aus (wie 1.0.0)", areaCount);
        }

        LoadQuestItems();
        if (!g_questItems.empty()) PatchQuestFilter();
    }

    // Gate: replaced by GateHook (automatic mode + diagnostics). The vanilla helper is the
    // "jmp rel32" at the end of the gate: "B2 08 49 8B C8 E9 rel32" at +0x1A.
    uint8_t* gate = sites[5].at;
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

    LONG64 lastQueries = -1;
    for (;;) {
        Sleep(5000);
        LONG64 q = g_statQueries;
        if (!g_cfg.debugLog || q == lastQueries) continue;
        lastQueries = q;
        Log("Magnet: Abfragen=%lld Gegenstaende=%lld Ziele=%lld AllesNehmen=%lld Uebersprungen=%lld OhneFreigabe=%lld Gedrosselt=%lld KeinAngebot=%lld AngebotGesperrt=%lld OhneHelfer=%lld Angenommen=%lld QuestBehalten=%lld | Freigabe: Aufrufe=%lld E-gehalten=%lld Original-aktiv=%lld FensterPause=%lld",
            q, (LONG64)g_statPickups, (LONG64)g_statCorpses, (LONG64)g_statLootAll, (LONG64)g_statSkipped, (LONG64)g_statNotReleased,
            (LONG64)g_statThrottled, (LONG64)g_statNoOffer, (LONG64)g_statOfferDisabled, (LONG64)g_statNoHelpers, (LONG64)g_statAccepts,
            (LONG64)g_statQuestKept,
            (LONG64)g_statGate, (LONG64)g_statGateHeld, (LONG64)g_statGateVanilla, (LONG64)g_statGatePaused);
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
