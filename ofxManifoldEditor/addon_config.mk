# ofxManifoldEditor -- an editor for ofxManifold maps, as reusable pieces.
#
# Kept OUT of ofxManifold on purpose: everything in an addon's src/ is compiled
# into every project that uses it, and the editor carries opinions about keys,
# layout and workflow that ofxManifold's users should not have to take.
#
# An addon rather than a plain folder because Project Generator compiles only a
# project's own src/ and its addons (PLAN-editor.md decision A).

meta:
	ADDON_NAME = ofxManifoldEditor
	ADDON_DESCRIPTION = Editing logic and views for ofxManifold maps
	ADDON_AUTHOR = Lee Meredith
	ADDON_TAGS = "manifold" "editor" "spatial audio" "interpolation"
	ADDON_URL = https://github.com/leeMeredith/ofxManifold

common:
	ADDON_DEPENDENCIES = ofxManifold
