#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace veilknit::json {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Value {
public:
    using array = std::vector<Value>;
    using object = std::map<std::string, Value, std::less<>>;

    Value() : data_(nullptr) {}
    Value(std::nullptr_t) : data_(nullptr) {}
    Value(bool value) : data_(value) {}
    Value(std::int32_t value) : data_(static_cast<std::int64_t>(value)) {}
    Value(std::uint32_t value) : data_(static_cast<std::uint64_t>(value)) {}
    Value(std::int64_t value) : data_(value) {}
    Value(std::uint64_t value) : data_(value) {}
    Value(double value) : data_(value) {}
    Value(const char* value) : data_(std::string(value == nullptr ? "" : value)) {}
    Value(std::string value) : data_(std::move(value)) {}
    Value(array value) : data_(std::move(value)) {}
    Value(object value) : data_(std::move(value)) {}

    static Value make_array() { return array{}; }
    static Value make_object() { return object{}; }

    bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(data_); }
    bool is_bool() const noexcept { return std::holds_alternative<bool>(data_); }
    bool is_int() const noexcept { return std::holds_alternative<std::int64_t>(data_); }
    bool is_uint() const noexcept { return std::holds_alternative<std::uint64_t>(data_); }
    bool is_double() const noexcept { return std::holds_alternative<double>(data_); }
    bool is_number() const noexcept { return is_int() || is_uint() || is_double(); }
    bool is_string() const noexcept { return std::holds_alternative<std::string>(data_); }
    bool is_array() const noexcept { return std::holds_alternative<array>(data_); }
    bool is_object() const noexcept { return std::holds_alternative<object>(data_); }

    bool as_bool() const {
        if (!is_bool()) throw Error("JSON value is not a boolean");
        return std::get<bool>(data_);
    }

    std::int64_t as_i64() const {
        if (is_int()) return std::get<std::int64_t>(data_);
        if (is_uint()) {
            const auto value = std::get<std::uint64_t>(data_);
            if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                throw Error("JSON unsigned integer does not fit in int64");
            }
            return static_cast<std::int64_t>(value);
        }
        throw Error("JSON value is not an integer");
    }

    std::uint64_t as_u64() const {
        if (is_uint()) return std::get<std::uint64_t>(data_);
        if (is_int()) {
            const auto value = std::get<std::int64_t>(data_);
            if (value < 0) throw Error("JSON negative integer does not fit in uint64");
            return static_cast<std::uint64_t>(value);
        }
        throw Error("JSON value is not an unsigned integer");
    }

    double as_double() const {
        if (is_double()) return std::get<double>(data_);
        if (is_int()) return static_cast<double>(std::get<std::int64_t>(data_));
        if (is_uint()) return static_cast<double>(std::get<std::uint64_t>(data_));
        throw Error("JSON value is not numeric");
    }

    const std::string& as_string() const {
        if (!is_string()) throw Error("JSON value is not a string");
        return std::get<std::string>(data_);
    }

    const array& as_array() const {
        if (!is_array()) throw Error("JSON value is not an array");
        return std::get<array>(data_);
    }

    array& as_array() {
        if (!is_array()) throw Error("JSON value is not an array");
        return std::get<array>(data_);
    }

    const object& as_object() const {
        if (!is_object()) throw Error("JSON value is not an object");
        return std::get<object>(data_);
    }

    object& as_object() {
        if (!is_object()) throw Error("JSON value is not an object");
        return std::get<object>(data_);
    }

    bool contains(std::string_view key) const {
        if (!is_object()) return false;
        return as_object().find(key) != as_object().end();
    }

    const Value& at(std::string_view key) const {
        const auto& values = as_object();
        const auto iterator = values.find(key);
        if (iterator == values.end()) throw Error("JSON object is missing key: " + std::string(key));
        return iterator->second;
    }

    Value& operator[](std::string key) {
        if (is_null()) data_ = object{};
        return as_object()[std::move(key)];
    }

    const Value& operator[](std::string_view key) const { return at(key); }

    void push_back(Value value) {
        if (is_null()) data_ = array{};
        as_array().push_back(std::move(value));
    }

    std::string dump() const {
        std::string output;
        dump_to(output);
        return output;
    }

    static Value parse(std::string_view source);

private:
    std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double, std::string, array, object> data_;

    static void append_utf8(std::string& output, std::uint32_t codepoint) {
        if (codepoint <= 0x7F) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0x10FFFF) {
            output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            throw Error("invalid Unicode code point");
        }
    }

    static void dump_string(std::string& output, const std::string& value) {
        output.push_back('"');
        static constexpr char hex[] = "0123456789abcdef";
        for (const unsigned char character : value) {
            switch (character) {
                case '"': output += "\\\""; break;
                case '\\': output += "\\\\"; break;
                case '\b': output += "\\b"; break;
                case '\f': output += "\\f"; break;
                case '\n': output += "\\n"; break;
                case '\r': output += "\\r"; break;
                case '\t': output += "\\t"; break;
                default:
                    if (character < 0x20) {
                        output += "\\u00";
                        output.push_back(hex[(character >> 4) & 0x0F]);
                        output.push_back(hex[character & 0x0F]);
                    } else {
                        output.push_back(static_cast<char>(character));
                    }
                    break;
            }
        }
        output.push_back('"');
    }

    void dump_to(std::string& output) const {
        if (is_null()) {
            output += "null";
        } else if (is_bool()) {
            output += as_bool() ? "true" : "false";
        } else if (is_int()) {
            output += std::to_string(std::get<std::int64_t>(data_));
        } else if (is_uint()) {
            output += std::to_string(std::get<std::uint64_t>(data_));
        } else if (is_double()) {
            const double value = std::get<double>(data_);
            if (!std::isfinite(value)) throw Error("JSON cannot encode NaN or infinity");
            std::ostringstream stream;
            stream << std::setprecision(17) << value;
            output += stream.str();
        } else if (is_string()) {
            dump_string(output, as_string());
        } else if (is_array()) {
            output.push_back('[');
            bool first = true;
            for (const auto& value : as_array()) {
                if (!first) output.push_back(',');
                first = false;
                value.dump_to(output);
            }
            output.push_back(']');
        } else {
            output.push_back('{');
            bool first = true;
            for (const auto& [key, value] : as_object()) {
                if (!first) output.push_back(',');
                first = false;
                dump_string(output, key);
                output.push_back(':');
                value.dump_to(output);
            }
            output.push_back('}');
        }
    }

    class Parser {
    public:
        explicit Parser(std::string_view source) : source_(source) {}

        Value parse_document() {
            skip_space();
            Value result = parse_value();
            skip_space();
            if (position_ != source_.size()) fail("unexpected trailing characters");
            return result;
        }

    private:
        std::string_view source_;
        std::size_t position_ = 0;

        [[noreturn]] void fail(const std::string& message) const {
            throw Error(message + " at byte " + std::to_string(position_));
        }

        void skip_space() {
            while (position_ < source_.size()) {
                const char c = source_[position_];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++position_;
                else break;
            }
        }

        bool consume(char expected) {
            if (position_ < source_.size() && source_[position_] == expected) {
                ++position_;
                return true;
            }
            return false;
        }

        char take() {
            if (position_ >= source_.size()) fail("unexpected end of JSON");
            return source_[position_++];
        }

        Value parse_value() {
            skip_space();
            if (position_ >= source_.size()) fail("expected JSON value");
            switch (source_[position_]) {
                case 'n': parse_literal("null"); return nullptr;
                case 't': parse_literal("true"); return true;
                case 'f': parse_literal("false"); return false;
                case '"': return parse_string();
                case '[': return parse_array();
                case '{': return parse_object();
                default:
                    if (source_[position_] == '-' || (source_[position_] >= '0' && source_[position_] <= '9')) {
                        return parse_number();
                    }
                    fail("expected JSON value");
            }
        }

        void parse_literal(std::string_view literal) {
            if (source_.substr(position_, literal.size()) != literal) fail("invalid literal");
            position_ += literal.size();
        }

        static std::uint32_t hex_digit(char c) {
            if (c >= '0' && c <= '9') return static_cast<std::uint32_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<std::uint32_t>(10 + c - 'a');
            if (c >= 'A' && c <= 'F') return static_cast<std::uint32_t>(10 + c - 'A');
            throw Error("invalid hexadecimal escape");
        }

        std::uint32_t parse_hex4() {
            if (position_ + 4 > source_.size()) fail("truncated Unicode escape");
            std::uint32_t value = 0;
            for (int index = 0; index < 4; ++index) {
                value = (value << 4) | hex_digit(source_[position_++]);
            }
            return value;
        }

        std::string parse_string() {
            if (!consume('"')) fail("expected string");
            std::string output;
            while (true) {
                const char c = take();
                if (c == '"') break;
                if (static_cast<unsigned char>(c) < 0x20) fail("unescaped control character");
                if (c != '\\') {
                    output.push_back(c);
                    continue;
                }
                const char escape = take();
                switch (escape) {
                    case '"': output.push_back('"'); break;
                    case '\\': output.push_back('\\'); break;
                    case '/': output.push_back('/'); break;
                    case 'b': output.push_back('\b'); break;
                    case 'f': output.push_back('\f'); break;
                    case 'n': output.push_back('\n'); break;
                    case 'r': output.push_back('\r'); break;
                    case 't': output.push_back('\t'); break;
                    case 'u': {
                        std::uint32_t codepoint = parse_hex4();
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                            if (!consume('\\') || !consume('u')) fail("high surrogate without low surrogate");
                            const std::uint32_t low = parse_hex4();
                            if (low < 0xDC00 || low > 0xDFFF) fail("invalid low surrogate");
                            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                        } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                            fail("unexpected low surrogate");
                        }
                        append_utf8(output, codepoint);
                        break;
                    }
                    default: fail("invalid string escape");
                }
            }
            return output;
        }

        Value parse_array() {
            consume('[');
            array values;
            skip_space();
            if (consume(']')) return values;
            while (true) {
                values.push_back(parse_value());
                skip_space();
                if (consume(']')) break;
                if (!consume(',')) fail("expected ',' or ']' in array");
                skip_space();
            }
            return values;
        }

        Value parse_object() {
            consume('{');
            object values;
            skip_space();
            if (consume('}')) return values;
            while (true) {
                skip_space();
                if (position_ >= source_.size() || source_[position_] != '"') fail("expected object key");
                std::string key = parse_string();
                skip_space();
                if (!consume(':')) fail("expected ':' after object key");
                Value value = parse_value();
                const auto [_, inserted] = values.emplace(std::move(key), std::move(value));
                if (!inserted) fail("duplicate object key");
                skip_space();
                if (consume('}')) break;
                if (!consume(',')) fail("expected ',' or '}' in object");
                skip_space();
            }
            return values;
        }

        Value parse_number() {
            const std::size_t start = position_;
            consume('-');
            if (consume('0')) {
                if (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') {
                    fail("leading zero in number");
                }
            } else {
                if (position_ >= source_.size() || source_[position_] < '1' || source_[position_] > '9') {
                    fail("invalid number");
                }
                while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
            }
            bool floating = false;
            if (consume('.')) {
                floating = true;
                if (position_ >= source_.size() || source_[position_] < '0' || source_[position_] > '9') {
                    fail("fraction requires digits");
                }
                while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
            }
            if (position_ < source_.size() && (source_[position_] == 'e' || source_[position_] == 'E')) {
                floating = true;
                ++position_;
                if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-')) ++position_;
                if (position_ >= source_.size() || source_[position_] < '0' || source_[position_] > '9') {
                    fail("exponent requires digits");
                }
                while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
            }

            const std::string_view token = source_.substr(start, position_ - start);
            if (floating) {
                const std::string copy(token);
                char* end = nullptr;
                const double value = std::strtod(copy.c_str(), &end);
                if (end != copy.c_str() + copy.size() || !std::isfinite(value)) fail("invalid floating-point number");
                return value;
            }
            if (!token.empty() && token.front() == '-') {
                std::int64_t value = 0;
                const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
                if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) fail("integer out of range");
                return value;
            }
            std::uint64_t value = 0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
            if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) fail("integer out of range");
            return value;
        }
    };
};

inline Value Value::parse(std::string_view source) {
    return Parser(source).parse_document();
}

inline std::string optional_string(const Value& object, std::string_view key, std::string fallback = {}) {
    if (!object.contains(key) || object.at(key).is_null()) return fallback;
    return object.at(key).as_string();
}

inline std::uint64_t optional_u64(const Value& object, std::string_view key, std::uint64_t fallback = 0) {
    if (!object.contains(key) || object.at(key).is_null()) return fallback;
    return object.at(key).as_u64();
}

inline bool optional_bool(const Value& object, std::string_view key, bool fallback = false) {
    if (!object.contains(key) || object.at(key).is_null()) return fallback;
    return object.at(key).as_bool();
}

} // namespace veilknit::json
