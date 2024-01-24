#pragma once
#include <iostream>
#include <cstddef>

namespace minisdk {

class Buffer {
private:
    unsigned char *data;
    size_t size;
public:
    
    Buffer(); 
    explicit Buffer(size_t size_); 
    ~Buffer(); //destruktor
    Buffer(const Buffer &other); 
    Buffer& operator=(const Buffer &other); 
    Buffer(Buffer &&other) noexcept; 
    Buffer& operator=(Buffer &&other) noexcept;
    unsigned char& operator[](size_t index);
    const unsigned char& operator[](size_t index) const;
    bool operator==(const Buffer& other) const;
    Buffer& operator+=(const Buffer& other);
    friend std::ostream& operator<<(std::ostream& out, const Buffer &buffer);
    friend void swap(Buffer& a, Buffer& b) noexcept;
    size_t getSize() const;

};

}
