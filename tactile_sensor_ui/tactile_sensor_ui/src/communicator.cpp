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

#include "communicator.h"
#include <QTime>
#include <QApplication>

enum UsbPacketSpecial
{
    USB_PACKET_START_BYTE = 0x9A,
};

enum UsbCommands
{
    USB_COMMAND_READ_SENSORS = 0x61,
    USB_COMMAND_AUTOSEND_SENSORS = 0x58,

    USB_COMMAND_ENTER_BOOTLOADER = 0xE2,
};

// Sensor types occupy the higher 4 bits, the 2 bits lower than that identify finger, and the lower 2 bits is used as an index.
enum UsbSensorType
{
    USB_SENSOR_TYPE_STATIC_TACTILE = 0x10,
    USB_SENSOR_TYPE_DYNAMIC_TACTILE = 0x20,
    USB_SENSOR_TYPE_ACCELEROMETER = 0x30,
    USB_SENSOR_TYPE_GYROSCOPE = 0x40,
    USB_SENSOR_TYPE_TEMPERATURE = 0x60,
    USB_SENSOR_TYPE_TIMESTAMP = 0x70,
};

struct UsbPacket
{
    uint8_t start_byte;
    uint8_t crc8;           // over command, data_length and data
    uint8_t command;        // 4 bits of flag (MSB) and 4 bits of command (LSB)
    uint8_t data_length;
    uint8_t data[60];
};

static uint8_t calcCrc8(uint8_t *data, size_t len)
{
    // TODO: calculate CRC8
    return data[-1];
}

static void usbSend(QSerialPort *port, UsbPacket *packet)
{
    uint8_t *p = (uint8_t *)packet;

    packet->start_byte = USB_PACKET_START_BYTE;
    packet->crc8 = calcCrc8(p + 2, packet->data_length + 2);

    port->write((char *)p, packet->data_length + 4);
    port->waitForBytesWritten(1);
}

static bool usbReadByte(UsbPacket *packet, unsigned int *readSoFar, uint8_t d)
{
    uint8_t *p = (uint8_t *)packet;

    // Make sure start byte is seen
    if (*readSoFar == 0 && d != USB_PACKET_START_BYTE)
        return false;

    // Buffer the byte (making sure not to overflow the packet)
    if (*readSoFar < 64)
        p[*readSoFar] = d;
    ++*readSoFar;

    // If length is read, stop when done
    if (*readSoFar > 3 && *readSoFar >= (unsigned)packet->data_length + 4)
    {
        *readSoFar = 0;

        // If CRC is ok, we have a new packet!  Return it.
        if (packet->crc8 == calcCrc8(p + 2, packet->data_length + 2))
            return true;

        // If CRC is not ok, find the next start byte and shift the packet back in hopes of getting back in sync
        for (unsigned int i = 1; i < (unsigned)packet->data_length + 4; ++i)
            if (p[i] == USB_PACKET_START_BYTE)
            {
                memmove(p, p + i, packet->data_length + 4 - i);
                *readSoFar = packet->data_length + 4 - i;
                break;
            }
    }

    return false;
}

static inline uint16_t parseBigEndian2(uint8_t *data)
{
    return (uint16_t)data[0] << 8 | data[1];
}

static uint8_t extractUint16(uint16_t *to, uint16_t toCount, uint8_t *data, unsigned int size)
{
    unsigned int cur;

    // Extract 16-bit values.  If not enough data, extract as much data as available
    for (cur = 0; 2 * cur + 1 < size && cur < toCount; ++cur)
        to[cur] = parseBigEndian2(&data[2 * cur]);

    // Return number of bytes read
    return cur * 2;
}

static bool parseSensors(UsbPacket *packet, Fingers *fingers)
{
    bool sawDynamic = false;
    for (unsigned int i = 0; i < packet->data_length;)
    {
        uint8_t sensorType = packet->data[i] & 0xF0;
        uint8_t f= packet->data[i] >> 2 & 0x03;
        ++i;

        uint8_t *sensorData = packet->data + i;
        unsigned int sensorDataBytes = packet->data_length - i;

        switch (sensorType)
        {
        case USB_SENSOR_TYPE_DYNAMIC_TACTILE:
            i += extractUint16((uint16_t *)fingers->finger[f].dynamicTactile, FINGER_DYNAMIC_TACTILE_COUNT, sensorData, sensorDataBytes);
            sawDynamic = true;
            break;
        case USB_SENSOR_TYPE_STATIC_TACTILE:
            i += extractUint16(fingers->finger[f].staticTactile, FINGER_STATIC_TACTILE_COUNT, sensorData, sensorDataBytes);
            break;
        case USB_SENSOR_TYPE_ACCELEROMETER:
            i += extractUint16((uint16_t *)fingers->finger[f].accelerometer, 3, sensorData, sensorDataBytes);
            break;
        case USB_SENSOR_TYPE_GYROSCOPE:
            i += extractUint16((uint16_t *)fingers->finger[f].gyroscope, 3, sensorData, sensorDataBytes);
            break;
        case USB_SENSOR_TYPE_TEMPERATURE:
            i += extractUint16((uint16_t *)&fingers->finger[f].temperature, 1, sensorData, sensorDataBytes);
            break;
        case USB_SENSOR_TYPE_TIMESTAMP:
            i += extractUint16(&fingers->finger[f].timestamp, 1, sensorData, sensorDataBytes);
            break;
        default:
             // Unknown sensor, we can't continue parsing anything from here on
             return sawDynamic;
        }
    }

    /*
     * Return true every time dynamic data is read.  This is used to identify when a whole set of data has
     * arrived and needs to be processed.
     */
    return sawDynamic;
}

Communicator::Communicator(MainWindow *w_, const char *portName, unsigned int ms):
    w(w_), period_ms(ms), receiveBuffer(1024)
{
    port = new QSerialPort;

    port->setPortName(portName);
    port->setBaudRate(QSerialPort::Baud115200);
    port->setDataBits(QSerialPort::Data8);
    port->setParity(QSerialPort::NoParity);
    port->setStopBits(QSerialPort::OneStop);
    port->setFlowControl(QSerialPort::NoFlowControl);

    port->open(QIODevice::ReadWrite);

    port->moveToThread(this);
}

Communicator::~Communicator()
{
    requestInterruption();
    wait();

    delete port;
}

void Communicator::run()
{
    UsbPacket send;
    UsbPacket recv;
    unsigned int recvSoFar = 0;

    // A timer for timestamps
    QTime timestamp;
    timestamp.start();

    // Timer and info used to calculate data rate
    QTime dataRate;
    dataRate.start();
    unsigned int receivedBytes = 0;

    // Gathered data
    Fingers fingers = {0};

    // Send auto-send message
    send.command = USB_COMMAND_AUTOSEND_SENSORS;
    send.data_length = 1;
    send.data[0] = period_ms;
    usbSend(port, &send);

    while (!isInterruptionRequested())
    {
        // Call waitForReadyRead to process messages (remember, this thread doesn't have a QT event loop)
        port->waitForReadyRead(1);

        int64_t available = port->bytesAvailable();
        if (available <= 0)
            continue;

        // Make sure there is enough room in the buffer
        if (receiveBuffer.size() < available)
            receiveBuffer.resize(available);

        available = port->read(receiveBuffer.data(), available);

        // Show progress
        receivedBytes += available;
        if (dataRate.elapsed() > 200)
        {
            int elapsed = dataRate.restart();
            emit w->updateConnectionDataRateSignal((uint64_t)receivedBytes * 1000 / elapsed);
            receivedBytes = 0;
        }

        // Parse packets and store sensor values
        for (int64_t i = 0; i < available; ++i)
        {
            if (usbReadByte(&recv, &recvSoFar, receiveBuffer[i]))
            {
                bool newSetOfData = parseSensors(&recv, &fingers);

                // Many messages can arrive in the same millisecond, so let the data accumulate and store it only when a whole set is complete
                if (newSetOfData)
                {
                    fingers.timestamp = timestamp.elapsed();
                    emit w->newFingerDataSignal(fingers);
                }
            }
        }
    }

    // Stop auto-send message
    send.command = USB_COMMAND_AUTOSEND_SENSORS;
    send.data_length = 1;
    send.data[0] = 0;
    usbSend(port, &send);

    port->moveToThread(QApplication::instance()->thread());
}
