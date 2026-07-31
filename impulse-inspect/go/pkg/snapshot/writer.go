package snapshot

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"os"
	"sort"

	"github.com/impulse-graph/impulse-cli/pkg/engine"
	"github.com/impulse-graph/impulse-cli/pkg/schema"
)

type WriteResult struct {
	FilePath     string
	ByteSize     int64
	SHA256Hex    string
	SHA256Bytes  [32]byte
	KafkaOffset  uint64
	DomainCount  int
	RelationCount int
}

func BuildAndWriteSnapshot(graph *engine.RefGraphEngine, outputPath string) (*WriteResult, error) {
	// 1. Sort domain names deterministically
	var domainNames []string
	for dName := range graph.Domains {
		domainNames = append(domainNames, dName)
	}
	sort.Strings(domainNames)

	// 2. Sort relation names deterministically
	var relationNames []string
	for rName := range graph.Relations {
		relationNames = append(relationNames, rName)
	}
	sort.Strings(relationNames)

	var payload []byte

	// Reserve 58 bytes for header
	headerBuf := make([]byte, 58)

	// Build Domain Section
	var domainBuf []byte
	for _, dName := range domainNames {
		dom := graph.Domains[dName]
		var entry [5]byte
		binary.LittleEndian.PutUint16(entry[0:2], dom.ID)
		entry[2] = byte(dom.KeyType)
		nameBytes := []byte(dom.Name)
		binary.LittleEndian.PutUint16(entry[3:5], uint16(len(nameBytes)))

		domainBuf = append(domainBuf, entry[:]...)
		domainBuf = append(domainBuf, nameBytes...)

		// Append IdMapper entries (sorted by BusinessKey)
		mapper := graph.Mappers[dName]
		if mapper != nil {
			var bks []string
			for bk := range mapper.BkToDense {
				bks = append(bks, bk)
			}
			sort.Strings(bks)

			var mapCountBuf [4]byte
			binary.LittleEndian.PutUint32(mapCountBuf[:], uint32(len(bks)))
			domainBuf = append(domainBuf, mapCountBuf[:]...)

			for _, bk := range bks {
				denseId := mapper.BkToDense[bk]
				bkBytes := []byte(bk)
				var mapEntry [6]byte
				binary.LittleEndian.PutUint32(mapEntry[0:4], denseId)
				binary.LittleEndian.PutUint16(mapEntry[4:6], uint16(len(bkBytes)))

				domainBuf = append(domainBuf, mapEntry[:]...)
				domainBuf = append(domainBuf, bkBytes...)
			}
		}
	}

	// Build Relation Section (CSR Matrices)
	var relBuf []byte
	for _, rName := range relationNames {
		csr, err := graph.BuildCanonicalCsr(rName)
		if err != nil {
			return nil, err
		}

		relInfo := graph.Relations[rName]
		srcDom := graph.Domains[relInfo.SrcDomain]
		tgtDom := graph.Domains[relInfo.TgtDomain]

		var rHead [32]byte
		if srcDom != nil {
			binary.LittleEndian.PutUint16(rHead[0:2], srcDom.ID)
		}
		if tgtDom != nil {
			binary.LittleEndian.PutUint16(rHead[2:4], tgtDom.ID)
		}
		binary.LittleEndian.PutUint32(rHead[4:8], csr.NodeCount)
		binary.LittleEndian.PutUint64(rHead[8:16], csr.EdgeCount)

		rowOffsetBytes := uint64(len(csr.RowOffsets) * 4)
		colIndexBytes := uint64(len(csr.ColumnIndices) * 4)

		binary.LittleEndian.PutUint64(rHead[16:24], rowOffsetBytes)
		binary.LittleEndian.PutUint64(rHead[24:32], colIndexBytes)

		relBuf = append(relBuf, rHead[:]...)

		// Append rowOffsets array (Little-Endian uint32)
		for _, offset := range csr.RowOffsets {
			var b [4]byte
			binary.LittleEndian.PutUint32(b[:], offset)
			relBuf = append(relBuf, b[:]...)
		}

		// Append columnIndices array (Little-Endian uint32)
		for _, col := range csr.ColumnIndices {
			var b [4]byte
			binary.LittleEndian.PutUint32(b[:], col)
			relBuf = append(relBuf, b[:]...)
		}
	}

	// Assemble complete payload (Domains + Relations) for SHA256 hashing
	payload = append(payload, domainBuf...)
	payload = append(payload, relBuf...)

	shaHash := sha256.Sum256(payload)

	hdr := schema.SnapshotHeader{
		Magic:         schema.SnapshotMagic,
		Version:       schema.SnapshotVersion,
		DomainCount:   uint16(len(domainNames)),
		RelationCount: uint16(len(relationNames)),
		KafkaOffset:   graph.KafkaOffset,
		TimestampMs:   0,
		SHA256:        shaHash,
	}

	hdr.Serialize(headerBuf)

	// Combine header + payload
	finalFileBytes := append(headerBuf, payload...)

	err := os.WriteFile(outputPath, finalFileBytes, 0644)
	if err != nil {
		return nil, fmt.Errorf("failed to write snapshot file: %w", err)
	}

	return &WriteResult{
		FilePath:      outputPath,
		ByteSize:      int64(len(finalFileBytes)),
		SHA256Hex:     hex.EncodeToString(shaHash[:]),
		SHA256Bytes:   shaHash,
		KafkaOffset:   graph.KafkaOffset,
		DomainCount:   len(domainNames),
		RelationCount: len(relationNames),
	}, nil
}
