#ifndef IMPULSE_OPT_CODECS_HPP
#define IMPULSE_OPT_CODECS_HPP

#include <vector>
#include <cstdint>
#include <cstddef>

namespace impulse::opt {

uint64_t read_vbyte(const uint8_t* buf, size_t& offset, size_t max_len);
void write_vbyte(std::vector<uint8_t>& buf, uint64_t val);

void encode_simdcomp(std::vector<uint8_t>& buf, const std::vector<uint32_t>& row_offsets, const std::vector<uint32_t>& col_indices, uint64_t node_count);
void decode_simdcomp(const uint8_t* buf, size_t buf_len, uint64_t edge_count, std::vector<uint32_t>& out_deltas);

void encode_sliced_ellpack(std::vector<uint8_t>& buf, const std::vector<uint32_t>& row_offsets, const std::vector<uint32_t>& col_indices, uint64_t node_count);
void decode_sliced_ellpack(const uint8_t* buf, size_t buf_len, const std::vector<uint32_t>& row_offsets, uint64_t node_count, std::vector<uint32_t>& out_col_indices);

} // namespace impulse::opt

#endif // IMPULSE_OPT_CODECS_HPP
