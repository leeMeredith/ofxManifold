#pragma once

// ofxManifold — umbrella header.
//
// Include this from an openFrameworks project. It pulls in the kernel, the
// interpretation and mapping layers, serialization, and the renderer.
//
// If you want the kernel WITHOUT openFrameworks -- in a command-line tool, a
// plugin, or a test harness -- include the individual headers under src/core
// instead. Nothing there includes ofMain.h, which is checked on every build by
// `make headers`.

#include "core/ofxManifoldTypes.h"
#include "core/ofxManifoldTriangle.h"
#include "core/ofxManifold2D.h"
#include "core/ofxManifoldEvaluator.h"

#include "interpretation/ofxManifoldCurves.h"
#include "interpretation/ofxManifoldSpread.h"
#include "interpretation/ofxManifoldBlend.h"
#include "interpretation/ofxManifoldInterpolate.h"

#include "mapping/ofxManifoldMapping.h"

#include "io/ofxManifoldJSON.h"
#include "io/ofxManifoldSerialize.h"

#include "ofx/ofxManifoldRenderer.h"
