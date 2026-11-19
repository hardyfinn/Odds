# Odds

A collection of small, odd VCV Rack modules by Haer.

## Minimal Nation

One big knob, one input, one output. The signal passes straight through (all
polyphony channels) and the knob does nothing. It just looks good.

12 HP, white panel, Hær knob design 241: a fixed shaded ball inside a black
ring, with a black bar and red dot that turn.

## Building

Every push builds the plugin for Apple Silicon Macs in GitHub Actions. Pushes to
`main` publish to the "latest" release, branch pushes to "preview".

Panel and knob art: `pip install fonttools && python3 res/make_panel.py`.
Layout check without Rack: `python3 tools/preview.py preview.png`.
