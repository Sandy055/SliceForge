// =============================================================================
// SliceForge CLI — Command Line Interface
// =============================================================================
// Usage: ./sliceforge <input.stl|input.obj> [layer_height] [output.json]
//
// This is the non-GUI entry point. It loads a mesh, slices it with both
// single-threaded and multi-threaded approaches (for benchmarking), and
// writes the output as JSON.

#include "core/Mesh.h"
#include "core/STLParser.h"
#include "core/OBJParser.h"
#include "core/Slicer.h"
#include "core/SlicingPipeline.h"
#include "core/ProfileSerializer.h"

#include <iostream>
#include <string>
#include <algorithm>

using namespace SliceForge;

// Detect file type from extension
enum class FileType { STL, OBJ, UNKNOWN };

FileType detectFileType(const std::string& filepath) {
    std::string ext = filepath.substr(filepath.find_last_of('.') + 1);
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == "stl") return FileType::STL;
    if (ext == "obj") return FileType::OBJ;
    return FileType::UNKNOWN;
}

int main(int argc, char* argv[]) {
    std::cout << "╔═══════════════════════════════════════╗\n";
    std::cout << "║  SliceForge — 3D Geometry Slicer      ║\n";
    std::cout << "╚═══════════════════════════════════════╝\n\n";

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <input.stl|input.obj> [layer_height] [output.json]\n";
        return 1;
    }

    std::string inputFile = argv[1];
    float layerHeight = (argc >= 3) ? std::stof(argv[2]) : 0.2f;
    std::string outputFile = (argc >= 4) ? argv[3] : "output_profile.json";

    try {
        // ── Load Mesh ──
        std::cout << "Loading: " << inputFile << "\n";
        Mesh mesh;
        FileType type = detectFileType(inputFile);

        if (type == FileType::STL) {
            mesh = STLParser::parse(inputFile);
        } else if (type == FileType::OBJ) {
            mesh = OBJParser::parse(inputFile);
        } else {
            std::cerr << "Error: Unsupported file format. Use .stl or .obj\n";
            return 1;
        }

        const auto& bounds = mesh.getBounds();
        std::cout << "  Triangles: " << mesh.triangleCount() << "\n";
        std::cout << "  Bounds: ["
                  << bounds.min.x << ", " << bounds.min.y << ", " << bounds.min.z
                  << "] to ["
                  << bounds.max.x << ", " << bounds.max.y << ", " << bounds.max.z
                  << "]\n";
        std::cout << "  Model height: " << bounds.depth() << " mm\n";
        std::cout << "  Degenerate triangles: "
                  << mesh.countDegenerateTriangles() << "\n\n";

        // ── Sequential Slicing (baseline) ──
        std::cout << "Slicing (single-threaded) with layer height "
                  << layerHeight << " mm...\n";
        SlicingPipeline::Config config;
        config.layerHeight = layerHeight;

        auto seqResult = SlicingPipeline::sliceSequential(mesh, config);
        std::cout << "  Layers: " << seqResult.layers.size() << "\n";
        std::cout << "  Time: " << seqResult.elapsedMs << " ms\n\n";

        // ── Parallel Slicing ──
        std::cout << "Slicing (multi-threaded)...\n";
        auto parResult = SlicingPipeline::sliceParallel(mesh, config);
        std::cout << "  Layers: " << parResult.layers.size() << "\n";
        std::cout << "  Threads: " << parResult.threadsUsed << "\n";
        std::cout << "  Time: " << parResult.elapsedMs << " ms\n";

        // ── Speedup ──
        if (parResult.elapsedMs > 0) {
            double speedup = seqResult.elapsedMs / parResult.elapsedMs;
            std::cout << "  Speedup: " << speedup << "x\n\n";
        }

        // ── Serialize Output ──
        std::cout << "Writing build profile to: " << outputFile << "\n";
        ProfileSerializer::BuildConfig buildConfig;
        buildConfig.layerHeight = layerHeight;
        buildConfig.modelName = inputFile;
        ProfileSerializer::toFile(outputFile, parResult.layers, mesh, buildConfig);

        // ── Summary ──
        size_t totalContours = 0;
        size_t totalSegments = 0;
        for (const auto& layer : parResult.layers) {
            totalContours += layer.contours.size();
            totalSegments += layer.segments.size();
        }
        std::cout << "  Total contours: " << totalContours << "\n";
        std::cout << "  Total segments: " << totalSegments << "\n";
        std::cout << "\nDone!\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
