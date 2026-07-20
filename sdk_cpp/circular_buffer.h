// Copyright (c) 2016 Shahbaz Youssefi
// Copyright (c) 2026 Robotiq
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the copyright holder nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// Robotiq Tactile Sensor UI

#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <mutex>
#include <vector>

template<typename T>
class SafeCircularBuffer
{
public:
    SafeCircularBuffer(size_t size): buffer(size), start(0), count(0) {}
    ~SafeCircularBuffer() {}

    size_t size()
    {
        size_t s;

        mutex.lock();
        s = count;
        mutex.unlock();

        return s;
    }

    bool empty() { return size() == 0; }

    void push(const T &t)
    {
        mutex.lock();

        buffer[(start + count) % buffer.size()] = t;

        // If buffer is already full, throw away the oldest data
        if (count == buffer.size())
            start = (start + 1) % buffer.size();
        // Otherwise just indicate that there is more data
        else
            ++count;

        mutex.unlock();
    }

    T pop()
    {
        T t;

        mutex.lock();

        // They shouldn't call this function with empty buffer, so fail in this case
        if (count == 0)
        {
            mutex.unlock();
            return t;
        }

        t = buffer[start];

        start = (start + 1) % buffer.size();
        --count;

        mutex.unlock();

        return t;
    }

    T front()       // Oldest data
    {
        T t;

        mutex.lock();

        // They shouldn't call this function with empty buffer, so fail in this case
        if (count == 0)
        {
            mutex.unlock();
            return t;
        }

        t = buffer[start];

        mutex.unlock();

        return t;
    }

    T back()        // Newest data
    {
        T t;

        mutex.lock();

        // They shouldn't call this function with empty buffer, so fail in this case
        if (count == 0)
        {
            mutex.unlock();
            return t;
        }

        t = buffer[(start + count - 1) % buffer.size()];

        mutex.unlock();

        return t;
    }

    void clear()
    {
        mutex.lock();
        start = count = 0;
        mutex.unlock();
    }

    void extract(std::vector<T> &vec, bool consume = false)
    {
        mutex.lock();
        vec.resize(count);
        for (size_t i = 0; i < count; ++i)
            vec[i] = buffer[(start + i) % buffer.size()];
        if (consume)
            start = count = 0;
        mutex.unlock();
    }

private:
    std::mutex mutex;
    std::vector<T> buffer;
    size_t start;   // Next item to read
    size_t count;   // read + count is next item to write
};

#endif // CIRCULAR_BUFFER_H
