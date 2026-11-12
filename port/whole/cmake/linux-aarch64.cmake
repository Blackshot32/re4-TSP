# 64-bit ARM Linux cross build (milestone 12; Ubuntu g++-aarch64-linux-gnu
# sysroot): the arm64-v8a code generation of the Android build, run under
# qemu-aarch64 in the cloud (whole_build.sh aarch64). LP64 like x86_64; the
# Android build uses the NDK toolchain instead.
#
# The sysroot and the GCC tools (crtbeginS.o, libgcc.a) are looked up where
# the distribution puts them: /usr/aarch64-linux-gnu on Debian, Ubuntu and
# Void (the cross-aarch64-linux-gnu package), and the same path inside the
# Android NDK is not used here. Without --sysroot and --gcc-toolchain the
# compiler takes the host's headers and the link fails; with them the build
# is the same as the one of the container image.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_ASM_COMPILER_TARGET aarch64-linux-gnu)

# The cross sysroot: the headers, the C library and the C++ library of the
# target. RE4_AARCH64_SYSROOT overrides the place.
if(NOT DEFINED RE4_AARCH64_SYSROOT)
    foreach(_candidate
            /usr/aarch64-linux-gnu
            /usr/local/aarch64-linux-gnu
            ${CMAKE_CURRENT_LIST_DIR}/../../../../aarch64-linux-gnu)
        if(EXISTS "${_candidate}/include")
            set(RE4_AARCH64_SYSROOT "${_candidate}")
            break()
        endif()
    endforeach()
endif()
if(RE4_AARCH64_SYSROOT AND EXISTS "${RE4_AARCH64_SYSROOT}")
    set(CMAKE_SYSROOT "${RE4_AARCH64_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH "${RE4_AARCH64_SYSROOT}")
    # The GCC tools of the target (crtbeginS.o, libgcc.a) and its C++ library.
    # The compiler looks for them under <gcc-toolchain>/lib/gcc/<target>/<ver>
    # and <gcc-toolchain>/<target>/include/c++/<ver>, so the toolchain root is
    # the prefix that holds both: /usr on Debian, Ubuntu and Void (the cross
    # packages install into /usr/aarch64-linux-gnu and /usr/lib/gcc).
    set(_gcc_root
        "${RE4_AARCH64_SYSROOT}/.."
        "${RE4_AARCH64_SYSROOT}/usr"
        "/usr")
    foreach(_candidate ${_gcc_root})
        get_filename_component(_candidate "${_candidate}" ABSOLUTE)
        if(EXISTS "${_candidate}/lib/gcc/aarch64-linux-gnu"
           OR EXISTS "${_candidate}/lib/gcc-cross/aarch64-linux-gnu"
           OR IS_DIRECTORY "${_candidate}/aarch64-linux-gnu/include/c++")
            set(RE4_AARCH64_GCC_TOOLCHAIN "${_candidate}")
            break()
        endif()
    endforeach()
    if(RE4_AARCH64_GCC_TOOLCHAIN)
        # The compiler takes the C++ library and the GCC tools from there.
        set(_flags "--gcc-toolchain=${RE4_AARCH64_GCC_TOOLCHAIN}")
        set(CMAKE_C_FLAGS_INIT "${_flags}")
        set(CMAKE_CXX_FLAGS_INIT "${_flags}")
    endif()
endif()
