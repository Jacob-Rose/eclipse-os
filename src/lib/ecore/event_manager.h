#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include <unordered_map>
#include <string>
#include <functional>
#include "delegate.h"
#include "name.h"

using namespace ecore;

// Hash and equality for Name in unordered_map
namespace std {
    template<>
    struct hash<ecore::Name> {
        size_t operator()(const ecore::Name& name) const {
            return name.GetHash();
        }
    };
}

/**
 * Event data structure
 * Contains event name and optional custom data pointer
 */
struct Event {
    Name name;
    void* customData;

    Event(const Name& eventName, void* data = nullptr)
        : name(eventName), customData(data) {}

    // Overload for const char* for backward compatibility
    Event(const char* eventName, void* data = nullptr)
        : name(Name(eventName)), customData(data) {}
};

/**
 * EventManager - Lightweight event system for situational use
 *
 * Usage:
 *   EventManager mqtt_events;
 *   mqtt_events.subscribe("message_received", [](const Event& e) {
 *       // Handle event
 *   });
 *   mqtt_events.publish("message_received");
 */
class EventManager {
public:
    using EventHandler = std::function<void(const Event&)>;
    using EventDelegate = MulticastDelegate<const Event&>;

    EventManager() = default;
    ~EventManager() = default;

    /**
     * Subscribe to an event by name
     * @param eventName Name of the event to listen for
     * @param handler Callback function to invoke when event is published
     */
    void subscribe(const char* eventName, EventHandler handler) {
        Name key(eventName);
        handlers[key].add(handler);
    }

    /**
     * Subscribe to an event by name (string version)
     */
    void subscribe(const std::string& eventName, EventHandler handler) {
        Name key(eventName);
        handlers[key].add(handler);
    }

    /**
     * Subscribe to an event by Name
     */
    void subscribe(const Name& eventName, EventHandler handler) {
        handlers[eventName].add(handler);
    }

    /**
     * Unsubscribe all handlers for a specific event
     * @param eventName Name of the event to unsubscribe from
     */
    void unsubscribe(const char* eventName) {
        Name key(eventName);
        handlers.erase(key);
    }

    /**
     * Unsubscribe all handlers for a specific event (Name version)
     */
    void unsubscribe(const Name& eventName) {
        handlers.erase(eventName);
    }

    /**
     * Publish an event to all subscribers
     * @param event Event to publish
     */
    void publish(const Event& event) {
        auto it = handlers.find(event.name);
        if (it != handlers.end()) {
            it->second.invoke(event);
        }
    }

    /**
     * Publish an event by name
     * @param eventName Name of the event
     * @param customData Optional custom data pointer
     */
    void publish(const char* eventName, void* customData = nullptr) {
        Event event(eventName, customData);
        publish(event);
    }

    /**
     * Publish an event by Name
     */
    void publish(const Name& eventName, void* customData = nullptr) {
        Event event(eventName, customData);
        publish(event);
    }

    /**
     * Check if any handlers are registered for an event
     * @param eventName Name of the event to check
     * @return true if handlers exist
     */
    bool hasHandlers(const char* eventName) const {
        Name key(eventName);
        return handlers.find(key) != handlers.end();
    }

    /**
     * Check if any handlers are registered for an event (Name version)
     */
    bool hasHandlers(const Name& eventName) const {
        return handlers.find(eventName) != handlers.end();
    }

    /**
     * Clear all event handlers
     */
    void clear() {
        handlers.clear();
    }

private:
    std::unordered_map<Name, EventDelegate> handlers;
};

#endif // EVENT_MANAGER_H
