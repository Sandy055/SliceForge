// =============================================================================
// SliceForge QML UI — Main Interface
// =============================================================================
//
// Layout:
// ┌──────────────────────────────────────────────┐
// │  Toolbar: [Open File] [Slice] [Export]        │
// ├───────────────────┬──────────────────────────┤
// │  Model Info       │  Layer Preview (Canvas)   │
// │  - triangles      │  - draws contours at      │
// │  - dimensions     │    current layer height   │
// │  - layer height   │                           │
// │                   │                           │
// │  Slice Info       │                           │
// │  - layers         │                           │
// │  - time           │                           │
// │  - threads        │                           │
// ├───────────────────┴──────────────────────────┤
// │  Layer Slider: [0 ========●======= 200]       │
// └──────────────────────────────────────────────┘
//
// The Canvas element draws 2D contours by querying the C++ bridge
// for point data at the current layer index.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width: 960
    height: 720
    title: "SliceForge — 3D Geometry Slicer"
    color: "#1a1a2e"

    // ── State ──
    property int currentLayer: 0
    property double layerHeight: 0.2

    // ── Top Toolbar ──
    header: ToolBar {
        background: Rectangle { color: "#16213e" }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 12

            Button {
                text: "Open Model"
                icon.name: "document-open"
                onClicked: fileDialog.open()
            }

            // Layer height input
            Label {
                text: "Layer height (mm):"
                color: "#a0a0b8"
            }
            SpinBox {
                id: layerHeightSpin
                from: 1       // 0.01mm
                to: 100       // 1.0mm
                value: 20     // 0.2mm
                stepSize: 5
                property real realValue: value / 100.0
                textFromValue: function(value) {
                    return (value / 100.0).toFixed(2);
                }
                valueFromText: function(text) {
                    return Math.round(parseFloat(text) * 100);
                }
                onValueChanged: root.layerHeight = realValue
            }

            Button {
                text: "Slice"
                enabled: sliceBridge.meshLoaded
                onClicked: {
                    sliceBridge.sliceMesh(root.layerHeight);
                    root.currentLayer = 0;
                }
                highlighted: true
            }

            Button {
                text: "Export JSON"
                enabled: sliceBridge.layerCount > 0
                onClicked: saveDialog.open()
            }

            Item { Layout.fillWidth: true }

            Label {
                text: sliceBridge.modelName || "No model loaded"
                color: "#e0e0e8"
                font.bold: true
            }
        }
    }

    // ── File Dialogs ──
    FileDialog {
        id: fileDialog
        title: "Open 3D Model"
        nameFilters: ["3D Models (*.stl *.obj)", "STL Files (*.stl)", "OBJ Files (*.obj)"]
        onAccepted: {
            sliceBridge.loadModel(selectedFile.toString());
        }
    }

    FileDialog {
        id: saveDialog
        title: "Export Build Profile"
        nameFilters: ["JSON Files (*.json)"]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            sliceBridge.exportProfile(selectedFile.toString());
        }
    }

    // ── Main Content ──
    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // ── Left Panel: Model & Slice Info ──
        ColumnLayout {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            spacing: 12

            // Model info card
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: infoCol.implicitHeight + 24
                color: "#16213e"
                radius: 8

                Column {
                    id: infoCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    Label {
                        text: "Model Info"
                        font.bold: true
                        font.pixelSize: 14
                        color: "#e0e0e8"
                    }

                    Label {
                        text: "Triangles: " + sliceBridge.triangleCount
                        color: "#a0a0b8"
                        font.pixelSize: 13
                    }
                    Label {
                        text: "Height: " + sliceBridge.modelHeight.toFixed(1) + " mm"
                        color: "#a0a0b8"
                        font.pixelSize: 13
                    }
                }
            }

            // Slice info card
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: sliceCol.implicitHeight + 24
                color: "#16213e"
                radius: 8
                visible: sliceBridge.layerCount > 0

                Column {
                    id: sliceCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    Label {
                        text: "Slice Results"
                        font.bold: true
                        font.pixelSize: 14
                        color: "#e0e0e8"
                    }
                    Label {
                        text: "Layers: " + sliceBridge.layerCount
                        color: "#a0a0b8"
                        font.pixelSize: 13
                    }
                    Label {
                        text: "Time: " + sliceBridge.sliceTimeMs.toFixed(2) + " ms"
                        color: "#a0a0b8"
                        font.pixelSize: 13
                    }
                    Label {
                        text: "Threads: " + sliceBridge.threadsUsed
                        color: "#a0a0b8"
                        font.pixelSize: 13
                    }
                    Label {
                        text: "Layer height: " + root.layerHeight.toFixed(2) + " mm"
                        color: "#a0a0b8"
                        font.pixelSize: 13
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        // ── Right Panel: Layer Preview Canvas ──
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            // Canvas for drawing contours
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#0f0f23"
                radius: 8

                Canvas {
                    id: layerCanvas
                    anchors.fill: parent
                    anchors.margins: 16

                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.clearRect(0, 0, width, height);

                        if (sliceBridge.layerCount === 0) {
                            // Draw placeholder text
                            ctx.fillStyle = "#555570";
                            ctx.font = "16px sans-serif";
                            ctx.textAlign = "center";
                            ctx.fillText(
                                sliceBridge.meshLoaded
                                    ? "Click 'Slice' to generate layers"
                                    : "Open a 3D model to begin",
                                width / 2, height / 2
                            );
                            return;
                        }

                        // Get contour data from C++ bridge
                        var contours = sliceBridge.getLayerContours(root.currentLayer);
                        var bounds = sliceBridge.getBounds();
                        if (contours.length === 0) return;

                        // Compute scale to fit contours in canvas
                        var modelW = bounds.maxX - bounds.minX;
                        var modelH = bounds.maxY - bounds.minY;
                        var scale = Math.min(
                            (width - 40) / modelW,
                            (height - 40) / modelH
                        );
                        var offsetX = (width - modelW * scale) / 2;
                        var offsetY = (height - modelH * scale) / 2;

                        // Draw grid
                        ctx.strokeStyle = "#1a1a3e";
                        ctx.lineWidth = 0.5;
                        var gridSize = 10 * scale;
                        for (var gx = offsetX % gridSize; gx < width; gx += gridSize) {
                            ctx.beginPath();
                            ctx.moveTo(gx, 0);
                            ctx.lineTo(gx, height);
                            ctx.stroke();
                        }
                        for (var gy = offsetY % gridSize; gy < height; gy += gridSize) {
                            ctx.beginPath();
                            ctx.moveTo(0, gy);
                            ctx.lineTo(width, gy);
                            ctx.stroke();
                        }

                        // Draw each contour
                        for (var c = 0; c < contours.length; c++) {
                            var pts = contours[c];
                            if (pts.length < 2) continue;

                            // Fill
                            ctx.fillStyle = "rgba(0, 200, 255, 0.08)";
                            ctx.beginPath();
                            ctx.moveTo(
                                (pts[0].x - bounds.minX) * scale + offsetX,
                                height - ((pts[0].y - bounds.minY) * scale + offsetY)
                            );
                            for (var i = 1; i < pts.length; i++) {
                                ctx.lineTo(
                                    (pts[i].x - bounds.minX) * scale + offsetX,
                                    height - ((pts[i].y - bounds.minY) * scale + offsetY)
                                );
                            }
                            ctx.closePath();
                            ctx.fill();

                            // Stroke
                            ctx.strokeStyle = "#00c8ff";
                            ctx.lineWidth = 1.5;
                            ctx.stroke();
                        }

                        // Layer label
                        var z = sliceBridge.getLayerZ(root.currentLayer);
                        ctx.fillStyle = "#a0a0b8";
                        ctx.font = "12px monospace";
                        ctx.textAlign = "left";
                        ctx.fillText(
                            "Layer " + (root.currentLayer + 1) +
                            " / " + sliceBridge.layerCount +
                            "  z = " + z.toFixed(2) + " mm",
                            8, height - 8
                        );
                    }
                }
            }

            // Layer slider
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                visible: sliceBridge.layerCount > 0

                Label {
                    text: "Layer:"
                    color: "#a0a0b8"
                }

                Slider {
                    id: layerSlider
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(0, sliceBridge.layerCount - 1)
                    stepSize: 1
                    value: root.currentLayer
                    onValueChanged: {
                        root.currentLayer = value;
                        layerCanvas.requestPaint();
                    }
                }

                Label {
                    text: (root.currentLayer + 1) + " / " + sliceBridge.layerCount
                    color: "#e0e0e8"
                    font.family: "monospace"
                }
            }
        }
    }

    // Repaint canvas when slice completes
    Connections {
        target: sliceBridge
        function onSliceComplete() {
            layerCanvas.requestPaint();
        }
        function onMeshChanged() {
            layerCanvas.requestPaint();
        }
    }
}
