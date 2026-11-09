// ofxManifold -- example-osc's receiver for SuperCollider (round 3B-1).
//
// Run the whole block: put the cursor inside the outer parentheses and press
// cmd-return. Then start example-osc. Headphones are fine.
//
// It builds one voice per output, as many as /manifold/outputs says, and
// builds them again when that changes -- add an output in the editor, open
// the pair in example-osc, and the patch follows. Each voice is pink noise
// through its own band, so channels can be told apart by ear, placed evenly
// from left to right, and its volume follows its level with a 20 ms lag so
// sixty updates a second sound smooth.
//
// It says hello once a second, so example-osc shows it answered, and asks to
// be told the outputs when it starts, so it need not wait for them.
//
// cmd-period stops the sound and the hellos; run the block again to restart.

(
~manifold = ~manifold ? ();
~manifold.app = NetAddr("127.0.0.1", 57121);   // where example-osc listens
~manifold.prefix = "/manifold";

s.waitForBoot {
    SynthDef(\manifoldVoice, { |out = 0, pan = 0, amp = 0, centre = 440|
        var sig = BPF.ar(PinkNoise.ar, centre, 0.4) * 2;
        sig = sig * Lag.kr(amp, 0.02);
        Out.ar(out, Pan2.ar(sig, pan));
    }).add;
    s.sync;

    ~manifold.voices = ~manifold.voices ? [];
    ~manifold.names = nil;

    // One voice per output, spread from left to right, each a band higher.
    ~manifold.build = { |names|
        ~manifold.voices.do(_.free);
        ~manifold.voices = names.collect { |name, i|
            var pan = if (names.size > 1) { i.linlin(0, names.size - 1, -0.9, 0.9) } { 0 };
            Synth(\manifoldVoice, [\pan, pan, \centre, 200 * (1.5 ** i)]);
        };
        ("manifold: % outputs -- %".format(names.size, names.join(", "))).postln;
    };

    // /manifold/outputs N name0 ch0 name1 ch1 ...
    OSCdef(\manifoldOutputs, { |msg|
        var n = msg[1];
        var names = Array.fill(n, { |i| msg[2 + (2 * i)].asString });
        if (names != ~manifold.names) {
            ~manifold.names = names;
            ~manifold.build.(names);
        };
    }, ~manifold.prefix ++ "/outputs");

    // /manifold/levels f0 f1 ... in the order the outputs were described
    OSCdef(\manifoldLevels, { |msg|
        msg[1..].do { |level, i|
            var v = ~manifold.voices[i];
            if (v.notNil) { v.set(\amp, level) };
        };
    }, ~manifold.prefix ++ "/levels");

    // Hello once a second; and ask for the outputs now.
    ~manifold.hello.stop;
    ~manifold.hello = Routine {
        loop {
            ~manifold.app.sendMsg(~manifold.prefix ++ "/hello", "sclang");
            1.wait;
        }
    }.play;
    ~manifold.app.sendMsg(~manifold.prefix ++ "/describe");
    "manifold: listening on %, answering to %".format(NetAddr.langPort, ~manifold.app).postln;
};
)
