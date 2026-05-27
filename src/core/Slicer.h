#ifndef SLICEFORGE_SLICER_H
#define SLICEFORGE_SLICER_H

// =============================================================================
// Slicer — The Heart of the Engine
// =============================================================================
//
// This is where the core computational geometry happens. The slicer takes
// a 3D mesh and a Z-height, and computes the 2D contour (outline) at that
// height. Do this for hundreds of Z-heights and you have all the layers
// a 3D printer needs.
//
// THE KEY ALGORITHM: Plane-Triangle Intersection
//
// Imagine a horizontal plane (like a sheet of glass) at height Z. For each
// triangle in the mesh, there are 5 possible cases:
//
//   1. Triangle is entirely ABOVE the plane → skip
//   2. Triangle is entirely BELOW the plane → skip
//   3. Triangle is exactly ON the plane → edge case (we handle it)
//   4. Plane cuts through the triangle → produces a LINE SEGMENT
//   5. Plane touches just one vertex → produces a POINT (degenerate)
//
// Case 4 is the interesting one. When a plane cuts a triangle, it always
// produces exactly one line segment. Here's why:
//
//   The plane separates the triangle's 3 vertices into two groups:
//   those above and those below. There are only two configurations:
//   - 1 vertex above, 2 below (or vice versa): plane crosses 2 edges
//   - Each crossing gives us one intersection point
//   - Connect them → line segment
//
//        v0 (above)
//        /\
//    ---/--\--- ← cutting plane at height Z
//      /    \
//    v1------v2 (both below)
//
//   The two intersection points are where the plane crosses edge v0-v1
//   and edge v0-v2. The line segment between them is our slice contour
//   for this triangle.
//
// HOW WE FIND THE INTERSECTION POINT ON AN EDGE:
//
//   Given two vertices A and B on either side of the plane at height Z:
//   We need to find the point P on the line segment A→B where P.z = Z.
//
//   Using linear interpolation:
//     t = (Z - A.z) / (B.z - A.z)      ← how far along the edge
//     P = A + t * (B - A)                ← the actual point
//
//   t ranges from 0 (at A) to 1 (at B). If t is between 0 and 1,
//   the intersection is on the edge segment.
//
// WHY THIS MATTERS FOR THE INTERVIEW:
// This is real computational geometry — the same math used in professional
// slicers like Cura and PrusaSlicer. Be ready to explain:
// - How plane-triangle intersection works
// - Why you get exactly one line segment per intersected triangle
// - How line segments are chained into closed contours

#include "Mesh.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace SliceForge {

// A 2D point — the result of projecting a 3D intersection onto the XY plane.
// After slicing at height Z, we only care about X and Y coordinates.
struct Point2D {
    float x, y;

    Point2D() : x(0.0f), y(0.0f) {}
    Point2D(float x, float y) : x(x), y(y) {}

    bool operator==(const Point2D& other) const {
        const float eps = 1e-5f;
        return std::fabs(x - other.x) < eps &&
               std::fabs(y - other.y) < eps;
    }
};

// A line segment in 2D — one triangle's contribution to the slice
struct Segment2D {
    Point2D start, end;
    Segment2D() = default;
    Segment2D(const Point2D& s, const Point2D& e) : start(s), end(e) {}
};

// A contour is a closed loop of 2D points — one outline of the cross-section.
// A single slice can have multiple contours (think of a hollow tube:
// the outer wall is one contour, the inner wall is another).
using Contour = std::vector<Point2D>;

// A Layer holds all contours at a given Z height
struct Layer {
    float z;                        // The height of this layer
    std::vector<Contour> contours;  // All closed loops at this height
    std::vector<Segment2D> segments; // Raw segments before chaining

    Layer() : z(0.0f) {}
    Layer(float z) : z(z) {}
};


class Slicer {
public:
    // Slice a single layer at a given Z height
    //
    // Steps:
    // 1. Find all triangles that intersect the plane at Z
    // 2. Compute intersection segments
    // 3. Chain segments into closed contours
    static Layer sliceAtZ(const Mesh& mesh, float z) {
        Layer layer(z);

        // Step 1 & 2: Compute intersection segments
        for (const auto& tri : mesh.getTriangles()) {
            // Quick rejection: skip triangles that don't reach this Z height
            if (tri.minZ() > z + 1e-6f || tri.maxZ() < z - 1e-6f) {
                continue;
            }

            Segment2D seg;
            if (intersectTriangle(tri, z, seg)) {
                layer.segments.push_back(seg);
            }
        }

        // Step 3: Chain segments into closed contours
        layer.contours = chainSegments(layer.segments);

        return layer;
    }

    // Slice the entire mesh into layers
    // layerHeight: distance between consecutive slices (e.g., 0.2mm)
    static std::vector<Layer> sliceAll(const Mesh& mesh, float layerHeight) {
        if (mesh.empty()) {
            throw std::runtime_error("Cannot slice an empty mesh");
        }
        if (layerHeight <= 0.0f) {
            throw std::runtime_error("Layer height must be positive");
        }

        const auto& bounds = mesh.getBounds();
        float startZ = bounds.min.z + layerHeight / 2.0f; // Start half a layer up
        float endZ = bounds.max.z;

        std::vector<Layer> layers;
        for (float z = startZ; z < endZ; z += layerHeight) {
            layers.push_back(sliceAtZ(mesh, z));
        }

        return layers;
    }

private:
    // Compute the intersection of a horizontal plane at height Z with a triangle.
    // Returns true if an intersection segment exists, false otherwise.
    //
    // This is the core geometric algorithm. We classify each vertex as
    // above, below, or on the plane, then find edge crossings.
    static bool intersectTriangle(const Triangle& tri, float z, Segment2D& result) {
        const float eps = 1e-6f;

        // Classify each vertex relative to the plane
        // +1 = above, -1 = below, 0 = on the plane
        auto classify = [&](float vz) -> int {
            if (vz > z + eps) return 1;   // above
            if (vz < z - eps) return -1;  // below
            return 0;                      // on the plane
        };

        int c0 = classify(tri.v0.z);
        int c1 = classify(tri.v1.z);
        int c2 = classify(tri.v2.z);

        // If all vertices are on the same side, no intersection
        if (c0 == c1 && c1 == c2) {
            if (c0 == 0) {
                // All three vertices are ON the plane — coplanar triangle.
                // We skip these because they don't define a clean cross-section.
                return false;
            }
            return false; // All above or all below
        }

        // Collect intersection points by checking each edge
        std::vector<Point2D> intersections;

        auto checkEdge = [&](const Vertex& a, const Vertex& b, int ca, int cb) {
            if (ca == 0 && cb == 0) {
                // Both on plane — the entire edge is on the plane.
                // Add both endpoints.
                intersections.push_back(Point2D(a.x, a.y));
                intersections.push_back(Point2D(b.x, b.y));
            }
            else if (ca == 0) {
                // Vertex a is exactly on the plane
                intersections.push_back(Point2D(a.x, a.y));
            }
            else if (cb == 0) {
                // Vertex b is exactly on the plane
                intersections.push_back(Point2D(b.x, b.y));
            }
            else if (ca != cb) {
                // Vertices are on opposite sides — compute crossing point.
                // This is the linear interpolation formula:
                //   t = (z - a.z) / (b.z - a.z)
                //   point = a + t * (b - a)
                float t = (z - a.z) / (b.z - a.z);
                float ix = a.x + t * (b.x - a.x);
                float iy = a.y + t * (b.y - a.y);
                intersections.push_back(Point2D(ix, iy));
            }
            // If ca == cb (both on same side, neither on plane), no intersection
        };

        checkEdge(tri.v0, tri.v1, c0, c1);
        checkEdge(tri.v1, tri.v2, c1, c2);
        checkEdge(tri.v2, tri.v0, c2, c0);

        // Remove duplicate points (can happen when plane passes through a vertex)
        auto unique_end = std::unique(intersections.begin(), intersections.end());
        intersections.erase(unique_end, intersections.end());

        // We need exactly 2 points to form a segment
        if (intersections.size() == 2) {
            result = Segment2D(intersections[0], intersections[1]);
            return true;
        }

        return false; // Degenerate case (0, 1, or 3+ points)
    }

    // Chain individual segments into closed contours.
    //
    // After intersecting all triangles, we have a bunch of disconnected
    // line segments. But adjacent triangles produce segments that share
    // endpoints. We need to connect these into closed loops (contours).
    //
    // Algorithm:
    // 1. Build an adjacency map: for each point, which segments touch it?
    // 2. Start at any unused segment, follow the chain of connected
    //    segments until we return to the start → closed contour.
    // 3. Repeat until all segments are used.
    //
    // This is essentially finding cycles in a graph where segments are
    // edges and intersection points are nodes.
    static std::vector<Contour> chainSegments(const std::vector<Segment2D>& segments) {
        if (segments.empty()) return {};

        // Hash function for Point2D to use in unordered_map
        // We quantize coordinates to a grid to handle floating-point noise
        auto pointHash = [](const Point2D& p) -> size_t {
            // Quantize to 0.001mm grid to merge nearly-identical points
            int ix = static_cast<int>(std::round(p.x * 1000.0f));
            int iy = static_cast<int>(std::round(p.y * 1000.0f));
            return std::hash<int>()(ix) ^ (std::hash<int>()(iy) << 16);
        };

        auto pointEqual = [](const Point2D& a, const Point2D& b) -> bool {
            const float eps = 1e-3f;
            return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps;
        };

        // Build adjacency: point → list of (connected_point, segment_index)
        using PointMap = std::unordered_map<
            Point2D,
            std::vector<std::pair<Point2D, size_t>>,
            decltype(pointHash),
            decltype(pointEqual)
        >;
        PointMap adjacency(segments.size() * 2, pointHash, pointEqual);

        for (size_t i = 0; i < segments.size(); i++) {
            adjacency[segments[i].start].push_back({segments[i].end, i});
            adjacency[segments[i].end].push_back({segments[i].start, i});
        }

        // Track which segments have been used
        std::vector<bool> used(segments.size(), false);
        std::vector<Contour> contours;

        // Find all closed loops
        for (size_t startIdx = 0; startIdx < segments.size(); startIdx++) {
            if (used[startIdx]) continue;

            Contour contour;
            Point2D current = segments[startIdx].start;
            Point2D first = current;
            contour.push_back(current);
            used[startIdx] = true;
            current = segments[startIdx].end;
            contour.push_back(current);

            // Follow the chain
            bool progressed = true;
            while (progressed) {
                progressed = false;

                auto it = adjacency.find(current);
                if (it == adjacency.end()) break;

                for (auto& [neighbor, segIdx] : it->second) {
                    if (!used[segIdx]) {
                        used[segIdx] = true;
                        current = neighbor;
                        contour.push_back(current);
                        progressed = true;
                        break;
                    }
                }
            }

            // Only keep contours with at least 3 points (valid polygon)
            if (contour.size() >= 3) {
                contours.push_back(contour);
            }
        }

        return contours;
    }
};

} // namespace SliceForge

#endif // SLICEFORGE_SLICER_H
