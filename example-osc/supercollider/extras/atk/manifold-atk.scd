// ofxManifold -- the Ambisonic Toolkit's room (round 3B-3b), an extra.
//
// Adds one room, \atk, to the patch in supercollider/manifold-receive.scd:
// each speaker encoded at its direction into a first-order sound field, and
// heard through ears measured on a dummy head (the CIPIC set) -- front and
// back, up and down, far clearer than the patch's own head -- with the same
// distance and reverb.
//
// It needs the Ambisonic Toolkit, its kernels, and sc3-plugins: this
// folder's README.md, and manifold-atk-modes.scd beside this file, say how
// to get them. The patch itself needs none of it.
//
// The easy way in is manifold-atk-modes.scd: its START runs the patch and
// then this file. Run by hand, this file expects the patch playing.
//
//   ~manifold[\ears] = 21       whose ears -- a CIPIC subject; 21 is the
//                              KEMAR dummy head. Set before this file runs.
//   ~manifold[\atkTrim] = 1     its level against the head's.

(
var there = \Atk.asClass.notNil and: { \FoaDecoderKernel.asClass.notNil }
    and: { \FoaPanB.asClass.notNil }
    and: { File.exists(\Atk.asClass.userKernelDir +/+ "FOA" +/+ "decoders" +/+ "cipic") };
case
{ ~manifold.isNil or: { ~manifold[\playing].isNil } or: { ~manifold[\playing].().not } } {
    "manifold: the ATK's room adds itself to the patch -- start the patch first (START in manifold-atk-modes.scd does both)".warn;
}
{ there.not } {
    ("manifold: the ATK's room needs the Ambisonic Toolkit, its kernels and sc3-plugins -- "
     ++ "not all are here, so the patch plays on without it. "
     ++ "WHERE THINGS STAND in manifold-atk-modes.scd says what is missing and what to do.").warn;
}
{
    fork {
        var panB = \FoaPanB.asClass, decode = \FoaDecode.asClass, old;
        // The patch's stage first: its groups and the reverb's bus.
        while { ~manifold[\staged] != true } { 0.1.wait };
        old = ~manifold[\rooms][\atk];
        old !? { old[\free].() };
        ~manifold[\ears] = ~manifold[\ears] ? 21;
        ~manifold[\atkTrim] = ~manifold[\atkTrim] ? 1;
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
        // One decoder from the sound field to the headphones, after the
        // speakers.
        SynthDef(\manifoldAtkDecode, { |in, out = 0, trim = 1|
            Out.ar(out, decode.ar(In.ar(in, 4), ~manifold[\atkKernel]) * trim);
        }).add;
        s.sync;
        ~manifold[\atkBus] = Bus.audio(s, 4);
        ~manifold[\atkDecoder] = Synth.after(~manifold[\speakersGroup], \manifoldAtkDecode,
            [in: ~manifold[\atkBus], trim: ~manifold[\atkTrim]]);
        // The room, as the patch asks for one (its "rooms plug in").
        ~manifold[\rooms][\atk] = (
            says: { "the ATK's measured ears (CIPIC %)".format(~manifold[\ears]) },
            speaker: { |b|
                Synth(\manifoldAtk, [in: b, foa: ~manifold[\atkBus], rev: ~manifold[\roomBus],
                    latency: ~manifold[\atkLatency]], ~manifold[\speakersGroup]) },
            place: { |sp, pan, at, h|
                sp.set(\az, h[\az], \el, h[\el], \gain, h[\gain], \air, h[\air],
                       \wet, h[\wet], \inside, if (at[\inside]) { 1 } { 0 }) },
            free: {
                // Back to the head on any map still in this room, then let go.
                if (~manifold[\room] == \atk) {
                    ~manifold[\room] = \head;
                    (~manifold[\maps] ? []).do { |m| if (m[\names].notNil) { ~manifold[\speak].(m) } };
                };
                [\atkDecoder, \atkBus, \atkKernel].do { |k|
                    ~manifold[k] !? (_.free); ~manifold[k] = nil };
                ~manifold[\rooms].removeAt(\atk);
            });
        "manifold: the ATK's room is ready -- ears: CIPIC %".format(~manifold[\ears]).postln;
    };
};
)
