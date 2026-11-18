// Round 3B-2b: example-osc's SuperCollider patch, its musical behaviours
// checked without a server -- and (3B-2g) what several points do. Runs the patch's own functions -- the part
// between "// ---- music" and "// ---- end of music" -- against
// tests/vectors/music.vec, written by tests/ref/reference_music.py.
//
//     sclang tests/sc/check_music.scd        (make sc runs it)
//
// Prints a line per vector and "N/N GREEN" or "RED", then exits.

(
var root = thisProcess.nowExecutingPath.dirname.dirname.dirname;
var patch = File.readAllString(root +/+ "example-osc/supercollider/manifold-receive.scd");
var vec = File.readAllString(root +/+ "tests/vectors/music.vec");
var from = patch.find("// ---- music"), to = patch.find("// ---- end of music");
var shapes = Dictionary.new, pass = 0, fail = 0;
var close = { |a, b| (a - b).abs <= (1e-4 * b.abs.max(1)) };
var check = { |name, ok, why|
    if (ok) { pass = pass + 1; "  pass  %".format(name).postln }
    { fail = fail + 1; "  FAIL  % -- %".format(name, why).postln };
};

~manifold = ();
// The patch's settings, as it sets them at its top.
~manifold[\scale] = [0, 2, 4, 7, 9];
~manifold[\span] = 12;
~manifold[\quiet] = 0.4;
patch[from..(to - 1)].interpret;

vec.split($\n).do { |line|
    var words = line.split($ ).reject(_.isEmpty);
    case
    { line.beginsWith("SHAPE ") } {
        var name = words[1];
        var bar = line.find(" | ");
        var heights = line[0..(bar - 1)].split($ ).reject(_.isEmpty)[2..].collect(_.asFloat);
        var outs = line[(bar + 3)..].split($,).collect { |item|
            var p = item.split($:);
            (name: p[0], level: p[1].asInteger, levels: p[2].asInteger,
             placed: p[3] == "1", x: p[4].asFloat, y: p[5].asFloat, z: p[6].asFloat)
        };
        shapes[name] = (heights: heights, outputs: outs);
    }
    { line.beginsWith("VOICES ") } {
        var name = words[1], shape = if (words[2] == "-") { nil } { shapes[words[2]] };
        var count = words[3].asInteger, lowest = words[6].asInteger;
        var want = line[(line.find("EXPECT ") + 7)..].split($;).collect { |v|
            v.split($ ).reject(_.isEmpty).collect(_.asFloat)
        };
        var got, why = "";
        ~manifold[\music] = words[4].asSymbol;
        ~manifold[\fit] = words[5] == "1";
        got = ~manifold[\voicing].(shape, lowest, count);
        if (got.size != want.size) { why = "% voices, want %".format(got.size, want.size) } {
            got.do { |g, i|
                var w = want[i];
                if (why.isEmpty and: {
                    close.(g[\freq], w[0]).not or: { close.(g[\pan], w[1]).not }
                    or: { close.(g[\gain], w[2]).not } }) {
                    why = "voice %: % % %, want % % %".format(i, g[\freq], g[\pan], g[\gain],
                        w[0], w[1], w[2]);
                };
            };
        };
        check.(name, why.isEmpty, why);
    }
    { line.beginsWith("MIX ") } {
        // 3B-2g: MIX name n mode players carrier depth | points EXPECT ...
        var name = words[1], n = words[2].asInteger;
        var bar = line.find(" | "), ex = line.find(" EXPECT ");
        var pts = line[(bar + 3)..(ex - 1)].split($;).collect { |item|
            var p = item.split($ ).reject(_.isEmpty).first.split($:);
            (number: p[0].asInteger, bright: p[1].asFloat,
             u: if (p[2] == "-") { nil } { p[2].asFloat },
             levels: p[3].split($,).collect(_.asFloat))
        };
        var want = line[(ex + 8)..].split($;).collect { |v|
            v.split($ ).reject(_.isEmpty).collect(_.asFloat)
        };
        var got, why = "";
        ~manifold[\points] = words[3].asSymbol;
        ~manifold[\players] = words[4].asInteger;
        ~manifold[\carrier] = words[5].asSymbol;
        ~manifold[\depth] = words[6].asFloat;
        got = ~manifold[\mix].(pts, n);
        if (got.size != want.size) { why = "% voices, want %".format(got.size, want.size) } {
            got.do { |g, i|
                var w = want[i];
                var have = [g[\amp], g[\bright], g[\fm], g[\ring], g[\ratio]];
                if (why.isEmpty and: { have.every { |x, k| close.(x, w[k]) }.not }) {
                    why = "voice %: %, want %".format(i, have, w);
                };
            };
        };
        check.(name, why.isEmpty, why);
    }
    { line.beginsWith("BRIGHT ") } {
        var name = words[1], shape = if (words[2] == "-") { nil } { shapes[words[2]] };
        var z = words[3].asFloat, u = if (words[4] == "-") { nil } { words[4].asFloat };
        var want = words[6].asFloat;
        var got = ~manifold[\brightness].(shape, z, u);
        check.(name, close.(got, want), "% want %".format(got, want));
    };
};
// The whole patch compiles -- the server part too, not run.
check.("whole_patch_compiles", thisProcess.interpreter.compile(patch).notNil,
       "a syntax error in the patch");
// The shape message as the player sends it (OSC.md): i L, L heights, i N,
// then per output s name, i channel, i level, i levels, i placed, f x y z.
{
    var sh = ~manifold[\readShape].(['/manifold/room/shape', 2, 0.0, 0.25, 2,
        'FL', 0, 0, 1, 1, 0.2, 0.8, 0.0,
        'mix', 3, -1, 0, 0, 0.0, 0.0, 0.0]);
    var o = sh[\outputs];
    check.("read_shape_message",
        sh[\heights] == [0.0, 0.25] and: { o.size == 2 }
        and: { o[0][\name] == "FL" } and: { o[0][\placed] } and: { o[0][\x] == 0.2 }
        and: { o[1][\name] == "mix" } and: { o[1][\level] == -1 }
        and: { o[1][\levels] == 0 } and: { o[1][\placed].not },
        sh.asString);
}.value;
"\n%/%  %".format(pass, pass + fail, if (fail == 0) { "GREEN" } { "RED" }).postln;
0.exit;
)
