# Odds

A collection of small, odd VCV Rack modules by Haer.

## Minimal Nation

One big knob, one input, one output. The signal passes straight through (all
polyphony channels) and the knob does nothing. It just looks good.

12 HP, white panel, Hær knob design 241: a fixed shaded ball inside a black
ring, with a black bar and red dot that turn.

## Poltergeist

A ghost that lives in your rack and moves real cables. 14 HP.

- **FENCE** (1-16): the ghost may only touch the next N modules to its right on
  the same row (never VCV Core modules such as Audio and MIDI). It only moves
  cables with both ends inside the fence, so cables to your mixer or Audio
  module are safe. The fence glows mint green on the rack.
- **HAUNT** (trigger or button, **CHANCE** decides if a trigger counts): one
  piece of mischief, picked by the **SWAP / WANDER / VANISH** weights.
  SWAP exchanges where two cables go, WANDER moves one end of a cable to another
  socket, VANISH pulls a cable out and puts it back after **RETURN** (0.1-20 s,
  or never).
- **EXORCISE** (trigger or button): undoes everything the ghost did, newest first.
  Every mischief is also a Ctrl+Z step.
- **MISCHIEF** out fires when a cable moves; **HAUNTED** out is 1 V per mischief
  not yet undone.
- Right-click: allow patching a module into itself, turn the Ctrl+Z steps off,
  show the fence always / while turning FENCE / never.

Panel: `python3 res/make_poltergeist.py`, preview: `python3 tools/preview_poltergeist.py out.png`.

## Building

Every push builds the plugin for Apple Silicon Macs in GitHub Actions. Pushes to
`main` publish to the "latest" release, branch pushes to "preview".

Panel and knob art: `pip install fonttools && python3 res/make_panel.py`.
Layout check without Rack: `python3 tools/preview.py preview.png`.

## Licence

MIT (see `LICENSE.txt`): you're free to use, change, port (VST/CLAP, other platforms, hardware) and share this code, as long as the copyright notice and licence text come along. Fonts on the panel (Inter, Climate Crisis) are under the SIL Open Font Licence.
