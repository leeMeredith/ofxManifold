// Round 3B-3a: the room on headphones -- the patch's own ~manifold[\speakerAt]
// and ~manifold[\hear] (the part between "// ---- room" and "// ---- end of
// room") against tests/vectors/room.vec, written by tests/ref/reference_room.py.
// No server.
//
//     sclang tests/sc/check_room.scd         (make sc runs it)
//
// Prints a line per vector and "N/N GREEN" or "RED", then exits.

(
var root = thisProcess.nowExecutingPath.dirname.dirname.dirname;
var patch = File.readAllString(root +/+ "example-osc/supercollider/manifold-receive.scd");
var vec = File.readAllString(root +/+ "tests/vectors/room.vec");
var from = patch.find("// ---- room"), to = patch.find("// ---- end of room");
var pass = 0, fail = 0;
var close = { |a, b| (a - b).abs <= (1e-4 * b.abs.max(1)) };
// An angle: the same direction, whichever way round it is said.
var sameAngle = { |a, b| (a - b).wrap(-pi, pi).abs <= 1e-4 };
var keys = [\az, \el, \dist, \delayL, \delayR, \gainL, \gainR, \shelfL, \shelfR,
            \notch, \air, \wet];
var check = { |name, ok, why|
    if (ok) { pass = pass + 1; "  pass  %".format(name).postln }
    { fail = fail + 1; "  FAIL  % -- %".format(name, why).postln };
};

~manifold = ();
patch[from..(to - 1)].interpret;

vec.split($\n).do { |line|
    if (line.beginsWith("HEAR ")) {
        var parts = line.split($|);
        var head = parts[0].split($ ).reject(_.isEmpty);
        var heights = parts[1].split($ ).reject(_.isEmpty).collect(_.asFloat);
        var rest = parts[2].split($ ).reject(_.isEmpty);
        var ex = rest.detectIndex { |w| w == "EXPECT" };
        var size = rest[0..2].collect(_.asFloat), facing = rest[3].asFloat;
        var want = rest[(ex + 1)..].collect(_.asFloat);
        var o = (placed: head[2] == "1", x: head[3].asFloat, y: head[4].asFloat,
                 z: head[5].asFloat);
        var got = ~manifold[\hear].(~manifold[\speakerAt].(o, heights, size), facing);
        var why = "";
        keys.do { |k, i|
            var ok = if (k == \az) { sameAngle.(got[k], want[i]) } { close.(got[k], want[i]) };
            if (ok.not and: { why.isEmpty }) { why = "% is %, want %".format(k, got[k], want[i]) };
        };
        check.(head[1], why.isEmpty, why);
    };
};
"%/% %".format(pass, pass + fail, if (fail == 0) { "GREEN" } { "RED" }).postln;
0.exit;
)
