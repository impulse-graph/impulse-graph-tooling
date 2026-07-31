package snapshot

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"os"

	"github.com/impulse-graph/impulse-cli/pkg/schema"
)

type VerificationResult struct {
	IsValid        bool
	SnapshotPath   string
	Header         schema.SnapshotHeader
	ExpectedSHA256 string
	ActualSHA256   string
	ByteSize       int64
	ErrorMessage   string
}

type BinaryDomainInfo struct {
	ID       uint16
	Name     string
	KeyType  schema.KeyType
	MapCount uint32
}

type BinaryRelationInfo struct {
	SrcDomainID uint16
	TgtDomainID uint16
	NodeCount   uint32
	EdgeCount   uint64
	RowOffsets  []uint32
	SampleEdges []uint32
}

type BinarySnapshotDump struct {
	Header       schema.SnapshotHeader
	ByteSize     int64
	SHA256Hex    string
	IsValid      bool
	Domains      []BinaryDomainInfo
	Relations    []BinaryRelationInfo
}

func VerifySnapshot(filePath string) (*VerificationResult, error) {
	data, err := os.ReadFile(filePath)
	if err != nil {
		return nil, fmt.Errorf("failed to read snapshot file: %w", err)
	}

	if len(data) < 58 {
		return &VerificationResult{
			IsValid:      false,
			SnapshotPath: filePath,
			ByteSize:     int64(len(data)),
			ErrorMessage: "file smaller than minimum 58-byte header",
		}, nil
	}

	hdr := schema.DeserializeHeader(data[:58])

	if hdr.Magic != schema.SnapshotMagic {
		return &VerificationResult{
			IsValid:      false,
			SnapshotPath: filePath,
			Header:       hdr,
			ByteSize:     int64(len(data)),
			ErrorMessage: fmt.Sprintf("invalid magic bytes 0x%X (expected 0x%X)", hdr.Magic, schema.SnapshotMagic),
		}, nil
	}

	payload := data[hdr.DataOffset:]
	computedHash := sha256.Sum256(payload)
	expectedHex := hex.EncodeToString(hdr.SHA256[:])
	actualHex := hex.EncodeToString(computedHash[:])

	isValid := expectedHex == actualHex
	errMsg := ""
	if !isValid {
		errMsg = fmt.Sprintf("SHA256 checksum mismatch: expected %s, got %s", expectedHex, actualHex)
	}

	return &VerificationResult{
		IsValid:        isValid,
		SnapshotPath:   filePath,
		Header:         hdr,
		ExpectedSHA256: expectedHex,
		ActualSHA256:   actualHex,
		ByteSize:       int64(len(data)),
		ErrorMessage:   errMsg,
	}, nil
}

func ReadAndDumpSnapshotBinary(filePath string) (*BinarySnapshotDump, error) {
	data, err := os.ReadFile(filePath)
	if err != nil {
		return nil, fmt.Errorf("failed to read file: %w", err)
	}

	if len(data) < 58 {
		return nil, fmt.Errorf("file size %d bytes is smaller than 58-byte header", len(data))
	}

	hdr := schema.DeserializeHeader(data)
	if hdr.Magic != schema.SnapshotMagic {
		return nil, fmt.Errorf("invalid magic 0x%X", hdr.Magic)
	}

	payload := data[hdr.DataOffset:]
	computedHash := sha256.Sum256(payload)
	shaHex := hex.EncodeToString(computedHash[:])
	expectedHex := hex.EncodeToString(hdr.SHA256[:])

	dump := &BinarySnapshotDump{
		Header:    hdr,
		ByteSize:  int64(len(data)),
		SHA256Hex: shaHex,
		IsValid:   shaHex == expectedHex,
	}

	offset := int(hdr.DataOffset)

	// Parse Domains
	for i := 0; i < int(hdr.DomainCount); i++ {
		if offset+5 > len(data) {
			break
		}
		domID := binary.LittleEndian.Uint16(data[offset : offset+2])
		keyType := schema.KeyType(data[offset+2])
		nameLen := int(binary.LittleEndian.Uint16(data[offset+3 : offset+5]))
		offset += 5

		if offset+nameLen+4 > len(data) {
			break
		}
		domName := string(data[offset : offset+nameLen])
		offset += nameLen

		mapCount := binary.LittleEndian.Uint32(data[offset : offset+4])
		offset += 4

		// Skip mapping entries
		for m := 0; m < int(mapCount); m++ {
			if offset+6 > len(data) {
				break
			}
			offset += 4 // denseId
			bkLen := int(binary.LittleEndian.Uint16(data[offset : offset+2]))
			offset += 2 + bkLen
		}

		dump.Domains = append(dump.Domains, BinaryDomainInfo{
			ID:       domID,
			Name:     domName,
			KeyType:  keyType,
			MapCount: mapCount,
		})
	}

	// Parse Relations (CSR Matrices)
	for j := 0; j < int(hdr.RelationCount); j++ {
		headerSize := 32
		if hdr.Version >= 2 {
			headerSize = 33
		}
		if offset+headerSize > len(data) {
			break
		}

		srcDomID := binary.LittleEndian.Uint16(data[offset : offset+2])
		tgtDomID := binary.LittleEndian.Uint16(data[offset+2 : offset+4])

		relOffset := offset + 4
		if hdr.Version >= 2 {
			relOffset++ // skip EncodingType byte
		}

		nodeCount := binary.LittleEndian.Uint32(data[relOffset : relOffset+4])
		edgeCount := binary.LittleEndian.Uint64(data[relOffset+4 : relOffset+12])
		rowOffBytes := binary.LittleEndian.Uint64(data[relOffset+12 : relOffset+20])
		colIdxBytes := binary.LittleEndian.Uint64(data[relOffset+20 : relOffset+28])
		offset += headerSize

		relInfo := BinaryRelationInfo{
			SrcDomainID: srcDomID,
			TgtDomainID: tgtDomID,
			NodeCount:   nodeCount,
			EdgeCount:   edgeCount,
		}

		// Read sample rowOffsets
		if offset+int(rowOffBytes) <= len(data) {
			rowOffEnd := offset + int(rowOffBytes)
			sampleCount := int(rowOffBytes / 4)
			if sampleCount > 10 {
				sampleCount = 10
			}
			for r := 0; r < sampleCount; r++ {
				relInfo.RowOffsets = append(relInfo.RowOffsets, binary.LittleEndian.Uint32(data[offset+r*4:offset+(r+1)*4]))
			}
			offset = rowOffEnd
		}

		// Skip columnIndices in dump detail
		if offset+int(colIdxBytes) <= len(data) {
			offset += int(colIdxBytes)
		}

		dump.Relations = append(dump.Relations, relInfo)
	}

	return dump, nil
}
