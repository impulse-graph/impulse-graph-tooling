#include "impulse_opt/optimizer.hpp"
#include "impulse_opt/codecs.hpp"
#include "impulse_graph.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <CommonCrypto/CommonDigest.h>

namespace impulse::opt {

ImpulseOptimizer::ImpulseOptimizer(OptimizerOptions options)
    : options_(std::move(options)) {}

bool ImpulseOptimizer::run() {
    auto t_start_total = std::chrono::high_resolution_clock::now();

    std::ifstream ifs(options_.input_path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        std::cerr << "[!] Error opening input snapshot file: " << options_.input_path << std::endl;
        return false;
    }

    size_t file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    if (file_size < 58) {
        std::cerr << "[!] Input snapshot file too small: " << file_size << " bytes" << std::endl;
        return false;
    }

    std::vector<uint8_t> buffer(file_size);
    if (!ifs.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        std::cerr << "[!] Error reading input snapshot file" << std::endl;
        return false;
    }
    ifs.close();

    const auto* hdr = reinterpret_cast<const impulse_snapshot_header_t*>(buffer.data());
    if (hdr->magic != IMPULSE_MAGIC) {
        std::cerr << "[!] Invalid magic bytes: 0x" << std::hex << hdr->magic << std::dec << std::endl;
        return false;
    }

    size_t data_offset = hdr->data_offset;
    if (hdr->version < 2 || data_offset == 0) data_offset = 64;

    uint8_t computed_sha256[32];
    CC_SHA256(buffer.data() + data_offset, file_size - data_offset, computed_sha256);
    if (std::memcmp(hdr->sha256_checksum, computed_sha256, 32) != 0) {
        std::cerr << "[!] Input SHA256 checksum mismatch! File may be corrupt." << std::endl;
        return false;
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

        std::vector<uint32_t> row_offsets(node_count + 2, 0);
        if (row_off_offset + row_off_bytes <= file_size) {
            std::memcpy(row_offsets.data(), buffer.data() + row_off_offset, row_off_bytes);
        }

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
            if (col_idx_offset + edge_count * 4 <= file_size) {
                column_indices.resize(edge_count);
                std::memcpy(column_indices.data(), buffer.data() + col_idx_offset, edge_count * 4);
            }
        }

        if (options_.enable_rcm_reorder && node_count > 0 && edge_count > 0) {
            for (size_t node = 0; node <= node_count; ++node) {
                uint32_t start = row_offsets[node];
                uint32_t end = row_offsets[node + 1];
                if (start < end && end <= column_indices.size()) {
                    std::sort(column_indices.begin() + start, column_indices.begin() + end);
                }
            }
        }

        uint8_t out_enc = options_.override_target_encoding ? options_.target_encoding : enc_type;
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
    impulse_writer_t* writer = impulse_writer_create(options_.output_path.c_str(), hdr->global_required_features);
    if (!writer) {
        std::cerr << "[!] Error creating C-ABI snapshot writer: " << impulse_get_last_error() << std::endl;
        return false;
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
        return false;
    }

    auto t_end_total = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t_end_total - t_start_total).count();

    std::cout << "\n==========================================================================" << std::endl;
    std::cout << " [✓] SNAPSHOT ENCODING TRANSLATION & OPTIMIZATION COMPLETE (C-ABI Writer API)!" << std::endl;
    std::cout << " Output File:     " << options_.output_path << std::endl;
    std::cout << " Execution Time:  " << total_ms << " ms" << std::endl;
    std::cout << "==========================================================================" << std::endl;

    return true;
}

} // namespace impulse::opt
