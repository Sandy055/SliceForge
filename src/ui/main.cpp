// =============================================================================
// SliceForge Qt GUI — Application Entry Point
// =============================================================================
//
// This sets up the Qt application and loads the QML interface.
// The C++ backend (Mesh, Slicer, Pipeline) is exposed to QML through
// a "bridge" class that QML can call directly.
//
// Architecture:
//   C++ backend (core/) ←→ SliceBridge (Qt bridge) ←→ QML UI
//
// The bridge pattern keeps the core engine independent of Qt.
// You could swap the UI for a web frontend or CLI without touching
// the geometry code. This is the Dependency Inversion principle in action.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QObject>
#include <QUrl>

#include "../core/Mesh.h"
#include "../core/STLParser.h"
#include "../core/OBJParser.h"
#include "../core/Slicer.h"
#include "../core/SlicingPipeline.h"
#include "../core/ProfileSerializer.h"

using namespace SliceForge;

// =============================================================================
// SliceBridge — Connects the C++ engine to the QML UI
// =============================================================================
// QML can't call C++ directly. This class wraps our engine functions
// and exposes them as Q_INVOKABLE methods that QML can call.
//
// The Q_PROPERTY macros expose read-only state to QML's data binding
// system — when a property changes, the UI updates automatically.

class SliceBridge : public QObject {
    Q_OBJECT

    // Properties that QML can bind to (auto-update the UI when they change)
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY meshChanged)
    Q_PROPERTY(int layerCount READ layerCount NOTIFY sliceComplete)
    Q_PROPERTY(double sliceTimeMs READ sliceTimeMs NOTIFY sliceComplete)
    Q_PROPERTY(int threadsUsed READ threadsUsed NOTIFY sliceComplete)
    Q_PROPERTY(bool meshLoaded READ meshLoaded NOTIFY meshChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY meshChanged)
    Q_PROPERTY(double modelHeight READ modelHeight NOTIFY meshChanged)

public:
    explicit SliceBridge(QObject* parent = nullptr) : QObject(parent) {}

    // ── Property getters ──
    int triangleCount() const { return mesh_.triangleCount(); }
    int layerCount() const { return static_cast<int>(layers_.size()); }
    double sliceTimeMs() const { return sliceTimeMs_; }
    int threadsUsed() const { return threadsUsed_; }
    bool meshLoaded() const { return !mesh_.empty(); }
    QString modelName() const { return modelName_; }
    double modelHeight() const {
        if (mesh_.empty()) return 0.0;
        return mesh_.getBounds().depth();
    }

    // ── Methods callable from QML ──

    // Load a 3D model file (STL or OBJ)
    Q_INVOKABLE bool loadModel(const QString& filepath) {
        try {
            std::string path = filepath.toStdString();
            // Remove "file://" prefix if present (QML file dialogs add it)
            if (path.substr(0, 7) == "file://") {
                path = path.substr(7);
            }

            std::string ext = path.substr(path.find_last_of('.') + 1);
            if (ext == "stl" || ext == "STL") {
                mesh_ = STLParser::parse(path);
            } else if (ext == "obj" || ext == "OBJ") {
                mesh_ = OBJParser::parse(path);
            } else {
                return false;
            }

            modelName_ = QString::fromStdString(
                path.substr(path.find_last_of('/') + 1)
            );
            layers_.clear();
            emit meshChanged();
            return true;

        } catch (const std::exception& e) {
            return false;
        }
    }

    // Slice the loaded mesh with given layer height
    Q_INVOKABLE void sliceMesh(double layerHeight) {
        if (mesh_.empty()) return;

        SlicingPipeline::Config config;
        config.layerHeight = static_cast<float>(layerHeight);

        auto result = SlicingPipeline::sliceParallel(mesh_, config);
        layers_ = std::move(result.layers);
        sliceTimeMs_ = result.elapsedMs;
        threadsUsed_ = result.threadsUsed;

        emit sliceComplete();
    }

    // Get contour data for a specific layer (for the layer preview)
    // Returns a list of contours, each contour is a list of [x, y] points
    Q_INVOKABLE QVariantList getLayerContours(int layerIndex) {
        QVariantList result;
        if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size())) {
            return result;
        }

        const auto& layer = layers_[layerIndex];
        for (const auto& contour : layer.contours) {
            QVariantList contourPoints;
            for (const auto& pt : contour) {
                QVariantMap point;
                point["x"] = pt.x;
                point["y"] = pt.y;
                contourPoints.append(point);
            }
            result.append(contourPoints);
        }
        return result;
    }

    // Get the z-height of a specific layer
    Q_INVOKABLE double getLayerZ(int layerIndex) {
        if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size())) {
            return 0.0;
        }
        return layers_[layerIndex].z;
    }

    // Get bounding box for the viewport
    Q_INVOKABLE QVariantMap getBounds() {
        QVariantMap bounds;
        if (mesh_.empty()) return bounds;
        const auto& b = mesh_.getBounds();
        bounds["minX"] = b.min.x;
        bounds["minY"] = b.min.y;
        bounds["minZ"] = b.min.z;
        bounds["maxX"] = b.max.x;
        bounds["maxY"] = b.max.y;
        bounds["maxZ"] = b.max.z;
        return bounds;
    }

    // Export sliced profile to JSON
    Q_INVOKABLE bool exportProfile(const QString& filepath) {
        try {
            std::string path = filepath.toStdString();
            if (path.substr(0, 7) == "file://") {
                path = path.substr(7);
            }
            ProfileSerializer::BuildConfig config;
            config.modelName = modelName_.toStdString();
            ProfileSerializer::toFile(path, layers_, mesh_, config);
            return true;
        } catch (...) {
            return false;
        }
    }

signals:
    void meshChanged();
    void sliceComplete();

private:
    Mesh mesh_;
    std::vector<Layer> layers_;
    QString modelName_;
    double sliceTimeMs_ = 0.0;
    size_t threadsUsed_ = 0;
};


// ── Application Entry Point ──
int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("SliceForge");
    app.setOrganizationName("SliceForge");

    // Create the bridge and expose it to QML
    SliceBridge bridge;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("sliceBridge", &bridge);

    // Load the QML UI
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}

#include "main.moc"
