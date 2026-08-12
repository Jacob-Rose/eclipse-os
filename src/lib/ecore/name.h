#pragma once

#include <string>
#include <vector>
#include <functional>
#include <sstream>

// No `using namespace std;` here, deliberately.
//
// A header that opens std at global scope opens it for every translation unit
// that includes it, and the arduino-pico core builds at -std=gnu++23 - where
// std has `byte`, `lerp`, `size`, `data` and a growing list of names Arduino
// also defines. The collisions read as errors deep inside SPI.h or Common.h,
// nowhere near the header that caused them. Qualify instead.

namespace ecore
{
    class Name {
    private:
        std::string original;
        size_t hashValue;
        std::vector<std::string> tokens;

        void tokenize(char delimiter = '.') {
            std::stringstream ss(original);
            std::string token;
            while (std::getline(ss, token, delimiter)) {
                tokens.push_back(token);
            }
        }

    public:
        // Constructor
        explicit Name(const std::string& name) : original(name) {
            hashValue = std::hash<std::string>{}(original);
            tokenize();
        }

        // Get the original string
        const std::string& GetOriginal() const { return original; }

        // Get hash value
        size_t GetHash() const { return hashValue; }

        // Get tokens
        const std::vector<std::string>& GetTokens() const { return tokens; }
    
        // Comparison operators (fast due to hashing)
        bool operator==(const Name& other) const { return hashValue == other.hashValue; }
        bool operator!=(const Name& other) const { return !(*this == other); }
        bool operator<(const Name& other) const { return original < other.original; }
        bool operator>(const Name& other) const { return original > other.original; }
    };
}