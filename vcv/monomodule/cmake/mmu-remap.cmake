# SPDX-License-Identifier: AGPL-3.0-only
# dsp56300's MmuArray gives a JIT cache block private memory by unmapping it and mapping it straight back with
# MAP_FIXED (MmuHelper::unmapRegion, then mapRegion). On Linux and macOS the munmap leaves a hole for a moment.
# Another thread's mmap can land in it: asmjit code pages, malloc, or the other emulator when Machinemodule and
# Monomodule load together. The MAP_FIXED then silently replaces that thread's pages with the cache block, and
# Rack crashed in the JIT (tests/mmu_test.cpp shows the race). Windows already keeps the range reserved through
# placeholders. This private copy does the same: unmapRegion leaves a PROT_NONE reservation in place, and
# freeAddressRange releases the whole reserved range at the end.
# The pinned checkout stays untouched; dsp56kBase compiles the copy.
function(mmu_atomic_remap source)
    file(READ "${source}" text)
    set(unmap "\t\tconst auto res = munmap(_addr, it->second) == 0;\n")
    string(CONCAT reserve "\t\t// DSP_Emulation: replace the pages with a reservation in one call. munmap would leave a hole that\n"
        "\t\t// another thread's mmap can take before mapRegion's MAP_FIXED lands over it (cmake/mmu-remap.cmake).\n"
        "\t\tconst auto res = mmap(_addr, it->second, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, InvalidHandle, 0) == _addr;\n")
    set(clear "\t\t// the individual mapRegion calls. Just clear the pointer.\n")
    string(CONCAT release "\t\t// the individual mapRegion calls. Just clear the pointer.\n"
        "\t\t// DSP_Emulation: unmapRegion now leaves the range reserved rather than unmapped, so release it all here.\n"
        "\t\tmunmap(m_basePtr, m_basePtrSize);\n")
    foreach(pair "unmap;reserve" "clear;release")
        list(GET pair 0 from)
        list(GET pair 1 to)
        string(FIND "${text}" "${${from}}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "mmu-remap: ${source} no longer has the code this patch replaces; recheck the patch")
        endif()
        string(REPLACE "${${from}}" "${${to}}" text "${text}")
    endforeach()
    set(copy "${CMAKE_CURRENT_BINARY_DIR}/mmu-remap/mmuhelper.cpp")
    file(WRITE "${copy}.new" "${text}")
    file(COPY_FILE "${copy}.new" "${copy}" ONLY_IF_DIFFERENT)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    get_filename_component(dir "${source}" DIRECTORY)
    get_target_property(sources dsp56kBase SOURCES)
    list(REMOVE_ITEM sources mmuhelper.cpp "${source}")
    list(APPEND sources "${copy}")
    set_property(TARGET dsp56kBase PROPERTY SOURCES "${sources}")
    target_include_directories(dsp56kBase PRIVATE "${dir}")
endfunction()
