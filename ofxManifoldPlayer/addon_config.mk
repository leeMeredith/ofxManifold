# ofxManifoldPlayer -- the pieces the players are built from, no openFrameworks.
#
# Round 3B builds several small players (PLAN-round3.md, section J), each
# useful on its own, and collects what they prove here: first, OSC out that
# grows and shrinks with the map. Kept out of ofxManifold for the same reason
# as ofxManifoldEditor: the kernel holds mechanisms with no meaning, and a
# player's choices -- what to send, how often -- are meaning.

meta:
	ADDON_NAME = ofxManifoldPlayer
	ADDON_DESCRIPTION = Player pieces for ofxManifold maps: OSC out
	ADDON_AUTHOR = Lee Meredith
	ADDON_TAGS = "manifold" "player" "osc" "spatial audio"
	ADDON_URL = https://github.com/leeMeredith/ofxManifold

common:
	ADDON_DEPENDENCIES = ofxManifold
