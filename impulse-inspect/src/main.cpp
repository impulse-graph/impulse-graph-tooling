#include "impulse_graph.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <CommonCrypto/CommonDigest.h>

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

static std::string bytes_to_hex(const uint8_t* bytes, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
    }
    return oss.str();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <snapshot.imps|snapshot.bin>" << std::endl;
        return 1;
    }

    std::string file_path = argv[1];
    std::ifstream ifs(file_path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        std::cerr << "[!] Error opening snapshot file: " << file_path << std::endl;
        return 1;
    }

    size_t file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    if (file_size < 58) {
        std::cerr << "[!] Snapshot file too small: " << file_size << " bytes" << std::endl;
        return 1;
    }

    std::vector<uint8_t> buffer(file_size);
    if (!ifs.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        std::cerr << "[!] Error reading snapshot file" << std::endl;
        return 1;
    }
    ifs.close();

    uint32_t magic = *reinterpret_cast<const uint32_t*>(buffer.data());
    if (magic != IMPULSE_MAGIC) {
        std::cerr << "[!] Invalid magic bytes: 0x" << std::hex << magic << " (expected 0x" << IMPULSE_MAGIC << ")" << std::endl;
        return 1;
    }

    const auto* hdr = reinterpret_cast<const impulse_snapshot_header_t*>(buffer.data());
    uint32_t data_offset = hdr->data_offset;
    if (hdr->version < 2 || data_offset == 0) {
        data_offset = 64;
    }

    uint8_t computed_sha256[32];
    CC_SHA256(buffer.data() + data_offset, file_size - data_offset, computed_sha256);

    bool checksum_valid = (std::memcmp(hdr->sha256_checksum, computed_sha256, 32) == 0);

    std::cout << "==========================================================================" << std::endl;
    std::cout << " IMPULSE-INSPECT: BINARY SNAPSHOT HEADER & DIRECTORY INSPECTOR" << std::endl;
    std::cout << "==========================================================================" << std::endl;
    std::cout << " File Path:                " << file_path << std::endl;
    std::cout << " Validation Status:        " << (checksum_valid ? "VALID ✅" : "CHECKSUM MISMATCH ❌") << std::endl;
    std::cout << " File Byte Size:           " << file_size << " bytes (" << std::fixed << std::setprecision(2) << (file_size / 1024.0 / 1024.0) << " MB)" << std::endl;
    std::cout << " Magic Bytes:              0x" << std::hex << hdr->magic << " (\"IMPS\")" << std::dec << std::endl;
    std::cout << " Format Version:           " << hdr->version << std::endl;
    std::cout << " DataOffset (Header Size): " << data_offset << " bytes (" << (data_offset == 4096 ? "4KB Page Aligned" : "64B Baseline") << ")" << std::endl;
    std::cout << " Domain Count:             " << hdr->domain_count << std::endl;
    std::cout << " Relation Count:           " << hdr->relation_count << std::endl;
    std::cout << " Kafka Offset:             " << hdr->kafka_offset << std::endl;
    std::cout << " Timestamp (ms):           " << hdr->timestamp_ms << std::endl;
    std::cout << " Expected SHA256:          " << bytes_to_hex(hdr->sha256_checksum, 32) << std::endl;
    std::cout << " Computed SHA256:          " << bytes_to_hex(computed_sha256, 32) << std::endl;
    std::cout << " Global Required Features: " << format_global_features(hdr->global_required_features) << std::endl;

    // Unpack Domain Catalog
    std::cout << "\n--------------------------------------------------------------------------" << std::endl;
    std::cout << " SECTION 2 PART A: DOMAIN CATALOG (" << hdr->domain_count << " DOMAINS)" << std::endl;
    std::cout << "--------------------------------------------------------------------------" << std::endl;

    size_t offset = data_offset;
    for (uint16_t d = 0; d < hdr->domain_count; ++d) {
        if (offset + sizeof(impulse_domain_catalog_entry_header_t) > file_size) break;
        const auto* dom_hdr = reinterpret_cast<const impulse_domain_catalog_entry_header_t*>(buffer.data() + offset);
        offset += sizeof(impulse_domain_catalog_entry_header_t);

        std::string name(reinterpret_cast<const char*>(buffer.data() + offset), dom_hdr->name_len);
        offset += dom_hdr->name_len;

        std::cout << "  - Domain [" << d << "]: ID=" << dom_hdr->domain_id << ", KeyType=0x" << std::hex << (int)dom_hdr->key_type << std::dec 
                  << ", Name=\"" << name << "\"" << std::endl;
    }

    // Align 64 to reach Directory Table
    size_t rem64 = offset % 64;
    if (rem64 != 0) offset += (64 - rem64);

    // Unpack Relation Directory Table
    std::cout << "\n--------------------------------------------------------------------------" << std::endl;
    std::cout << " SECTION 2 PART B: RELATION DIRECTORY TABLE (" << hdr->relation_count << " RELATIONS)" << std::endl;
    std::cout << "--------------------------------------------------------------------------" << std::endl;

    for (uint16_t r = 0; r < hdr->relation_count; ++r) {
        if (data_offset == 4096) {
            if (offset + sizeof(impulse_relation_directory_entry_t) > file_size) break;
            const auto* entry = reinterpret_cast<const impulse_relation_directory_entry_t*>(buffer.data() + offset);
            offset += sizeof(impulse_relation_directory_entry_t);

            double avg_deg = (entry->node_count > 0) ? (double)entry->edge_count / (double)entry->node_count : 0.0;

            std::cout << "  - Relation [" << r << "]: SrcDomain=" << entry->src_domain_id << " -> TgtDomain=" << entry->tgt_domain_id << std::endl;
            std::cout << "      Encoding Type:       0x" << std::hex << (int)entry->encoding_type << std::dec << std::endl;
            std::cout << "      Section Features:    " << format_section_features(entry->section_features) << std::endl;
            std::cout << "      Matrix Scale:        N=" << entry->node_count << " nodes, E=" << entry->edge_count << " edges (Avg Deg: " << std::fixed << std::setprecision(2) << avg_deg << ")" << std::endl;
            std::cout << "      RowOffsets Stream:   Offset 0x" << std::hex << entry->csr_row_off_offset << " (" << std::dec << entry->csr_row_off_bytes << " bytes)" << std::endl;
            std::cout << "      ColumnIndices Stream: Offset 0x" << std::hex << entry->csr_col_idx_offset << " (" << std::dec << entry->csr_col_idx_bytes << " bytes)" << std::endl;
        } else {
            // Legacy / Baseline v2.3 format
            if (offset + 33 > file_size) break;
            uint16_t src_dom = *reinterpret_cast<const uint16_t*>(buffer.data() + offset); offset += 2;
            uint16_t tgt_dom = *reinterpret_cast<const uint16_t*>(buffer.data() + offset); offset += 2;
            uint8_t enc_type = buffer[offset++];
            uint32_t node_count = *reinterpret_cast<const uint32_t*>(buffer.data() + offset); offset += 4;
            uint64_t edge_count = *reinterpret_cast<const uint64_t*>(buffer.data() + offset); offset += 8;
            uint64_t row_off_bytes = *reinterpret_cast<const uint64_t*>(buffer.data() + offset); offset += 8;
            uint64_t col_idx_bytes = *reinterpret_cast<const uint64_t*>(buffer.data() + offset); offset += 8;

            std::cout << "  - Relation [" << r << "]: SrcDomain=" << src_dom << " -> TgtDomain=" << tgt_dom << std::endl;
            std::cout << "      Encoding Type:       0x" << std::hex << (int)enc_type << std::dec << std::endl;
            std::cout << "      Matrix Scale:        N=" << node_count << " nodes, E=" << edge_count << " edges" << std::endl;
            std::cout << "      RowOffsets Bytes:    " << row_off_bytes << " bytes" << std::endl;
            std::cout << "      ColumnIndices Bytes: " << col_idx_bytes << " bytes" << std::endl;
        }
    }

    std::cout << "==========================================================================" << std::endl;

    return 0;
}
