# cmake/PackageDeb.cmake - CPack 变量定义
# 不在此处 install()，仅设置变量；install 规则由顶层 CMakeLists 在末尾处理

if(NOT DEFINED UDAF_VERSION)
    set(UDAF_VERSION "0.1.0")
endif()
if(NOT DEFINED UDAF_DEB_ARCH)
    set(UDAF_DEB_ARCH "amd64")
endif()

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "udaf")
set(CPACK_PACKAGE_VERSION "${UDAF_VERSION}")
set(CPACK_PACKAGE_CONTACT "udaf@example.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "UDAF - Unified Device & Application Framework")
set(CPACK_PACKAGE_DESCRIPTION "UDAF 提供多协议设备发现、分布式数据流框架、设备↔PC 通信三大能力")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "UDAF Team <udaf@example.com>")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${UDAF_DEB_ARCH}")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.31), libgcc-s1 (>= 3.0), libstdc++6 (>= 11), libssl3 (>= 3.0), libspdlog1.12, libyaml-cpp0.7, libzmq5")
set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")