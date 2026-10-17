// A small JSON value with a parser and serializer: the protocol between the text service and the
// engine server, settings files, and the benchmark files read by imecli. Strings are UTF-8.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace zl::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;  // keeps insertion order

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>>
    Value(T n) : type_(Type::Number), num_(double(n)) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(Array a) : type_(Type::Array), arr_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : type_(Type::Object), obj_(std::make_shared<Object>(std::move(o))) {}

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
    double asNumber(double def = 0) const { return type_ == Type::Number ? num_ : def; }
    int asInt(int def = 0) const { return type_ == Type::Number ? int(num_) : def; }
    const std::string& asString() const;
    const Array& items() const;
    const Object& members() const;

    /// Object member (a null value when missing or not an object).
    const Value& operator[](const std::string& key) const;
    bool has(const std::string& key) const;
    /// Array element (a null value when out of range).
    const Value& operator[](size_t i) const;
    size_t size() const;

    /// Sets an object member (the value becomes an object if it was null).
    Value& set(const std::string& key, Value v);
    /// Appends to an array (the value becomes an array if it was null).
    Value& push(Value v);

    std::string dump() const;
    /// Throws std::runtime_error on malformed input.
    static Value parse(const std::string& text);

private:
    void dumpTo(std::string& out) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::shared_ptr<Array> arr_;
    std::shared_ptr<Object> obj_;
};

}  // namespace zl::json
