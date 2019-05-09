# FPGA CI Test Shield Updater

This program is for updating the FPGA CI Test Shield. Once it is built and loaded onto an Mbed board the script `update.py` can be used to apply firmware updates directly from a host PC.

## Required hardware

The hardware needed for this repository is as follows:
 - A Mbed board with a supported form factor, such as the Arduino form factor
 - FPGA CI Test Shield

## Updating firmware

 - Clone the repository
 ```
 > mbed import fpga-ci-test-shield-updater
 > cd fpga-ci-test-shield-updater
 ```
 - Compile and flash this repository
 ```
 > mbed compile -t ARM -m NUCLEO_F401RE -f
 ```
 - Download the latest FPGA firmware from the [FPGA CI Test Shield repository](https://github.com/ARMmbed/fpga-ci-test-shield)
 - Program flash using the update script
 ```
 > python update.py --update latest_release.bin --reload COM5
 ```
 - (Optional) Confirm that the new version is loaded by running the version command
 ```
 > python update.py --version COM5
 ```

## Commands

The supported commands can be found by running the help command of `update.py`:

`> python update.py -h`

Which gives the following output:

```
FPGA CI Test Shield Upgrade tool

positional arguments:
  connection           Connection to use

optional arguments:
  -h, --help           show this help message and exit
  --dump DUMP          Dump FPGA image to the given file
  --dump_all DUMP_ALL  Dump FPGA flash to the given file
  --update UPDATE      Update the FPGA from the given file
  --version            Check the FPGA version
  --reload             Force the FPGA to reload firmware from flash
  --baud BAUD          Baudrate to use for the serial port connection
  -v                   Print verbose information
  ```

## License and contributions

The software is provided under the [Apache-2.0 license](https://github.com/ARMmbed/mbed-os/blob/master/LICENSE-apache-2.0.txt). Contributions to this project are accepted under the same license. Please see [contributing.md](https://github.com/ARMmbed/mbed-os/blob/master/CONTRIBUTING.md) for more information.

This project contains code from other projects. The original license text is included in those source files. They must comply with our [license guide](https://os.mbed.com/docs/mbed-os/latest/contributing/license.html).

Folders containing files under different permissive license than Apache 2.0 are listed in the [LICENSE](https://github.com/ARMmbed/mbed-os/blob/master/LICENSE.md) file.
