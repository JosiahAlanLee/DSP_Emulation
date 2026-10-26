// SPDX-License-Identifier: AGPL-3.0-only
#include <rack.hpp>
#include <cassert>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#else
#include <dlfcn.h>
#endif

int main(int argc, char** argv) {
    assert(argc == 2);
#ifdef _WIN32
    auto library = LoadLibraryW(std::filesystem::path(argv[1]).wstring().c_str());
    if (!library) std::cerr << "LoadLibrary failed: " << GetLastError() << '\n';
    assert(library);
    auto init = reinterpret_cast<void (*)(rack::Plugin*)>(GetProcAddress(library, "init"));
#else
    auto library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) std::cerr << dlerror() << '\n';
    assert(library);
    auto init = reinterpret_cast<void (*)(rack::Plugin*)>(dlsym(library, "init"));
#endif
    assert(init);
    {
        rack::Plugin plugin;
        init(&plugin);
        assert(plugin.models.size() == 1);
        assert(plugin.models.front()->slug == "Machinemodule");
    }
#ifdef _WIN32
    assert(FreeLibrary(library));
#else
    assert(dlclose(library) == 0);
#endif
    std::cout << "Plugin loads and registers Machinemodule against the real Rack API\n";
}
