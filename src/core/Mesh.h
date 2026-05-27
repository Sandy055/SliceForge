#ifndef SLICEFORGE_MESH_H
#define SLICEFORGE_MESH_H

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace SliceForge {

// =============================================================================
// Vertex — A single point in 3D space
// =============================================================================
// Every 3D model is ultimately made of points (vertices). Each vertex has
// an x, y, z coordinate. Think of it like a pin stuck in 3D space.
//
// Why a struct and not just three floats? Because we want to attach behavior
// to it — like computing distance, or checking equality. This is OOP:
// bundling data with the operations that act on it.

struct Vertex {
    float x, y, z;

    Vertex() : x(0.0f), y(0.0f), z(0.0f) {}
    Vertex(float x, float y, float z) : x(x), y(y), z(z) {}

    // Vector subtraction — gives us the direction from one point to another.
    // Used heavily in triangle normal calculation and intersection math.
    Vertex operator-(const Vertex& other) const {
        return Vertex(x - other.x, y - other.y, z - other.z);
    }

    Vertex operator+(const Vertex& other) const {
        return Vertex(x + other.x, y + other.y, z + other.z);
    }

    // Scalar multiplication — scale a vector up or down.
    // Used in interpolation: finding the exact point where a cutting plane
    // crosses a triangle edge.
    Vertex operator*(float scalar) const {
        return Vertex(x * scalar, y * scalar, z * scalar);
    }

    // Cross product — gives a vector perpendicular to two input vectors.
    // This is how we compute a triangle's "normal" (which direction it faces).
    // The normal tells us the outside surface direction, critical for knowing
    // what's inside vs outside the model.
    Vertex cross(const Vertex& other) const {
        return Vertex(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }

    // Dot product — measures how much two vectors point in the same direction.
    // Returns a scalar: positive = same direction, zero = perpendicular,
    // negative = opposite. Used in intersection tests.
    float dot(const Vertex& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    // Length of the vector (distance from origin)
    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    // Normalize — make the vector length 1 while keeping its direction.
    // A "unit vector" is easier to work with in many geometric calculations.
    Vertex normalized() const {
        float len = length();
        if (len < 1e-8f) {
            throw std::runtime_error("Cannot normalize a zero-length vector");
        }
        return Vertex(x / len, y / len, z / len);
    }

    bool operator==(const Vertex& other) const {
        // Floating point comparison with tolerance (epsilon).
        // Never compare floats with == directly because of rounding errors.
        // Example: 0.1 + 0.2 might be 0.30000000004 in floating point.
        const float eps = 1e-6f;
        return std::fabs(x - other.x) < eps &&
               std::fabs(y - other.y) < eps &&
               std::fabs(z - other.z) < eps;
    }

    bool operator!=(const Vertex& other) const {
        return !(*this == other);
    }
};


// =============================================================================
// Triangle — Three vertices forming a flat surface
// =============================================================================
// A triangle is the simplest polygon — it's always flat (three points define
// a plane), which makes math much easier than dealing with quads or polygons.
// That's why virtually all 3D formats use triangles as their basic unit.
//
// Each triangle also has a "normal" — a vector pointing outward perpendicular
// to the triangle's surface. This tells us which side is the "outside."

struct Triangle {
    Vertex v0, v1, v2;  // The three corner vertices
    Vertex normal;       // Outward-facing direction

    Triangle() = default;

    Triangle(const Vertex& v0, const Vertex& v1, const Vertex& v2)
        : v0(v0), v1(v1), v2(v2) {
        computeNormal();
    }

    Triangle(const Vertex& v0, const Vertex& v1, const Vertex& v2,
             const Vertex& normal)
        : v0(v0), v1(v1), v2(v2), normal(normal) {}

    // Compute normal using the cross product of two edges.
    // Edge1 = v1 - v0, Edge2 = v2 - v0.
    // Their cross product is perpendicular to the triangle's surface.
    //
    // WHY THIS MATTERS FOR THE INTERVIEW:
    // Steven might ask "how do you determine which direction a surface faces?"
    // Answer: cross product of two edges gives the surface normal. The winding
    // order (clockwise vs counterclockwise) of the vertices determines which
    // way the normal points — this is a convention in 3D graphics.
    void computeNormal() {
        Vertex edge1 = v1 - v0;
        Vertex edge2 = v2 - v0;
        Vertex n = edge1.cross(edge2);
        float len = n.length();
        if (len > 1e-8f) {
            normal = Vertex(n.x / len, n.y / len, n.z / len);
        } else {
            // Degenerate triangle (zero area) — all three points are collinear
            normal = Vertex(0.0f, 0.0f, 0.0f);
        }
    }

    // Get the minimum and maximum Z values of this triangle.
    // Used by the slicer to quickly skip triangles that don't intersect
    // a given horizontal cutting plane. If the plane is at z=5.0 and the
    // triangle's Z range is [7.0, 9.0], we can skip it entirely.
    float minZ() const { return std::min({v0.z, v1.z, v2.z}); }
    float maxZ() const { return std::max({v0.z, v1.z, v2.z}); }
};


// =============================================================================
// BoundingBox — The smallest box that contains the entire mesh
// =============================================================================
// Imagine shrink-wrapping a rectangular box around your 3D model.
// This tells us the model's extent in each dimension.
// The slicer uses this to know where to start and stop slicing (minZ to maxZ).

struct BoundingBox {
    Vertex min;  // Corner with smallest x, y, z
    Vertex max;  // Corner with largest x, y, z

    BoundingBox()
        : min(Vertex( 1e30f,  1e30f,  1e30f)),
          max(Vertex(-1e30f, -1e30f, -1e30f)) {}

    // Expand the bounding box to include a new vertex
    void expand(const Vertex& v) {
        min.x = std::min(min.x, v.x);
        min.y = std::min(min.y, v.y);
        min.z = std::min(min.z, v.z);
        max.x = std::max(max.x, v.x);
        max.y = std::max(max.y, v.y);
        max.z = std::max(max.z, v.z);
    }

    float width()  const { return max.x - min.x; }
    float height() const { return max.y - min.y; }
    float depth()  const { return max.z - min.z; }
};


// =============================================================================
// Mesh — The complete 3D model, a collection of triangles
// =============================================================================
// This is the top-level container. A mesh holds all the triangles plus
// metadata like the bounding box and triangle count.
//
// DESIGN PATTERN: This follows the Composite pattern loosely — a Mesh is
// composed of Triangles, which are composed of Vertices. Each level
// encapsulates its own operations.

class Mesh {
public:
    Mesh() = default;

    // Add a triangle and update the bounding box
    void addTriangle(const Triangle& tri) {
        triangles_.push_back(tri);
        bounds_.expand(tri.v0);
        bounds_.expand(tri.v1);
        bounds_.expand(tri.v2);
    }

    // Accessors — const references to avoid copying large data
    const std::vector<Triangle>& getTriangles() const { return triangles_; }
    const BoundingBox& getBounds() const { return bounds_; }
    size_t triangleCount() const { return triangles_.size(); }
    bool empty() const { return triangles_.empty(); }

    // Clear and reset
    void clear() {
        triangles_.clear();
        bounds_ = BoundingBox();
    }

    // Validate mesh integrity
    // Checks for degenerate triangles (zero area) that could cause
    // division-by-zero in slicing calculations.
    size_t countDegenerateTriangles() const {
        size_t count = 0;
        for (const auto& tri : triangles_) {
            if (tri.normal.length() < 1e-8f) {
                count++;
            }
        }
        return count;
    }

private:
    std::vector<Triangle> triangles_;
    BoundingBox bounds_;
};

} // namespace SliceForge

#endif // SLICEFORGE_MESH_H
