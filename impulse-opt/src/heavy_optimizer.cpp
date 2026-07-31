#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <CommonCrypto/CommonDigest.h>

#pragma pack(push, 1)
struct SnapshotHeader {
    uint32_t magic;         // 0x494D5053 ("IMPS")
    uint16_t version;       // Format Version (2)
    uint32_t data_offset;   // Offset in bytes from file start to payload data (64)
    uint16_t domain_count;  // Domain count
    uint16_t relation_count;// Relation count
    uint64_t kafka_offset;  // Kafka WAL offset
    uint64_t timestamp_ms;  // Snapshot timestamp (ms)
    uint8_t sha256[32];     // 32-byte SHA256 payload digest
    uint8_t reserved[2];    // Reserved header padding (Header size = 64 bytes)
};
#pragma pack(pop)

enum RelationEncoding : uint8_t {
    ENCODING_RAW_UINT32             = 0x00,
    ENCODING_DELTA_VBYTE            = 0x01,
    ENCODING_RAW_UINT16             = 0x02,
    ENCODING_HYBRID_UINT16_UINT32   = 0x03,
    ENCODING_SIMDCOMP_BITPACKED     = 0x04
};

struct BusinessKeyMap {
    uint32_t dense_id;
    std::string business_key;
};

struct DomainData {
    uint16_t domain_id;
    uint8_t key_type;
    std::string name;
    std::vector<BusinessKeyMap> mappings;
};

struct RelationData {
    uint16_t src_domain_id;
    uint16_t tgt_domain_id;
    uint8_t encoding_type; // 0x00 = Raw uint32, 0x01 = Delta-VByte, 0x02 = Raw uint16, 0x03 = Hybrid
    uint32_t node_count;
    uint64_t edge_count;
    std::vector<uint32_t> row_offsets;
    std::vector<uint32_t> column_indices;
};

static std::string bytes_to_hex(const uint8_t* bytes, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
    }
    return oss.str();
}

static void write_vbyte(std::vector<uint8_t>& buf, uint64_t val) {
    while (val >= 0x80) {
        buf.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
        val >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(val & 0x7F));
}

static void pad64(std::vector<uint8_t>& buf) {
    size_t rem = buf.size() % 64;
    if (rem != 0) {
        buf.insert(buf.end(), 64 - rem, 0x00);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <input_snapshot.bin> <output_optimized.bin> [--vbyte] [--uint16] [--hybrid] [--optimize]" << std::endl;
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = argv[2];

    bool enable_vbyte = false;
    bool enable_uint16 = false;
    bool enable_hybrid = false;
    bool enable_optimize = false;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--vbyte") enable_vbyte = true;
        if (arg == "--uint16") enable_uint16 = true;
        if (arg == "--hybrid") enable_hybrid = true;
        if (arg == "--optimize") enable_optimize = true;
    }

    std::cout << "==========================================================================" << std::endl;
    std::cout << " IMPULSE-GRAPH C++20 HEAVY OPTIMIZER (64-Byte SIMD Aligned & DataOffset)" << std::endl;
    std::cout << "==========================================================================" << std::endl;
    std::cout << " Input Snapshot File:  " << input_path << std::endl;
    std::cout << " Output Snapshot File: " << output_path << std::endl;
    std::cout << " Degree Permutation:   " << (enable_optimize ? "ENABLED" : "DISABLED (Preserve Original Order)") << std::endl;
    std::cout << " VByte / SIMDComp Opt: " << (enable_vbyte ? "ENABLED (0x01)" : "DISABLED") << std::endl;
    std::cout << " RAW uint16 Opt:       " << (enable_uint16 ? "ENABLED (0x02)" : "DISABLED") << std::endl;
    std::cout << " HYBRID uint16/32 Opt: " << (enable_hybrid ? "ENABLED (0x03)" : "DISABLED") << std::endl;

    auto t_start_total = std::chrono::high_resolution_clock::now();

    // 1. Read Binary File
    auto t0 = std::chrono::high_resolution_clock::now();
    std::ifstream ifs(input_path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        std::cerr << "[!] Error opening input snapshot file: " << input_path << std::endl;
        return 1;
    }

    std::streamsize file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    if (file_size < 58) {
        std::cerr << "[!] Snapshot file too small: " << file_size << " bytes" << std::endl;
        return 1;
    }

    std::vector<uint8_t> buffer(file_size);
    if (!ifs.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        std::cerr << "[!] Error reading snapshot buffer" << std::endl;
        return 1;
    }
    ifs.close();
    auto t_read = std::chrono::high_resolution_clock::now();

    // 2. Parse Header
    uint32_t magic = *reinterpret_cast<uint32_t*>(buffer.data());
    uint16_t version = *reinterpret_cast<uint16_t*>(buffer.data() + 4);

    if (magic != 0x494D5053) {
        std::cerr << "[!] Invalid magic bytes: 0x" << std::hex << magic << std::dec << std::endl;
        return 1;
    }

    size_t data_offset = 58;
    uint16_t domain_count = 0;
    uint16_t relation_count = 0;
    uint64_t kafka_offset = 0;
    uint64_t timestamp_ms = 0;
    const uint8_t* expected_sha256 = nullptr;

    if (version >= 2 && file_size >= 64) {
        data_offset = *reinterpret_cast<uint32_t*>(buffer.data() + 6);
        domain_count = *reinterpret_cast<uint16_t*>(buffer.data() + 10);
        relation_count = *reinterpret_cast<uint16_t*>(buffer.data() + 12);
        kafka_offset = *reinterpret_cast<uint64_t*>(buffer.data() + 14);
        timestamp_ms = *reinterpret_cast<uint64_t*>(buffer.data() + 22);
        expected_sha256 = buffer.data() + 30;
    } else {
        domain_count = *reinterpret_cast<uint16_t*>(buffer.data() + 6);
        relation_count = *reinterpret_cast<uint16_t*>(buffer.data() + 8);
        kafka_offset = *reinterpret_cast<uint64_t*>(buffer.data() + 10);
        timestamp_ms = *reinterpret_cast<uint64_t*>(buffer.data() + 18);
        expected_sha256 = buffer.data() + 26;
        data_offset = 58;
    }

    uint8_t computed_sha256[32];
    CC_SHA256(buffer.data() + data_offset, file_size - data_offset, computed_sha256);

    std::string expected_hex = bytes_to_hex(expected_sha256, 32);

    if (std::memcmp(expected_sha256, computed_sha256, 32) != 0) {
        std::cerr << "[!] SHA256 Checksum mismatch during C++ input load!" << std::endl;
        return 1;
    }
    std::cout << " [✓] Payload SHA256 Checksum Verified Cleanly! (DataOffset=" << data_offset << " bytes)" << std::endl;

    // 3. Unpack Domains & Relations
    auto t1 = std::chrono::high_resolution_clock::now();
    size_t offset = data_offset;

    std::vector<DomainData> domains(domain_count);
    for (uint16_t d = 0; d < domain_count; ++d) {
        std::memcpy(&domains[d].domain_id, buffer.data() + offset, 2); offset += 2;
        domains[d].key_type = buffer[offset++];
        uint16_t name_len;
        std::memcpy(&name_len, buffer.data() + offset, 2); offset += 2;
        domains[d].name.assign(reinterpret_cast<char*>(buffer.data() + offset), name_len); offset += name_len;

        uint32_t map_count;
        std::memcpy(&map_count, buffer.data() + offset, 4); offset += 4;
        domains[d].mappings.resize(map_count);
        for (uint32_t m = 0; m < map_count; ++m) {
            std::memcpy(&domains[d].mappings[m].dense_id, buffer.data() + offset, 4); offset += 4;
            uint16_t bk_len;
            std::memcpy(&bk_len, buffer.data() + offset, 2); offset += 2;
            domains[d].mappings[m].business_key.assign(reinterpret_cast<char*>(buffer.data() + offset), bk_len); offset += bk_len;
        }
    }

    std::vector<RelationData> relations(relation_count);
    for (uint16_t r = 0; r < relation_count; ++r) {
        std::memcpy(&relations[r].src_domain_id, buffer.data() + offset, 2); offset += 2;
        std::memcpy(&relations[r].tgt_domain_id, buffer.data() + offset, 2); offset += 2;
        
        if (version >= 2) {
            relations[r].encoding_type = buffer[offset++];
        } else {
            relations[r].encoding_type = ENCODING_RAW_UINT32;
        }

        std::memcpy(&relations[r].node_count, buffer.data() + offset, 4); offset += 4;
        std::memcpy(&relations[r].edge_count, buffer.data() + offset, 8); offset += 8;

        uint64_t row_off_bytes, col_idx_bytes;
        std::memcpy(&row_off_bytes, buffer.data() + offset, 8); offset += 8;
        std::memcpy(&col_idx_bytes, buffer.data() + offset, 8); offset += 8;

        uint32_t num_row_offsets = row_off_bytes / 4;
        relations[r].row_offsets.resize(num_row_offsets);
        std::memcpy(relations[r].row_offsets.data(), buffer.data() + offset, row_off_bytes); offset += row_off_bytes;

        uint32_t num_col_indices;
        if (relations[r].encoding_type == ENCODING_RAW_UINT16) {
            num_col_indices = col_idx_bytes / 2;
            relations[r].column_indices.resize(num_col_indices);
            const uint16_t* u16_ptr = reinterpret_cast<const uint16_t*>(buffer.data() + offset);
            for (uint32_t c = 0; c < num_col_indices; ++c) {
                relations[r].column_indices[c] = u16_ptr[c];
            }
            offset += col_idx_bytes;
        } else if (relations[r].encoding_type == ENCODING_HYBRID_UINT16_UINT32) {
            relations[r].column_indices.resize(relations[r].edge_count);
            uint32_t col_ptr = 0;
            for (uint32_t node = 0; node <= relations[r].node_count; ++node) {
                uint32_t start = relations[r].row_offsets[node];
                uint32_t end = relations[r].row_offsets[node + 1];
                uint32_t row_len = end - start;
                uint16_t num_hot = *reinterpret_cast<const uint16_t*>(buffer.data() + offset); offset += 2;
                for (uint16_t i = 0; i < num_hot; ++i) {
                    uint16_t tgt16 = *reinterpret_cast<const uint16_t*>(buffer.data() + offset); offset += 2;
                    relations[r].column_indices[col_ptr++] = tgt16;
                }
                for (uint32_t i = num_hot; i < row_len; ++i) {
                    uint32_t tgt32 = *reinterpret_cast<const uint32_t*>(buffer.data() + offset); offset += 4;
                    relations[r].column_indices[col_ptr++] = tgt32;
                }
            }
        } else {
            num_col_indices = col_idx_bytes / 4;
            relations[r].column_indices.resize(num_col_indices);
            std::memcpy(relations[r].column_indices.data(), buffer.data() + offset, col_idx_bytes); offset += col_idx_bytes;
        }
    }
    auto t_unpack = std::chrono::high_resolution_clock::now();

    // 4. Perform Heavy Graph Optimization: Degree Permutation & Ascending Sort
    std::cout << "\n--------------------------------------------------------------------------" << std::endl;
    std::cout << " EXECUTING DEGREE PERMUTATION & VBYTE / SIMDCOMP EDGE COMPRESSION..." << std::endl;
    std::cout << "--------------------------------------------------------------------------" << std::endl;

    for (uint16_t r = 0; r < relation_count; ++r) {
        RelationData& rel = relations[r];
        if (rel.node_count == 0 || rel.edge_count == 0) continue;

        if (enable_optimize) {
            std::vector<uint32_t> degrees(rel.node_count + 1, 0);
            for (uint32_t node = 0; node <= rel.node_count; ++node) {
                degrees[node] = rel.row_offsets[node + 1] - rel.row_offsets[node];
            }

            std::vector<uint32_t> perm(rel.node_count + 1);
            std::iota(perm.begin(), perm.end(), 0);
            std::sort(perm.begin(), perm.end(), [&](uint32_t a, uint32_t b) {
                return degrees[a] > degrees[b];
            });

            std::vector<uint32_t> old_to_new(rel.node_count + 1);
            for (uint32_t new_id = 0; new_id <= rel.node_count; ++new_id) {
                old_to_new[perm[new_id]] = new_id;
            }

            std::vector<uint32_t> new_column_indices;
            std::vector<uint32_t> new_row_offsets(rel.node_count + 2, 0);

            uint32_t curr_off = 0;
            for (uint32_t new_node = 0; new_node <= rel.node_count; ++new_node) {
                new_row_offsets[new_node] = curr_off;
                uint32_t old_node = perm[new_node];
                uint32_t start = rel.row_offsets[old_node];
                uint32_t end = rel.row_offsets[old_node + 1];

                std::vector<uint32_t> neighbors;
                for (uint32_t idx = start; idx < end; ++idx) {
                    uint32_t old_tgt = rel.column_indices[idx];
                    uint32_t new_tgt = (old_tgt <= rel.node_count) ? old_to_new[old_tgt] : old_tgt;
                    neighbors.push_back(new_tgt);
                }

                std::sort(neighbors.begin(), neighbors.end());

                for (uint32_t tgt : neighbors) {
                    new_column_indices.push_back(tgt);
                    curr_off++;
                }
            }
            new_row_offsets[rel.node_count + 1] = curr_off;

            rel.row_offsets = std::move(new_row_offsets);
            rel.column_indices = std::move(new_column_indices);
            rel.edge_count = rel.column_indices.size();
        } else {
            for (uint32_t node = 0; node <= rel.node_count; ++node) {
                uint32_t start = rel.row_offsets[node];
                uint32_t end = rel.row_offsets[node + 1];
                if (start < end && end <= rel.column_indices.size()) {
                    std::sort(rel.column_indices.begin() + start, rel.column_indices.begin() + end);
                }
            }
        }

        if (enable_vbyte) {
            rel.encoding_type = ENCODING_DELTA_VBYTE;
        } else if (enable_hybrid) {
            rel.encoding_type = ENCODING_HYBRID_UINT16_UINT32;
        } else if (enable_uint16) {
            bool all_fit_u16 = true;
            for (uint32_t tgt : rel.column_indices) {
                if (tgt >= 65536) {
                    all_fit_u16 = false;
                    break;
                }
            }
            if (all_fit_u16) {
                rel.encoding_type = ENCODING_RAW_UINT16;
            }
        }

        std::cout << "   Relation [" << r << "]: " << rel.node_count << " nodes & " << rel.edge_count 
                  << " edges (Encoding 0x" << std::hex << (int)rel.encoding_type << std::dec << ")" << std::endl;
    }

    auto t_opt = std::chrono::high_resolution_clock::now();

    // 5. Re-serialize Canonical Binary Payload with 64-byte Memory Alignment
    std::vector<uint8_t> payload;

    // Serialize Domain Section
    for (const auto& dom : domains) {
        uint16_t d_id = dom.domain_id;
        uint8_t k_type = dom.key_type;
        uint16_t n_len = dom.name.size();
        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&d_id), reinterpret_cast<uint8_t*>(&d_id) + 2);
        payload.push_back(k_type);
        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&n_len), reinterpret_cast<uint8_t*>(&n_len) + 2);
        payload.insert(payload.end(), dom.name.begin(), dom.name.end());

        uint32_t m_count = dom.mappings.size();
        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&m_count), reinterpret_cast<uint8_t*>(&m_count) + 4);
        for (const auto& m : dom.mappings) {
            uint32_t dense_id = m.dense_id;
            uint16_t bk_len = m.business_key.size();
            payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&dense_id), reinterpret_cast<uint8_t*>(&dense_id) + 4);
            payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&bk_len), reinterpret_cast<uint8_t*>(&bk_len) + 2);
            payload.insert(payload.end(), m.business_key.begin(), m.business_key.end());
        }
    }
    std::cout << "[DEBUG C++] Domain Payload Size: " << payload.size() << " bytes" << std::endl;
    pad64(payload); // 64-byte align end of Domain section
    std::cout << "[DEBUG C++] Padded Domain Payload Size: " << payload.size() << " bytes" << std::endl;

    // Serialize Relation Section
    for (const auto& rel : relations) {
        uint16_t src_id = rel.src_domain_id;
        uint16_t tgt_id = rel.tgt_domain_id;
        uint8_t enc_type = rel.encoding_type;
        uint32_t node_count = rel.node_count;
        uint64_t edge_count = rel.edge_count;
        uint64_t row_off_bytes = rel.row_offsets.size() * 4;

        std::vector<uint8_t> encoded_col_bytes;
        if (enc_type == ENCODING_DELTA_VBYTE) {
            for (uint32_t node = 0; node <= rel.node_count; ++node) {
                uint32_t start = rel.row_offsets[node];
                uint32_t end = rel.row_offsets[node + 1];
                uint32_t prev_tgt = 0;
                for (uint32_t idx = start; idx < end; ++idx) {
                    uint32_t tgt = rel.column_indices[idx];
                    uint32_t delta = (idx == start) ? tgt : (tgt - prev_tgt);
                    write_vbyte(encoded_col_bytes, delta);
                    prev_tgt = tgt;
                }
            }
        } else if (enc_type == ENCODING_HYBRID_UINT16_UINT32) {
            for (uint32_t node = 0; node <= rel.node_count; ++node) {
                uint32_t start = rel.row_offsets[node];
                uint32_t end = rel.row_offsets[node + 1];
                uint16_t num_hot = 0;
                for (uint32_t idx = start; idx < end; ++idx) {
                    if (rel.column_indices[idx] < 65536) {
                        num_hot++;
                    } else {
                        break;
                    }
                }
                const uint8_t* hot_ptr = reinterpret_cast<const uint8_t*>(&num_hot);
                encoded_col_bytes.insert(encoded_col_bytes.end(), hot_ptr, hot_ptr + 2);
                for (uint32_t idx = start; idx < start + num_hot; ++idx) {
                    uint16_t tgt16 = static_cast<uint16_t>(rel.column_indices[idx]);
                    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&tgt16);
                    encoded_col_bytes.insert(encoded_col_bytes.end(), ptr, ptr + 2);
                }
                for (uint32_t idx = start + num_hot; idx < end; ++idx) {
                    uint32_t tgt32 = rel.column_indices[idx];
                    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&tgt32);
                    encoded_col_bytes.insert(encoded_col_bytes.end(), ptr, ptr + 4);
                }
            }
        } else if (enc_type == ENCODING_RAW_UINT16) {
            for (uint32_t tgt : rel.column_indices) {
                uint16_t tgt16 = static_cast<uint16_t>(tgt);
                const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&tgt16);
                encoded_col_bytes.insert(encoded_col_bytes.end(), ptr, ptr + 2);
            }
        } else {
            const uint8_t* c_ptr = reinterpret_cast<const uint8_t*>(rel.column_indices.data());
            encoded_col_bytes.assign(c_ptr, c_ptr + rel.column_indices.size() * 4);
        }

        uint64_t col_idx_bytes = encoded_col_bytes.size();

        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&src_id), reinterpret_cast<uint8_t*>(&src_id) + 2);
        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&tgt_id), reinterpret_cast<uint8_t*>(&tgt_id) + 2);
        payload.push_back(enc_type);
        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&node_count), reinterpret_cast<uint8_t*>(&node_count) + 4);
        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&edge_count), reinterpret_cast<uint8_t*>(&edge_count) + 8);
        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&row_off_bytes), reinterpret_cast<uint8_t*>(&row_off_bytes) + 8);
        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&col_idx_bytes), reinterpret_cast<uint8_t*>(&col_idx_bytes) + 8);
        pad64(payload); // 64-byte align before rowOffsets array

        const uint8_t* r_ptr = reinterpret_cast<const uint8_t*>(rel.row_offsets.data());
        payload.insert(payload.end(), r_ptr, r_ptr + row_off_bytes);
        pad64(payload); // 64-byte align rowOffsets array

        payload.insert(payload.end(), encoded_col_bytes.begin(), encoded_col_bytes.end());
        pad64(payload); // 64-byte align columnIndices array
    }

    // Compute New SHA256 Digest over padded payload
    uint8_t new_sha256[32];
    CC_SHA256(payload.data(), payload.size(), new_sha256);

    // Build 64-byte Aligned Header
    SnapshotHeader out_header;
    out_header.magic = 0x494D5053;
    out_header.version = 2;
    out_header.data_offset = 64;
    out_header.domain_count = domain_count;
    out_header.relation_count = relation_count;
    out_header.kafka_offset = kafka_offset;
    out_header.timestamp_ms = timestamp_ms;
    std::memcpy(out_header.sha256, new_sha256, 32);
    std::memset(out_header.reserved, 0x00, sizeof(out_header.reserved));

    std::ofstream ofs(output_path, std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "[!] Error creating output snapshot file: " << output_path << std::endl;
        return 1;
    }

    ofs.write(reinterpret_cast<char*>(&out_header), sizeof(SnapshotHeader)); // 64 bytes
    ofs.write(reinterpret_cast<char*>(payload.data()), payload.size());
    ofs.close();

    auto t_end_total = std::chrono::high_resolution_clock::now();

    double ms_read = std::chrono::duration<double, std::milli>(t_read - t0).count();
    double ms_unpack = std::chrono::duration<double, std::milli>(t_unpack - t_read).count();
    double ms_opt = std::chrono::duration<double, std::milli>(t_opt - t_unpack).count();
    double ms_save = std::chrono::duration<double, std::milli>(t_end_total - t_opt).count();
    double ms_total = std::chrono::duration<double, std::milli>(t_end_total - t_start_total).count();

    std::cout << "\n==========================================================================" << std::endl;
    std::cout << " C++20 HEAVY OPTIMIZER PERFORMANCE BREAKDOWN (64-Byte Aligned)" << std::endl;
    std::cout << "==========================================================================" << std::endl;
    std::cout << " Total Execution Time:        " << ms_total << " ms" << std::endl;
    std::cout << " NEW Optimized SHA256:         " << bytes_to_hex(new_sha256, 32) << std::endl;
    std::cout << " [✓] 64-Byte SIMD-Aligned Snapshot Saved: " << output_path << std::endl;
    std::cout << "==========================================================================" << std::endl;

    return 0;
}
