set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_HEADER_ONLY enabled)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AviadCohen24/LockFreeRingBuffer
    REF "v${VERSION}"
    SHA512 297bd4b08fc6e6d0a16915e7ee7f6e818fc1c9b0b00b9403bc6a5f13da5644755e498649ad3698ef440a5d44264143c12cdbc15daf23b5dfc482686cdee8690e
    HEAD_REF main
)

file(INSTALL "${SOURCE_PATH}/ring_buffer.h"
     DESTINATION "${CURRENT_PACKAGES_DIR}/include/${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
