package main

import (
	"fmt"
	"os"
	"strings"

	"github.com/impulse-graph/impulse-cli/pkg/engine"
	"github.com/impulse-graph/impulse-cli/pkg/parser"
	"github.com/impulse-graph/impulse-cli/pkg/snapshot"
)

func main() {
	if len(os.Args) < 2 {
		printUsage()
		os.Exit(1)
	}

	command := os.Args[1]

	switch command {
	case "build":
		if len(os.Args) < 4 {
			fmt.Println("Usage: impulse-cli build <input.tsv> <output.bin>")
			os.Exit(1)
		}
		runBuild(os.Args[2], os.Args[3])

	case "verify":
		if len(os.Args) < 3 {
			fmt.Println("Usage: impulse-cli verify <snapshot.bin>")
			os.Exit(1)
		}
		runVerify(os.Args[2])

	case "dump":
		if len(os.Args) < 3 {
			fmt.Println("Usage: impulse-cli dump <input.tsv|snapshot.bin>")
			os.Exit(1)
		}
		runDump(os.Args[2])

	default:
		fmt.Printf("Unknown command: %s\n", command)
		printUsage()
		os.Exit(1)
	}
}

func printUsage() {
	fmt.Println("ImpulseGraph CLI & Reference Engine (Correctness Oracle)")
	fmt.Println("\nCommands:")
	fmt.Println("  build  <input.tsv> <output.bin>  Build deterministic binary snapshot from TSV test fixture")
	fmt.Println("  verify <snapshot.bin>            Verify magic bytes & SHA256 checksum of a binary snapshot")
	fmt.Println("  dump   <input.tsv|snapshot.bin>  Dump human-readable graph topology & metadata")
}

func runBuild(tsvPath, binPath string) {
	file, err := os.Open(tsvPath)
	if err != nil {
		fmt.Printf("Error opening TSV file: %v\n", err)
		os.Exit(1)
	}
	defer file.Close()

	ops, err := parser.ParseTSV(file)
	if err != nil {
		fmt.Printf("Error parsing TSV file: %v\n", err)
		os.Exit(1)
	}

	refEngine := engine.NewRefGraphEngine()
	if err := refEngine.ApplyOperations(ops); err != nil {
		fmt.Printf("Error applying operations: %v\n", err)
		os.Exit(1)
	}

	res, err := snapshot.BuildAndWriteSnapshot(refEngine, binPath)
	if err != nil {
		fmt.Printf("Error writing snapshot: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("[+] Snapshot Built Successfully!\n")
	fmt.Printf("    File:          %s\n", res.FilePath)
	fmt.Printf("    Size:          %d bytes\n", res.ByteSize)
	fmt.Printf("    Domains:       %d\n", res.DomainCount)
	fmt.Printf("    Relations:     %d\n", res.RelationCount)
	fmt.Printf("    Kafka Offset:  %d\n", res.KafkaOffset)
	fmt.Printf("    SHA256 Hash:   %s\n", res.SHA256Hex)
}

func runVerify(binPath string) {
	res, err := snapshot.VerifySnapshot(binPath)
	if err != nil {
		fmt.Printf("Error verifying snapshot: %v\n", err)
		os.Exit(1)
	}

	if !res.IsValid {
		fmt.Printf("[!] Snapshot Verification FAILED!\n")
		fmt.Printf("    File:          %s\n", res.SnapshotPath)
		fmt.Printf("    Error:         %s\n", res.ErrorMessage)
		os.Exit(1)
	}

	fmt.Printf("[+] Snapshot Verification PASSED!\n")
	fmt.Printf("    File:          %s\n", res.SnapshotPath)
	fmt.Printf("    Size:          %d bytes\n", res.ByteSize)
	fmt.Printf("    Magic:         0x%X\n", res.Header.Magic)
	fmt.Printf("    Version:       %d\n", res.Header.Version)
	fmt.Printf("    Domains:       %d\n", res.Header.DomainCount)
	fmt.Printf("    Relations:     %d\n", res.Header.RelationCount)
	fmt.Printf("    Kafka Offset:  %d\n", res.Header.KafkaOffset)
	fmt.Printf("    SHA256 Hash:   %s\n", res.ActualSHA256)
}

func runDump(inputPath string) {
	if strings.HasSuffix(inputPath, ".bin") {
		runDumpBinary(inputPath)
		return
	}

	file, err := os.Open(inputPath)
	if err != nil {
		fmt.Printf("Error opening file: %v\n", err)
		os.Exit(1)
	}
	defer file.Close()

	ops, err := parser.ParseTSV(file)
	if err != nil {
		// If TSV parsing fails, try parsing as binary snapshot
		runDumpBinary(inputPath)
		return
	}

	refEngine := engine.NewRefGraphEngine()
	if err := refEngine.ApplyOperations(ops); err != nil {
		fmt.Printf("Error applying operations: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("=== Graph TSV Topology Summary ===\n")
	fmt.Printf("Registered Domains (%d):\n", len(refEngine.Domains))
	for name, dom := range refEngine.Domains {
		mapper := refEngine.Mappers[name]
		nNodes := 0
		if mapper != nil {
			nNodes = len(mapper.BkToDense)
		}
		fmt.Printf("  - %s (ID=%d, KeyType=%s, ActiveNodes=%d)\n", dom.Name, dom.ID, dom.KeyType, nNodes)
	}

	fmt.Printf("\nRegistered Relations (%d):\n", len(refEngine.Relations))
	for name, rel := range refEngine.Relations {
		csr, _ := refEngine.BuildCanonicalCsr(name)
		nEdges := uint64(0)
		if csr != nil {
			nEdges = csr.EdgeCount
		}
		fmt.Printf("  - %s (%s -> %s, TotalEdges=%d, BiDirectional=%t)\n",
			rel.Name, rel.SrcDomain, rel.TgtDomain, nEdges, rel.IsBiDirectional)
	}
}

func runDumpBinary(binPath string) {
	dump, err := snapshot.ReadAndDumpSnapshotBinary(binPath)
	if err != nil {
		fmt.Printf("Error dumping binary snapshot: %v\n", err)
		os.Exit(1)
	}

	status := "VALID ✅"
	if !dump.IsValid {
		status = "CORRUPTED ❌"
	}

	fmt.Printf("=== ImpulseGraph Binary Snapshot Dump ===\n")
	fmt.Printf("File:          %s\n", binPath)
	fmt.Printf("Status:        %s\n", status)
	fmt.Printf("File Size:     %.2f MB (%d bytes)\n", float64(dump.ByteSize)/(1024*1024), dump.ByteSize)
	fmt.Printf("Magic Bytes:   0x%X (IMPS)\n", dump.Header.Magic)
	fmt.Printf("Format Ver:    %d\n", dump.Header.Version)
	fmt.Printf("Kafka Offset:  %d\n", dump.Header.KafkaOffset)
	fmt.Printf("Timestamp:     %d\n", dump.Header.TimestampMs)
	fmt.Printf("SHA256 Hash:   %s\n", dump.SHA256Hex)

	fmt.Printf("\nRegistered Domains (%d):\n", len(dump.Domains))
	for _, dom := range dump.Domains {
		fmt.Printf("  - %s (ID=%d, KeyType=%s, ExplicitMapCount=%d)\n", dom.Name, dom.ID, dom.KeyType, dom.MapCount)
	}

	fmt.Printf("\nCSR Adjacency Matrices (%d):\n", len(dump.Relations))
	for _, rel := range dump.Relations {
		avgDeg := 0.0
		if rel.NodeCount > 0 {
			avgDeg = float64(rel.EdgeCount) / float64(rel.NodeCount)
		}
		fmt.Printf("  - Matrix [SrcDomainId=%d -> TgtDomainId=%d]:\n", rel.SrcDomainID, rel.TgtDomainID)
		fmt.Printf("      Total Nodes (N):    %d\n", rel.NodeCount)
		fmt.Printf("      Total Edges (E):    %d\n", rel.EdgeCount)
		fmt.Printf("      Average Degree:     %.2f edges/node\n", avgDeg)
		if len(rel.RowOffsets) > 0 {
			fmt.Printf("      Sample rowOffsets:  %v...\n", rel.RowOffsets)
		}
	}
}
