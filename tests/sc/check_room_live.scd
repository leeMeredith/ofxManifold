// Round 3B-3a: the room on headphones, heard -- the patch run whole on a
// server, a click played into one output's voice at a time, the two
// headphone channels recorded and measured -- the native room, and (3B-3b)
// the ATK's when it is installed. Not what the numbers say: what
// comes out. A speaker on the left arrives first, and loudest, on the left,
// by the head's delay; one ahead the same in both ears; one behind duller
// than one ahead; one near louder than one far; one overhead the same in
// both ears; an output with no place in the middle of the head; turned
// round, left is right; turned right, ahead is on the left; the stereo room panned, with no delay; the reverb
// heard only when asked for.
//
//     sclang tests/sc/check_room_live.scd     (make sc-live runs it)
//
// Boots the default server: on a Mac, your sound card -- a few quiet clicks.
// Prints "N/N GREEN" or "RED", then exits.

(
var root = thisProcess.nowExecutingPath.dirname.dirname.dirname;
var patch = File.readAllString(root +/+ "example-osc/supercollider/manifold-receive.scd");
var me = NetAddr.localAddr;
var pass = 0, fail = 0;
var check = { |name, ok, why|
    if (ok) { pass = pass + 1; "  pass  %".format(name).postln }
    { fail = fail + 1; "  FAIL  % -- %".format(name, why ? "").postln };
};
// The outputs: name, level, placed, x, y, z -- on the middle of three
// levels (the ears' height) but for one overhead, and one with no place.
var outs = [
    ["left", 1, 1, 0.1, 0.5, 0.25], ["right", 1, 1, 0.9, 0.5, 0.25],
    ["ahead", 1, 1, 0.5, 0.9, 0.25], ["behind", 1, 1, 0.5, 0.1, 0.25],
    ["near", 1, 1, 0.6, 0.5, 0.25], ["overhead", 2, 1, 0.5, 0.5, 0.5],
    ["mix", -1, 0, 0.0, 0.0, 0.0]];
patch.interpret;
fork {
    var m, at, sr, click, timeout = 0;
    while { (s.serverRunning.not or: { ~manifold[\hello].isNil }) and: { timeout < 100 } } {
        0.2.wait; timeout = timeout + 1;
    };
    1.wait;
    sr = s.sampleRate;
    SynthDef(\roomCheckClick, { |out| Out.ar(out, Impulse.ar(0) * Line.kr(1, 1, 0.05, doneAction: 2)) }).add;
    SynthDef(\roomCheckRecord, { |buf| RecordBuf.ar(In.ar(0, 2), buf, loop: 0, doneAction: 2) }).add;
    s.sync;
    Routine { loop { me.sendMsg("/manifold/map", "/manifold/check", 0, outs.size, 5, 1); 1.wait } }.play;
    0.5.wait;
    me.sendMsg(*(["/manifold/check/outputs", outs.size]
        ++ outs.collect { |o, i| [o[0], i] }.flatten));
    0.3.wait;
    me.sendMsg(*(["/manifold/check/shape", 3, 0.0, 0.25, 0.5, outs.size]
        ++ outs.collect { |o, i| [o[0], i, o[1], if (o[1] >= 0) { 1 } { 0 }, o[2], o[3], o[4], o[5]] }.flatten));
    0.5.wait;
    m = ~manifold[\maps]["/manifold/check"];
    check.("map_playing", m.notNil and: { m[\speakers].size == outs.size }, "no map");
    if (m.isNil) { "0/1 RED".postln; 0.exit };
    ~manifold[\choose].("check: dry", (reverb: 0));
    0.3.wait;

    // One click into output k's voice; the headphones recorded. (onset in
    // samples, peak) for each ear, and what came after 0.1 s.
    click = { |k, long = 0.3|
        var buf = Buffer.alloc(s, (sr * long).asInteger, 2), data, l, r, onset, peak, tail;
        s.sync;
        Synth.tail(s, \roomCheckRecord, [buf: buf]);
        0.01.wait;
        Synth.head(~manifold[\voicesGroup], \roomCheckClick, [out: m[\buses][k]]);
        (long + 0.1).wait;
        buf.loadToFloatArray(action: { |a| data = a });
        while { data.isNil } { 0.05.wait };
        buf.free;
        l = data[0, 2..]; r = data[1, 3..];
        peak = max(l.abs.maxItem, r.abs.maxItem);
        onset = { |x| x.detectIndex { |v| v.abs > (0.1 * peak) } ? inf };
        tail = { |x| x[(sr * 0.1).asInteger..].abs.maxItem };
        (onsetL: onset.(l), onsetR: onset.(r), peakL: l.abs.maxItem, peakR: r.abs.maxItem,
         tail: max(tail.(l), tail.(r)))
    };
    at = { |name| outs.detectIndex { |o| o[0] == name } };

    {
        var left = click.(at.("left")), right = click.(at.("right"));
        var ahead = click.(at.("ahead")), behind = click.(at.("behind"));
        var near = click.(at.("near")), over = click.(at.("overhead")), mix = click.(at.("mix"));
        var h = ~manifold[\hear].(~manifold[\speakerAt].(m[\shape][\outputs][at.("left")],
            m[\shape][\heights], ~manifold[\roomSize]), 0);
        var itd = h[\delayR].max(h[\delayL]) * sr;
        check.("left_first_on_the_left",
            ((left[\onsetR] - left[\onsetL]) - itd).abs <= 3 and: { itd > 20 },
            "% samples later on the right, want %".format(left[\onsetR] - left[\onsetL], itd));
        check.("left_loudest_on_the_left", left[\peakL] > (left[\peakR] * 1.5), left.asString);
        check.("right_first_on_the_right",
            ((right[\onsetL] - right[\onsetR]) - itd).abs <= 3, right.asString);
        check.("right_loudest_on_the_right", right[\peakR] > (right[\peakL] * 1.5), right.asString);
        check.("ahead_the_same_in_both_ears",
            (ahead[\onsetL] - ahead[\onsetR]).abs <= 1
            and: { (ahead[\peakL] / ahead[\peakR] - 1).abs < 0.05 }, ahead.asString);
        check.("behind_duller_than_ahead",
            behind[\peakL] < (ahead[\peakL] * 0.95) and: { (behind[\onsetL] - behind[\onsetR]).abs <= 1 },
            "behind %, ahead %".format(behind[\peakL], ahead[\peakL]));
        check.("near_louder_than_far", near[\peakR] > (right[\peakR] * 1.5),
            "near %, far %".format(near[\peakR], right[\peakR]));
        check.("overhead_the_same_in_both_ears",
            (over[\onsetL] - over[\onsetR]).abs <= 1 and: { (over[\peakL] / over[\peakR] - 1).abs < 0.05 },
            over.asString);
        check.("no_place_in_the_middle",
            (mix[\onsetL] - mix[\onsetR]).abs <= 1 and: { (mix[\peakL] / mix[\peakR] - 1).abs < 0.05 },
            mix.asString);
        check.("dry_has_no_tail", left[\tail] < 1e-4, "tail %".format(left[\tail]));
    }.value;

    // Turned round: the left speaker is now on the right.
    ~manifold[\choose].("check: turned round", (facing: 180));
    0.3.wait;
    {
        var left = click.(at.("left"));
        check.("turned_round_left_is_right",
            left[\onsetR] < left[\onsetL] and: { left[\peakR] > (left[\peakL] * 1.5) }, left.asString);
    }.value;
    // Turned to the right: what is ahead is now on the left.
    ~manifold[\choose].("check: facing right", (facing: 90));
    0.3.wait;
    {
        var ahead = click.(at.("ahead"));
        check.("facing_right_ahead_is_on_the_left",
            ahead[\onsetL] < ahead[\onsetR] and: { ahead[\peakL] > (ahead[\peakR] * 1.5) }, ahead.asString);
    }.value;
    ~manifold[\choose].("check: facing the front", (facing: 0));

    // The reverb: a tail after the click, only when asked for.
    ~manifold[\choose].("check: a room", (reverb: 0.5));
    0.3.wait;
    {
        var right = click.(at.("right"), 0.6);
        check.("reverb_has_a_tail", right[\tail] > 1e-3, "tail %".format(right[\tail]));
    }.value;
    ~manifold[\choose].("check: dry", (reverb: 0));

    // The stereo room: panned, the same moment in both ears.
    ~manifold[\choose].("check: stereo", (room: \stereo));
    0.5.wait;
    {
        var left = click.(at.("left"));
        check.("stereo_panned_no_delay",
            left[\onsetL] == left[\onsetR] and: { left[\peakL] > (left[\peakR] * 3) }, left.asString);
        check.("stereo_speakers", m[\speakers].every { |sp| sp.defName == \manifoldStereo },
            m[\speakers].collect(_.defName).asString);
    }.value;
    ~manifold[\choose].("check: head", (room: \head));
    0.5.wait;
    check.("head_speakers_again", m[\speakers].every { |sp| sp.defName == \manifoldHead },
        m[\speakers].collect(_.defName).asString);
    // The ATK's room (3B-3b), when the ATK is installed: measured ears.
    if (~manifold[\atkReady]) {
        var head = click.(at.("ahead"));
        ~manifold[\choose].("check: the ATK", (room: \atk, reverb: 0));
        0.5.wait;
        {
            var left = click.(at.("left")), right = click.(at.("right"));
            var ahead = click.(at.("ahead")), behind = click.(at.("behind"));
            var near = click.(at.("near")), over = click.(at.("overhead")), mix = click.(at.("mix"));
            check.("atk_speakers", m[\speakers].every { |sp| sp.defName == \manifoldAtk },
                m[\speakers].collect(_.defName).asString);
            check.("atk_left_on_the_left", left[\peakL] > (left[\peakR] * 3), left.asString);
            check.("atk_right_on_the_right", right[\peakR] > (right[\peakL] * 3), right.asString);
            check.("atk_ahead_about_even", (ahead[\peakL] / ahead[\peakR]).inclusivelyBetween(0.8, 1.25),
                ahead.asString);
            check.("atk_behind_unlike_ahead",
                (behind[\peakL] + behind[\peakR]) < ((ahead[\peakL] + ahead[\peakR]) * 0.9),
                "behind %, ahead %".format(behind, ahead));
            check.("atk_near_louder_than_far", near[\peakR] > (right[\peakR] * 1.5),
                "near %, far %".format(near[\peakR], right[\peakR]));
            check.("atk_overhead_about_even", (over[\peakL] / over[\peakR]).inclusivelyBetween(0.9, 1.1),
                over.asString);
            check.("atk_no_place_in_the_middle_with_the_room",
                mix[\onsetL] == mix[\onsetR] and: { mix[\peakL] == mix[\peakR] }
                and: { (mix[\onsetL] - ahead[\onsetL]).abs < 250 },
                "middle at %, the room at %".format(mix[\onsetL], ahead[\onsetL]));
            check.("atk_as_loud_as_the_head",
                ((ahead[\peakL] + ahead[\peakR]) / (head[\peakL] + head[\peakR])).inclusivelyBetween(0.67, 1.5),
                "atk %, head %".format(ahead, head));
        }.value;
        ~manifold[\choose].("check: the ATK turned round", (facing: 180));
        0.3.wait;
        {
            var left = click.(at.("left"));
            check.("atk_turned_round_left_is_right", left[\peakR] > (left[\peakL] * 3), left.asString);
        }.value;
        ~manifold[\choose].("check: the head", (room: \head, facing: 0));
        0.5.wait;
    } { "  (the ATK is not installed: its room not checked)".postln };
    // The ATK's room asked for without the ATK: the head, and a warning.
    {
        var was = ~manifold[\atkReady];
        ~manifold[\atkReady] = false;
        ~manifold[\choose].("check: the ATK, not there", (room: \atk));
        0.5.wait;
        check.("no_atk_stays_with_the_head", ~manifold[\room] == \head
            and: { m[\speakers].every { |sp| sp.defName == \manifoldHead } }, ~manifold[\room].asString);
        ~manifold[\atkReady] = was;
    }.value;
    "%/% %".format(pass, pass + fail, if (fail == 0) { "GREEN" } { "RED" }).postln;
    0.exit;
};
)
