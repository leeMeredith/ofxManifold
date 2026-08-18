#pragma once

// ofxManifold — minimal JSON.
//
// Why not vendor a JSON library:
//
// src/core may depend on glm and nothing else, so anything the io layer needs
// has to be vendored alongside it and carried by every consumer. nlohmann/json
// is twenty-five thousand lines to read a schema that is four keys deep and
// which we control both ends of. openFrameworks already bundles it as ofJson,
// so the WRAPPER can use that freely -- but the kernel test suite must build
// with a compiler and nothing else, and that is the constraint that decides it.
//
// What this supports: objects, arrays, strings, numbers (including exponents),
// true, false, null, and the standard two-character escapes. What it does not:
// \u escapes beyond passing them through verbatim, and any of the extensions
// people wish were in JSON. A manifold file that needs those is a manifold file
// doing something it should not.
//
// The parser reports the byte offset of a failure. A file that fails to load
// with "unexpected character" and no position is a file you have to bisect by
// hand, which is a bad hour for someone whose show opens tonight.

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ofxManifold {
namespace json {

class Value;
using Object = std::map<std::string, Value>;
using Array  = std::vector<Value>;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Value() : type_(Type::Null) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(double n) : type_(Type::Number), num_(n) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(Array a) : type_(Type::Array), arr_(std::move(a)) {}
    Value(Object o) : type_(Type::Object), obj_(std::move(o)) {}

    Type type() const { return type_; }
    bool isNull()   const { return type_ == Type::Null; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray()  const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    double             asNumber() const { return num_; }
    float              asFloat()  const { return static_cast<float>(num_); }
    bool               asBool()   const { return bool_; }
    const std::string& asString() const { return str_; }
    const Array&       asArray()  const { return arr_; }
    const Object&      asObject() const { return obj_; }

    bool has(const std::string& k) const {
        return type_ == Type::Object && obj_.find(k) != obj_.end();
    }
    const Value& operator[](const std::string& k) const {
        static const Value none;
        auto it = obj_.find(k);
        return (it == obj_.end()) ? none : it->second;
    }

private:
    Type        type_;
    bool        bool_ = false;
    double      num_  = 0.0;
    std::string str_;
    Array       arr_;
    Object      obj_;
};

struct ParseResult {
    Value       value;
    bool        ok = false;
    std::string error;
    std::size_t offset = 0;
};

namespace detail {

struct Parser {
    const std::string& s;
    std::size_t i = 0;
    std::string err;

    explicit Parser(const std::string& src) : s(src) {}

    void ws() {
        while (i < s.size() &&
               (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
            ++i;
        }
    }

    bool fail(const std::string& m) { if (err.empty()) err = m; return false; }

    bool parseValue(Value& out) {
        ws();
        if (i >= s.size()) return fail("unexpected end of input");
        switch (s[i]) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': {
                std::string v;
                if (!parseString(v)) return false;
                out = Value(v);
                return true;
            }
            case 't':
                if (s.compare(i, 4, "true") == 0) { i += 4; out = Value(true); return true; }
                return fail("expected true");
            case 'f':
                if (s.compare(i, 5, "false") == 0) { i += 5; out = Value(false); return true; }
                return fail("expected false");
            case 'n':
                if (s.compare(i, 4, "null") == 0) { i += 4; out = Value(); return true; }
                return fail("expected null");
            default: return parseNumber(out);
        }
    }

    bool parseObject(Value& out) {
        Object o;
        ++i;                             // '{'
        ws();
        if (i < s.size() && s[i] == '}') { ++i; out = Value(o); return true; }
        while (true) {
            ws();
            if (i >= s.size() || s[i] != '"') return fail("expected key string");
            std::string key;
            if (!parseString(key)) return false;
            ws();
            if (i >= s.size() || s[i] != ':') return fail("expected ':'");
            ++i;
            Value v;
            if (!parseValue(v)) return false;
            // A duplicate key is silently last-wins in most parsers. Here it is
            // an error: in a manifold file it means two nodes claimed the same
            // slot, and quietly discarding one is how a map loads without the
            // region the author was looking for.
            if (o.find(key) != o.end()) return fail("duplicate key: " + key);
            o[key] = v;
            ws();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') { ++i; break; }
            return fail("expected ',' or '}'");
        }
        out = Value(o);
        return true;
    }

    bool parseArray(Value& out) {
        Array a;
        ++i;                             // '['
        ws();
        if (i < s.size() && s[i] == ']') { ++i; out = Value(a); return true; }
        while (true) {
            Value v;
            if (!parseValue(v)) return false;
            a.push_back(v);
            ws();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == ']') { ++i; break; }
            return fail("expected ',' or ']'");
        }
        out = Value(a);
        return true;
    }

    bool parseString(std::string& out) {
        ++i;                             // opening quote
        std::string v;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '"') { ++i; out = v; return true; }
            if (c == '\\') {
                ++i;
                if (i >= s.size()) return fail("unterminated escape");
                switch (s[i]) {
                    case '"':  v += '"';  break;
                    case '\\': v += '\\'; break;
                    case '/':  v += '/';  break;
                    case 'b':  v += '\b'; break;
                    case 'f':  v += '\f'; break;
                    case 'n':  v += '\n'; break;
                    case 'r':  v += '\r'; break;
                    case 't':  v += '\t'; break;
                    case 'u':
                        // Passed through verbatim rather than decoded. Node
                        // names are identifiers; a file relying on this is
                        // doing something the format does not intend.
                        v += "\\u";
                        break;
                    default: return fail("bad escape");
                }
                ++i;
                continue;
            }
            v += c;
            ++i;
        }
        return fail("unterminated string");
    }

    bool parseNumber(Value& out) {
        const std::size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        bool digits = false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            ++i; digits = true;
        }
        if (i < s.size() && s[i] == '.') {
            ++i;
            while (i < s.size() &&
                   std::isdigit(static_cast<unsigned char>(s[i]))) {
                ++i; digits = true;
            }
        }
        if (!digits) return fail("expected a number");
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
            bool ed = false;
            while (i < s.size() &&
                   std::isdigit(static_cast<unsigned char>(s[i]))) {
                ++i; ed = true;
            }
            if (!ed) return fail("exponent has no digits");
        }
        out = Value(std::strtod(s.substr(start, i - start).c_str(), nullptr));
        return true;
    }
};

} // namespace detail

inline ParseResult parse(const std::string& text) {
    detail::Parser p(text);
    ParseResult r;
    if (!p.parseValue(r.value)) {
        r.ok = false; r.error = p.err; r.offset = p.i;
        return r;
    }
    p.ws();
    if (p.i != text.size()) {
        r.ok = false; r.error = "trailing content after value"; r.offset = p.i;
        return r;
    }
    r.ok = true;
    return r;
}

// ---- writing -------------------------------------------------------------

// Nine significant digits. Not seventeen, and not a round number chosen by
// taste: nine is the smallest precision that round-trips a 32-bit float
// exactly, so save -> load -> save is byte-stable and a loaded manifold
// evaluates bit-identically to the one that was saved.
//
// This is the same concern as DECISIONS.md D-006 from the other side. There the
// problem was emitting MORE precision than was reproducible; here it would be
// emitting less than is sufficient.
inline std::string number(float v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    return buf;
}

inline std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;      break;
        }
    }
    return out + "\"";
}

} // namespace json
} // namespace ofxManifold
