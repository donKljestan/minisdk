#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <mutex>

namespace minisdk {

    class EventBus {

        // subscribers per event name
        private:
            mutable std::mutex mutex_;
            std::unordered_map<std::string, std::vector<std::function<void(const std::string&)>>> event_map;

        public:
            void addEvent(const std::string& event);
            void addSubscriber(const std::string& event, const std::function<void(const std::string&)>& function);
            // void removeSubscriber(const std::string& event, std::function function);
            void removeEvent(const std::string& event);
            void eventHappen(const std::string& event, const std::string& message) const;

    };
}