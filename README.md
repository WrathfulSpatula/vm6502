# VM6502-Q Emulator

[![VM6502-Q Build Status](https://api.travis-ci.org/vm6502q/vm6502q.svg?branch=master)](https://travis-ci.org/vm6502q/vm6502q/builds)

This is a working 6502 emulator with support for quantum operations provided by [qrack](https://github.org/vm6502q/qrack).

See [ReadMe.txt](https://github.com/vm6502q/vm6502q/blob/master/ReadMe.txt) for the original readme documentation.

# Building

Clone [qrack](https://github.org/vm6502q/qrack). Follow the instructions on that repository for installing qrack, including OpenCL support.

(From qrack checkout directory:)

`mkdir _build && cd _build && cmake .. && make install`

(The `make install` operation might require `sudo` privileges.)

Then, `make all` in the vm6502q checkout directory. This will use the installed qrack library and headers.

# Usage

Run `./vm65 -r file.dat` or use `make run` from the [examples](https://github.com/vm6502q/examples) repository.
