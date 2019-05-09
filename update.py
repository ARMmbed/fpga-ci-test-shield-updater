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

from random import randrange
from time import sleep, time
from argparse import ArgumentParser, FileType
from sys import stdout
from serial import Serial, serialutil

# Example usages:
#
# Get FPGA Firmware version
# > python update.py --version COM5
#
# Update firmware
# > python update.py --update fpga_firmware.bin COM5
#
# Read back firmware
# > python update.py --dump dump.bin COM5
#
# Run new firmware
# > python update.py --reload COM5
#

def main():
    parser = ArgumentParser(description='FPGA CI Test Shield Upgrade tool')
    parser.add_argument('--dump', type=FileType("wb"), default=None, help="Dump FPGA image to the given file")
    parser.add_argument('--dump_all', type=FileType("wb"), default=None, help="Dump FPGA flash to the given file")
    parser.add_argument('--update', type=FileType("rb"), default=None, help="Update the FPGA from the given file")
    parser.add_argument('--version', action="store_true", help="Check the FPGA version")
    parser.add_argument('--reload', action="store_true", help="Force the FPGA to reload firmware from flash")
    parser.add_argument('--baud', type=int, default=115200, help="Baudrate to use for the serial port connection")
    parser.add_argument('-v', action="store_true", help="Print verbose information")
    parser.add_argument('connection', type=str, help="Connection to use")
    args = parser.parse_args()

    try:
        stream = SerialPacketStream(args.connection)
        fpga = FpgaCiTestShield(stream)
        fpga.reset()
        fpga.baud(args.baud)

        if args.dump:
            start = time()
            data = fpga.dump(dump_progress)
            end = time()
            if data is None:
                print("\nError during dump")
            else:
                args.dump.write(data)
                args.dump.close()
                print("\nDump completed in %s seconds" % (end - start))

        if args.dump_all:
            start = time()
            data = fpga.dump_all(dump_progress)
            end = time()
            if data is None:
                print("\nError during dump all")
            else:
                args.dump_all.write(data)
                args.dump_all.close()
                print("\nDump all completed in %s seconds" % (end - start))

        if args.update:
            start = time()
            fpga_image = args.update.read()
            args.update.close()
            ret = fpga.update(fpga_image, update_progress)
            end = time()
            if ret:
                print("\nUpdate completed in %s seconds" % (end - start))
            else:
                print("\nError during update")

        if args.reload:
            fpga.reload()

        if args.version:
            print("FPGA version %s" % fpga.get_version())

    except RuntimeError as error:
        if args.v:
            raise
        print("Error: %s" % error)
        print("Depending on the target a lower baudrate may be needed")
    except IOError as _:
        if args.v:
            raise
        print('Error opening serial port "%s"' % args.connection)


class SerialPacketStream:

    def __init__(self, port):
        serial = Serial(port, baudrate=9600)
        serial.timeout = 2
        self._serial = serial

    def reset(self):
        self._serial.send_break()

    def write(self, data):
        payload = cobs_encode(data)
        payload.append(0)
        self._serial.write(payload)

    def read(self):
        data = self._serial.read_until(b"\x00")
        if (len(data) == 0) or (data[-1:] != b"\x00"):
            raise RuntimeError("Timeout when reading from serial port")
        return cobs_decode(data[0:-1])

    def baud(self, baudrate):
        self._serial.baudrate = baudrate


def update_progress(pos, size):
    if size <= 0:
        size = 1
    if pos > size:
        pos = size
    percent = (pos * 100) // size
    total_bars = 20
    bars = pos * total_bars // size
    whites = total_bars - bars

    stdout.write("[" + ("=" * bars) + (" " * whites) + "] %3i%%\r" % percent)
    stdout.flush()


def dump_progress(pos, size):
    stdout.write("Reading from %8i\r" % pos)
    stdout.flush()


class FpgaCiTestShield:

    def __init__(self, stream):
        self._stream = stream
        pass

    def reset(self):
        """
        Reset the mbed board connected to the FPGA

        :return: True if successful, False otherwise
        """
        self._stream.reset()
        sleep(0.1)
        # Send some empty packets to flush any corrupt data
        self._stream.write(b"")
        self._stream.write(b"")
        self._stream.write(b"")
        self.get_version()

    def get_version(self):
        """
        Get the version of software loaded on the FPGA

        :return: Number indicating the FPGA version
        """
        self._stream.write(b"version")
        return int(self._stream.read())

    def dump(self, progress=None):
        """
        Get the firmware loaded on the FPGA as bytes

        :return: FPGA image as bytes or None if an error occurred
        """
        self._stream.write(b"dump")
        data = host_file(self._stream, progress=progress)
        return data if self._stream.read() == b"ok" else None

    def dump_all(self, progress=None):
        """
        Get the serial flash contents as bytes

        :return: Flash image as bytes or None if an error occurred
        """
        self._stream.write(b"dump_all")
        data = host_file(self._stream, progress=progress)
        return data if self._stream.read() == b"ok" else None

    def update(self, data, progress=None):
        """
        Update the FPGA firmware.

        :param data: Bytes object which is the binary file to program
        :return: True if successful, False otherwise
        """
        self._stream.write(b"update")
        host_file(self._stream, data, progress=progress)
        return True if self._stream.read() == b"ok" else False

    def baud(self, baudrate):
        """
        Set the serial baudrate

        :param baudrate: new baudrate to use
        :return: True if new baudrate is set, False otherwise
        """
        self._stream.write(b"baud")
        self._stream.write(b"%i" % baudrate)
        result = self._stream.read() == b"ok"
        sleep(0.1)
        self._stream.baud(baudrate)
        return result

    def reload(self):
        """
        Trigger the FPGA to reload its image

        :return: True if successful, False otherwise
        """
        self._stream.write(b"reload")
        return self._stream.read() == b"ok"


def host_file(connection, file_data=b"", progress=None):
    remote_file = RemoteFile(file_data, progress)
    return remote_file.host(connection.write, connection.read)


class RemoteFile:

    def __init__(self, data, progress):
        self.send = None
        self.recv = None
        self._data = bytearray(data)
        self._finished = False
        self._pos = 0
        self._progress = (lambda a, b: None) if progress is None else progress
        self.commands = {
            b"close": self.close,
            b"read": self.read,
            b"write": self.write,
            b"seek": self.seek
        }

    def host(self, send, recv):
        self._progress(self._pos, len(self._data))
        self.send = send
        self.recv = recv
        while True:
            cmd = bytes(self.recv()).split(b',')
            if cmd[0] not in self.commands:
                raise RuntimeError("Invalid command %s" % cmd)
            try:
                ret = self.commands[cmd[0]](*cmd[1:])
            except TypeError as error:
                print("Error handling command \"%s\"" % cmd[0].decode("utf-8"))
                print("%s" % cmd)
                raise
            if ret is not None:
                send(b"%i" % ret)
            if self._finished:
                return self._data

    def read(self, size):
        size = int(size)

        data = self._data[self._pos:self._pos + size]
        self._pos += len(data)
        self._progress(self._pos, len(self._data))
        self.send(data)
        return None

    def write(self):
        data = self.recv()
        self._data[self._pos:self._pos + len(data)] = data
        self._pos += len(data)
        self._progress(self._pos, len(self._data))
        # print("Writing data \"%s\"" % data)
        return len(data)

    def seek(self, offset, origin):
        offset = int(offset)
        origin = int(origin)
        if origin not in (0, 1, 2):
            return -1
        if origin == 0:
            # absolute
            self._pos = offset
        elif origin == 1:
            # relative
            self._pos += offset
        else:
            # from end
            self._pos = len(self._data) + offset

        if self._pos < 0:
            self._pos = 0
        if self._pos > len(self._data):
            self._pos = len(self._data)
        return self._pos

    def close(self):
        self._progress(len(self._data), len(self._data))
        self._finished = True
        return 0


def cobs_encode(data, add_optional=False):
    data = bytearray(data)
    encoded = bytearray(1)
    last_zero = len(encoded) - 1
    added_size_zero = 0
    for element in data:

        if added_size_zero >= 254:
            # Bitstuff if there have been 254 non-zero bytes
            encoded.append(0)
            added_size_zero += 1

            # Update previous zero and mark the position of this zero
            encoded[last_zero] = added_size_zero
            last_zero = len(encoded) - 1
            added_size_zero = 0

        encoded.append(element)
        added_size_zero += 1

        if element == 0:
            # Update previous zero and mark the position of this zero
            encoded[last_zero] = added_size_zero
            last_zero = len(encoded) - 1
            added_size_zero = 0

    # Optional padding byte if the transfer ends with 254 non-zero bytes
    if add_optional and (added_size_zero >= 254):
        # Bitstuff if there have been 254 non-zero bytes
        encoded.append(0)
        added_size_zero += 1

        # Update previous zero and mark the position of this zero
        encoded[last_zero] = added_size_zero
        last_zero = len(encoded) - 1
        added_size_zero = 0

    # Update the previous zero to point to the element after the last one
    encoded[last_zero] = added_size_zero + 1

    return encoded


def cobs_decode(data, require_optional=False):
    data = bytearray(data)
    decoded = bytearray(0)
    next_zero = 0
    next_pad = True
    for element in data:
        if next_zero == 0:
            next_zero = element
            if not next_pad:
                decoded.append(0)
            next_pad = next_zero == 0xff
        else:
            decoded.append(element)
        next_zero -= 1

    if next_zero != 0:
        raise RuntimeError("COB Decoding Error - Last offset wrong: %s" % list(data))
    if require_optional and next_pad:
        raise RuntimeError("COB Decoding Error - Required optional byte not present")
    return decoded


# https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing
tests = (
    ([0x00], [0x01, 0x01]),
    ([0x00, 0x00], [0x01, 0x01, 0x01]),
    ([0x11, 0x22, 0x00, 0x33], [0x03, 0x11, 0x22, 0x02, 0x33]),
    ([0x11, 0x22, 0x33, 0x44], [0x05, 0x11, 0x22, 0x33, 0x44]),
    ([0x11, 0x00, 0x00, 0x00], [0x02, 0x11, 0x01, 0x01, 0x01]),
    (list(range(0x01, 0xFF)), [0xFF] + list(range(0x01, 0xFF))),
    (list(range(0x00, 0xFF)), [0x01, 0xFF] + list(range(0x01, 0xFF))),
    (list(range(0x01, 0x100)), [0xFF] + list(range(0x01, 0xFF)) + [0x02, 0xFF]),
    (list(range(0x02, 0x100)) + [0x00], [0xFF] + list(range(0x02, 0x100)) + [0x01, 0x01]),
    (list(range(0x03, 0x100)) + [0x00, 0x01], [0xFE] + list(range(0x03, 0x100)) + [0x02, 0x01]),
)
new_tests = []
for first, second in tests:
    new_tests.append((bytearray(first), bytearray(second)))
tests = new_tests


def test_cobs():
    for unencoded, encoded in tests:
        # print("Testing %s -> %s" % (unencoded, encoded))
        assert cobs_encode(unencoded) == encoded, "Expected %s got %s" % (encoded, cobs_encode(unencoded))
    for unencoded, encoded in tests:
        # print("Testing %s -> %s" % (encoded, unencoded))
        assert cobs_decode(encoded) == unencoded, "Expected %s got %s" % (unencoded, cobs_decode(encoded))
    for i in range(1000):
        random_data = bytearray([randrange(0, 256) for _ in range(randrange(0, 1000))])
        encoded = cobs_encode(random_data)
        assert 0 not in encoded
        decoded = cobs_decode(encoded)
        assert random_data == decoded
        # print("Testing arrray of size %s" % len(random_data))


if __name__ == "__main__":
    main()
