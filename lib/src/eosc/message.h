// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

///
/// eosc: Open Sound Control for the library. A message model, a packet codec,
/// address matching and a binding from messages to a look's properties.
/// Transports - a WiFi UDP socket on a Pico W, a desk's socket - live with
/// their platform and hand datagrams in; everything here builds on both.
///

namespace eosc
{
    /* @brief One argument of a message, tagged the way OSC tags it. Numbers
    * of every kind read back as one float through number(), which is what a
    * knob wants; strings and blobs keep their bytes.
    */
    struct Arg
    {
        enum class Type : char
        {
            Int32 = 'i',
            Float32 = 'f',
            Float64 = 'd',
            String = 's',
            Blob = 'b',
            True = 'T',
            False = 'F',
            Nil = 'N',
            Impulse = 'I',
        };

        Type type{Type::Nil};
        int32_t i{0};
        float f{0.0f};
        double d{0.0};
        std::string bytes;   ///< the text of a String, the contents of a Blob

        static Arg Int(int32_t value) { Arg a; a.type = Type::Int32; a.i = value; return a; }
        static Arg Float(float value) { Arg a; a.type = Type::Float32; a.f = value; return a; }
        static Arg Double(double value) { Arg a; a.type = Type::Float64; a.d = value; return a; }
        static Arg Str(const std::string& value) { Arg a; a.type = Type::String; a.bytes = value; return a; }
        static Arg Blob(const std::string& value) { Arg a; a.type = Type::Blob; a.bytes = value; return a; }
        static Arg Bool(bool value) { Arg a; a.type = value ? Type::True : Type::False; return a; }
        static Arg NilValue() { return Arg(); }
        static Arg ImpulseValue() { Arg a; a.type = Type::Impulse; return a; }

        /// An int, a float, a double or a bool: something a knob can take.
        bool isNumber() const
        {
            return type == Type::Int32 || type == Type::Float32 || type == Type::Float64
                || type == Type::True || type == Type::False;
        }

        /// The number, as a float; 0 for anything that is not one.
        float number() const
        {
            switch (type)
            {
                case Type::Int32:   return static_cast<float>(i);
                case Type::Float32: return f;
                case Type::Float64: return static_cast<float>(d);
                case Type::True:    return 1.0f;
                default:            return 0.0f;
            }
        }
    };

    /* @brief An address and its arguments.
    *
    *     Message colour("/controls/global/color/1");
    *     colour.add(1.0f).add(0.5f).add(0.0f);
    *
    *     float level;
    *     if (message.number(0, level)) { ... }
    */
    struct Message
    {
        std::string address;
        std::vector<Arg> args;

        Message() = default;
        explicit Message(const std::string& inAddress) : address(inAddress) {}

        Message& add(const Arg& arg) { args.push_back(arg); return *this; }
        Message& add(int32_t value) { return add(Arg::Int(value)); }
        Message& add(float value) { return add(Arg::Float(value)); }
        Message& add(double value) { return add(Arg::Double(value)); }
        Message& add(const char* value) { return add(Arg::Str(value)); }
        Message& add(const std::string& value) { return add(Arg::Str(value)); }
        Message& add(bool value) { return add(Arg::Bool(value)); }

        /// The argument at `index` as a float. False when there is no argument
        /// there or it is not a number: a string is not a zero.
        bool number(int index, float& outValue) const
        {
            if (index < 0 || index >= static_cast<int>(args.size()) || !args[static_cast<size_t>(index)].isNumber())
            {
                return false;
            }
            outValue = args[static_cast<size_t>(index)].number();
            return true;
        }

        /// The type tag string, ",fis" and so on, as a sender would write it.
        std::string typeTags() const
        {
            std::string tags(",");
            for (const Arg& arg : args) tags.push_back(static_cast<char>(arg.type));
            return tags;
        }
    };

    /* @brief Where decoded messages go. `when` is the caller's clock, in
    * seconds, so a trigger can be placed in time.
    */
    class MessageSink
    {
    public:
        virtual ~MessageSink() = default;
        virtual void onMessage(const Message& message, double when) = 0;
    };
}
