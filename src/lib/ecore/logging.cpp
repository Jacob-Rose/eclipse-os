// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "logging.h"

#include <string>
#include "../ecore/core.h"

#include <type_traits>

using namespace ecore::log;

std::string ecore::log::getVerbosityString(ecore::log::Verbosity inVerbosity)
{
    switch(inVerbosity)
    {
        case Verbosity::VeryVerbose: return "VeryVerbose";
        case Verbosity::Verbose:     return "Verbose";
        case Verbosity::Display:     return "Display";
        case Verbosity::Warning:     return "Warning";
        case Verbosity::Error:       return "Error";
        case Verbosity::Fatal:       return "Fatal";
        default:                     return "UnknownVerbosity";
    }

    return "";
}

std::string padString(std::string input, int length) {
    while (input.length() < length) {
        input += " ";  // Append spaces until desired length
    }
    return input.substr(0, length);  // Trim if too long
}

void ecore::log::dbgLog(const char *InMsg, Verbosity InVerbosity, Category InHideCategories)
{
#if DEBUG_LOGGING_ENABLED
    if(ProjectVerbosity > InVerbosity)
    {
        return;
    }


    if((ProjectHideCategories & InHideCategories) != Category::None)
    {
        // easier for people to provide hide categories

        // bitshift print each bit of the category to see if it is set
        //dbgLog("dbgLog: " + std::to_string(static_cast<std::underlying_type_t<Category>>(InHideCategories)), Verbosity::Verbose, Category::None);
        //dbgLog("dbgLog &: " + std::to_string(static_cast<std::underlying_type_t<Category>>((ProjectHideCategories & InHideCategories))), Verbosity::Verbose, Category::None);
        return;
    }

    std::string verbosityMsg = padString(getVerbosityString(InVerbosity).c_str(), 15);
    Serial.print(verbosityMsg.c_str());
    Serial.print(" | ");
    Serial.println(InMsg);
#endif
}

void ecore::log::dbgLog(std::string msg, Verbosity InVerbosity, Category InHideCategories)
{ 
    dbgLog(msg.c_str(), InVerbosity, InHideCategories);
}