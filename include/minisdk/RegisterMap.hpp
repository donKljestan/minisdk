#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace minisdk {

    template <typename T>
    class RegisterMap {
        private:
            std::unordered_map<std::string, T> registers_;
        public:
            void add(const std::string& name, const T& value) {
                if (!registers_.insert({name, value}).second) {
                    throw std::invalid_argument("RegisterMap::add: register already exists: " + name);
                }
            }
            void remove(const std::string& name) {
                registers_.erase(name);
            }
            void update(const std::string& name, const T& value) {
                registers_.at(name) = value;
            }
            const T& get(const std::string& name) const {
                return registers_.at(name);
            }
            bool contains(const std::string& name) const {
                return registers_.contains(name);
            }
            void print() const {
                for (const auto& [name, value] : registers_) {
                    std::cout << name << ":" << value << std::endl;
                }
            }
            size_t size() const {
                return registers_.size();
            }

            std::vector<std::string> keys() const {
                std::vector<std::string> result;
                result.reserve(registers_.size());
                std::transform(registers_.begin(), registers_.end(), std::back_inserter(result),
                               [](const auto& entry) { return entry.first; });
                return result;
            }

            std::vector<std::string> diff(const RegisterMap<T>& other) const {
                std::vector<std::string> allKeys = keys();
                std::vector<std::string> changed;
                std::copy_if(allKeys.begin(), allKeys.end(), std::back_inserter(changed),
                             [this, &other](const std::string& name) {
                                 return !other.contains(name) || other.get(name) != get(name);
                             });
                return changed;
            }

            bool operator==(const RegisterMap<T>& other) const {
                if (registers_.size() != other.registers_.size()) return false;
                return std::all_of(registers_.begin(), registers_.end(), [&other](const auto& entry) {
                    return other.contains(entry.first) && other.get(entry.first) == entry.second;
                });
            }
    };
}