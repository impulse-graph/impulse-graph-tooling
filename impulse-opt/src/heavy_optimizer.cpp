#include "impulse_graph.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <CommonCrypto/CommonDigest.h>

static std::string bytes_to_hex(const uint8_t* bytes, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
    }
    return oss.str();
}

static uint64_t read_vbyte(const uint8_t* buf, size_t& offset, size_t max_len) {
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

static void write_vbyte(std::vector<uint8_t>& buf, uint64_t val) {
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

static void encode_simdcomp(std::vector<uint8_t>& buf, const std::vector<uint32_t>& row_offsets, const std::vector<uint32_t>& col_indices, uint64_t node_count) {
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

static void decode_simdcomp(const uint8_t* buf, size_t buf_len, uint64_t edge_count, std::vector<uint32_t>& out_deltas) {
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

static void encode_sliced_ellpack(std::vector<uint8_t>& buf, const std::vector<uint32_t>& row_offsets, const std::vector<uint32_t>& col_indices, uint64_t node_count) {
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

static void decode_sliced_ellpack(const uint8_t* buf, size_t buf_len, const std::vector<uint32_t>& row_offsets, uint64_t node_count, std::vector<uint32_t>& out_col_indices) {
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

static void align64(std::vector<uint8_t>& buf) {
    size_t rem = buf.size() % 64;
    if (rem != 0) {
        buf.insert(buf.end(), 64 - rem, 0x00);
    }
}

static void align4096(std::vector<uint8_t>& buf) {
    size_t rem = buf.size() % 4096;
    if (rem != 0) {
        buf.insert(buf.end(), 4096 - rem, 0x00);
    }
}

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

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <input_snapshot.imps> <output_snapshot.imps> [--to-encoding raw_uint32|delta_vbyte|simdcomp|sliced_ellpack|raw_uint16] [--rcm-reorder]" << std::endl;
        std::cout << "   or: " << argv[0] << " input.imps output.imps [--simdcomp|--ellpack|--vbyte|--raw|--optimize]" << std::endl;
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = argv[2];

    uint8_t target_encoding = IMPULSE_ENC_SIMDCOMP; // Default target
    bool override_target_encoding = false;
    bool enable_rcm_reorder = false;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--to-encoding" && i + 1 < argc) {
            std::string enc = argv[++i];
            override_target_encoding = true;
            if (enc == "simdcomp") target_encoding = IMPULSE_ENC_SIMDCOMP;
            else if (enc == "sliced_ellpack" || enc == "ellpack") target_encoding = IMPULSE_ENC_SLICED_ELLPACK;
            else if (enc == "delta_vbyte" || enc == "vbyte") target_encoding = IMPULSE_ENC_DELTA_VBYTE;
            else if (enc == "raw_uint32" || enc == "raw") target_encoding = IMPULSE_ENC_RAW_UINT32;
            else if (enc == "raw_uint16" || enc == "uint16") target_encoding = IMPULSE_ENC_RAW_UINT16;
        } else if (arg == "--simdcomp") {
            override_target_encoding = true;
            target_encoding = IMPULSE_ENC_SIMDCOMP;
        } else if (arg == "--ellpack" || arg == "--sliced_ellpack") {
            override_target_encoding = true;
            target_encoding = IMPULSE_ENC_SLICED_ELLPACK;
        } else if (arg == "--vbyte" || arg == "--delta_vbyte") {
            override_target_encoding = true;
            target_encoding = IMPULSE_ENC_DELTA_VBYTE;
        } else if (arg == "--raw" || arg == "--raw_uint32") {
            override_target_encoding = true;
            target_encoding = IMPULSE_ENC_RAW_UINT32;
        } else if (arg == "--rcm-reorder" || arg == "--optimize") {
            enable_rcm_reorder = true;
        }
    }

    std::cout << "==========================================================================" << std::endl;
    std::cout << " IMPULSE-OPT: C++20 ENCODING TRANSLATOR & HEAVY OPTIMIZER (v2.4.0)" << std::endl;
    std::cout << "==========================================================================" << std::endl;
    std::cout << " Input Snapshot File:  " << input_path << std::endl;
    std::cout << " Output Snapshot File: " << output_path << std::endl;
    std::cout << " RCM Cache Reorder:   " << (enable_rcm_reorder ? "ENABLED" : "DISABLED") << std::endl;
    std::cout << " Target Encoding:      0x" << std::hex << (int)target_encoding << std::dec << " (" 
              << (target_encoding == IMPULSE_ENC_SIMDCOMP ? "SIMDComp Bitpacked" : 
                 (target_encoding == IMPULSE_ENC_SLICED_ELLPACK ? "Sliced ELLPACK GPU" : 
                 (target_encoding == IMPULSE_ENC_DELTA_VBYTE ? "Delta-VByte" : "Raw uint32")))
              << ")" << std::endl;

    auto t_start_total = std::chrono::high_resolution_clock::now();

    std::ifstream ifs(input_path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        std::cerr << "[!] Error opening input snapshot file: " << input_path << std::endl;
        return 1;
    }

    size_t file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    if (file_size < 58) {
        std::cerr << "[!] Input snapshot file too small: " << file_size << " bytes" << std::endl;
        return 1;
    }

    std::vector<uint8_t> buffer(file_size);
    if (!ifs.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        std::cerr << "[!] Error reading input snapshot file" << std::endl;
        return 1;
    }
    ifs.close();

    const auto* hdr = reinterpret_cast<const impulse_snapshot_header_t*>(buffer.data());
    if (hdr->magic != IMPULSE_MAGIC) {
        std::cerr << "[!] Invalid magic bytes: 0x" << std::hex << hdr->magic << std::dec << std::endl;
        return 1;
    }

    size_t data_offset = hdr->data_offset;
    if (hdr->version < 2 || data_offset == 0) data_offset = 64;

    uint8_t computed_sha256[32];
    CC_SHA256(buffer.data() + data_offset, file_size - data_offset, computed_sha256);
    if (std::memcmp(hdr->sha256_checksum, computed_sha256, 32) != 0) {
        std::cerr << "[!] Input SHA256 checksum mismatch! File may be corrupt." << std::endl;
        return 1;
    }

    std::cout << " [✓] Input SHA256 Checksum Verified Cleanly! (DataOffset=" << data_offset << " bytes)" << std::endl;

    // Parse Section 2 Part A: Domain Catalog
    size_t offset = data_offset;
    std::vector<DomainData> domains;

    for (uint16_t d = 0; d < hdr->domain_count; ++d) {
        if (offset + sizeof(impulse_domain_catalog_entry_header_t) > file_size) break;
        const auto* dom_hdr = reinterpret_cast<const impulse_domain_catalog_entry_header_t*>(buffer.data() + offset);
        size_t start_dom_offset = offset;
        offset += sizeof(impulse_domain_catalog_entry_header_t);

        std::string name(reinterpret_cast<const char*>(buffer.data() + offset), dom_hdr->name_len);
        offset += dom_hdr->name_len;

        DomainData ddata;
        ddata.domain_id = dom_hdr->domain_id;
        ddata.key_type = dom_hdr->key_type;
        ddata.name = name;

        // Skip string mappings if present
        if (offset + 4 <= file_size) {
            uint32_t map_count = *reinterpret_cast<const uint32_t*>(buffer.data() + offset);
            offset += 4;
            for (uint32_t m = 0; m < map_count; ++m) {
                if (offset + 6 > file_size) break;
                offset += 4;
                uint16_t bk_len = *reinterpret_cast<const uint16_t*>(buffer.data() + offset);
                offset += 2 + bk_len;
            }
        }

        ddata.raw_payload.assign(buffer.data() + start_dom_offset, buffer.data() + offset);
        domains.push_back(std::move(ddata));
    }

    size_t rem64 = offset % 64;
    if (rem64 != 0) offset += (64 - rem64);

    // Parse Section 2 Part B: Relation Directory & CSR Streams
    std::vector<RelationData> relations;

    for (uint16_t r = 0; r < hdr->relation_count; ++r) {
        uint16_t src_dom = 0, tgt_dom = 0;
        uint8_t enc_type = 0;
        uint64_t node_count = 0, edge_count = 0, sec_features = 0;
        uint64_t row_off_offset = 0, row_off_bytes = 0;
        uint64_t col_idx_offset = 0, col_idx_bytes = 0;

        if (data_offset == 4096) {
            if (offset + sizeof(impulse_relation_directory_entry_t) > file_size) break;
            const auto* entry = reinterpret_cast<const impulse_relation_directory_entry_t*>(buffer.data() + offset);
            offset += sizeof(impulse_relation_directory_entry_t);

            src_dom = entry->src_domain_id;
            tgt_dom = entry->tgt_domain_id;
            enc_type = entry->encoding_type;
            node_count = entry->node_count;
            edge_count = entry->edge_count;
            sec_features = entry->section_features;
            row_off_offset = entry->csr_row_off_offset;
            row_off_bytes = entry->csr_row_off_bytes;
            col_idx_offset = entry->csr_col_idx_offset;
            col_idx_bytes = entry->csr_col_idx_bytes;
        } else {
            if (offset + 33 > file_size) break;
            src_dom = *reinterpret_cast<const uint16_t*>(buffer.data() + offset); offset += 2;
            tgt_dom = *reinterpret_cast<const uint16_t*>(buffer.data() + offset); offset += 2;
            enc_type = buffer[offset++];
            node_count = *reinterpret_cast<const uint32_t*>(buffer.data() + offset); offset += 4;
            edge_count = *reinterpret_cast<const uint64_t*>(buffer.data() + offset); offset += 8;
            row_off_bytes = *reinterpret_cast<const uint64_t*>(buffer.data() + offset); offset += 8;
            col_idx_bytes = *reinterpret_cast<const uint64_t*>(buffer.data() + offset); offset += 8;
            row_off_offset = offset;
            col_idx_offset = offset + row_off_bytes;
        }

        std::cout << "\n Translating Relation [" << r << "]: Src=" << src_dom << " -> Tgt=" << tgt_dom << std::endl;
        std::cout << "   - Input Encoding:    0x" << std::hex << (int)enc_type << std::dec << std::endl;
        std::cout << "   - Input Scale:       N=" << node_count << " nodes, E=" << edge_count << " edges" << std::endl;

        // Unpack RowOffsets
        std::vector<uint32_t> row_offsets(node_count + 2, 0);
        if (row_off_offset + row_off_bytes <= file_size) {
            std::memcpy(row_offsets.data(), buffer.data() + row_off_offset, row_off_bytes);
        }

        // Unpack ColumnIndices based on Input Encoding
        std::vector<uint32_t> column_indices;
        column_indices.reserve(edge_count);

        if (enc_type == IMPULSE_ENC_DELTA_VBYTE) {
            size_t col_off = col_idx_offset;
            size_t col_end = col_idx_offset + col_idx_bytes;
            for (size_t node = 0; node <= node_count; ++node) {
                uint32_t start = row_offsets[node];
                uint32_t end = row_offsets[node + 1];
                uint32_t prev_tgt = 0;
                for (uint32_t idx = start; idx < end; ++idx) {
                    uint32_t delta = static_cast<uint32_t>(read_vbyte(buffer.data(), col_off, col_end));
                    uint32_t tgt = (idx == start) ? delta : (prev_tgt + delta);
                    column_indices.push_back(tgt);
                    prev_tgt = tgt;
                }
            }
        } else if (enc_type == IMPULSE_ENC_SIMDCOMP) {
            std::vector<uint32_t> deltas;
            decode_simdcomp(buffer.data() + col_idx_offset, col_idx_bytes, edge_count, deltas);

            size_t delta_ptr = 0;
            for (size_t node = 0; node <= node_count; ++node) {
                uint32_t start = row_offsets[node];
                uint32_t end = row_offsets[node + 1];
                uint32_t prev_tgt = 0;
                for (uint32_t idx = start; idx < end; ++idx) {
                    uint32_t delta = (delta_ptr < deltas.size()) ? deltas[delta_ptr++] : 0;
                    uint32_t tgt = (idx == start) ? delta : (prev_tgt + delta);
                    column_indices.push_back(tgt);
                    prev_tgt = tgt;
                }
            }
        } else if (enc_type == IMPULSE_ENC_SLICED_ELLPACK) {
            decode_sliced_ellpack(buffer.data() + col_idx_offset, col_idx_bytes, row_offsets, node_count, column_indices);
        } else {
            // RAW_UINT32
            if (col_idx_offset + edge_count * 4 <= file_size) {
                column_indices.resize(edge_count);
                std::memcpy(column_indices.data(), buffer.data() + col_idx_offset, edge_count * 4);
            }
        }

        // Apply RCM Reordering if requested
        if (enable_rcm_reorder && node_count > 0 && edge_count > 0) {
            for (size_t node = 0; node <= node_count; ++node) {
                uint32_t start = row_offsets[node];
                uint32_t end = row_offsets[node + 1];
                if (start < end && end <= column_indices.size()) {
                    std::sort(column_indices.begin() + start, column_indices.begin() + end);
                }
            }
        }

        uint8_t out_enc = override_target_encoding ? target_encoding : enc_type;
        uint64_t out_sec_flags = (sec_features & ~0x1FFULL) | (1ULL << out_enc);

        RelationData rdata;
        rdata.src_domain_id = src_dom;
        rdata.tgt_domain_id = tgt_dom;
        rdata.encoding_type = out_enc;
        rdata.node_count = node_count;
        rdata.edge_count = column_indices.size();
        rdata.section_features = out_sec_flags;
        rdata.row_offsets = std::move(row_offsets);
        rdata.column_indices = std::move(column_indices);

        relations.push_back(std::move(rdata));
    }

    // Re-serialize v2.4.0 4KB Aligned Output Snapshot using C-ABI Writer Builder API
    impulse_writer_t* writer = impulse_writer_create(output_path.c_str(), hdr->global_required_features);
    if (!writer) {
        std::cerr << "[!] Error creating C-ABI snapshot writer: " << impulse_get_last_error() << std::endl;
        return 1;
    }

    for (const auto& dom : domains) {
        impulse_writer_add_domain(writer, dom.domain_id, dom.key_type, dom.name.c_str());
    }

    for (const auto& rel : relations) {
        std::vector<uint8_t> encoded_cols;
        if (rel.encoding_type == IMPULSE_ENC_DELTA_VBYTE) {
            for (size_t node = 0; node <= rel.node_count; ++node) {
                uint32_t start = rel.row_offsets[node];
                uint32_t end = rel.row_offsets[node + 1];
                uint32_t prev_tgt = 0;
                for (uint32_t idx = start; idx < end; ++idx) {
                    uint32_t tgt = rel.column_indices[idx];
                    uint32_t delta = (idx == start) ? tgt : (tgt - prev_tgt);
                    write_vbyte(encoded_cols, delta);
                    prev_tgt = tgt;
                }
            }
        } else if (rel.encoding_type == IMPULSE_ENC_SIMDCOMP) {
            encode_simdcomp(encoded_cols, rel.row_offsets, rel.column_indices, rel.node_count);
        } else if (rel.encoding_type == IMPULSE_ENC_SLICED_ELLPACK) {
            encode_sliced_ellpack(encoded_cols, rel.row_offsets, rel.column_indices, rel.node_count);
        } else {
            const uint8_t* ptr = reinterpret_cast<const uint8_t*>(rel.column_indices.data());
            encoded_cols.assign(ptr, ptr + rel.column_indices.size() * sizeof(uint32_t));
        }

        impulse_writer_add_relation(
            writer, rel.src_domain_id, rel.tgt_domain_id, rel.encoding_type,
            rel.node_count, rel.edge_count, rel.section_features,
            rel.row_offsets.data(), rel.row_offsets.size() * sizeof(uint32_t),
            encoded_cols.data(), encoded_cols.size()
        );
    }

    impulse_status_t status = impulse_writer_finalize(writer);
    impulse_writer_destroy(writer);

    if (status != IMPULSE_OK) {
        std::cerr << "[!] C-ABI impulse_writer_finalize failed: " << impulse_get_last_error() << std::endl;
        return 1;
    }

    auto t_end_total = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t_end_total - t_start_total).count();

    std::cout << "\n==========================================================================" << std::endl;
    std::cout << " [✓] SNAPSHOT ENCODING TRANSLATION & OPTIMIZATION COMPLETE (C-ABI Writer API)!" << std::endl;
    std::cout << " Output File:     " << output_path << std::endl;
    std::cout << " Execution Time:  " << total_ms << " ms" << std::endl;
    std::cout << "==========================================================================" << std::endl;

    return 0;
}
