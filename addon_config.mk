meta:
	ADDON_NAME = ofxManifold
	ADDON_DESCRIPTION = Continuous preset morphing. Place presets as nodes, drag a point between them, get a weighted blend.
	ADDON_AUTHOR = Lee Meredith
	ADDON_TAGS = "interpolation" "mapping" "control" "spatial audio" "parameters"
	ADDON_URL = https://github.com/leeMeredith/ofxManifold

common:
	ADDON_INCLUDES = src
	ADDON_INCLUDES += src/core
	ADDON_INCLUDES += src/interpretation
	ADDON_INCLUDES += src/mapping
	ADDON_INCLUDES += src/io
	ADDON_INCLUDES += src/ofx

	# libs/glm is DELIBERATELY not on this list.
	#
	# openFrameworks already ships glm, and adding our vendored copy to the
	# include path would put two versions of the same headers in front of the
	# compiler. The vendored copy exists so the kernel test suite builds with
	# nothing but a compiler and make, on a machine with no openFrameworks
	# checkout. Inside an oF project, oF's copy is the right one.
	#
	# tests/ is excluded for the same class of reason: it contains main()
	# functions, and Project Generator would try to compile them into the app.
	ADDON_SOURCES_EXCLUDE = tests/%
	ADDON_SOURCES_EXCLUDE += libs/%
	ADDON_INCLUDES_EXCLUDE = tests/%
	ADDON_INCLUDES_EXCLUDE += libs/%
