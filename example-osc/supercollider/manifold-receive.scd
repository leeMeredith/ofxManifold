// ofxManifold -- example-osc's receiver for SuperCollider (round 3B-2b,
// protocol 5: several maps at once, each telling its shape; several points
// on a map, each its own levels -- round 3B-2g).
//
// Run the whole block: put the cursor inside the outer parentheses and press
// cmd-return. Then start example-osc -- one copy, or several with different
// maps. Headphones are fine.
//
// It listens for the roll call, /manifold/map, and starts a map for each
// prefix it hears (/manifold/room, /manifold/forest ...): one voice per
// output, built again when the outputs change. A voice's volume follows its
// level with a 20 ms lag, so sixty updates a second sound smooth.
//
// WHAT IT SOUNDS LIKE IS DECIDED HERE, not in openFrameworks. The map tells
// its shape (/manifold/<map>/shape): how many levels, each output's level,
// and where its node is. The settings just below choose what to make of it:
//
//   ~manifold[\music] = \progression each level a chord in one key -- I, IV,
//                                    V, vi, ii, iii -- each of its outputs a
//                                    chord note: 3 a triad, 4 a seventh, on
//                                    up the scale in thirds. Every chord sits
//                                    in ONE register, the octave above the
//                                    root, as close together as it goes: going
//                                    up the levels changes the harmony, not
//                                    how high it is. More than seven outputs
//                                    on a level: the rest an octave up.
//   ~manifold[\music] = \somber      the same in a minor key: i, iv, VI, v,
//                                    III, VII.
//   ~manifold[\music] = \registers   each level the same chord in its own
//                                    register, rising: 1 3 5 7 9 #11 13.
//                                    ~manifold[\fit] = true: the levels share
//                                    ~manifold[\span] semitones -- more levels
//                                    sit closer; false: an octave each.
//   ~manifold[\music] = \series      each level the next overtone over one
//                                    root, its outputs the notes within it:
//                                    wide at the bottom, close at the top.
//   ~manifold[\bright] = true        the point's height opens each voice.
//
// A map may have several points (example-osc's a key). What they do is
// decided here too:
//
//   ~manifold[\points] = \together   every point plays the same voices: an
//                                    output as loud as the points there
//                                    together, held to 1 -- one instrument,
//                                    two hands; its brightness the loudest
//                                    point's there.
//   ~manifold[\points] = \carrier    the first ~manifold[\players] points
//                                    play, as \together; every point after
//                                    them makes no sound of its own but shapes
//                                    theirs: its level at an output is that
//                                    voice's modulation, ~manifold[\depth] at
//                                    full, held there. The modulator's ratio
//                                    is the level the loudest shaping point
//                                    there is on: 1, 2, 3 -- harmonic, so going
//                                    up a level changes the character, not the
//                                    tuning.
//   ~manifold[\carrier] = \fm        frequency modulation: pure to bright to
//                                    bell-like. \ring: ring modulation, the
//                                    bolder, metallic one, mixed in by the
//                                    same amount.
//
// With one point, every setting sounds as before.
//
// The room (round 3B-3). Every placed output is a speaker in a room, at its
// node's place on the floor plan and its level's height, and you listen on
// headphones from the middle of it:
//
//   ~manifold[\room] = \head          the native room: each speaker heard
//                                    through a model of a head -- the far ear
//                                    later, duller and quieter; height as a
//                                    notch that rises with it; distance as
//                                    level and air -- and one shared reverb.
//                                    Core SuperCollider only.
//   ~manifold[\room] = \atk           the Ambisonic Toolkit's room (3B-3b):
//                                    each speaker encoded at its direction,
//                                    heard through measured ears -- front and
//                                    back, up and down, far clearer -- with
//                                    the same distance and reverb. Needs the
//                                    ATK installed (manifold-modes.scd says
//                                    how); without it, \head, and a warning.
//   ~manifold[\ears] = 21              \atk: whose ears -- a CIPIC subject;
//                                    21 is the KEMAR dummy head.
//   ~manifold[\room] = \stereo        panned left to right by place, as
//                                    before the room.
//   ~manifold[\facing] = 0             the way the listener faces, in degrees
//                                    turned to the right of the map's front
//                                    (its top).
//   ~manifold[\reverb] = 0.3           how much room: 0 dry.
//   ~manifold[\roomSize] = [8, 8, 4]   width, depth and height, in metres --
//                                    the levels spread from floor to ceiling,
//                                    the listener's ears halfway up.
//
// An output with no place on the map is heard in the middle of the head. An
// output fed by no level, or shared by several, is not any level's chord: a
// quiet tone at the root (~manifold[\quiet]). A map that sends no shape (an
// older example-osc) climbs a pentatonic scale in channel order, as before.
// Each map starts a fifth higher than the last, so maps can be told apart.
// To try the settings while it plays, without reading this file, open
// manifold-modes.scd beside it: its blocks start this patch, then choose the
// notes, the sound and what several points do, each explained.
//
// Change a setting and run the block again. ~manifold[\sound] = \noise gives
// the first version's pink noise bands instead of tones. The post window
// lists each output's note.
//
// It says hello to each map once a second, at the port its roll call gives,
// so example-osc shows it answered; it asks every map to describe itself when
// it starts; and it lets a map go -- voices freed -- when its roll call has
// been silent for five seconds.
//
// cmd-period stops the sound and the hellos; run the block again to restart.

(
~manifold = ~manifold ? ();
~manifold[\root] = "/manifold";
~manifold[\firstPort] = 57121;    // where example-osc copies may hear answers
~manifold[\ports] = 16;
~manifold[\silentAfter] = 5;      // seconds without a roll call: the map has gone
~manifold[\sound] = \tone;        // \tone: a note per output; \noise: a noise band
~manifold[\music] = \progression; // \progression, \somber, \registers or \series
~manifold[\span] = 12;            // \registers, fit: the semitones the levels share
~manifold[\fit] = true;           // \registers: share the span (true) or an octave each
~manifold[\bright] = true;        // the point's height opens each voice
~manifold[\quiet] = 0.4;          // a shared or unfed output's voice, against 1
~manifold[\scale] = [0, 2, 4, 7, 9];  // no shape: major pentatonic, in semitones
~manifold[\lowest] = 48;          // the first map's root: C3, as MIDI
~manifold[\points] = \together;   // several points: \together, or \carrier
~manifold[\players] = 1;          // \carrier: how many points play; the rest shape them
~manifold[\carrier] = \fm;        // \carrier: \fm or \ring
~manifold[\depth] = 3;            // \carrier: the modulation at a shaping point's full level
~manifold[\maxPoints] = 16;       // the most points a map has (example-osc's kMaxPoints)
~manifold[\room] = \head;         // the room: \head (native), \atk (the ATK), or \stereo
~manifold[\ears] = 21;            // \atk: the CIPIC subject whose ears -- 21, KEMAR
~manifold[\facing] = 0;           // the listener turned to the right, degrees
~manifold[\reverb] = 0.3;         // how much room: 0 dry
~manifold[\roomSize] = [8, 8, 4]; // width, depth, height in metres
~manifold[\atkTrim] = 1;          // \atk: its level against the head's

// ---- choosing while it plays (round 3B-2h) ---------------------------------
// manifold-modes.scd, beside this file, is the easy way in: blocks to pick
// the notes, the sound and what several points do, each explained. They all
// come here. The settings above, as this run starts with them, are what its
// reset puts back.
~manifold[\defaults] = (music: ~manifold[\music], span: ~manifold[\span],
    fit: ~manifold[\fit], quiet: ~manifold[\quiet], sound: ~manifold[\sound],
    bright: ~manifold[\bright], points: ~manifold[\points],
    players: ~manifold[\players], carrier: ~manifold[\carrier],
    depth: ~manifold[\depth], room: ~manifold[\room], facing: ~manifold[\facing],
    reverb: ~manifold[\reverb]);

// Where things stand, in a line for the post window.
~manifold[\summary] = {
    var notes = ~manifold[\music].asString ++ if (~manifold[\music] == \registers) {
        if (~manifold[\fit]) { " sharing % semitones".format(~manifold[\span]) }
                            { " an octave apart" }
    } { "" };
    var sound = if (~manifold[\sound] == \noise) { "noise bands" } {
        if (~manifold[\bright]) { "tones, opening with height" } { "tones, height not heard" }
    };
    var points = if (~manifold[\points] == \carrier) {
        "carrier: %%, the rest shape them by %, depth %".format(
            ~manifold[\players], if (~manifold[\players] == 1) { " point plays" } { " points play" },
            if (~manifold[\carrier] == \ring) { "ring modulation" } { "FM" },
            ~manifold[\depth])
    } { "together: every point plays" };
    var room = if (~manifold[\room] == \stereo) { "stereo" } {
        "% in the room, facing %, reverb %".format(
            if (~manifold[\room] == \atk) { "the ATK's measured ears (CIPIC %)".format(~manifold[\ears]) }
                                          { "a head" },
            if (~manifold[\facing] == 0) { "the front" } { "% degrees right".format(~manifold[\facing]) },
            ~manifold[\reverb])
    };
    "notes %; sound %; points %; room %".format(notes, sound, points, room);
};

// Choose: the settings given, each by its name above, and the voices
// retuned -- or built again, for another sound -- on every map playing. The
// points' settings need neither: they are read every frame. `what` is said
// in the post window, with where things now stand.
~manifold[\choose] = { |what, settings|
    var known = ~manifold[\defaults].keys, build, tune, speak, place;
    // The ATK's room without the ATK: the native one, and how to get it.
    if (settings[\room] == \atk and: { (~manifold[\atkReady] ? false).not }) {
        ~manifold[\atkMissing].();
        settings = settings.copy.put(\room, \head);
    };
    build = settings[\sound].notNil and: { settings[\sound] != ~manifold[\sound] };
    tune = [\music, \span, \fit, \quiet, \bright].any { |k| settings[k].notNil };
    speak = settings[\room].notNil and: { settings[\room] != ~manifold[\room] };
    place = settings[\facing].notNil;
    settings.keysValuesDo { |k, v|
        if (known.includes(k)) { ~manifold[k] = v } {
            "manifold: no setting called % -- left alone".format(k).warn;
        };
    };
    (~manifold[\maps] ? []).do { |m|
        if (m[\names].notNil) {
            case
            { build } { ~manifold[\build].(m) }
            { speak } { ~manifold[\speak].(m); if (tune) { ~manifold[\tune].(m) } }
            { tune } { ~manifold[\tune].(m) }
            { place } { ~manifold[\place].(m) };
        };
    };
    if (~manifold[\roomSynth].notNil) { ~manifold[\roomSynth].set(\amount, ~manifold[\reverb]) };
    "manifold: % -- %".format(what, ~manifold[\summary].()).postln;
};

// Playing: started, and not stopped by cmd-period since -- the modes file's
// START asks, so as not to start it twice.
~manifold[\stopped] = false;
CmdPeriod.doOnce { ~manifold[\stopped] = true };
~manifold[\playing] = { ~manifold[\stopped].not };

// ---- music: what each output sounds, from the map's shape -----------------
// Plain functions, no server: tests/sc/check_music.scd runs them in sclang
// and checks them against tests/ref/reference_music.py (make sc).

// The chord tones above a chord's root, for \registers: 1 3 5 7 9 #11 13,
// then the same two octaves up.
~manifold[\stack] = [0, 4, 7, 11, 14, 18, 21];
~manifold[\major] = [0, 2, 4, 5, 7, 9, 11];
~manifold[\degrees] = [0, 3, 4, 5, 1, 2];     // I IV V vi ii iii
~manifold[\minor] = [0, 2, 3, 5, 7, 8, 10];   // natural minor
~manifold[\somberDegrees] = [0, 3, 5, 4, 2, 6];  // i iv VI v III VII

// The i-th chord tone of the stack, i from 0.
~manifold[\stackTone] = { |i|
    var st = ~manifold[\stack];
    st[i % st.size] + (24 * (i div: st.size));
};
// A progression's chord note, as semitones above the root: the level's
// chord, `at` thirds up the scale from its degree, brought into the octave
// above the root -- every chord in one register; the eighth note on, an
// octave higher.
~manifold[\chordNote] = { |scale, degrees, k, at|
    var d = degrees[k % degrees.size] + (2 * at);
    scale[d % scale.size] + (12 * (at div: scale.size));
};

// Every output's voice, from the shape and the map's root (MIDI): an Array,
// in the outputs' order, of (freq:, pan:, gain:). `shape` is (heights: [...],
// outputs: [(name:, level:, levels:, placed:, x:, y:, z:) ...]), or nil --
// then the climb, by order.
~manifold[\voicing] = { |shape, lowest, count|
    var outs, n, levels, members, panOf;
    if (shape.isNil) {
        count.collect { |i|
            var steps = ~manifold[\scale].size;
            var note = lowest + ~manifold[\scale][i % steps] + (12 * (i div: steps));
            (freq: note.midicps,
             pan: if (count > 1) { i.linlin(0, count - 1, -0.9, 0.9) } { 0 },
             gain: 1)
        };
    } {
        outs = shape[\outputs];
        n = outs.size;
        levels = shape[\heights].size.max(1);
        // Each level's own outputs, in order: fed by that level alone.
        members = Array.fill(levels, { List.new });
        outs.do { |o, i|
            if (o[\levels] == 1 and: { o[\level] >= 0 } and: { o[\level] < levels }) {
                members[o[\level]].add(i);
            };
        };
        panOf = { |o, i|
            if (o[\placed]) { o[\x].linlin(0, 1, -0.9, 0.9) } {
                if (n > 1) { i.linlin(0, n - 1, -0.9, 0.9) } { 0 }
            };
        };
        outs.collect { |o, i|
            var k = o[\level], at, freq, gain = 1, root;
            if (o[\levels] == 1 and: { k >= 0 } and: { k < levels }) {
                at = members[k].indexOf(i);
                freq = switch (~manifold[\music])
                { \progression } {
                    (lowest + ~manifold[\chordNote].(~manifold[\major],
                        ~manifold[\degrees], k, at)).midicps
                }
                { \somber } {
                    (lowest + ~manifold[\chordNote].(~manifold[\minor],
                        ~manifold[\somberDegrees], k, at)).midicps
                }
                { \series } {
                    lowest.midicps * ((k + 1) + (at / members[k].size))
                }
                { \registers } {
                    root = if (~manifold[\fit]) {
                        lowest + (~manifold[\span] * k div: levels)
                    } { lowest + (12 * k) };
                    (root + ~manifold[\stackTone].(at)).midicps
                }
                {   // anything unknown: \progression
                    (lowest + ~manifold[\chordNote].(~manifold[\major],
                        ~manifold[\degrees], k, at)).midicps
                };
            } {
                freq = lowest.midicps;      // shared, or fed by none
                gain = ~manifold[\quiet];
            };
            (freq: freq, pan: panOf.(o, i), gain: gain)
        };
    };
};

// How bright, 0 to 1, from /position x y z [u] and the shape's heights: by
// the level position u where sent, else by z between the lowest and highest
// level; half way when there is nothing to go by.
~manifold[\brightness] = { |shape, z, u|
    var hs = if (shape.notNil) { shape[\heights] } { [] };
    case
    { u.notNil and: { hs.size > 1 } } { (u / (hs.size - 1)).clip(0, 1) }
    { hs.size > 1 and: { hs.last > hs.first } } { z.linlin(hs.first, hs.last, 0, 1) }
    { 0.5 };
};

// The level a point is on, from its level position u, as its modulator's
// ratio: 1 on the first level, 2 on the second ... nil (no u sent): 1.
~manifold[\ratioFor] = { |u| if (u.isNil) { 1 } { u.max(0).floor + 1 } };

// What every voice does, from every point (round 3B-2g): `pts` an Array of
// (number:, levels: [...], bright:, u:), any order; `n` outputs. An Array of
// n Events (amp:, bright:, fm:, ring:, ratio:) -- \together every point
// playing; \carrier the first ~manifold[\players] by number playing, the
// rest shaping.
~manifold[\mix] = { |pts, n|
    var sorted = pts.copy.sort { |a, b| a[\number] < b[\number] };
    var carrier = ~manifold[\points] == \carrier;
    var count = if (carrier) { ~manifold[\players].max(1) } { sorted.size };
    var players = sorted.keep(count), shapers = sorted.drop(count);
    var depth = ~manifold[\depth], ring = ~manifold[\carrier] == \ring;
    n.collect { |i|
        var at = { |p| p[\levels][i] ? 0 };
        var amp = players.sum(at).min(1);
        var loud = players.maxItem(at);
        var shaped = shapers.sum(at);
        var lead = shapers.maxItem(at);
        var amount = (shaped * depth).min(depth);
        (amp: amp,
         bright: if (loud.notNil) { loud[\bright] } { 0.5 },
         fm: if (ring) { 0 } { amount },
         ring: if (ring and: { depth > 0 }) { amount / depth } { 0 },
         ratio: if (lead.notNil and: { shaped > 0 }) { ~manifold[\ratioFor].(lead[\u]) } { 1 })
    };
};

// /manifold/<map>/shape as sent: i L, L heights, i N, then per output
// s name, i channel, i level, i levels, i placed, f x, f y, f z.
~manifold[\readShape] = { |msg|
    var at = 1, nl = msg[at], heights, no, outs;
    heights = msg[(at + 1)..(at + nl)];
    at = at + nl + 1;
    no = msg[at];
    at = at + 1;
    outs = no.collect { |i|
        var b = at + (8 * i);
        (name: msg[b].asString, channel: msg[b + 1], level: msg[b + 2],
         levels: msg[b + 3], placed: msg[b + 4] == 1,
         x: msg[b + 5], y: msg[b + 6], z: msg[b + 7])
    };
    (heights: heights, outputs: outs)
};
// ---- end of music ----------------------------------------------------------

// ---- room: where each output is heard from (round 3B-3) --------------------
// Plain functions, checked against tests/ref/reference_room.py (make sc).

// A speaker's place from the listener, in metres -- (right:, ahead:, up:,
// inside:) -- from its output in the shape: x across the room's width, y its
// depth (the map's top the front), its height between the lowest level (the
// floor) and the highest (the ceiling); the ears halfway up. One level: all
// at the ears' height. No place on the map: inside the head.
~manifold[\speakerAt] = { |o, heights, size|
    var lo, hi;
    if (o.isNil or: { o[\placed].not }) {
        (right: 0, ahead: 0, up: 0, inside: true)
    } {
        lo = heights.minItem ? 0;
        hi = heights.maxItem ? 0;
        (right: (o[\x] - 0.5) * size[0], ahead: (o[\y] - 0.5) * size[1],
         up: if (hi > lo) { ((o[\z] - lo) / (hi - lo) - 0.5) * size[2] } { 0 },
         inside: false)
    };
};

// How a listener turned `facing` degrees right hears a speaker there,
// through a head 17.5 cm across: its direction and distance, and what each
// ear gets -- delay (seconds), gain, a high shelf (dB, at 2.5 kHz: the far
// ear in the head's shadow, both ears for a sound behind), the notch the
// outer ear makes (Hz, rising with elevation), the air (a low-pass, Hz),
// and how much goes to the room's reverb.
~manifold[\hear] = { |at, facing|
    var flat, dist, az, el, lat, itd, far, shade, behind, gain;
    if (at[\inside]) {
        az = 0; el = 0; dist = 1;
    } {
        flat = at[\right].hypot(at[\ahead]);
        dist = (flat.squared + at[\up].squared).sqrt;
        az = if (flat > 0) { at[\right].atan2(at[\ahead]) } { 0 };
        az = (az - facing.degrad).wrap(-pi, pi);
        el = if (dist > 0) { at[\up].atan2(flat) } { 0 };
    };
    dist = dist.max(0.3);
    lat = (az.sin * el.cos).asin;                    // towards the right ear
    itd = 0.0875 / 343 * (lat.abs + lat.abs.sin);    // Woodworth
    far = (-6 * lat.abs.sin).dbamp;
    shade = -12 * lat.abs.sin;
    behind = -5 * (az.cos * el.cos).neg.max(0);
    gain = (2 / dist).min(1) * 0.5.sqrt;
    (az: az, el: el, dist: dist, gain: gain,
     delayL: if (lat > 0) { itd } { 0 }, delayR: if (lat < 0) { itd } { 0 },
     gainL: gain * (if (lat > 0) { far } { 1 }), gainR: gain * (if (lat < 0) { far } { 1 }),
     shelfL: behind + (if (lat > 0) { shade } { 0 }),
     shelfR: behind + (if (lat < 0) { shade } { 0 }),
     notch: 6000 * (2 ** (0.75 * el / 0.5pi)),
     air: 20000 * (0.93 ** dist),
     wet: dist / (dist + 3))
};
// ---- end of room -----------------------------------------------------------

// The ATK (3B-3b): there if its classes are, and its kernels; said once how
// to get it when it is asked for and is not.
~manifold[\atkThere] = {
    \Atk.asClass.notNil and: { \FoaDecoderKernel.asClass.notNil } and: {
        File.exists(\Atk.asClass.userKernelDir +/+ "FOA" +/+ "decoders" +/+ "cipic")
    }
};
~manifold[\atkMissing] = {
    ("manifold: the ATK's room needs the Ambisonic Toolkit -- staying with the head. "
     ++ "To install it, run once, then restart SuperCollider:\n"
     ++ "    Quarks.install(\"https://github.com/ambisonictoolkit/atk-sc3.git\");\n"
     ++ "and after the restart:  Atk.downloadKernels; Atk.downloadMatrices;\n"
     ++ "It also needs sc3-plugins: https://supercollider.github.io/sc3-plugins").warn;
};

s.waitForBoot {
    // Every voice plays into a bus of its own (3B-3); a speaker reads it and
    // places it in the room.
    SynthDef(\manifoldVoice, { |out = 0, amp = 0, gain = 1, centre = 440|
        var sig = BPF.ar(PinkNoise.ar, centre, 0.4) * 2;
        sig = sig * Lag.kr(amp * gain, 0.02);
        Out.ar(out, sig);
    }).add;
    // A soft tone: a sine and quiet overtones. `bright` 0 keeps little but
    // the sine; 1 lets every overtone through. A modulator at `ratio` times
    // the note (3B-2g): `fm` its index, `ring` how much ring modulation is
    // mixed in -- both 0 unless another point shapes this one.
    SynthDef(\manifoldTone, { |out = 0, amp = 0, gain = 1, freq = 220, bright = 1,
                               fm = 0, ring = 0, ratio = 1|
        var f = Lag.kr(freq, 0.05);
        var mf = f * Lag.kr(ratio, 0.05);
        var mod = SinOsc.ar(mf);
        var car = f + (mod * Lag.kr(fm, 0.05) * mf);
        var rm = Lag.kr(ring, 0.05);
        var sig = SinOsc.ar(car * [1, 2, 3, 4, 5], 0, [1, 0.25, 0.08, 0.05, 0.03]).sum;
        sig = LPF.ar(sig, (f * Lag.kr(bright, 0.05).linexp(0, 1, 1.2, 6)).clip(20, 20000));
        sig = (sig * (1 - rm)) + (sig * mod * rm);
        sig = sig * 0.3 * Lag.kr(amp * gain, 0.02);
        Out.ar(out, sig);
    }).add;
    // The speakers (3B-3): one per output, reading its voice's bus. Stereo:
    // panned, as before the room.
    SynthDef(\manifoldStereo, { |in, out = 0, pan = 0|
        Out.ar(out, Pan2.ar(In.ar(in, 1), Lag.kr(pan, 0.1)));
    }).add;
    // A head in the room: ~manifold[\hear]'s numbers, gliding as they change.
    SynthDef(\manifoldHead, { |in, out = 0, rev, delayL = 0, delayR = 0,
                               gainL = 0.707, gainR = 0.707, shelfL = 0, shelfR = 0,
                               notch = 6000, air = 20000, wet = 0.25|
        var sig = In.ar(in, 1), l, r, lag = 0.05;
        sig = LPF.ar(sig, Lag.kr(air, lag));
        sig = BPeakEQ.ar(sig, Lag.kr(notch, lag), 0.5, -12);
        l = DelayC.ar(sig, 0.002, Lag.kr(delayL, lag));
        r = DelayC.ar(sig, 0.002, Lag.kr(delayR, lag));
        l = BHiShelf.ar(l, 2500, 1, Lag.kr(shelfL, lag)) * Lag.kr(gainL, lag);
        r = BHiShelf.ar(r, 2500, 1, Lag.kr(shelfR, lag)) * Lag.kr(gainR, lag);
        Out.ar(out, [l, r]);
        Out.ar(rev, [l, r] * Lag.kr(wet, lag));
    }).add;
    // The room's reverb, one for every map: what the speakers send it.
    SynthDef(\manifoldRoom, { |in, out = 0, amount = 0.3|
        var sig = In.ar(in, 2);
        sig = FreeVerb2.ar(sig[0], sig[1], 1, 0.85, 0.4);
        Out.ar(out, sig * Lag.kr(amount, 0.1));
    }).add;
    s.sync;

    // The stage (3B-3): voices first, then the speakers, then the room.
    // Anything from a run before goes first.
    [\roomSynth, \atkDecoder, \speakersGroup, \voicesGroup, \roomBus, \atkBus,
     \atkKernel].do { |k| ~manifold[k] !? (_.free); ~manifold[k] = nil };
    ~manifold[\voicesGroup] = Group.head(s);
    ~manifold[\speakersGroup] = Group.after(~manifold[\voicesGroup]);
    ~manifold[\roomBus] = Bus.audio(s, 2);
    ~manifold[\roomSynth] = Synth.after(~manifold[\speakersGroup], \manifoldRoom,
        [in: ~manifold[\roomBus], amount: ~manifold[\reverb]]);

    // The ATK's room (3B-3b), if the ATK is there: the ears loaded, a
    // first-order sound field every speaker is encoded into, and one
    // decoder from it to the headphones, after the speakers. Its classes are
    // named by symbol, so this file runs where the ATK is not installed.
    ~manifold[\atkReady] = false;
    if (~manifold[\atkThere].()) {
        var panB = \FoaPanB.asClass, decode = \FoaDecode.asClass;
        ~manifold[\atkKernel] = \FoaDecoderKernel.asClass.newCIPIC(~manifold[\ears], s);
        s.sync;
        // A speaker in the ATK's room: the distance as the head has it (level,
        // air, the reverb's share); the direction encoded -- the ATK's
        // azimuth turns to the left, ours to the right. With no place: both
        // ears alike, in the middle of the head. What does not go through the
        // ears -- the middle of the head, the reverb's share -- waits as long
        // as the ears take (the decoder's convolution, a kernel long), so all
        // of it arrives together.
        ~manifold[\atkLatency] = ~manifold[\atkKernel].kernel[0][0].numFrames / s.sampleRate;
        SynthDef(\manifoldAtk, { |in, out = 0, foa, rev, az = 0, el = 0, gain = 0.707,
                                  air = 20000, wet = 0.25, inside = 0, latency = 0.0107|
            var sig = LPF.ar(In.ar(in, 1), Lag.kr(air, 0.05)) * Lag.kr(gain, 0.05);
            var mid = Lag.kr(inside, 0.05);
            var late = DelayN.ar(sig, 0.1, latency);
            Out.ar(foa, panB.ar(sig * (1 - mid), Lag.kr(az, 0.05).neg, Lag.kr(el, 0.05)));
            Out.ar(out, (late * mid) ! 2);
            Out.ar(rev, (late * Lag.kr(wet, 0.05)) ! 2);
        }).add;
        SynthDef(\manifoldAtkDecode, { |in, out = 0, trim = 1|
            Out.ar(out, decode.ar(In.ar(in, 4), ~manifold[\atkKernel]) * trim);
        }).add;
        s.sync;
        ~manifold[\atkBus] = Bus.audio(s, 4);
        ~manifold[\atkDecoder] = Synth.after(~manifold[\speakersGroup], \manifoldAtkDecode,
            [in: ~manifold[\atkBus], trim: ~manifold[\atkTrim]]);
        ~manifold[\atkReady] = true;
        "manifold: the ATK's room is ready -- ears: CIPIC %".format(~manifold[\ears]).postln;
    };
    if (~manifold[\room] == \atk and: { ~manifold[\atkReady].not }) {
        ~manifold[\atkMissing].();
        ~manifold[\room] = \head;
    };

    // Everything in ~manifold is read and set by key -- ~manifold[\maps],
    // ~manifold[\drop].(p) -- never as ~manifold.maps. An Event answers
    // .something by running its own method of that name if it has one
    // (~manifold.count is Collection's count, not ours), and by calling a
    // function it holds with ~manifold as the first argument.

    // Let a map go: its voices and its listeners.
    ~manifold[\drop] = { |prefix|
        var m = ~manifold[\maps][prefix];
        if (m.notNil) {
            m[\voices].do(_.free);
            m[\speakers].do(_.free);
            m[\buses].do(_.free);
            m[\funcs].do(_.free);
            ~manifold[\maps].removeAt(prefix);
            "manifold: % has gone".format(prefix).postln;
        };
    };

    // Anything from a run before this one goes first. The maps are kept in a
    // Dictionary, not an Event: their keys are prefixes, strings, and an Event
    // matches keys by identity -- the same text arriving again in a new
    // message would not find its map.
    (~manifold[\maps] ? []).do { |m|
        m[\voices].do(_.free); m[\speakers].do(_.free); m[\buses].do(_.free);
        m[\funcs].do(_.free);
    };
    ~manifold[\maps] = Dictionary.new;
    ~manifold[\count] = 0;

    // A MIDI note as a name: 60 is C4.
    ~manifold[\noteName] = { |n|
        n = n.round.asInteger;
        ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][n % 12]
        ++ ((n div: 12) - 1);
    };

    // The shape only counts for the outputs it names, in their order.
    ~manifold[\shapeFor] = { |m|
        var sh = m[\shape];
        if (sh.notNil and: { sh[\outputs].collect(_[\name]) == m[\names] }) { sh } { nil };
    };

    // Tune a map's voices to its shape -- or the climb, without one -- and
    // say so in the post window.
    ~manifold[\tune] = { |m|
        var sh = ~manifold[\shapeFor].(m);
        var vs = ~manifold[\voicing].(sh, m[\lowest], m[\names].size);
        m[\voices].do { |v, i|
            if (~manifold[\sound] == \noise) {
                v.set(\gain, vs[i][\gain], \centre, vs[i][\freq] * 2);
            } {
                v.set(\gain, vs[i][\gain], \freq, vs[i][\freq],
                      \bright, if (~manifold[\bright]) { m[\bright] } { 1 });
            };
        };
        ~manifold[\place].(m, vs);
        "manifold: % -- % outputs, %:".format(m[\prefix], m[\names].size,
            if (sh.notNil) { ~manifold[\music] } { "no shape: the climb" }).postln;
        m[\names].do { |name, i|
            "  %  %%".format(name, ~manifold[\noteName].(vs[i][\freq].cpsmidi),
                if (vs[i][\gain] < 1) { " (quiet)" } { "" }).postln;
        };
    };

    // Every voice from every point (3B-2g): ~manifold[\mix] over the points
    // heard. Once per burst -- a frame's messages arrive together, so the
    // voices are set once, a few milliseconds after the first of them.
    ~manifold[\apply] = { |m|
        var mx = ~manifold[\mix].(m[\pts].values.asArray, m[\voices].size);
        m[\voices].do { |v, i|
            if (~manifold[\sound] == \noise) { v.set(\amp, mx[i][\amp]) } {
                v.set(\amp, mx[i][\amp], \fm, mx[i][\fm], \ring, mx[i][\ring],
                      \ratio, mx[i][\ratio],
                      \bright, if (~manifold[\bright]) { mx[i][\bright] } { 1 });
            };
        };
    };
    ~manifold[\soon] = { |m|
        if (m[\due].not) {
            m[\due] = true;
            SystemClock.sched(0.004, { m[\due] = false; ~manifold[\apply].(m); nil });
        };
    };
    // A point heard: its entry, made when first heard.
    ~manifold[\point] = { |m, n|
        var p = m[\pts][n];
        if (p.isNil) {
            p = (number: n, levels: [], bright: 0.5, u: nil);
            m[\pts][n] = p;
        };
        p;
    };

    // Where each speaker is (3B-3): \stereo its pan, \head what the head
    // hears from there. `vs`: the voicing, if it has just been worked out.
    ~manifold[\place] = { |m, vs|
        var sh = ~manifold[\shapeFor].(m);
        vs = vs ?? { ~manifold[\voicing].(sh, m[\lowest], m[\names].size) };
        m[\speakers].do { |sp, i|
            var h, at;
            if (~manifold[\room] == \stereo) { sp.set(\pan, vs[i][\pan]) } {
                at = ~manifold[\speakerAt].(
                    if (sh.notNil) { sh[\outputs][i] }, if (sh.notNil) { sh[\heights] } { [0] },
                    ~manifold[\roomSize]);
                h = ~manifold[\hear].(at, ~manifold[\facing]);
                if (~manifold[\room] == \atk) {
                    sp.set(\az, h[\az], \el, h[\el], \gain, h[\gain], \air, h[\air],
                           \wet, h[\wet], \inside, if (at[\inside]) { 1 } { 0 });
                } {
                    sp.set(\delayL, h[\delayL], \delayR, h[\delayR], \gainL, h[\gainL],
                           \gainR, h[\gainR], \shelfL, h[\shelfL], \shelfR, h[\shelfR],
                           \notch, h[\notch], \air, h[\air], \wet, h[\wet]);
                };
            };
        };
    };
    // A map's speakers, one per output, for the room chosen.
    ~manifold[\speak] = { |m|
        m[\speakers].do(_.free);
        m[\speakers] = m[\buses].collect { |b|
            switch (~manifold[\room])
            { \stereo } { Synth(\manifoldStereo, [in: b], ~manifold[\speakersGroup]) }
            { \atk } {
                Synth(\manifoldAtk, [in: b, foa: ~manifold[\atkBus], rev: ~manifold[\roomBus],
                    latency: ~manifold[\atkLatency]], ~manifold[\speakersGroup])
            }
            { Synth(\manifoldHead, [in: b, rev: ~manifold[\roomBus]], ~manifold[\speakersGroup]) };
        };
        ~manifold[\place].(m);
    };
    // A map's voices: one per output, each into a bus of its own, its
    // speaker reading it; tuned and placed.
    ~manifold[\build] = { |m|
        m[\voices].do(_.free);
        m[\speakers].do(_.free);
        m[\buses].do(_.free);
        m[\buses] = m[\names].collect { Bus.audio(s, 1) };
        m[\voices] = m[\buses].collect { |b|
            Synth(if (~manifold[\sound] == \noise) { \manifoldVoice } { \manifoldTone },
                [out: b], ~manifold[\voicesGroup]);
        };
        m[\speakers] = [];
        ~manifold[\speak].(m);
        ~manifold[\tune].(m);
    };

    // A map heard for the first time: listen on its outputs, shape, levels
    // and position, and ask it to describe itself now.
    ~manifold[\start] = { |prefix, port|
        var m = (prefix: prefix, port: port, app: NetAddr("127.0.0.1", port),
                 names: nil, shape: nil, shapeMsg: nil, bright: 0.5,
                 voices: [], speakers: [], buses: [],
                 seen: Main.elapsedTime, warned: false, due: false,
                 pts: Dictionary.new,
                 lowest: ~manifold[\lowest] + (7 * (~manifold[\count] % 4)));
        ~manifold[\count] = ~manifold[\count] + 1;
        ~manifold[\point].(m, 1);
        m[\funcs] = [
            // .../outputs N name0 ch0 name1 ch1 ...
            OSCFunc({ |msg|
                var n = msg[1];
                var names = Array.fill(n, { |i| msg[2 + (2 * i)].asString });
                if (names != m[\names]) {
                    m[\names] = names;
                    ~manifold[\build].(m);
                };
            }, (prefix ++ "/outputs").asSymbol),
            // .../shape -- retuned, not rebuilt, when only it changes
            OSCFunc({ |msg|
                var raw = msg[1..];
                if (raw != m[\shapeMsg]) {
                    m[\shapeMsg] = raw;
                    m[\shape] = ~manifold[\readShape].(msg);
                    if (m[\names].notNil) { ~manifold[\tune].(m) };
                };
            }, (prefix ++ "/shape").asSymbol),
            // .../levels f0 f1 ... in the order the outputs were described:
            // point 1's
            OSCFunc({ |msg|
                ~manifold[\point].(m, 1)[\levels] = msg[1..];
                ~manifold[\soon].(m);
            }, (prefix ++ "/levels").asSymbol),
            // .../position x y z [u]: point 1's -- how bright
            OSCFunc({ |msg|
                var p = ~manifold[\point].(m, 1);
                p[\u] = msg[4];
                p[\bright] = ~manifold[\brightness].(m[\shape], msg[3], msg[4]);
                m[\bright] = p[\bright];
                ~manifold[\soon].(m);
            }, (prefix ++ "/position").asSymbol),
            // .../points P, then per point i number, s name, s inlets: the
            // points there are -- one removed is let go (its levels came at
            // 0 just before)
            OSCFunc({ |msg|
                var numbers = msg[1].collect { |k| msg[2 + (3 * k)] };
                m[\pts].keys.copy.do { |n| if (n != 1 and: { numbers.includes(n).not }) { m[\pts].removeAt(n) } };
                ~manifold[\soon].(m);
            }, (prefix ++ "/points").asSymbol)
        ] ++ (2..~manifold[\maxPoints]).collect { |n|
            // .../point/<n>/levels and /position: points 2 and up (3B-2g)
            [OSCFunc({ |msg|
                ~manifold[\point].(m, n)[\levels] = msg[1..];
                ~manifold[\soon].(m);
             }, (prefix ++ "/point/" ++ n ++ "/levels").asSymbol),
             OSCFunc({ |msg|
                var p = ~manifold[\point].(m, n);
                p[\u] = msg[4];
                p[\bright] = ~manifold[\brightness].(m[\shape], msg[3], msg[4]);
                ~manifold[\soon].(m);
             }, (prefix ++ "/point/" ++ n ++ "/position").asSymbol)]
        }.flatten;
        ~manifold[\maps][prefix] = m;
        "manifold: % playing, answers on port %".format(prefix, port).postln;
        // Port 0: the map hears no answers -- nothing to send to.
        if (port > 0) { m[\app].sendMsg(prefix ++ "/describe") };
        m;
    };

    // The roll call: /manifold/map prefix answer-port outputs protocol
    OSCdef(\manifoldRollCall, { |msg|
        var prefix = msg[1].asString, port = msg[2];
        var m = ~manifold[\maps][prefix];
        if (m.isNil) {
            m = ~manifold[\start].(prefix, port);
        } {
            if (m[\port] != port and: { m[\warned].not }) {
                "manifold: two maps called % (answering on % and %) -- give one another name"
                .format(prefix, m[\port], port).warn;
                m[\warned] = true;
            };
        };
        m[\seen] = Main.elapsedTime;
    }, (~manifold[\root] ++ "/map").asSymbol);

    // Once a second: hello to every map; let go of any gone silent.
    ~manifold[\hello].stop;
    ~manifold[\hello] = Routine {
        loop {
            var now = Main.elapsedTime;
            ~manifold[\maps].keys.copy.do { |p|
                var m = ~manifold[\maps][p];
                if (now - m[\seen] > ~manifold[\silentAfter]) {
                    ~manifold[\drop].(p);
                } {
                    if (m[\port] > 0) { m[\app].sendMsg(p ++ "/hello", "sclang") };
                };
            };
            1.wait;
        }
    }.play;

    // Ask every map listening to describe itself now: the root describe, to
    // each port a copy of example-osc may have.
    ~manifold[\ports].do { |k|
        NetAddr("127.0.0.1", ~manifold[\firstPort] + k).sendMsg(~manifold[\root] ++ "/describe");
    };
    "manifold: listening on % for %/map".format(NetAddr.langPort, ~manifold[\root]).postln;
};
)
