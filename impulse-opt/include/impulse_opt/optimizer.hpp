#ifndef IMPULSE_OPT_OPTIMIZER_HPP
#define IMPULSE_OPT_OPTIMIZER_HPP

#include "impulse_graph.h"
#include <string>
#include <vector>
#include <cstdint>

namespace impulse::opt {

struct DomainData {
    uint16_t domain_id;
    uint8_t key_type;
    std::string name;
    std::vector<uint8_t> raw_payload;
};

struct RelationData {
    uint16_t src_domain_id;
    uint16_t tgt_domain_id;
    uint8_t encoding_type;
    uint64_t node_count;
    uint64_t edge_count;
    uint64_t section_features;
    std::vector<uint32_t> row_offsets;
    std::vector<uint32_t> column_indices;
};

struct OptimizerOptions {
    std::string input_path;
    std::string output_path;
    uint8_t target_encoding = IMPULSE_ENC_SIMDCOMP;
    bool override_target_encoding = false;
    bool enable_rcm_reorder = false;
};

class ImpulseOptimizer {
public:
    explicit ImpulseOptimizer(OptimizerOptions options);
    bool run();

private:
    OptimizerOptions options_;
};

} // namespace impulse::opt

#endif // IMPULSE_OPT_OPTIMIZER_HPP
