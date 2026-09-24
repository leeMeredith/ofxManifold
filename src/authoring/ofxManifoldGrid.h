#pragma once

// ofxManifold — authoring grids.
//
// Where a node lands when it is placed or dragged. Three modes, and free-hand
// is one of them (PLAN-authoring.md decision B): the editor calls snap()
// everywhere, and in free mode that is the identity. No `if (snapping)`
// scattered through drag, place and duplicate handling, which is how a code
// path gets missed and free-hand quietly snaps in one place.
//
// This is how a map is MADE, not how one evaluates, so it is not core. It is
// pure arithmetic, so it gets the kernel's treatment anyway: a Python
// reference, vectors, mutation gates, glm only.
//
// DOUBLE PRECISION internally. Authoring is not a hot path -- one pointer, a
// few hundred comparisons at most -- and computing in double keeps exact ties
// exact, so the lowest-address rule behaves identically here and in the
// reference. The float noise floor that forced kAreaEpsilon up (D-001) would
// otherwise turn genuine ties into coin flips.

#include <glm/vec2.hpp>

#include <cmath>
#include <cstdint>
#include <utility>

namespace ofxManifold {

enum class GridMode { Free, Lattice, Polar };

// (i, j) on a lattice; (ring, spoke) on a polar grid. Invalid in free mode,
// which has no addresses -- so duplicate detection there has to fall back to
// a distance tolerance (decision F).
struct GridAddress {
    int  a = 0;
    int  b = 0;
    bool valid = false;

    bool operator==(const GridAddress& o) const {
        return valid == o.valid && (!valid || (a == o.a && b == o.b));
    }
    bool operator!=(const GridAddress& o) const { return !(*this == o); }
};

struct SnapResult {
    glm::vec2   point{0.0f, 0.0f};
    GridAddress address;
};

class Grid {
public:
    Grid() = default;

    // ---- construction ----------------------------------------------------

    static Grid free() { return Grid(); }

    // Points origin + i*u + j*v, rotated by `angle` radians about the origin.
    //
    // u and v are the two basis vectors. One implementation gives square,
    // rectangular, triangular, hexagonal and sheared grids by choosing them.
    // Rotation changes where the grid sits, not what it is (decision H).
    static Grid lattice(glm::vec2 u, glm::vec2 v,
                        glm::vec2 origin = {0.0f, 0.0f}, float angle = 0.0f) {
        Grid g;
        g.mode_ = GridMode::Lattice;
        const double c = std::cos(double(angle)), s = std::sin(double(angle));
        // Rotated basis, as columns.
        g.b00_ = c * u.x - s * u.y;  g.b10_ = s * u.x + c * u.y;
        g.b01_ = c * v.x - s * v.y;  g.b11_ = s * v.x + c * v.y;
        g.ox_ = origin.x;  g.oy_ = origin.y;
        g.reduce();
        return g;
    }

    // Rings of spacing `dr` around `centre`, `spokes` equally spaced, spoke 0
    // at `angle` radians. A five-spoke grid at a quarter turn puts a point at
    // the top -- the star question answered by rotation, not by a default.
    static Grid polar(glm::vec2 centre, float dr, int spokes,
                      float angle = 0.0f) {
        Grid g;
        g.mode_ = GridMode::Polar;
        g.ox_ = centre.x;  g.oy_ = centre.y;
        g.dr_ = dr;
        g.spokes_ = (spokes < 1) ? 1 : spokes;
        g.angle_ = angle;
        return g;
    }

    GridMode mode() const { return mode_; }

    // Smallest distance between neighbouring grid points, the unit hysteresis
    // is measured in. Zero in free mode.
    double spacing() const {
        if (mode_ == GridMode::Polar) return dr_;
        if (mode_ == GridMode::Lattice) {
            const double lu = std::hypot(r00_, r10_);
            const double lv = std::hypot(r01_, r11_);
            return lu < lv ? lu : lv;
        }
        return 0.0;
    }

    // ---- addresses -------------------------------------------------------

    glm::vec2 point(GridAddress addr) const {
        if (!addr.valid || mode_ == GridMode::Free) return {0.0f, 0.0f};
        if (mode_ == GridMode::Lattice) {
            return {float(ox_ + b00_ * addr.a + b01_ * addr.b),
                    float(oy_ + b10_ * addr.a + b11_ * addr.b)};
        }
        if (addr.a == 0) return {float(ox_), float(oy_)};
        const double t = double(angle_)
                       + addr.b * 2.0 * kPi / double(spokes_);
        return {float(ox_ + addr.a * dr_ * std::cos(t)),
                float(oy_ + addr.a * dr_ * std::sin(t))};
    }

    // ---- snapping --------------------------------------------------------

    // The NEAREST grid point, not the rounded one (decision C).
    //
    // Measured before this was written: rounding alone picks a non-nearest
    // point 16% of the time on a triangular grid and 22% on a five-spoke
    // polar grid -- roughly one click in five landing on a neighbour of the
    // point it was beside.
    SnapResult snap(glm::vec2 p) const {
        if (mode_ == GridMode::Lattice) return snapLattice(p);
        if (mode_ == GridMode::Polar)   return snapPolar(p);
        SnapResult r;
        r.point = p;               // free: the identity, exactly
        return r;
    }

    // Snap while dragging, keeping `current` until the pointer is CLEARLY
    // closer to another point -- by more than dead * spacing (decision I).
    //
    // Near-ties happen constantly during a drag, exact ties almost never. This
    // is what stops a snap flickering between two points on a boundary.
    SnapResult snapHysteresis(glm::vec2 p, GridAddress current,
                              float dead) const {
        const SnapResult s = snap(p);
        if (mode_ == GridMode::Free || !current.valid
            || s.address == current) {
            return s;
        }
        const glm::vec2 cq = point(current);
        const double dc = dist(p, cq);
        const double ds = dist(p, s.point);
        if (dc <= ds + double(dead) * spacing()) {
            SnapResult keep;
            keep.point = cq;
            keep.address = current;
            return keep;
        }
        return s;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    // Distances closer than this are a tie. On doubles, well above the noise
    // of the arithmetic here and far below any spacing an author would use.
    static constexpr double kTie = 1e-9;

    static double dist(glm::vec2 a, glm::vec2 b) {
        const double dx = double(a.x) - double(b.x);
        const double dy = double(a.y) - double(b.y);
        return std::sqrt(dx * dx + dy * dy);
    }

    // Is (d, addr) better than the best so far?
    //
    // Strictly closer wins. Within kTie, the LOWER address wins, compared as
    // (a, b). Written out rather than left to std::round(), because Python
    // rounds halves to even and C++ rounds them away from zero -- round(0.5)
    // is 0 in one and 1 in the other (decision L).
    static bool better(double d, int a, int b,
                       double bestD, int bestA, int bestB, bool have) {
        if (!have) return true;
        if (d < bestD - kTie) return true;
        if (std::fabs(d - bestD) <= kTie) {
            if (a != bestA) return a < bestA;
            return b < bestB;
        }
        return false;
    }

    // Lagrange-Gauss reduction of the basis, done once.
    //
    // Checking the eight neighbours of the rounded point finds the nearest for
    // every sensible grid, but a strongly sheared basis still missed 43 times
    // in 6,000 -- the true nearest point sat further out. Reducing first makes
    // one step each way always sufficient (decision L).
    //
    // The unimodular matrix U is tracked so a point found in the REDUCED
    // basis can be reported at its address in the ORIGINAL one. Addresses are
    // in terms of the basis the author gave, not the one used internally.
    void reduce() {
        r00_ = b00_; r10_ = b10_; r01_ = b01_; r11_ = b11_;
        u00_ = 1; u01_ = 0; u10_ = 0; u11_ = 1;
        for (int guard = 0; guard < 64; ++guard) {
            double nu = r00_ * r00_ + r10_ * r10_;
            double nv = r01_ * r01_ + r11_ * r11_;
            if (nu > nv) {                      // swap columns
                std::swap(r00_, r01_); std::swap(r10_, r11_);
                std::swap(u00_, u01_); std::swap(u10_, u11_);
                std::swap(nu, nv);
            }
            const double mu = std::floor(
                (r00_ * r01_ + r10_ * r11_) / nu + 0.5);
            if (mu == 0.0) break;
            const long m = long(mu);
            r01_ -= mu * r00_;  r11_ -= mu * r10_;
            u01_ -= m * u00_;   u11_ -= m * u10_;
        }
        const double det = r00_ * r11_ - r01_ * r10_;
        i00_ =  r11_ / det;  i01_ = -r01_ / det;
        i10_ = -r10_ / det;  i11_ =  r00_ / det;
    }

    SnapResult snapLattice(glm::vec2 p) const {
        const double px = double(p.x) - ox_, py = double(p.y) - oy_;
        const double kx = i00_ * px + i01_ * py;
        const double ky = i10_ * px + i11_ * py;
        const long k0 = long(std::floor(kx + 0.5));
        const long k1 = long(std::floor(ky + 0.5));

        bool have = false;
        double bestD = 0.0;
        int bestA = 0, bestB = 0;
        for (long d0 = -1; d0 <= 1; ++d0) {
            for (long d1 = -1; d1 <= 1; ++d1) {
                const long q0 = k0 + d0, q1 = k1 + d1;
                const double x = ox_ + r00_ * q0 + r01_ * q1;
                const double y = oy_ + r10_ * q0 + r11_ * q1;
                const double dx = double(p.x) - x, dy = double(p.y) - y;
                const double d = std::sqrt(dx * dx + dy * dy);
                // Address in the ORIGINAL basis.
                const int a = int(u00_ * q0 + u01_ * q1);
                const int b = int(u10_ * q0 + u11_ * q1);
                if (better(d, a, b, bestD, bestA, bestB, have)) {
                    have = true; bestD = d; bestA = a; bestB = b;
                }
            }
        }
        SnapResult r;
        r.address = {bestA, bestB, true};
        r.point = point(r.address);
        return r;
    }

    // Every ring out to just past the pointer's own, every spoke.
    //
    // NOT a neighbour search around the rounded ring. Between two spokes the
    // closest radius is R cos(pi / spokes), which with few spokes lies well
    // inward: at ring 10 of a five-spoke grid the nearest point is on ring 8.
    // A search of one ring either side would miss it. The cost is a few
    // hundred comparisons at most, which an editor with one pointer does not
    // notice.
    SnapResult snapPolar(glm::vec2 p) const {
        const double r = dist(p, {float(ox_), float(oy_)});
        const int top = int(r / dr_) + 2;

        // The centre, where angle is undefined.
        bool have = true;
        double bestD = r;
        int bestA = 0, bestB = 0;

        for (int ring = 1; ring <= top; ++ring) {
            for (int spoke = 0; spoke < spokes_; ++spoke) {
                const glm::vec2 q = point({ring, spoke, true});
                const double d = dist(p, q);
                if (better(d, ring, spoke, bestD, bestA, bestB, have)) {
                    bestD = d; bestA = ring; bestB = spoke;
                }
            }
        }
        SnapResult res;
        res.address = {bestA, bestB, true};
        res.point = point(res.address);
        return res;
    }

    GridMode mode_ = GridMode::Free;

    // lattice: given basis (rotated), reduced basis, its inverse, and the
    // unimodular map from reduced coordinates to original addresses
    double b00_ = 0, b01_ = 0, b10_ = 0, b11_ = 0;
    double r00_ = 0, r01_ = 0, r10_ = 0, r11_ = 0;
    double i00_ = 0, i01_ = 0, i10_ = 0, i11_ = 0;
    long   u00_ = 1, u01_ = 0, u10_ = 0, u11_ = 1;

    // shared: lattice origin or polar centre
    double ox_ = 0, oy_ = 0;

    // polar
    double dr_ = 0.1;
    int    spokes_ = 8;
    float  angle_ = 0.0f;
};

} // namespace ofxManifold
