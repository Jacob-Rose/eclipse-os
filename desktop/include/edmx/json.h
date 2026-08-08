// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <map>
#include <string>
#include <vector>

///
/// A small, dependency-free JSON reader.
///
/// The config file is the contract between the python wrapper and this
/// executable, and it is the only thing we need to parse, so a full JSON
/// library would be a lot of weight for very little. This handles the whole
/// spec except \u escapes outside the BMP, which a lighting config will
/// never contain.
///

namespace edmx
{
    class JsonValue
    {
    public:
        enum class Type
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object,
        };

        JsonValue() = default;

        Type getType() const { return type; }

        bool isNull() const { return type == Type::Null; }
        bool isObject() const { return type == Type::Object; }
        bool isArray() const { return type == Type::Array; }

        /// Lookup on an object. Returns a null value when the key (or the
        /// receiver) is not there, so chained access never needs guarding.
        const JsonValue& operator[](const std::string& key) const;
        /// Lookup on an array. Same null-on-miss behaviour.
        const JsonValue& operator[](size_t idx) const;

        size_t size() const;

        /// Typed reads. Each falls back to the supplied default when this value
        /// is absent or the wrong type, which is what makes the config loader
        /// read as a flat list of "field, default" lines.
        bool asBool(bool fallback = false) const;
        double asNumber(double fallback = 0.0) const;
        float asFloat(float fallback = 0.0f) const;
        int asInt(int fallback = 0) const;
        std::string asString(const std::string& fallback = std::string()) const;

        /// Keys of an object, in file order.
        const std::vector<std::string>& keys() const { return objectKeys; }

        static const JsonValue& nullValue();

    private:
        friend class JsonParser;

        Type type{Type::Null};
        bool boolValue{false};
        double numberValue{0.0};
        std::string stringValue;
        std::vector<JsonValue> arrayValues;
        std::map<std::string, JsonValue> objectValues;
        std::vector<std::string> objectKeys;
    };

    /// Parses `text`. On failure returns false and fills `outError` with a
    /// message that names the offending line.
    bool parseJson(const std::string& text, JsonValue& outValue, std::string& outError);

    /// Convenience: read a file off disk and parse it.
    bool parseJsonFile(const std::string& path, JsonValue& outValue, std::string& outError);
}
