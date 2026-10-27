# Verification history

## Build 169 — native collector separation and linked inventories

- Removed native corpse query expansion, no-PickupItem branch redirection, accept-event construction, release callbacks and loot-all stub. There is no native corpse path to bypass the helper's corpse setting or safety checks.
- Production Patch() runs against synthetic executable memory with corpse settings on and off. Tests verify that vanilla query/skip/loot-all bytes stay unchanged, loose-item patches still install, and a missing required signature causes no writes. No game process or loot generator is executed.
- The actual page-aware inventory summary and eligibility policy are exercised with an empty root and a linked eternal-ice item, linked cycles, linked quest protection and fully empty pages. These are synthetic inventories; no real missing item was restored.
- All twelve published native suites pass, retaining zero per-transfer memory-region queries or synchronous file opens in the UI fixture. The installer fixture verifies payload hashes, generated paths, collision refusal, recovery and unrelated/custom-loader preservation.
- Helper, bootstrap and native menu are rebuilt together. Fresh gameplay, source/destination conservation for delayed special drops and full inventories, and Proton remain untested. This is a repair of confirmed code defects, not proof that the earlier mod caused a specific missing drop.

## Build 136 verification

- Source builds with existing MSVC x64. Eleven published native suites pass, including animal produce, pet exclusion, crop protection, corpse recovery, 12,372 menu/GUID checks, movement preservation, Steam preferences and the production UI pump.
- Added production animal-produce test exercises all eight exported tamed producer templates, untamed/dead/unoffered/other-verb cases, the independent setting, offer change immediately before a tap, exact native selection, and post-collection receipt handling. Seven dog/cat pet templates remain blocked.
- Full exported refill audit covers 31 templates: five planted renewable harvest-tree templates pass the structural/action/payload rule; living actors, growing crops, prototypes and missing action/payload are rejected by the tree rule. Farm animals use a separate tamed-produce rule.
- GUID hashing independently agrees with 2,157 game-exported actor-sequence hashes before deriving produce verb IDs.
- Real game: helper135 near hazelnuts recorded 12 accepted interactions and eight inventory-gain batches. Helper136 recorded accepted interactions with actual placed catdeer1032 and goat1031, two inventory-gain batches, and no safety stop. Exact milk/nut item deltas were not separately logged. This does not prove every animal species or every mixed patch works.
- Source/package installer fixture passes generated paths, integrity, collision refusal, backups, unrelated-file and custom-loader preservation.
- UI pump fixture retains zero per-transfer VirtualQuery calls and zero synchronous file opens. Native timing is not live FPS evidence.
- Mixed growing/wild patches can still wait on protected native selection. Fresh portable-package gameplay and Proton remain untested. Published helper is the exact deployed build136 binary; bootstrap/menu are portable rebuilds.
- User confirmed the live collection result: "It is properly collecting" during the build136 farm-animal check.

## Build 137 — out-of-radius receipt handling

A live136stop reported a known, enabled non-quest target with a valid position outside the current pickup radius.137 discards this receipt and waits for idle; it does not count pickup success, widen new interaction reach or latch all future pickup off. Unknown/protected/quest/missing-position targets retain strict safeguards. Native regression cases reproduce the captured guard inputs and verify their negative cases. Live137 resumed accepted interaction and inventory gains after deployment; the moved-receipt branch has not yet fired again in the observed window. Earlier136tree/produce gameplay evidence remains separate from this callback correction.
