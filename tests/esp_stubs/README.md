# ESP-IDF API stubs

These headers are intentionally minimal host-build stubs. They allow the ESP32-dependent C sources to be parsed with the host compiler under `-Wall -Wextra -Werror`.

They are not an ESP-IDF replacement, are not linked into the target firmware, and do not prove hardware or ABI compatibility. The real target build must use ESP-IDF headers and toolchain.
