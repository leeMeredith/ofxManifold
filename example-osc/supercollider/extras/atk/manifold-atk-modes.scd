// ofxManifold -- the Ambisonic Toolkit's room: its choices (round 3B-3b).
//
// An extra: everything in supercollider/manifold-modes.scd works without
// this folder. This file adds one more room to choose, heard through
// measured ears, and says how to install what it needs.
//
// HOW TO USE THIS FILE
//
// Each block is a short piece between parentheses. Put the cursor anywhere
// inside one and press cmd-return; the post window says what happened.
//
//   1. Once: WHERE THINGS STAND says what is installed and what to do next.
//   2. START: runs the patch, then adds the ATK's room to it.
//   3. Start example-osc, put on headphones, and choose a block under THE
//      ATK'S ROOM. Every other choice -- notes, sound, points, the head's
//      room -- is in manifold-modes.scd: keep it open beside this file and
//      use both.
//   4. Stop with cmd-period, or STOP in manifold-modes.scd.


// ==== START ================================================================

// START -- the patch (if it is not playing already), then the ATK's room.
// Without the ATK installed it says so, and the patch plays on with its own
// rooms.
(
var here = thisProcess.nowExecutingPath, core;
if (here.isNil) {
    "manifold: save this file where it came, in supercollider/extras/atk, then START again".warn;
} {
    core = here.dirname.dirname.dirname +/+ "manifold-receive.scd";
    if (~manifold.isNil or: { ~manifold[\playing].isNil } or: { ~manifold[\playing].().not }) {
        core.load;
    };
    (here.dirname +/+ "manifold-atk.scd").load;
};
)


// ==== THE ATK'S ROOM =======================================================

// ATK, MEASURED EARS -- each speaker encoded at its direction and heard
// through ears measured on a dummy head (CIPIC subject 21, KEMAR), with the
// same distance and room as HEAD.
// Expect: front and behind, above and below, far clearer than HEAD -- walk
// the point round the room's corners, then climb a level.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("room: the ATK's measured ears", (room: \atk, facing: 0, reverb: 0.3));
};
)

// ATK, DRY -- the same, with no room.
// Expect: the plainest sense of where each speaker is.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("room: the ATK, dry", (room: \atk, facing: 0, reverb: 0));
};
)

// BACK TO THE HEAD -- the patch's own room, to compare.
// Expect: the same places, less clearly front or behind.
(
if (~manifold.isNil) { "manifold: run START first".warn } {
    ~manifold[\choose].("room: a head", (room: \head, facing: 0, reverb: 0.3));
};
)


// ==== INSTALLING THE ATK: once =============================================
// The Ambisonic Toolkit is SuperCollider's own spatial library, installed
// apart. It needs four things; WHERE THINGS STAND says which are here.
// README.md, beside this file, has the addresses to download each by hand.

// WHERE THINGS STAND -- yes or NO for each thing the ATK's room needs, and
// what to do next for each NO. Run it again after each step.
(
var atk = \Atk.asClass, git, say;
say = { |ok, what, next|
    var mark = if (ok) { "  yes  " } { "  NO   " };
    var tail = if (ok) { "" } { "  --  next: " ++ next };
    (mark ++ what ++ tail).postln;
};
git = "git --version".systemCmd == 0;
"\nThe ATK -- where things stand:".postln;
say.(git, "git", "in Terminal, run  xcode-select --install");
say.(\FoaPanB.asClass.notNil and: { \AtkKernelConv.asClass.notNil }, "sc3-plugins",
    "download them (README.md has the address), put the SC3plugins folder in "
    ++ Platform.userExtensionDir ++ ", then Language > Recompile Class Library");
say.(atk.notNil, "the toolkit (the atk-sc3 quark)",
    "run THE TOOLKIT below, then Language > Recompile Class Library");
say.(atk.notNil and: { File.exists(atk.userKernelDir +/+ "FOA" +/+ "decoders" +/+ "cipic") },
    "its ears (the kernels)", "run ITS EARS below");
say.(atk.notNil and: { File.exists(atk.userMatrixDir) }, "its matrices", "run ITS EARS below");
"".postln;
)

// THE TOOLKIT -- needs git. When the post window says "atk-sc3 installed",
// choose Language > Recompile Class Library (cmd-shift-L). "Language" is a
// menu in SuperCollider's menu bar, not a language to pick.
(
Quarks.install("https://github.com/ambisonictoolkit/atk-sc3.git");
)

// ITS EARS -- after the recompile: the measured ears, downloaded once, and
// its matrices. Then run START again, and choose ATK, MEASURED EARS.
(
Atk.downloadKernels;
Atk.downloadMatrices;
)
