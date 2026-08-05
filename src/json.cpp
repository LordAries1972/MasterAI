// MasterAI bounded JSON parser and canonical string encoder.
//
// The parser rejects duplicate keys and excessive input/nesting. The encoder
// emits protocol-safe quoted strings for every JSON-producing source unit.
#include "json.hpp"

#include <array>
#include <cctype>
#include <limits>
#include <sstream>

namespace masterai {

JsonValue::JsonValue(const bool value) : type_(Type::boolean), boolean_(value) {}
JsonValue::JsonValue(const std::int64_t value) : type_(Type::number), integer_(value) {}
JsonValue::JsonValue(const double value)
    : type_(Type::number), double_(value), is_float_(true) {}
JsonValue::JsonValue(std::string value)
    : type_(Type::string), string_(std::move(value)) {}
JsonValue::JsonValue(Array value) : type_(Type::array), array_(std::move(value)) {}
JsonValue::JsonValue(Object value) : type_(Type::object), object_(std::move(value)) {}

JsonValue::Type JsonValue::type() const noexcept { return type_; }
bool JsonValue::is_object() const noexcept { return type_ == Type::object; }

const JsonValue::Object& JsonValue::as_object() const {
    if (type_ != Type::object) {
        throw std::runtime_error("JSON value is not an object");
    }
    return object_;
}

const JsonValue::Array& JsonValue::as_array() const {
    if (type_ != Type::array) {
        throw std::runtime_error("JSON value is not an array");
    }
    return array_;
}

const std::string& JsonValue::as_string() const {
    if (type_ != Type::string) {
        throw std::runtime_error("JSON value is not a string");
    }
    return string_;
}

std::int64_t JsonValue::as_integer() const {
    if (type_ != Type::number || is_float_) {
        throw std::runtime_error("JSON value is not an integer");
    }
    return integer_;
}

double JsonValue::as_double() const {
    if (type_ != Type::number) {
        throw std::runtime_error("JSON value is not a number");
    }
    return is_float_ ? double_ : static_cast<double>(integer_);
}

bool JsonValue::as_boolean() const {
    if (type_ != Type::boolean) {
        throw std::runtime_error("JSON value is not a boolean");
    }
    return boolean_;
}

const JsonValue& JsonValue::required(const std::string& key) const {
    const auto& object = as_object();
    const auto found = object.find(key);
    if (found == object.end()) {
        throw std::runtime_error("required JSON field is missing: " + key);
    }
    return found->second;
}

JsonValue& JsonValue::required_mutable(const std::string& key) {
    return const_cast<JsonValue&>(
        static_cast<const JsonValue&>(*this).required(key));
}

std::string JsonValue::take_string() {
    if (type_ != Type::string) {
        throw std::runtime_error("JSON value is not a string");
    }
    std::string result = std::move(string_);
    string_.clear();
    return result;
}

const JsonValue* JsonValue::optional(const std::string& key) const noexcept {
    if (type_ != Type::object) {
        return nullptr;
    }
    const auto found = object_.find(key);
    return found == object_.end() ? nullptr : &found->second;
}

namespace {

class Parser final {
public:
    explicit Parser(const std::string& input) : input_(input) {}

    JsonValue parse() {
        if (input_.size() > 1024U * 1024U) {
            throw std::runtime_error("JSON input exceeds one MiB");
        }
        skip_space();
        JsonValue value = parse_value(0U);
        skip_space();
        if (position_ != input_.size()) {
            fail("trailing content");
        }
        return value;
    }

private:
    JsonValue parse_value(const unsigned int depth) {
        if (depth > 64U || position_ >= input_.size()) {
            fail("invalid nesting or unexpected end");
        }
        switch (input_[position_]) {
            case '{': return parse_object(depth + 1U);
            case '[': return parse_array(depth + 1U);
            case '"': return JsonValue(parse_string());
            case 't': consume_literal("true"); return JsonValue(true);
            case 'f': consume_literal("false"); return JsonValue(false);
            case 'n': consume_literal("null"); return JsonValue();
            default:
                if (input_[position_] == '-' ||
                    std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                    return parse_number();
                }
                fail("unexpected token");
        }
    }

    JsonValue parse_object(const unsigned int depth) {
        ++position_;
        skip_space();
        JsonValue::Object object;
        if (consume('}')) {
            return JsonValue(std::move(object));
        }
        while (true) {
            std::string key = parse_object_key();
            if (object.find(key) != object.end()) {
                fail("duplicate object key");
            }
            skip_space();
            expect(':');
            skip_space();
            object.emplace(std::move(key), parse_value(depth));
            if (sequence_complete('}')) {
                return JsonValue(std::move(object));
            }
        }
    }

    JsonValue parse_array(const unsigned int depth) {
        ++position_;
        skip_space();
        JsonValue::Array array;
        if (consume(']')) {
            return JsonValue(std::move(array));
        }
        while (true) {
            require_array_capacity(array.size());
            array.push_back(parse_value(depth));
            if (sequence_complete(']')) {
                return JsonValue(std::move(array));
            }
        }
    }

    // Reads one object key outside the member loop to keep nesting bounded.
    std::string parse_object_key() {
        if (position_ >= input_.size() || input_[position_] != '"') {
            fail("object key must be a string");
        }
        return parse_string();
    }

    // Applies the fixed JSON array element limit before insertion.
    void require_array_capacity(const std::size_t size) const {
        if (size >= 100000U) {
            fail("array element limit exceeded");
        }
    }

    // Consumes a closing delimiter or requires the next sequence comma.
    bool sequence_complete(const char closing) {
        skip_space();
        if (consume(closing)) return true;
        expect(',');
        skip_space();
        return false;
    }

    std::string parse_string() {
        expect('"');
        std::string output;
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"') {
                return output;
            }
            if (static_cast<unsigned char>(character) < 0x20U) {
                fail("control character in string");
            }
            if (character != '\\') {
                output.push_back(character);
                continue;
            }
            if (position_ >= input_.size()) {
                fail("incomplete escape");
            }
            const char escape = input_[position_++];
            switch (escape) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u':
                    parse_unicode_escape(output);
                    break;
                default: fail("unsupported escape");
            }
        }
        fail("unterminated string");
    }

    void parse_unicode_escape(std::string& output) {
        if (position_ + 4U > input_.size()) {
            fail("incomplete unicode escape");
        }
        unsigned int code_point = 0U;
        for (unsigned int count = 0; count < 4U; ++count) {
            const char value = input_[position_++];
            code_point <<= 4U;
            if (value >= '0' && value <= '9') {
                code_point += static_cast<unsigned int>(value - '0');
            } else if (value >= 'a' && value <= 'f') {
                code_point += static_cast<unsigned int>(value - 'a' + 10);
            } else if (value >= 'A' && value <= 'F') {
                code_point += static_cast<unsigned int>(value - 'A' + 10);
            } else {
                fail("invalid unicode escape");
            }
        }
        if (code_point >= 0xd800U && code_point <= 0xdfffU) {
            fail("surrogate escapes are not accepted");
        }
        if (code_point <= 0x7fU) {
            output.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        } else {
            output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        }
    }

    void consume_digits() {
        while (position_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }

    // Parses a JSON number. Upstream runner responses (e.g. llama.cpp
    // timings) carry fields with fractions/exponents that our own code never
    // reads via as_integer(); those are kept as doubles instead of aborting
    // the whole parse, while plain integers keep their exact int64 value.
    JsonValue parse_number() {
        const std::size_t start = position_;
        if (input_[position_] == '-') {
            ++position_;
        }
        if (position_ >= input_.size()) {
            fail("incomplete number");
        }
        if (input_[position_] == '0') {
            ++position_;
        } else {
            if (!std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                fail("invalid number");
            }
            consume_digits();
        }
        bool is_float = false;
        if (position_ < input_.size() && input_[position_] == '.') {
            is_float = true;
            ++position_;
            if (position_ >= input_.size() ||
                !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                fail("invalid fraction");
            }
            consume_digits();
        }
        if (position_ < input_.size() &&
            (input_[position_] == 'e' || input_[position_] == 'E')) {
            is_float = true;
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= input_.size() ||
                !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                fail("invalid exponent");
            }
            consume_digits();
        }
        const std::string literal = input_.substr(start, position_ - start);
        if (is_float) {
            try {
                return JsonValue(std::stod(literal));
            } catch (const std::exception&) {
                fail("number is outside double range");
            }
        }
        try {
            return JsonValue(static_cast<std::int64_t>(std::stoll(literal)));
        } catch (const std::exception&) {
            fail("integer is outside 64-bit range");
        }
    }

    void consume_literal(const char* literal) {
        for (std::size_t index = 0; literal[index] != '\0'; ++index) {
            if (position_ >= input_.size() || input_[position_++] != literal[index]) {
                fail("invalid literal");
            }
        }
    }

    void skip_space() {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }

    bool consume(const char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(const char expected) {
        if (!consume(expected)) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    [[noreturn]] void fail(const std::string& reason) const {
        std::ostringstream message;
        message << "JSON parse error at byte " << position_ << ": " << reason;
        throw std::runtime_error(message.str());
    }

    const std::string& input_;
    std::size_t position_{0U};
};

}  // namespace

JsonValue parse_json(const std::string& input) {
    return Parser(input).parse();
}

// Escapes control characters and returns a complete quoted JSON string.
std::string json_string(const std::string& value) {
    static constexpr char hexadecimal[] = "0123456789abcdef";
    // Precalculated per-byte "needs escaping" table covering all 256
    // possible input bytes: only '"', '\\', and controls below 0x20 ever
    // need escape work. Clean stretches between escapes are appended as
    // whole runs (one bulk append per run) instead of byte-at-a-time --
    // this encoder serializes every JSON value the server emits, so large
    // mostly-clean strings dominate its runtime.
    static constexpr auto needs_escape = [] {
        std::array<bool, 256> table{};
        for (unsigned int byte = 0U; byte < 0x20U; ++byte) {
            table[byte] = true;
        }
        table[static_cast<unsigned char>('"')] = true;
        table[static_cast<unsigned char>('\\')] = true;
        return table;
    }();
    std::string output;
    output.reserve(value.size() + 2U);
    output.push_back('"');
    std::size_t run_start = 0U;
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const auto character = static_cast<unsigned char>(value[index]);
        if (!needs_escape[character]) continue;
        output.append(value, run_start, index - run_start);
        switch (character) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                output += "\\u00";
                output.push_back(hexadecimal[character >> 4U]);
                output.push_back(hexadecimal[character & 0x0fU]);
                break;
        }
        run_start = index + 1U;
    }
    output.append(value, run_start, value.size() - run_start);
    output.push_back('"');
    return output;
}

}  // namespace masterai
