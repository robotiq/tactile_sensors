/*
 * Robotiq Tactile Sensor UI
 * Copyright (C) 2016  Shahbaz Youssefi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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
