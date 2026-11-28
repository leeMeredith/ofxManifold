# The Ambisonic Toolkit's room

An extra. The two files in `supercollider/` -- `manifold-receive.scd` (the
patch) and `manifold-modes.scd` (its choices) -- need nothing installed and
never use this folder. This folder adds one more room to the patch: each
speaker heard through ears measured on a dummy head, so front and behind,
above and below, are far clearer than the patch's own head.

- `manifold-atk-modes.scd` -- open this one. START runs the patch and adds
  the room; then choose ATK, MEASURED EARS or ATK, DRY. Its WHERE THINGS
  STAND block says what is installed and what to do next.
- `manifold-atk.scd` -- the room itself; START loads it.

## What it needs, once

1. **git** -- on a Mac, `xcode-select --install` in Terminal.
2. **sc3-plugins** --
   <https://github.com/supercollider/sc3-plugins/releases/download/Version-3.13.0/sc3-plugins-3.13.0-macOS.zip>.
   Its `SC3plugins` folder goes in
   `~/Library/Application Support/SuperCollider/Extensions`.
3. **The toolkit** -- THE TOOLKIT block in `manifold-atk-modes.scd`, then
   Language > Recompile Class Library.
4. **Its ears and matrices** -- ITS EARS in the same file; or by hand,
   <https://github.com/ambisonictoolkit/atk-kernels/releases/latest/download/kernels.zip>
   and
   <https://github.com/ambisonictoolkit/atk-matrices/releases/latest/download/matrices.zip>,
   their `kernels` and `matrices` folders into
   `~/Library/Application Support/ATK`.

On a Mac, plugins downloaded with a browser may be blocked ("cannot be
opened"). In Terminal:

    xattr -rd com.apple.quarantine ~/Library/Application\ Support/SuperCollider/Extensions/SC3plugins

## Adding another extra

A room is an event of functions added to `~manifold[\rooms]` once the patch
is playing -- `manifold-receive.scd` says which, under "rooms plug in".
Give each library a folder of its own beside this one.
