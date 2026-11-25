#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<Value> arr_val;
    std::map<std::string, Value> obj_val;

    static Value make_null() { return Value{}; }
    static Value make_bool(bool b) { Value v; v.type = Type::Bool; v.bool_val = b; return v; }
    static Value make_number(double n) { Value v; v.type = Type::Number; v.num_val = n; return v; }
    static Value make_int(int64_t n) { return make_number(static_cast<double>(n)); }
    static Value make_string(std::string s) { Value v; v.type = Type::String; v.str_val = std::move(s); return v; }
    static Value make_array() { Value v; v.type = Type::Array; return v; }
    static Value make_object() { Value v; v.type = Type::Object; return v; }

    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Bool; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    bool as_bool(bool def = false) const { return is_bool() ? bool_val : def; }
    double as_double(double def = 0.0) const { return is_number() ? num_val : def; }
    float as_float(float def = 0.0f) const { return is_number() ? static_cast<float>(num_val) : def; }
    int64_t as_int64(int64_t def = 0) const { return is_number() ? static_cast<int64_t>(std::llround(num_val)) : def; }
    int as_int(int def = 0) const { return static_cast<int>(as_int64(def)); }
    std::string as_string(const std::string& def = "") const { return is_string() ? str_val : def; }

    const Value* get(const std::string& key) const {
        if (!is_object()) return nullptr;
        auto it = obj_val.find(key);
        return it == obj_val.end() ? nullptr : &it->second;
    }

    bool has(const std::string& key) const { return get(key) != nullptr; }

    Value& operator[](const std::string& key) {
        if (type != Type::Object) {
            type = Type::Object;
            obj_val.clear();
        }
        return obj_val[key];
    }
};

class Parser {
public:
    explicit Parser(const std::string& s) : src_(s), pos_(0) {}

    bool parse(Value& out) {
        skip_ws();
        if (!parse_value(out)) return false;
        skip_ws();
        return pos_ == src_.size();
    }

private:
    const std::string& src_;
    size_t pos_;

    void skip_ws() {
        while (pos_ < src_.size() && (src_[pos_] == ' ' || src_[pos_] == '\t' || src_[pos_] == '\r' || src_[pos_] == '\n')) {
            ++pos_;
        }
    }

    bool parse_value(Value& out) {
        skip_ws();
        if (pos_ >= src_.size()) return false;
        char c = src_[pos_];
        if (c == 'n') return parse_literal("null", Value::make_null(), out);
        if (c == 't') return parse_literal("true", Value::make_bool(true), out);
        if (c == 'f') return parse_literal("false", Value::make_bool(false), out);
        if (c == '"') return parse_string(out);
        if (c == '[') return parse_array(out);
        if (c == '{') return parse_object(out);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number(out);
        return false;
    }

    bool parse_literal(const char* lit, Value val, Value& out) {
        size_t len = std::char_traits<char>::length(lit);
        if (src_.compare(pos_, len, lit) == 0) {
            pos_ += len;
            out = std::move(val);
            return true;
        }
        return false;
    }

    static void append_utf8(std::string& s, uint32_t cp) {
        if (cp <= 0x7F) {
            s.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parse_string_raw(std::string& out) {
        if (pos_ >= src_.size() || src_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= src_.size()) return false;
                char e = src_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > src_.size()) return false;
                        uint32_t cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = src_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else return false;
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default:
                        return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool parse_string(Value& out) {
        std::string s;
        if (!parse_string_raw(s)) return false;
        out = Value::make_string(std::move(s));
        return true;
    }

    bool parse_number(Value& out) {
        size_t start = pos_;
        if (src_[pos_] == '-') ++pos_;
        if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) return false;
        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            ++pos_;
            if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) return false;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) return false;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        }
        char* end_ptr = nullptr;
        double d = std::strtod(src_.c_str() + start, &end_ptr);
        if (end_ptr == src_.c_str() + start) return false;
        out = Value::make_number(d);
        return true;
    }

    bool parse_array(Value& out) {
        ++pos_; // '['
        out = Value::make_array();
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (pos_ < src_.size()) {
            Value elem;
            if (!parse_value(elem)) return false;
            out.arr_val.push_back(std::move(elem));
            skip_ws();
            if (pos_ < src_.size() && src_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < src_.size() && src_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return false;
        }
        return false;
    }

    bool parse_object(Value& out) {
        ++pos_; // '{'
        out = Value::make_object();
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (pos_ < src_.size()) {
            skip_ws();
            std::string key;
            if (!parse_string_raw(key)) return false;
            skip_ws();
            if (pos_ >= src_.size() || src_[pos_] != ':') return false;
            ++pos_;
            Value val;
            if (!parse_value(val)) return false;
            out.obj_val[std::move(key)] = std::move(val);
            skip_ws();
            if (pos_ < src_.size() && src_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < src_.size() && src_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return false;
        }
        return false;
    }
};

inline bool parse(const std::string& text, Value& out) {
    Parser p(text);
    return p.parse(out);
}

inline std::string escape_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
    return out;
}

inline void dump_impl(const Value& v, std::ostringstream& oss, int indent, int depth) {
    switch (v.type) {
        case Value::Type::Null:
            oss << "null";
            break;
        case Value::Type::Bool:
            oss << (v.bool_val ? "true" : "false");
            break;
        case Value::Type::Number: {
            double intpart = 0.0;
            if (std::modf(v.num_val, &intpart) == 0.0 && std::fabs(v.num_val) < 1e15) {
                oss << static_cast<int64_t>(intpart);
            } else {
                oss << std::setprecision(6) << v.num_val;
            }
            break;
        }
        case Value::Type::String:
            oss << escape_string(v.str_val);
            break;
        case Value::Type::Array: {
            oss << "[";
            for (size_t i = 0; i < v.arr_val.size(); ++i) {
                if (i > 0) oss << ", ";
                dump_impl(v.arr_val[i], oss, indent, depth + 1);
            }
            oss << "]";
            break;
        }
        case Value::Type::Object: {
            oss << "{";
            if (!v.obj_val.empty()) {
                oss << "\n";
                size_t idx = 0;
                for (const auto& [k, val] : v.obj_val) {
                    oss << std::string((depth + 1) * indent, ' ') << escape_string(k) << ": ";
                    dump_impl(val, oss, indent, depth + 1);
                    if (++idx < v.obj_val.size()) oss << ",";
                    oss << "\n";
                }
                oss << std::string(depth * indent, ' ');
            }
            oss << "}";
            break;
        }
    }
}

inline std::string dump(const Value& v, int indent = 2) {
    std::ostringstream oss;
    dump_impl(v, oss, indent, 0);
    return oss.str();
}

} // namespace json
