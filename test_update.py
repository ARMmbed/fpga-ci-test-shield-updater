#
# Copyright (c) 2019, Arm Limited and affiliates.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import random
from binascii import crc32
from struct import pack
from update import SerialPacketStream, FpgaCiTestShield, dump_progress, update_progress
from argparse import ArgumentParser, FileType


def main():
    parser = ArgumentParser(description='FPGA CI Test Shield Test tool')
    parser.add_argument('--baud', type=int, default=115200, help="Baudrate to use for the serial port connection")
    parser.add_argument('--firmware', type=FileType("rb"), default=None, help="Firmware to load after the test")
    parser.add_argument('connection', type=str, help="Connection to use")
    args = parser.parse_args()

    stream = SerialPacketStream(args.connection)
    fpga = FpgaCiTestShield(stream)

    fpga.reset()
    fpga.baud(args.baud)

    print("Running update self test")

    if args.firmware is None:
        print("Saving off original firmware")
        firmware = fpga.dump(dump_progress)
        print("\nSave complete")
    else:
        firmware = args.firmware.read()
        args.firmware.close()

    failure_count = 0
    for name, data, result in test_image_iterator():
        print("----------------------")
        print('Running test "%s"' % name)
        if result:
            print("  Expecting update to succeed")
        else:
            print("  Expecting update to detect an error")

        test_pass = True
        print("Starting update")
        ret = fpga.update(data, update_progress)
        print("\nUpdate complete")
        if ret:
            print("  No problems detected during update")
        else:
            print("  Error detected during update")
        if ret != result:
            test_pass = False

        if ret and test_pass:
            print("Starting dump")
            final_data = fpga.dump(dump_progress)
            print("\nDump complete")
            if data == final_data:
                print("  Data matches")
            else:
                print("  **Data does not match**")
                test_pass = False

        if test_pass:
            print("Test passed")
        else:
            print("Test failed")
            failure_count += 1
            break
        print("")

    print("Restoring firmware")
    fpga.update(firmware, dump_progress)
    print("\nRestore")

    exit(0 if failure_count == 0 else 1)


def test_image_iterator():

    # Test small image sizes
    for size in range(16):
        yield ("Small size %s" % size, crc_data(random_bytes(size)), True)

    # Test 256 byte boundary
    for size in range(240, 270):
        yield ("Byte boundary %s" % size, crc_data(random_bytes(size)), True)

    yield ("Bad CRC", crc_data_bad(random_bytes(1024)), False)

    yield ("Bad Size +1", crc_data_bad(random_bytes(1024), crc_delta=0, size_delta=1), False)

    yield ("Bad Size -1", crc_data_bad(random_bytes(1024), crc_delta=0, size_delta=-1), False)

    yield ("Good Size +0", crc_data_bad(random_bytes(1024), crc_delta=0, size_delta=0), True)

    yield ("Image too big", crc_data(random_bytes(0x220000 - 7)), False)

    yield ("Max size image", crc_data(random_bytes(0x220000 - 8)), True)


def random_bytes(size):
    return bytes(bytearray(random.randint(0, 255) for _ in range(size)))


def crc_data(data):
    size = len(data)
    crc = crc32(data) & 0xFFFFFFFF
    raw_size = pack("<I", size)
    raw_crc = pack("<I", crc)
    return raw_size + data + raw_crc


def crc_data_bad(data, crc_delta=1, size_delta=0):
    size = len(data) + size_delta
    crc = (crc32(data) + crc_delta) & 0xFFFFFFFF
    raw_size = pack("<I", size)
    raw_crc = pack("<I", crc)
    return raw_size + data + raw_crc


if __name__ == "__main__":
    main()
