//
//  Json.h
//  AudioKitSynthOne - Linux port
//
//  Small recursive-descent JSON reader, enough for the two shapes Synth One
//  ships: wavetables ({"content":[...],"phase":n,"type":n}) and preset banks
//  (arrays of flat objects of numbers, strings, bools and small arrays).
//
//  Parsing happens at load time only -- never on the audio thread.
//

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace s1 {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::string, JsonValue> objectValue;

    bool isNull() const { return type == Type::Null; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    /// Object lookup; returns a null value when absent.
    const JsonValue &operator[](const std::string &key) const;
    bool contains(const std::string &key) const;

    /// Coercions that tolerate the mixed number/bool spellings in the preset
    /// data (e.g. "reverbToggled": 1 vs true).
    double asDouble(double fallback = 0.0) const;
    float asFloat(float fallback = 0.0f) const { return static_cast<float>(asDouble(fallback)); }
    int asInt(int fallback = 0) const { return static_cast<int>(asDouble(fallback)); }
    bool asBool(bool fallback = false) const;
    std::string asString(const std::string &fallback = "") const;

    /// Parse. Returns false and fills `error` on malformed input.
    static bool parse(const std::string &text, JsonValue &out, std::string &error);
    static bool parseFile(const std::string &path, JsonValue &out, std::string &error);
};

} // namespace s1
