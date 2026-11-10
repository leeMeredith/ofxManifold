#include "ofxManifoldOsc.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ofxManifold {
namespace player {

namespace {

void putString(std::vector<std::uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
    while (out.size() % 4 != 0) out.push_back(0);
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(std::uint8_t(v >> 24));
    out.push_back(std::uint8_t(v >> 16));
    out.push_back(std::uint8_t(v >> 8));
    out.push_back(std::uint8_t(v));
}

std::uint32_t get32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16)
         | (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

// A string at `at`: null-terminated, then zeros to a multiple of four.
bool getString(const std::uint8_t* data, std::size_t size, std::size_t& at,
               std::string& out) {
    std::size_t end = at;
    while (end < size && data[end] != 0) ++end;
    if (end >= size) return false;
    out.assign(reinterpret_cast<const char*>(data + at), end - at);
    std::size_t next = end + 1;
    next += (4 - next % 4) % 4;
    if (next > size) return false;
    for (std::size_t k = end; k < next; ++k) {
        if (data[k] != 0) return false;
    }
    at = next;
    return true;
}

} // namespace

std::string OscMessage::tags() const {
    std::string t = ",";
    for (const OscArg& a : args) t += a.type;
    return t;
}

std::vector<std::uint8_t> encode(const OscMessage& m) {
    std::vector<std::uint8_t> out;
    putString(out, m.address);
    putString(out, m.tags());
    for (const OscArg& a : m.args) {
        if (a.type == 'i') {
            put32(out, std::uint32_t(a.i));
        } else if (a.type == 'f') {
            std::uint32_t bits;
            std::memcpy(&bits, &a.f, 4);
            put32(out, bits);
        } else {
            putString(out, a.s);
        }
    }
    return out;
}

bool decode(const std::uint8_t* data, std::size_t size, OscMessage& out) {
    out = OscMessage{};
    if (size % 4 != 0 || size < 8) return false;
    std::size_t at = 0;
    std::string address, tags;
    if (!getString(data, size, at, address) || address.empty()
        || address[0] != '/') {
        return false;
    }
    if (!getString(data, size, at, tags) || tags.empty() || tags[0] != ',') {
        return false;
    }
    OscMessage m;
    m.address = address;
    for (std::size_t k = 1; k < tags.size(); ++k) {
        const char t = tags[k];
        if (t == 'i' || t == 'f') {
            if (at + 4 > size) return false;
            const std::uint32_t bits = get32(data + at);
            at += 4;
            if (t == 'i') {
                m.args.push_back(OscArg::integer(std::int32_t(bits)));
            } else {
                float f;
                std::memcpy(&f, &bits, 4);
                m.args.push_back(OscArg::number(f));
            }
        } else if (t == 's') {
            std::string s;
            if (!getString(data, size, at, s)) return false;
            m.args.push_back(OscArg::text(s));
        } else {
            return false;
        }
    }
    if (at != size) return false;
    out = m;
    return true;
}

std::string oscName(const std::string& name) {
    static const std::string bad = " #*,/?[]{}";
    std::string out = name;
    for (char& c : out) {
        if (bad.find(c) != std::string::npos) c = '_';
    }
    return out.empty() ? std::string("_") : out;
}

// ---------------------------------------------------------------------------

void OscOut::setPrefix(const std::string& root) {
    std::string q = root;
    while (!q.empty() && q.back() == '/') q.pop_back();
    root_ = q.empty() ? std::string("/manifold") : q;
    rename();
}

void OscOut::setMapName(const std::string& name) {
    mapName_ = name.empty() ? std::string("untitled") : oscName(name);
    rename();
}

// The addresses change: everything is said again under the new ones.
void OscOut::rename() {
    const std::string p = root_ + "/" + mapName_;
    if (p == prefix_) return;
    prefix_ = p;
    reset();
}

void OscOut::setRate(double perSecond) {
    rate_ = std::min(1000.0, std::max(1.0, perSecond));
}

void OscOut::reset() {
    sentYet_ = false;
    describedYet_ = false;
    described_.clear();
    describeAsked_ = false;
    refreshedYet_ = false;
    sentOut_.clear();
    sentValue_.clear();
}

std::vector<OscMessage> OscOut::frame(double now, const OscFrame& f) {
    std::vector<OscMessage> msgs;
    if (sentYet_ && now < nextSend_ - 1e-9) return msgs;
    // The next send one period on -- or, after a late frame, one period from
    // now: never a burst to catch up.
    const double period = 1.0 / rate_;
    if (!sentYet_ || now > nextSend_ + period) {
        nextSend_ = now + period;
    } else {
        nextSend_ += period;
    }
    sentYet_ = true;

    const std::string& p = prefix_;
    std::vector<std::pair<std::string, int>> layout;
    for (const OscOutput& o : f.outputs) layout.emplace_back(o.name, o.channel);
    if (!describedYet_ || layout != described_ || describeAsked_
        || now >= lastDescribe_ + kDescribeEvery - 1e-9) {
        msgs.push_back({root_ + "/map", {OscArg::text(p),
                                         OscArg::integer(std::int32_t(answerPort_)),
                                         OscArg::integer(std::int32_t(layout.size())),
                                         OscArg::integer(kProtocol)}});
        OscMessage m{p + "/outputs", {OscArg::integer(std::int32_t(layout.size()))}};
        for (const auto& nc : layout) {
            m.args.push_back(OscArg::text(nc.first));
            m.args.push_back(OscArg::integer(nc.second));
        }
        msgs.push_back(m);
        described_ = layout;
        describedYet_ = true;
        describeAsked_ = false;
        lastDescribe_ = now;
    }
    if (packed_) {
        OscMessage m{p + "/levels", {}};
        for (const OscOutput& o : f.outputs) m.args.push_back(OscArg::number(o.level));
        msgs.push_back(m);
    }
    const bool refresh = !refreshedYet_
                      || now >= lastRefresh_ + kRefreshEvery - 1e-9;
    auto changed = [refresh](const std::map<std::string, float>& sent,
                             const std::string& name, float v) {
        const auto it = sent.find(name);
        return refresh || it == sent.end() || std::fabs(v - it->second) > kChanged;
    };
    if (perOutput_) {
        for (const OscOutput& o : f.outputs) {
            if (!changed(sentOut_, o.name, o.level)) continue;
            msgs.push_back({p + "/out/" + oscName(o.name), {OscArg::number(o.level)}});
            sentOut_[o.name] = o.level;
        }
    }
    if (packed_ && !f.values.empty()) {
        OscMessage m{p + "/values", {OscArg::integer(std::int32_t(f.values.size()))}};
        for (const OscValue& v : f.values) {
            m.args.push_back(OscArg::text(v.name));
            m.args.push_back(OscArg::number(v.value));
        }
        msgs.push_back(m);
    }
    if (perOutput_) {
        for (const OscValue& v : f.values) {
            if (!changed(sentValue_, v.name, v.value)) continue;
            msgs.push_back({p + "/value/" + oscName(v.name), {OscArg::number(v.value)}});
            sentValue_[v.name] = v.value;
        }
        if (refresh) {
            refreshedYet_ = true;
            lastRefresh_ = now;
        }
    }
    if (f.hasPosition) {
        msgs.push_back({p + "/position", {OscArg::number(f.position.x),
                                          OscArg::number(f.position.y),
                                          OscArg::number(f.position.z)}});
    }
    return msgs;
}

void OscOut::receive(double now, const OscMessage& m) {
    if (m.address == prefix_ + "/describe" || m.address == root_ + "/describe") {
        describeAsked_ = true;
    } else if (m.address == prefix_ + "/hello" || m.address == root_ + "/hello") {
        helloYet_ = true;
        lastHello_ = now;
        peer_ = (!m.args.empty() && m.args[0].type == 's') ? m.args[0].s
                                                            : std::string();
    }
}

bool OscOut::connected(double now) const {
    return helloYet_ && now - lastHello_ < kHelloLasts;
}

} // namespace player
} // namespace ofxManifold
