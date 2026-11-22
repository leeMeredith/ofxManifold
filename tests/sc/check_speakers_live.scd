// Round 3B-4a: manifold-speakers.scd, every block run as someone would run
// it, on a server with four speakers -- START told 4, as a reader would
// write it -- and a map playing four outputs: on channels 0, 1, 3 and 6, so
// speaker 3 has no output and the fourth output has no speaker. What comes
// out of each speaker is recorded and measured: CHECK's pulses counted on
// each, one for speaker 1, two for speaker 2 ...; each sound at each
// output's level on its own speaker and nowhere else; a gap silent; the
// output past the last speaker said and silent; on headphones every output
// folded left to right by where its node is, and moved when the node is;
// STOP silent and START again playing; START with another count starting
// the server again with that many, and the fourth output then heard.
//
//     sclang tests/sc/check_speakers_live.scd     (make sc-live runs it)
//
// Boots the default server: on a Mac, your sound card -- quiet tones and
// pulses for a few seconds. Prints "N/N GREEN" or "RED", then exits.

(
var root = thisProcess.nowExecutingPath.dirname.dirname.dirname;
var path = root +/+ "example-osc/supercollider/manifold-speakers.scd";
var lines = File.readAllString(path).split($\n);
var me = NetAddr.localAddr;
var pass = 0, fail = 0;
var check = { |name, ok, why|
    if (ok) { pass = pass + 1; "  pass  %".format(name).postln }
    { fail = fail + 1; "  FAIL  % -- %".format(name, why ? "").postln };
};
var near = { |a, b, tol = 0.05| (a - b).abs <= (tol * b.abs.max(0.01)) };
var quiet = { |a| a < 1e-4 };
// The map: name, channel, placed, x.
var outs = [["A", 0, 1, 0.0], ["B", 1, 1, 1.0], ["C", 3, 1, 0.5], ["D", 6, 1, 0.25]];
var blocks = List.new, group, title, body, blank = true;
var clock = TempoClock.new.permanent_(true);
var run = { |b, from, to|
    var ok = true, code = b[\code];
    if (from.notNil) { code = code.replace(from, to) };
    try { code.interpret } { |e| ok = false; e.errorString.postln };
    ok;
};
var block = { |t| blocks.detect { |b| b[\title] == t } };
lines.do { |line|
    case
    { line.beginsWith("// ====") } {
        group = line.copyRange(8, line.size - 1);
        group = group.select { |c| c.isAlpha or: { c == $ } }.stripWhiteSpace.asSymbol;
    }
    { line.beginsWith("// ") and: { blank } and: { body.isNil } } {
        title = line.copyRange(3, (line.find(" -- ") ? line.size) - 1);
    }
    { line == "(" } { body = List.new }
    { line == ")" } {
        blocks.add((group: group, title: title, code: body.join("\n")));
        body = nil;
    }
    { body.notNil } { body.add(line) };
    blank = line.stripWhiteSpace.isEmpty;
};
"% blocks in manifold-speakers.scd".format(blocks.size).postln;
check.("blocks", blocks.collect { |b| [b[\group], b[\title]] } == [
    ['SETUP', "DEVICES"], ['SETUP', "START"], ['SETUP', "STOP"],
    ['CHECK THE SPEAKERS', "CHECK"],
    ['A SOUND THROUGH THE MAP', "TEST TONE (START's own)"],
    ['A SOUND THROUGH THE MAP', "PINK NOISE"],
    ['A SOUND THROUGH THE MAP', "A SOUND FILE"],
    ['A SOUND THROUGH THE MAP', "THE INPUT"],
    ['A SOUND THROUGH THE MAP', "NOTHING"],
    ['WHERE IT PLAYS', "ON THE SPEAKERS (START's own)"],
    ['WHERE IT PLAYS', "ON HEADPHONES"]],
    blocks.collect { |b| [b[\group], b[\title]] }.asString);
check.("loads_nothing_else", File.readAllString(path).findRegexp("\\.load|loadRelative|Quarks|\\bAtk|Foa").isEmpty, "");

Routine {
    var sr, record, rms, m, levels, roll, player, shape, timeout = 0, sounds, file = "/tmp/speakers-check-sine.wav";
    var start = block.("START"), wait;
    // Each speaker's level: what comes out of channels 0 .. n-1, recorded.
    // `during`, if given, starts once the recording has.
    record = { |dur = 0.3, n = 4, during|
        var buf = Buffer.alloc(s, (sr * dur).asInteger, n), data;
        s.sync;
        Synth.tail(s, \speakersCheckRecord ++ n, [buf: buf]);
        during !? { during.() };
        (dur + 0.1).wait;
        buf.loadToFloatArray(action: { |a| data = a });
        while { data.isNil } { 0.05.wait };
        buf.free;
        n.collect { |c| data[c, (c + n)..] };
    };
    rms = { |x| (x.squared.sum / x.size).sqrt };
    levels = { |l| me.sendMsg(*(["/manifold/check/levels"] ++ l)); 0.15.wait };
    wait = { |test| var t = 0; while { test.().not and: { t < 150 } } { 0.1.wait; t = t + 1 } };

    // Before START: a warning, nothing started.
    ~speakers = nil;
    check.("choice_before_start_warns", run.(block.("TEST TONE (START's own)")) and: { ~speakers.isNil },
        "an error, or something started");
    check.("devices_lists_or_says", run.(block.("DEVICES")), "threw");

    // START, told four speakers.
    check.("start_starts", run.(start, "var speakers = 2;", "var speakers = 4;"), "threw");
    wait.({ ~speakers.notNil and: { ~speakers[\playing] == true } });
    check.("start_playing_four", s.serverRunning and: { ~speakers[\playing] == true }
        and: { s.options.numOutputBusChannels == 4 } and: { ~speakers[\sound] == \tone },
        "running % playing % outputs %".format(s.serverRunning, ~speakers !? (_[\playing]), s.options.numOutputBusChannels));
    sr = s.sampleRate;
    [2, 4, 6].do { |n|
        SynthDef(\speakersCheckRecord ++ n, { |buf| RecordBuf.ar(In.ar(0, n), buf, loop: 0, doneAction: 2) }).add;
    };
    SynthDef(\speakersCheckInject, { ReplaceOut.ar(NumOutputBuses.ir, SinOsc.ar(300) * 0.5) }).add;
    s.sync;
    {
        var g = ~speakers[\sources].nodeID;
        run.(start, "var speakers = 2;", "var speakers = 4;");
        0.5.wait;
        check.("start_again_leaves_it", ~speakers[\sources].nodeID == g and: { ~speakers[\playing] == true },
            "started again");
    }.value;

    // The map, as example-osc sends it: the roll call, the outputs and the
    // shape every second. (cmd-period clears every clock's queue, this
    // one's too: STOP below starts it again.)
    shape = outs.collect { |o| [o[0], o[1], 0, 1, o[2], o[3], 0.5, 0.0] }.flatten;
    roll = {
        Routine { loop {
            me.sendMsg("/manifold/map", "/manifold/check", 0, outs.size, 5, 1);
            me.sendMsg(*(["/manifold/check/outputs", outs.size] ++ outs.collect { |o| [o[0], o[1]] }.flatten));
            me.sendMsg(*(["/manifold/check/shape", 1, 0.0, outs.size] ++ shape));
            1.wait
        } }.play(clock)
    };
    player = roll.();
    1.5.wait;
    m = ~speakers[\maps]["/manifold/check"];
    check.("map_heard", m.notNil, "no map");
    if (m.isNil) { "%/% RED".format(pass, pass + fail).postln; 0.exit };
    check.("a_synth_per_speaker", m[\outs].collect { |o| o !? (_.defName) }
        == [\speakersOut, \speakersOut, \speakersOut, nil], m[\outs].asString);
    check.("past_the_last_speaker_said", m[\silent] == 1, m[\silent].asString);

    // CHECK, the map silent: speaker k pulses k times, and only speaker k.
    levels.([0, 0, 0, 0]);
    {
        var got, counts;
        got = record.(4.6, 4, { run.(block.("CHECK")) });
        counts = got.collect { |x|
            var n = 0, last = -1e9;
            x.do { |v, i| if (v.abs > 0.05) { if (i - last > (sr * 0.05)) { n = n + 1 }; last = i } };
            n
        };
        check.("check_counts", counts == [1, 2, 3, 4], counts.asString);
    }.value;

    // Each sound at each output's level, on its own speaker: speaker 3 (a
    // gap) silent.
    sounds = [
        ["TEST TONE (START's own)", nil, nil, 0.3 * 0.5.sqrt],
        ["PINK NOISE", nil, nil, nil],
        ["A SOUND FILE", "var path = \"\";", "var path = \"" ++ file ++ "\";", 0.3 * 0.5 * 0.5.sqrt],
        ["THE INPUT", nil, nil, 0.3 * 0.5 * 0.5.sqrt]];
    {
        var sig, sf = SoundFile.new;
        // A second of a sine at half scale: 480 cycles, a seamless loop.
        sig = Signal.fill(48000, { |i| (2pi * 480 * i / 48000).sin * 0.5 });
        sf.headerFormat = "WAV"; sf.sampleFormat = "float"; sf.numChannels = 1; sf.sampleRate = 48000;
        sf.openWrite(file); sf.writeData(sig); sf.close;
    }.value;
    // An empty path: said, the sound left as it was.
    run.(block.("TEST TONE (START's own)"));
    run.(block.("A SOUND FILE"));
    0.3.wait;
    check.("file_empty_path_said", ~speakers[\sound] == \tone, ~speakers[\sound].asString);
    Synth(\speakersCheckInject, target: s.defaultGroup, addAction: \addToHead);
    sounds.do { |snd|
        var full, got, why = "";
        run.(block.(snd[0]), snd[1], snd[2]);
        1.wait;
        levels.([1, 0.5, 0.25, 1]);
        got = record.(0.5).collect(rms);
        full = snd[3] ? got[0];
        case
        { got[0] < 0.01 } { why = "silent: %".format(got) }
        { near.(got[0], full).not } { why = "speaker 1 % -- want %".format(got[0], full) }
        { near.(got[1], full * 0.5).not } { why = "speaker 2 % -- want half of %".format(got[1], full) }
        { quiet.(got[2]).not } { why = "speaker 3, no output, %".format(got[2]) }
        { near.(got[3], full * 0.25).not } { why = "speaker 4 % -- want a quarter of %".format(got[3], full) };
        check.("sound_through_map / %".format(snd[0]), why.isEmpty, why);
    };
    // The levels followed.
    levels.([0, 1, 0, 0]);
    {
        var got = record.(0.3).collect(rms);
        check.("levels_followed", quiet.(got[0]) and: { got[1] > 0.05 } and: { quiet.(got[3]) }, got.asString);
    }.value;
    run.(block.("NOTHING"));
    0.3.wait;
    levels.([1, 1, 1, 1]);
    check.("nothing_silent", record.(0.3).collect(rms).every(quiet), "");

    // On headphones: every output, the one past the last speaker too, left to
    // right by where its node is.
    run.(block.("TEST TONE (START's own)"));
    run.(block.("ON HEADPHONES"));
    0.5.wait;
    check.("headphones_every_output", m[\outs].collect(_.defName) == (\speakersFold ! 4), m[\outs].asString);
    {
        var each = outs.collect { |o, i|
            var l = 0 ! 4;
            l[i] = 1;
            levels.(l);
            0.1.wait;
            record.(0.3, 2).collect(rms)
        };
        check.("headphones_left", each[0][0] > 0.1 and: { quiet.(each[0][1]) }, each[0].asString);
        check.("headphones_right", each[1][1] > 0.1 and: { quiet.(each[1][0]) }, each[1].asString);
        check.("headphones_middle", near.(each[2][0], each[2][1]) and: { each[2][0] > 0.1 }, each[2].asString);
        check.("headphones_past_last_speaker_heard", each[3][0] > (each[3][1] * 1.5) and: { each[3][1] > 0.01 },
            each[3].asString);
        // A node moved: A to the right.
        shape = outs.collect { |o| [o[0], o[1], 0, 1, o[2], if (o[0] == "A") { 1.0 } { o[3] }, 0.5, 0.0] }.flatten;
        levels.([1, 0, 0, 0]);
        1.2.wait;
        each = record.(0.3, 2).collect(rms);
        check.("headphones_follow_the_node", each[1] > 0.1 and: { quiet.(each[0]) }, each.asString);
    }.value;
    run.(block.("ON THE SPEAKERS (START's own)"));
    0.3.wait;
    check.("back_on_the_speakers", m[\outs].collect { |o| o !? (_.defName) }
        == [\speakersOut, \speakersOut, \speakersOut, nil], m[\outs].asString);

    // STOP: silence, the map let go; START again: playing, the map heard.
    run.(block.("STOP"));
    0.5.wait;
    levels.([1, 1, 1, 1]);
    check.("stop_silent", ~speakers[\playing] == false and: { record.(0.3).collect(rms).every(quiet) }
        and: { ~speakers[\maps].isEmpty }, ~speakers[\playing].asString);
    player = roll.();
    run.(start, "var speakers = 2;", "var speakers = 4;");
    wait.({ ~speakers[\playing] == true and: { ~speakers[\maps]["/manifold/check"].notNil } });
    1.5.wait;
    levels.([1, 0, 0, 0]);
    {
        var got = record.(0.3).collect(rms);
        check.("start_after_stop", near.(got[0], 0.3 * 0.5.sqrt) and: { quiet.(got[1]) }, got.asString);
    }.value;

    // START with seven: the server started again with seven outputs, and
    // the output on channel 6 has its speaker.
    run.(start, "var speakers = 2;", "var speakers = 7;");
    0.5.wait;
    wait.({ ~speakers[\playing] == true and: { s.serverRunning } and: { ~speakers[\count] == 7 } });
    1.5.wait;
    check.("start_seven_restarts", s.options.numOutputBusChannels == 7 and: { s.serverRunning }
        and: { ~speakers[\maps]["/manifold/check"].notNil },
        "outputs % maps %".format(s.options.numOutputBusChannels, ~speakers[\maps].keys));
    // The server itself, asked how many outputs it has -- not the setting.
    {
        var got;
        OSCFunc({ |msg| got = msg[3] }, '/speakersCheckOutputs').oneShot;
        { SendReply.kr(Impulse.kr(0), '/speakersCheckOutputs', NumOutputBuses.ir); FreeSelf.kr(Impulse.kr(0)); 0 }.play;
        1.wait;
        check.("server_has_seven", got == 7, got.asString);
    }.value;
    m = ~speakers[\maps]["/manifold/check"];
    check.("seven_speakers_none_silent", m.notNil and: { m[\silent] == 0 } and: { m[\outs].every(_.notNil) }, "");

    player.stop;
    "%/% %".format(pass, pass + fail, if (fail == 0) { "GREEN" } { "RED" }).postln;
    0.exit;
}.play(clock);
)
