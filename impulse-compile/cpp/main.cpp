#include "compiler.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " --manifest-dir <directory_path> --output <output.imps>" << std::endl;
        std::cout << "   or: " << argv[0] << " <directory_path> <output.imps>" << std::endl;
        return 1;
    }

    std::string dir_path;
    std::string output_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--manifest-dir" && i + 1 < argc) {
            dir_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (dir_path.empty()) {
            dir_path = arg;
        } else if (output_path.empty()) {
            output_path = arg;
        }
    }

    if (dir_path.empty() || output_path.empty()) {
        std::cerr << "[!] Error: Missing manifest directory or output file path." << std::endl;
        return 1;
    }

    try {
        if (!impulse::SnapshotCompiler::compile_directory(dir_path, output_path)) {
            return 1;
        }
    } catch (const std::exception& ex) {
        std::cerr << "[!] Exception during compilation: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
