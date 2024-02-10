
#include <iostream>
#include "minisdk/buffer.hpp"
#include "minisdk/trace.hpp"
#include <algorithm>

using namespace minisdk;

Buffer::Buffer() {
    this->size = 0;
    this->data = nullptr;
    if constexpr (traceEnabled) std::cout << "Buffer created with size:" << this->size << std::endl;
}

Buffer::~Buffer() {
    delete[] this->data;
    if constexpr (traceEnabled) std::cout << "Buffer deleted with size:" << this->size << std::endl;
}

Buffer::Buffer(size_t size_): size(size_) {
    this->data = new unsigned char[this->size]();
    if constexpr (traceEnabled) std::cout << "Buffer created with size:" << this->size << std::endl;
}

Buffer::Buffer(const Buffer &other) {

    this->size = other.size;
    this->data = new unsigned char[this->size];
    std::copy(other.data, other.data + other.size, this->data);
    if constexpr (traceEnabled) std::cout << "Buffer copied with size:" << this->size << std::endl;
}

Buffer& Buffer::operator=(const Buffer &other) {
    Buffer tmp(other);
    swap(*this, tmp);
    if constexpr (traceEnabled) std::cout << "Buffer assigned with size:" << this->size << std::endl;

    return *this;
}

Buffer::Buffer(Buffer &&other) noexcept {

    this->size = other.size;
    this->data = other.data;
    other.data = nullptr;
    other.size = 0;
    if constexpr (traceEnabled) std::cout << "Buffer moved with size: " << this->size << std::endl;
}

Buffer& Buffer::operator=(Buffer &&other) noexcept {
    
    if (this == &other) {
        return *this;
    }

    delete[] this->data;
    this->size = other.size;
    this->data = other.data;
    other.data = nullptr;
    other.size = 0;
    if constexpr (traceEnabled) std::cout << "Buffer move-assigned with size:" << this->size << std::endl;

    return *this;
}

namespace minisdk {

std::ostream& operator<<(std::ostream& out, const Buffer &buffer) {
    for (size_t i = 0; i < buffer.size; ++i)
    {
        out << buffer.data[i] << " ";
    }

    return out;
}

void swap(Buffer& a, Buffer& b) noexcept {
    std::swap(a.data, b.data);
    std::swap(a.size, b.size);
}

} // namespace minisdk

unsigned char& Buffer::operator[](size_t index) {
    return this->data[index];
}

const unsigned char& Buffer::operator[](size_t index) const {
    return this->data[index];
}

bool Buffer::operator==(const Buffer& other) const {
    if (this->size != other.size) {
        return false;
    }
    for (size_t i=0; i<this->size; i++) {
        if (this->data[i] != other.data[i]) {
            return false;
        }
    }
    return true;

}
    
Buffer& Buffer::operator+=(const Buffer& other)
{
    size_t old_size = this->size;
    size_t new_size = old_size + other.size;

    unsigned char* new_data = new unsigned char[new_size];

    for (size_t i = 0; i < old_size; ++i)
        new_data[i] = this->data[i];

    for (size_t i = 0; i < other.size; ++i)
        new_data[old_size + i] = other.data[i];

    delete[] this->data;

    this->data = new_data;
    this->size = new_size;

    return *this;
}

size_t Buffer::getSize() const{
    return this->size;
}