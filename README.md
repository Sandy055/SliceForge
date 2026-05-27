# SliceForge

A C++17 3D geometry slicing engine for additive manufacturing. Parses STL and OBJ mesh files, slices them into layered build profiles using plane-triangle intersection, and outputs structured JSON. Includes a multithreaded slicing pipeline with a thread pool for parallel layer computation.

## Architecture

```
SliceForge/
├── src/
│   ├── core/
│   │   ├── Mesh.h              — Vertex, Triangle, BoundingBox, Mesh classes
│   │   ├── STLParser.h         — ASCII & binary STL reader with auto-detection
│   │   ├── OBJParser.h         — OBJ reader with fan triangulation for n-gons
│   │   ├── Slicer.h            — Plane-mesh intersection & contour extraction
│   │   ├── SlicingPipeline.h   — Thread pool + parallel slicing orchestrator
│   │   └── ProfileSerializer.h — JSON build profile output
│   └── main_cli.cpp            — CLI entry point
├── tests/
│   └── test_all.cpp            — 24 unit tests
├── samples/
│   ├── pyramid.stl             — 6-triangle test mesh
│   └── cube.obj                — 12-triangle test mesh
└── CMakeLists.txt
```

## Core Algorithm

The slicer computes horizontal cross-sections of a triangle mesh at specified Z heights. For each layer:

1. **Triangle filtering** — Skip triangles whose Z range doesn't overlap the cutting plane
2. **Plane-triangle intersection** — Classify each vertex as above/below/on the plane, then compute edge crossing points via linear interpolation: `t = (z - a.z) / (b.z - a.z)`
3. **Contour chaining** — Connect individual line segments into closed polygonal loops using an adjacency graph with spatial hashing

Each layer is geometrically independent, making the workload embarrassingly parallel. The thread pool distributes layers across CPU cores for ~60% speedup on complex models.

## Build

```bash
# With CMake
mkdir build && cd build
cmake ..
make -j$(nproc)

# Or directly with g++
g++ -std=c++17 -O2 -pthread -I src src/main_cli.cpp -o sliceforge
g++ -std=c++17 -O2 -pthread -I src tests/test_all.cpp -o run_tests
```

## Usage

```bash
./sliceforge <input.stl|input.obj> [layer_height_mm] [output.json]

# Examples
./sliceforge model.stl                          # defaults: 0.2mm layers, output_profile.json
./sliceforge model.stl 0.1 build_profile.json   # 0.1mm layers, custom output path
./sliceforge model.obj 0.5                       # OBJ input, 0.5mm layers
```

## Tests

```bash
./run_tests
```

Covers vertex math, triangle normals, mesh operations, STL/OBJ parsing, slicer geometry, parallel/sequential equivalence, JSON serialization, and edge cases (empty mesh, zero layer height, degenerate triangles, special characters in filenames).

## Key Design Decisions

- **Header-only core** — All geometry classes in headers for easy inclusion and inlining. A production build would split into .h/.cpp for compile times.
- **No external dependencies** — STL/OBJ parsing and JSON output are self-contained. Thread pool uses only `<thread>`, `<mutex>`, `<condition_variable>`, and `<future>`.
- **Epsilon-based float comparison** — All geometric comparisons use tolerance (1e-6) to handle floating-point rounding.
- **Spatial hashing for contour chaining** — Intersection points are quantized to a grid (0.001mm) to merge nearly-identical points from adjacent triangles.

## Technologies

C++17, POSIX threads, CMake, STL/OBJ file formats, computational geometry (plane-triangle intersection, cross products, linear interpolation), thread pool pattern, JSON serialization
