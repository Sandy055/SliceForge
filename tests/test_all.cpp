// =============================================================================
// SliceForge Unit Tests
// =============================================================================
// A minimal test framework without external dependencies.
// Tests cover: Vertex math, Triangle normals, Mesh operations,
// STL/OBJ parsing, Slicer geometry, Pipeline threading, Serialization.
//
// WHY THIS MATTERS FOR THE INTERVIEW:
// Both roles list TDD (Test-Driven Development) as a desired skill.
// These tests demonstrate you think about edge cases, validate correctness,
// and write tests alongside code — not as an afterthought.

#include "../src/core/Mesh.h"
#include "../src/core/STLParser.h"
#include "../src/core/OBJParser.h"
#include "../src/core/Slicer.h"
#include "../src/core/SlicingPipeline.h"
#include "../src/core/ProfileSerializer.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <fstream>

using namespace SliceForge;

// Simple test macro
static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    void name(); \
    struct name##_registrar { \
        name##_registrar() { \
            std::cout << "  Running " << #name << "... "; \
            try { \
                name(); \
                std::cout << "PASSED\n"; \
                testsPassed++; \
            } catch (const std::exception& e) { \
                std::cout << "FAILED: " << e.what() << "\n"; \
                testsFailed++; \
            } \
        } \
    } name##_instance; \
    void name()

#define ASSERT_TRUE(expr) \
    if (!(expr)) throw std::runtime_error("Assertion failed: " #expr)

#define ASSERT_NEAR(a, b, eps) \
    if (std::fabs((a) - (b)) > (eps)) \
        throw std::runtime_error( \
            "ASSERT_NEAR failed: " + std::to_string(a) + " vs " + std::to_string(b))

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) \
        throw std::runtime_error( \
            "ASSERT_EQ failed: " + std::to_string(a) + " vs " + std::to_string(b))


// ─── Helper: Create a simple test STL file ──────────────────────────
void writeTestSTL(const std::string& path) {
    // A simple triangle lying flat on the XY plane at z=0,
    // plus another triangle forming a roof at z=10.
    // Together they make a crude "tent" shape.
    std::ofstream f(path);
    f << "solid TestModel\n";
    // Base triangle at z=0
    f << "  facet normal 0 0 -1\n";
    f << "    outer loop\n";
    f << "      vertex 0 0 0\n";
    f << "      vertex 10 0 0\n";
    f << "      vertex 5 10 0\n";
    f << "    endloop\n";
    f << "  endfacet\n";
    // Left wall
    f << "  facet normal -0.894 0 0.447\n";
    f << "    outer loop\n";
    f << "      vertex 0 0 0\n";
    f << "      vertex 5 10 0\n";
    f << "      vertex 5 5 10\n";
    f << "    endloop\n";
    f << "  endfacet\n";
    // Right wall
    f << "  facet normal 0.894 0 0.447\n";
    f << "    outer loop\n";
    f << "      vertex 10 0 0\n";
    f << "      vertex 5 5 10\n";
    f << "      vertex 5 10 0\n";
    f << "    endloop\n";
    f << "  endfacet\n";
    // Front wall
    f << "  facet normal 0 -0.894 0.447\n";
    f << "    outer loop\n";
    f << "      vertex 0 0 0\n";
    f << "      vertex 5 5 10\n";
    f << "      vertex 10 0 0\n";
    f << "    endloop\n";
    f << "  endfacet\n";
    f << "endsolid TestModel\n";
}

// ─── Helper: Create a test OBJ file ────────────────────────────────
void writeTestOBJ(const std::string& path) {
    // A simple cube-like shape
    std::ofstream f(path);
    f << "# Test cube\n";
    f << "v 0 0 0\n";   // 1
    f << "v 10 0 0\n";  // 2
    f << "v 10 10 0\n"; // 3
    f << "v 0 10 0\n";  // 4
    f << "v 0 0 10\n";  // 5
    f << "v 10 0 10\n"; // 6
    f << "v 10 10 10\n"; // 7
    f << "v 0 10 10\n";  // 8
    // Bottom face (two triangles)
    f << "f 1 2 3\n";
    f << "f 1 3 4\n";
    // Top face
    f << "f 5 7 6\n";
    f << "f 5 8 7\n";
    // Front face
    f << "f 1 2 6\n";
    f << "f 1 6 5\n";
    // Back face
    f << "f 3 4 8\n";
    f << "f 3 8 7\n";
    // Left face
    f << "f 1 4 8\n";
    f << "f 1 8 5\n";
    // Right face
    f << "f 2 3 7\n";
    f << "f 2 7 6\n";
}


// ═══════════════════════════════════════════════════════════════════════
// VERTEX TESTS
// ═══════════════════════════════════════════════════════════════════════

TEST(test_vertex_construction) {
    Vertex v(1.0f, 2.0f, 3.0f);
    ASSERT_NEAR(v.x, 1.0f, 1e-6f);
    ASSERT_NEAR(v.y, 2.0f, 1e-6f);
    ASSERT_NEAR(v.z, 3.0f, 1e-6f);
}

TEST(test_vertex_subtraction) {
    Vertex a(3.0f, 5.0f, 7.0f);
    Vertex b(1.0f, 2.0f, 3.0f);
    Vertex diff = a - b;
    ASSERT_NEAR(diff.x, 2.0f, 1e-6f);
    ASSERT_NEAR(diff.y, 3.0f, 1e-6f);
    ASSERT_NEAR(diff.z, 4.0f, 1e-6f);
}

TEST(test_vertex_cross_product) {
    // Cross product of X-axis and Y-axis should give Z-axis
    Vertex x(1, 0, 0);
    Vertex y(0, 1, 0);
    Vertex z = x.cross(y);
    ASSERT_NEAR(z.x, 0.0f, 1e-6f);
    ASSERT_NEAR(z.y, 0.0f, 1e-6f);
    ASSERT_NEAR(z.z, 1.0f, 1e-6f);
}

TEST(test_vertex_dot_product) {
    Vertex a(1, 0, 0);
    Vertex b(0, 1, 0);
    ASSERT_NEAR(a.dot(b), 0.0f, 1e-6f); // Perpendicular = 0

    Vertex c(1, 0, 0);
    ASSERT_NEAR(a.dot(c), 1.0f, 1e-6f); // Parallel = 1
}

TEST(test_vertex_normalize) {
    Vertex v(3.0f, 4.0f, 0.0f);
    Vertex n = v.normalized();
    ASSERT_NEAR(n.length(), 1.0f, 1e-6f);
    ASSERT_NEAR(n.x, 0.6f, 1e-6f);
    ASSERT_NEAR(n.y, 0.8f, 1e-6f);
}

TEST(test_vertex_equality_with_epsilon) {
    Vertex a(1.0f, 2.0f, 3.0f);
    Vertex b(1.0000001f, 2.0000001f, 3.0000001f);
    ASSERT_TRUE(a == b); // Should be equal within epsilon
}


// ═══════════════════════════════════════════════════════════════════════
// TRIANGLE TESTS
// ═══════════════════════════════════════════════════════════════════════

TEST(test_triangle_normal_computation) {
    // A triangle on the XY plane should have a Z-pointing normal
    Triangle tri(Vertex(0, 0, 0), Vertex(1, 0, 0), Vertex(0, 1, 0));
    ASSERT_NEAR(std::fabs(tri.normal.z), 1.0f, 1e-6f);
}

TEST(test_triangle_z_bounds) {
    Triangle tri(Vertex(0, 0, 1), Vertex(1, 0, 5), Vertex(0, 1, 3));
    ASSERT_NEAR(tri.minZ(), 1.0f, 1e-6f);
    ASSERT_NEAR(tri.maxZ(), 5.0f, 1e-6f);
}

TEST(test_degenerate_triangle) {
    // All three vertices are collinear — zero area triangle
    Triangle tri(Vertex(0, 0, 0), Vertex(1, 0, 0), Vertex(2, 0, 0));
    ASSERT_NEAR(tri.normal.length(), 0.0f, 1e-6f);
}


// ═══════════════════════════════════════════════════════════════════════
// MESH TESTS
// ═══════════════════════════════════════════════════════════════════════

TEST(test_mesh_add_triangle) {
    Mesh mesh;
    ASSERT_TRUE(mesh.empty());
    mesh.addTriangle(Triangle(Vertex(0, 0, 0), Vertex(1, 0, 0), Vertex(0, 1, 0)));
    ASSERT_EQ(mesh.triangleCount(), (size_t)1);
    ASSERT_TRUE(!mesh.empty());
}

TEST(test_mesh_bounding_box) {
    Mesh mesh;
    mesh.addTriangle(Triangle(Vertex(-5, -3, 0), Vertex(10, 0, 0), Vertex(0, 8, 15)));
    ASSERT_NEAR(mesh.getBounds().min.x, -5.0f, 1e-6f);
    ASSERT_NEAR(mesh.getBounds().max.x, 10.0f, 1e-6f);
    ASSERT_NEAR(mesh.getBounds().max.z, 15.0f, 1e-6f);
}


// ═══════════════════════════════════════════════════════════════════════
// PARSER TESTS
// ═══════════════════════════════════════════════════════════════════════

TEST(test_stl_parser_ascii) {
    writeTestSTL("/tmp/test_model.stl");
    Mesh mesh = STLParser::parse("/tmp/test_model.stl");
    ASSERT_EQ(mesh.triangleCount(), (size_t)4);
    ASSERT_NEAR(mesh.getBounds().max.z, 10.0f, 1e-6f);
}

TEST(test_obj_parser) {
    writeTestOBJ("/tmp/test_model.obj");
    Mesh mesh = OBJParser::parse("/tmp/test_model.obj");
    ASSERT_EQ(mesh.triangleCount(), (size_t)12); // 6 faces × 2 triangles
    ASSERT_NEAR(mesh.getBounds().max.z, 10.0f, 1e-6f);
}

TEST(test_parser_invalid_file) {
    bool threw = false;
    try {
        STLParser::parse("/tmp/nonexistent_file.stl");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}


// ═══════════════════════════════════════════════════════════════════════
// SLICER TESTS
// ═══════════════════════════════════════════════════════════════════════

TEST(test_slice_cube_produces_contours) {
    writeTestOBJ("/tmp/test_cube.obj");
    Mesh mesh = OBJParser::parse("/tmp/test_cube.obj");

    // Slice at z=5 (middle of a 10-unit cube)
    Layer layer = Slicer::sliceAtZ(mesh, 5.0f);

    // Should have segments (the plane cuts through the cube walls)
    ASSERT_TRUE(layer.segments.size() > 0);
    std::cout << "(segments=" << layer.segments.size()
              << ", contours=" << layer.contours.size() << ") ";
}

TEST(test_slice_above_mesh_is_empty) {
    writeTestOBJ("/tmp/test_cube.obj");
    Mesh mesh = OBJParser::parse("/tmp/test_cube.obj");

    // Slice above the cube — should be empty
    Layer layer = Slicer::sliceAtZ(mesh, 20.0f);
    ASSERT_EQ(layer.segments.size(), (size_t)0);
}

TEST(test_slice_all_layers) {
    writeTestOBJ("/tmp/test_cube.obj");
    Mesh mesh = OBJParser::parse("/tmp/test_cube.obj");

    auto layers = Slicer::sliceAll(mesh, 1.0f);
    // Cube is 10 units tall, layer height 1.0, so ~9-10 layers
    ASSERT_TRUE(layers.size() >= 8);
    ASSERT_TRUE(layers.size() <= 11);
}


// ═══════════════════════════════════════════════════════════════════════
// PIPELINE TESTS
// ═══════════════════════════════════════════════════════════════════════

TEST(test_parallel_matches_sequential) {
    writeTestOBJ("/tmp/test_cube.obj");
    Mesh mesh = OBJParser::parse("/tmp/test_cube.obj");

    SlicingPipeline::Config config;
    config.layerHeight = 1.0f;

    auto seqResult = SlicingPipeline::sliceSequential(mesh, config);
    auto parResult = SlicingPipeline::sliceParallel(mesh, config);

    // Both should produce the same number of layers
    ASSERT_EQ(seqResult.layers.size(), parResult.layers.size());

    // Each layer should have the same number of segments
    for (size_t i = 0; i < seqResult.layers.size(); i++) {
        ASSERT_EQ(seqResult.layers[i].segments.size(),
                  parResult.layers[i].segments.size());
    }
}

TEST(test_pipeline_reports_timing) {
    writeTestOBJ("/tmp/test_cube.obj");
    Mesh mesh = OBJParser::parse("/tmp/test_cube.obj");

    SlicingPipeline::Config config;
    config.layerHeight = 1.0f;

    auto result = SlicingPipeline::sliceParallel(mesh, config);
    ASSERT_TRUE(result.elapsedMs >= 0.0);
    ASSERT_TRUE(result.threadsUsed >= 1);
}


// ═══════════════════════════════════════════════════════════════════════
// SERIALIZER TESTS
// ═══════════════════════════════════════════════════════════════════════

TEST(test_json_serialization) {
    writeTestOBJ("/tmp/test_cube.obj");
    Mesh mesh = OBJParser::parse("/tmp/test_cube.obj");

    auto layers = Slicer::sliceAll(mesh, 2.0f);
    ProfileSerializer::BuildConfig config;
    config.layerHeight = 2.0f;
    config.modelName = "test_cube";

    std::string json = ProfileSerializer::toJSON(layers, mesh, config);

    // Verify JSON contains expected fields
    ASSERT_TRUE(json.find("\"buildProfile\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"layerHeight\": 2.0") != std::string::npos);
    ASSERT_TRUE(json.find("\"modelName\": \"test_cube\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"layers\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"triangleCount\": 12") != std::string::npos);
}

TEST(test_json_file_output) {
    writeTestOBJ("/tmp/test_cube.obj");
    Mesh mesh = OBJParser::parse("/tmp/test_cube.obj");

    auto layers = Slicer::sliceAll(mesh, 2.0f);
    ProfileSerializer::BuildConfig config;
    config.modelName = "test_cube";

    ProfileSerializer::toFile("/tmp/test_output.json", layers, mesh, config);

    // Verify file exists and has content
    std::ifstream f("/tmp/test_output.json");
    ASSERT_TRUE(f.is_open());
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.size() > 50);
}


// ═══════════════════════════════════════════════════════════════════════
// EDGE CASE TESTS
// ═══════════════════════════════════════════════════════════════════════

TEST(test_empty_mesh_throws) {
    Mesh mesh;
    bool threw = false;
    try {
        Slicer::sliceAll(mesh, 0.2f);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

TEST(test_zero_layer_height_throws) {
    Mesh mesh;
    mesh.addTriangle(Triangle(Vertex(0,0,0), Vertex(1,0,0), Vertex(0,1,1)));
    bool threw = false;
    try {
        Slicer::sliceAll(mesh, 0.0f);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

TEST(test_json_escapes_special_chars) {
    Mesh mesh;
    mesh.addTriangle(Triangle(Vertex(0,0,0), Vertex(1,0,0), Vertex(0,1,1)));
    auto layers = Slicer::sliceAll(mesh, 0.5f);
    ProfileSerializer::BuildConfig config;
    config.modelName = "file\"with\\quotes";

    std::string json = ProfileSerializer::toJSON(layers, mesh, config);
    ASSERT_TRUE(json.find("file\\\"with\\\\quotes") != std::string::npos);
}


// ═══════════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "\n╔══════════════════════════════════════╗\n";
    std::cout << "║  SliceForge Test Suite               ║\n";
    std::cout << "╚══════════════════════════════════════╝\n\n";

    // Tests run automatically via static initialization above

    std::cout << "\n──────────────────────────────────────\n";
    std::cout << "Results: " << testsPassed << " passed, "
              << testsFailed << " failed\n";

    return testsFailed > 0 ? 1 : 0;
}
