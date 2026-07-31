package parser

import (
	"bufio"
	"fmt"
	"io"
	"strconv"
	"strings"

	"github.com/impulse-graph/impulse-cli/pkg/schema"
)

type OpType string

const (
	OpRegisterDomain   OpType = "REGISTER_DOMAIN"
	OpRegisterRelation OpType = "REGISTER_RELATION"
	OpInsertNode       OpType = "INSERT_NODE"
	OpDeleteNode       OpType = "DELETE_NODE"
	OpInsertEdge       OpType = "INSERT_EDGE"
	OpDeleteEdge       OpType = "DELETE_EDGE"
	OpCheckpoint       OpType = "CHECKPOINT"
)

type Operation struct {
	Type           OpType
	LineNum        int
	DomainName     string
	DomainID       uint16
	KeyType        schema.KeyType
	RelationName   string
	SrcDomain      string
	TgtDomain      string
	IsBiDirection  bool
	BusinessKey    string
	SrcKey         string
	TgtKey         string
	SnapshotID     string
	KafkaOffset    uint64
}

func ParseTSV(r io.Reader) ([]Operation, error) {
	scanner := bufio.NewScanner(r)
	buf := make([]byte, 1024*1024)
	scanner.Buffer(buf, 10*1024*1024)
	var ops []Operation
	lineNum := 0

	for scanner.Scan() {
		lineNum++
		line := strings.TrimRight(scanner.Text(), "\r\n")

		// Skip comments and blank lines
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		parts := strings.Split(line, "\t")
		if len(parts) == 0 {
			continue
		}

		opCode := OpType(strings.TrimSpace(parts[0]))
		op := Operation{Type: opCode, LineNum: lineNum}

		switch opCode {
		case OpRegisterDomain:
			if len(parts) < 4 {
				return nil, fmt.Errorf("line %d: REGISTER_DOMAIN requires 3 args (name, id, key_type)", lineNum)
			}
			op.DomainName = parts[1]
			id, err := strconv.ParseUint(parts[2], 10, 16)
			if err != nil {
				return nil, fmt.Errorf("line %d: invalid domain_id '%s'", lineNum, parts[2])
			}
			op.DomainID = uint16(id)
			op.KeyType = schema.ParseKeyType(parts[3])

		case OpRegisterRelation:
			if len(parts) < 5 {
				return nil, fmt.Errorf("line %d: REGISTER_RELATION requires 4 args (name, src, tgt, bidirectional)", lineNum)
			}
			op.RelationName = parts[1]
			op.SrcDomain = parts[2]
			op.TgtDomain = parts[3]
			op.IsBiDirection = strings.ToLower(parts[4]) == "true" || parts[4] == "1"

		case OpInsertNode:
			if len(parts) < 3 {
				return nil, fmt.Errorf("line %d: INSERT_NODE requires 2 args (domain, key)", lineNum)
			}
			op.DomainName = parts[1]
			op.BusinessKey = parts[2]

		case OpDeleteNode:
			if len(parts) < 3 {
				return nil, fmt.Errorf("line %d: DELETE_NODE requires 2 args (domain, key)", lineNum)
			}
			op.DomainName = parts[1]
			op.BusinessKey = parts[2]

		case OpInsertEdge:
			if len(parts) < 4 {
				return nil, fmt.Errorf("line %d: INSERT_EDGE requires 3 args (relation, src_key, tgt_key)", lineNum)
			}
			op.RelationName = parts[1]
			op.SrcKey = parts[2]
			op.TgtKey = parts[3]

		case OpDeleteEdge:
			if len(parts) < 4 {
				return nil, fmt.Errorf("line %d: DELETE_EDGE requires 3 args (relation, src_key, tgt_key)", lineNum)
			}
			op.RelationName = parts[1]
			op.SrcKey = parts[2]
			op.TgtKey = parts[3]

		case OpCheckpoint:
			if len(parts) < 3 {
				return nil, fmt.Errorf("line %d: CHECKPOINT requires 2 args (snapshot_id, kafka_offset)", lineNum)
			}
			op.SnapshotID = parts[1]
			offset, err := strconv.ParseUint(parts[2], 10, 64)
			if err != nil {
				return nil, fmt.Errorf("line %d: invalid kafka_offset '%s'", lineNum, parts[2])
			}
			op.KafkaOffset = offset

		default:
			return nil, fmt.Errorf("line %d: unknown opcode '%s'", lineNum, opCode)
		}

		ops = append(ops, op)
	}

	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return ops, nil
}
