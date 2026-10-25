// Standalone backend. Reads note, velocity, params, sources as classic-locale numbers.
#include "../src/engine/CompileVastish.hpp"
#include <iomanip>
#include <iostream>
#include <locale>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char** argv) {
    std::cin.imbue(std::locale::classic()); std::cout.imbue(std::locale::classic());
    try {
        if (argc == 2 && std::string(argv[1]) == "--describe") {
            std::cout << "{\"parameter_count\":" << vastish::PARAM_COUNT << ",\"source_count\":" << vastish::SOURCE_COUNT << ",\"defaults\":[";
            for (int i = 0; i < vastish::PARAM_COUNT; ++i) std::cout << (i ? "," : "") << std::setprecision(9) << vastish::programs()[0][i];
            std::cout << "],\"names\":[";
            for (int i = 0; i < vastish::PARAM_COUNT; ++i) std::cout << (i ? "," : "") << std::quoted(vastish::metadata()[i].name);
            std::cout << "]}\n"; return 0;
        }
        if (argc != 1) throw std::runtime_error("Usage: compile_vastish_backend [--describe]");
        dspemu::lookup::FrozenControls frozen; vastish::Parameters params{};
        if (!(std::cin >> frozen.note >> frozen.velocity)) throw std::runtime_error("Expected frozen note and velocity");
        for (float& p : params) if (!(std::cin >> p)) throw std::runtime_error("Expected a complete parameter snapshot");
        for (float& s : frozen.sources) if (!(std::cin >> s)) throw std::runtime_error("Expected a complete source snapshot");
        std::string extra; if (std::cin >> extra) throw std::runtime_error("Unexpected trailing input");
        auto table = dspemu::lookup::compileVastish(params, frozen);
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::cout.write(reinterpret_cast<const char*>(table.data()), table.size());
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
