#ifndef SLICEFORGE_OBJ_PARSER_H
#define SLICEFORGE_OBJ_PARSER_H

// =============================================================================
// OBJ Parser — The other major 3D format
// =============================================================================
//
// OBJ (Wavefront) files store geometry differently from STL:
//
// - STL stores each triangle independently with its own 3 vertices.
//   If two triangles share an edge, both triangles store copies of
//   those shared vertices. This is redundant but simple.
//
// - OBJ stores vertices in one list, then defines faces by referencing
//   vertex INDICES. Shared vertices are stored once and referenced
//   multiple times. More memory-efficient.
//
// Example OBJ file (a simple square made of 2 triangles):
//   v 0.0 0.0 0.0      ← vertex 1
//   v 1.0 0.0 0.0      ← vertex 2
//   v 1.0 1.0 0.0      ← vertex 3
//   v 0.0 1.0 0.0      ← vertex 4
//   f 1 2 3             ← triangle using vertices 1, 2, 3
//   f 1 3 4             ← triangle using vertices 1, 3, 4
//
// OBJ also supports quads (f 1 2 3 4) and n-gons, but we triangulate
// them because our slicer only works with triangles.
//
// WHY THIS MATTERS FOR THE INTERVIEW:
// This demonstrates understanding of different data representations
// for the same concept, and the trade-offs between them (STL is simpler
// to parse but wastes memory; OBJ is compact but requires index lookups).

#include "Mesh.h"
#include <fstream>
#include <sstream>

namespace SliceForge {

class OBJParser {
public:
    static Mesh parse(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::in);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filepath);
        }

        std::vector<Vertex> vertices; // All vertices, indexed from 0
        Mesh mesh;
        std::string line;

        while (std::getline(file, line)) {
            // Trim leading whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);

            // Skip comments
            if (line[0] == '#') continue;

            if (line.substr(0, 2) == "v ") {
                // Parse vertex: "v x y z"
                Vertex v;
                std::istringstream iss(line.substr(2));
                iss >> v.x >> v.y >> v.z;
                vertices.push_back(v);
            }
            else if (line.substr(0, 2) == "f ") {
                // Parse face: "f v1 v2 v3 [v4 ...]"
                // OBJ indices are 1-based, so we subtract 1.
                //
                // Face entries can be complex: "f v/vt/vn v/vt/vn v/vt/vn"
                // where vt = texture coord index, vn = normal index.
                // We only care about the vertex index (first number).
                std::vector<int> faceIndices;
                std::istringstream iss(line.substr(2));
                std::string token;

                while (iss >> token) {
                    // Extract vertex index (everything before the first '/')
                    int idx = std::stoi(token.substr(0, token.find('/')));
                    // Convert 1-based to 0-based index
                    idx -= 1;

                    if (idx < 0 || idx >= static_cast<int>(vertices.size())) {
                        throw std::runtime_error(
                            "OBJ face references invalid vertex index: " +
                            std::to_string(idx + 1)
                        );
                    }
                    faceIndices.push_back(idx);
                }

                if (faceIndices.size() < 3) {
                    throw std::runtime_error(
                        "OBJ face has fewer than 3 vertices"
                    );
                }

                // Fan triangulation for polygons with 4+ vertices.
                //
                // If we have a quad (4 vertices: A B C D), we split it into
                // two triangles: (A, B, C) and (A, C, D).
                //
                // This generalizes to n-gons: always use the first vertex
                // as the "anchor" and create triangles with consecutive
                // pairs of the remaining vertices.
                //
                //      A ---- B          A ---- B
                //      |      |    →     |   / |
                //      |      |          | /   |
                //      D ---- C          D ---- C
                //
                //  Triangle 1: A, B, C    Triangle 2: A, C, D
                for (size_t i = 1; i + 1 < faceIndices.size(); i++) {
                    mesh.addTriangle(Triangle(
                        vertices[faceIndices[0]],
                        vertices[faceIndices[i]],
                        vertices[faceIndices[i + 1]]
                    ));
                }
            }
            // We ignore vt (texture coords), vn (normals), etc.
            // Our slicer computes its own normals from vertex positions.
        }

        if (mesh.empty()) {
            throw std::runtime_error("OBJ file contains no faces: " + filepath);
        }

        return mesh;
    }
};

} // namespace SliceForge

#endif // SLICEFORGE_OBJ_PARSER_H
