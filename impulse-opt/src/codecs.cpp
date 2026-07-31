#include "impulse_opt/codecs.hpp"
#include <algorithm>
#include <cstring>

namespace impulse::opt {

uint64_t read_vbyte(const uint8_t* buf, size_t& offset, size_t max_len) {
    uint64_t val = 0;
    int shift = 0;
    while (offset < max_len) {
        uint8_t b = buf[offset++];
        val |= (static_cast<uint64_t>(b & 0x7F) << shift);
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    return val;
}

void write_vbyte(std::vector<uint8_t>& buf, uint64_t val) {
    while (val >= 0x80) {
        buf.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
        val >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(val & 0x7F));
}

static void write_bitpacked_block(std::vector<uint8_t>& buf, const uint32_t* values, size_t count, uint8_t bit_width) {
    buf.push_back(bit_width);
    uint64_t bit_buffer = 0;
    int bits_in_buffer = 0;

    for (size_t i = 0; i < count; ++i) {
        bit_buffer |= (static_cast<uint64_t>(values[i]) << bits_in_buffer);
        bits_in_buffer += bit_width;

        while (bits_in_buffer >= 8) {
            buf.push_back(static_cast<uint8_t>(bit_buffer & 0xFF));
            bit_buffer >>= 8;
            bits_in_buffer -= 8;
        }
    }
    if (bits_in_buffer > 0) {
        buf.push_back(static_cast<uint8_t>(bit_buffer & 0xFF));
    }
}

void encode_simdcomp(std::vector<uint8_t>& buf, const std::vector<uint32_t>& row_offsets, const std::vector<uint32_t>& col_indices, uint64_t node_count) {
    std::vector<uint32_t> deltas;
    deltas.reserve(col_indices.size());

    for (size_t node = 0; node <= node_count; ++node) {
        uint32_t start = row_offsets[node];
        uint32_t end = row_offsets[node + 1];
        uint32_t prev_tgt = 0;
        for (uint32_t idx = start; idx < end; ++idx) {
            uint32_t tgt = col_indices[idx];
            uint32_t delta = (idx == start) ? tgt : (tgt - prev_tgt);
            deltas.push_back(delta);
            prev_tgt = tgt;
        }
    }

    size_t pos = 0;
    while (pos < deltas.size()) {
        size_t block_size = std::min<size_t>(128, deltas.size() - pos);
        uint32_t max_val = 0;
        for (size_t i = 0; i < block_size; ++i) {
            if (deltas[pos + i] > max_val) max_val = deltas[pos + i];
        }

        uint8_t bit_width = 0;
        while ((1ULL << bit_width) <= max_val && bit_width < 32) {
            bit_width++;
        }

        write_bitpacked_block(buf, &deltas[pos], block_size, bit_width);
        pos += block_size;
    }
}

void decode_simdcomp(const uint8_t* buf, size_t buf_len, uint64_t edge_count, std::vector<uint32_t>& out_deltas) {
    out_deltas.clear();
    out_deltas.reserve(edge_count);

    size_t pos = 0;
    while (pos < buf_len && out_deltas.size() < edge_count) {
        uint8_t bit_width = buf[pos++];
        size_t block_size = std::min<size_t>(128, edge_count - out_deltas.size());

        uint64_t bit_buffer = 0;
        int bits_in_buffer = 0;

        for (size_t i = 0; i < block_size; ++i) {
            while (bits_in_buffer < bit_width && pos < buf_len) {
                bit_buffer |= (static_cast<uint64_t>(buf[pos++]) << bits_in_buffer);
                bits_in_buffer += 8;
            }
            uint32_t val = bit_buffer & ((1ULL << bit_width) - 1);
            bit_buffer >>= bit_width;
            bits_in_buffer -= bit_width;
            out_deltas.push_back(val);
        }
    }
}

void encode_sliced_ellpack(std::vector<uint8_t>& buf, const std::vector<uint32_t>& row_offsets, const std::vector<uint32_t>& col_indices, uint64_t node_count) {
    const size_t SLICE_SIZE = 32;
    size_t num_slices = (node_count + 1 + SLICE_SIZE - 1) / SLICE_SIZE;

    for (size_t slice = 0; slice < num_slices; ++slice) {
        size_t start_row = slice * SLICE_SIZE;
        size_t end_row = std::min<size_t>(start_row + SLICE_SIZE, node_count + 1);

        uint32_t max_deg = 0;
        for (size_t r = start_row; r < end_row; ++r) {
            uint32_t deg = row_offsets[r + 1] - row_offsets[r];
            if (deg > max_deg) max_deg = deg;
        }

        const uint8_t* deg_ptr = reinterpret_cast<const uint8_t*>(&max_deg);
        buf.insert(buf.end(), deg_ptr, deg_ptr + 4);

        for (uint32_t col = 0; col < max_deg; ++col) {
            for (size_t r = start_row; r < start_row + SLICE_SIZE; ++r) {
                uint32_t val = 0xFFFFFFFF;
                if (r < end_row) {
                    uint32_t r_start = row_offsets[r];
                    uint32_t r_deg = row_offsets[r + 1] - r_start;
                    if (col < r_deg) {
                        val = col_indices[r_start + col];
                    }
                }
                const uint8_t* val_ptr = reinterpret_cast<const uint8_t*>(&val);
                buf.insert(buf.end(), val_ptr, val_ptr + 4);
            }
        }
    }
}

void decode_sliced_ellpack(const uint8_t* buf, size_t buf_len, const std::vector<uint32_t>& row_offsets, uint64_t node_count, std::vector<uint32_t>& out_col_indices) {
    const size_t SLICE_SIZE = 32;
    size_t num_slices = (node_count + 1 + SLICE_SIZE - 1) / SLICE_SIZE;
    out_col_indices.clear();

    size_t offset = 0;
    for (size_t slice = 0; slice < num_slices; ++slice) {
        if (offset + 4 > buf_len) break;
        uint32_t max_deg = *reinterpret_cast<const uint32_t*>(buf + offset);
        offset += 4;

        size_t start_row = slice * SLICE_SIZE;
        size_t end_row = std::min<size_t>(start_row + SLICE_SIZE, node_count + 1);

        std::vector<std::vector<uint32_t>> row_neighbors(end_row - start_row);

        for (uint32_t col = 0; col < max_deg; ++col) {
            for (size_t r_idx = 0; r_idx < SLICE_SIZE; ++r_idx) {
                if (offset + 4 > buf_len) break;
                uint32_t val = *reinterpret_cast<const uint32_t*>(buf + offset);
                offset += 4;
                size_t row_i = start_row + r_idx;
                if (row_i < end_row && val != 0xFFFFFFFF) {
                    row_neighbors[r_idx].push_back(val);
                }
            }
        }

        for (size_t r_idx = 0; r_idx < row_neighbors.size(); ++r_idx) {
            out_col_indices.insert(out_col_indices.end(), row_neighbors[r_idx].begin(), row_neighbors[r_idx].end());
        }
    }
}

} // namespace impulse::opt
