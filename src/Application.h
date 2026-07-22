#pragma once

#include "Camera.h"
#include "CollisionBuilder.h"
#include "DffExporter.h"
#include "ModelDocument.h"
#include "OpenGLRenderer.h"
#include "RenderWareReader.h"
#include "TxdReader.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

class Application
{
public:
    bool Initialize();
    int Run();
    void Shutdown();

private:
    void DrawMainMenu();
    void DrawToolbar();
    void DrawModelList();
    void DrawProperties();
    void DrawStatusBar();
    void DrawViewport();
    void DrawCollisionOverlay(
        float left,
        float top,
        float right,
        float bottom);
    void Draw2DFXOverlay(
        float left,
        float top,
        float right,
        float bottom);
    void HandleViewportInput();

    void LoadDffPaths(
        const std::vector<std::filesystem::path>& paths,
        bool replace);

    void LoadDffFolder(
        const std::filesystem::path& folder);

    void LoadTxd(
        const std::filesystem::path& path);

    void FindAndLoadMatchingTxd(
        const std::vector<std::filesystem::path>& dffPaths);

    void AttachCollisionToSelected();
    void AttachCollisionToAll();
    void DetachCollisionFromSelected();
    void DetachCollisionFromAll();
    void ExportSelectedDff();
    void ExportDffGroup();

    bool ReadSourceBytes(
        const std::filesystem::path& path,
        std::vector<std::uint8_t>& bytes,
        std::string& error) const;

    ModelDocument* SelectedDocument();
    const ModelDocument* SelectedDocument() const;
    void SetStatus(std::string text);

    static void ScrollCallback(
        GLFWwindow* window,
        double xOffset,
        double yOffset);

    static void DropCallback(
        GLFWwindow* window,
        int count,
        const char** paths);

    GLFWwindow* window = nullptr;

    RenderWareReader dffReader;
    TxdReader txdReader;
    OpenGLRenderer renderer;
    CollisionBuilder collisionBuilder;
    DffExporter dffExporter;
    Camera camera;

    std::vector<std::unique_ptr<ModelDocument>> documents;
    std::size_t selectedIndex = 0;
    TxdData txd;

    CollisionMode collisionMode = CollisionMode::Box;
    bool wireframe = false;
    bool showCollision = true;
    bool showEffects2D = true;
    bool showGrid = true;

    bool viewportHovered = false;
    int viewportPixelX = 0;
    int viewportPixelY = 0;
    int viewportPixelWidth = 1;
    int viewportPixelHeight = 1;

    float pendingScroll = 0.0f;
    std::string statusText = "Ready";

    double lastCursorX = 0.0;
    double lastCursorY = 0.0;
    bool firstCursorSample = true;
};
