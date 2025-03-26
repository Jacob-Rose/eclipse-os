#pragma once

#include <string>
#include <vector>
#include <functional>
#include <sstream>

using namespace std;

namespace ecore
{
    class Name {
    private:
        string original;
        size_t hashValue;
        vector<string> tokens;
    
        void tokenize(char delimiter = '.') {
            stringstream ss(original);
            string token;
            while (getline(ss, token, delimiter)) {
                tokens.push_back(token);
            }
        }
    
    public:
        // Constructor
        explicit Name(const string& name) : original(name) {
            hashValue = hash<string>{}(original);
            tokenize();
        }
    
        // Get the original string
        const string& GetOriginal() const { return original; }
    
        // Get hash value
        size_t GetHash() const { return hashValue; }
    
        // Get tokens
        const vector<string>& GetTokens() const { return tokens; }
    
        // Comparison operators (fast due to hashing)
        bool operator==(const Name& other) const { return hashValue == other.hashValue; }
        bool operator!=(const Name& other) const { return !(*this == other); }
        bool operator<(const Name& other) const { return original < other.original; }
        bool operator>(const Name& other) const { return original > other.original; }
    };
}