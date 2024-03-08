// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "logging.h"

#include <type_traits>

Category operator&(Category lhs, Category rhs) {
    return static_cast<Category>(
        static_cast<std::underlying_type_t<Category>>(lhs) &
        static_cast<std::underlying_type_t<Category>>(rhs)
    );
}

Category operator|(Category lhs, Category rhs) {
    return static_cast<Category>(
        static_cast<std::underlying_type_t<Category>>(lhs) |
        static_cast<std::underlying_type_t<Category>>(rhs)
    );
}

void jlog::print(const char *msg, Verbosity Verbosity, Category HideCategories)
{
    if(ProjectVerbosity > Verbosity)
    {
        return;
    }

    if((int)(ProjectHideCategories & HideCategories) != 0)
    {
        // easier for people to provide hide categories
        return;
    }

    Serial.println(msg);
}