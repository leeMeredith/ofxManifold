# ofxManifold OSC out

What an ofxManifold player sends, and what it listens for. Written for anyone
building a receiver: in SuperCollider, Max, Pd, TouchDesigner, a lighting
desk, a script. `example-osc` sends it; `example-osc/supercollider/` and
`example-osc/receive.py` are two receivers to start from.

The idea in two lines: **the receiver does not need to know how many outputs
there are**, nor **how many maps are playing.** The sender tells it, and
tells it again when that changes, so the messages grow and shrink with the
maps.

This is **protocol 4**: protocol 3 and **points moved from outside** -- a
receiver, or anything else on this machine, may take a map's point and move
it, by the messages under "Points moved from outside" below; the roll call
counts the points and each description lists them. Protocol 3 added the
`shape` message, saying how the map is built -- its levels, and where each
output sits; protocol 2, the map's name in every address and the roll call;
protocol 1 (the first 3B-1) sent everything under `/manifold` alone. A
receiver written for 2 or 3 reads 4 unchanged: it ignores what it does not
know, and the roll call's new last argument with it.

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
| `/manifold/map` | `s` prefix, `i` answer port, `i` N outputs, `i` protocol (4), `i` points | just before every `outputs` message (below): the first send, every change, when asked, every 2 seconds |

The roll call is always on the root, whatever the map's name, so a receiver
listens to one address to find every map playing: what it is called, where
it hears answers (0: nowhere), and how many outputs it has. A map that has
not answered the roll call for a few seconds (example's receivers: 5) has
stopped.

## Messages out, per map

| Address | Arguments | When |
|---|---|---|
| `/manifold/room/outputs` | `i` N, then N pairs of `s` name, `i` channel | with the roll call: the first send; whenever an output is added or removed or its channel changes; when asked; every 2 seconds |
| `/manifold/room/shape` | `i` L, then L × `f` level heights; `i` N, then N × (`s` name, `i` channel, `i` level, `i` levels, `i` placed, `f` x, `f` y, `f` z) | right after every `outputs` message, and on its own whenever it changes -- a level moved, a node moved or raised, a binding |
| `/manifold/room/points` | `i` P, then P × (`i` number, `s` name, `s` inlets) | after the shape on every description, and on its own when it changes: the points a sender may move, their names ("" if none) and the inlets each takes, as words -- `xy z u xyz` |
| `/manifold/room/levels` | N × `f` | every send, if packed is on (the default) |
| `/manifold/room/out/NAME` | `f` | per output, if that is on: what changed, at every send; all of them every 0.1 s |
| `/manifold/room/values` | `i` M, then M pairs of `s` name, `f` value | every send, if packed is on and the map has values |
| `/manifold/room/value/NAME` | `f` | per value, as per output |
| `/manifold/room/position` | `f` x, `f` y, `f` z, and optionally `f` u | every send |

- **Levels are in the order `outputs` lists the outputs**, which is channel
  order. The channel numbers say where each would go on a device; they need
  not be 0, 1, 2 -- a gap is a channel nothing feeds.
- **A level is a gain**, as the map routes it at the point, after the
  output's trim -- what the editor's faders show. Outside the map every level
  is 0. The sender smooths it (below).
- **x and y are on the map, 0 to 1**, y up. **z is the height** in the map's
  own units -- its levels' heights. **u, when sent, is where between the
  levels**: 0 on the bottom level's surface at the point, 1 on the next, 1.5
  half way between the second and third -- measured against the levels as
  they bend there. Past 0 or the top level, the point is outside: silent.
  A receiver reading three numbers is unaffected.
- **The shape says what the map already knows, and nothing musical.**
  The **level heights** are where each level starts, lowest first. Then,
  for each output in the `outputs` order: its **level**, the lowest level
  whose nodes feed it (-1: none does); **levels**, how many levels feed it
  (1 for a level's own output, more for one shared across levels, 0 for
  none); and **placed**, whether it has an owner node -- the node of its
  name, or the one it is named after (FL for FL-2). If it has, **x y** are
  that node's place on the map and **z** its height on that level; if not,
  all 0. A map with one level has L = 1. What to do with any of it -- chords
  by level, panning by place -- is the receiver's to decide.
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

## Points moved from outside

Also to the map's answer port. A sender takes a point, moves it, and lets it
go; while it holds it, the point's own movement -- a walk, a recording --
stops, and the player's hand, by a click on the map, can always take it
back. The rules are the SuperCollider Audio Engine's for OSC coming in:
paths fixed by the map, never chosen by a sender; exact argument counts;
finite numbers; senders on this machine only; every refusal counted, and the
last one's reason shown on the player's panel.

| Address | Arguments | Effect |
|---|---|---|
| `/manifold/room/point/1/take` | `i` request id, `i` reply port | the point is yours until you release it, or fall silent (5 seconds unless the control file says otherwise); answered. Reply port 0: your own port |
| `/manifold/room/point/1/release` | `i` request id | back to what moved it before; answered |
| `/manifold/room/point/1/xy` | `f` x, `f` y | where on the floor plan: refused if off it, the point staying |
| `/manifold/room/point/1/z` | `f` z | its height, held to the levels |
| `/manifold/room/point/1/u` | `f` u | its level position, held to the levels |
| `/manifold/room/point/1/xyz` | `f` x, `f` y, `f` z | both at once |

Moves are taken only from the sender holding the point, and are not
answered: send them as often as you like. Request ids are positive int32s;
send the same id again and you get the same answer again -- nothing is done
twice -- so a lost reply can simply be asked again.

| Answer, to your IP at the reply port | Arguments |
|---|---|
| `/manifold/room/point/1/reply` | `i` request id, `i` accepted (1 or 0), `s` command, `s` reason ("" when accepted; else e.g. `held by 127.0.0.1:57120`, `not held`, `not the holder`, `wrong arguments`) |
| `/manifold/room/point/1/lost` | `s` reason: `hand` (taken back on the player) or `silent` |

**The control file.** Nothing to set up: every map has point 1, taking
every inlet. A map may have a control file beside it, `room-control.json`
next to `room.json` -- the player writes it with `C` -- which narrows what a
point takes, and never widens it:

```json
{
  "control": 1,
  "points": [
    { "number": 1, "name": "lead", "inlets": ["xy", "z"],
      "range": { "x": [0, 127], "y": [0, 127] }, "silence": 5 }
  ]
}
```

`name` reaches the point as `/point/lead/xy` as well as `/point/1/xy`;
`inlets` are those it takes moves on (`take` and `release` are always open);
`range` is what a controller sends on an axis, mapped onto the map -- x and
y onto 0..1, z onto the lowest to highest level, u onto 0 to the top level;
`silence`, the seconds before a silent hold ends, 0 for never. A file it
cannot read, it refuses, saying why, and keeps the defaults. This version of
example-osc has one point; more are to come.

## A clock from SuperCollider (example-osc)

Not part of the protocol above, and nothing waits for it: example-osc also
listens on port 9001 (`kBridgePort` in its `ofApp.h`) for the contract of
ofxScPhaseBridge (github.com/leeMeredith/ofxScPhaseBridge), so its
`bridge.scd` drives movement unchanged. The port is not shared: the
SuperCollider Audio Engine's panel listens on 9001 too, and whichever starts
second hears no bridge -- the panel says "port 9001 taken". To run both,
set `kBridgePort` to another port and send the bridge there.

| Address | Arguments | Effect |
|---|---|---|
| `/phase` | `f` 0..1 across a bar, optional `i` bar | the clock: bars counted as the phase wraps (`PhaseClock`); with the bar, the bar itself |
| `/beat` | `i` step | with steps on the bridge's beats, one step of a walk |

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
4. On its `shape`, if it differs from the last, retune what you built --
   no need to rebuild: the outputs are the same ones, in the same order.
   No `shape` at all: an older sender; place things by order.
5. On its `levels`, set each from its argument, in order. Smoothing over
   about 20 ms hides the steps between sends. The sender smooths jumps
   itself -- a click across the map, the point leaving it -- so a receiver
   needs no more than that (`OutputSmoother`, half-life 0.05 s in
   example-osc).
6. Optionally say the map's `hello` to its answer port once a second.

The SuperCollider patch beside `example-osc` is exactly this, and decides
what the shape sounds like in settings at its top: `\progression`,
`\somber`, `\registers` or `\series`, brightness, the span.
