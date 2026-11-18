// Round 3B-2f: points moved from outside, over a real socket. Listens on
// 127.0.0.1:LISTEN_PORT as example-osc's answer port does, hands each packet
// and its sender to PointControl (the room's prefix, its square floor, levels
// 0 to 0.5), sends every answer back over UDP, and prints what it applied --
// until /done. tests/check_control_udp.py sends from two real sockets with
// python-osc and checks the answers that come back and these lines against
// tests/ref/reference_control.py.

#include "../ofxManifoldPlayer/src/ofxManifoldPointControl.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

using namespace ofxManifold::player;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "control_udp LISTEN_PORT\n"); return 2; }
    const int port = std::atoi(argv[1]);
    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in me{};
    me.sin_family = AF_INET;
    me.sin_port = htons(std::uint16_t(port));
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, reinterpret_cast<sockaddr*>(&me), sizeof me) != 0) {
        std::perror("bind");
        return 2;
    }
    std::printf("listening\n");
    std::fflush(stdout);

    PointControl c;
    c.setPrefix("/manifold/room");
    c.setInside([](float x, float y) { return x >= 0.2f && x <= 0.8f && y >= 0.2f && y <= 0.8f; });
    c.setExtent(0.0f, 0.5f, 2.0f);

    std::uint8_t buf[2048];
    int k = 0;
    for (;;) {
        sockaddr_in from{};
        socklen_t len = sizeof from;
        const ssize_t n = recvfrom(sock, buf, sizeof buf, 0,
                                   reinterpret_cast<sockaddr*>(&from), &len);
        if (n <= 0) continue;
        OscMessage m;
        if (!decode(buf, std::size_t(n), m)) continue;
        if (m.address == "/done") break;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        // Time by arrival order: a tenth of a second apart.
        c.receive(0.1 * k++, m, OscPeer{ip, ntohs(from.sin_port)});
        for (const PointMove& mv : c.takeMoves()) {
            std::printf("MOVE %d", mv.point);
            if (mv.hasXY) std::printf(" xy %.6g %.6g", double(mv.x), double(mv.y));
            if (mv.hasZ) std::printf(" z %.6g", double(mv.z));
            if (mv.hasU) std::printf(" u %.6g", double(mv.u));
            std::printf("\n");
        }
        for (const PointEvent& e : c.takeEvents()) {
            std::printf("EVENT %s %d %s\n", e.kind == PointEvent::Taken ? "Taken" : "Returned",
                        e.point, e.why.empty() ? "-" : e.why.c_str());
        }
        for (const PointReply& r : c.takeReplies()) {
            const std::vector<std::uint8_t> bytes = encode(r.message);
            sockaddr_in to{};
            to.sin_family = AF_INET;
            to.sin_port = htons(std::uint16_t(r.to.port));
            inet_pton(AF_INET, r.to.ip.c_str(), &to.sin_addr);
            sendto(sock, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
        }
        std::fflush(stdout);
    }
    std::printf("COUNTS %ld %ld %s\n", c.accepted(), c.refused(),
                c.lastRefusal().empty() ? "-" : c.lastRefusal().c_str());
    close(sock);
    return 0;
}
