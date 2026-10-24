// Auto Loot Plus – in-game settings menu (F8)
//
// Draws a Dear ImGui window into the game's own Vulkan swapchain:
//   * The game fetches vkGetInstanceProcAddr with GetProcAddress. That import of enshrouded.exe is
//     redirected, so every Vulkan function the game asks for passes our resolver, which hands out
//     wrappers for instance/device/queue/surface/swapchain creation and vkQueuePresentKHR.
//   * vkQueuePresentKHR: while the menu is open, one extra render pass draws the menu on top of the
//     finished frame (waits on the game's semaphores, signals our own one for the present).
//   * Keys and the cursor are polled (GetAsyncKeyState/GetCursorPos); mouse clicks are also taken from
//     the window messages, so short clicks between two frames are not lost. While the menu is open
//     the game window gets no keyboard/mouse messages, and the game cannot recenter or clip the cursor.
// Nothing is drawn or submitted while the menu is closed.

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

void Log(const char* fmt, ...);
void ApplyLiveSetting(const std::string& key, const std::string& value);
const std::wstring& GameDirectory();

// ---------------------------------------------------------------------------
// Settings (config.txt)
// ---------------------------------------------------------------------------

enum class Kind { Bool, Int, Float };

struct Setting {
    const char* section;
    const char* key;
    const char* label;
    const char* help;
    Kind kind;
    float min, max;
    bool live;              // takes effect at once; otherwise after a game restart
    const char* fallback;   // value when the key is missing in config.txt
    std::string value;
    std::string startValue; // value the game was started with
};

static std::vector<Setting> g_settings = {
    { "Einsammeln", "autoLoot", "Automatisch einsammeln",
      "Alles im Radius wird eingesammelt, ohne dass du E hältst.", Kind::Bool, 0, 0, true, "true" },
    { "Einsammeln", "radiusMeters", "Radius",
      "Reichweite in Metern. Einsammeln ändert sich sofort, Zerschlagen nach einem Neustart.",
      Kind::Float, 1, 50, true, "15" },
    { "Einsammeln", "instantLoot", "Sofort einsammeln",
      "Dinge im Radius direkt einsammeln statt sie heranfliegen zu lassen (nötig für Pilze, Glühwürmchen).",
      Kind::Bool, 0, 0, true, "true" },
    { "Einsammeln", "requireLineOfSight", "Nur mit Sichtlinie",
      "Nur einsammeln, was du direkt sehen kannst (Spiel-Standard). Aus = auch hinter Blättern und Wänden.",
      Kind::Bool, 0, 0, true, "false" },
    { "Einsammeln", "collectGroundLoot", "Bodenbeute",
      "Alles, was im Wald wächst oder liegt: Steine, Äste, Holz, Erz, Pilze, Beeren am Boden, "
      "Tränke, Munition, Drops. Aus = bleibt liegen, auch beim Halten von E; einzeln mit E geht weiter.",
      Kind::Bool, 0, 0, false, "true" },
    { "Einsammeln", "collectStones", "Steine",
      "Steine am Boden und Steinhaufen. Aus = bleiben liegen, und Gesteinsbrocken werden nicht mehr zerschlagen. "
      "Feuerstein zählt nicht dazu. Ist Bodenbeute aus, sind auch Steine aus.",
      Kind::Bool, 0, 0, false, "true" },
    { "Einsammeln", "collectCollectibles", "Sammelstücke",
      "Fossilien, Urnen, Gnome, Meteoriten, Mosaike, Farbpaletten, Event-Kürbisse usw.",
      Kind::Bool, 0, 0, false, "true" },
    { "Einsammeln", "collectPlants", "Büsche und Pflanzen",
      "Was an lebenden Pflanzen geerntet wird: Beerenbüsche, Kräuter, Pflanzenfasern, Schilf, Glühwürmchen.",
      Kind::Bool, 0, 0, false, "true" },
    { "Einsammeln", "collectFarmPlants", "Reife Beete ernten",
      "Nur erntereife Pflanzen, keine Setzlinge. Gilt auch in deiner Basis.", Kind::Bool, 0, 0, false, "true" },
    { "Plündern", "lootEnemyCorpses", "Gegnerleichen plündern",
      "Auch was Gegner beim Tod fallen lassen: Tierteile, Knochen, Fledermausfell.", Kind::Bool, 0, 0, false, "true" },
    { "Plündern", "lootChests", "Truhen und Behälter leeren",
      "Welt-Truhen, Sarkophage, Schränke, Regale, Müll- und Werkzeughaufen. Questgegenstände bleiben liegen, eigene "
      "Truhen werden nie angefasst.",
      Kind::Bool, 0, 0, false, "true" },
    { "Zerschlagen", "autoSmash", "Automatisch zerschlagen",
      "Fässer, Kisten, Körbe, Vasen, Urnen und kaputte Möbel in Ruinen im Radius. In und nahe deiner Basis nie.",
      Kind::Bool, 0, 0, false, "true" },
    { "Zerschlagen", "smashBuildableAwayFromBase", "Auch baubare Möbel zerschlagen",
      "Fässer, Stoffballen, Tische usw., die man auch selbst bauen kann – nur weit weg von jeder Basis.",
      Kind::Bool, 0, 0, false, "true" },
    { "Inventar", "stackSize", "Stapelgröße",
      "Maximale Stapelgröße für stapelbare Gegenstände. 0 = Spiel-Standard.", Kind::Int, 0, 9999, false, "9999" },
    { "Inventar", "cheatBestBackpack", "Bester Rucksack für 1 Zutat",
      "Schummeln: Jedes Rucksack-Rezept liefert den besten Rucksack und kostet je Zutat nur 1 Stück.",
      Kind::Bool, 0, 0, false, "false" },
    { "Mod", "enabled", "Mod aktiv", "Schaltet die ganze Mod an oder aus.", Kind::Bool, 0, 0, false, "true" },
};

// English texts (config.txt "language=auto|de|en"; auto = Windows display language)
struct EnglishText { const char* key; const char* section; const char* label; const char* help; };
static const EnglishText kEnglish[] = {
    { "autoLoot", "Looting", "Auto loot", "Everything in the radius is picked up without holding E." },
    { "radiusMeters", "Looting", "Radius",
      "Range in meters. Looting changes at once, smashing after a restart." },
    { "collectGroundLoot", "Looting", "Ground loot",
      "Everything that grows or lies in the wild: stones, branches, wood, ore, mushrooms, berries on the ground, "
      "potions, ammo, drops. Off = stays there, also when holding E; picking up one by one with E still works." },
    { "collectStones", "Looting", "Stones",
      "Stones on the ground and stone piles. Off = they stay there and rock boulders are no longer smashed. "
      "Flint is not included. With ground loot off, stones are off too." },
    { "instantLoot", "Looting", "Instant loot",
      "Loot things in the radius directly instead of pulling them in (needed for mushrooms, fireflies)." },
    { "requireLineOfSight", "Looting", "Line of sight only",
      "Only loot what you can see directly (game default). Off = also behind leaves and walls." },
    { "collectCollectibles", "Looting", "Collectibles",
      "Fossils, urns, gnomes, meteorites, mosaics, color palettes, event pumpkins and more." },
    { "collectPlants", "Looting", "Bushes and plants", "Harvested from living plants: berry bushes, herbs, plant fiber, reeds, fireflies." },
    { "collectFarmPlants", "Looting", "Harvest ripe crops", "Only ripe plants, no seedlings. Also inside your base." },
    { "lootEnemyCorpses", "Plundering", "Loot enemy corpses", "Also what enemies drop on death: critter parts, bones, bat fur." },
    { "lootChests", "Plundering", "Empty chests and containers",
      "World chests, sarcophagi, cupboards, shelves, trash and tool heaps. Quest items stay, your own chests are never touched." },
    { "autoSmash", "Smashing", "Auto smash",
      "Barrels, crates, baskets, vases, urns and broken furniture in ruins within the radius. Never in or near your base." },
    { "smashBuildableAwayFromBase", "Smashing", "Also smash buildable furniture",
      "Barrels, cloth bales, tables etc. that can also be built - only far away from any base." },
    { "stackSize", "Inventory", "Stack size", "Maximum stack size for stackable items. 0 = game default." },
    { "cheatBestBackpack", "Inventory", "Best backpack for 1 ingredient",
      "Cheat: every backpack recipe gives the best backpack and costs only 1 of each ingredient." },
    { "enabled", "Mod", "Mod enabled", "Turns the whole mod on or off." },
};
static bool g_english = false;

// Lite: dinput8.dll without EML and the Lua part (mods\auto-loot-plus\src\mod.lua). Then only the
// settings the DLL itself applies are shown. Decided once at start.
static bool g_lite = false;
static const char* const kDllKeys[] = { "autoLoot", "radiusMeters", "instantLoot", "requireLineOfSight", "enabled" };

static bool IsDllSetting(const Setting& st)
{
    for (const char* k : kDllKeys) if (strcmp(k, st.key) == 0) return true;
    return false;
}

static bool FileExists(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static const EnglishText* EnglishFor(const Setting& st)
{
    if (!g_english) return nullptr;
    for (auto& e : kEnglish) if (strcmp(e.key, st.key) == 0) return &e;
    return nullptr;
}
static const char* SectionText(const Setting& st) { auto e = EnglishFor(st); return e ? e->section : st.section; }
static const char* LabelText(const Setting& st) { auto e = EnglishFor(st); return e ? e->label : st.label; }
static const char* HelpText(const Setting& st) { auto e = EnglishFor(st); return e ? e->help : st.help; }
static const char* T(const char* de, const char* en) { return g_english ? en : de; }

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static bool IsTrue(const std::string& v)
{
    std::string s;
    for (char c : v) s += (char)tolower((unsigned char)c);
    return s == "true" || s == "1" || s == "yes" || s == "on";
}

static std::wstring ConfigPath() { return GameDirectory() + L"\\mods\\auto-loot-plus\\config.txt"; }

static bool ReadLines(std::vector<std::string>& lines)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, ConfigPath().c_str(), L"rb") != 0 || !f) return false;
    std::string all;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
    fclose(f);
    lines.clear();
    size_t start = 0;
    while (start <= all.size()) {
        size_t end = all.find('\n', start);
        if (end == std::string::npos) {
            if (start < all.size()) lines.push_back(all.substr(start));
            break;
        }
        lines.push_back(all.substr(start, end - start));   // keeps a trailing '\r'
        start = end + 1;
    }
    return true;
}

// Returns the key of a "key=value" line (empty for comments and other lines)
static std::string LineKey(const std::string& line, std::string* value)
{
    std::string s = Trim(line);
    if (s.empty() || s[0] == '#' || s[0] == ';') return std::string();
    size_t eq = s.find('=');
    if (eq == std::string::npos) return std::string();
    if (value) {
        std::string v = Trim(s.substr(eq + 1));
        size_t comment = v.find_first_of("#;");
        if (comment != std::string::npos) v = Trim(v.substr(0, comment));
        *value = v;
    }
    return Trim(s.substr(0, eq));
}

static void LoadSettings()
{
    for (auto& st : g_settings) st.value = st.fallback;
    std::string language = "auto";
    std::vector<std::string> lines;
    if (ReadLines(lines)) {
        for (auto& line : lines) {
            std::string value;
            std::string key = LineKey(line, &value);
            if (key == "language") language = value;
            for (auto& st : g_settings)
                if (key == st.key) st.value = value;
        }
    }
    if (language == "de") g_english = false;
    else if (language == "en") g_english = true;
    else g_english = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_GERMAN;
    for (auto& st : g_settings) st.startValue = st.value;
    g_lite = !(FileExists(GameDirectory() + L"\\dbghelp.dll") &&
               FileExists(GameDirectory() + L"\\mods\\auto-loot-plus\\src\\mod.lua"));
    if (g_lite) Log("Menü: Lite-Betrieb (kein EML oder kein Lua-Teil) – nur DLL-Einstellungen");
}

static void SaveSetting(const Setting& st)
{
    std::vector<std::string> lines;
    if (!ReadLines(lines)) { Log("Menü: config.txt nicht lesbar – nicht gespeichert"); return; }
    bool found = false;
    for (auto& line : lines) {
        if (LineKey(line, nullptr) == st.key) {
            bool cr = !line.empty() && line.back() == '\r';
            line = std::string(st.key) + "=" + st.value + (cr ? "\r" : "");
            found = true;
        }
    }
    if (!found) {
        lines.push_back("");
        lines.push_back(std::string(st.key) + "=" + st.value);
    }
    FILE* f = nullptr;
    if (_wfopen_s(&f, ConfigPath().c_str(), L"wb") != 0 || !f) { Log("Menü: config.txt nicht schreibbar"); return; }
    for (size_t i = 0; i < lines.size(); ++i) {
        fwrite(lines[i].data(), 1, lines[i].size(), f);
        if (i + 1 < lines.size()) fputc('\n', f);
    }
    fputc('\n', f);
    fclose(f);
}

// ---------------------------------------------------------------------------
// Hook state
// ---------------------------------------------------------------------------

static std::mutex g_lock;
static std::atomic<bool> g_menuOpen{ false };
extern "C" __declspec(dllexport) bool AutoLootPlusMenuOpen() { return g_menuOpen.load(); }
static std::atomic<int> g_wheel{ 0 };

// Mouse clicks from window messages. The menu is drawn once per frame; a click that starts and ends
// between two frames (low FPS, frame generation) would be lost if the buttons were only polled.
struct MouseClick { int button; bool down; };
static std::mutex g_clickLock;
static std::vector<MouseClick> g_clicks;
// Only one source per click, otherwise a click could arrive twice and toggle a checkbox back:
// raw input if the game uses it, else the classic button messages. Polling the buttons (as in 1.0.0)
// is only the fallback while no mouse message at all has reached the window; moving the mouse to a
// checkbox already decides this before the first click.
static std::atomic<bool> g_rawMouse{ false };
static std::atomic<bool> g_legacyMouse{ false };

static void QueueClick(int button, bool down)
{
    std::lock_guard<std::mutex> guard(g_clickLock);
    if (g_clicks.size() < 64) g_clicks.push_back({ button, down });
}

// Typed characters (WM_CHAR) for the text field of the stack size
static std::vector<unsigned int> g_chars;

static void QueueChar(unsigned int c)
{
    std::lock_guard<std::mutex> guard(g_clickLock);
    if (c >= 32 && g_chars.size() < 64) g_chars.push_back(c);
}
static bool g_broken = false;          // a failure disables the overlay for this session
static HWND g_hwnd = nullptr;
static WNDPROC g_origWndProc = nullptr;
static VkInstance g_instance = VK_NULL_HANDLE;

struct DeviceInfo { VkDevice device; VkPhysicalDevice physical; };
struct QueueInfo { VkQueue queue; uint32_t family; };
static std::vector<DeviceInfo> g_devices;
static std::vector<QueueInfo> g_queues;

#define REAL(fn) static PFN_##fn g_real_##fn = nullptr
REAL(vkGetInstanceProcAddr);
REAL(vkGetDeviceProcAddr);
REAL(vkCreateInstance);
REAL(vkCreateDevice);
REAL(vkDestroyDevice);
REAL(vkGetDeviceQueue);
REAL(vkGetDeviceQueue2);
REAL(vkCreateWin32SurfaceKHR);
REAL(vkCreateSwapchainKHR);
REAL(vkDestroySwapchainKHR);
REAL(vkQueuePresentKHR);
#undef REAL

// Device functions for our own rendering
struct DeviceFns {
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR;
    PFN_vkCreateImageView CreateImageView;
    PFN_vkDestroyImageView DestroyImageView;
    PFN_vkCreateFramebuffer CreateFramebuffer;
    PFN_vkDestroyFramebuffer DestroyFramebuffer;
    PFN_vkCreateRenderPass CreateRenderPass;
    PFN_vkDestroyRenderPass DestroyRenderPass;
    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkFreeCommandBuffers FreeCommandBuffers;
    PFN_vkResetCommandBuffer ResetCommandBuffer;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkCmdBeginRenderPass CmdBeginRenderPass;
    PFN_vkCmdEndRenderPass CmdEndRenderPass;
    PFN_vkCreateFence CreateFence;
    PFN_vkDestroyFence DestroyFence;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkResetFences ResetFences;
    PFN_vkCreateSemaphore CreateSemaphore;
    PFN_vkDestroySemaphore DestroySemaphore;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkDeviceWaitIdle DeviceWaitIdle;
};
static DeviceFns g_d = {};
static VkDevice g_fnDevice = VK_NULL_HANDLE;

struct Frame {
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore done = VK_NULL_HANDLE;
};

struct Swap {
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {};
    uint32_t minImages = 2;
    std::vector<Frame> frames;
    bool built = false;
};
static Swap g_swap;

static VkCommandPool g_pool = VK_NULL_HANDLE;
static uint32_t g_poolFamily = UINT32_MAX;
static VkRenderPass g_renderPass = VK_NULL_HANDLE;
static VkFormat g_renderPassFormat = VK_FORMAT_UNDEFINED;
static bool g_imguiContext = false;
static bool g_imguiVulkan = false;
static bool g_imguiFunctions = false;
static uint32_t g_imguiImageCount = 0;
static float g_fontSize = 18.0f;
// Latin-1 (umlauts, ß) plus general punctuation (– — „ “ … •)
static const ImWchar kGlyphs[] = { 0x0020, 0x00FF, 0x2010, 0x2027, 0 };

static bool LoadDeviceFns(VkDevice device)
{
    if (g_fnDevice == device) return true;
#define LOAD(name) g_d.name = reinterpret_cast<PFN_vk##name>(g_real_vkGetDeviceProcAddr(device, "vk" #name)); \
    if (!g_d.name) { Log("Menü: Vulkan-Funktion vk%s fehlt", #name); return false; }
    LOAD(GetSwapchainImagesKHR) LOAD(CreateImageView) LOAD(DestroyImageView) LOAD(CreateFramebuffer)
    LOAD(DestroyFramebuffer) LOAD(CreateRenderPass) LOAD(DestroyRenderPass) LOAD(CreateCommandPool)
    LOAD(DestroyCommandPool) LOAD(AllocateCommandBuffers) LOAD(FreeCommandBuffers) LOAD(ResetCommandBuffer)
    LOAD(BeginCommandBuffer) LOAD(EndCommandBuffer) LOAD(CmdBeginRenderPass) LOAD(CmdEndRenderPass)
    LOAD(CreateFence) LOAD(DestroyFence) LOAD(WaitForFences) LOAD(ResetFences) LOAD(CreateSemaphore)
    LOAD(DestroySemaphore) LOAD(QueueSubmit) LOAD(DeviceWaitIdle)
#undef LOAD
    g_fnDevice = device;
    return true;
}

// Frees the per-image resources (views, framebuffers, command buffers, sync objects)
static void DestroyFrames()
{
    if (!g_swap.device || g_fnDevice != g_swap.device) { g_swap.frames.clear(); g_swap.built = false; return; }
    VkDevice dev = g_swap.device;
    for (auto& fr : g_swap.frames) {
        if (fr.fence) g_d.WaitForFences(dev, 1, &fr.fence, VK_TRUE, 1000000000ull);
    }
    for (auto& fr : g_swap.frames) {
        if (fr.framebuffer) g_d.DestroyFramebuffer(dev, fr.framebuffer, nullptr);
        if (fr.view) g_d.DestroyImageView(dev, fr.view, nullptr);
        if (fr.cmd) g_d.FreeCommandBuffers(dev, g_pool, 1, &fr.cmd);
        if (fr.fence) g_d.DestroyFence(dev, fr.fence, nullptr);
        if (fr.done) g_d.DestroySemaphore(dev, fr.done, nullptr);
    }
    g_swap.frames.clear();
    g_swap.built = false;
}

static void DestroyAll()
{
    if (g_imguiVulkan) { ImGui_ImplVulkan_Shutdown(); g_imguiVulkan = false; }
    DestroyFrames();
    if (g_fnDevice) {
        if (g_renderPass) g_d.DestroyRenderPass(g_fnDevice, g_renderPass, nullptr);
        if (g_pool) g_d.DestroyCommandPool(g_fnDevice, g_pool, nullptr);
    }
    g_renderPass = VK_NULL_HANDLE;
    g_renderPassFormat = VK_FORMAT_UNDEFINED;
    g_pool = VK_NULL_HANDLE;
    g_poolFamily = UINT32_MAX;
}

// ---------------------------------------------------------------------------
// Vulkan wrappers handed to the game
// ---------------------------------------------------------------------------

static PFN_vkVoidFunction Intercept(const char* name, PFN_vkVoidFunction real);

static PFN_vkVoidFunction VKAPI_CALL Hook_vkGetInstanceProcAddr(VkInstance instance, const char* name)
{
    if (instance) g_instance = instance;
    return Intercept(name, g_real_vkGetInstanceProcAddr(instance, name));
}

static PFN_vkVoidFunction VKAPI_CALL Hook_vkGetDeviceProcAddr(VkDevice device, const char* name)
{
    return Intercept(name, g_real_vkGetDeviceProcAddr(device, name));
}

static VkResult VKAPI_CALL Hook_vkCreateInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* alloc,
                                                 VkInstance* instance)
{
    VkResult r = g_real_vkCreateInstance(info, alloc, instance);
    if (r == VK_SUCCESS) g_instance = *instance;
    return r;
}

static VkResult VKAPI_CALL Hook_vkCreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
                                               const VkAllocationCallbacks* alloc, VkDevice* device)
{
    VkResult r = g_real_vkCreateDevice(physical, info, alloc, device);
    if (r == VK_SUCCESS) {
        std::lock_guard<std::mutex> guard(g_lock);
        g_devices.push_back({ *device, physical });
    }
    return r;
}

static void VKAPI_CALL Hook_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc)
{
    {
        std::lock_guard<std::mutex> guard(g_lock);
        if (device == g_swap.device || device == g_fnDevice) {
            DestroyAll();
            g_swap = Swap();
            g_fnDevice = VK_NULL_HANDLE;
        }
    }
    g_real_vkDestroyDevice(device, alloc);
}

static void VKAPI_CALL Hook_vkGetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue* queue)
{
    g_real_vkGetDeviceQueue(device, family, index, queue);
    if (*queue) {
        std::lock_guard<std::mutex> guard(g_lock);
        g_queues.push_back({ *queue, family });
    }
}

static void VKAPI_CALL Hook_vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* queue)
{
    g_real_vkGetDeviceQueue2(device, info, queue);
    if (*queue) {
        std::lock_guard<std::mutex> guard(g_lock);
        g_queues.push_back({ *queue, info->queueFamilyIndex });
    }
}

static VkResult VKAPI_CALL Hook_vkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* info,
                                                        const VkAllocationCallbacks* alloc, VkSurfaceKHR* surface)
{
    if (info && info->hwnd) g_hwnd = info->hwnd;
    return g_real_vkCreateWin32SurfaceKHR(instance, info, alloc, surface);
}

static VkResult VKAPI_CALL Hook_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* info,
                                                     const VkAllocationCallbacks* alloc, VkSwapchainKHR* swapchain)
{
    // We render into the swapchain images; color attachment usage is always supported for surfaces.
    VkSwapchainCreateInfoKHR copy = *info;
    copy.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkResult r = g_real_vkCreateSwapchainKHR(device, &copy, alloc, swapchain);
    if (r == VK_SUCCESS) {
        std::lock_guard<std::mutex> guard(g_lock);
        DestroyFrames();
        g_swap.device = device;
        g_swap.swapchain = *swapchain;
        g_swap.format = info->imageFormat;
        g_swap.extent = info->imageExtent;
        g_swap.minImages = info->minImageCount < 2 ? 2 : info->minImageCount;
    }
    return r;
}

static void VKAPI_CALL Hook_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                  const VkAllocationCallbacks* alloc)
{
    {
        std::lock_guard<std::mutex> guard(g_lock);
        if (swapchain == g_swap.swapchain) {
            DestroyFrames();
            g_swap.swapchain = VK_NULL_HANDLE;
        }
    }
    g_real_vkDestroySwapchainKHR(device, swapchain, alloc);
}

static VkResult VKAPI_CALL Hook_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info);

static PFN_vkVoidFunction Intercept(const char* name, PFN_vkVoidFunction real)
{
    if (!real || !name) return real;
#define HOOK(fn) if (strcmp(name, #fn) == 0) { g_real_##fn = reinterpret_cast<PFN_##fn>(real); \
                                               return reinterpret_cast<PFN_vkVoidFunction>(&Hook_##fn); }
    HOOK(vkGetInstanceProcAddr)
    HOOK(vkGetDeviceProcAddr)
    HOOK(vkCreateInstance)
    HOOK(vkCreateDevice)
    HOOK(vkDestroyDevice)
    HOOK(vkGetDeviceQueue)
    HOOK(vkGetDeviceQueue2)
    HOOK(vkCreateWin32SurfaceKHR)
    HOOK(vkCreateSwapchainKHR)
    HOOK(vkDestroySwapchainKHR)
    HOOK(vkQueuePresentKHR)
#undef HOOK
    return real;
}

// ---------------------------------------------------------------------------
// Window input
// ---------------------------------------------------------------------------

using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
static SetCursorPosFn g_realSetCursorPos = nullptr;
static ClipCursorFn g_realClipCursor = nullptr;

uint64_t g_lastCursorRecenter = 0;   // diagnostics for main.cpp: the game recenters the cursor during play

static BOOL WINAPI Hook_SetCursorPos(int x, int y)
{
    g_lastCursorRecenter = GetTickCount64();
    if (g_menuOpen) return TRUE;   // the game recenters the cursor every frame
    return g_realSetCursorPos(x, y);
}

static BOOL WINAPI Hook_ClipCursor(const RECT* rect)
{
    return g_realClipCursor(g_menuOpen ? nullptr : rect);
}

static FARPROC WINAPI Hook_GetProcAddress(HMODULE module, LPCSTR name)
{
    FARPROC p = GetProcAddress(module, name);
    if (!p || IS_INTRESOURCE(name) || name[0] != 'v' || name[1] != 'k') return p;
    return reinterpret_cast<FARPROC>(Intercept(name, reinterpret_cast<PFN_vkVoidFunction>(p)));
}

// While the menu is open the game gets no presses, but every release still reaches it: a key that
// was pressed before the menu opened must not stay "held" in the game (that blocked Esc).
// F8 belongs to the menu and never reaches the game.
static LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INPUT) {
        RAWINPUT ri;
        UINT size = sizeof ri;
        bool ok = GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1;
        bool keyboard = ok && ri.header.dwType == RIM_TYPEKEYBOARD;
        bool mouse = ok && ri.header.dwType == RIM_TYPEMOUSE;
        if (keyboard && ri.data.keyboard.VKey == VK_F8) return DefWindowProcW(hwnd, msg, wp, lp);
        if (g_menuOpen) {
            if (mouse && (ri.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)) g_wheel += (short)ri.data.mouse.usButtonData;
            if (mouse) {
                g_rawMouse = true;
                USHORT b = ri.data.mouse.usButtonFlags;
                if (b & RI_MOUSE_LEFT_BUTTON_DOWN) QueueClick(0, true);
                if (b & RI_MOUSE_LEFT_BUTTON_UP) QueueClick(0, false);
                if (b & RI_MOUSE_RIGHT_BUTTON_DOWN) QueueClick(1, true);
                if (b & RI_MOUSE_RIGHT_BUTTON_UP) QueueClick(1, false);
            }
            const USHORT kUps = RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_UP |
                                RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP;
            bool release = (keyboard && (ri.data.keyboard.Flags & RI_KEY_BREAK)) ||
                           (mouse && (ri.data.mouse.usButtonFlags & kUps));
            if (!release) return DefWindowProcW(hwnd, msg, wp, lp);   // the game does not see it
        }
        return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
    }
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP) && wp == VK_F8) return 0;
    if (g_menuOpen && !g_rawMouse) {
        if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) g_legacyMouse = true;
        switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: QueueClick(0, true); break;
        case WM_LBUTTONUP: QueueClick(0, false); break;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: QueueClick(1, true); break;
        case WM_RBUTTONUP: QueueClick(1, false); break;
        }
    }
    if (g_menuOpen) {
        switch (msg) {
        case WM_MOUSEWHEEL:
            g_wheel += GET_WHEEL_DELTA_WPARAM(wp);
            return 0;
        case WM_CHAR:
            QueueChar((unsigned int)wp);
            return 0;
        case WM_KEYDOWN:
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
            return 0;
        case WM_SETCURSOR:
            SetCursor(nullptr);   // ImGui draws the cursor
            return TRUE;
        }
    }
    return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
}

static void SetMenuOpen(bool open)
{
    if (g_menuOpen == open) return;
    g_menuOpen = open;
    if (open) {
        {
            std::lock_guard<std::mutex> guard(g_clickLock);
            g_clicks.clear();
            g_chars.clear();
        }
        if (g_imguiContext) {   // no button stays "held" from the last time the menu was open
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            ImGui::GetIO().AddMouseButtonEvent(1, false);
        }
        if (g_realClipCursor) g_realClipCursor(nullptr);
        if (g_hwnd) {
            RECT rc;
            GetClientRect(g_hwnd, &rc);
            POINT center = { rc.right / 2, rc.bottom / 2 };
            ClientToScreen(g_hwnd, &center);
            if (g_realSetCursorPos) g_realSetCursorPos(center.x, center.y);
        }
    }
}

static bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// Hotkeys work in every frame, also while the menu is closed
static void PollHotkeys()
{
    static bool f8Before = false, escBefore = false;
    bool foreground = g_hwnd && GetForegroundWindow() == g_hwnd;
    bool f8 = foreground && KeyDown(VK_F8);
    bool esc = foreground && KeyDown(VK_ESCAPE);
    if (f8 && !f8Before) SetMenuOpen(!g_menuOpen);
    else if (esc && !escBefore && g_menuOpen) SetMenuOpen(false);
    f8Before = f8;
    escBefore = esc;
}

static void FeedImGuiInput()
{
    ImGuiIO& io = ImGui::GetIO();
    bool foreground = GetForegroundWindow() == g_hwnd;

    POINT pt;
    RECT rc;
    if (GetCursorPos(&pt) && ScreenToClient(g_hwnd, &pt) && GetClientRect(g_hwnd, &rc) && rc.right > 0 && rc.bottom > 0) {
        float sx = io.DisplaySize.x / (float)rc.right;
        float sy = io.DisplaySize.y / (float)rc.bottom;
        io.AddMousePosEvent(pt.x * sx, pt.y * sy);
    }
    if (g_rawMouse || g_legacyMouse) {
        // every press and release in order; ImGui spreads several of them over the next frames
        std::lock_guard<std::mutex> guard(g_clickLock);
        for (auto& c : g_clicks) io.AddMouseButtonEvent(c.button, c.down);
        g_clicks.clear();
        for (unsigned int c : g_chars) io.AddInputCharacter(c);
        g_chars.clear();
        if (!foreground) {   // a release outside the game window never arrives
            io.AddMouseButtonEvent(0, false);
            io.AddMouseButtonEvent(1, false);
        }
    } else {
        io.AddMouseButtonEvent(0, foreground && KeyDown(VK_LBUTTON));
        io.AddMouseButtonEvent(1, foreground && KeyDown(VK_RBUTTON));
    }
    int wheel = g_wheel.exchange(0);
    if (wheel) io.AddMouseWheelEvent(0.0f, wheel / (float)WHEEL_DELTA);

    static const struct { int vk; ImGuiKey key; } kKeys[] = {
        { VK_UP, ImGuiKey_UpArrow }, { VK_DOWN, ImGuiKey_DownArrow }, { VK_LEFT, ImGuiKey_LeftArrow },
        { VK_RIGHT, ImGuiKey_RightArrow }, { VK_RETURN, ImGuiKey_Enter }, { VK_SPACE, ImGuiKey_Space },
        { VK_TAB, ImGuiKey_Tab }, { VK_HOME, ImGuiKey_Home }, { VK_END, ImGuiKey_End },
        { VK_PRIOR, ImGuiKey_PageUp }, { VK_NEXT, ImGuiKey_PageDown },
        { VK_BACK, ImGuiKey_Backspace }, { VK_DELETE, ImGuiKey_Delete }, { VK_ESCAPE, ImGuiKey_Escape },
    };
    for (auto& k : kKeys) io.AddKeyEvent(k.key, foreground && KeyDown(k.vk));
    io.AddKeyEvent(ImGuiMod_Ctrl, foreground && KeyDown(VK_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, foreground && KeyDown(VK_SHIFT));
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------

static void ApplyStyle(float scale)
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 10.0f;
    s.FrameRounding = 6.0f;
    s.GrabRounding = 6.0f;
    s.WindowPadding = ImVec2(18, 14);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(12, 8);
    s.WindowBorderSize = 1.0f;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.08f, 0.11f, 0.94f);
    c[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.13f, 0.22f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.18f, 0.32f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.80f, 0.65f, 0.35f, 0.45f);
    c[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.18f, 0.24f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.26f, 0.34f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.36f, 0.30f, 0.40f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.78f, 0.40f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.90f, 0.72f, 0.36f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.82f, 0.45f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.36f, 0.24f, 0.38f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.48f, 0.32f, 0.50f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.38f, 0.58f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.80f, 0.65f, 0.35f, 0.35f);
    c[ImGuiCol_Text] = ImVec4(0.95f, 0.92f, 0.88f, 1.0f);
    c[ImGuiCol_NavHighlight] = ImVec4(0.95f, 0.78f, 0.40f, 1.0f);
    s.ScaleAllSizes(scale);
}

// Informational limits for an unchanged dedicated server; never rewrite settings.
static const char* MultiplayerWarning(const Setting& st)
{
    if (strcmp(st.key, "radiusMeters") == 0 && atof(st.value.c_str()) > 5.0)
        return T("Mehrspieler: Ueber 5 m unzuverlaessig; der Server begrenzt die Reichweite.",
                 "Multiplayer: above 5 m is unreliable; server reach limits still apply.");
    if (strcmp(st.key, "autoSmash") == 0 || strcmp(st.key, "smashBuildableAwayFromBase") == 0)
        return T("Mehrspieler: funktioniert nicht mit einer reinen Client-Mod.",
                 "Multiplayer: does not work with a client-only mod.");
    if (strcmp(st.key, "stackSize") == 0)
        return T("Mehrspieler: geaenderte Stapelgroessen werden vom Server nicht uebernommen.",
                 "Multiplayer: custom stack sizes are not applied by the server.");
    if (strcmp(st.key, "cheatBestBackpack") == 0)
        return T("Mehrspieler: geaenderte Rezepte werden vom Server nicht uebernommen.",
                 "Multiplayer: custom backpack recipes are not applied by the server.");
    if (strcmp(st.key, "instantLoot") == 0)
        return T("Mehrspieler: entfernt weder Ernteanimation noch Verlangsamung.",
                 "Multiplayer: does not remove harvesting animations or slowdown.");
    if (strcmp(st.key, "requireLineOfSight") == 0)
        return T("Mehrspieler: Ausschalten umgeht die Sichtpruefung des Servers nicht.",
                 "Multiplayer: switching this off cannot bypass server visibility checks.");
    return nullptr;
}

// One setting: checkbox, slider or slider + text field. Returns true when a restart is pending for it.
static bool HelperLiveSetting(const Setting& st)
{
    if (!GetModuleHandleW(L"ClientLootRequests.asi")) return false;
    // These switches are consumed from config.txt by the multiplayer helper.
    // Asset-only settings keep the upstream restart marker.
    const char* keys[] = { "collectGroundLoot", "collectStones", "collectCollectibles",
                          "collectPlants", "collectFarmPlants", "lootEnemyCorpses", "lootChests" };
    for (const char* key : keys) if (strcmp(st.key, key) == 0) return true;
    return false;
}

static bool DrawSetting(Setting& st, float fs)
{
    ImGui::PushID(st.key);
    bool changed = false, commit = false;
    if (st.kind == Kind::Bool) {
        bool v = IsTrue(st.value);
        if (ImGui::Checkbox(LabelText(st), &v)) { st.value = v ? "true" : "false"; changed = commit = true; }
    } else if (st.kind == Kind::Int) {
        int v = atoi(st.value.c_str());
        ImGui::SetNextItemWidth(fs * 10.0f);
        if (ImGui::SliderInt("##slider", &v, (int)st.min, (int)st.max, v == 0 ? T("Standard", "Default") : "%d",
                             ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            st.value = std::to_string(v);
            changed = true;
        }
        commit = ImGui::IsItemDeactivatedAfterEdit();
        // exact value: type it or step with the buttons (max 65535, the game's limit)
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 7.5f);
        int typed = v;
        if (ImGui::InputInt("##input", &typed, 100, 1000)) {
            if (typed < 0) typed = 0;
            if (typed > 65535) typed = 65535;
            st.value = std::to_string(typed);
            changed = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;
        ImGui::SameLine();
        ImGui::TextUnformatted(LabelText(st));
    } else {
        float v = (float)atof(st.value.c_str());
        ImGui::SetNextItemWidth(fs * 10.0f);
        if (ImGui::SliderFloat(LabelText(st), &v, st.min, st.max, "%.0f m",
                               ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            char buf[32];
            sprintf_s(buf, "%.0f", v);
            st.value = buf;
            changed = true;
        }
        commit = ImGui::IsItemDeactivatedAfterEdit();
    }
    const char* help = HelpText(st);
    if (help && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(fs * 24.0f);
        ImGui::TextUnformatted(help);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (changed && st.live) ApplyLiveSetting(st.key, st.value);
    if (commit) SaveSetting(st);
    bool pending = !st.live && !HelperLiveSetting(st) && st.value != st.startValue;
    if (pending) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.30f, 1.0f), "*");
    }
    if (const char* warning = MultiplayerWarning(st)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.26f, 0.26f, 1.0f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fs * 24.0f);
        ImGui::TextWrapped("%s", warning);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
    return pending;
}

// Sections side by side, so the menu fits a 16:9 screen without scrolling
static const char* const kColumns[][3] = {
    { "Einsammeln", nullptr, nullptr },
    { "Plündern", "Zerschlagen", nullptr },
    { "Inventar", "Mod", nullptr },
};

static void DrawMenu()
{
    ImGuiIO& io = ImGui::GetIO();
    float fs = g_fontSize;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool open = true;
    if (ImGui::Begin(T("Auto Loot Plus  –  Einstellungen###alp", "Auto Loot Plus  –  Settings###alp"), &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        bool restartPending = false;
        const int columnCount = (int)(sizeof kColumns / sizeof kColumns[0]);
        auto columnHasContent = [&](int col) {
            for (const char* section : kColumns[col]) {
                if (!section) break;
                for (auto& st : g_settings)
                    if (strcmp(st.section, section) == 0 && (!g_lite || IsDllSetting(st))) return true;
            }
            return false;
        };
        int visibleColumns = 0;
        for (int col = 0; col < columnCount; ++col) if (columnHasContent(col)) ++visibleColumns;
        if (visibleColumns > 0 &&
            ImGui::BeginTable("columns", visibleColumns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
            for (int col = 0; col < columnCount; ++col) {
                if (!columnHasContent(col)) continue;
                ImGui::TableNextColumn();
                ImGui::BeginGroup();
                ImGui::Dummy(ImVec2(fs * 18.0f, 0.0f));   // minimum column width
                for (const char* section : kColumns[col]) {
                    if (!section) break;
                    bool first = true;
                    for (auto& st : g_settings) {
                        if (strcmp(st.section, section) != 0) continue;
                        if (g_lite && !IsDllSetting(st)) continue;
                        if (first) {
                            ImGui::Spacing();
                            ImGui::SeparatorText(SectionText(st));
                            first = false;
                        }
                        if (DrawSetting(st, fs)) restartPending = true;
                    }
                }
                ImGui::EndGroup();
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::Separator();
        if (g_lite)
            ImGui::TextDisabled("%s", T("Lite: ohne EML. Nur Gegenstände am Boden; Leichen, Truhen, Pflanzen, Zerschlagen und Stapel brauchen die Vollversion.",
                                        "Lite: without EML. Items on the ground only; corpses, chests, plants, smashing and stacks need the full version."));
        ImGui::TextDisabled("%s", T("Änderungen werden sofort gespeichert.", "Changes are saved immediately."));
        if (restartPending)
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.30f, 1.0f), "%s",
                               T("* gilt erst nach einem Neustart des Spiels", "* takes effect after restarting the game"));
        ImGui::Spacing();
        if (ImGui::Button(T("Schließen  (F8)###close", "Close  (F8)###close"))) open = false;
    }
    ImGui::End();
    if (!open) SetMenuOpen(false);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

static PFN_vkVoidFunction LoadForImGui(const char* name, void*)
{
    PFN_vkVoidFunction f = g_real_vkGetDeviceProcAddr(g_swap.device, name);
    if (!f) f = g_real_vkGetInstanceProcAddr(g_instance, name);
    return f;
}

static VkPhysicalDevice PhysicalFor(VkDevice device)
{
    for (auto& d : g_devices) if (d.device == device) return d.physical;
    return VK_NULL_HANDLE;
}

static bool FamilyOf(VkQueue queue, uint32_t* family)
{
    for (auto& q : g_queues) if (q.queue == queue) { *family = q.family; return true; }
    return false;
}

static bool HasGraphics(VkPhysicalDevice physical, uint32_t family)
{
    auto getProps = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        g_real_vkGetInstanceProcAddr(g_instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    if (!getProps) return false;
    uint32_t count = 0;
    getProps(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    getProps(physical, &count, props.data());
    return family < count && (props[family].queueFlags & VK_QUEUE_GRAPHICS_BIT);
}

static bool Fail(const char* what)
{
    Log("Menü: %s – Menü für diese Sitzung aus", what);
    g_broken = true;
    return false;
}

static bool EnsureResources(VkQueue queue, uint32_t family)
{
    VkDevice dev = g_swap.device;
    if (!LoadDeviceFns(dev)) return Fail("Vulkan-Funktionen fehlen");

    if (!g_pool || g_poolFamily != family) {
        if (g_pool) { DestroyFrames(); g_d.DestroyCommandPool(dev, g_pool, nullptr); g_pool = VK_NULL_HANDLE; }
        VkCommandPoolCreateInfo ci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = family;
        if (g_d.CreateCommandPool(dev, &ci, nullptr, &g_pool) != VK_SUCCESS) return Fail("Command-Pool");
        g_poolFamily = family;
    }

    if (!g_renderPass || g_renderPassFormat != g_swap.format) {
        if (g_imguiVulkan) { ImGui_ImplVulkan_Shutdown(); g_imguiVulkan = false; }
        if (g_renderPass) g_d.DestroyRenderPass(dev, g_renderPass, nullptr);
        VkAttachmentDescription att{};
        att.format = g_swap.format;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;            // keep the game's frame
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        ci.attachmentCount = 1;
        ci.pAttachments = &att;
        ci.subpassCount = 1;
        ci.pSubpasses = &sub;
        ci.dependencyCount = 1;
        ci.pDependencies = &dep;
        if (g_d.CreateRenderPass(dev, &ci, nullptr, &g_renderPass) != VK_SUCCESS) return Fail("Render-Pass");
        g_renderPassFormat = g_swap.format;
    }

    if (!g_swap.built) {
        uint32_t count = 0;
        if (g_d.GetSwapchainImagesKHR(dev, g_swap.swapchain, &count, nullptr) != VK_SUCCESS || !count)
            return Fail("Swapchain-Bilder");
        std::vector<VkImage> images(count);
        g_d.GetSwapchainImagesKHR(dev, g_swap.swapchain, &count, images.data());
        g_swap.frames.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            Frame& fr = g_swap.frames[i];
            VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vi.image = images[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = g_swap.format;
            vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            if (g_d.CreateImageView(dev, &vi, nullptr, &fr.view) != VK_SUCCESS) return Fail("Image-View");
            VkFramebufferCreateInfo fi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            fi.renderPass = g_renderPass;
            fi.attachmentCount = 1;
            fi.pAttachments = &fr.view;
            fi.width = g_swap.extent.width;
            fi.height = g_swap.extent.height;
            fi.layers = 1;
            if (g_d.CreateFramebuffer(dev, &fi, nullptr, &fr.framebuffer) != VK_SUCCESS) return Fail("Framebuffer");
            VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            ai.commandPool = g_pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            if (g_d.AllocateCommandBuffers(dev, &ai, &fr.cmd) != VK_SUCCESS) return Fail("Command-Buffer");
            VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if (g_d.CreateFence(dev, &fci, nullptr, &fr.fence) != VK_SUCCESS) return Fail("Fence");
            VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            if (g_d.CreateSemaphore(dev, &sci, nullptr, &fr.done) != VK_SUCCESS) return Fail("Semaphore");
        }
        g_swap.built = true;
    }

    if (!g_imguiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.MouseDrawCursor = true;
        g_fontSize = (float)g_swap.extent.height / 50.0f;
        if (g_fontSize < 16.0f) g_fontSize = 16.0f;
        if (g_fontSize > 56.0f) g_fontSize = 56.0f;
        wchar_t winDir[MAX_PATH];
        GetWindowsDirectoryW(winDir, MAX_PATH);
        char font[MAX_PATH * 3];
        WideCharToMultiByte(CP_UTF8, 0, (std::wstring(winDir) + L"\\Fonts\\segoeui.ttf").c_str(), -1, font, sizeof font, nullptr, nullptr);
        if (GetFileAttributesA(font) == INVALID_FILE_ATTRIBUTES || !io.Fonts->AddFontFromFileTTF(font, g_fontSize, nullptr, kGlyphs)) {
            ImFontConfig cfg;
            cfg.SizePixels = g_fontSize;
            io.Fonts->AddFontDefault(&cfg);
        }
        ApplyStyle(g_fontSize / 18.0f);
        g_imguiContext = true;
    }

    uint32_t imageCount = (uint32_t)g_swap.frames.size();
    if (g_imguiVulkan && g_imguiImageCount != imageCount) { ImGui_ImplVulkan_Shutdown(); g_imguiVulkan = false; }
    if (!g_imguiVulkan) {
        if (!g_imguiFunctions) {
            if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_0, &LoadForImGui, nullptr)) return Fail("ImGui-Vulkan-Funktionen");
            g_imguiFunctions = true;
        }
        ImGui_ImplVulkan_InitInfo ii = {};
        ii.ApiVersion = VK_API_VERSION_1_0;
        ii.Instance = g_instance;
        ii.PhysicalDevice = PhysicalFor(dev);
        ii.Device = dev;
        ii.QueueFamily = family;
        ii.Queue = queue;
        ii.RenderPass = g_renderPass;
        ii.MinImageCount = g_swap.minImages <= imageCount ? g_swap.minImages : imageCount;
        if (ii.MinImageCount < 2) ii.MinImageCount = 2;
        ii.ImageCount = imageCount < ii.MinImageCount ? ii.MinImageCount : imageCount;
        ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ii.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE + 4;
        if (!ii.PhysicalDevice) return Fail("Grafikkarte nicht zugeordnet");
        if (!ImGui_ImplVulkan_Init(&ii)) return Fail("ImGui-Vulkan-Start");
        g_imguiVulkan = true;
        g_imguiImageCount = imageCount;
        Log("Menü bereit (%ux%u, Format %d, %u Bilder)", g_swap.extent.width, g_swap.extent.height, (int)g_swap.format, imageCount);
    }
    return true;
}

// Records and submits the menu for one swapchain image. Returns the semaphore the present must
// wait on, or VK_NULL_HANDLE when nothing was drawn.
static VkSemaphore RenderMenu(VkQueue queue, uint32_t imageIndex, uint32_t waitCount, const VkSemaphore* waits)
{
    std::lock_guard<std::mutex> guard(g_lock);
    if (!g_real_vkGetInstanceProcAddr || !g_instance) { Fail("Vulkan-Instanz unbekannt"); return VK_NULL_HANDLE; }
    if (!g_real_vkGetDeviceProcAddr)
        g_real_vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(g_real_vkGetInstanceProcAddr(g_instance, "vkGetDeviceProcAddr"));
    if (!g_real_vkGetDeviceProcAddr) { Fail("vkGetDeviceProcAddr fehlt"); return VK_NULL_HANDLE; }
    uint32_t family = 0;
    if (!FamilyOf(queue, &family)) { Fail("Present-Queue unbekannt"); return VK_NULL_HANDLE; }
    if (!HasGraphics(PhysicalFor(g_swap.device), family)) { Fail("Present-Queue kann nicht zeichnen"); return VK_NULL_HANDLE; }
    if (!EnsureResources(queue, family)) return VK_NULL_HANDLE;
    if (imageIndex >= g_swap.frames.size()) return VK_NULL_HANDLE;
    VkDevice dev = g_swap.device;
    Frame& fr = g_swap.frames[imageIndex];
    if (g_d.WaitForFences(dev, 1, &fr.fence, VK_TRUE, 1000000000ull) != VK_SUCCESS) return VK_NULL_HANDLE;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)g_swap.extent.width, (float)g_swap.extent.height);
    static LARGE_INTEGER freq = {}, last = {};
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    io.DeltaTime = last.QuadPart ? (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart : 1.0f / 60.0f;
    if (io.DeltaTime <= 0.0f || io.DeltaTime > 1.0f) io.DeltaTime = 1.0f / 60.0f;
    last = now;

    FeedImGuiInput();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    DrawMenu();
    ImGui::Render();

    g_d.ResetFences(dev, 1, &fr.fence);
    g_d.ResetCommandBuffer(fr.cmd, 0);
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    g_d.BeginCommandBuffer(fr.cmd, &bi);
    VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp.renderPass = g_renderPass;
    rp.framebuffer = fr.framebuffer;
    rp.renderArea.extent = g_swap.extent;
    g_d.CmdBeginRenderPass(fr.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), fr.cmd);
    g_d.CmdEndRenderPass(fr.cmd);
    g_d.EndCommandBuffer(fr.cmd);

    std::vector<VkPipelineStageFlags> stages(waitCount, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.waitSemaphoreCount = waitCount;
    si.pWaitSemaphores = waits;
    si.pWaitDstStageMask = stages.data();
    si.commandBufferCount = 1;
    si.pCommandBuffers = &fr.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &fr.done;
    if (g_d.QueueSubmit(queue, 1, &si, fr.fence) != VK_SUCCESS) {
        // The fence stays unsignaled; recreate it so the next wait does not stall
        g_d.DestroyFence(dev, fr.fence, nullptr);
        VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        g_d.CreateFence(dev, &fci, nullptr, &fr.fence);
        Fail("Zeichnen abgelehnt");
        return VK_NULL_HANDLE;
    }
    return fr.done;
}

static VkSemaphore RenderMenuGuarded(VkQueue queue, uint32_t imageIndex, uint32_t waitCount, const VkSemaphore* waits)
{
    __try {
        return RenderMenu(queue, imageIndex, waitCount, waits);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_broken = true;
        Log("Menü: Absturz beim Zeichnen abgefangen (Code %08X) – Menü für diese Sitzung aus", GetExceptionCode());
        return VK_NULL_HANDLE;
    }
}

static VkResult VKAPI_CALL Hook_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info)
{
    static bool logged = false;
    if (!logged) { logged = true; Log("Menü: Spielbild erkannt, F8 öffnet die Einstellungen"); }

    if (!g_hwnd) {   // surface created without our wrapper: take the game's foreground window
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (pid == GetCurrentProcessId()) g_hwnd = fg;
    }

    if (g_hwnd && !g_origWndProc) {
        g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&MenuWndProc)));
        if (!g_origWndProc) Log("Menü: Fenster nicht übernommen (%lu)", GetLastError());
    }
    if (g_broken || !g_origWndProc) return g_real_vkQueuePresentKHR(queue, info);

    PollHotkeys();
    if (!g_menuOpen || info->swapchainCount < 1 || info->pSwapchains[0] != g_swap.swapchain)
        return g_real_vkQueuePresentKHR(queue, info);

    VkSemaphore done = RenderMenuGuarded(queue, info->pImageIndices[0], info->waitSemaphoreCount, info->pWaitSemaphores);
    if (!done) return g_real_vkQueuePresentKHR(queue, info);
    VkPresentInfoKHR copy = *info;
    copy.waitSemaphoreCount = 1;
    copy.pWaitSemaphores = &done;
    return g_real_vkQueuePresentKHR(queue, &copy);
}

// ---------------------------------------------------------------------------
// Install: redirect imports of enshrouded.exe (runs in DllMain, before the game starts Vulkan)
// ---------------------------------------------------------------------------

static bool PatchImport(HMODULE module, void* original, void* replacement)
{
    auto base = reinterpret_cast<uint8_t*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !original) return false;
    bool done = false;
    for (auto desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
        for (auto thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk); thunk->u1.Function; ++thunk) {
            if (reinterpret_cast<void*>(thunk->u1.Function) != original) continue;
            DWORD old;
            if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                thunk->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
                VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                done = true;
            }
        }
    }
    return done;
}

void Overlay_Install()
{
    LoadSettings();
    HMODULE game = GetModuleHandleW(nullptr);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    HMODULE user = GetModuleHandleW(L"user32.dll");
    void* gpa = kernel ? reinterpret_cast<void*>(GetProcAddress(kernel, "GetProcAddress")) : nullptr;
    g_realSetCursorPos = user ? reinterpret_cast<SetCursorPosFn>(GetProcAddress(user, "SetCursorPos")) : nullptr;
    g_realClipCursor = user ? reinterpret_cast<ClipCursorFn>(GetProcAddress(user, "ClipCursor")) : nullptr;
    bool okGpa = PatchImport(game, gpa, reinterpret_cast<void*>(&Hook_GetProcAddress));
    bool okPos = PatchImport(game, reinterpret_cast<void*>(g_realSetCursorPos), reinterpret_cast<void*>(&Hook_SetCursorPos));
    bool okClip = PatchImport(game, reinterpret_cast<void*>(g_realClipCursor), reinterpret_cast<void*>(&Hook_ClipCursor));
    Log("Menü: Importe umgeleitet GetProcAddress=%d SetCursorPos=%d ClipCursor=%d", okGpa, okPos, okClip);
}
