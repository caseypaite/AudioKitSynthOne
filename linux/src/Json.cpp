//
//  Json.cpp
//  AudioKitSynthOne - Linux port
//

#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace s1 {

static const JsonValue kNullValue;

const JsonValue &JsonValue::operator[](const std::string &key) const {
    if (type != Type::Object) return kNullValue;
    auto it = objectValue.find(key);
    return it == objectValue.end() ? kNullValue : it->second;
}

bool JsonValue::contains(const std::string &key) const {
    return type == Type::Object && objectValue.find(key) != objectValue.end();
}

double JsonValue::asDouble(double fallback) const {
    switch (type) {
        case Type::Number: return numberValue;
        case Type::Bool:   return boolValue ? 1.0 : 0.0;
        case Type::String: {
            try { return std::stod(stringValue); } catch (...) { return fallback; }
        }
        default: return fallback;
    }
}

bool JsonValue::asBool(bool fallback) const {
    switch (type) {
        case Type::Bool:   return boolValue;
        case Type::Number: return numberValue != 0.0;
        default: return fallback;
    }
}

std::string JsonValue::asString(const std::string &fallback) const {
    return type == Type::String ? stringValue : fallback;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

class Parser {
public:
    Parser(const std::string &text) : s(text) {}

    bool parseValue(JsonValue &out) {
        skipWhitespace();
        if (pos >= s.size()) return fail("unexpected end of input");

        switch (s[pos]) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': {
                out.type = JsonValue::Type::String;
                return parseString(out.stringValue);
            }
            case 't':
                if (match("true"))  { out.type = JsonValue::Type::Bool; out.boolValue = true;  return true; }
                return fail("invalid literal");
            case 'f':
                if (match("false")) { out.type = JsonValue::Type::Bool; out.boolValue = false; return true; }
                return fail("invalid literal");
            case 'n':
                if (match("null"))  { out.type = JsonValue::Type::Null; return true; }
                return fail("invalid literal");
            default: return parseNumber(out);
        }
    }

    std::string error;

private:
    const std::string &s;
    size_t pos = 0;

    bool fail(const char *what) {
        if (error.empty()) {
            std::ostringstream os;
            os << what << " at offset " << pos;
            error = os.str();
        }
        return false;
    }

    void skipWhitespace() {
        while (pos < s.size()) {
            const char c = s[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos;
            } else {
                break;
            }
        }
    }

    bool match(const char *literal) {
        const size_t n = std::strlen(literal);
        if (s.compare(pos, n, literal) != 0) return false;
        pos += n;
        return true;
    }

    bool parseObject(JsonValue &out) {
        out.type = JsonValue::Type::Object;
        ++pos; // '{'
        skipWhitespace();
        if (pos < s.size() && s[pos] == '}') { ++pos; return true; }

        for (;;) {
            skipWhitespace();
            if (pos >= s.size() || s[pos] != '"') return fail("expected object key");

            std::string key;
            if (!parseString(key)) return false;

            skipWhitespace();
            if (pos >= s.size() || s[pos] != ':') return fail("expected ':'");
            ++pos;

            JsonValue value;
            if (!parseValue(value)) return false;
            out.objectValue[key] = std::move(value);

            skipWhitespace();
            if (pos >= s.size()) return fail("unterminated object");
            if (s[pos] == ',') { ++pos; continue; }
            if (s[pos] == '}') { ++pos; return true; }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(JsonValue &out) {
        out.type = JsonValue::Type::Array;
        ++pos; // '['
        skipWhitespace();
        if (pos < s.size() && s[pos] == ']') { ++pos; return true; }

        for (;;) {
            JsonValue value;
            if (!parseValue(value)) return false;
            out.arrayValue.push_back(std::move(value));

            skipWhitespace();
            if (pos >= s.size()) return fail("unterminated array");
            if (s[pos] == ',') { ++pos; continue; }
            if (s[pos] == ']') { ++pos; return true; }
            return fail("expected ',' or ']'");
        }
    }

    bool parseString(std::string &out) {
        ++pos; // opening quote
        out.clear();
        while (pos < s.size()) {
            const char c = s[pos++];
            if (c == '"') return true;
            if (c != '\\') { out.push_back(c); continue; }

            if (pos >= s.size()) return fail("unterminated escape");
            const char e = s[pos++];
            switch (e) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > s.size()) return fail("truncated \\u escape");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = s[pos + i];
                        cp <<= 4;
                        if (h >= '0' && h <= '9')      cp |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                        else return fail("invalid \\u escape");
                    }
                    pos += 4;
                    // UTF-8 encode. Surrogate pairs are passed through as-is;
                    // no preset text relies on them.
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: return fail("invalid escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseNumber(JsonValue &out) {
        const char *start = s.c_str() + pos;
        char *end = nullptr;
        const double v = std::strtod(start, &end);
        if (end == start) return fail("invalid number");
        pos += static_cast<size_t>(end - start);
        out.type = JsonValue::Type::Number;
        out.numberValue = v;
        return true;
    }
};

} // namespace

bool JsonValue::parse(const std::string &text, JsonValue &out, std::string &error) {
    Parser p(text);
    if (!p.parseValue(out)) {
        error = p.error;
        return false;
    }
    return true;
}

bool JsonValue::parseFile(const std::string &path, JsonValue &out, std::string &error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return parse(buf.str(), out, error);
}

} // namespace s1
