# Effects from Pastel EFX Grid

The groovebox's master effects (a chain of 8 slots) and its 8 hold-to-play performance effects use the effects of
the user's Pastel EFX Grid (`~/ai_experiments/pastel-efx-grid/Source`), borrowed at the user's request
("You can borrow effects from EFX grid"). The files here are copies of that project's `EffectRegistry.*` and
`dsp/` (the registry is generated there from `fx_spec.py`; regenerate it there and copy it over).

One local change survives a copy and must be redone after one: the category colours (`catAccent`, `catDim`,
`catInk`) and the `juce_graphics` include are wrapped in `#if JUCE_MODULE_AVAILABLE_juce_graphics`, so the
headless Android core (../minibox-android) builds without JUCE's graphics module.

Minibox's own code is `MasterFx.h/.cpp` (the chain and the pads behind the master bus's insert point,
standalone only) — see docs/master-fx.md.
