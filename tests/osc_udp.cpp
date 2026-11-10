// ofxManifoldPlayer -- the OSC sender over a real socket (round 3B-1).
//
// Run by tests/check_osc_udp.py, which listens on SEND_PORT: this sends ten
// frames of a fixed run to it as UDP packets, the bytes encode() makes, then
// waits for a hello on LISTEN_PORT and says whether the sender counts it.
// POSIX sockets, test-only; the example app sends through ofxOsc.

#include "../ofxManifoldPlayer/src/ofxManifoldOsc.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

using namespace ofxManifold::player;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "osc_udp SEND_PORT LISTEN_PORT\n"); return 2; }
    const int sendPort = std::atoi(argv[1]), listenPort = std::atoi(argv[2]);

    const int in = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in me{};
    me.sin_family = AF_INET;
    me.sin_port = htons(std::uint16_t(listenPort));
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(in, reinterpret_cast<sockaddr*>(&me), sizeof me) != 0) {
        std::perror("bind");
        return 2;
    }
    const int out = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(std::uint16_t(sendPort));
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // The run check_osc_udp.py expects: per output on as well, a third
    // output arriving at the fifth frame, a value, the point moving.
    // Steps of a sixteenth, so every level is exact in float: an arm64 Mac
    // fuses 1 - 0.1*k into one rounding and a tenth came out a bit lower
    // than Python's two roundings. Sixteenths round the same either way.
    OscOut osc;
    osc.setPerOutput(true);
    osc.setMapName("room");             // protocol 2: named, with a roll call
    osc.setAnswerPort(listenPort);
    int packets = 0;
    for (int k = 0; k < 10; ++k) {
        const double now = k / 60.0;
        OscFrame f;
        const float step = float(k) / 16.0f;
        f.outputs.push_back({"front", 0, 1.0f - step});
        f.outputs.push_back({"left", 1, step});
        if (k >= 4) f.outputs.push_back({"rear", 3, 0.0f});
        f.values.push_back({"brightness", 0.5f});
        f.hasPosition = true;
        f.position = {step, 0.5f, 0.0f};
        for (const OscMessage& m : osc.frame(now, f)) {
            const auto bytes = encode(m);
            sendto(out, bytes.data(), bytes.size(), 0,
                   reinterpret_cast<sockaddr*>(&to), sizeof to);
            ++packets;
        }
    }
    std::printf("sent %d\n", packets);
    std::fflush(stdout);

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(in, &fds);
    timeval wait{3, 0};
    if (select(in + 1, &fds, nullptr, nullptr, &wait) > 0) {
        std::uint8_t buf[1536];
        const ssize_t n = recv(in, buf, sizeof buf, 0);
        OscMessage m;
        if (n > 0 && decode(buf, std::size_t(n), m)) osc.receive(1.0, m);
    }
    std::printf(osc.connected(1.0) ? "connected %s\n" : "not connected%s\n",
                osc.peer().c_str());
    close(in);
    close(out);
    return 0;
}
