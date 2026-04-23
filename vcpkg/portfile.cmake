# lock-free-ring-buffer is a header-only library — no compilation needed.
set(VCPKG_BUILD_TYPE release)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AviadCohen24/LockFreeRingBuffer
    REF "v${VERSION}"
    SHA512 0  # TODO: replace with real SHA512 after creating the v1.0.0 GitHub release
              # Run: vcpkg install lock-free-ring-buffer --overlay-ports=./vcpkg
              # and copy the hash from the error message.
    HEAD_REF main
)

# Install the single header
file(INSTALL "${SOURCE_PATH}/ring_buffer.h"
     DESTINATION "${CURRENT_PACKAGES_DIR}/include")

# Install license
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
