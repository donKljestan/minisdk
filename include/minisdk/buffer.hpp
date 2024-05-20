#pragma once
#include <iostream>
#include <cstddef>

namespace minisdk {

class BufferPool;

class Buffer {
private:
    unsigned char *data;
    size_t size;
    // Null means plain new[]/delete[]. A pool must outlive every Buffer that was given it.
    BufferPool* pool;

    unsigned char* allocatePayload(size_t bytes) const;
    void freePayload(unsigned char* payload) const noexcept;

public:
    
    Buffer(); 
    explicit Buffer(size_t size_, BufferPool* pool_ = nullptr);
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
    BufferPool* allocator() const noexcept { return pool; }

};

}
