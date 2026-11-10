# ofxManifold OSC out

What an ofxManifold player sends, and what it listens for. Written for anyone
building a receiver: in SuperCollider, Max, Pd, TouchDesigner, a lighting
desk, a script. `example-osc` sends it; `example-osc/supercollider/` and
`example-osc/receive.py` are two receivers to start from.

The idea in two lines: **the receiver does not need to know how many outputs
there are**, nor **how many maps are playing.** The sender tells it, and
tells it again when that changes, so the messages grow and shrink with the
maps.

This is **protocol 2**. Protocol 1 (the first 3B-1) sent everything under
`/manifold` alone, with no map's name and no roll call; one map at a time.

## Maps and their prefixes

Every map's messages go under its own prefix: the root, `/manifold`, then
the map's name -- `/manifold/room`, `/manifold/forest`. The name is the
map's file name, with any of ` #*,/?[]{}` replaced by `_` (so `my room`
becomes `my_room`); a map with no name is `untitled`. In the tables below
`/manifold/room` stands for any map's prefix.

Several maps can play at once -- copies of `example-osc` side by side, or
one player with several maps -- and a receiver tells them apart by prefix.
Two maps with the same name cannot be told apart; give one another name.

## The roll call

| Address | Arguments | When |
|---|---|---|
| `/manifold/map` | `s` prefix, `i` answer port, `i` N outputs, `i` protocol (2) | just before every `outputs` message (below): the first send, every change, when asked, every 2 seconds |

The roll call is always on the root, whatever the map's name, so a receiver
listens to one address to find every map playing: what it is called, where
it hears answers (0: nowhere), and how many outputs it has. A map that has
not answered the roll call for a few seconds (example's receivers: 5) has
stopped.

## Messages out, per map

| Address | Arguments | When |
|---|---|---|
| `/manifold/room/outputs` | `i` N, then N pairs of `s` name, `i` channel | with the roll call: the first send; whenever an output is added or removed or its channel changes; when asked; every 2 seconds |
| `/manifold/room/levels` | N × `f` | every send, if packed is on (the default) |
| `/manifold/room/out/NAME` | `f` | per output, if that is on: what changed, at every send; all of them every 0.1 s |
| `/manifold/room/values` | `i` M, then M pairs of `s` name, `f` value | every send, if packed is on and the map has values |
| `/manifold/room/value/NAME` | `f` | per value, as per output |
| `/manifold/room/position` | `f` x, `f` y, `f` z | every send |

- **Levels are in the order `outputs` lists the outputs**, which is channel
  order. The channel numbers say where each would go on a device; they need
  not be 0, 1, 2 -- a gap is a channel nothing feeds.
- **A level is a gain**, as the map routes it at the point, after the
  output's trim -- what the editor's faders show. Outside the map every level
  is 0. The sender smooths it (below).
- **x and y are on the map, 0 to 1**, y up. **z is the height** in the map's
  own units -- its levels' heights.
- **Per-output addresses use the output's name** cleaned as map names are.
  The `outputs` message has the name as written.
- **Sends happen at a rate, 60 a second by default** (settable from 1 to
  1000), never twice in one period; a frame that comes late sends once, then
  the rate carries on from it.
- **A map that changes its name** -- another file opened -- says everything
  again at once under the new prefix: roll call, outputs, levels.

## Messages in

Optional. A sender sends everything whether anyone answers or not. Send them
to the map's answer port, from its roll call.

| Address | Arguments | Effect |
|---|---|---|
| `/manifold/room/describe` | none | the next send has the roll call and `outputs` |
| `/manifold/room/hello` | `s` name, optional | the sender shows the receiver as connected, by that name, for 3 seconds; say it once a second to stay connected |
| `/manifold/describe`, `/manifold/hello` | as above | the same, on the root: heard by whichever map listens on the port it is sent to |

A receiver starting up need not wait two seconds for the roll call: send
`/manifold/describe` to each port maps may be listening on, and each answers
at once.

`example-osc` sends to port 57120 (sclang's) and hears answers on the first
port free from 57121 (up to 57136), so several copies run side by side; the
panel shows which. Both are set at the top of its `ofApp.h`. It does not
share its port with another program (no SO_REUSEPORT): a second copy takes
the next one rather than quietly splitting the first one's answers.

## On the wire

OSC 1.0: one message per UDP packet, no bundles, arguments `i` (32-bit int),
`f` (32-bit float) and `s` (string) only -- what SuperCollider, Max and Pd
all read without a bundle parser. Every message is checked byte for byte
against an independent OSC library (python-osc) in the tests.

## A receiver in outline

1. Listen on `/manifold/map`. Ask, if you like: send `/manifold/describe` to
   ports 57121 to 57136.
2. On a roll call from a prefix you have not seen, start a map: note its
   answer port, listen on its `outputs` and `levels`. On every roll call,
   note the time; forget a map silent for a few seconds.
3. On a map's `outputs`, if the list differs from the one you have, rebuild:
   one voice, light, parameter or channel per output.
4. On its `levels`, set each from its argument, in order. Smoothing over
   about 20 ms hides the steps between sends. The sender smooths jumps
   itself -- a click across the map, the point leaving it -- so a receiver
   needs no more than that (`OutputSmoother`, half-life 0.05 s in
   example-osc).
5. Optionally say the map's `hello` to its answer port once a second.

The SuperCollider patch beside `example-osc` is exactly this.
