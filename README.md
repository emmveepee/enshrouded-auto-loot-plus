# Auto Loot Plus — Client Adaptation

Experimental Windows x64 client mod for Steam Enshrouded. Based on Auto Loot Plus 1.0.3 by **cw** (MIT), with a client-side pickup helper and F8 settings.

**Build 169** removes the inherited native corpse/container collector, which could continue into loot-all without establishing an accepted interaction. Corpse and harvest interactions now belong exclusively to the guarded client helper, including its category settings. The client also checks linked inventory pages before deciding that a corpse is empty. Normal native loose-pickup radius/visibility handling remains. Other players and the server can remain unmodified.

## Download and install

Download **AutoLootPlus-Client-169.zip** from [Releases](https://github.com/emmveepee/enshrouded-auto-loot-plus/releases), extract the whole ZIP, close the game, then run `Install.cmd`. It finds Steam libraries or asks for the game folder. Start the game normally through Steam; press **F8** for settings.

The installer checks package hashes, refuses differing existing files by default, backs up replacements, and generates paths for your PC. Keep the extracted folder's `Backups` directory for recovery. Existing custom ASI loaders require review and `-UseExistingAsiLoader`; do not overwrite one blindly. Updates to an existing auto-loot installation may need `-ReplaceExisting` after reviewing its configuration.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Install.ps1 -GamePath 'D:\SteamLibrary\steamapps\common\Enshrouded'
```

No compiler, Python, EML installation, server mod or offline companion is required to use the ZIP. An optional Ultimate ASI Loader is included for installations without an ASI loader. Do not run multiple auto-loot helpers or point an existing combined loot/salvage loader at this helper without reviewing the arrangement; this package contains no salvage mod.

## Behavior and limits

- Default radius: 5 m. Ground loot, collectibles, wild plants, mature farm crops and enemy corpses enabled. Chests, smashing and asset-only cheats disabled.
- Growing crops and placed pet dogs/cats are excluded from automatic interactions. Manual interaction remains available. Ready produce from eight supported farm-animal types is collected only when the animal is alive, tamed, offering the generated produce verb, and confirmed as the native selected target; pet/feed offers are excluded. Pet-family exclusion was confirmed by live classification and native tests; a new in-range pet gameplay test was not performed for build 136.
- **Mixed wild/growing patches are not fully solved.** Wild plants remain eligible, but when native selection stays on a growing crop the helper waits to avoid uprooting it. Some resources can still require manual pickup.
- Corpse inventory can appear briefly. The helper uses the native transfer/empty-menu behavior; completely invisible corpse looting is not guaranteed.
- No automatic walking or rendered-camera steering. Focus, menus, normal interaction reach and server authority still matter. Configured radius does not override server reach limits.
- Not all inherited F8 options have a client-only implementation. Leave chest looting, auto-smash, stack-size/backpack cheats and touch zones off.
- Corpse transfers read your Steam Enshrouded pickup/backpack preference. An ambiguous/missing settings profile can pause transfers. Save game settings through Steam before retrying.
- Tested executable fingerprint: `D64966E23B1A458EE6F147BCBCEAC0B577FA7E39A212F371287D8CC119817882`. A different game build is refused by default; `-AllowDifferentGameBuild` requires compatibility review. Runtime signatures also reject unsupported integrations.

This is an experimental development snapshot, not a promise of universal pickup or zero frame hitches. Build169's helper, bootstrap and menu are rebuilt from the published source. Twelve native suites and isolated installer checks pass; fresh gameplay, delayed special-drop conservation and Proton remain untested. This fixes confirmed code defects, but does not establish that they caused any particular missing drop or restore previously missing items. Native tests are documented in [VERIFICATION.md](docs/VERIFICATION.md). Unfinished private animation/sprint experiments are excluded.

Harvestable planted hazelnut, coffee, date, coconut and banana refill trees follow the farm-plants toggle. Seedlings remain protected. A live hazelnut-tree session produced accepted requests and inventory gains; live catdeer/goat collection also produced accepted requests and inventory gains. Exact item deltas were not separately logged.

## Configuration and removal

F8 settings: `mods/auto-loot-plus/config.txt`. Helper configuration: `ClientLootRequests.ini`. Set `[Safety] CollectAnimalProduce=0` to disable farm-animal produce collection independently of the plant toggles. Quiet helper log: `mods/AutoLootClient/AutoLoot.log`. `PauseAutomaticActions=1` is an intentional safety interlock for an older input route; the separately guarded resource/corpse routes remain enabled.

For rollback, close the game and inspect `Backups/<installation>/receipt.json`: restore saved originals for `Existed=true`; remove only matching installed files for `Existed=false`. Preserve other mods and any loader they use. Do not delete the whole `mods` directory.

## Build and test

Use MSVC x64 / C++17 from an x64 Native Tools Command Prompt, or set `VCVARS64` to your existing `vcvars64.bat`. Supply [Dear ImGui v1.91.9b](https://github.com/ocornut/imgui/tree/v1.91.9b) and [Vulkan-Headers v1.4.309](https://github.com/KhronosGroup/Vulkan-Headers/tree/v1.4.309) as local source dependencies:

```cmd
set IMGUI_DIR=D:\dependencies\imgui-1.91.9b
set VULKAN_INCLUDE_DIR=D:\dependencies\Vulkan-Headers-1.4.309\include
scripts\Build.cmd
scripts\Test.cmd
```

`src/client` contains the helper, bootstrap and native regression tests. `src/menu` contains the native/F8 implementation. Generated template/component/ABI tables are required lookup metadata, not game executables or asset archives. `Build.cmd` rebuilds and packages the helper, bootstrap and menu. Regenerate the package manifest with `scripts/Manifest.ps1` after intentional package changes.

## Credits and license

Upstream Auto Loot Plus 1.0.3 by cw; source supplied with the original mod's MIT notice. Local additions are MIT. See [LICENSE](LICENSE) and `package/licenses/` for upstream, local, Dear ImGui, Vulkan-Headers and [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) notices. No game executable, Steam API DLL, save, credentials, private logs or unrelated mods are distributed.



