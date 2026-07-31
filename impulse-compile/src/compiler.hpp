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

    static CompilerManifest parse_simple_manifest(const std::string& manifest_path) {
        CompilerManifest manifest;
        std::ifstream ifs(manifest_path);
        if (!ifs.is_open()) {
            throw std::runtime_error("Could not open manifest file: " + manifest_path);
        }

        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        
        // Lightweight parsing fallback for manifest.json
        // Domains
        size_t pos = 0;
        uint16_t next_domain_id = 0;

        // Simple line parser if text/json
        std::istringstream iss(content);
        std::string line;
        bool in_domains = false;
        bool in_relations = false;

        while (std::getline(iss, line)) {
            if (line.find("\"domains\"") != std::string::npos) { in_domains = true; in_relations = false; }
            if (line.find("\"relations\"") != std::string::npos) { in_relations = true; in_domains = false; }

            if (in_domains && line.find("\"name\"") != std::string::npos) {
                DomainManifest dom;
                dom.id = next_domain_id++;
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
                rel.encoding = IMPULSE_ENC_DELTA_VBYTE;
                rel.section_features = IMPULSE_RELATION_FEAT_ENC_DELTA_VBYTE;

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

    static bool compile_directory(const std::string& input_dir, const std::string& output_imps_path) {
        std::string manifest_path = (fs::path(input_dir) / "manifest.json").string();
        std::cout << "==========================================================================" << std::endl;
        std::cout << " IMPULSE-COMPILE: C++20 BINARY SNAPSHOT COMPILER (Spec v2.4.0 4KB Aligned)" << std::endl;
        std::cout << "==========================================================================" << std::endl;
        std::cout << " Input Directory:  " << input_dir << std::endl;
        std::cout << " Manifest File:   " << manifest_path << std::endl;
        std::cout << " Output .imps:     " << output_imps_path << std::endl;

        CompilerManifest manifest = parse_simple_manifest(manifest_path);

        std::cout << " Found " << manifest.domains.size() << " domains and " 
                  << manifest.relations.size() << " relation manifests." << std::endl;

        std::vector<CompiledRelation> compiled_relations;

        // Process Relations
        for (const auto& rel_m : manifest.relations) {
            fs::path edge_path = fs::path(input_dir) / rel_m.filename;
            std::cout << "\n Compiling Relation: " << rel_m.filename << " (File: " << edge_path.string() << ")" << std::endl;

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

            uint64_t node_count = src_key_map.size();
            uint64_t edge_count = edges.size();

            std::cout << "   - Nodes: " << node_count << " | Edges: " << edge_count << std::endl;

            // Sort edges by src_id, then tgt_id
            std::sort(edges.begin(), edges.end(), [](const RawEdge& a, const RawEdge& b) {
                if (a.src_id != b.src_id) return a.src_id < b.src_id;
                return a.tgt_id < b.tgt_id;
            });

            // Build RowOffsets
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

            // Encode ColumnIndices stream
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
            } else {
                const uint8_t* ptr = reinterpret_cast<const uint8_t*>(col_indices.data());
                encoded_cols.assign(ptr, ptr + col_indices.size() * sizeof(uint32_t));
            }

            CompiledRelation crel;
            crel.src_domain_id = rel_m.src_domain;
            crel.tgt_domain_id = rel_m.tgt_domain;
            crel.encoding_type = rel_m.encoding;
            crel.node_count = node_count;
            crel.edge_count = edge_count;
            crel.section_features = rel_m.section_features | (1ULL << rel_m.encoding);
            crel.row_offsets = std::move(row_offsets);
            crel.column_indices = std::move(col_indices);
            crel.encoded_col_indices = std::move(encoded_cols);

            compiled_relations.push_back(std::move(crel));
        }

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

        // Reserve space for directory entries in payload
        size_t dir_bytes = compiled_relations.size() * sizeof(impulse_relation_directory_entry_t);
        payload.insert(payload.end(), dir_bytes, 0x00);
        align64(payload);

        // Append CSR Arrays & Fill Pointers
        uint64_t base_file_offset = 4096; // DataOffset = 4096

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

        // Copy populated directory entries back into payload
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

        std::cout << "\n [✓] Successfully compiled .imps binary snapshot!" << std::endl;
        std::cout << "     File: " << output_imps_path << " (" << (sizeof(header) + payload.size()) << " total bytes)" << std::endl;
        std::cout << "==========================================================================" << std::endl;

        return true;
    }
};

} // namespace impulse

#endif // IMPULSE_COMPILER_HPP
