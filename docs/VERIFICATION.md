# Build 134 verification

- Source snapshot builds successfully with MSVC 2022 x64 using published scripts.
- Published test runner passes all ten native suites: 12,372 menu/configuration/GUID checks; corpse selection and menu recovery; production pet classification across seven exported pet templates; 576 movement-preservation cases; camera composition; native UI command layout; resource-tail scope; radius parsing; actual Steam settings reader; production UI-pump safety/hitch fixture.
- UI-pump fixture: zero memory-region queries and zero synchronous file opens during two transfers. This is a native fixture, not a live FPS measurement.
- Original gameplay work demonstrated resource inventory increases and corpse transfer/native menu closure. Later fixes carry narrower validation. Build 134 classified the reported dog as Blocked in the live game; it was outside pickup range during that snapshot.
- Mixed growing/wild patch pickup, in-range no-auto-petting, fresh portable-package gameplay and Proton have not been validated. Do not treat synthetic checks as gameplay proof.
- Helper payload matches the exact deployed Probe134 DLL. Public bootstrap/menu rebuilds are packaging changes; no change was installed into the developer's running game during publication.
- Adapted installer passed an isolated fake-game fixture: payload integrity, recipient paths, collision refusal, backup receipts, unrelated files and custom ASI loader preservation. The running game was not used or modified.
