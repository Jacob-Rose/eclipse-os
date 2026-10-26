// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "edmx/json.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

using namespace edmx;

const JsonValue& JsonValue::nullValue()
{
    static const JsonValue instance;
    return instance;
}

const JsonValue& JsonValue::operator[](const std::string& key) const
{
    if (type != Type::Object)
    {
        return nullValue();
    }
    auto it = objectValues.find(key);
    return (it == objectValues.end()) ? nullValue() : it->second;
}

const JsonValue& JsonValue::operator[](size_t idx) const
{
    if (type != Type::Array || idx >= arrayValues.size())
    {
        return nullValue();
    }
    return arrayValues[idx];
}

size_t JsonValue::size() const
{
    if (type == Type::Array)
    {
        return arrayValues.size();
    }
    if (type == Type::Object)
    {
        return objectValues.size();
    }
    return 0;
}

bool JsonValue::asBool(bool fallback) const
{
    if (type == Type::Bool)
    {
        return boolValue;
    }
    if (type == Type::Number)
    {
        return numberValue != 0.0;
    }
    return fallback;
}

double JsonValue::asNumber(double fallback) const
{
    if (type == Type::Number)
    {
        return numberValue;
    }
    if (type == Type::Bool)
    {
        return boolValue ? 1.0 : 0.0;
    }
    return fallback;
}

float JsonValue::asFloat(float fallback) const
{
    return static_cast<float>(asNumber(static_cast<double>(fallback)));
}

int JsonValue::asInt(int fallback) const
{
    if (type != Type::Number)
    {
        return fallback;
    }
    return static_cast<int>(std::llround(numberValue));
}

std::string JsonValue::asString(const std::string& fallback) const
{
    return (type == Type::String) ? stringValue : fallback;
}

namespace edmx
{
    class JsonParser
    {
    public:
        JsonParser(const std::string& inText) : text(inText) {}

        bool parse(JsonValue& out)
        {
            skipWhitespace();
            if (!parseValue(out))
            {
                return false;
            }
            skipWhitespace();
            if (pos != text.size())
            {
                return fail("trailing content after the top-level value");
            }
            return true;
        }

        const std::string& getError() const { return error; }

    private:
        const std::string& text;
        size_t pos{0};
        std::string error;

        bool fail(const std::string& msg)
        {
            // report a line number: a config typo should not send anyone
            // counting bytes.
            int line = 1;
            for (size_t i = 0; i < pos && i < text.size(); ++i)
            {
                if (text[i] == '\n')
                {
                    ++line;
                }
            }
            error = "line " + std::to_string(line) + ": " + msg;
            return false;
        }

        bool atEnd() const { return pos >= text.size(); }
        char peek() const { return atEnd() ? '\0' : text[pos]; }

        void skipWhitespace()
        {
            while (!atEnd())
            {
                const char c = text[pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    ++pos;
                }
                else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '/')
                {
                    // not strictly JSON, but a hand-edited lighting rig config
                    // wants comments badly enough to be worth the four lines.
                    while (!atEnd() && text[pos] != '\n')
                    {
                        ++pos;
                    }
                }
                else
                {
                    return;
                }
            }
        }

        bool expect(char c)
        {
            if (peek() != c)
            {
                return fail(std::string("expected '") + c + "'");
            }
            ++pos;
            return true;
        }

        bool parseValue(JsonValue& out)
        {
            if (atEnd())
            {
                return fail("unexpected end of input");
            }

            switch (peek())
            {
                case '{': return parseObject(out);
                case '[': return parseArray(out);
                case '"': return parseStringValue(out);
                case 't': return parseLiteral("true", out, true);
                case 'f': return parseLiteral("false", out, false);
                case 'n': return parseNull(out);
                default:  return parseNumber(out);
            }
        }

        bool parseLiteral(const char* literal, JsonValue& out, bool value)
        {
            const size_t len = std::strlen(literal);
            if (text.compare(pos, len, literal) != 0)
            {
                return fail(std::string("expected ") + literal);
            }
            pos += len;
            out.type = JsonValue::Type::Bool;
            out.boolValue = value;
            return true;
        }

        bool parseNull(JsonValue& out)
        {
            if (text.compare(pos, 4, "null") != 0)
            {
                return fail("expected null");
            }
            pos += 4;
            out.type = JsonValue::Type::Null;
            return true;
        }

        bool parseNumber(JsonValue& out)
        {
            const size_t start = pos;
            if (peek() == '-' || peek() == '+')
            {
                ++pos;
            }
            while (!atEnd() && (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.' ||
                                text[pos] == 'e' || text[pos] == 'E' || text[pos] == '-' || text[pos] == '+'))
            {
                ++pos;
            }
            if (start == pos)
            {
                return fail("expected a value");
            }

            const std::string token = text.substr(start, pos - start);
            try
            {
                size_t consumed = 0;
                const double parsed = std::stod(token, &consumed);
                if (consumed != token.size())
                {
                    return fail("malformed number '" + token + "'");
                }
                out.type = JsonValue::Type::Number;
                out.numberValue = parsed;
            }
            catch (const std::exception&)
            {
                return fail("malformed number '" + token + "'");
            }
            return true;
        }

        bool parseStringValue(JsonValue& out)
        {
            std::string parsed;
            if (!parseString(parsed))
            {
                return false;
            }
            out.type = JsonValue::Type::String;
            out.stringValue = parsed;
            return true;
        }

        bool parseString(std::string& out)
        {
            if (!expect('"'))
            {
                return false;
            }

            out.clear();
            while (true)
            {
                if (atEnd())
                {
                    return fail("unterminated string");
                }

                const char c = text[pos++];
                if (c == '"')
                {
                    return true;
                }

                if (c != '\\')
                {
                    out.push_back(c);
                    continue;
                }

                if (atEnd())
                {
                    return fail("unterminated escape");
                }

                const char esc = text[pos++];
                switch (esc)
                {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u':
                    {
                        if (pos + 4 > text.size())
                        {
                            return fail("truncated \\u escape");
                        }
                        unsigned int code = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            const char h = text[pos + i];
                            code <<= 4;
                            if (h >= '0' && h <= '9')      code |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                            else return fail("bad hex digit in \\u escape");
                        }
                        pos += 4;
                        appendUtf8(out, code);
                        break;
                    }
                    default:
                        return fail(std::string("unknown escape '\\") + esc + "'");
                }
            }
        }

        static void appendUtf8(std::string& out, unsigned int code)
        {
            if (code < 0x80)
            {
                out.push_back(static_cast<char>(code));
            }
            else if (code < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        }

        bool parseArray(JsonValue& out)
        {
            if (!expect('['))
            {
                return false;
            }
            out.type = JsonValue::Type::Array;

            skipWhitespace();
            if (peek() == ']')
            {
                ++pos;
                return true;
            }

            while (true)
            {
                skipWhitespace();
                JsonValue element;
                if (!parseValue(element))
                {
                    return false;
                }
                out.arrayValues.push_back(std::move(element));

                skipWhitespace();
                if (peek() == ',')
                {
                    ++pos;
                    continue;
                }
                if (peek() == ']')
                {
                    ++pos;
                    return true;
                }
                return fail("expected ',' or ']' in array");
            }
        }

        bool parseObject(JsonValue& out)
        {
            if (!expect('{'))
            {
                return false;
            }
            out.type = JsonValue::Type::Object;

            skipWhitespace();
            if (peek() == '}')
            {
                ++pos;
                return true;
            }

            while (true)
            {
                skipWhitespace();
                std::string key;
                if (!parseString(key))
                {
                    return false;
                }

                skipWhitespace();
                if (!expect(':'))
                {
                    return false;
                }

                skipWhitespace();
                JsonValue value;
                if (!parseValue(value))
                {
                    return false;
                }

                if (out.objectValues.find(key) == out.objectValues.end())
                {
                    out.objectKeys.push_back(key);
                }
                out.objectValues[key] = std::move(value);

                skipWhitespace();
                if (peek() == ',')
                {
                    ++pos;
                    continue;
                }
                if (peek() == '}')
                {
                    ++pos;
                    return true;
                }
                return fail("expected ',' or '}' in object");
            }
        }
    };
}

bool edmx::parseJson(const std::string& text, JsonValue& outValue, std::string& outError)
{
    JsonParser parser(text);
    if (!parser.parse(outValue))
    {
        outError = parser.getError();
        return false;
    }
    return true;
}

bool edmx::parseJsonFile(const std::string& path, JsonValue& outValue, std::string& outError)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        outError = "could not open '" + path + "'";
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    if (!parseJson(buffer.str(), outValue, outError))
    {
        outError = path + ": " + outError;
        return false;
    }
    return true;
}
