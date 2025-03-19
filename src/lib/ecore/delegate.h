// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <algorithm>
#include <vector>
#include <functional>

namespace ecore
{
    template<typename... Args>
    class MulticastDelegate {
    public:
        using Callback = std::function<void(Args...)>;
        
        void add(Callback callback) {
            callbacks.push_back(std::move(callback));
        }
        
        void remove(const Callback& callback) {
            callbacks.erase(
                std::remove_if(callbacks.begin(), callbacks.end(), [&](const Callback& cb) {
                    return cb.target_type() == callback.target_type();
                }),
                callbacks.end()
            );
        }
        
        void invoke(Args... args) const {
            for (const auto& callback : callbacks) {
                callback(args...);
            }
        }
    
        void operator()(Args... args) const {
            invoke(args...);
        }
    
    private:
        std::vector<Callback> callbacks;
    };    
}

