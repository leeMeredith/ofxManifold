# ofxManifold OSC out

What an ofxManifold player sends, and what it listens for. Written for anyone
building a receiver: in SuperCollider, Max, Pd, TouchDesigner, a lighting
desk, a script. `example-osc` sends it; `example-osc/supercollider/` and
`example-osc/receive.py` are two receivers to start from.

The idea in one line: **the receiver does not need to know how many outputs
there are.** The sender tells it, and tells it again when that changes, so
the messages grow and shrink with the map.

## Messages out

All under a prefix, `/manifold` unless it is changed (two players side by
side would use two).

| Address | Arguments | When |
|---|---|---|
| `/manifold/outputs` | `i` N, then N pairs of `s` name, `i` channel | the first send; whenever an output is added or removed or its channel changes; when asked; every 2 seconds |
| `/manifold/levels` | N × `f` | every send, if packed is on (the default) |
| `/manifold/out/NAME` | `f` | per output, if that is on: what changed, at every send; all of them every 0.1 s |
| `/manifold/values` | `i` M, then M pairs of `s` name, `f` value | every send, if packed is on and the map has values |
| `/manifold/value/NAME` | `f` | per value, as per output |
| `/manifold/position` | `f` x, `f` y, `f` z | every send |

- **Levels are in the order `/manifold/outputs` lists the outputs**, which is
  channel order. The channel numbers say where each would go on a device;
  they need not be 0, 1, 2 -- a gap is a channel nothing feeds.
- **A level is a gain**, as the map routes it at the point, after the
  output's trim -- what the editor's faders show. Outside the map every level
  is 0.
- **x and y are on the map, 0 to 1**, y up. **z is the height** in the map's
  own units -- its levels' heights.
- **Per-output addresses use the output's name** with any of ` #*,/?[]{}`
  replaced by `_`, since those cannot stand in an address. The `outputs`
  message has the name as written.
- **Sends happen at a rate, 60 a second by default** (settable from 1 to
  1000), never twice in one period; a frame that comes late sends once, then
  the rate carries on from it.

## Messages in

Optional. A sender sends everything whether anyone answers or not.

| Address | Arguments | Effect |
|---|---|---|
| `/manifold/describe` | none | the next send includes `/manifold/outputs` |
| `/manifold/hello` | `s` name, optional | the sender shows the receiver as connected, by that name, for 3 seconds; say it once a second to stay connected |

`example-osc` sends to port 57120 (sclang's) and listens on 57121; both are
set at the top of its `ofApp.h`.

## On the wire

OSC 1.0: one message per UDP packet, no bundles, arguments `i` (32-bit int),
`f` (32-bit float) and `s` (string) only -- what SuperCollider, Max and Pd
all read without a bundle parser. Every message is checked byte for byte
against an independent OSC library (python-osc) in the tests.

## A receiver in outline

1. Ask: send `/manifold/describe`.
2. On `/manifold/outputs`, if the list differs from the one you have, rebuild:
   one voice, light, parameter or channel per output.
3. On `/manifold/levels`, set each from its argument, in order. Smoothing over
   about 20 ms hides the steps between sends. The sender smooths jumps
   itself -- a click across the map, the point leaving it -- so a receiver
   needs no more than that (`OutputSmoother`, half-life 0.05 s in
   example-osc).
4. Optionally say `/manifold/hello` once a second.

The SuperCollider patch beside `example-osc` is exactly this, in fifty
lines.
