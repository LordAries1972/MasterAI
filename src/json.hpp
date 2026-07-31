// MasterAI bounded JSON value, parser, and output-string contract.
//
// This unit exposes the one independently authored JSON boundary used by HTTP,
// MCP, IDE, and persistence code so escaping behavior is not reimplemented.
#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace masterai {

class JsonValue final {
public:
    enum class Type { null_value, boolean, number, string, array, object };
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;

    JsonValue() = default;
    explicit JsonValue(bool value);
    explicit JsonValue(std::int64_t value);
    explicit JsonValue(double value);
    explicit JsonValue(std::string value);
    explicit JsonValue(Array value);
    explicit JsonValue(Object value);

    Type type() const noexcept;
    bool is_object() const noexcept;
    const Object& as_object() const;
    const Array& as_array() const;
    const std::string& as_string() const;
    // Throws if this number was written with a fraction/exponent (see
    // as_double()); manifests and our own request bodies never emit those,
    // but upstream runner responses (e.g. llama.cpp timings) do.
    std::int64_t as_integer() const;
    double as_double() const;
    bool as_boolean() const;
    const JsonValue& required(const std::string& key) const;
    JsonValue& required_mutable(const std::string& key);
    std::string take_string();
    const JsonValue* optional(const std::string& key) const noexcept;

private:
    Type type_{Type::null_value};
    bool boolean_{false};
    std::int64_t integer_{0};
    double double_{0.0};
    bool is_float_{false};
    std::string string_;
    Array array_;
    Object object_;
};

JsonValue parse_json(const std::string& input);

// Encodes one UTF-8 value as a complete quoted JSON string.
std::string json_string(const std::string& value);

}  // namespace masterai
