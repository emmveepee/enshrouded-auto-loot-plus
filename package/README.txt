Run Install.cmd from the extracted package while Enshrouded is closed.
See the repository README for compatibility, recovery and experimental limits.
F8 opens Auto Loot settings. Growing crops and pet dogs/cats are excluded.
The package contains no auto-salvage mod. Do not overwrite an existing combined loader without reviewing it.
Build136 adds ready farm-animal produce (e.g. goat milk) and renewable harvest trees. Set [Safety] CollectAnimalProduce=0 to disable animal collection; ordinary pet/feed interactions are excluded.
Build137 prevents known non-quest out-of-radius receipts from permanently disabling nearby auto-loot. Ordinary outgoing range checks remain enforced.
Build169 removes the inherited native corpse/container accept and loot-all hooks. The guarded client helper owns those interactions and their settings. Linked corpse inventory pages now count toward eligibility and quest protection. Native tests pass; fresh gameplay and special-drop conservation remain unverified. This update does not restore previously missing items or include private animation/sprint experiments.
