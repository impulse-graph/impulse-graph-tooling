#include "impulse_opt/optimizer.hpp"
#include "impulse_graph.h"
#include <iostream>
#include <string>
#include <iomanip>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <input_snapshot.imps> <output_snapshot.imps> [--to-encoding raw_uint32|delta_vbyte|simdcomp|sliced_ellpack|raw_uint16] [--rcm-reorder]" << std::endl;
        std::cout << "   or: " << argv[0] << " input.imps output.imps [--simdcomp|--ellpack|--vbyte|--raw|--optimize]" << std::endl;
        return 1;
    }

    impulse::opt::OptimizerOptions options;
    options.input_path = argv[1];
    options.output_path = argv[2];

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--to-encoding" && i + 1 < argc) {
            std::string enc = argv[++i];
            options.override_target_encoding = true;
            if (enc == "simdcomp") options.target_encoding = IMPULSE_ENC_SIMDCOMP;
            else if (enc == "sliced_ellpack" || enc == "ellpack") options.target_encoding = IMPULSE_ENC_SLICED_ELLPACK;
            else if (enc == "delta_vbyte" || enc == "vbyte") options.target_encoding = IMPULSE_ENC_DELTA_VBYTE;
            else if (enc == "raw_uint32" || enc == "raw") options.target_encoding = IMPULSE_ENC_RAW_UINT32;
            else if (enc == "raw_uint16" || enc == "uint16") options.target_encoding = IMPULSE_ENC_RAW_UINT16;
        } else if (arg == "--simdcomp") {
            options.override_target_encoding = true;
            options.target_encoding = IMPULSE_ENC_SIMDCOMP;
        } else if (arg == "--ellpack" || arg == "--sliced_ellpack") {
            options.override_target_encoding = true;
            options.target_encoding = IMPULSE_ENC_SLICED_ELLPACK;
        } else if (arg == "--vbyte" || arg == "--delta_vbyte") {
            options.override_target_encoding = true;
            options.target_encoding = IMPULSE_ENC_DELTA_VBYTE;
        } else if (arg == "--raw" || arg == "--raw_uint32") {
            options.override_target_encoding = true;
            options.target_encoding = IMPULSE_ENC_RAW_UINT32;
        } else if (arg == "--rcm-reorder" || arg == "--optimize") {
            options.enable_rcm_reorder = true;
        }
    }

    std::cout << "==========================================================================" << std::endl;
    std::cout << " IMPULSE-OPT: C++20 ENCODING TRANSLATOR & HEAVY OPTIMIZER (v2.4.0)" << std::endl;
    std::cout << "==========================================================================" << std::endl;
    std::cout << " Input Snapshot File:  " << options.input_path << std::endl;
    std::cout << " Output Snapshot File: " << options.output_path << std::endl;
    std::cout << " RCM Cache Reorder:   " << (options.enable_rcm_reorder ? "ENABLED" : "DISABLED") << std::endl;
    std::cout << " Target Encoding:      0x" << std::hex << (int)options.target_encoding << std::dec << " (" 
              << (options.target_encoding == IMPULSE_ENC_SIMDCOMP ? "SIMDComp Bitpacked" : 
                 (options.target_encoding == IMPULSE_ENC_SLICED_ELLPACK ? "Sliced ELLPACK GPU" : 
                 (options.target_encoding == IMPULSE_ENC_DELTA_VBYTE ? "Delta-VByte" : "Raw uint32")))
              << ")" << std::endl;

    impulse::opt::ImpulseOptimizer optimizer(options);
    if (!optimizer.run()) {
        std::cerr << "[!] Optimization failed!" << std::endl;
        return 1;
    }

    return 0;
}
