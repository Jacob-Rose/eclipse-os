// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>

#include "core.h"

namespace ecore
{
    namespace log
    {
        enum class Verbosity // levels are self explanatory
        {
            VeryVerbose, 
            Verbose,
            Display,
            Warning,
            Error,
            Fatal,
        };

        static std::string getVerbosityString(Verbosity inVerbosity);
    
        enum class Category : uint64_t
        {
            None        = 0b00000000, // none flag
            OnTick      = 0b00000001, // logging flag for those activated on the tick function
            State       = 0b00000010, // state related logging
            IO          = 0b00000100, // IO related logging
            Relic       = 0b00001000, // part of the relic core
            Library     = 0b00010000, // part of the core eclipse library
        };
    
        // Enable bitwise operations
        constexpr Category operator|(Category lhs, Category rhs) {
            return static_cast<Category>(
                static_cast<std::underlying_type_t<Category>>(lhs) |
                static_cast<std::underlying_type_t<Category>>(rhs)
            );
        };

        constexpr Category operator&(Category lhs, Category rhs) {
            return static_cast<Category>(
                static_cast<std::underlying_type_t<Category>>(lhs) &
                static_cast<std::underlying_type_t<Category>>(rhs)
            );
        };

        inline bool operator!=(Category lhs, Category rhs) {
            return static_cast<std::underlying_type_t<Category>>(lhs) != 
                   static_cast<std::underlying_type_t<Category>>(rhs);
        };
    
        constexpr Verbosity ProjectVerbosity = Verbosity::Verbose;
        constexpr Category ProjectHideCategories (
            Category::OnTick
        );
    
        void dbgLog(const char *msg, Verbosity InVerbosity = Verbosity::Display, Category InHideCategories = Category::None);
        void dbgLog(std::string msg, Verbosity InVerbosity = Verbosity::Display, Category InHideCategories = Category::None);
    }
}