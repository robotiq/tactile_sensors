// SPDX-License-Identifier: BSD-3-Clause
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

#ifndef FINGER_DATA_H
#define FINGER_DATA_H

#include <stdint.h>

#define FINGER_COUNT 2
#define FINGER_STATIC_TACTILE_ROW 7
#define FINGER_STATIC_TACTILE_COL 4
#define FINGER_STATIC_TACTILE_COUNT (FINGER_STATIC_TACTILE_ROW * FINGER_STATIC_TACTILE_COL)
#define FINGER_DYNAMIC_TACTILE_COUNT 1

struct FingerData
{
    // True when this finger's data was updated in the current packet.
    // Set by the parser, cleared by the SDK after dispatching the callback.
    bool newDataAvailable;

    uint16_t staticTactile[FINGER_STATIC_TACTILE_COUNT];
    int16_t dynamicTactile[FINGER_DYNAMIC_TACTILE_COUNT];
    int16_t accelerometer[3];
    int16_t gyroscope[3];
    int16_t temperature;
    uint64_t timestamp;

    // Baseline values for static tactile (initialized to 0)
    uint16_t baseline[FINGER_STATIC_TACTILE_COUNT];
};

struct Fingers
{
    int64_t timestamp;
    FingerData finger[FINGER_COUNT];
};

#endif // FINGER_DATA_H
