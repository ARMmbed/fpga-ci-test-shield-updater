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

/*
 * This is a program for updating the FPGA CI Test Shield
 *
 * This is intended for use in CI systems to perform remote updates of
 * the FPGA CI Test Shield firmware. The associated python script `update.py` is
 * used to send firmware to the mbed board so no SD card or physical interactions
 * are needed. See `update.py` for more information on how to perform the update.
 *
 */
#include "mbed.h"
#include "PacketStream.h"
#include "RemoteFile.h"
#include <string.h>

#include "MbedTester.h"
#include "pinmap.h"

UARTSerial serial(USBTX, USBRX, 9600);
PacketStream pc(&serial);

typedef struct {
    const char *name;
    void (*handler)();
} command_t;

extern size_t command_count;
extern command_t commands[];

uint8_t buf[64];

uint32_t cobs_errors;
uint32_t unknown_commands;

const PinList *form_factor = pinmap_ff_default_pins();
const PinList *restricted = pinmap_restricted_pins();
MbedTester tester(pinmap_ff_default_pins(), pinmap_restricted_pins());

int main()
{
    while (true) {
        memset(buf, 0, sizeof(buf));
        uint32_t actual;
        bool good_read = pc.read(buf, sizeof(buf) - 1, &actual);
        if (!good_read) {
            cobs_errors++;
            continue;
        }
        if (actual == 0) {
            continue;
        }

        command_t *command = NULL;
        for (size_t i = 0; i < command_count; i++) {
            const char *name = commands[i].name;
            if (actual != strlen(name)) {
                continue;
            }
            if (memcmp((char*)buf, name, actual) != 0) {
                continue;
            }
            command = commands + i;
        }

        if (command) {
            command->handler();
        } else {
            unknown_commands++;
        }

    }

    return 0;
}

void version_handler()
{
    pc.printf("%i", tester.version());
}

void dump_all_handler()
{
    RemoteFile file(&pc);

    bool success = tester.firmware_dump_all(&file);

    file.close();
    pc.printf(success ? "ok" : "error");
}

void dump_handler()
{
    RemoteFile file(&pc);

    bool success = tester.firmware_dump(&file);

    file.close();
    pc.printf(success ? "ok" : "error");
}

void update_handler()
{
    RemoteFile file(&pc);

    bool success = tester.firmware_update(&file);

    file.close();
    pc.printf(success ? "ok" : "error");
}

void reload_handler()
{
    tester.reprogram();
    pc.printf("ok");
}

void baud_handler()
{
    int baud = 0;
    if (pc.scanf("%i", &baud) == 1) {
        pc.printf("ok");
        wait_ms(50);
        serial.set_baud(baud);
    } else {
        pc.printf("error");
    }
}

void stats_handler()
{
    pc.printf("encoding_errors: %i, unknown_commands %i", cobs_errors, unknown_commands);
}

command_t commands[] = {
    {"version", version_handler},
    {"dump_all", dump_all_handler},
    {"dump", dump_handler},
    {"update", update_handler},
    {"reload", reload_handler},
    {"baud", baud_handler},
    {"stats", stats_handler}
};
size_t command_count = sizeof(commands) / sizeof(commands[0]);
