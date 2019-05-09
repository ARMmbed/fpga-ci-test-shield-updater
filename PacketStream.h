/*
 * Copyright (c) 2019, Arm Limited and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PACKET_STREAM_H
#define PACKET_STREAM_H

#include "FileHandle.h"
#include <stdlib.h>
#include <stdint.h>


class PacketStream {
public:

    /**
     * Create a new PacketStream
     *
     * @param io FileHandle to write to and read from
     */
    PacketStream(mbed::FileHandle *io);

    /**
     * Write a delimited packet of the given size
     *
     * On the receiving end a call to read will return
     * a packet of this same size.
     *
     * @param data Block of data to send
     * @param size Size of data to send
     * @return true if the packet was sent, false otherwise
     */
    bool write(const uint8_t *data, size_t size);

    /**
     * Read a delimited packet
     *
     * If the buffer passed in is not big enough to store all the data
     * the extra will be discarded.
     *
     * @param data Buffer to write the packet to
     * @param size Size of the buffer
     * @param actual The actual size of the packet
     * @return true if a valid packet was read, false otherwise
     */
    bool read(uint8_t *data, size_t size, size_t *actual);

    /**
     * Read a delimited packet in the given format
     *
     * @param format format string to use
     * @return The number of arguments written successfully or negative
     * if an error occurred
     */
    int scanf(const char *format, ...);

    /**
     * Print a delimited packet in the given format
     *
     * @param format string to use
     * @return true if the packet was sent, false otherwise
     */
    bool printf(const char *format, ...);

    /*
     * Testing only function to run a self test
     *
     * @return true if the test passed
     */
    static bool self_test();

private:

    enum DecodingStatus {
        DecodingDone = 0,
        DecodingContinue = 1,
        DecodingCobsError = 2,
        DecodingFileError = 3
    };

    bool _cobs_write(const uint8_t *data, size_t size, bool last);

    DecodingStatus _getc(uint8_t *val);

    DecodingStatus _cobs_getc(uint8_t *val);

    uint16_t _tx_pos;
    uint8_t _tx_buf[256 + 1];

    uint8_t _rx_next_zero;
    bool _rx_next_pad;

    mbed::FileHandle *_io;

};

#endif
