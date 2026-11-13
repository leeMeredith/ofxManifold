// ofxManifold -- example-osc's receiver for SuperCollider (round 3B-1,
// protocol 2: several maps at once).
//
// Run the whole block: put the cursor inside the outer parentheses and press
// cmd-return. Then start example-osc -- one copy, or several with different
// maps. Headphones are fine.
//
// It listens for the roll call, /manifold/map, and starts a map for each
// prefix it hears (/manifold/room, /manifold/forest ...): one voice per
// output, as many as that map's outputs message says, built again when that
// changes, placed evenly from left to right. A voice's volume follows its
// level with a 20 ms lag, so sixty updates a second sound smooth.
//
// Each voice is a tone of its own -- a soft sine with two quiet overtones --
// on the steps of a major pentatonic scale, so any outputs sounding together
// stay consonant and each can be picked out by pitch: on the room, FL FR RL
// RR on the floor, then the middle's four, then the top's, climbing. The post
// window lists each output's note. Each map starts a fifth higher than the
// last, so maps can be told apart too. Set ~manifold[\sound] = \noise below for
// the first version's pink noise bands instead.
//
// It says hello to each map once a second, at the port its roll call gives,
// so example-osc shows it answered; it asks every map to describe itself when
// it starts, so it need not wait for the roll call; and it lets a map go --
// voices freed -- when its roll call has been silent for five seconds.
//
// cmd-period stops the sound and the hellos; run the block again to restart.

(
~manifold = ~manifold ? ();
~manifold[\root] = "/manifold";
~manifold[\firstPort] = 57121;    // where example-osc copies may hear answers
~manifold[\ports] = 16;
~manifold[\silentAfter] = 5;      // seconds without a roll call: the map has gone
~manifold[\sound] = \tone;        // \tone: a note per output; \noise: a noise band
~manifold[\scale] = [0, 2, 4, 7, 9];  // major pentatonic, in semitones
~manifold[\lowest] = 48;          // the first map's first note: C3, as MIDI

s.waitForBoot {
    SynthDef(\manifoldVoice, { |out = 0, pan = 0, amp = 0, centre = 440|
        var sig = BPF.ar(PinkNoise.ar, centre, 0.4) * 2;
        sig = sig * Lag.kr(amp, 0.02);
        Out.ar(out, Pan2.ar(sig, pan));
    }).add;
    SynthDef(\manifoldTone, { |out = 0, pan = 0, amp = 0, freq = 220|
        var sig = SinOsc.ar(freq) + (SinOsc.ar(freq * 2) * 0.25) + (SinOsc.ar(freq * 3) * 0.08);
        sig = sig * 0.3 * Lag.kr(amp, 0.02);
        Out.ar(out, Pan2.ar(sig, pan));
    }).add;
    s.sync;

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
            m[\funcs].do(_.free);
            ~manifold[\maps].removeAt(prefix);
            "manifold: % has gone".format(prefix).postln;
        };
    };

    // Anything from a run before this one goes first. The maps are kept in a
    // Dictionary, not an Event: their keys are prefixes, strings, and an Event
    // matches keys by identity -- the same text arriving again in a new
    // message would not find its map.
    (~manifold[\maps] ? []).do { |m| m[\voices].do(_.free); m[\funcs].do(_.free) };
    ~manifold[\maps] = Dictionary.new;
    ~manifold[\count] = 0;

    // The i-th step up the scale from a map's lowest note, as MIDI.
    ~manifold[\note] = { |m, i|
        var steps = ~manifold[\scale].size;
        m[\lowest] + ~manifold[\scale][i % steps] + (12 * (i div: steps));
    };
    // A MIDI note as a name: 60 is C4.
    ~manifold[\noteName] = { |n|
        ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][n % 12]
        ++ ((n div: 12) - 1);
    };

    // A map's voices: one per output, spread from left to right, each a step
    // higher -- a note up the scale, or a noise band.
    ~manifold[\build] = { |m, names|
        var notes = names.collect { |name, i| ~manifold[\note].(m, i) };
        m[\voices].do(_.free);
        m[\voices] = names.collect { |name, i|
            var pan = if (names.size > 1) { i.linlin(0, names.size - 1, -0.9, 0.9) } { 0 };
            if (~manifold[\sound] == \noise) {
                Synth(\manifoldVoice, [\pan, pan, \centre, 200 * (1.5 ** i)]);
            } {
                Synth(\manifoldTone, [\pan, pan, \freq, notes[i].midicps]);
            };
        };
        "manifold: % -- % outputs:".format(m[\prefix], names.size).postln;
        names.do { |name, i|
            if (~manifold[\sound] == \noise) {
                "  %".format(name).postln;
            } {
                "  %  %".format(name, ~manifold[\noteName].(notes[i])).postln;
            };
        };
    };

    // A map heard for the first time: listen on its outputs and levels, and
    // ask it to describe itself now.
    ~manifold[\start] = { |prefix, port|
        var m = (prefix: prefix, port: port, app: NetAddr("127.0.0.1", port),
                 names: nil, voices: [], seen: Main.elapsedTime, warned: false,
                 lowest: ~manifold[\lowest] + (7 * (~manifold[\count] % 4)));
        ~manifold[\count] = ~manifold[\count] + 1;
        m[\funcs] = [
            // .../outputs N name0 ch0 name1 ch1 ...
            OSCFunc({ |msg|
                var n = msg[1];
                var names = Array.fill(n, { |i| msg[2 + (2 * i)].asString });
                if (names != m[\names]) {
                    m[\names] = names;
                    ~manifold[\build].(m, names);
                };
            }, (prefix ++ "/outputs").asSymbol),
            // .../levels f0 f1 ... in the order the outputs were described
            OSCFunc({ |msg|
                msg[1..].do { |level, i|
                    var v = m[\voices][i];
                    if (v.notNil) { v.set(\amp, level) };
                };
            }, (prefix ++ "/levels").asSymbol)
        ];
        ~manifold[\maps][prefix] = m;
        "manifold: % playing, answers on port %".format(prefix, port).postln;
        m[\app].sendMsg(prefix ++ "/describe");
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
