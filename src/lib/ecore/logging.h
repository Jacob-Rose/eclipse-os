// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>

#define DEBUG_LOGGING_ENABLED 1

namespace ecore
{
    namespace log
    {
        enum class Verbosity
        {
            VeryVerbose,
            Verbose,
            Display,
            Warning,
            Error
        };
    
        enum class Category
        {
            None        = 0b00000000,
            OnTick      = 0b00000001,
            StateInfo   = 0b00000010,
        };
    
        Category operator|(Category lhs, Category rhs);
    
        Category operator&(Category lhs, Category rhs);
    
        constexpr Verbosity ProjectVerbosity = Verbosity::VeryVerbose;
        constexpr Category ProjectHideCategories { 
            Category::OnTick
        };
    
        void dbgLog(const char *msg, Verbosity Verbosity = Verbosity::Display, Category HideCategories = Category::None);
    }
}