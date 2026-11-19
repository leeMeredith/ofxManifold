// ofxManifold -- example-osc's sounds, to choose from (round 3B-2h).
//
// HOW TO USE THIS FILE
//
// Each choice below is a short block between parentheses. Put the cursor
// anywhere inside a block and press cmd-return: that choice is made at once,
// while it plays, and the post window says what you chose and where things
// now stand.
//
//   1. Run START (just below). It starts the patch, manifold-receive.scd,
//      beside this file -- you never need to open that one.
//   2. Start example-osc. Move the point around: you hear the map.
//   3. Pick one block from each group -- NOTES, SHARED OUTPUTS, SOUND,
//      POINTS. A choice changes only its own group, so they combine: SOMBER
//      with NOISE BANDS, REGISTERS with CARRIER, and so on.
//   4. RESET puts back the patch's own settings. STOP is cmd-period.
//
// Choices last until the patch is started again (START after STOP). To make
// one the patch's own, change the matching line at the top of
// manifold-receive.scd.
//
// In example-osc, these are the moves that matter here:
//   drag the point       move across the map: which outputs sound, how loud
//   hold up / down       climb through the levels (the room has three)
//   a                    add a point at the mouse (A removes the last)
//   left / right         choose which point the keys move
//   m                    let the chosen point move on its own (m again: glide)


// ==== START AND STOP ========================================================

// START -- starts the patch. Run it once. If it is already playing, it says
// so and changes nothing.
(
var here = thisProcess.nowExecutingPath;
case
{ here.isNil } {
    "manifold: save this file beside manifold-receive.scd, then START again".warn;
}
{ ~manifold.notNil and: { ~manifold[\playing].notNil } and: { ~manifold[\playing].() } } {
    "manifold: already playing -- choose below".postln;
}
{ (here.dirname +/+ "manifold-receive.scd").load };
)

// STOP -- the same as cmd-period: silence. START plays again, with the
// patch's own settings.
(
CmdPeriod.run;
"manifold: stopped -- START to play again".postln;
)

// RESET -- every group back to the patch's own settings.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("reset: the patch's own settings", ~manifold[\defaults]);
};
)


// ==== NOTES: what each output plays ========================================
// Each level of the map is heard as a chord; its outputs are the chord's
// notes. These choose the chords.

// PROGRESSION (the patch's own) -- a major key. Each level is the next
// chord: I, IV, V, vi, ii, iii. All the chords sit in the same register, so
// climbing changes the harmony, not how high it is.
// Expect: bright and settled. Hold up to step through the chords.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("notes: progression", (music: \progression, fit: true, span: 12));
};
)

// SOMBER -- the same, in a minor key: i, iv, VI, v, III, VII.
// Expect: darker and more serious; the same movement as PROGRESSION.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("notes: somber", (music: \somber, fit: true, span: 12));
};
)

// REGISTERS, CLOSE -- every level plays the same wide chord (1 3 5 7 9 #11
// 13), each level higher than the one below; all the levels share one
// octave.
// Expect: climbing is heard as rising, gently. Same chord, new height.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("notes: registers, sharing an octave", (music: \registers, fit: true, span: 12));
};
)

// REGISTERS, WIDE -- as CLOSE, the levels sharing two octaves.
// Expect: a bigger climb from bottom to top.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("notes: registers, sharing two octaves", (music: \registers, fit: true, span: 24));
};
)

// REGISTERS, AN OCTAVE EACH -- each level a whole octave above the last.
// Expect: the steepest climb; with three or four levels the top is very
// high.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("notes: registers, an octave each", (music: \registers, fit: false, span: 12));
};
)

// SERIES -- the overtones of one low note: each level the next overtone up,
// its outputs the notes between it and the next.
// Expect: open and wide at the bottom, closer and stranger at the top --
// like a horn's natural notes.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("notes: series", (music: \series, fit: true, span: 12));
};
)


// ==== SHARED OUTPUTS: how loud an output no single level owns ===============
// An output fed by two levels at once, or by none -- the octahedron's rim
// (x in example-osc) -- plays a soft root note instead of a chord note.

// SHARED SOFT (the patch's own) -- under the chord, not lost.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("shared outputs soft", (quiet: 0.4));
};
)

// SHARED FAINT -- barely there.
// Expect: the chords clearer; crossing a shared output almost silent.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("shared outputs faint", (quiet: 0.1));
};
)

// SHARED FULL -- as loud as any other output.
// Expect: a steady root under everything wherever the outputs are shared.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("shared outputs full", (quiet: 1.0));
};
)


// ==== SOUND: what a voice is ================================================

// TONES (the patch's own) -- a soft tone per output, opening up as the point
// climbs.
// Expect: duller at the bottom level, brighter at the top.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("sound: tones, opening with height", (sound: \tone, bright: true));
};
)

// TONES, EVEN -- the same tones, always fully open.
// Expect: only the notes and how loud they are change; height is heard in
// the notes alone.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("sound: tones, always open", (sound: \tone, bright: false));
};
)

// NOISE BANDS -- a band of noise per output, pitched by its note, in place
// of a tone.
// Expect: wind and breath; the map heard as texture more than harmony. The
// POINTS group's shaping does nothing here -- noise has nothing to shape.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("sound: noise bands", (sound: \noise, bright: true));
};
)


// ==== POINTS: what a second point does ======================================
// Press a in example-osc, with the mouse over the map, for a second point
// (a again for a third). Left / right choose which one you move.

// TOGETHER (the patch's own) -- every point plays the same voices, louder
// where they meet.
// Expect: two hands on one instrument. Where both points hear an output, it
// is as loud as both together.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("points: together", (points: \together, players: 1, carrier: \fm, depth: 3));
};
)

// CARRIER, GENTLE -- point 1 plays; point 2 makes no sound of its own but
// colours point 1's notes by frequency modulation, wherever it is near them.
// Expect: notes turning brighter and glassier where point 2 sits. Move
// point 2 up a level (right to choose it, then up) and the colour changes
// character while the notes stay in tune.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("points: carrier, gentle FM", (points: \carrier, players: 1, carrier: \fm, depth: 1));
};
)

// CARRIER, DEEP -- the same, much stronger.
// Expect: bells, then grit, where point 2 is strongest.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("points: carrier, deep FM", (points: \carrier, players: 1, carrier: \fm, depth: 6));
};
)

// RING -- point 2 mixes ring modulation into point 1's notes.
// Expect: metallic, out-of-tune edges where point 2 is near; clean where
// it is far.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("points: carrier, ring modulation", (points: \carrier, players: 1, carrier: \ring, depth: 3));
};
)

// TWO PLAY, THE REST SHAPE -- points 1 and 2 both play; point 3 and up
// colour them by FM. Needs three points (a, a).
// Expect: two hands playing, a third changing their colour.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("points: two play, the rest shape", (points: \carrier, players: 2, carrier: \fm, depth: 3));
};
)
