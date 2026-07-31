package schema

import "encoding/binary"

// Magic bytes: "IMPS" (0x494D5053)
const SnapshotMagic uint32 = 0x494D5053
const SnapshotVersion uint16 = 2

// Key Types
type KeyType uint8

const (
	KeyTypeInt16  KeyType = 1
	KeyTypeInt32  KeyType = 2
	KeyTypeInt64  KeyType = 3
	KeyTypeUUID   KeyType = 4
	KeyTypeString KeyType = 5
)

// Relation Encoding Types
type EncodingType uint8

const (
	EncodingRawUint32           EncodingType = 0x00
	EncodingDeltaVbyte          EncodingType = 0x01
	EncodingRawUint16           EncodingType = 0x02
	EncodingHybridUint1632      EncodingType = 0x03
	EncodingSimdcompBitpacked    EncodingType = 0x04
)

func ParseKeyType(s string) KeyType {
	switch s {
	case "INT16":
		return KeyTypeInt16
	case "INT32":
		return KeyTypeInt32
	case "INT64":
		return KeyTypeInt64
	case "UUID":
		return KeyTypeUUID
	case "STRING":
		return KeyTypeString
	default:
		return KeyTypeString
	}
}

func (k KeyType) String() string {
	switch k {
	case KeyTypeInt16:
		return "INT16"
	case KeyTypeInt32:
		return "INT32"
	case KeyTypeInt64:
		return "INT64"
	case KeyTypeUUID:
		return "UUID"
	case KeyTypeString:
		return "STRING"
	default:
		return "UNKNOWN"
	}
}

// C-ABI Extensible Header (4KB Page Aligned in v2.4)
type SnapshotHeader struct {
	Magic                  uint32
	Version                uint16
	DataOffset             uint32
	DomainCount            uint16
	RelationCount          uint16
	KafkaOffset            uint64
	TimestampMs            uint64
	SHA256                 [32]byte
	GlobalRequiredFeatures uint64
}

func (h *SnapshotHeader) Serialize(buf []byte) {
	binary.LittleEndian.PutUint32(buf[0:4], h.Magic)
	binary.LittleEndian.PutUint16(buf[4:6], h.Version)
	binary.LittleEndian.PutUint32(buf[6:10], h.DataOffset)
	binary.LittleEndian.PutUint16(buf[10:12], h.DomainCount)
	binary.LittleEndian.PutUint16(buf[12:14], h.RelationCount)
	binary.LittleEndian.PutUint64(buf[14:22], h.KafkaOffset)
	binary.LittleEndian.PutUint64(buf[22:30], h.TimestampMs)
	copy(buf[30:62], h.SHA256[:])
	buf[62] = 0
	buf[63] = 0
	if len(buf) >= 72 {
		binary.LittleEndian.PutUint64(buf[64:72], h.GlobalRequiredFeatures)
	}
}

func DeserializeHeader(buf []byte) SnapshotHeader {
	var h SnapshotHeader
	h.Magic = binary.LittleEndian.Uint32(buf[0:4])
	h.Version = binary.LittleEndian.Uint16(buf[4:6])

	if h.Version >= 2 && len(buf) >= 64 {
		h.DataOffset = binary.LittleEndian.Uint32(buf[6:10])
		h.DomainCount = binary.LittleEndian.Uint16(buf[10:12])
		h.RelationCount = binary.LittleEndian.Uint16(buf[12:14])
		h.KafkaOffset = binary.LittleEndian.Uint64(buf[14:22])
		h.TimestampMs = binary.LittleEndian.Uint64(buf[22:30])
		copy(h.SHA256[:], buf[30:62])

		if len(buf) >= 72 {
			h.GlobalRequiredFeatures = binary.LittleEndian.Uint64(buf[64:72])
		}
	} else {
		h.DataOffset = 58
		h.DomainCount = binary.LittleEndian.Uint16(buf[6:8])
		h.RelationCount = binary.LittleEndian.Uint16(buf[8:10])
		h.KafkaOffset = binary.LittleEndian.Uint64(buf[10:18])
		h.TimestampMs = binary.LittleEndian.Uint64(buf[18:26])
		copy(h.SHA256[:], buf[26:58])
	}
	return h
}

func FormatGlobalFeatures(flags uint64) string {
	names := []string{}
	if flags&(1<<0) != 0 {
		names = append(names, "GLOBAL_FEAT_64BIT_NODES")
	}
	if flags&(1<<1) != 0 {
		names = append(names, "GLOBAL_FEAT_ZSTD_DICT_EMBEDDED")
	}
	if flags&(1<<2) != 0 {
		names = append(names, "GLOBAL_FEAT_DELTA_LOG_PRESENT")
	}
	if flags&(1<<3) != 0 {
		names = append(names, "GLOBAL_FEAT_4KB_PAGE_ALIGNED")
	}
	return formatFlags(flags, names)
}

func FormatSectionFeatures(flags uint64) string {
	names := []string{}
	if flags&(1<<0) != 0 {
		names = append(names, "RELATION_FEAT_ENC_RAW_UINT32")
	}
	if flags&(1<<1) != 0 {
		names = append(names, "RELATION_FEAT_ENC_DELTA_VBYTE")
	}
	if flags&(1<<2) != 0 {
		names = append(names, "RELATION_FEAT_ENC_RAW_UINT16")
	}
	if flags&(1<<3) != 0 {
		names = append(names, "RELATION_FEAT_ENC_HYBRID_16_32")
	}
	if flags&(1<<4) != 0 {
		names = append(names, "RELATION_FEAT_ENC_SIMDCOMP")
	}
	if flags&(1<<5) != 0 {
		names = append(names, "RELATION_FEAT_ENC_SLICED_ELLPACK")
	}
	if flags&(1<<6) != 0 {
		names = append(names, "RELATION_FEAT_ENC_TPU_BCOO")
	}
	if flags&(1<<7) != 0 {
		names = append(names, "RELATION_FEAT_ENC_RAW_UINT64")
	}
	if flags&(1<<8) != 0 {
		names = append(names, "RELATION_FEAT_ENC_ROARING_BITMAP")
	}
	if flags&(1<<16) != 0 {
		names = append(names, "RELATION_FEAT_WEIGHTED_EDGES")
	}
	if flags&(1<<17) != 0 {
		names = append(names, "RELATION_FEAT_KV_LABELS")
	}
	if flags&(1<<18) != 0 {
		names = append(names, "RELATION_FEAT_DTO_EDGE_ANNOTATIONS")
	}
	if flags&(1<<19) != 0 {
		names = append(names, "RELATION_FEAT_TEMPORAL_TIMESTAMPS")
	}
	if flags&(1<<20) != 0 {
		names = append(names, "RELATION_FEAT_PER_SECTION_ZSTD")
	}
	if flags&(1<<21) != 0 {
		names = append(names, "RELATION_FEAT_INCOMING_CSR_INDEX")
	}
	return formatFlags(flags, names)
}

func formatFlags(flags uint64, names []string) string {
	strNames := ""
	for i, n := range names {
		if i > 0 {
			strNames += ", "
		}
		strNames += n
	}
	return fmt.Sprintf("0x%016X [%s]", flags, strNames)
}
