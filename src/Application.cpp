#include "Application.h"
#include "FileDialog.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl2.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <unordered_set>


namespace
{
    struct CollisionOverlayEdge
    {
        std::uint32_t first = 0;
        std::uint32_t second = 0;

        bool operator==(const CollisionOverlayEdge& other) const
        {
            return
                first == other.first &&
                second == other.second;
        }
    };

    struct CollisionOverlayEdgeHash
    {
        std::size_t operator()(const CollisionOverlayEdge& edge) const
        {
            const std::size_t firstHash =
                std::hash<std::uint32_t>{}(edge.first);

            const std::size_t secondHash =
                std::hash<std::uint32_t>{}(edge.second);

            return firstHash ^ (secondHash << 1);
        }
    };

    struct CollisionViewPoint
    {
        float x = 0.0f;
        float y = 0.0f;
        float depth = 0.0f;
    };

    CollisionOverlayEdge MakeCollisionOverlayEdge(
        std::uint32_t first,
        std::uint32_t second)
    {
        if (first > second)
        {
            std::swap(first, second);
        }

        return {first, second};
    }

    bool IsFiniteCollisionPoint(const Vec3& point)
    {
        return
            std::isfinite(point.x) &&
            std::isfinite(point.y) &&
            std::isfinite(point.z);
    }

    bool ClipLineToRectangle(
        float left,
        float top,
        float right,
        float bottom,
        ImVec2& first,
        ImVec2& second)
    {
        const float deltaX = second.x - first.x;
        const float deltaY = second.y - first.y;

        const float p[4] = {
            -deltaX,
            deltaX,
            -deltaY,
            deltaY
        };

        const float q[4] = {
            first.x - left,
            right - first.x,
            first.y - top,
            bottom - first.y
        };

        float minimum = 0.0f;
        float maximum = 1.0f;

        for (int index = 0; index < 4; ++index)
        {
            if (std::abs(p[index]) <= 0.000001f)
            {
                if (q[index] < 0.0f)
                {
                    return false;
                }

                continue;
            }

            const float ratio = q[index] / p[index];

            if (p[index] < 0.0f)
            {
                minimum = std::max(minimum, ratio);
            }
            else
            {
                maximum = std::min(maximum, ratio);
            }

            if (minimum > maximum)
            {
                return false;
            }
        }

        const ImVec2 originalFirst = first;

        first.x = originalFirst.x + deltaX * minimum;
        first.y = originalFirst.y + deltaY * minimum;

        second.x = originalFirst.x + deltaX * maximum;
        second.y = originalFirst.y + deltaY * maximum;

        return true;
    }
}

bool Application::Initialize()
{
    if (!glfwInit())
    {
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    window = glfwCreateWindow(1280, 820, "ODFF v0.1.9", nullptr, nullptr);
    if (window == nullptr)
    {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetWindowUserPointer(window, this);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetDropCallback(window, DropCallback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    return true;
}

int Application::Run()
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawMainMenu();

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);

        const ImGuiWindowFlags rootFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_MenuBar;

        ImGui::Begin("ODFFRoot", nullptr, rootFlags);

        DrawToolbar();

        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        const float contentHeight = ImGui::GetContentRegionAvail().y - statusHeight;

        ImGui::BeginChild("Content", ImVec2(0.0f, contentHeight), false);

        const bool groupMode = documents.size() > 1;

        if (groupMode)
        {
            ImGui::BeginChild("ModelList", ImVec2(240.0f, 0.0f), true);
            DrawModelList();
            ImGui::EndChild();
            ImGui::SameLine();
        }

        const float propertiesWidth = 290.0f;
        ImGui::BeginChild("ViewportArea", ImVec2(-propertiesWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f), true);
        DrawViewport();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("Properties", ImVec2(propertiesWidth, 0.0f), true);
        DrawProperties();
        ImGui::EndChild();

        ImGui::EndChild();

        DrawStatusBar();
        ImGui::End();

        ImGui::Render();

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.52f, 0.53f, 0.56f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    return 0;
}

void Application::Shutdown()
{
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (window != nullptr)
    {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    glfwTerminate();
}

void Application::DrawMainMenu()
{
}

void Application::DrawToolbar()
{
    if (ImGui::Button("Open DFF"))
    {
        LoadDffPaths(FileDialog::OpenDffFiles(false), true);
    }

    ImGui::SameLine();

    if (ImGui::Button("Open Folder"))
    {
        LoadDffFolder(FileDialog::SelectDffFolder());
    }

    ImGui::SameLine();

    if (ImGui::Button("Open TXD"))
    {
        LoadTxd(FileDialog::OpenTxdFile());
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    ImGui::Checkbox("Wireframe", &wireframe);
    ImGui::SameLine();
    ImGui::Checkbox("Collision", &showCollision);
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &showGrid);

    ImGui::SameLine();

    if (ImGui::Button("About"))
    {
#ifdef _WIN32
        MessageBoxA(
            nullptr,
            "A program for embedding SAMP collision in DFFs\n\n"
            "https://github.com/spicybung\n\n"
            "Reigns Studios\n\n"
            "v 0.1.9 2026",
            "About ODFF",
            MB_OK | MB_ICONINFORMATION);
#endif
    }
}

void Application::DrawModelList()
{
    ImGui::TextUnformatted("DFF Group");
    ImGui::Separator();

    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        const bool selected = index == selectedIndex;

        if (ImGui::Selectable(documents[index]->displayName.c_str(), selected))
        {
            selectedIndex = index;
            camera.Frame(documents[index]->model.bounds);
        }

        if (selected)
        {
            ImGui::SetItemDefaultFocus();
        }
    }
}

void Application::DrawProperties()
{
    ModelDocument* document = SelectedDocument();

    ImGui::TextUnformatted("Model");
    ImGui::Separator();

    if (document == nullptr)
    {
        ImGui::TextDisabled("No DFF loaded.");
    }
    else
    {
        ImGui::TextWrapped("%s", document->displayName.c_str());
        ImGui::Text("Geometries: %zu", document->model.geometries.size());
        ImGui::Text("Frames: %zu", document->model.frames.size());
        ImGui::Text("Atomics: %zu", document->model.atomics.size());

        std::size_t vertexCount = 0;
        std::size_t triangleCount = 0;

        for (const Geometry& geometry : document->model.geometries)
        {
            vertexCount += geometry.vertices.size();
            triangleCount += geometry.triangles.size();
        }

        ImGui::Text("Vertices: %zu", vertexCount);
        ImGui::Text("Triangles: %zu", triangleCount);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Make Collision");
    ImGui::Separator();

    int mode = static_cast<int>(collisionMode);
    ImGui::RadioButton("Empty", &mode, static_cast<int>(CollisionMode::Empty));
    ImGui::RadioButton("Box", &mode, static_cast<int>(CollisionMode::Box));
    ImGui::RadioButton("Mesh Faces", &mode, static_cast<int>(CollisionMode::MeshFaces));
    collisionMode = static_cast<CollisionMode>(mode);

    ImGui::Checkbox("Optimize", &optimizeCollision);

    if (document == nullptr)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Attach Collision", ImVec2(-1.0f, 0.0f)))
    {
        AttachCollisionToSelected();
    }

    if (documents.size() <= 1)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button(
            "Attach Collision to All",
            ImVec2(-1.0f, 0.0f)))
    {
        AttachCollisionToAll();
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    {
        ImGui::SetTooltip(
            "Attach collision to DFF group");
    }

    if (documents.size() <= 1)
    {
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Export");
    ImGui::Separator();

    if (ImGui::Button("Export DFF", ImVec2(-1.0f, 0.0f)))
    {
        ExportSelectedDff();
    }

    if (documents.size() <= 1)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Export DFF Group", ImVec2(-1.0f, 0.0f)))
    {
        ExportDffGroup();
    }

    if (documents.size() <= 1)
    {
        ImGui::EndDisabled();
    }

    if (document == nullptr)
    {
        ImGui::EndDisabled();
    }

    if (document != nullptr && document->hasCollision)
    {
        ImGui::Text("COL vertices: %zu", document->collision.vertices.size());
        ImGui::Text("COL faces: %zu", document->collision.faces.size());
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("TXD");
    ImGui::Separator();

    if (txd.sourcePath.empty())
    {
        ImGui::TextDisabled("No TXD loaded.");
    }
    else
    {
        ImGui::TextWrapped("%s", txd.sourcePath.filename().string().c_str());
        ImGui::Text("Textures: %zu", txd.textures.size());

        ImGui::BeginChild("TextureList", ImVec2(0.0f, 180.0f), true);
        for (const TxdTextureInfo& texture : txd.textures)
        {
            ImGui::Text("%s", texture.name.empty() ? "<unnamed>" : texture.name.c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "%ux%u, %u-bit, %u mipmaps, platform %u",
                    texture.width,
                    texture.height,
                    texture.depth,
                    texture.mipmapCount,
                    texture.platformId);
            }
        }
        ImGui::EndChild();
    }
}

void Application::DrawStatusBar()
{
    ImGui::Separator();
    ImGui::TextUnformatted(statusText.c_str());
}

void Application::DrawViewport()
{
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 viewportPosition = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(
        "OpenGLViewport",
        available,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonMiddle);

    viewportHovered = ImGui::IsItemHovered();

    const ImVec2 viewportMinimum = ImGui::GetItemRectMin();
    const ImVec2 viewportMaximum = ImGui::GetItemRectMax();
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

    const int rawX = static_cast<int>(
        (viewportMinimum.x - mainViewport->Pos.x) * framebufferScale.x);

    const int rawYTop = static_cast<int>(
        (viewportMinimum.y - mainViewport->Pos.y) * framebufferScale.y);

    const int rawWidth = static_cast<int>(
        (viewportMaximum.x - viewportMinimum.x) * framebufferScale.x);

    const int rawHeight = static_cast<int>(
        (viewportMaximum.y - viewportMinimum.y) * framebufferScale.y);

    const int rawY = framebufferHeight - rawYTop - rawHeight;

    viewportPixelX = std::clamp(rawX, 0, framebufferWidth);
    viewportPixelY = std::clamp(rawY, 0, framebufferHeight);
    viewportPixelWidth = std::clamp(
        rawWidth,
        0,
        std::max(framebufferWidth - viewportPixelX, 0));

    viewportPixelHeight = std::clamp(
        rawHeight,
        0,
        std::max(framebufferHeight - viewportPixelY, 0));

    HandleViewportInput();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCallback(
        [](const ImDrawList*, const ImDrawCmd* command)
        {
            const auto* application = static_cast<const Application*>(
                command->UserCallbackData);

            const int x = application->viewportPixelX;
            const int y = application->viewportPixelY;
            const int width = application->viewportPixelWidth;
            const int height = application->viewportPixelHeight;

            if (width <= 0 || height <= 0)
            {
                return;
            }

            glPushAttrib(GL_ALL_ATTRIB_BITS);

            GLint previousMatrixMode = GL_MODELVIEW;
            glGetIntegerv(GL_MATRIX_MODE, &previousMatrixMode);

            glMatrixMode(GL_PROJECTION);
            glPushMatrix();

            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();

            glEnable(GL_SCISSOR_TEST);
            glScissor(x, y, width, height);

            glClearColor(
                0.8666667f,
                0.8823529f,
                0.9803922f,
                1.0f);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            application->renderer.Render(
                application->SelectedDocument(),
                application->camera,
                x,
                y,
                width,
                height,
                application->wireframe,
                application->showCollision,
                application->showGrid);

            glMatrixMode(GL_MODELVIEW);
            glPopMatrix();

            glMatrixMode(GL_PROJECTION);
            glPopMatrix();

            glMatrixMode(previousMatrixMode);
            glPopAttrib();
        },
        this);

    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    DrawCollisionOverlay(
        viewportMinimum.x,
        viewportMinimum.y,
        viewportMaximum.x,
        viewportMaximum.y);

}


void Application::DrawCollisionOverlay(
    float left,
    float top,
    float right,
    float bottom)
{
    const ModelDocument* document = SelectedDocument();

    if (!showCollision ||
        document == nullptr ||
        !document->hasCollision ||
        document->collision.mode == CollisionMode::Empty ||
        right - left <= 1.0f ||
        bottom - top <= 1.0f)
    {
        return;
    }

    const CollisionData& collision = document->collision;

    if (collision.vertices.empty())
    {
        return;
    }

    std::unordered_set<
        CollisionOverlayEdge,
        CollisionOverlayEdgeHash> edges;

    if (collision.mode == CollisionMode::Box &&
        collision.vertices.size() >= 8)
    {
        const std::uint32_t boxEdges[][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7}
        };

        for (const auto& edge : boxEdges)
        {
            edges.insert(MakeCollisionOverlayEdge(
                edge[0],
                edge[1]));
        }
    }
    else
    {
        edges.reserve(collision.faces.size() * 3);

        for (const CollisionFace& face : collision.faces)
        {
            if (face.a >= collision.vertices.size() ||
                face.b >= collision.vertices.size() ||
                face.c >= collision.vertices.size())
            {
                continue;
            }

            edges.insert(MakeCollisionOverlayEdge(
                face.a,
                face.b));

            edges.insert(MakeCollisionOverlayEdge(
                face.b,
                face.c));

            edges.insert(MakeCollisionOverlayEdge(
                face.c,
                face.a));
        }
    }

    if (edges.empty())
    {
        return;
    }

    const Vec3 eye = camera.Position();
    const Vec3 cameraForward = camera.Forward();
    const Vec3 cameraRight = camera.Right();
    const Vec3 cameraUp = camera.Up();

    const float viewportWidth = right - left;
    const float viewportHeight = bottom - top;
    const float aspect = viewportWidth / viewportHeight;

    constexpr float fieldOfViewDegrees = 55.0f;
    constexpr float pi = 3.14159265358979323846f;

    const float tangentHalfFieldOfView = std::tan(
        fieldOfViewDegrees * pi / 360.0f);

    float sceneRadius = 1.0f;

    if (document->model.bounds.valid)
    {
        sceneRadius = std::max(
            document->model.bounds.Radius(),
            0.01f);
    }

    const float nearPlane = std::max({
        0.001f,
        camera.distance * 0.001f,
        sceneRadius * 0.0005f
    });

    auto toViewPoint = [&](const Vec3& point)
    {
        const Vec3 relative = point - eye;

        return CollisionViewPoint{
            Dot(relative, cameraRight),
            Dot(relative, cameraUp),
            Dot(relative, cameraForward)
        };
    };

    auto project = [&](const CollisionViewPoint& point)
    {
        const float normalizedX =
            point.x /
            (point.depth * tangentHalfFieldOfView * aspect);

        const float normalizedY =
            point.y /
            (point.depth * tangentHalfFieldOfView);

        return ImVec2(
            left + (normalizedX + 1.0f) * viewportWidth * 0.5f,
            top + (1.0f - normalizedY) * viewportHeight * 0.5f);
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(
        ImVec2(left, top),
        ImVec2(right, bottom),
        true);

    const ImU32 collisionColor =
        IM_COL32(220, 30, 48, 255);

    for (const CollisionOverlayEdge& edge : edges)
    {
        if (edge.first >= collision.vertices.size() ||
            edge.second >= collision.vertices.size())
        {
            continue;
        }

        const Vec3& worldFirst =
            collision.vertices[edge.first];

        const Vec3& worldSecond =
            collision.vertices[edge.second];

        if (!IsFiniteCollisionPoint(worldFirst) ||
            !IsFiniteCollisionPoint(worldSecond))
        {
            continue;
        }

        CollisionViewPoint viewFirst =
            toViewPoint(worldFirst);

        CollisionViewPoint viewSecond =
            toViewPoint(worldSecond);

        if (viewFirst.depth < nearPlane &&
            viewSecond.depth < nearPlane)
        {
            continue;
        }

        if (viewFirst.depth < nearPlane ||
            viewSecond.depth < nearPlane)
        {
            CollisionViewPoint* behind =
                viewFirst.depth < nearPlane
                    ? &viewFirst
                    : &viewSecond;

            const CollisionViewPoint* inFront =
                viewFirst.depth < nearPlane
                    ? &viewSecond
                    : &viewFirst;

            const float denominator =
                inFront->depth - behind->depth;

            if (std::abs(denominator) <= 0.000001f)
            {
                continue;
            }

            const float amount =
                (nearPlane - behind->depth) /
                denominator;

            behind->x +=
                (inFront->x - behind->x) * amount;

            behind->y +=
                (inFront->y - behind->y) * amount;

            behind->depth = nearPlane;
        }

        ImVec2 screenFirst = project(viewFirst);
        ImVec2 screenSecond = project(viewSecond);

        if (!std::isfinite(screenFirst.x) ||
            !std::isfinite(screenFirst.y) ||
            !std::isfinite(screenSecond.x) ||
            !std::isfinite(screenSecond.y))
        {
            continue;
        }

        if (!ClipLineToRectangle(
                left,
                top,
                right,
                bottom,
                screenFirst,
                screenSecond))
        {
            continue;
        }

        drawList->AddLine(
            screenFirst,
            screenSecond,
            collisionColor,
            1.25f);
    }

    drawList->PopClipRect();
}

void Application::HandleViewportInput()
{
    if (!viewportHovered)
    {
        firstCursorSample = true;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    if (pendingScroll != 0.0f)
    {
        camera.Zoom(pendingScroll);
        pendingScroll = 0.0f;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F))
    {
        const ModelDocument* document = SelectedDocument();
        if (document != nullptr)
        {
            camera.Frame(document->model.bounds);
        }
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);

    if (firstCursorSample)
    {
        lastCursorX = cursorX;
        lastCursorY = cursorY;
        firstCursorSample = false;
    }

    const float deltaX = static_cast<float>(cursorX - lastCursorX);
    const float deltaY = static_cast<float>(cursorY - lastCursorY);

    if (io.MouseDown[0])
    {
        camera.Orbit(deltaX, deltaY);
    }

    if (io.MouseDown[2])
    {
        camera.Pan(deltaX, deltaY);
    }

    lastCursorX = cursorX;
    lastCursorY = cursorY;
}

void Application::LoadDffPaths(
    const std::vector<std::filesystem::path>& paths,
    bool replace)
{
    if (paths.empty())
    {
        return;
    }

    if (replace)
    {
        documents.clear();
        selectedIndex = 0;
    }

    std::size_t loadedCount = 0;
    std::ostringstream failures;

    for (const std::filesystem::path& path : paths)
    {
        ModelData model{};
        std::string error;

        if (!dffReader.LoadDff(path, model, error))
        {
            failures << path.filename().string() << ": " << error << "  ";
            continue;
        }

        std::vector<std::uint8_t> sourceBytes;

        if (!ReadSourceBytes(path, sourceBytes, error))
        {
            failures << path.filename().string()
                     << ": " << error << "  ";
            continue;
        }

        auto document = std::make_unique<ModelDocument>();
        document->sourcePath = path;
        document->displayName = path.filename().string();
        document->sourceBytes = std::move(sourceBytes);
        document->model = std::move(model);

        documents.push_back(std::move(document));
        ++loadedCount;
    }

    if (!documents.empty())
    {
        selectedIndex = std::min(selectedIndex, documents.size() - 1);
        camera.Frame(documents[selectedIndex]->model.bounds);
    }

    std::ostringstream status;
    status << "Loaded " << loadedCount << " DFF file";
    if (loadedCount != 1)
    {
        status << "s";
    }

    if (!failures.str().empty())
    {
        status << ". Failed: " << failures.str();
    }

    SetStatus(status.str());
}

void Application::LoadDffFolder(const std::filesystem::path& folder)
{
    if (folder.empty())
    {
        return;
    }

    std::vector<std::filesystem::path> paths;

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(folder))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        std::string extension = entry.path().extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });

        if (extension == ".dff")
        {
            paths.push_back(entry.path());
        }
    }

    std::sort(paths.begin(), paths.end());
    LoadDffPaths(paths, true);
}

void Application::LoadTxd(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return;
    }

    std::string error;
    TxdData loaded{};

    if (!txdReader.Load(path, loaded, error))
    {
        SetStatus("TXD load failed: " + error);
        return;
    }

    txd = std::move(loaded);

    std::ostringstream status;
    status << "Loaded TXD " << path.filename().string()
           << " with " << txd.textures.size() << " texture";
    if (txd.textures.size() != 1)
    {
        status << "s";
    }

    SetStatus(status.str());
}

void Application::AttachCollisionToSelected()
{
    ModelDocument* document = SelectedDocument();
    if (document == nullptr)
    {
        return;
    }

    document->collision = collisionBuilder.Build(
        document->model,
        collisionMode,
        optimizeCollision);

    document->hasCollision = true;

    std::ostringstream status;
    status << "Attached collision to " << document->displayName
           << ": " << document->collision.vertices.size() << " vertices, "
           << document->collision.faces.size() << " faces.";

    SetStatus(status.str());
}

void Application::AttachCollisionToAll()
{
    for (std::unique_ptr<ModelDocument>& document : documents)
    {
        document->collision = collisionBuilder.Build(
            document->model,
            collisionMode,
            optimizeCollision);

        document->hasCollision = true;
    }

    std::ostringstream status;
    status << "Attached collision to all "
           << documents.size()
           << " DFF files.";
    SetStatus(status.str());
}


void Application::ExportSelectedDff()
{
    ModelDocument* document = SelectedDocument();

    if (document == nullptr)
    {
        return;
    }

    const std::filesystem::path outputPath =
        FileDialog::SaveDffFile(document->displayName);

    if (outputPath.empty())
    {
        return;
    }

    std::string error;

    if (!dffExporter.ExportDocument(
            *document,
            outputPath,
            error))
    {
        SetStatus("DFF export failed: " + error);
        return;
    }

    if (document->hasCollision)
    {
        SetStatus(
            "Exported DFF with embedded SA-MP COL3: " +
            outputPath.string());
    }
    else
    {
        SetStatus(
            "Exported DFF: " +
            outputPath.string());
    }
}

void Application::ExportDffGroup()
{
    if (documents.empty())
    {
        return;
    }

    const std::filesystem::path outputFolder =
        FileDialog::SelectExportFolder();

    if (outputFolder.empty())
    {
        return;
    }

    std::unordered_set<std::string> exportedNames;
    std::size_t exportedCount = 0;
    std::size_t skippedDuplicates = 0;
    std::ostringstream failures;

    for (const std::unique_ptr<ModelDocument>& document : documents)
    {
        std::string key = document->sourcePath.filename().string();

        std::transform(
            key.begin(),
            key.end(),
            key.begin(),
            [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });

        if (!exportedNames.insert(key).second)
        {
            ++skippedDuplicates;
            continue;
        }

        const std::filesystem::path outputPath =
            outputFolder / document->sourcePath.filename();

        std::string error;

        if (!dffExporter.ExportDocument(
                *document,
                outputPath,
                error))
        {
            failures << document->displayName
                     << ": " << error << "  ";
            continue;
        }

        ++exportedCount;
    }

    std::ostringstream status;
    status << "Exported " << exportedCount
           << " DFF file";

    if (exportedCount != 1)
    {
        status << "s";
    }

    status << " to " << outputFolder.string();

    if (skippedDuplicates != 0)
    {
        status << ". Skipped "
               << skippedDuplicates
               << " duplicate filename";

        if (skippedDuplicates != 1)
        {
            status << "s";
        }
    }

    if (!failures.str().empty())
    {
        status << ". Failed: " << failures.str();
    }

    SetStatus(status.str());
}

bool Application::ReadSourceBytes(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& bytes,
    std::string& error) const
{
    std::ifstream stream(
        path,
        std::ios::binary | std::ios::ate);

    if (!stream)
    {
        error = "Could not reopen the DFF to retain its original bytes.";
        return false;
    }

    const std::streampos endPosition = stream.tellg();

    if (endPosition < 0)
    {
        error = "Could not determine the source DFF size.";
        return false;
    }

    const std::size_t size =
        static_cast<std::size_t>(endPosition);

    bytes.resize(size);
    stream.seekg(0, std::ios::beg);

    if (size != 0)
    {
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size));
    }

    if (!stream)
    {
        error = "Could not read the complete source DFF.";
        bytes.clear();
        return false;
    }

    return true;
}


ModelDocument* Application::SelectedDocument()
{
    if (documents.empty() || selectedIndex >= documents.size())
    {
        return nullptr;
    }

    return documents[selectedIndex].get();
}

const ModelDocument* Application::SelectedDocument() const
{
    if (documents.empty() || selectedIndex >= documents.size())
    {
        return nullptr;
    }

    return documents[selectedIndex].get();
}

void Application::SetStatus(std::string text)
{
    statusText = std::move(text);
}

void Application::ScrollCallback(GLFWwindow* callbackWindow, double, double yOffset)
{
    auto* application = static_cast<Application*>(
        glfwGetWindowUserPointer(callbackWindow));

    if (application != nullptr && application->viewportHovered)
    {
        application->pendingScroll += static_cast<float>(yOffset);
    }
}

void Application::DropCallback(
    GLFWwindow* callbackWindow,
    int count,
    const char** paths)
{
    auto* application = static_cast<Application*>(
        glfwGetWindowUserPointer(callbackWindow));

    if (application == nullptr || count <= 0)
    {
        return;
    }

    std::vector<std::filesystem::path> dffPaths;
    std::filesystem::path txdPath;

    for (int index = 0; index < count; ++index)
    {
        const std::filesystem::path path(paths[index]);

        std::string extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });

        if (extension == ".dff")
        {
            dffPaths.push_back(path);
        }
        else if (extension == ".txd")
        {
            txdPath = path;
        }
    }

    if (!dffPaths.empty())
    {
        application->LoadDffPaths(dffPaths, true);
    }

    if (!txdPath.empty())
    {
        application->LoadTxd(txdPath);
    }
}
