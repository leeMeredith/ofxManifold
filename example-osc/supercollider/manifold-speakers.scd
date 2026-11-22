// ofxManifold -- your speakers, from your map (round 3B-4a).
//
// One sound, played on every speaker at the level the map gives that
// speaker. The map is the panner: move the point and the sound moves
// between your speakers. No notes, no reverb, nothing but SuperCollider
// itself -- this file loads nothing else, and needs nothing installed.
//
// HOW TO USE THIS FILE
//
// Each block below is a short piece between parentheses. Put the cursor
// anywhere inside a block and press cmd-return; the post window says what
// happened.
//
//   1. In the editor, make the map: put a node where each speaker stands,
//      on the floor plan, and join them (select them, f). Each node gets
//      its own output, and the outputs are numbered in the order the nodes
//      were placed: the first node is speaker 1, the second speaker 2, and
//      on. Save the map.
//   2. SETUP: run DEVICES to see your audio devices; in START, write the
//      device's name and how many speakers it has; run START.
//   3. CHECK THE SPEAKERS: each speaker in turn plays one pulse for speaker
//      1, two for speaker 2, and so on -- check the wiring by ear.
//   4. Start example-osc and open your map (o). Move the point: the sound
//      follows it from speaker to speaker. Choose the sound below.
//   5. No speakers yet? ON HEADPHONES folds every speaker into left and
//      right by where its node is on the map.
//   6. STOP is the same as cmd-period.
//
// Only one of the two SuperCollider files at a time: this one, or
// manifold-modes.scd (the music). Both listen for the map.
//
// How a speaker is chosen: by its output's channel. Channel 0 is speaker 1,
// the device's first output; channel 1 is speaker 2; and so on. The editor
// gives the channels in order as outputs are made. An output on a channel
// past the last speaker is said in the post window, and silent.


// ==== SETUP ================================================================

// DEVICES -- lists the audio devices this computer can play through.
(
try {
    "speakers: devices that can play --".postln;
    ServerOptions.outDevices.do { |d| ("    \"" ++ d ++ "\"").postln };
} {
    "speakers: this SuperCollider cannot list devices -- leave the device empty in START, and choose it in your system's sound settings".warn;
};
)

// START -- starts SuperCollider's server on your device and listens for the
// map. Change the three lines below, then run it. Run it again after a
// change and it starts again with the new settings.
(
var device = "";      // a name from DEVICES, in quotes; "" for the system's own
var speakers = 2;     // how many speakers the device has
var volume = 0.3;     // the sound at full level: 0 silent, 1 full scale

var same = ~speakers.notNil and: { ~speakers[\playing] == true }
    and: { ~speakers[\device] == device } and: { ~speakers[\count] == speakers };
if (same) {
    ~speakers[\volume] = volume;
    ~speakers[\sources].set(\volume, volume);
    "speakers: already playing -- volume %".format(volume).postln;
} {
    if (~speakers.notNil and: { ~speakers[\playing] == true }) { ~speakers[\stop].() };
    ~speakers = (device: device, count: speakers, volume: volume,
        sound: \tone, where: \speakers, path: nil, input: 0,
        root: "/manifold", firstPort: 57121, ports: 16, silentAfter: 5,
        maps: Dictionary.new, playing: false);

    // Every map's outputs, built again: one small synth per output, taking
    // the sound at that output's level -- to its speaker, or, on
    // headphones, panned by where its node is.
    ~speakers[\build] = { |m|
        var said = List.new;
        m[\outs].do { |o| o !? (_.free) };
        m[\outs] = m[\channels].collect { |ch, i|
            var level = m[\levels][i] ? 0;
            case
            { ~speakers[\where] == \headphones } {
                Synth(\speakersFold, [in: ~speakers[\bus], level: level,
                    pan: ~speakers[\pan].(m, i)], ~speakers[\outsGroup]);
            }
            { ch < ~speakers[\count] } {
                Synth(\speakersOut, [in: ~speakers[\bus], out: ch, level: level],
                    ~speakers[\outsGroup]);
            }
            { said.add("% (channel %: speaker %)".format(m[\names][i], ch, ch + 1)); nil };
        };
        if (said.notEmpty) {
            "speakers: only % speakers -- silent in %: %".format(~speakers[\count], m[\prefix],
                said.join(", ")).warn;
        };
        m[\silent] = said.size;
    };

    // Left to right by where an output's node is on the map; the middle for
    // an output no node places.
    ~speakers[\pan] = { |m, i|
        var p = m[\places] !? { |pl| pl[m[\names][i]] };
        if (p.isNil) { 0 } { (p * 2 - 1).clip(-1, 1) };
    };

    // The sound everything plays, on one bus.
    ~speakers[\play] = { |sound|
        var args = [out: ~speakers[\bus], volume: ~speakers[\volume]];
        ~speakers[\sources].freeAll;
        ~speakers[\sound] = sound;
        switch (sound)
        { \tone } { Synth(\speakersTone, args, ~speakers[\sources]) }
        { \noise } { Synth(\speakersNoise, args, ~speakers[\sources]) }
        { \input } { Synth(\speakersInput, args ++ [input: ~speakers[\input]], ~speakers[\sources]) }
        { \file } {
            Synth(\speakersFile, args ++ [buf: ~speakers[\buffer]], ~speakers[\sources]) }
        { \nothing } { };
    };

    // A map heard for the first time: its outputs, its shape, its levels.
    ~speakers[\start] = { |prefix, port|
        var m = (prefix: prefix, port: port, names: [], channels: [], levels: [],
                 places: nil, outs: [], seen: Main.elapsedTime, silent: 0);
        m[\funcs] = [
            // .../outputs N name0 ch0 name1 ch1 ...
            OSCFunc({ |msg|
                var n = msg[1];
                var names = Array.fill(n, { |i| msg[2 + (2 * i)].asString });
                var channels = Array.fill(n, { |i| msg[3 + (2 * i)] });
                if (names != m[\names] or: { channels != m[\channels] }) {
                    m[\names] = names;
                    m[\channels] = channels;
                    m[\levels] = 0 ! n;
                    ~speakers[\build].(m);
                    "speakers: % -- %".format(prefix, names.collect { |nm, i|
                        "% on speaker %".format(nm, channels[i] + 1) }.join(", ")).postln;
                };
            }, (prefix ++ "/outputs").asSymbol),
            // .../shape L heights..., N, then per output: name, channel,
            // level, levels, placed, x, y, z -- where each node is, for
            // headphones
            OSCFunc({ |msg|
                var at = 2 + msg[1], n = msg[at], places = Dictionary.new;
                n.do { |k|
                    var b = at + 1 + (8 * k);
                    if (msg[b + 4] != 0) { places[msg[b].asString] = msg[b + 5] };
                };
                if (places != m[\places]) {
                    m[\places] = places;
                    if (~speakers[\where] == \headphones) {
                        m[\outs].do { |o, i| o !? { o.set(\pan, ~speakers[\pan].(m, i)) } };
                    };
                };
            }, (prefix ++ "/shape").asSymbol),
            // .../levels f0 f1 ...: the first point's, in the outputs' order
            OSCFunc({ |msg|
                m[\levels] = msg[1..];
                m[\outs].do { |o, i| o !? { o.set(\level, m[\levels][i] ? 0) } };
            }, (prefix ++ "/levels").asSymbol)
        ];
        ~speakers[\maps][prefix] = m;
        "speakers: % heard".format(prefix).postln;
        if (port > 0) { NetAddr("127.0.0.1", port).sendMsg(prefix ++ "/describe") };
        m;
    };

    ~speakers[\drop] = { |prefix|
        var m = ~speakers[\maps].removeAt(prefix);
        m !? { m[\funcs].do(_.free); m[\outs].do { |o| o !? (_.free) } };
        "speakers: % has stopped".format(prefix).postln;
    };

    // Stopped -- by STOP or cmd-period, whose server has already freed
    // every synth -- or by START starting again.
    ~speakers[\stop] = { |byCmdPeriod = false|
        if (byCmdPeriod) { ~speakers[\maps].do { |m| m[\outs] = [] } };
        ~speakers[\maps].keys.copy.do { |p| ~speakers[\drop].(p) };
        if (byCmdPeriod.not) { [\sources, \outsGroup].do { |k| ~speakers[k] !? (_.free) } };
        ~speakers[\bus] !? (_.free);
        ~speakers[\hello].stop;
        OSCdef(\speakersRollCall).free;
        CmdPeriod.remove(~speakers[\onStop]);
        ~speakers[\playing] = false;
    };
    ~speakers[\onStop] = {
        ~speakers[\stop].(true);
        "speakers: stopped -- START to play again".postln;
    };

    fork {
        var options = s.options;
        if (s.serverRunning and: {
            options.numOutputBusChannels != speakers.max(2)
            or: { (options.outDevice ? "") != device } }) {
            "speakers: starting the server again for % speakers".format(speakers).postln;
            s.quit;
            while { s.serverRunning } { 0.1.wait };
        };
        options.numOutputBusChannels = speakers.max(2);
        options.outDevice = if (device.isEmpty) { nil } { device };
        if (s.serverRunning.not) { s.bootSync };

        // A speaker: the sound at its level. A pulse, for CHECK THE SPEAKERS.
        // The sounds.
        SynthDef(\speakersOut, { |in, out, level = 0|
            Out.ar(out, In.ar(in) * Lag.kr(level, 0.02));
        }).add;
        SynthDef(\speakersFold, { |in, level = 0, pan = 0|
            Out.ar(0, Pan2.ar(In.ar(in) * Lag.kr(level, 0.02), Lag.kr(pan, 0.05)));
        }).add;
        SynthDef(\speakersPulse, { |out, volume = 0.3|
            var env = EnvGen.ar(Env.linen(0.005, 0.07, 0.005), doneAction: 2);
            Out.ar(out, SinOsc.ar(880) * env * volume);
        }).add;
        SynthDef(\speakersTone, { |out, volume = 0.3|
            Out.ar(out, SinOsc.ar(440) * volume);
        }).add;
        SynthDef(\speakersNoise, { |out, volume = 0.3|
            Out.ar(out, PinkNoise.ar * 2 * volume);
        }).add;
        SynthDef(\speakersInput, { |out, input = 0, volume = 0.3|
            Out.ar(out, SoundIn.ar(input) * volume);
        }).add;
        SynthDef(\speakersFile, { |out, buf, volume = 0.3|
            Out.ar(out, PlayBuf.ar(1, buf, BufRateScale.kr(buf), loop: 1) * volume);
        }).add;
        s.sync;

        ~speakers[\bus] = Bus.audio(s, 1);
        ~speakers[\sources] = Group(s);
        ~speakers[\outsGroup] = Group.after(~speakers[\sources]);
        ~speakers[\play].(\tone);
        ~speakers[\playing] = true;
        CmdPeriod.add(~speakers[\onStop]);

        // The roll call: /manifold/map prefix answer-port outputs protocol
        OSCdef(\speakersRollCall, { |msg|
            var prefix = msg[1].asString;
            var m = ~speakers[\maps][prefix] ?? { ~speakers[\start].(prefix, msg[2]) };
            m[\seen] = Main.elapsedTime;
        }, (~speakers[\root] ++ "/map").asSymbol);

        // Once a second: let go of any map gone silent.
        ~speakers[\hello] = Routine {
            loop {
                ~speakers[\maps].keys.copy.do { |p|
                    if (Main.elapsedTime - ~speakers[\maps][p][\seen] > ~speakers[\silentAfter]) {
                        ~speakers[\drop].(p);
                    };
                };
                1.wait;
            }
        }.play;
        ~speakers[\ports].do { |k|
            NetAddr("127.0.0.1", ~speakers[\firstPort] + k).sendMsg(~speakers[\root] ++ "/describe");
        };
        "speakers: % speakers on %, volume % -- playing a test tone through the map; start example-osc"
        .format(speakers, if (device.isEmpty) { "the system's own output" } { device }, volume).postln;
    };
};
)

// STOP -- silence, the same as cmd-period. START plays again.
(
CmdPeriod.run;
)


// ==== CHECK THE SPEAKERS ===================================================

// CHECK -- each speaker in turn: one pulse for speaker 1, two for speaker 2,
// and on, then a pause. The map plays on underneath. Expect: count along,
// and each count from the speaker it names.
(
if (~speakers.isNil or: { ~speakers[\playing] != true }) { "speakers: run START first".warn } {
    fork {
        ~speakers[\count].do { |ch|
            "speakers: speaker % (channel %)".format(ch + 1, ch).postln;
            (ch + 1).do {
                Synth(\speakersPulse, [out: ch, volume: ~speakers[\volume]], ~speakers[\outsGroup], \addAfter);
                0.2.wait;
            };
            0.5.wait;
        };
        "speakers: checked % speakers".format(~speakers[\count]).postln;
    };
};
)


// ==== A SOUND THROUGH THE MAP ==============================================
// What every speaker plays, at its level. Move the point in example-osc:
// the sound moves with it.

// TEST TONE (START's own) -- a steady A, 440 Hz.
// Expect: one note, louder from the speakers nearest the point.
(
if (~speakers.isNil or: { ~speakers[\playing] != true }) { "speakers: run START first".warn } {
    ~speakers[\play].(\tone);
    "speakers: a test tone".postln;
};
)

// PINK NOISE -- a steady hiss, every pitch at once.
// Expect: easier than the tone to place in the room.
(
if (~speakers.isNil or: { ~speakers[\playing] != true }) { "speakers: run START first".warn } {
    ~speakers[\play].(\noise);
    "speakers: pink noise".postln;
};
)

// A SOUND FILE -- write its path between the quotes; it loops. Only its first
// channel plays. Drag the file onto this window to write its path.
// Expect: your sound, moving with the point.
(
var path = "";
if (~speakers.isNil or: { ~speakers[\playing] != true }) { "speakers: run START first".warn } {
    if (path.isEmpty or: { File.exists(path).not }) {
        "speakers: write the sound file's path between the quotes".warn;
    } {
        fork {
            var old = ~speakers[\buffer];
            ~speakers[\buffer] = Buffer.readChannel(s, path, channels: [0]);
            s.sync;
            ~speakers[\play].(\file);
            old !? (_.free);
            "speakers: %".format(path.basename).postln;
        };
    };
};
)

// THE INPUT -- a microphone or line in, live: input 1 of the device. Mind
// feedback with a microphone near the speakers.
// Expect: what goes in, moving with the point.
(
var input = 1;
if (~speakers.isNil or: { ~speakers[\playing] != true }) { "speakers: run START first".warn } {
    ~speakers[\input] = input - 1;
    ~speakers[\play].(\input);
    "speakers: input %".format(input).postln;
};
)

// NOTHING -- silence, the map still followed.
(
if (~speakers.isNil or: { ~speakers[\playing] != true }) { "speakers: run START first".warn } {
    ~speakers[\play].(\nothing);
    "speakers: nothing playing".postln;
};
)


// ==== WHERE IT PLAYS =======================================================

// ON THE SPEAKERS (START's own) -- each output on its own speaker.
(
if (~speakers.isNil or: { ~speakers[\playing] != true }) { "speakers: run START first".warn } {
    ~speakers[\where] = \speakers;
    ~speakers[\maps].do { |m| ~speakers[\build].(m) };
    "speakers: on the speakers".postln;
};
)

// ON HEADPHONES -- every output folded into left and right by where its
// node is on the map, left to right. A check before there are speakers,
// not a room: front and back sound the same. Every output plays, however
// many speakers START was told.
// Expect: the sound moving left and right with the point.
(
if (~speakers.isNil or: { ~speakers[\playing] != true }) { "speakers: run START first".warn } {
    ~speakers[\where] = \headphones;
    ~speakers[\maps].do { |m| ~speakers[\build].(m) };
    "speakers: on headphones".postln;
};
)
