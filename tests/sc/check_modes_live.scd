// Round 3B-2h: manifold-modes.scd, every block run as someone would run it,
// against the patch on a server with a map playing. A choice before START
// warns and does nothing; START starts the patch, and a second START leaves
// it alone; each choice sets its whole group -- and only its group -- to
// what it says, retunes the voices (or builds them again, for another
// sound), and they play what the patch's voicing says they should; RESET
// puts back the patch's own settings; STOP stops it.
//
//     sclang tests/sc/check_modes_live.scd     (make sc-live runs it)
//
// Boots the default server: on a Mac, your sound card -- silent, nothing
// plays loud enough to hear. Prints "N/N GREEN" or "RED", then exits.

(
var root = thisProcess.nowExecutingPath.dirname.dirname.dirname;
var modesPath = root +/+ "example-osc/supercollider/manifold-modes.scd";
var lines = File.readAllString(modesPath).split($\n);
var me = NetAddr.localAddr;
var pass = 0, fail = 0;
var check = { |name, ok, why|
    if (ok) { pass = pass + 1; "  pass  %".format(name).postln }
    { fail = fail + 1; "  FAIL  % -- %".format(name, why ? "").postln };
};
var close = { |a, b| (a - b).abs <= (1e-3 * b.abs.max(1)) };
// Each group's settings: a choice sets all of its own, and none of another's.
var groups = (
    'START AND STOP': [],
    'NOTES': [\music, \fit, \span],
    'SHARED OUTPUTS': [\quiet],
    'SOUND': [\sound, \bright],
    'POINTS': [\points, \players, \carrier, \depth]);
var every = groups.values.flatten;
// What each choice is said to be, written again here: a block whose words
// and settings part ways is caught.
var meant = (
    'PROGRESSION (the patch\'s own)': (music: \progression, fit: true, span: 12),
    'SOMBER': (music: \somber, fit: true, span: 12),
    'REGISTERS, CLOSE': (music: \registers, fit: true, span: 12),
    'REGISTERS, WIDE': (music: \registers, fit: true, span: 24),
    'REGISTERS, AN OCTAVE EACH': (music: \registers, fit: false, span: 12),
    'SERIES': (music: \series, fit: true, span: 12),
    'SHARED SOFT (the patch\'s own)': (quiet: 0.4),
    'SHARED FAINT': (quiet: 0.1),
    'SHARED FULL': (quiet: 1.0),
    'TONES (the patch\'s own)': (sound: \tone, bright: true),
    'TONES, EVEN': (sound: \tone, bright: false),
    'NOISE BANDS': (sound: \noise, bright: true),
    'TOGETHER (the patch\'s own)': (points: \together, players: 1, carrier: \fm, depth: 3),
    'CARRIER, GENTLE': (points: \carrier, players: 1, carrier: \fm, depth: 1),
    'CARRIER, DEEP': (points: \carrier, players: 1, carrier: \fm, depth: 6),
    'RING': (points: \carrier, players: 1, carrier: \ring, depth: 3),
    'TWO PLAY, THE REST SHAPE': (points: \carrier, players: 2, carrier: \fm, depth: 3));
// The blocks, as someone would select them: "(" to ")", each with its group
// (the last "// ====" heading) and its title (the first line of the comment
// just above it).
var blocks = List.new, group, title, body, blank = true;
var run = { |b|
    // Run as the IDE runs a block from this file: from the file's own place.
    var was = thisProcess.nowExecutingPath, ok = true;
    thisProcess.nowExecutingPath = modesPath;
    try { b[\code].interpret } { |e| ok = false; e.errorString.postln };
    thisProcess.nowExecutingPath = was;
    ok;
};
lines.do { |line|
    case
    { line.beginsWith("// ====") } {
        group = line.copyRange(8, line.size - 1).split($:).first;
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
"% blocks in manifold-modes.scd".format(blocks.size).postln;
check.("groups_known", blocks.every { |b| groups[b[\group]].notNil },
    blocks.collect(_[\group]).asSet.asString);

fork {
    var start = blocks.detect { |b| b[\title] == "START" };
    var stop = blocks.detect { |b| b[\title] == "STOP" };
    var reset = blocks.detect { |b| b[\title] == "RESET" };
    var choices = blocks.select { |b| b[\group] != 'START AND STOP' };
    var m, calls = List.new, timeout = 0, settings, read, initial;
    settings = { every.collect { |k| [k, ~manifold[k]] }.flatten.asDict };
    // What the server's voices are playing.
    read = {
        var got = m[\voices].collect { () };
        m[\voices].do { |v, i|
            [\freq, \centre, \gain, \fm, \ratio].do { |k| v.get(k, { |x| got[i][k] = x }) };
        };
        0.4.wait;
        got;
    };

    // Before START: a warning, no error.
    check.("choice_before_start_warns", run.(choices.first) and: { ~manifold.isNil },
        "an error, or something started");
    check.("start_starts", run.(start), "START threw");
    while { (s.serverRunning.not or: { ~manifold[\hello].isNil }) and: { timeout < 100 } } {
        0.2.wait; timeout = timeout + 1;
    };
    1.wait;
    // A map, as example-osc sends it: three outputs over two levels; its
    // roll call every second, as the app's, so the patch keeps it.
    Routine { loop { me.sendMsg("/manifold/map", "/manifold/check", 0, 3, 5, 1); 1.wait } }.play;
    0.3.wait;
    me.sendMsg("/manifold/check/outputs", 3, "FL", 0, "FL-2", 1, "mix", 2);
    0.3.wait;
    me.sendMsg("/manifold/check/shape", 2, 0.0, 0.25, 3,
        "FL", 0, 0, 1, 1, 0.2, 0.8, 0.0,
        "FL-2", 1, 1, 1, 1, 0.2, 0.8, 0.25,
        "mix", 2, -1, 0, 0, 0.0, 0.0, 0.0);
    me.sendMsg("/manifold/check/levels", 0.01, 0.01, 0.0);
    0.4.wait;
    m = ~manifold[\maps]["/manifold/check"];
    check.("map_playing", m.notNil and: { m[\voices].size == 3 }, "no map");
    if (m.notNil) {
        initial = settings.();
        // START again: already playing, nothing restarted.
        run.(start);
        0.3.wait;
        check.("start_again_leaves_it", ~manifold[\maps]["/manifold/check"] === m,
            "the patch started again");
        // Every choice the way it is meant to be used: one after another.
        ~manifold[\choose] = { |f| { |what, st| calls.add(st); f.(what, st) } }.(~manifold[\choose]);
        choices.do { |b|
            var before = settings.(), ids = m[\voices].collect(_.nodeID), call, after, got, want;
            var own = groups[b[\group]], why = "";
            calls.clear;
            if (run.(b).not) { why = "threw" };
            0.3.wait;
            call = calls.first;
            after = settings.();
            case
            { why.notEmpty } { }
            { calls.size != 1 } { why = "% calls to choose".format(calls.size) }
            { call.keys != own.asSet } { why = "sets % -- its group is %".format(call.keys, own) }
            { meant[b[\title].asSymbol] != call } {
                why = "sets % -- said to be %".format(call, meant[b[\title].asSymbol]);
            }
            { own.any { |k| after[k] != call[k] } } { why = "not set: %".format(after) }
            { (every.asSet - own.asSet).any { |k| after[k] != before[k] } } {
                why = "another group's setting changed";
            }
            { (call[\sound].notNil and: { call[\sound] != before[\sound] })
                != (m[\voices].collect(_.nodeID) != ids) } {
                why = "voices built again % (sound % to %)".format(
                    m[\voices].collect(_.nodeID) != ids, before[\sound], call[\sound]);
            };
            if (why.isEmpty) {
                got = read.();
                want = ~manifold[\voicing].(m[\shape], m[\lowest], 3);
                if (got.every { |g, i|
                    close.(g[\gain], want[i][\gain]) and: {
                        if (~manifold[\sound] == \noise) { close.(g[\centre], want[i][\freq] * 2) }
                                                         { close.(g[\freq], want[i][\freq]) }
                    }
                }.not) { why = "the voices play % -- want %".format(got, want) };
            };
            check.("% / %".format(b[\group], b[\title]), why.isEmpty, why);
        };
        // RESET: every group as the patch starts.
        run.(reset);
        0.3.wait;
        check.("reset_puts_back", settings.() == initial,
            "% -- want %".format(settings.(), initial));
        check.("every_group_chosen", groups.keys.every { |g|
            g == 'START AND STOP' or: { choices.any { |b| b[\group] == g } } }, "");
        check.("every_choice_run", meant.keys == choices.collect { |b| b[\title].asSymbol }.asSet,
            choices.collect(_[\title]).asString);
    };
    // STOP last: it stops this routine's clock too, so the checks after it
    // wait for nothing.
    run.(stop);
    check.("stop_stops", ~manifold[\playing].().not, "still playing");
    "%/% %".format(pass, pass + fail, if (fail == 0) { "GREEN" } { "RED" }).postln;
    0.exit;
};
)
