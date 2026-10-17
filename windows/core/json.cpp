#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace zl::json {

namespace {
const Value& nullValue() {
    static const Value v;
    return v;
}

void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out.push_back(char(cp));
    } else if (cp < 0x800) {
        out.push_back(char(0xC0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(char(0xF0 | (cp >> 18)));
        out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
}

void dumpString(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(char(c));
                }
        }
    }
    out.push_back('"');
}

class Parser {
public:
    explicit Parser(const std::string& t) : s_(t) {}

    Value parseDocument() {
        Value v = parseValue(0);
        skipSpace();
        if (i_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const char* what) {
        throw std::runtime_error(std::string("json: ") + what + " at " + std::to_string(i_));
    }

    void skipSpace() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }

    bool consume(const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (s_.compare(i_, n, word) == 0) {
            i_ += n;
            return true;
        }
        return false;
    }

    Value parseValue(int depth) {
        if (depth > 64) fail("too deep");
        skipSpace();
        if (i_ >= s_.size()) fail("unexpected end");
        const char c = s_[i_];
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') return Value(parseString());
        if (consume("true")) return Value(true);
        if (consume("false")) return Value(false);
        if (consume("null")) return Value();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        fail("unexpected character");
    }

    Value parseObject(int depth) {
        ++i_;
        Value obj = Value::object();
        skipSpace();
        if (i_ < s_.size() && s_[i_] == '}') {
            ++i_;
            return obj;
        }
        while (true) {
            skipSpace();
            if (i_ >= s_.size() || s_[i_] != '"') fail("expected key");
            std::string key = parseString();
            skipSpace();
            if (i_ >= s_.size() || s_[i_] != ':') fail("expected ':'");
            ++i_;
            obj.set(key, parseValue(depth + 1));
            skipSpace();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return obj; }
            fail("expected ',' or '}'");
        }
    }

    Value parseArray(int depth) {
        ++i_;
        Value arr = Value::array();
        skipSpace();
        if (i_ < s_.size() && s_[i_] == ']') {
            ++i_;
            return arr;
        }
        while (true) {
            arr.push(parseValue(depth + 1));
            skipSpace();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return arr; }
            fail("expected ',' or ']'");
        }
    }

    unsigned hex4() {
        if (i_ + 4 > s_.size()) fail("bad \\u escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else fail("bad hex digit");
        }
        return v;
    }

    std::string parseString() {
        ++i_;  // opening quote
        std::string out;
        while (true) {
            if (i_ >= s_.size()) fail("unterminated string");
            char c = s_[i_++];
            if (c == '"') return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i_ >= s_.size()) fail("bad escape");
            char e = s_[i_++];
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
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && i_ + 6 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        size_t save = i_;
                        i_ += 2;
                        unsigned lo = hex4();
                        if (lo >= 0xDC00 && lo < 0xE000) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            i_ = save;
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xD800 && cp < 0xE000) {
                        cp = 0xFFFD;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
    }

    Value parseNumber() {
        const char* begin = s_.c_str() + i_;
        char* end = nullptr;
        double v = std::strtod(begin, &end);
        if (end == begin) fail("bad number");
        i_ += size_t(end - begin);
        return Value(v);
    }

    const std::string& s_;
    size_t i_ = 0;
};
}  // namespace

const std::string& Value::asString() const {
    static const std::string empty;
    return type_ == Type::String ? str_ : empty;
}

const Value::Array& Value::items() const {
    static const Array empty;
    return type_ == Type::Array ? *arr_ : empty;
}

const Value::Object& Value::members() const {
    static const Object empty;
    return type_ == Type::Object ? *obj_ : empty;
}

const Value& Value::operator[](const std::string& key) const {
    if (type_ != Type::Object) return nullValue();
    for (const auto& kv : *obj_) if (kv.first == key) return kv.second;
    return nullValue();
}

bool Value::has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (const auto& kv : *obj_) if (kv.first == key) return true;
    return false;
}

const Value& Value::operator[](size_t i) const {
    if (type_ != Type::Array || i >= arr_->size()) return nullValue();
    return (*arr_)[i];
}

size_t Value::size() const {
    if (type_ == Type::Array) return arr_->size();
    if (type_ == Type::Object) return obj_->size();
    return 0;
}

Value& Value::set(const std::string& key, Value v) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        obj_ = std::make_shared<Object>();
    } else if (obj_.use_count() > 1) {
        obj_ = std::make_shared<Object>(*obj_);  // copy on write
    }
    for (auto& kv : *obj_) {
        if (kv.first == key) {
            kv.second = std::move(v);
            return *this;
        }
    }
    obj_->emplace_back(key, std::move(v));
    return *this;
}

Value& Value::push(Value v) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arr_ = std::make_shared<Array>();
    } else if (arr_.use_count() > 1) {
        arr_ = std::make_shared<Array>(*arr_);
    }
    arr_->push_back(std::move(v));
    return *this;
}

void Value::dumpTo(std::string& out) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            if (std::isfinite(num_) && num_ == std::floor(num_) && std::fabs(num_) < 1e15) {
                out += std::to_string((long long)num_);
            } else if (std::isfinite(num_)) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%.9g", num_);
                out += buf;
            } else {
                out += "null";
            }
            break;
        }
        case Type::String: dumpString(out, str_); break;
        case Type::Array: {
            out.push_back('[');
            bool first = true;
            for (const auto& v : *arr_) {
                if (!first) out.push_back(',');
                first = false;
                v.dumpTo(out);
            }
            out.push_back(']');
            break;
        }
        case Type::Object: {
            out.push_back('{');
            bool first = true;
            for (const auto& kv : *obj_) {
                if (!first) out.push_back(',');
                first = false;
                dumpString(out, kv.first);
                out.push_back(':');
                kv.second.dumpTo(out);
            }
            out.push_back('}');
            break;
        }
    }
}

std::string Value::dump() const {
    std::string out;
    dumpTo(out);
    return out;
}

Value Value::parse(const std::string& text) { return Parser(text).parseDocument(); }

}  // namespace zl::json
