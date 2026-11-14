// ofxManifoldPlayer -- the bridge's contract over a real socket (round 3B-2).
//
// tests/check_phase_udp.py plays a stand-in for ofxScPhaseBridge's
// bridge.scd: /phase ramps, wrapping each bar, and /beat echoes. This program
// reads them as example-osc does -- decode, readPhase into a PhaseClock,
// readBeat as one step of a walk at the clock's count -- and, on /done,
// prints what it ended with for the script to check against the reference.
//
// POSIX sockets, test-only.

#include "ofxManifoldClocks.h"
#include "ofxManifoldOsc.h"
#include "sources/ofxManifoldMotion.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

using namespace ofxManifold;
using namespace ofxManifold::player;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "phase_udp LISTEN_PORT\n"); return 2; }
    const int port = std::atoi(argv[1]);
    const int in = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in me{};
    me.sin_family = AF_INET;
    me.sin_port = htons(std::uint16_t(port));
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(in, reinterpret_cast<sockaddr*>(&me), sizeof me) != 0) {
        std::perror("bind");
        return 2;
    }
    std::printf("listening\n");
    std::fflush(stdout);

    PhaseClock clock;
    WalkSettings s;
    s.seed = 5;
    s.steps = StepTiming::clicks();
    s.travel = 0.0;
    RandomWalk walk(s);
    int phases = 0, beats = 0;
    std::uint8_t buf[2048];
    for (;;) {
        const ssize_t n = recv(in, buf, sizeof buf, 0);
        if (n <= 0) continue;
        OscMessage m;
        if (!decode(buf, std::size_t(n), m)) continue;
        if (m.address == "/done") break;
        // The arrival time does not matter to the count, only to "heard":
        // phases are numbered as they come.
        if (readPhase(m, clock, double(phases))) { ++phases; continue; }
        int step = 0;
        if (readBeat(m, step)) {
            walk.addStep(clock.count());
            ++beats;
        }
    }
    const glm::dvec3 p = walk.atPrecise(clock.count());
    std::printf("phases %d beats %d count %.17g steps %zu at %.17g %.17g %.17g\n", phases,
                beats, clock.count(), walk.stepsBy(clock.count()), p.x, p.y, p.z);
    close(in);
    return 0;
}
