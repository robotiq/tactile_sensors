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

#ifndef FINGER_DATA_H
#define FINGER_DATA_H

#include <stdint.h>

#define FINGER_COUNT 2
#define FINGER_STATIC_TACTILE_ROW 4
#define FINGER_STATIC_TACTILE_COL 7
#define FINGER_STATIC_TACTILE_COUNT (FINGER_STATIC_TACTILE_ROW * FINGER_STATIC_TACTILE_COL)
#define FINGER_DYNAMIC_TACTILE_COUNT 1

struct FingerData
{
    uint16_t staticTactile[FINGER_STATIC_TACTILE_COUNT];
    int16_t dynamicTactile[FINGER_DYNAMIC_TACTILE_COUNT];
    int16_t accelerometer[3];
    int16_t gyroscope[3];
    int16_t temperature;
    uint16_t timestamp;
};

struct Fingers
{
    int64_t timestamp;
    FingerData finger[FINGER_COUNT];
};

#endif // FINGER_DATA_H
