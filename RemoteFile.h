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

#ifndef REMOTE_FILE_H
#define REMOTE_FILE_H

#include <cstdio>

#include "PacketStream.h"

class RemoteFile : public mbed::FileHandle {
public:

    RemoteFile(PacketStream *stream) {
        _stream = stream;
    }

    virtual ssize_t read(void *buffer, size_t size) {
        size_t actual;
        _stream->printf("read,%i", (int)size);
        _stream->read((uint8_t *)buffer, size, &actual);
        return actual;
    }

    virtual ssize_t write(const void *buffer, size_t size) {
        int ret = -1;
        _stream->printf("write");
        _stream->write((const uint8_t *)buffer, size);
        _stream->scanf("%i", &ret);
        return ret;
    }

    virtual off_t seek(off_t offset, int whence = SEEK_SET) {
        int ret = -1;
        _stream->printf("seek,%i,%i", offset, whence);
        _stream->scanf("%i", &ret);
        return ret;
    }

    virtual int close() {
        int ret = -1;
        _stream->printf("close");
        _stream->scanf("%i", &ret);
        return ret;
    }

private:

    PacketStream *_stream;
};


#endif
