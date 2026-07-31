package engine_test

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/impulse-graph/impulse-cli/pkg/engine"
	"github.com/impulse-graph/impulse-cli/pkg/parser"
	"github.com/impulse-graph/impulse-cli/pkg/snapshot"
)

func TestParseAndBuildSnapshotFixture(t *testing.T) {
	fixturePath := filepath.Join("..", "..", "testdata", "fixtures", "sample_rbac.tsv")
	file, err := os.Open(fixturePath)
	if err != nil {
		t.Fatalf("failed to open fixture file: %v", err)
	}
	defer file.Close()

	ops, err := parser.ParseTSV(file)
	if err != nil {
		t.Fatalf("failed to parse TSV fixture: %v", err)
	}

	if len(ops) == 0 {
		t.Fatalf("parsed 0 operations from TSV fixture")
	}

	refEngine := engine.NewRefGraphEngine()
	if err := refEngine.ApplyOperations(ops); err != nil {
		t.Fatalf("failed to apply operations: %v", err)
	}

	tmpOut := filepath.Join(t.TempDir(), "sample_rbac.bin")
	res, err := snapshot.BuildAndWriteSnapshot(refEngine, tmpOut)
	if err != nil {
		t.Fatalf("failed to build snapshot: %v", err)
	}

	if res.SHA256Hex == "" {
		t.Fatalf("expected non-empty SHA256 hex string")
	}

	if res.KafkaOffset != 5000 {
		t.Errorf("expected KafkaOffset 5000, got %d", res.KafkaOffset)
	}

	t.Logf("Snapshot Built Successfully!")
	t.Logf("  Path:       %s", res.FilePath)
	t.Logf("  Size:       %d bytes", res.ByteSize)
	t.Logf("  Domains:    %d", res.DomainCount)
	t.Logf("  Relations:  %d", res.RelationCount)
	t.Logf("  SHA256 Hash: %s", res.SHA256Hex)

	// Verify snapshot integrity
	verRes, err := snapshot.VerifySnapshot(tmpOut)
	if err != nil {
		t.Fatalf("failed to verify snapshot: %v", err)
	}

	if !verRes.IsValid {
		t.Fatalf("snapshot verification failed: %s", verRes.ErrorMessage)
	}
}
