#ifndef IMPULSE_COMPILER_HPP
#define IMPULSE_COMPILER_HPP

#include "impulse_graph.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <filesystem>
#include <CommonCrypto/CommonDigest.h>

namespace fs = std::filesystem;

namespace impulse {

struct DomainManifest {
    uint16_t id;
    std::string name;
    impulse_key_type_t key_type;
};

struct RelationManifest {
    uint16_t src_domain;
    uint16_t tgt_domain;
    impulse_encoding_type_t encoding;
    uint64_t section_features;
    std::string filename;
};

struct CompilerManifest {
    std::string version = "2.4.0";
    uint64_t global_features = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED;
    std::vector<DomainManifest> domains;
    std::vector<RelationManifest> relations;
};

struct RawEdge {
    std::string src_key;
    std::string tgt_key;
    uint32_t src_id = 0;
    uint32_t tgt_id = 0;
};

struct CompiledRelation {
    uint16_t src_domain_id;
    uint16_t tgt_domain_id;
    uint8_t encoding_type;
    uint64_t node_count;
    uint64_t edge_count;
    uint64_t section_features;
    std::vector<uint32_t> row_offsets;
    std::vector<uint32_t> column_indices;
    std::vector<uint8_t> encoded_col_indices;
};

class SnapshotCompiler {
public:
    static void write_vbyte(std::vector<uint8_t>& buf, uint64_t val) {
        while (val >= 0x80) {
            buf.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
            val >>= 7;
        }
        buf.push_back(static_cast<uint8_t>(val & 0x7F));
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

        // Pack 128-integer SIMDComp blocks
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

    static void encode_sliced_ellpack(std::vector<uint8_t>& buf, const std::vector<uint32_t>& row_offsets, const std::vector<uint32_t>& col_indices, uint64_t node_count) {
        // Sliced ELLPACK GPU format: 32-row slice warp width
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

            // Write 32-bit slice header (max_deg)
            const uint8_t* deg_ptr = reinterpret_cast<const uint8_t*>(&max_deg);
            buf.insert(buf.end(), deg_ptr, deg_ptr + 4);

            // Write column-major warp coalesced array
            for (uint32_t col = 0; col < max_deg; ++col) {
                for (size_t r = start_row; r < start_row + SLICE_SIZE; ++r) {
                    uint32_t val = 0xFFFFFFFF; // Padding marker
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

    static CompilerManifest parse_simple_manifest(const std::string& manifest_path) {
        CompilerManifest manifest;
        std::ifstream ifs(manifest_path);
        if (!ifs.is_open()) {
            throw std::runtime_error("Could not open manifest file: " + manifest_path);
        }

        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        
        std::istringstream iss(content);
        std::string line;
        bool in_domains = false;
        bool in_relations = false;

        while (std::getline(iss, line)) {
            if (line.find("\"domains\"") != std::string::npos) { in_domains = true; in_relations = false; }
            if (line.find("\"relations\"") != std::string::npos) { in_relations = true; in_domains = false; }

            if (in_domains && line.find("\"name\"") != std::string::npos) {
                DomainManifest dom;
                dom.id = manifest.domains.size();
                size_t q1 = line.find('"', line.find("\"name\"") + 6);
                size_t q2 = line.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    dom.name = line.substr(q1 + 1, q2 - q1 - 1);
                } else {
                    dom.name = "Domain_" + std::to_string(dom.id);
                }
                dom.key_type = IMPULSE_KEY_TYPE_STRING;
                manifest.domains.push_back(dom);
            }

            if (in_relations && line.find("\"file\"") != std::string::npos) {
                RelationManifest rel;
                rel.src_domain = 0;
                rel.tgt_domain = manifest.domains.size() > 1 ? 1 : 0;
                rel.encoding = IMPULSE_ENC_RAW_UINT32;
                rel.section_features = IMPULSE_RELATION_FEAT_ENC_RAW_UINT32;

                if (content.find("\"encoding\": \"simdcomp\"") != std::string::npos || content.find("\"simdcomp\"") != std::string::npos) {
                    rel.encoding = IMPULSE_ENC_SIMDCOMP;
                    rel.section_features = IMPULSE_RELATION_FEAT_ENC_SIMDCOMP;
                } else if (content.find("\"encoding\": \"sliced_ellpack\"") != std::string::npos || content.find("\"sliced_ellpack\"") != std::string::npos || content.find("\"ellpack\"") != std::string::npos) {
                    rel.encoding = IMPULSE_ENC_SLICED_ELLPACK;
                    rel.section_features = IMPULSE_RELATION_FEAT_ENC_SLICED_ELLPACK;
                } else if (content.find("\"encoding\": \"delta_vbyte\"") != std::string::npos || content.find("\"delta_vbyte\"") != std::string::npos) {
                    rel.encoding = IMPULSE_ENC_DELTA_VBYTE;
                    rel.section_features = IMPULSE_RELATION_FEAT_ENC_DELTA_VBYTE;
                } else if (content.find("\"encoding\": \"raw_uint32\"") != std::string::npos || content.find("\"raw_uint32\"") != std::string::npos) {
                    rel.encoding = IMPULSE_ENC_RAW_UINT32;
                    rel.section_features = IMPULSE_RELATION_FEAT_ENC_RAW_UINT32;
                }

                size_t q1 = line.find('"', line.find("\"file\"") + 6);
                size_t q2 = line.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    rel.filename = line.substr(q1 + 1, q2 - q1 - 1);
                } else {
                    rel.filename = "edges.tsv";
                }
                manifest.relations.push_back(rel);
            }
        }

        if (manifest.domains.empty()) {
            DomainManifest d0{0, "SourceDomain", IMPULSE_KEY_TYPE_STRING};
            DomainManifest d1{1, "TargetDomain", IMPULSE_KEY_TYPE_STRING};
            manifest.domains.push_back(d0);
            manifest.domains.push_back(d1);
        }

        if (manifest.relations.empty()) {
            RelationManifest r0{0, 1, IMPULSE_ENC_DELTA_VBYTE, IMPULSE_RELATION_FEAT_ENC_DELTA_VBYTE, "edges.tsv"};
            manifest.relations.push_back(r0);
        }

        return manifest;
    }

    static std::string format_global_features(uint64_t flags) {
        std::vector<std::string> names;
        if (flags & IMPULSE_GLOBAL_FEAT_64BIT_NODES) names.push_back("GLOBAL_FEAT_64BIT_NODES");
        if (flags & IMPULSE_GLOBAL_FEAT_ZSTD_DICT_EMBEDDED) names.push_back("GLOBAL_FEAT_ZSTD_DICT_EMBEDDED");
        if (flags & IMPULSE_GLOBAL_FEAT_DELTA_LOG_PRESENT) names.push_back("GLOBAL_FEAT_DELTA_LOG_PRESENT");
        if (flags & IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED) names.push_back("GLOBAL_FEAT_4KB_PAGE_ALIGNED");
        
        std::ostringstream oss;
        oss << "0x" << std::hex << std::setw(16) << std::setfill('0') << flags << " [";
        for (size_t i = 0; i < names.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << names[i];
        }
        oss << "]";
        return oss.str();
    }

    static std::string format_section_features(uint64_t flags) {
        std::vector<std::string> names;
        if (flags & IMPULSE_RELATION_FEAT_ENC_RAW_UINT32) names.push_back("RELATION_FEAT_ENC_RAW_UINT32");
        if (flags & IMPULSE_RELATION_FEAT_ENC_DELTA_VBYTE) names.push_back("RELATION_FEAT_ENC_DELTA_VBYTE");
        if (flags & IMPULSE_RELATION_FEAT_ENC_RAW_UINT16) names.push_back("RELATION_FEAT_ENC_RAW_UINT16");
        if (flags & IMPULSE_RELATION_FEAT_ENC_HYBRID_16_32) names.push_back("RELATION_FEAT_ENC_HYBRID_16_32");
        if (flags & IMPULSE_RELATION_FEAT_ENC_SIMDCOMP) names.push_back("RELATION_FEAT_ENC_SIMDCOMP");
        if (flags & IMPULSE_RELATION_FEAT_ENC_SLICED_ELLPACK) names.push_back("RELATION_FEAT_ENC_SLICED_ELLPACK");
        if (flags & IMPULSE_RELATION_FEAT_ENC_TPU_BCOO) names.push_back("RELATION_FEAT_ENC_TPU_BCOO");
        if (flags & IMPULSE_RELATION_FEAT_ENC_RAW_UINT64) names.push_back("RELATION_FEAT_ENC_RAW_UINT64");
        if (flags & IMPULSE_RELATION_FEAT_ENC_ROARING_BITMAP) names.push_back("RELATION_FEAT_ENC_ROARING_BITMAP");

        if (flags & IMPULSE_RELATION_FEAT_WEIGHTED_EDGES) names.push_back("RELATION_FEAT_WEIGHTED_EDGES");
        if (flags & IMPULSE_RELATION_FEAT_KV_LABELS) names.push_back("RELATION_FEAT_KV_LABELS");
        if (flags & IMPULSE_RELATION_FEAT_DTO_EDGE_ANNOTATIONS) names.push_back("RELATION_FEAT_DTO_EDGE_ANNOTATIONS");
        if (flags & IMPULSE_RELATION_FEAT_TEMPORAL_TIMESTAMPS) names.push_back("RELATION_FEAT_TEMPORAL_TIMESTAMPS");
        if (flags & IMPULSE_RELATION_FEAT_PER_SECTION_ZSTD) names.push_back("RELATION_FEAT_PER_SECTION_ZSTD");
        if (flags & IMPULSE_RELATION_FEAT_INCOMING_CSR_INDEX) names.push_back("RELATION_FEAT_INCOMING_CSR_INDEX");

        std::ostringstream oss;
        oss << "0x" << std::hex << std::setw(16) << std::setfill('0') << flags << " [";
        for (size_t i = 0; i < names.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << names[i];
        }
        oss << "]";
        return oss.str();
    }

    static bool compile_directory(const std::string& input_dir, const std::string& output_imps_path) {
        auto t_start_total = std::chrono::high_resolution_clock::now();

        std::string manifest_path = (fs::path(input_dir) / "manifest.json").string();
        std::cout << "==========================================================================" << std::endl;
        std::cout << " IMPULSE-COMPILE: C++20 BINARY SNAPSHOT COMPILER & BENCHMARK SUITE" << std::endl;
        std::cout << "==========================================================================" << std::endl;
        std::cout << " Input Directory:  " << input_dir << std::endl;
        std::cout << " Manifest File:   " << manifest_path << std::endl;
        std::cout << " Output .imps:     " << output_imps_path << std::endl;

        CompilerManifest manifest = parse_simple_manifest(manifest_path);

        std::cout << " Found " << manifest.domains.size() << " domains and " 
                  << manifest.relations.size() << " relation manifests." << std::endl;
        std::cout << " Global Features:  " << format_global_features(manifest.global_features) << std::endl;

        std::vector<CompiledRelation> compiled_relations;
        double total_parse_ms = 0.0;
        double total_build_ms = 0.0;
        double total_encode_ms = 0.0;
        uint64_t grand_total_edges = 0;
        uint64_t grand_total_raw_bytes = 0;
        uint64_t grand_total_encoded_bytes = 0;

        // Process Relations
        for (const auto& rel_m : manifest.relations) {
            fs::path edge_path = fs::path(input_dir) / rel_m.filename;
            std::cout << "\n--------------------------------------------------------------------------" << std::endl;
            std::cout << " Compiling Relation File: " << rel_m.filename << std::endl;
            std::cout << "--------------------------------------------------------------------------" << std::endl;

            auto t_parse_0 = std::chrono::high_resolution_clock::now();
            std::ifstream edge_ifs(edge_path);
            if (!edge_ifs.is_open()) {
                std::cerr << "[!] Error opening edge file: " << edge_path << std::endl;
                return false;
            }

            std::unordered_map<std::string, uint32_t> src_key_map;
            std::unordered_map<std::string, uint32_t> tgt_key_map;
            std::vector<RawEdge> edges;

            std::string line;
            while (std::getline(edge_ifs, line)) {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream iss(line);
                std::string src_k, tgt_k;
                if (iss >> src_k >> tgt_k) {
                    uint32_t s_id = 0, t_id = 0;
                    if (src_key_map.find(src_k) == src_key_map.end()) {
                        s_id = src_key_map.size();
                        src_key_map[src_k] = s_id;
                    } else {
                        s_id = src_key_map[src_k];
                    }

                    if (tgt_key_map.find(tgt_k) == tgt_key_map.end()) {
                        t_id = tgt_key_map.size();
                        tgt_key_map[tgt_k] = t_id;
                    } else {
                        t_id = tgt_key_map[tgt_k];
                    }

                    edges.push_back({src_k, tgt_k, s_id, t_id});
                }
            }
            auto t_parse_1 = std::chrono::high_resolution_clock::now();
            double parse_ms = std::chrono::duration<double, std::milli>(t_parse_1 - t_parse_0).count();
            total_parse_ms += parse_ms;

            uint64_t node_count = src_key_map.size();
            uint64_t edge_count = edges.size();
            grand_total_edges += edge_count;

            // Build RowOffsets & CSR
            auto t_build_0 = std::chrono::high_resolution_clock::now();
            std::sort(edges.begin(), edges.end(), [](const RawEdge& a, const RawEdge& b) {
                if (a.src_id != b.src_id) return a.src_id < b.src_id;
                return a.tgt_id < b.tgt_id;
            });

            std::vector<uint32_t> row_offsets(node_count + 2, 0);
            std::vector<uint32_t> col_indices;
            col_indices.reserve(edge_count);

            uint32_t curr_off = 0;
            uint32_t curr_node = 0;

            for (const auto& e : edges) {
                while (curr_node < e.src_id) {
                    row_offsets[curr_node + 1] = curr_off;
                    curr_node++;
                }
                col_indices.push_back(e.tgt_id);
                curr_off++;
            }
            while (curr_node <= node_count) {
                row_offsets[curr_node + 1] = curr_off;
                curr_node++;
            }
            auto t_build_1 = std::chrono::high_resolution_clock::now();
            double build_ms = std::chrono::duration<double, std::milli>(t_build_1 - t_build_0).count();
            total_build_ms += build_ms;

            // Encode ColumnIndices stream
            auto t_encode_0 = std::chrono::high_resolution_clock::now();
            std::vector<uint8_t> encoded_cols;
            if (rel_m.encoding == IMPULSE_ENC_DELTA_VBYTE) {
                for (size_t node = 0; node <= node_count; ++node) {
                    uint32_t start = row_offsets[node];
                    uint32_t end = row_offsets[node + 1];
                    uint32_t prev_tgt = 0;
                    for (uint32_t idx = start; idx < end; ++idx) {
                        uint32_t tgt = col_indices[idx];
                        uint32_t delta = (idx == start) ? tgt : (tgt - prev_tgt);
                        write_vbyte(encoded_cols, delta);
                        prev_tgt = tgt;
                    }
                }
            } else if (rel_m.encoding == IMPULSE_ENC_SIMDCOMP) {
                encode_simdcomp(encoded_cols, row_offsets, col_indices, node_count);
            } else if (rel_m.encoding == IMPULSE_ENC_SLICED_ELLPACK) {
                encode_sliced_ellpack(encoded_cols, row_offsets, col_indices, node_count);
            } else {
                const uint8_t* ptr = reinterpret_cast<const uint8_t*>(col_indices.data());
                encoded_cols.assign(ptr, ptr + col_indices.size() * sizeof(uint32_t));
            }
            auto t_encode_1 = std::chrono::high_resolution_clock::now();
            double encode_ms = std::chrono::duration<double, std::milli>(t_encode_1 - t_encode_0).count();
            total_encode_ms += encode_ms;

            uint64_t raw_csr_bytes = (node_count + 2) * 4 + edge_count * 4;
            uint64_t comp_csr_bytes = (node_count + 2) * 4 + encoded_cols.size();
            grand_total_raw_bytes += raw_csr_bytes;
            grand_total_encoded_bytes += comp_csr_bytes;

            double ratio = (double)raw_csr_bytes / (double)comp_csr_bytes;
            double savings = (1.0 - (double)comp_csr_bytes / (double)raw_csr_bytes) * 100.0;

            uint64_t sec_flags = rel_m.section_features | (1ULL << rel_m.encoding);

            std::cout << "   - Graph Topology:     " << node_count << " nodes, " << edge_count << " edges" << std::endl;
            std::cout << "   - Section Features:   " << format_section_features(sec_flags) << std::endl;
            std::cout << "   - Raw uint32 CSR Size:" << std::fixed << std::setprecision(2) << (raw_csr_bytes / 1024.0 / 1024.0) << " MB" << std::endl;
            std::cout << "   - Encoded CSR Size:   " << (comp_csr_bytes / 1024.0 / 1024.0) << " MB" << std::endl;
            std::cout << "   - Compression Ratio:  " << ratio << "x (" << savings << "% space savings)" << std::endl;
            std::cout << "   - Relation Timings:   Parse: " << parse_ms << " ms | Build CSR: " << build_ms << " ms | Encode: " << encode_ms << " ms" << std::endl;

            CompiledRelation crel;
            crel.src_domain_id = rel_m.src_domain;
            crel.tgt_domain_id = rel_m.tgt_domain;
            crel.encoding_type = rel_m.encoding;
            crel.node_count = node_count;
            crel.edge_count = edge_count;
            crel.section_features = sec_flags;
            crel.row_offsets = std::move(row_offsets);
            crel.column_indices = std::move(col_indices);
            crel.encoded_col_indices = std::move(encoded_cols);

            compiled_relations.push_back(std::move(crel));
        }

        auto t_ser_0 = std::chrono::high_resolution_clock::now();

        // Build Payload (Section 2 Domain Catalog + Directory Table + Section 3 Arrays)
        std::vector<uint8_t> payload;

        // Section 2 Part A: Domain Catalog
        for (const auto& dom : manifest.domains) {
            impulse_domain_catalog_entry_header_t dom_hdr;
            dom_hdr.domain_id = dom.id;
            dom_hdr.key_type = dom.key_type;
            dom_hdr.name_len = static_cast<uint16_t>(dom.name.size());

            const uint8_t* hdr_ptr = reinterpret_cast<const uint8_t*>(&dom_hdr);
            payload.insert(payload.end(), hdr_ptr, hdr_ptr + sizeof(dom_hdr));
            payload.insert(payload.end(), dom.name.begin(), dom.name.end());
        }
        align64(payload);

        // Section 2 Part B: Relation Directory Table
        size_t directory_start_offset = payload.size();
        std::vector<impulse_relation_directory_entry_t> dir_table(compiled_relations.size());

        size_t dir_bytes = compiled_relations.size() * sizeof(impulse_relation_directory_entry_t);
        payload.insert(payload.end(), dir_bytes, 0x00);
        align64(payload);

        // Append CSR Arrays & Fill Pointers
        uint64_t base_file_offset = 4096;

        for (size_t i = 0; i < compiled_relations.size(); ++i) {
            const auto& crel = compiled_relations[i];
            auto& entry = dir_table[i];

            entry.src_domain_id = crel.src_domain_id;
            entry.tgt_domain_id = crel.tgt_domain_id;
            entry.encoding_type = crel.encoding_type;
            entry.node_count = crel.node_count;
            entry.edge_count = crel.edge_count;
            entry.section_features = crel.section_features;

            // RowOffsets Array
            align64(payload);
            entry.csr_row_off_offset = base_file_offset + payload.size();
            entry.csr_row_off_bytes = crel.row_offsets.size() * sizeof(uint32_t);

            const uint8_t* row_ptr = reinterpret_cast<const uint8_t*>(crel.row_offsets.data());
            payload.insert(payload.end(), row_ptr, row_ptr + entry.csr_row_off_bytes);

            // ColumnIndices Stream
            align64(payload);
            entry.csr_col_idx_offset = base_file_offset + payload.size();
            entry.csr_col_idx_bytes = crel.encoded_col_indices.size();

            payload.insert(payload.end(), crel.encoded_col_indices.begin(), crel.encoded_col_indices.end());

            entry.id_map_offset = 0;
            entry.id_map_bytes = 0;
            entry.dto_lookup_offset = 0;
            entry.dto_lookup_bytes = 0;
            entry.delta_log_offset = 0;
            entry.delta_log_bytes = 0;
        }

        std::memcpy(payload.data() + directory_start_offset, dir_table.data(), dir_bytes);
        align4096(payload);

        // Compute SHA-256 Payload Digest
        uint8_t payload_sha256[32];
        CC_SHA256(payload.data(), payload.size(), payload_sha256);

        // Section 1: Snapshot Header (4096 Bytes)
        impulse_snapshot_header_t header;
        std::memset(&header, 0x00, sizeof(header));
        header.magic = IMPULSE_MAGIC;
        header.version = IMPULSE_VERSION_MAJOR;
        header.data_offset = IMPULSE_DEFAULT_DATA_OFFSET;
        header.domain_count = static_cast<uint16_t>(manifest.domains.size());
        header.relation_count = static_cast<uint16_t>(manifest.relations.size());
        header.kafka_offset = 0;
        header.timestamp_ms = static_cast<uint64_t>(std::time(nullptr) * 1000ULL);
        std::memcpy(header.sha256_checksum, payload_sha256, 32);
        header.global_required_features = manifest.global_features;

        // Write Final Binary Output
        std::ofstream ofs(output_imps_path, std::ios::binary);
        if (!ofs.is_open()) {
            std::cerr << "[!] Error creating output snapshot file: " << output_imps_path << std::endl;
            return false;
        }

        ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
        ofs.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        ofs.close();

        auto t_ser_1 = std::chrono::high_resolution_clock::now();
        double ser_ms = std::chrono::duration<double, std::milli>(t_ser_1 - t_ser_0).count();

        auto t_end_total = std::chrono::high_resolution_clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(t_end_total - t_start_total).count();

        uint64_t final_snapshot_bytes = sizeof(header) + payload.size();
        double total_m_edges_sec = (grand_total_edges / 1000000.0) / (total_ms / 1000.0);
        double total_mb_sec = (final_snapshot_bytes / 1024.0 / 1024.0) / (total_ms / 1000.0);
        double overall_ratio = (double)grand_total_raw_bytes / (double)grand_total_encoded_bytes;
        double overall_savings = (1.0 - (double)grand_total_encoded_bytes / (double)grand_total_raw_bytes) * 100.0;

        std::cout << "\n==========================================================================" << std::endl;
        std::cout << " COMPILATION & BENCHMARK PERFORMANCE REPORT" << std::endl;
        std::cout << "==========================================================================" << std::endl;
        std::cout << " Total Compiled File Size:   " << (final_snapshot_bytes / 1024.0 / 1024.0) << " MB (" << final_snapshot_bytes << " bytes)" << std::endl;
        std::cout << " Total Edges Processed:      " << grand_total_edges << " edges" << std::endl;
        std::cout << " Overall Compression Ratio:  " << std::fixed << std::setprecision(2) << overall_ratio << "x (" << overall_savings << "% space savings)" << std::endl;
        std::cout << " Compilation Throughput:     " << total_m_edges_sec << " M_edges/sec (" << total_mb_sec << " MB/sec)" << std::endl;
        std::cout << " Timing Breakdown:           " << std::endl;
        std::cout << "   - Parse & Key Resolution: " << total_parse_ms << " ms" << std::endl;
        std::cout << "   - CSR Index Building:     " << total_build_ms << " ms" << std::endl;
        std::cout << "   - Stream Encoding:        " << total_encode_ms << " ms" << std::endl;
        std::cout << "   - 4KB Disk Serialization: " << ser_ms << " ms" << std::endl;
        std::cout << "   - Total Execution Time:   " << total_ms << " ms" << std::endl;
        std::cout << "==========================================================================" << std::endl;

        return true;
    }
};

} // namespace impulse

#endif // IMPULSE_COMPILER_HPP
