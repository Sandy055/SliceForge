#ifndef SLICEFORGE_PROFILE_SERIALIZER_H
#define SLICEFORGE_PROFILE_SERIALIZER_H

// =============================================================================
// ProfileSerializer — JSON Output of Build Profiles
// =============================================================================
//
// After slicing, we need to output the contours in a format that downstream
// systems (printer firmware, build preparators) can consume. We use JSON
// because it's human-readable, widely supported, and easy to validate.
//
// Output structure:
// {
//   "buildProfile": {
//     "layerHeight": 0.2,
//     "totalLayers": 150,
//     "boundingBox": { "min": [x,y,z], "max": [x,y,z] },
//     "layers": [
//       {
//         "z": 0.1,
//         "contours": [
//           { "points": [[x,y], [x,y], ...], "closed": true }
//         ]
//       }
//     ]
//   }
// }
//
// WHY NO EXTERNAL JSON LIBRARY?
// We could use nlohmann/json or rapidjson, but for output-only serialization
// it's simpler to write our own. This avoids a dependency and demonstrates
// understanding of the JSON format. In a production codebase you'd absolutely
// use a library — but for a focused project, simplicity wins.

#include "Slicer.h"
#include "Mesh.h"
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>

namespace SliceForge {

class ProfileSerializer {
public:
    struct BuildConfig {
        float layerHeight = 0.2f;
        std::string modelName = "unknown";
    };

    // Serialize to a JSON string
    static std::string toJSON(
        const std::vector<Layer>& layers,
        const Mesh& mesh,
        const BuildConfig& config
    ) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);

        const auto& bounds = mesh.getBounds();

        ss << "{\n";
        ss << "  \"buildProfile\": {\n";
        ss << "    \"modelName\": \"" << escapeJSON(config.modelName) << "\",\n";
        ss << "    \"layerHeight\": " << config.layerHeight << ",\n";
        ss << "    \"totalLayers\": " << layers.size() << ",\n";
        ss << "    \"triangleCount\": " << mesh.triangleCount() << ",\n";

        // Bounding box
        ss << "    \"boundingBox\": {\n";
        ss << "      \"min\": [" << bounds.min.x << ", "
           << bounds.min.y << ", " << bounds.min.z << "],\n";
        ss << "      \"max\": [" << bounds.max.x << ", "
           << bounds.max.y << ", " << bounds.max.z << "]\n";
        ss << "    },\n";

        // Layers
        ss << "    \"layers\": [\n";
        for (size_t i = 0; i < layers.size(); i++) {
            const auto& layer = layers[i];
            ss << "      {\n";
            ss << "        \"z\": " << layer.z << ",\n";
            ss << "        \"segmentCount\": " << layer.segments.size() << ",\n";
            ss << "        \"contours\": [\n";

            for (size_t j = 0; j < layer.contours.size(); j++) {
                const auto& contour = layer.contours[j];
                ss << "          {\n";
                ss << "            \"pointCount\": " << contour.size() << ",\n";
                ss << "            \"closed\": true,\n";
                ss << "            \"points\": [";

                for (size_t k = 0; k < contour.size(); k++) {
                    ss << "[" << contour[k].x << ", " << contour[k].y << "]";
                    if (k + 1 < contour.size()) ss << ", ";
                }

                ss << "]\n";
                ss << "          }";
                if (j + 1 < layer.contours.size()) ss << ",";
                ss << "\n";
            }

            ss << "        ]\n";
            ss << "      }";
            if (i + 1 < layers.size()) ss << ",";
            ss << "\n";
        }

        ss << "    ]\n";
        ss << "  }\n";
        ss << "}\n";

        return ss.str();
    }

    // Write directly to a file
    static void toFile(
        const std::string& filepath,
        const std::vector<Layer>& layers,
        const Mesh& mesh,
        const BuildConfig& config
    ) {
        std::string json = toJSON(layers, mesh, config);
        std::ofstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open output file: " + filepath);
        }
        file << json;
    }

private:
    // Escape special characters for JSON strings
    static std::string escapeJSON(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n";  break;
                case '\t': result += "\\t";  break;
                default:   result += c;
            }
        }
        return result;
    }
};

} // namespace SliceForge

#endif // SLICEFORGE_PROFILE_SERIALIZER_H
