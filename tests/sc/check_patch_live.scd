// Round 3B-2b: example-osc's SuperCollider patch, run whole on a server --
// the wiring, not the music: a roll call, outputs, the shape, levels and a
// position sent to sclang as example-osc sends them, and the synths read
// back. Before the shape the voices climb; once it comes they are retuned,
// not rebuilt; volumes follow /levels and brightness /position.
//
//     sclang tests/sc/check_patch_live.scd     (make sc-live runs it)
//
// Boots the default server: on a Mac, your sound card -- it sounds for about
// a second, quietly. Prints "N/N GREEN" or "RED", then exits.

(
var root = thisProcess.nowExecutingPath.dirname.dirname.dirname;
var patch = File.readAllString(root +/+ "example-osc/supercollider/manifold-receive.scd");
var me = NetAddr.localAddr;
var pass = 0, fail = 0;
var check = { |name, ok, why|
    if (ok) { pass = pass + 1; "  pass  %".format(name).postln }
    { fail = fail + 1; "  FAIL  % -- %".format(name, why).postln };
};
var close = { |a, b| (a - b).abs <= (1e-3 * b.abs.max(1)) };
patch.interpret;
fork {
    var m, read, want, shape, timeout = 0;
    while { (s.serverRunning.not or: { ~manifold[\hello].isNil }) and: { timeout < 100 } } {
        0.2.wait; timeout = timeout + 1;
    };
    1.wait;
    // Every voice's freq amp pan bright gain, as the server has them.
    read = {
        var got = m[\voices].collect { () };
        m[\voices].do { |v, i|
            [\freq, \amp, \pan, \bright, \gain].do { |k| v.get(k, { |x| got[i][k] = x }) };
        };
        0.5.wait;
        got;
    };
    me.sendMsg("/manifold/map", "/manifold/check", 0, 3, 3);
    0.3.wait;
    me.sendMsg("/manifold/check/outputs", 3, "FL", 0, "FL-2", 1, "mix", 2);
    0.3.wait;
    m = ~manifold[\maps]["/manifold/check"];
    check.("map_started", m.notNil, "no map for /manifold/check");
    if (m.notNil) {
        var before = read.(), ids = m[\voices].collect(_.nodeID), after;
        want = ~manifold[\voicing].(nil, m[\lowest], 3);
        check.("climb_before_shape",
            before.every { |g, i| close.(g[\freq], want[i][\freq]) and: { close.(g[\pan], want[i][\pan]) } },
            before.asString);
        me.sendMsg("/manifold/check/shape", 2, 0.0, 0.25, 3,
            "FL", 0, 0, 1, 1, 0.2, 0.8, 0.0,
            "FL-2", 1, 1, 1, 1, 0.2, 0.8, 0.25,
            "mix", 2, -1, 0, 0, 0.0, 0.0, 0.0);
        0.3.wait;
        me.sendMsg("/manifold/check/levels", 0.05, 0.025, 0.0);
        me.sendMsg("/manifold/check/position", 0.3, 0.7, 0.2, 0.8);
        0.3.wait;
        after = read.();
        shape = m[\shape];
        want = ~manifold[\voicing].(shape, m[\lowest], 3);
        check.("shape_read", shape.notNil and: { shape[\heights] == [0.0, 0.25] }, shape.asString);
        check.("retuned_not_rebuilt", m[\voices].collect(_.nodeID) == ids, "voices rebuilt");
        check.("tuned_to_shape",
            after.every { |g, i|
                close.(g[\freq], want[i][\freq]) and: { close.(g[\pan], want[i][\pan]) }
                and: { close.(g[\gain], want[i][\gain]) } },
            after.asString);
        check.("levels_followed",
            close.(after[0][\amp], 0.05) and: { close.(after[1][\amp], 0.025) }, after.asString);
        check.("brightness_by_u",
            after.every { |g| close.(g[\bright], 0.8) }, after.asString);
    };
    ~manifold[\drop].("/manifold/check");
    "\n%/%  %".format(pass, pass + fail, if (fail == 0) { "GREEN" } { "RED" }).postln;
    0.exit;
};
)
