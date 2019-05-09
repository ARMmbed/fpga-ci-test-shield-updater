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

#include "PacketStream.h"
#include <string.h>
#include <cstdarg>

#define PACKET_STREAM_PRINTF(...)
#define STRING_STACK_LIMIT    120

PacketStream::PacketStream(mbed::FileHandle *io)
{
    _tx_buf[0] = 0;
    _tx_pos = 1;

    _rx_next_zero = 1;
    _rx_next_pad = true;

    _io = io;
}

bool PacketStream::write(const uint8_t *data, size_t size)
{
    return _cobs_write(data, size, true);
}

bool PacketStream::read(uint8_t *data, size_t size, size_t *actual)
{
    uint8_t val;
    DecodingStatus status;
    size_t pos = 0;
    while (true) {
        status = _cobs_getc(&val);
        if (status == DecodingContinue) {
            if (pos < size) {
                data[pos] = val;
            }
            pos++;
            continue;
        }
        if (status == DecodingDone) {
            *actual = pos;
            return true;
        }

        // Return failure on any other status
        return false;
    }
}

int PacketStream::scanf(const char *format, ...)
{
    bool ret;
    uint8_t buf[64 + 1];
    size_t actual = 0;

    if (!read(buf, sizeof(buf) - 1, &actual)) {
        return 0;
    }
    buf[actual] = 0;

    std::va_list arg;
    va_start(arg, format);

    ret = vsscanf((char *)buf, format, arg);

    va_end(arg);
    return ret;
}

bool PacketStream::printf(const char *format, ...)
{
    bool ret;
    std::va_list arg;
    va_start(arg, format);
    // ARMCC microlib does not properly handle a size of 0.
    // As a workaround supply a dummy buffer with a size of 1.
    char dummy_buf[1];
    int len = vsnprintf(dummy_buf, sizeof(dummy_buf), format, arg);
    if (len < STRING_STACK_LIMIT) {
        char temp[STRING_STACK_LIMIT];
        vsprintf(temp, format, arg);
        ret = write((uint8_t*)temp, len);
    } else {
        char *temp = new char[len + 1];
        vsprintf(temp, format, arg);
        ret = write((uint8_t*)temp, len);
        delete[] temp;
    }
    va_end(arg);
    return ret;
}

bool PacketStream::_cobs_write(const uint8_t *data, size_t size, bool last)
{
    bool success = true;
    for (size_t i = 0; i < size; i++) {

        // Flush and stuff if not enough zeros
        if (_tx_pos >= 255) {
            _tx_buf[0] = _tx_pos;
            if (_io->write(_tx_buf, _tx_pos) != _tx_pos) {
                success = false;
            }
            _tx_pos = 0;

            _tx_buf[_tx_pos] = 0;
            _tx_pos++;
        }

        // Escape and flush if zero
        if (data[i] == 0) {
            _tx_buf[0] = _tx_pos;
            if (_io->write(_tx_buf, _tx_pos) != _tx_pos) {
                success = false;
            }
            _tx_pos = 0;
        }

        // Add byte
        _tx_buf[_tx_pos] = data[i];
        _tx_pos++;
    }

    if (last) {
        _tx_buf[0] = _tx_pos;

        // Add delimiter 0 (not actually part of COBS)
        _tx_buf[_tx_pos] = 0;
        _tx_pos++;

        if (_io->write(_tx_buf, _tx_pos) != _tx_pos) {
            success = false;
        }

        // Reset cobs
        _tx_buf[0] = 0;
        _tx_pos = 1;
    }
    return success;
}

PacketStream::DecodingStatus PacketStream::_cobs_getc(uint8_t *val)
{
    uint8_t data;
    DecodingStatus status = _getc(&data);
    if (status != DecodingContinue) {
        return status;
    }
    _rx_next_zero--;

    // Check for end of packet
    if (data == 0) {
        // Pointer to end of packet must be valid
        bool valid = _rx_next_zero == 0 ? true : false;
        _rx_next_zero = 1;
        _rx_next_pad = true;
        return valid ? DecodingDone : DecodingCobsError;
    }

    // Check for cobs encoded data
    if (_rx_next_zero == 0) {
        bool prev_rx_next_pad = _rx_next_pad;
        _rx_next_zero = data;
        _rx_next_pad = data == 255 ? true : false;

        if (prev_rx_next_pad) {
            // Guaranteed max two levels of recursion
            return _cobs_getc(val);
        } else {
            *val = 0;
            return DecodingContinue;
        }

    }

    // regular data
    *val = data;
    return DecodingContinue;
}

PacketStream::DecodingStatus PacketStream::_getc(uint8_t *val)
{
    if (_io->read(val, 1) == 1) {
        return DecodingContinue;
    }
    return DecodingFileError;
}

/*** Testing only below this point ***/

static void print_hex(const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        PACKET_STREAM_PRINTF(" 0x%02x", data[i]);
    }
}

class PacketStreamTestFileHandle : public mbed::FileHandle {
public:

    PacketStreamTestFileHandle() {
        _read_buf = 0;
        _read_size = 0;
        _read_pos = 0;

        _write_buf = 0;
        _write_size = 0;
        _write_pos = 0;
        _write_valid = false;
    }

    void write_set_expected(const uint8_t *buffer, size_t size) {
        _write_valid = true;
        _write_buf = (const uint8_t*)buffer;
        _write_size = size;
        _write_pos = 0;
    }

    bool write_valid() {
        return _write_valid && (_write_pos == _write_size);
    }

    void read_set_next(const uint8_t *buffer, size_t size) {
        _read_buf = (const uint8_t*)buffer;
        _read_pos = 0;
        _read_size = size;
    }

    bool read_done()
    {
        return _read_pos == _read_size;
    }

    virtual ssize_t read(void *buffer, size_t size) {
        const size_t available = _read_size - _read_pos;
        const size_t read_size = size > available ? available : size;
        memcpy(buffer, _read_buf + _read_pos, read_size);
        _read_pos += read_size;
        PACKET_STREAM_PRINTF("Read: ");
        print_hex((const uint8_t*)buffer, read_size);
        PACKET_STREAM_PRINTF("\n");
        return read_size;
    }

    virtual ssize_t write(const void *buffer, size_t size) {
        PACKET_STREAM_PRINTF("Write: ");
        print_hex((const uint8_t*)buffer, size);
        PACKET_STREAM_PRINTF("\n");
        if (_write_pos + size > _write_size) {
            // Too much data written
            _write_valid = false;
            return size;
        }

        if (memcmp(buffer, _write_buf + _write_pos, size) != 0) {
            // Wrong data written
            _write_valid = false;
            return size;
        }
        _write_pos += size;

        return size;
    }

    virtual off_t seek(off_t offset, int whence = SEEK_SET) {
        return 0;
    }

    virtual int close() {
        return 0;
    }

private:
    const uint8_t *_read_buf;
    size_t _read_size;
    size_t _read_pos;

    const uint8_t *_write_buf;
    size_t _write_size;
    size_t _write_pos;
    bool _write_valid;
};

class PacketStreamLoopFileHandle : public mbed::FileHandle {
public:

    PacketStreamLoopFileHandle() {
        _head = 0;
        _tail = 0;
        _size = sizeof(_buf);
    }

    virtual ssize_t read(void *buffer, size_t size) {
        uint8_t *buf = (uint8_t *)buffer;
        const size_t available = _used();
        const size_t read_size = size < available ? size : available;
        for (size_t i = 0; i < read_size; i++) {
            buf[i] = _buf[_head];
            _head++;
            if (_head >= _size) {
                _head = 0;
            }
        }
        return read_size;
    }

    virtual ssize_t write(const void *buffer, size_t size) {
        const uint8_t *buf = (const uint8_t *)buffer;
        size_t free = _free();
        if (free < size) {
            return -1;
        }
        for (size_t i = 0; i < size; i++) {
            _buf[_tail] = buf[i];
            _tail++;
            if (_tail >= _size) {
                _tail = 0;
            }
        }

        return size;
    }

    virtual off_t seek(off_t offset, int whence = SEEK_SET) {
        return 0;
    }

    virtual int close() {
        return 0;
    }

private:
    uint8_t _buf[2048];
    size_t _head;
    size_t _tail;
    size_t _size;

    size_t _used() {
        if (_head > _tail) {
            return _tail + _size - _head;
        } else {
            return _tail - _head;
        }
    }


    size_t _free() {
        return _size - _used() - 1;
    }
};

typedef struct {
     const uint8_t *decoded;
     size_t decoded_size;
     const uint8_t *encoded;
     size_t encoded_size;
} test_vector_t;

 static bool get_test_vector(size_t index, const uint8_t **decoded, size_t *decoded_size, const uint8_t **encoded, size_t *encoded_size)
{
    static const uint8_t decoded0[] = {0x00};
    static const uint8_t encoded0[] = {0x01, 0x01, 0x00};

    static const uint8_t decoded1[] = {0x00, 0x00};
    static const uint8_t encoded1[] = {0x01, 0x01, 0x01, 0x00};

    static const uint8_t decoded2[] = {0x11, 0x22, 0x00, 0x33};
    static const uint8_t encoded2[] = {03, 0x11, 0x22, 0x02, 0x33, 0x00};

    static const uint8_t decoded3[] = {0x11, 0x22, 0x33, 0x44};
    static const uint8_t encoded3[] = {05, 0x11, 0x22, 0x33, 0x44, 0x00};

    static const uint8_t decoded4[] = {0x11, 0x00, 0x00, 0x00};
    static const uint8_t encoded4[] = {02, 0x11, 0x01, 0x01, 0x01, 0x00};

    static uint8_t decoded5[254];
    static uint8_t encoded5[255 + 1];

    static uint8_t decoded6[255];
    static uint8_t encoded6[256 + 1];

    static uint8_t decoded7[255];
    static uint8_t encoded7[257 + 1];

    static uint8_t decoded8[255];
    static uint8_t encoded8[257 + 1];

    static uint8_t decoded9[255];
    static uint8_t encoded9[256 + 1];

    static uint8_t encoded10[] = {0x01, 0x00};

    static const test_vector_t test_vectors[] = {
        {decoded0, sizeof(decoded0), encoded0, sizeof(encoded0)},
        {decoded1, sizeof(decoded1), encoded1, sizeof(encoded1)},
        {decoded2, sizeof(decoded2), encoded2, sizeof(encoded2)},
        {decoded3, sizeof(decoded3), encoded3, sizeof(encoded3)},
        {decoded4, sizeof(decoded4), encoded4, sizeof(encoded4)},
        {decoded5, sizeof(decoded5), encoded5, sizeof(encoded5)},
        {decoded6, sizeof(decoded6), encoded6, sizeof(encoded6)},
        {decoded7, sizeof(decoded7), encoded7, sizeof(encoded7)},
        {decoded8, sizeof(decoded8), encoded8, sizeof(encoded8)},
        {decoded9, sizeof(decoded9), encoded9, sizeof(encoded9)},
        {0, 0, encoded10, sizeof(encoded10)},
    };

    static bool init = false;

    if (!init) {
        // 01 02 03 ... FD FE
        for (size_t i = 0; i < sizeof(decoded5); i++) {
            decoded5[i] = i + 1;
        }
        // FF 01 02 03 ... FD FE
        for (size_t i = 0; i < sizeof(encoded5); i++) {
            encoded5[i] = i;
        }
        encoded5[0] = 0xFF;
        encoded5[sizeof(encoded5) - 1] = 0x00;

        // 00 01 02 ... FC FD FE
        for (size_t i = 0; i < sizeof(decoded6); i++) {
            decoded6[i] = i;
        }
        // 01 FF 01 02 ... FC FD FE
        for (size_t i = 0; i < sizeof(encoded6); i++) {
            encoded6[i] = i - 1;
        }
        encoded6[0] = 0x01;
        encoded6[1] = 0xFF;
        encoded6[sizeof(encoded6) - 1] = 0x00;

        // 01 02 03 ... FD FE FF
        for (size_t i = 0; i < sizeof(decoded7); i++) {
            decoded7[i] = i + 1;
        }
        // FF 01 02 03 ... FD FE 02 FF
        for (size_t i = 0; i < sizeof(encoded7); i++) {
            encoded7[i] = i;
        }
        encoded7[0] = 0xFF;
        encoded7[sizeof(encoded7) - 3] = 0x02;
        encoded7[sizeof(encoded7) - 2] = 0xFF;
        encoded7[sizeof(encoded7) - 1] = 0x00;

        // 02 03 04 ... FE FF 00
        for (size_t i = 0; i < sizeof(decoded8); i++) {
            decoded8[i] = i + 2;
        }
        // FF 02 03 04 ... FE FF 01 01
        for (size_t i = 0; i < sizeof(encoded8); i++) {
            encoded8[i] = i + 1;
        }
        encoded8[0] = 0xFF;
        encoded8[sizeof(encoded8) - 3] = 0x01;
        encoded8[sizeof(encoded8) - 2] = 0x01;
        encoded8[sizeof(encoded8) - 1] = 0x00;

        // 03 04 05 ... FF 00 01
        for (size_t i = 0; i < sizeof(decoded9); i++) {
            decoded9[i] = i + 3;
        }
        // FE 03 04 05 ... FF 02 01
        for (size_t i = 0; i < sizeof(encoded9); i++) {
            encoded9[i] = i + 2;
        }
        encoded9[0] = 0xFE;
        encoded9[sizeof(encoded9) - 3] = 0x02;
        encoded9[sizeof(encoded9) - 2] = 0x01;
        encoded9[sizeof(encoded9) - 1] = 0x00;

        init = true;
    }

    if (index < sizeof(test_vectors) / sizeof(test_vectors[0])) {
        *decoded = test_vectors[index].decoded;
        *decoded_size = test_vectors[index].decoded_size;
        *encoded = test_vectors[index].encoded;
        *encoded_size = test_vectors[index].encoded_size;
        return true;
    } else {
        return false;
    }
}

bool PacketStream::self_test()
{
    PacketStreamTestFileHandle mock;
    PacketStream serial(&mock);
    const uint8_t *decoded;
    size_t decoded_size;
    const uint8_t *encoded;
    size_t encoded_size;

    // Test writing to the serial port
    for (size_t i = 0; get_test_vector(i, &decoded, &decoded_size, &encoded, &encoded_size); i++) {
        PACKET_STREAM_PRINTF("Testing %u\n", i);
        mock.write_set_expected(encoded, encoded_size);
        serial.write(decoded, decoded_size);
        if (!mock.write_valid()) {
            PACKET_STREAM_PRINTF("Wrong encoding for %u\n", i);
            PACKET_STREAM_PRINTF("Expected: ");
            print_hex(encoded, encoded_size);
            PACKET_STREAM_PRINTF("\n");
            return false;
        }
    }

    // Test reading from the serial port
    for (size_t i = 0; get_test_vector(i, &decoded, &decoded_size, &encoded, &encoded_size); i++) {
        uint8_t actual_decoded[512];
        size_t actual_size;
        PACKET_STREAM_PRINTF("Testing %u\n", i);
        mock.read_set_next(encoded, encoded_size);

        memset(actual_decoded, 0, sizeof(actual_decoded));
        actual_size = 0;
        serial.read(actual_decoded, sizeof(actual_decoded), &actual_size);

        if (actual_size != decoded_size) {
            PACKET_STREAM_PRINTF("Wrong data size, expected %u got %u\n", decoded_size, actual_size);
            return false;
        }
        if (memcmp(decoded, actual_decoded, actual_size) != 0) {
            PACKET_STREAM_PRINTF("Wrong data.\n");
            PACKET_STREAM_PRINTF("Expected ");
            print_hex(decoded, decoded_size);
            PACKET_STREAM_PRINTF("\nGot ");
            print_hex(actual_decoded, actual_size);
            PACKET_STREAM_PRINTF("\n");
            return false;
        }
    }

    // Test bad cobs
    {
        uint8_t actual_decoded[512];
        size_t actual_size;

        static const uint8_t bad_encoded[] = {0x01, 0x02, 0x00};

        static const uint8_t good_decoded[] = {0x00};
        static const uint8_t good_encoded[] = {0x01, 0x01, 0x00};

        mock.read_set_next(bad_encoded, sizeof(bad_encoded));
        memset(actual_decoded, 0, sizeof(actual_decoded));
        actual_size = 0xFFFFFFFF;
        bool ret = serial.read(actual_decoded, sizeof(actual_decoded), &actual_size);
        if (ret) {
            PACKET_STREAM_PRINTF("Invalid COBS not detected\n");
            return false;
        }

        mock.read_set_next(good_encoded, sizeof(good_encoded));
        memset(actual_decoded, 0, sizeof(actual_decoded));
        actual_size = 0;
        ret = serial.read(actual_decoded, sizeof(actual_decoded), &actual_size);
        if (!ret || (actual_size != sizeof(good_decoded)) || (memcmp(actual_decoded, good_decoded, actual_size) != 0)) {
            PACKET_STREAM_PRINTF("Failed to recover from bad packet %u %u\n", actual_size, sizeof(good_decoded));
        }
    }

    // Test 0 length cobs
    {
        uint8_t actual_decoded[512];
        size_t actual_size;

        static const uint8_t zero_encoded[] = {0x00};

        mock.read_set_next(zero_encoded, sizeof(zero_encoded));
        memset(actual_decoded, 0, sizeof(actual_decoded));
        actual_size = 0xFFFFFFFF;
        bool ret = serial.read(actual_decoded, sizeof(actual_decoded), &actual_size);
        if (!ret || (actual_size != 0)) {
            PACKET_STREAM_PRINTF("Zero improperly handled\n");
            return false;
        }
    }

    // Test loopback
    PacketStreamLoopFileHandle loop_mock;
    PacketStream loopback(&loop_mock);

    for (size_t i = 0; i < 1000; i++) {
        PACKET_STREAM_PRINTF("Testing loopback %u\n", i);

        // Create random data
        uint8_t src[1024];
        uint8_t dst[1024];
        uint32_t size = ((uint32_t)rand()) % sizeof(src);
        for (size_t j = 0; j < size; j++) {
            src[j] = rand() & 0xFF;
        }
        memset(dst, 0, sizeof(dst));
        PACKET_STREAM_PRINTF("Size %u\n", size);

        // Loopback
        size_t real_size = 0;
        loopback.write(src, size);
        bool ret = loopback.read(dst, sizeof(dst), &real_size);
        if (!ret) {
            PACKET_STREAM_PRINTF("Read failure\n");
            return false;
        }
        if (real_size != size) {
            PACKET_STREAM_PRINTF("Wrong size, expected %u got %u\n", size, real_size);
            return false;
        }
        if (memcmp(src, dst, size) != 0) {
            PACKET_STREAM_PRINTF("Wrong data\n");
            return false;
        }
    }

    for (size_t i = 0; i < 1000; i++) {
        int src_num = rand();
        int dst_num = 0;
        loopback.printf("Number is %i", src_num);
        if (loopback.scanf("Number is %i", &dst_num) != 1) {
            return false;
        }
        if (dst_num != src_num) {
            return false;
        }
    }

    return true;
}


