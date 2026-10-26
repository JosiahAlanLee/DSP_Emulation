# The pinned desktop core uses MSVC on Windows. Rack uses MinGW instead.
# Keep the checkout intact: adapt a private copy of just the emulator sources.
set(MD_DSP56300_DIR "${CMAKE_CURRENT_BINARY_DIR}/mingw-dsp56300" CACHE PATH "MinGW-compatible pinned DSP sources" FORCE)
file(MAKE_DIRECTORY "${MD_DSP56300_DIR}/source")
foreach(part asmjit dsp56kBase dsp56kEmu vtuneSdk)
    file(COPY "${CORE_DIR}/libs/dsp56300/source/${part}" DESTINATION "${MD_DSP56300_DIR}/source" PATTERN ".git" EXCLUDE)
endforeach()

# Windows headers are case insensitive on Windows, but cross compilers are not.
file(GLOB_RECURSE sources "${MD_DSP56300_DIR}/source/dsp56kBase/*.cpp" "${MD_DSP56300_DIR}/source/dsp56kEmu/*.cpp")
foreach(source IN LISTS sources)
    file(READ "${source}" text)
    string(REPLACE "<Windows.h>" "<windows.h>" fixed "${text}")
    if(NOT fixed STREQUAL text)
        file(WRITE "${source}" "${fixed}")
    endif()
endforeach()

# The old debugger naming exception requires MSVC SEH. MinGW still uses the
# existing dynamically loaded SetThreadDescription implementation immediately
# below it; never raise an exception that GCC cannot catch.
set(source "${MD_DSP56300_DIR}/source/dsp56kBase/threadtools.cpp")
file(READ "${source}" text)
string(REPLACE "\t\t__try  //" "#ifdef _MSC_VER\n\t\t__try  //" text "${text}")
string(REPLACE "\t}\n\n\t// SetThreadDescription" "#endif\n\t}\n\n\t// SetThreadDescription" text "${text}")
file(WRITE "${source}" "${text}")

# The upstream GNU constexpr limit is Linux-only, although the opcode table is
# identical under MinGW GCC.
add_compile_options(-fconstexpr-loop-limit=10000000)
