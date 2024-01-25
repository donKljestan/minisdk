

#include "minisdk/EventBus.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace minisdk {

void EventBus::addEvent(const std::string& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!event_map.insert({event, {}}).second) {
        throw std::invalid_argument("addEvent: event '" + event + "' is already registered");
    }
}

void EventBus::addSubscriber(const std::string& event, const std::function<void(const std::string&)>& function) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto search = event_map.find(event);
    if (search == event_map.end()) {
        throw std::invalid_argument("addSubscriber: event '" + event + "' was not registered via addEvent()");
    }
    search->second.push_back(function);
}

void EventBus::removeEvent(const std::string& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_map.erase(event);
}

void EventBus::eventHappen(const std::string& event, const std::string& message) const {
    // Copy the subscribers so a callback may modify the bus without invalidating iterators,
    // and invoke them outside the lock (otherwise addEvent() from a callback would deadlock).
    std::vector<std::function<void(const std::string&)>> subscribers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto search = event_map.find(event);
        if (search == event_map.end()) {
            return;
        }
        subscribers = search->second;
    }
    for (const auto& f : subscribers) {
        f(message);
    }
}

} // namespace minisdk

// void EventBus::removeSubscriber(const std::string& event, const std::function<void(const std::string&)>& function) {
//     auto search = event_map.find(event);
//     if (search == event_map.end()) {
//         return;
//     }
//     search->second.erase(function); // not possible: std::function has no operator==, needs a token/id
// }