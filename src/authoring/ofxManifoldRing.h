#pragma once

// ofxManifold — ordering a set of nodes into a ring.
//
// A region is an ORDERED ring, and the order decides the shape. When an author
// box-selects nodes to join, the selection has no order, so one has to be
// chosen. The obvious choice is angle around the centroid.
//
// Measured before this was written, over 200 shuffles of each shape:
//
//     quad, pentagon, star, deep star    recovered 200 / 200
//     L-shape                            recovered   0 / 200 -- and VALID
//
// Angle order recovers anything star-shaped about its centroid, which is every
// convex shape and both stars. For an L it produces a different ring that is
// still a legal region: no refusal, no error, just a shape the author did not
// draw. That silent substitution is why the editor offers two orderings rather
// than one -- angle order for a box selection, click order for nodes chosen
// one at a time -- and why it never falls back from one to the other without
// saying so.

#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace ofxManifold {

// Indices of `pts` in counter-clockwise angle order about their centroid.
//
// Ties -- points at the same angle -- are broken by distance from the centroid,
// nearer first, then by index, so the order is total and repeatable.
inline std::vector<std::size_t> orderByAngle(const std::vector<glm::vec2>& pts) {
    std::vector<std::size_t> idx(pts.size());
    std::iota(idx.begin(), idx.end(), std::size_t(0));
    if (pts.empty()) return idx;

    double cx = 0.0, cy = 0.0;
    for (const glm::vec2& p : pts) { cx += p.x; cy += p.y; }
    cx /= double(pts.size());
    cy /= double(pts.size());

    std::vector<double> ang(pts.size()), rad(pts.size());
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const double dx = double(pts[i].x) - cx;
        const double dy = double(pts[i].y) - cy;
        ang[i] = std::atan2(dy, dx);
        rad[i] = dx * dx + dy * dy;
    }
    std::stable_sort(idx.begin(), idx.end(),
        [&](std::size_t a, std::size_t b) {
            if (ang[a] != ang[b]) return ang[a] < ang[b];
            if (rad[a] != rad[b]) return rad[a] < rad[b];
            return a < b;
        });
    return idx;
}

} // namespace ofxManifold
