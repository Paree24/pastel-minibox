# Effects from Pastel EFX Grid

The groovebox's master effects (a chain of 8 slots) and its 8 hold-to-play performance effects use the effects of
the user's Pastel EFX Grid (`~/ai_experiments/Pastel Efxgrid/Source`), borrowed at the user's request
("You can borrow effects from EFX grid"). The files here are copies of that project's `EffectRegistry.*` and
`dsp/` (the registry is generated there from `fx_spec.py`; regenerate it there and copy it over).

Minibox's own code is `MasterFx.h/.cpp` (the chain and the pads behind the master bus's insert point,
standalone only) — see docs/master-fx.md.
