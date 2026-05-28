#ifndef SLICEFORGE_STL_PARSER_H
#define SLICEFORGE_STL_PARSER_H

// =============================================================================
// STL Parser — Reading 3D model files
// =============================================================================
//
// STL (STereoLithography) is THE standard format for 3D printing. It was
// created in 1987 for stereolithography and is still the most widely used
// format in additive manufacturing today.
//
// STL files come in two flavors:
//
// 1. ASCII STL — Human-readable text. Looks like:
//      solid MyModel
//        facet normal 0 0 1
//          outer loop
//            vertex 0 0 0
//            vertex 1 0 0
//            vertex 0 1 0
//          endloop
//        endfacet
//      endsolid MyModel
//
// 2. Binary STL — Compact binary. Structure:
//      80 bytes: header (ignored)
//       4 bytes: number of triangles (uint32)
//      For each triangle:
//        12 bytes: normal (3 floats)
//        36 bytes: 3 vertices (9 floats)
//         2 bytes: attribute byte count (usually 0)
//
// Binary is ~10x smaller than ASCII for the same model. Our parser handles
// both automatically by checking the file header.

#include "Mesh.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdint>

namespace SliceForge {

class STLParser {
public:
    // Main entry point — auto-detects ASCII vs binary
    static Mesh parse(const std::string& filepath) {
        // First, try to detect the format.
        // ASCII STL files start with the word "solid" followed by a name.
        // Binary STL files have an 80-byte header that MIGHT also start with
        // "solid", so we need a smarter check.
        if (isAsciiSTL(filepath)) {
            return parseAscii(filepath);
        } else {
            return parseBinary(filepath);
        }
    }

private:
    // Detect whether a file is ASCII or binary STL.
    //
    // The trick: we can't just check for "solid" at the start because some
    // binary files happen to start with "solid" in their header. Instead,
    // we check if the file starts with "solid" AND contains "facet" and
    // "vertex" keywords (which binary files won't have as readable text).
    static bool isAsciiSTL(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::in);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filepath);
        }

        // Read first line
        std::string firstLine;
        std::getline(file, firstLine);

        // Check if it starts with "solid"
        if (firstLine.substr(0, 5) != "solid") {
            return false;
        }

        // Look for "facet" in the next few lines to confirm ASCII
        for (int i = 0; i < 5 && !file.eof(); i++) {
            std::string line;
            std::getline(file, line);
            if (line.find("facet") != std::string::npos) {
                return true;
            }
        }

        return false; // Has "solid" header but no "facet" — likely binary
    }

    // Parse ASCII STL format
    static Mesh parseAscii(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::in);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filepath);
        }

        Mesh mesh;
        std::string line;
        Vertex normal;
        Vertex vertices[3];
        int vertexIndex = 0;

        while (std::getline(file, line)) {
            // Trim leading whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);

            if (line.substr(0, 12) == "facet normal") {
                // Parse the normal vector
                std::istringstream iss(line.substr(12));
                iss >> normal.x >> normal.y >> normal.z;
                vertexIndex = 0;
            }
            else if (line.substr(0, 6) == "vertex") {
                // Parse a vertex
                if (vertexIndex >= 3) {
                    throw std::runtime_error(
                        "STL parse error: more than 3 vertices in a facet"
                    );
                }
                std::istringstream iss(line.substr(6));
                iss >> vertices[vertexIndex].x
                    >> vertices[vertexIndex].y
                    >> vertices[vertexIndex].z;
                vertexIndex++;
            }
            else if (line.substr(0, 8) == "endfacet") {
                // We have all 3 vertices — create the triangle
                if (vertexIndex != 3) {
                    throw std::runtime_error(
                        "STL parse error: facet has fewer than 3 vertices"
                    );
                }
                mesh.addTriangle(
                    Triangle(vertices[0], vertices[1], vertices[2], normal)
                );
            }
            // We ignore "solid", "endsolid", "outer loop", "endloop" lines
        }

        if (mesh.empty()) {
            throw std::runtime_error("STL file contains no triangles: " + filepath);
        }

        return mesh;
    }

    // Parse binary STL format
    //
    // Binary format is straightforward: fixed-size header, then a packed
    // array of triangle records. We read it using raw byte operations.
    //
    // IMPORTANT DETAIL: We open the file in binary mode (std::ios::binary).
    // Without this flag, the OS might translate newline characters (\n vs
    // \r\n on Windows), corrupting the binary data. This is a common bug
    // that's easy to miss.
    static Mesh parseBinary(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filepath);
        }

        // Skip 80-byte header (usually contains model name or is zeroed out)
        char header[80];
        file.read(header, 80);

        // Read triangle count (4 bytes, little-endian uint32)
        uint32_t triangleCount;
        file.read(reinterpret_cast<char*>(&triangleCount), sizeof(uint32_t));

        if (triangleCount == 0) {
            throw std::runtime_error("Binary STL has zero triangles: " + filepath);
        }

        // Sanity check — each triangle is 50 bytes in binary STL.
        // If the file is too small, it's corrupt.
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        size_t expectedSize = 84 + (triangleCount * 50); // 80 header + 4 count + 50 per tri
        if (fileSize < expectedSize) {
            throw std::runtime_error(
                "Binary STL file is truncated: expected " +
                std::to_string(expectedSize) + " bytes, got " +
                std::to_string(fileSize)
            );
        }
        file.seekg(84); // Back to start of triangle data

        Mesh mesh;

        // Read each triangle: 12 bytes normal + 36 bytes vertices + 2 bytes attribute
        for (uint32_t i = 0; i < triangleCount; i++) {
            float data[12]; // 3 normal + 9 vertex floats
            file.read(reinterpret_cast<char*>(data), 12 * sizeof(float));

            Vertex normal(data[0], data[1], data[2]);
            Vertex v0(data[3], data[4], data[5]);
            Vertex v1(data[6], data[7], data[8]);
            Vertex v2(data[9], data[10], data[11]);

            // Skip 2-byte attribute count (rarely used)
            uint16_t attrByteCount;
            file.read(reinterpret_cast<char*>(&attrByteCount), sizeof(uint16_t));

            mesh.addTriangle(Triangle(v0, v1, v2, normal));
        }

        return mesh;
    }
};

} // namespace SliceForge

#endif // SLICEFORGE_STL_PARSER_H
