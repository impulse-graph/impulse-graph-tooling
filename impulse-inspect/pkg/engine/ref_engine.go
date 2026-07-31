package engine

import (
	"fmt"
	"sort"

	"github.com/impulse-graph/impulse-cli/pkg/parser"
	"github.com/impulse-graph/impulse-cli/pkg/schema"
)

type DomainInfo struct {
	ID      uint16
	Name    string
	KeyType schema.KeyType
}

type IdMapper struct {
	DomainName string
	BkToDense  map[string]uint32
	DenseToBk  map[uint32]string
	NextID     uint32
}

func NewIdMapper(domainName string) *IdMapper {
	return &IdMapper{
		DomainName: domainName,
		BkToDense:  make(map[string]uint32),
		DenseToBk:  make(map[uint32]string),
		NextID:     0,
	}
}

func (m *IdMapper) GetOrAssign(bk string) uint32 {
	if id, exists := m.BkToDense[bk]; exists {
		return id
	}
	id := m.NextID
	m.NextID++
	m.BkToDense[bk] = id
	m.DenseToBk[id] = bk
	return id
}

type Edge struct {
	Src uint32
	Tgt uint32
}

type RelationInfo struct {
	Name            string
	SrcDomain       string
	TgtDomain       string
	IsBiDirectional bool
	Edges           map[Edge]bool
}

type RefGraphEngine struct {
	Domains     map[string]*DomainInfo
	Mappers     map[string]*IdMapper
	Relations   map[string]*RelationInfo
	KafkaOffset uint64
}

func NewRefGraphEngine() *RefGraphEngine {
	return &RefGraphEngine{
		Domains:   make(map[string]*DomainInfo),
		Mappers:   make(map[string]*IdMapper),
		Relations: make(map[string]*RelationInfo),
	}
}

func (e *RefGraphEngine) ApplyOperations(ops []parser.Operation) error {
	for _, op := range ops {
		switch op.Type {
		case parser.OpRegisterDomain:
			e.Domains[op.DomainName] = &DomainInfo{
				ID:      op.DomainID,
				Name:    op.DomainName,
				KeyType: op.KeyType,
			}
			if _, exists := e.Mappers[op.DomainName]; !exists {
				e.Mappers[op.DomainName] = NewIdMapper(op.DomainName)
			}

		case parser.OpRegisterRelation:
			e.Relations[op.RelationName] = &RelationInfo{
				Name:            op.RelationName,
				SrcDomain:       op.SrcDomain,
				TgtDomain:       op.TgtDomain,
				IsBiDirectional: op.IsBiDirection,
				Edges:           make(map[Edge]bool),
			}
			if op.IsBiDirection {
				revName := fmt.Sprintf("%s_rev", op.RelationName)
				if _, exists := e.Relations[revName]; !exists {
					e.Relations[revName] = &RelationInfo{
						Name:            revName,
						SrcDomain:       op.TgtDomain,
						TgtDomain:       op.SrcDomain,
						IsBiDirectional: false,
						Edges:           make(map[Edge]bool),
					}
				}
			}

		case parser.OpInsertNode:
			mapper := e.Mappers[op.DomainName]
			if mapper == nil {
				return fmt.Errorf("line %d: domain '%s' not registered", op.LineNum, op.DomainName)
			}
			mapper.GetOrAssign(op.BusinessKey)

		case parser.OpDeleteNode:
			mapper := e.Mappers[op.DomainName]
			if mapper != nil {
				if denseId, exists := mapper.BkToDense[op.BusinessKey]; exists {
					delete(mapper.BkToDense, op.BusinessKey)
					delete(mapper.DenseToBk, denseId)

					// Cascading delete: Purge all edges involving this denseId
					for _, rel := range e.Relations {
						if rel.SrcDomain == op.DomainName || rel.TgtDomain == op.DomainName {
							for edge := range rel.Edges {
								if (rel.SrcDomain == op.DomainName && edge.Src == denseId) ||
									(rel.TgtDomain == op.DomainName && edge.Tgt == denseId) {
									delete(rel.Edges, edge)
								}
							}
						}
					}
				}
			}

		case parser.OpInsertEdge:
			rel, exists := e.Relations[op.RelationName]
			if !exists {
				return fmt.Errorf("line %d: relation '%s' not registered", op.LineNum, op.RelationName)
			}
			srcMapper := e.Mappers[rel.SrcDomain]
			tgtMapper := e.Mappers[rel.TgtDomain]
			if srcMapper == nil || tgtMapper == nil {
				return fmt.Errorf("line %d: missing domain mapper for relation '%s'", op.LineNum, op.RelationName)
			}

			srcId := srcMapper.GetOrAssign(op.SrcKey)
			tgtId := tgtMapper.GetOrAssign(op.TgtKey)
			rel.Edges[Edge{Src: srcId, Tgt: tgtId}] = true

			if rel.IsBiDirectional {
				revName := fmt.Sprintf("%s_rev", op.RelationName)
				if revRel, ok := e.Relations[revName]; ok {
					revRel.Edges[Edge{Src: tgtId, Tgt: srcId}] = true
				}
			}

		case parser.OpDeleteEdge:
			rel, exists := e.Relations[op.RelationName]
			if exists {
				srcMapper := e.Mappers[rel.SrcDomain]
				tgtMapper := e.Mappers[rel.TgtDomain]
				if srcMapper != nil && tgtMapper != nil {
					srcId, srcOk := srcMapper.BkToDense[op.SrcKey]
					tgtId, tgtOk := tgtMapper.BkToDense[op.TgtKey]
					if srcOk && tgtOk {
						delete(rel.Edges, Edge{Src: srcId, Tgt: tgtId})
						if rel.IsBiDirectional {
							revName := fmt.Sprintf("%s_rev", op.RelationName)
							if revRel, ok := e.Relations[revName]; ok {
								delete(revRel.Edges, Edge{Src: tgtId, Tgt: srcId})
							}
						}
					}
				}
			}

		case parser.OpCheckpoint:
			e.KafkaOffset = op.KafkaOffset
		}
	}
	return nil
}

// CanonicalCsr holds the contiguous binary CSR data for a single relation
type CanonicalCsr struct {
	RelationName string
	SrcDomain    string
	TgtDomain    string
	NodeCount    uint32
	EdgeCount    uint64
	RowOffsets   []uint32
	ColumnIndices []uint32
}

func (e *RefGraphEngine) BuildCanonicalCsr(relName string) (*CanonicalCsr, error) {
	rel, ok := e.Relations[relName]
	if !ok {
		return nil, fmt.Errorf("relation '%s' not found", relName)
	}

	srcMapper := e.Mappers[rel.SrcDomain]
	nodeCount := uint32(0)
	if srcMapper != nil {
		nodeCount = srcMapper.NextID
	}

	// Group edges by source node ID
	adj := make([][]uint32, nodeCount+1)
	for edge := range rel.Edges {
		if edge.Src < nodeCount+1 {
			adj[edge.Src] = append(adj[edge.Src], edge.Tgt)
		}
	}

	// Sort adjacency lists for determinism
	totalEdges := uint64(0)
	for i := range adj {
		sort.Slice(adj[i], func(a, b int) bool {
			return adj[i][a] < adj[i][b]
		})
		totalEdges += uint64(len(adj[i]))
	}

	rowOffsets := make([]uint32, nodeCount+2)
	columnIndices := make([]uint32, totalEdges)

	currOffset := uint32(0)
	for node := uint32(0); node <= nodeCount; node++ {
		rowOffsets[node] = currOffset
		for _, tgt := range adj[node] {
			columnIndices[currOffset] = tgt
			currOffset++
		}
	}
	rowOffsets[nodeCount+1] = currOffset

	return &CanonicalCsr{
		RelationName:  relName,
		SrcDomain:     rel.SrcDomain,
		TgtDomain:     rel.TgtDomain,
		NodeCount:     nodeCount,
		EdgeCount:     totalEdges,
		RowOffsets:    rowOffsets,
		ColumnIndices: columnIndices,
	}, nil
}
