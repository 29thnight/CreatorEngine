#include "../../Lattice/Core/LXGraph.h"
#include "../../Lattice/Core/LXDocument.h"
#include "../../Lattice/Core/LXNodeDefinition.h"
#include "../../Lattice/ImGui/LXCanvas.h"
#include "MaterialGraphTests.h"
#include "../../Editor/EngineGUIWindow/MaterialGraphPresentation.h"
#include "../../Editor/ImGuiHelper/EditorIconAlignment.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <d3d11.h>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <crtdbg.h>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;
IDXGISwapChain* gSwapChain = nullptr;
ID3D11RenderTargetView* gTarget = nullptr;
UINT gResizeWidth = 0, gResizeHeight = 0;
std::filesystem::path Utf8Path(const std::string& name);

struct LibraryProbe
{
    ImVec2 searchPoint{};
    std::vector<std::pair<int, ImVec2>> results;
    std::vector<std::pair<LX::Id, ImVec2>> groups;
};

LibraryProbe* gLibraryProbe = nullptr;

struct InspectorProbe
{
    std::map<std::string, ImVec2> controls;
};

InspectorProbe* gInspectorProbe = nullptr;

struct DynamicPinProbe
{
    ImVec2 addPoint{};
    std::vector<ImVec2> removePoints;
};

DynamicPinProbe* gDynamicPinProbe = nullptr;

struct CreationProbe
{
    ImVec2 searchPoint{};
    std::vector<std::pair<int, ImVec2>> results;
};

CreationProbe* gCreationProbe = nullptr;

struct ProblemProbe
{
    ImVec2 firstPoint{};
};

ProblemProbe* gProblemProbe = nullptr;

struct GroupCollapseProbe
{
    ImVec2 buttonPoint{};
};

GroupCollapseProbe* gGroupCollapseProbe = nullptr;

struct GroupEditorState
{
    LX::Id group = 0;
    std::uint64_t baseRevision = 0;
    std::string name;
    std::vector<LX::LXGroupSocket> sockets;
    LX::PinType newInputType = LX::PinType::Color;
    LX::PinType newOutputType = LX::PinType::Color;
    std::optional<LX::LXDocument> draft;
    LX::CanvasState canvas;
    std::string message;

    bool Open(const LX::LXDocument& parent, LX::Id id)
    {
        const LX::LXGroupDefinition* definition = parent.Graph().FindGroup(id);
        if (!definition || !definition->body)
        {
            return false;
        }
        draft.emplace(*definition->body, std::string{}, true);
        group = id;
        baseRevision = parent.Revision();
        name = definition->name;
        sockets = definition->sockets;
        newInputType = LX::PinType::Color;
        newOutputType = LX::PinType::Color;
        canvas = {};
        message.clear();
        return true;
    }

    bool Apply(LX::LXDocument& parent)
    {
        if (!draft)
        {
            return false;
        }
        const LX::LXCommandResult result =
            parent.Execute(LX::LXUpdateGroup{group, name, draft->Graph(), sockets}, baseRevision);
        if (!result.applied)
        {
            const LX::LXGroupDefinition* definition = parent.Graph().FindGroup(group);
            const auto brokenInterface = [&](const LX::LXGroupSocket& socket) {
                const LX::Pin* pin = draft->Graph().FindPin(socket.internalPin);
                return !pin || pin->direction != socket.direction || pin->type != socket.type;
            };
            if (result.code == "revision_conflict")
            {
                message = "Material graph changed; reopen this group";
            }
            else if (std::any_of(sockets.begin(), sockets.end(), brokenInterface))
            {
                message = "Group interface pins must remain present with the same type";
            }
            else if (definition && definition->name == name && definition->sockets == sockets &&
                     definition->body->Equals(draft->Graph()))
            {
                message = "No group changes to apply";
            }
            else
            {
                message = "Group graph or interface is invalid; disconnect linked sockets before removal";
            }
            return false;
        }
        Cancel();
        return true;
    }

    void Cancel()
    {
        draft.reset();
        group = 0;
        name.clear();
        sockets.clear();
        canvas = {};
        message.clear();
    }
};

struct GroupEditorProbe
{
    ImVec2 openPoint{};
    ImVec2 applyPoint{};
    ImVec2 cancelPoint{};
    ImVec2 addInputPoint{};
    ImVec2 addOutputPoint{};
    ImVec2 groupSelectionPoint{};
    std::map<std::string, ImVec2> inputCandidates;
    std::map<std::string, ImVec2> outputCandidates;
    std::map<std::string, ImVec2> defaultTreePoints;
    std::map<std::string, ImVec2> defaultCheckboxPoints;
    std::map<LX::Id, ImVec2> nestedGroupPoints;
};

GroupEditorProbe* gGroupEditorProbe = nullptr;

std::string NextGroupName(const LX::LXGraph& graph)
{
    for (unsigned index = 1; index < std::numeric_limits<unsigned>::max(); ++index)
    {
        const std::string name = "Group " + std::to_string(index);
        const bool exists = std::any_of(graph.Groups().begin(), graph.Groups().end(),
                                        [&](const auto& entry) { return entry.second.name == name; });
        if (!exists)
        {
            return name;
        }
    }
    return {};
}

void ReleaseTarget()
{
    if (gTarget)
    {
        gTarget->Release();
        gTarget = nullptr;
    }
}
void CreateTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(gSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
    {
        gDevice->CreateRenderTargetView(backBuffer, nullptr, &gTarget);
        backBuffer->Release();
    }
}
bool CreateDevice(HWND window)
{
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0;
    D3D_FEATURE_LEVEL featureLevel{};
    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    const HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, requested,
                                                         ARRAYSIZE(requested), D3D11_SDK_VERSION, &description,
                                                         &gSwapChain, &gDevice, &featureLevel, &gContext);
    if (FAILED(result))
    {
        return false;
    }
    CreateTarget();
    return gTarget != nullptr;
}
void ReleaseDevice()
{
    ReleaseTarget();
    if (gSwapChain)
    {
        gSwapChain->Release();
        gSwapChain = nullptr;
    }
    if (gContext)
    {
        gContext->Release();
        gContext = nullptr;
    }
    if (gDevice)
    {
        gDevice->Release();
        gDevice = nullptr;
    }
}

bool CaptureBackBuffer(const std::string& path)
{
    using Microsoft::WRL::ComPtr;
    const auto succeeded = [](HRESULT result, const char* operation) {
        if (FAILED(result))
        {
            std::cerr << operation << " failed: 0x" << std::hex << static_cast<unsigned int>(result) << std::dec
                      << std::endl;
            return false;
        }
        return true;
    };

    ComPtr<ID3D11Texture2D> backBuffer;
    if (!succeeded(gSwapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())), "GetBuffer"))
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC description{};
    backBuffer->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (!succeeded(gDevice->CreateTexture2D(&description, nullptr, staging.GetAddressOf()), "CreateTexture2D"))
    {
        return false;
    }
    gContext->CopyResource(staging.Get(), backBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE pixels{};
    if (!succeeded(gContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &pixels), "Map"))
    {
        return false;
    }
    // DX11 supplies RGBA bytes; the WIC PNG encoder accepts BGRA bytes.
    const UINT stride = description.Width * 4;
    std::vector<BYTE> bgra(static_cast<std::size_t>(stride) * description.Height);
    for (UINT y = 0; y < description.Height; ++y)
    {
        const BYTE* source = static_cast<const BYTE*>(pixels.pData) + static_cast<std::size_t>(y) * pixels.RowPitch;
        BYTE* destination = bgra.data() + static_cast<std::size_t>(y) * stride;
        for (UINT x = 0; x < description.Width; ++x)
        {
            destination[x * 4 + 0] = source[x * 4 + 2];
            destination[x * 4 + 1] = source[x * 4 + 1];
            destination[x * 4 + 2] = source[x * 4 + 0];
            destination[x * 4 + 3] = source[x * 4 + 3];
        }
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool mustUninitialize = SUCCEEDED(comResult);
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    bool saved = false;
    do
    {
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
        {
            succeeded(comResult, "CoInitializeEx");
            break;
        }
        if (!succeeded(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(factory.GetAddressOf())),
                       "Create WIC factory") ||
            !succeeded(factory->CreateStream(stream.GetAddressOf()), "Create WIC stream"))
        {
            break;
        }
        const std::wstring widePath = std::filesystem::absolute(Utf8Path(path)).wstring();
        if (!succeeded(stream->InitializeFromFilename(widePath.c_str(), GENERIC_WRITE), "Open PNG stream") ||
            !succeeded(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()),
                       "Create PNG encoder") ||
            !succeeded(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize PNG encoder") ||
            !succeeded(encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf()), "Create PNG frame") ||
            !succeeded(frame->Initialize(properties.Get()), "Initialize PNG frame") ||
            !succeeded(frame->SetSize(description.Width, description.Height), "Set PNG size"))
        {
            break;
        }
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (!succeeded(frame->SetPixelFormat(&format), "Set PNG pixel format"))
        {
            break;
        }
        if (format != GUID_WICPixelFormat32bppBGRA)
        {
            std::cerr << "PNG encoder did not accept BGRA pixels" << std::endl;
            break;
        }
        if (!succeeded(frame->WritePixels(description.Height, stride, stride * description.Height, bgra.data()),
                       "Write PNG pixels") ||
            !succeeded(frame->Commit(), "Commit PNG frame") || !succeeded(encoder->Commit(), "Commit PNG"))
        {
            break;
        }
        saved = true;
    } while (false);

    properties.Reset();
    frame.Reset();
    encoder.Reset();
    stream.Reset();
    factory.Reset();
    gContext->Unmap(staging.Get(), 0);
    if (mustUninitialize)
    {
        CoUninitialize();
    }
    return saved;
}
LRESULT WINAPI WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
    {
        return true;
    }
    switch (message)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            gResizeWidth = LOWORD(lParam);
            gResizeHeight = HIWORD(lParam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
        {
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LX::Pin Input(const char* name, LX::PinType type, bool multiple = false)
{
    LX::Pin pin;
    pin.name = name;
    pin.type = type;
    pin.multiple = multiple;
    return pin;
}

const LX::NodeLayout& Placement(const LX::LXGraph& graph, const LX::Node& node)
{
    return *graph.FindLayout(node.id);
}

LX::Pin Output(const char* name, LX::PinType type)
{
    LX::Pin pin;
    pin.name = name;
    pin.type = type;
    pin.direction = LX::Direction::Output;
    return pin;
}

LX::Pin WithValue(LX::Pin pin, LX::LXSocketValue value)
{
    pin.value = std::move(value);
    return pin;
}

LX::NodeSpec Spec(int index)
{
    using LX::PinType;
    switch (index)
    {
    case 0:
        return {"TEXTURE",
                "Base Color Texture",
                {WithValue(Output("Texture", PinType::Texture), std::string("T_BaseColor"))}};
    case 1:
        return {"SAMPLE", "Texture Sample", {Input("Texture", PinType::Texture), Output("Color", PinType::Color)}};
    case 2:
        return {"COLOR",
                "Tint Color",
                {WithValue(Output("Color", PinType::Color), std::array<double, 4>{0.824, 0.773, 0.675, 1.0})}};
    case 3:
        return {"MULTIPLY",
                "Multiply",
                {Input("A", PinType::Color), Input("B", PinType::Color),
                 WithValue(Input("Factor", PinType::Float), 0.8), Output("Result", PinType::Color)},
                {},
                LX::DynamicPinRule{LX::Direction::Input, PinType::Color, false, 4}};
    case 4:
        return {"NORMAL", "Normal Map", {Input("Texture", PinType::Texture), Output("Normal", PinType::Normal)}};
    case 5:
        return {
            "OUTPUT", "Principled Surface", {Input("Base Color", PinType::Color), Input("Normal", PinType::Normal)}};
    case 6:
        return {"NORMAL_TEXTURE",
                "Normal Texture",
                {WithValue(Output("Texture", PinType::Texture), std::string("T_Normal"))}};
    default:
        return {};
    }
}

LX::NodeSpec AnimationStateSpec()
{
    LX::Pin incoming = Input("In", LX::PinType::Flow);
    incoming.multiple = true;
    return {"FSM_STATE", "State", {incoming, Output("Out", LX::PinType::Flow)}};
}

std::shared_ptr<const LX::LXNodeDefinitionRegistry> ExampleRegistry()
{
    static const auto registry = [] {
        auto value = std::make_shared<LX::LXNodeDefinitionRegistry>();
        for (int index = 0; index < 7; ++index)
        {
            value->Register({"material", Spec(index)});
        }
        value->Register({"behavior", {"BT_ROOT", "ROOT", {Output("Child", LX::PinType::Flow)}}});
        value->Register(
            {"behavior",
             {"BT_COMPOSITE", "Composite", {Input("Parent", LX::PinType::Flow), Output("Child", LX::PinType::Flow)}}});
        value->Register(
            {"behavior",
             {"BT_BRANCH", "Branch", {Input("Parent", LX::PinType::Flow), Output("Child", LX::PinType::Flow)}}});
        value->Register({"behavior", {"BT_TASK", "Task", {Input("Parent", LX::PinType::Flow)}}});
        value->Register({"animation", AnimationStateSpec()});
        value->Register({"animation", {"FSM_ANY", "Any State", {Output("Out", LX::PinType::Flow)}}});
        return value;
    }();
    return registry;
}

LX::LXGraph Fixture()
{
    LX::LXGraph graph("material", ExampleRegistry());
    const auto a = graph.AddNode(Spec(0), 30, 80);
    const auto b = graph.AddNode(Spec(1), 270, 80);
    const auto c = graph.AddNode(Spec(2), 270, 260);
    const auto d = graph.AddNode(Spec(3), 485, 230);
    const auto e = graph.AddNode(Spec(6), 30, 450);
    const auto f = graph.AddNode(Spec(4), 270, 450);
    const auto g = graph.AddNode(Spec(5), 690, 348);
    if (!a || !b || !c || !d || !e || !f || !g)
    {
        std::cerr << "Fixture node creation failed: " << a << ',' << b << ',' << c << ',' << d << ',' << e << ',' << f
                  << ',' << g << std::endl;
        return graph;
    }
    const auto pin = [&](LX::Id node, int index) { return graph.FindNode(node)->pins.at(index).id; };
    graph.Connect(pin(a, 0), pin(b, 0));
    graph.Connect(pin(b, 1), pin(d, 0));
    graph.Connect(pin(c, 0), pin(d, 1));
    graph.Connect(pin(d, 3), pin(g, 0));
    graph.Connect(pin(e, 0), pin(f, 0));
    graph.Connect(pin(f, 1), pin(g, 1));
    return graph;
}

LX::Id AddExampleGroup(LX::LXGraph& graph, float x, float y)
{
    LX::LXGraph body("material", ExampleRegistry());
    const LX::Id operation = body.CreateNode("MULTIPLY", 100.0f, 80.0f);
    if (!operation)
    {
        return 0;
    }
    const LX::Node* node = body.FindNode(operation);
    const LX::Id group =
        graph.CreateGroup("Color group", body,
                          {{0, "a", "A", LX::Direction::Input, LX::PinType::Color, node->pins[0].id, {}},
                           {0, "b", "B", LX::Direction::Input, LX::PinType::Color, node->pins[1].id, {}},
                           {0, "result", "Result", LX::Direction::Output, LX::PinType::Color, node->pins[3].id, {}}});
    if (!group)
    {
        return 0;
    }
    const LX::Id instance = graph.CreateGroupInstance(group, x, y);
    if (instance && graph.Nodes().size() > 2)
    {
        graph.Connect(graph.Nodes()[2].pins[0].id, graph.FindNode(instance)->pins[0].id);
    }
    return instance;
}

ImVec4 Rgb(int red, int green, int blue, float alpha = 1.0f)
{
    return {red / 255.0f, green / 255.0f, blue / 255.0f, alpha};
}

LX::LXStyleSheet ExampleStyles()
{
    LX::LXStyleSheet styles;
    styles.canvas.background = Rgb(37, 37, 37);
    styles.canvas.grid = Rgb(59, 59, 59, 0.38f);
    styles.canvas.gridSpacing = 25.0f;
    styles.canvas.showGrid = true;
    const auto setNode = [&](const char* type, ImVec4 header, float width) {
        LX::LXNodeStyle node = styles.defaultNode;
        node.fill = Rgb(53, 53, 53);
        node.header = header;
        node.headerBottom = header;
        node.border = Rgb(23, 23, 23);
        node.propertyFill = Rgb(69, 69, 69);
        node.width = width;
        node.height = 0.0f;
        node.headerHeight = 22.0f;
        node.rowHeight = 25.0f;
        node.rounding = 4.0f;
        styles.SetTypeStyle(type, node);
    };
    setNode("TEXTURE", Rgb(101, 76, 42), 170.0f);
    setNode("NORMAL_TEXTURE", Rgb(101, 76, 42), 170.0f);
    setNode("COLOR", Rgb(111, 85, 44), 165.0f);
    setNode("SAMPLE", Rgb(39, 92, 88), 165.0f);
    setNode("MULTIPLY", Rgb(48, 76, 110), 170.0f);
    setNode("NORMAL", Rgb(74, 65, 102), 165.0f);
    setNode("OUTPUT", Rgb(58, 98, 75), 190.0f);
    setNode("LX_GROUP_INPUT", Rgb(61, 95, 78), 175.0f);
    setNode("LX_GROUP_OUTPUT", Rgb(61, 95, 78), 175.0f);

    const auto setPin = [&](LX::PinType type, LX::LXPinShape shape, ImVec4 color) {
        LX::LXPinStyle pin = styles.defaultPin;
        pin.shape = shape;
        pin.fill = color;
        pin.radius = 4.5f;
        styles.SetPinTypeStyle(type, pin);

        LX::LXWireStyle wire = styles.defaultWire;
        wire.color = Rgb(151, 151, 151, 0.90f);
        wire.selectedColor = Rgb(210, 210, 210);
        wire.thickness = 1.6f;
        wire.selectedThickness = 2.2f;
        styles.SetWireTypeStyle(type, wire);
    };
    setPin(LX::PinType::Flow, LX::LXPinShape::Circle, Rgb(185, 185, 185));
    setPin(LX::PinType::Texture, LX::LXPinShape::Circle, Rgb(194, 166, 95));
    setPin(LX::PinType::Color, LX::LXPinShape::Circle, Rgb(216, 189, 91));
    setPin(LX::PinType::Float, LX::LXPinShape::Circle, Rgb(166, 166, 166));
    setPin(LX::PinType::Normal, LX::LXPinShape::Circle, Rgb(140, 130, 187));
    return styles;
}

LX::LXNodeItemRegistry ExampleItems()
{
    LX::LXNodeItemRegistry items;
    items.Register("TEXTURE", {"Asset", LX::LXNodeItemKind::TexturePreview, 0.0f, 1.0f, 64.0f, "", "", "", "Texture"});
    items.Register("NORMAL_TEXTURE",
                   {"Asset", LX::LXNodeItemKind::TexturePreview, 0.0f, 1.0f, 64.0f, "", "", "", "Texture"});
    items.Register("COLOR", {"Value", LX::LXNodeItemKind::Color, 0.0f, 1.0f, 26.0f, "", "", "", "Color"});
    items.Register("MULTIPLY",
                   {"Factor", LX::LXNodeItemKind::FloatSlider, 0.0f, 1.0f, 23.0f, "Factor", "", "", "Factor"});
    return items;
}

ImVec2 FindLibraryPlacement(const LX::LXGraph& graph, const LX::CanvasState& canvas, const LX::LXStyleSheet& styles,
                            const LX::LXNodeItemRegistry& items, const LX::NodeSpec& spec)
{
    LX::Node preview;
    preview.type = spec.type;
    preview.title = spec.title;
    preview.pins = spec.pins;
    preview.properties = spec.properties;
    preview.dynamicPins = spec.dynamicPins;

    const LX::NodeLayout initial{};
    const LX::LXNodeGeometry size = LX::MeasureNode(preview, initial, styles.ForNode(preview), items);
    const float scale = std::max(0.01f, canvas.zoom * ImGui::GetStyle().FontScaleDpi);
    const float viewLeft = std::max(0.0f, -canvas.pan.x / scale + 12.0f);
    const float viewTop = std::max(0.0f, -canvas.pan.y / scale + 12.0f);
    const float startX = std::max(30.0f, viewLeft);
    const float startY = std::max(80.0f, viewTop);
    const float right = (canvas.canvasSize.x - canvas.pan.x) / scale - size.width - 12.0f;
    const float bottom = (canvas.canvasSize.y - canvas.pan.y) / scale - size.height - 12.0f;
    constexpr float gap = 16.0f;
    constexpr float step = 40.0f;

    const auto isOpen = [&](float x, float y) {
        for (const LX::Node& node : graph.Nodes())
        {
            const LX::NodeLayout* placement = graph.FindLayout(node.id);
            if (!placement)
            {
                continue;
            }
            const LX::LXNodeGeometry occupied = LX::MeasureNode(node, *placement, styles.ForNode(node), items);
            if (x < placement->x + occupied.width + gap && x + size.width + gap > placement->x &&
                y < placement->y + occupied.height + gap && y + size.height + gap > placement->y)
            {
                return false;
            }
        }
        return true;
    };

    for (float y = startY; y <= bottom; y += step)
    {
        for (float x = startX; x <= right; x += step)
        {
            if (isOpen(x, y))
            {
                return {x, y};
            }
        }
    }

    float lastBottom = startY;
    for (const LX::Node& node : graph.Nodes())
    {
        const LX::NodeLayout* placement = graph.FindLayout(node.id);
        if (placement)
        {
            const LX::LXNodeGeometry occupied = LX::MeasureNode(node, *placement, styles.ForNode(node), items);
            lastBottom = std::max(lastBottom, placement->y + occupied.height + gap);
        }
    }
    return {startX, lastBottom};
}

bool RevealLibraryNode(const LX::LXGraph& graph, LX::CanvasState& canvas, const LX::LXStyleSheet& styles,
                       const LX::LXNodeItemRegistry& items, LX::Id nodeId)
{
    const LX::Node* node = graph.FindNode(nodeId);
    const LX::NodeLayout* placement = graph.FindLayout(nodeId);
    if (!node || !placement || canvas.canvasSize.x <= 0.0f || canvas.canvasSize.y <= 0.0f)
    {
        return false;
    }

    const LX::LXNodeGeometry size = LX::MeasureNode(*node, *placement, styles.ForNode(*node), items);
    const float scale = canvas.zoom * ImGui::GetStyle().FontScaleDpi;
    const float left = canvas.pan.x + placement->x * scale;
    const float top = canvas.pan.y + placement->y * scale;
    constexpr float margin = 12.0f;
    if (left >= margin && top >= margin && left + size.width * scale <= canvas.canvasSize.x - margin &&
        top + size.height * scale <= canvas.canvasSize.y - margin)
    {
        return false;
    }

    canvas.pan = {canvas.canvasSize.x * 0.5f - (placement->x + size.width * 0.5f) * scale,
                  canvas.canvasSize.y * 0.5f - (placement->y + size.height * 0.5f) * scale};
    return true;
}

LX::LXGraph BehaviorFixture()
{
    LX::LXGraph graph("behavior", ExampleRegistry());
    const auto root = graph.AddNode(
        {"BT_ROOT", "ROOT", {Output("Child", LX::PinType::Flow)}, {{"01 Root", "BB_Character"}}}, 570.0f, 40.0f);
    const auto selector = graph.AddNode({"BT_COMPOSITE",
                                         "AI State",
                                         {Input("Parent", LX::PinType::Flow), Output("Child", LX::PinType::Flow)},
                                         {{"01 Composite", "Selector"}, {"02 Service", "Detect: tick every 0.9s"}}},
                                        525.0f, 200.0f);
    const auto chase = graph.AddNode({"BT_BRANCH",
                                      "Chase Player",
                                      {Input("Parent", LX::PinType::Flow), Output("Child", LX::PinType::Flow)},
                                      {{"01 Decorator", "Blackboard: target is set"}, {"02 Composite", "Sequence"}}},
                                     180.0f, 420.0f);
    const auto patrol = graph.AddNode({"BT_BRANCH",
                                       "Patrol",
                                       {Input("Parent", LX::PinType::Flow), Output("Child", LX::PinType::Flow)},
                                       {{"01 Composite", "Sequence"}}},
                                      780.0f, 420.0f);
    const auto move =
        graph.AddNode({"BT_TASK", "Move To", {Input("Parent", LX::PinType::Flow)}, {{"01 Task", "MoveTo: EnemyActor"}}},
                      180.0f, 630.0f);
    const auto wait = graph.AddNode(
        {"BT_TASK", "Wait", {Input("Parent", LX::PinType::Flow)}, {{"01 Task", "Wait: 1.0s"}}}, 780.0f, 630.0f);
    const auto input = [&](LX::Id id) { return graph.FindNode(id)->pins.front().id; };
    const auto output = [&](LX::Id id) { return graph.FindNode(id)->pins.back().id; };
    graph.Connect(output(root), input(selector));
    graph.Connect(output(selector), input(chase));
    graph.Connect(output(selector), input(patrol));
    graph.Connect(output(chase), input(move));
    graph.Connect(output(patrol), input(wait));
    return graph;
}

LX::LXNodeItemRegistry BehaviorItems()
{
    LX::LXNodeItemRegistry items;
    const auto card = [&](const char* type, const char* key, const char* role, const char* label) {
        items.Register(type, {key, LX::LXNodeItemKind::Card, 0.0f, 1.0f, 43.0f, "", role, label});
    };
    card("BT_ROOT", "01 Root", "bt.root", "ROOT");
    card("BT_COMPOSITE", "01 Composite", "bt.composite", "Selector");
    card("BT_COMPOSITE", "02 Service", "bt.service", "Detect");
    card("BT_BRANCH", "01 Decorator", "bt.decorator", "Target On");
    card("BT_BRANCH", "01 Composite", "bt.composite", "Sequence");
    card("BT_BRANCH", "02 Composite", "bt.composite", "Sequence");
    card("BT_TASK", "01 Task", "bt.task", "$node");
    return items;
}

LX::LXStyleSheet BehaviorStyles()
{
    LX::LXStyleSheet styles = ExampleStyles();
    styles.canvas.gridSpacing = 24.0f;
    styles.canvas.grid = Rgb(72, 72, 72, 0.38f);
    for (const char* type : {"BT_ROOT", "BT_COMPOSITE", "BT_BRANCH", "BT_TASK"})
    {
        LX::LXNodeStyle node = styles.defaultNode;
        node.width = type == std::string("BT_ROOT") ? 128.0f : 208.0f;
        node.headerHeight = 20.0f;
        node.bodyBottomPadding = 6.0f;
        node.propertyMargin = 4.0f;
        node.fill = Rgb(54, 54, 54);
        node.header = Rgb(55, 55, 55);
        node.headerBottom = Rgb(55, 55, 55);
        node.pinLayout = LX::LXPinLayout::TopBottom;
        styles.SetTypeStyle(type, node);
    }
    const auto colorCard = [&](const char* role, ImVec4 fill) {
        LX::LXItemStyle item;
        item.fill = fill;
        item.border = Rgb(25, 25, 25);
        styles.SetItemStyle(role, item);
    };
    colorCard("bt.root", Rgb(55, 55, 55));
    colorCard("bt.composite", Rgb(68, 68, 68));
    colorCard("bt.decorator", Rgb(20, 73, 135));
    colorCard("bt.service", Rgb(12, 103, 87));
    colorCard("bt.task", Rgb(92, 50, 142));
    LX::LXWireStyle flow = styles.defaultWire;
    flow.color = Rgb(217, 217, 217);
    flow.bend = 34.0f;
    flow.route = LX::LXWireStyle::Route::Straight;
    styles.SetWireTypeStyle(LX::PinType::Flow, flow);
    return styles;
}

LX::LXGraph AnimationFixture()
{
    LX::LXGraph graph("animation", ExampleRegistry());
    LX::NodeSpec state = AnimationStateSpec();
    const LX::Id any = graph.CreateNode("FSM_ANY", 65.0f, 245.0f);
    state.title = "Idle";
    const LX::Id idle = graph.AddNode(state, 350.0f, 245.0f);
    state.title = "Move";
    const LX::Id move = graph.AddNode(state, 650.0f, 245.0f);
    state.title = "Attack";
    const LX::Id attack = graph.AddNode(state, 950.0f, 450.0f);
    if (!any || !idle || !move || !attack)
    {
        return graph;
    }
    const auto input = [&](LX::Id id) { return graph.FindNode(id)->pins.front().id; };
    const auto output = [&](LX::Id id) { return graph.FindNode(id)->pins.back().id; };
    graph.Connect(output(any), input(idle));
    graph.Connect(output(idle), input(move));
    graph.Connect(output(move), input(idle));
    graph.Connect(output(move), input(attack));
    graph.Connect(output(attack), input(idle));
    return graph;
}

LX::LXStyleSheet AnimationStyles()
{
    LX::LXStyleSheet styles = ExampleStyles();
    styles.canvas.background = Rgb(31, 34, 38);
    styles.canvas.grid = Rgb(67, 71, 76, 0.35f);
    styles.canvas.gridSpacing = 30.0f;
    for (const char* type : {"FSM_ANY", "FSM_STATE"})
    {
        LX::LXNodeStyle node = styles.defaultNode;
        node.headerOnly = true;
        node.showPropertyPreview = false;
        node.width = 170.0f;
        node.headerHeight = 32.0f;
        node.fontSize = 14.0f;
        node.textPaddingX = 14.0f;
        node.header = type == std::string("FSM_ANY") ? Rgb(77, 79, 86) : Rgb(47, 81, 111);
        node.headerBottom = node.header;
        node.border = Rgb(19, 24, 29);
        node.selectedBorder = Rgb(243, 168, 69);
        node.text = Rgb(239, 241, 244);
        styles.SetTypeStyle(type, node);
    }
    LX::LXPinStyle flowPin = styles.defaultPin;
    flowPin.shape = LX::LXPinShape::Circle;
    flowPin.fill = styles.canvas.background;
    flowPin.border = Rgb(218, 226, 236);
    flowPin.borderThickness = 1.8f;
    flowPin.radius = 4.5f;
    flowPin.hitPadding = 7.0f;
    styles.SetPinTypeStyle(LX::PinType::Flow, flowPin);
    LX::LXWireStyle transition = styles.defaultWire;
    transition.color = Rgb(125, 191, 212);
    transition.selectedColor = Rgb(255, 209, 126);
    transition.thickness = 2.0f;
    transition.selectedThickness = 2.6f;
    transition.bend = 46.0f;
    transition.route = LX::LXWireStyle::Route::Straight;
    transition.arrowAtMidpoint = true;
    styles.SetWireTypeStyle(LX::PinType::Flow, transition);
    return styles;
}

std::string Utf8Name(const std::filesystem::path& path)
{
    const auto name = path.u8string();
    return std::string(name.begin(), name.end());
}

std::string DefaultExamplePath(const char* fileName)
{
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= static_cast<DWORD>(executable.size()))
    {
        return fileName;
    }
    executable.resize(length);
    return Utf8Name(std::filesystem::path(executable).parent_path() / fileName);
}

std::filesystem::path Utf8Path(const std::string& name)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()));
}

std::string StylePath(const std::string& graphPath, const char* windowKind = "MaterialGraph")
{
    const std::filesystem::path graph = Utf8Path(graphPath);
    const std::string documentName = Utf8Name(graph.filename());
    const std::string fileName = std::string(windowKind) + "." + documentName + ".lxstyle";
    const std::filesystem::path styleName = Utf8Path(fileName);
    return Utf8Name(graph.parent_path() / "LatticeStyles" / styleName);
}

bool SaveWindowStyle(const std::string& graphPath, const char* windowKind, const LX::LXStyleSheet& styles,
                     std::string* error)
{
    const std::string stylePath = StylePath(graphPath, windowKind);
    std::error_code fileError;
    std::filesystem::create_directories(Utf8Path(stylePath).parent_path(), fileError);
    if (fileError)
    {
        if (error)
        {
            *error = "Cannot create window style directory: " + fileError.message();
        }
        return false;
    }
    return styles.Save(stylePath, error);
}

bool LoadExampleStyles(const std::string& graphPath, LX::LXStyleSheet& styles, std::string* error,
                       const char* windowKind = "MaterialGraph")
{
    const std::string stylePath = StylePath(graphPath, windowKind);
    if (!std::filesystem::exists(Utf8Path(stylePath)))
    {
        return true;
    }

    auto loaded = LX::LXStyleSheet::Load(stylePath, error);
    if (!loaded)
    {
        return false;
    }
    styles = std::move(*loaded);
    return true;
}

void ApplyExampleTheme()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = {0.0f, 0.0f};
    style.FramePadding = {6.0f, 3.0f};
    style.ItemSpacing = {6.0f, 4.0f};
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 3.0f;
    style.GrabRounding = 2.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ScrollbarSize = 9.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = Rgb(220, 220, 220);
    colors[ImGuiCol_TextDisabled] = Rgb(145, 145, 145);
    colors[ImGuiCol_WindowBg] = Rgb(38, 38, 38);
    colors[ImGuiCol_ChildBg] = Rgb(45, 45, 45);
    colors[ImGuiCol_PopupBg] = Rgb(48, 48, 48);
    colors[ImGuiCol_Border] = Rgb(22, 22, 22);
    colors[ImGuiCol_FrameBg] = Rgb(69, 69, 69);
    colors[ImGuiCol_FrameBgHovered] = Rgb(83, 83, 83);
    colors[ImGuiCol_FrameBgActive] = Rgb(93, 93, 93);
    colors[ImGuiCol_Button] = Rgb(65, 65, 65);
    colors[ImGuiCol_ButtonHovered] = Rgb(84, 84, 84);
    colors[ImGuiCol_ButtonActive] = Rgb(99, 99, 99);
    colors[ImGuiCol_Header] = Rgb(64, 89, 119);
    colors[ImGuiCol_HeaderHovered] = Rgb(77, 105, 139);
    colors[ImGuiCol_HeaderActive] = Rgb(89, 118, 153);
    colors[ImGuiCol_Separator] = Rgb(26, 26, 26);
    colors[ImGuiCol_CheckMark] = Rgb(77, 145, 214);
    colors[ImGuiCol_SliderGrab] = Rgb(133, 133, 133);
    colors[ImGuiCol_SliderGrabActive] = Rgb(197, 197, 197);
    colors[ImGuiCol_Tab] = Rgb(47, 47, 47);
    colors[ImGuiCol_TabHovered] = Rgb(68, 68, 68);
    colors[ImGuiCol_TabSelected] = Rgb(72, 72, 72);
}

void LoadExampleFont()
{
    constexpr const char* fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (std::filesystem::exists(fontPath))
    {
        if (ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath, 13.0f))
        {
            ImGui::GetIO().FontDefault = font;
        }
    }
}

bool LoadMaterialPreviewFont()
{
    auto& atlas = *ImGui::GetIO().Fonts;
    atlas.Clear();
    static constexpr ImWchar exclusions[]{0xe000, 0xf8ff, 0};
    ImFontConfig textConfig;
    textConfig.SizePixels = 16.0f;
    textConfig.Flags |= ImFontFlags_NoLoadError;
    textConfig.GlyphExcludeRanges = exclusions;
    const auto textPath = DefaultExamplePath("Fonts/Inter-Regular.ttf");
    auto* text = atlas.AddFontFromFileTTF(textPath.c_str(), 16.0f, &textConfig);
    if (!text)
    {
        text = atlas.AddFontDefault(&textConfig);
    }
    ImGui::GetIO().FontDefault = text;
    const auto iconPath = DefaultExamplePath(EditorIcon::FontPath);
    if (!text || !editor::fonts::merge_aligned_icons(atlas, iconPath.c_str(), 16.0f))
    {
        std::cerr << "Cannot load the material preview icon font: " << iconPath << std::endl;
        return false;
    }
    for (const auto& role : EditorIcon::Roles)
    {
        if (!text->IsGlyphInFont(static_cast<ImWchar>(role.codepoint)))
        {
            std::cerr << "Missing material preview icon: " << role.name << std::endl;
            return false;
        }
    }
    std::cout << "LX_MATERIAL_ICON_FONT_OK roles=" << std::size(EditorIcon::Roles) << std::endl;
    return true;
}

bool MatchesSearch(const char* label, const char* query)
{
    if (!query[0])
    {
        return true;
    }
    std::string name = label;
    std::string search = query;
    const auto lower = [](unsigned char character) { return static_cast<char>(std::tolower(character)); };
    std::transform(name.begin(), name.end(), name.begin(), lower);
    std::transform(search.begin(), search.end(), search.begin(), lower);
    return name.find(search) != std::string::npos;
}

bool StyleColor(const char* label, ImVec4& color)
{
    return ImGui::ColorEdit4(label, &color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
}

bool StyleSlider(const char* label, const char* id, float* value, float minimum, float maximum, float dpi)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(112.0f * dpi);
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat(id, value, minimum, maximum, "%.1f");
}

void DrawDynamicPinControls(LX::LXGraph& graph, LX::CanvasState& canvas, LX::Id nodeId,
                            LX::LXDocument* document = nullptr)
{
    const LX::Node* node = graph.FindNode(nodeId);
    if (!node || !node->dynamicPins)
    {
        return;
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Dynamic Pins");
    const bool add = ImGui::Button("Add pin");
    if (gDynamicPinProbe)
    {
        const ImVec2 top = ImGui::GetItemRectMin();
        const ImVec2 bottom = ImGui::GetItemRectMax();
        gDynamicPinProbe->addPoint = {(top.x + bottom.x) * 0.5f, (top.y + bottom.y) * 0.5f};
    }
    if (add)
    {
        for (std::size_t index = 1; index <= node->dynamicPins->maxCount + 1; ++index)
        {
            const std::string name = "Extra " + std::to_string(index);
            const bool created =
                document ? document->Execute(LX::LXAddDynamicPin{nodeId, name}, document->Revision()).applied
                         : graph.AddDynamicPin(nodeId, name) != 0;
            if (created)
            {
                canvas.dirty = true;
                break;
            }
        }
    }
    for (const LX::Pin& pin : node->pins)
    {
        if (!pin.dynamic)
        {
            continue;
        }
        ImGui::PushID(static_cast<int>(pin.id));
        ImGui::TextUnformatted(pin.name.c_str());
        ImGui::SameLine();
        const bool up = ImGui::SmallButton("Up");
        ImGui::SameLine();
        const bool down = ImGui::SmallButton("Down");
        ImGui::SameLine();
        const bool remove = ImGui::SmallButton("Remove");
        if (gDynamicPinProbe)
        {
            const ImVec2 top = ImGui::GetItemRectMin();
            const ImVec2 bottom = ImGui::GetItemRectMax();
            gDynamicPinProbe->removePoints.push_back({(top.x + bottom.x) * 0.5f, (top.y + bottom.y) * 0.5f});
        }
        ImGui::PopID();
        const auto move = [&](int direction) {
            return document ? document->Execute(LX::LXMoveDynamicPin{pin.id, direction}, document->Revision()).applied
                            : graph.MoveDynamicPin(pin.id, direction);
        };
        const bool changed =
            (up && move(-1)) || (down && move(1)) ||
            (remove && (document ? document->Execute(LX::LXRemoveDynamicPin{pin.id}, document->Revision()).applied
                                 : graph.RemoveDynamicPin(pin.id)));
        if (changed)
        {
            canvas.dirty = true;
            break;
        }
    }
}

void DrawPortCreationPopup(LX::LXGraph& graph, LX::CanvasState& canvas, LX::LXDocument* document = nullptr)
{
    if (canvas.creationPin && !canvas.creationPopupOpen)
    {
        canvas.creationQuery.fill(0);
        ImGui::OpenPopup("Create connected node");
        canvas.creationPopupOpen = true;
    }
    if (ImGui::BeginPopup("Create connected node"))
    {
        const LX::Pin* source = graph.FindPin(canvas.creationPin);
        if (!source)
        {
            canvas.creationPin = 0;
            canvas.creationPopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        else
        {
            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputTextWithHint("##compatible_search", "Search compatible nodes...", canvas.creationQuery.data(),
                                     canvas.creationQuery.size());
            if (gCreationProbe)
            {
                const ImVec2 top = ImGui::GetItemRectMin();
                const ImVec2 bottom = ImGui::GetItemRectMax();
                gCreationProbe->searchPoint = {(top.x + bottom.x) * 0.5f, (top.y + bottom.y) * 0.5f};
            }
            for (int index = 0; index < 7; ++index)
            {
                const LX::NodeSpec spec = Spec(index);
                if (!MatchesSearch(spec.title.c_str(), canvas.creationQuery.data()))
                {
                    continue;
                }
                const auto compatible = std::find_if(spec.pins.begin(), spec.pins.end(), [&](const LX::Pin& pin) {
                    return pin.direction != source->direction && pin.type == source->type;
                });
                if (compatible == spec.pins.end())
                {
                    continue;
                }
                const bool chosen = ImGui::Selectable(spec.title.c_str());
                if (gCreationProbe)
                {
                    const ImVec2 top = ImGui::GetItemRectMin();
                    const ImVec2 bottom = ImGui::GetItemRectMax();
                    gCreationProbe->results.emplace_back(index,
                                                         ImVec2{(top.x + bottom.x) * 0.5f, (top.y + bottom.y) * 0.5f});
                }
                if (chosen)
                {
                    const std::size_t pinIndex = static_cast<std::size_t>(compatible - spec.pins.begin());
                    const LX::Id newNode =
                        document
                            ? document
                                  ->Execute(LX::LXAddConnectedNodeSpec{spec, canvas.creationAt.x, canvas.creationAt.y,
                                                                       canvas.creationPin, pinIndex},
                                            document->Revision())
                                  .created
                            : graph.AddConnectedNode(spec, canvas.creationAt.x, canvas.creationAt.y, canvas.creationPin,
                                                     pinIndex);
                    if (newNode)
                    {
                        canvas.selectedNode = newNode;
                        canvas.selectedFrame = 0;
                        canvas.selectedNodes.clear();
                        canvas.dirty = true;
                    }
                    canvas.creationPin = 0;
                    canvas.creationPopupOpen = false;
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
        }
        ImGui::EndPopup();
    }
    else if (canvas.creationPopupOpen && !ImGui::IsPopupOpen("Create connected node"))
    {
        canvas.creationPin = 0;
        canvas.creationPopupOpen = false;
    }
}

void DrawProblems(const LX::LXGraph& graph, LX::CanvasState& canvas, LX::LXDocument* document = nullptr)
{
    const std::vector<LX::Issue> issues = graph.Validate();
    ImGui::Text("Problems %zu", issues.size());
    for (std::size_t index = 0; index < issues.size(); ++index)
    {
        const LX::Issue& issue = issues[index];
        ImGui::PushID(static_cast<int>(index));
        const std::string label = issue.code + ": " + issue.message;
        const bool clicked = ImGui::Selectable(label.c_str());
        if (gProblemProbe && index == 0)
        {
            const ImVec2 top = ImGui::GetItemRectMin();
            const ImVec2 bottom = ImGui::GetItemRectMax();
            gProblemProbe->firstPoint = {(top.x + bottom.x) * 0.5f, (top.y + bottom.y) * 0.5f};
        }
        ImGui::PopID();
        if (clicked)
        {
            if (const LX::Node* node = graph.FindNode(issue.node))
            {
                canvas.selectedNode = node->id;
                canvas.selectedFrame = 0;
                canvas.selectedNodes.clear();
                const float scale = canvas.zoom * ImGui::GetStyle().FontScaleDpi;
                const LX::NodeLayout& layout = Placement(graph, *node);
                canvas.pan = {canvas.canvasSize.x * 0.5f - layout.x * scale,
                              canvas.canvasSize.y * 0.5f - layout.y * scale};
                if (document)
                {
                    document->Execute(LX::LXSetView{{true, layout.x, layout.y, canvas.zoom}}, document->Revision());
                }
            }
            canvas.message = issue.message;
        }
    }
}

enum class ExamplePage
{
    Material,
    Behavior,
    Animation
};

void DrawUI(LX::LXGraph& graph, LX::CanvasState& canvas, LX::LXStyleSheet& styles, const LX::LXNodeItemRegistry& items,
            const std::string& file, ExamplePage* page = nullptr, LX::LXDocument* document = nullptr,
            GroupEditorState* groupEditor = nullptr);
void DrawGroupEditor(GroupEditorState& editor, LX::LXDocument& parent, const LX::LXStyleSheet& windowStyles,
                     const LX::LXNodeItemRegistry& items);
bool DrawGroupSocketDefault(LX::LXGroupSocket& socket)
{
    if (socket.type == LX::PinType::Flow || socket.type == LX::PinType::Surface)
    {
        ImGui::TextDisabled("No literal default");
        return false;
    }
    bool hasValue = !std::holds_alternative<std::monostate>(socket.value);
    const bool toggled = ImGui::Checkbox("Use default", &hasValue);
    if (gGroupEditorProbe)
    {
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        gGroupEditorProbe->defaultCheckboxPoints[socket.identifier] = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    }
    if (toggled)
    {
        if (!hasValue)
        {
            socket.value = std::monostate{};
        }
        else
        {
            switch (socket.type)
            {
            case LX::PinType::Bool:
                socket.value = false;
                break;
            case LX::PinType::Int:
                socket.value = std::int64_t{0};
                break;
            case LX::PinType::Float:
                socket.value = 0.0;
                break;
            case LX::PinType::Vector:
            case LX::PinType::Normal:
                socket.value = std::array<double, 3>{};
                break;
            case LX::PinType::Color:
                socket.value = std::array<double, 4>{0.0, 0.0, 0.0, 1.0};
                break;
            case LX::PinType::Texture:
                socket.value = std::string{};
                break;
            case LX::PinType::Flow:
            case LX::PinType::Surface:
                break;
            }
        }
        return true;
    }
    if (!hasValue)
    {
        return false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    LX::LXSocketValue next = socket.value;
    bool changed = false;
    if (auto* boolValue = std::get_if<bool>(&next))
    {
        changed = ImGui::Checkbox("##default", boolValue);
    }
    else if (auto* intValue = std::get_if<std::int64_t>(&next))
    {
        changed = ImGui::InputScalar("##default", ImGuiDataType_S64, intValue, nullptr, nullptr, nullptr,
                                     ImGuiInputTextFlags_EnterReturnsTrue);
    }
    else if (auto* floatValue = std::get_if<double>(&next))
    {
        changed = ImGui::InputDouble("##default", floatValue, 0.0, 0.0, "%.6f", ImGuiInputTextFlags_EnterReturnsTrue);
    }
    else if (auto* vectorValue = std::get_if<std::array<double, 3>>(&next))
    {
        float components[3] = {static_cast<float>((*vectorValue)[0]), static_cast<float>((*vectorValue)[1]),
                               static_cast<float>((*vectorValue)[2])};
        changed = ImGui::InputFloat3("##default", components, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);
        if (changed)
        {
            *vectorValue = {components[0], components[1], components[2]};
        }
    }
    else if (auto* colorValue = std::get_if<std::array<double, 4>>(&next))
    {
        float components[4] = {static_cast<float>((*colorValue)[0]), static_cast<float>((*colorValue)[1]),
                               static_cast<float>((*colorValue)[2]), static_cast<float>((*colorValue)[3])};
        changed = ImGui::ColorEdit4("##default", components);
        if (changed)
        {
            *colorValue = {components[0], components[1], components[2], components[3]};
        }
    }
    else if (auto* textValue = std::get_if<std::string>(&next))
    {
        std::array<char, 256> text{};
        std::copy_n(textValue->data(), std::min(textValue->size(), text.size() - 1), text.data());
        changed = ImGui::InputText("##default", text.data(), text.size());
        if (changed)
        {
            *textValue = text.data();
        }
    }
    if (changed && LX::IsSocketValueValid(socket.type, next))
    {
        socket.value = std::move(next);
        return true;
    }
    return false;
}

void DrawGroupEditor(GroupEditorState& editor, LX::LXDocument& parent, const LX::LXStyleSheet& windowStyles,
                     const LX::LXNodeItemRegistry& items)
{
    assert(editor.draft);
    LX::LXDocument& draft = *editor.draft;
    LX::LXGraph& body = draft.GraphForCanvas();
    LX::Id inputBoundary = 0;
    LX::Id outputBoundary = 0;
    for (const LX::Node& node : body.Nodes())
    {
        if (node.type == "LX_GROUP_INPUT")
        {
            inputBoundary = node.id;
        }
        else if (node.type == "LX_GROUP_OUTPUT")
        {
            outputBoundary = node.id;
        }
    }
    const bool boundaryMode = inputBoundary || outputBoundary;
    LX::LXStyleSheet styles = windowStyles.WithoutIndividualOverrides();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("Lattice Group Editor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    ImGui::BeginChild("Group Toolbar", {0.0f, 34.0f * dpi}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Material Graph  /  %s", editor.name.c_str());
    ImGui::SameLine();
    const bool apply = ImGui::Button("Apply Group");
    if (gGroupEditorProbe)
    {
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        gGroupEditorProbe->applyPoint = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    }
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel");
    if (gGroupEditorProbe)
    {
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        gGroupEditorProbe->cancelPoint = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo"))
    {
        draft.Execute(LX::LXUndo{}, draft.Revision());
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo"))
    {
        draft.Execute(LX::LXRedo{}, draft.Revision());
    }
    ImGui::SameLine();
    const std::vector<LX::Id> selected = editor.canvas.selectedNodes.empty() && editor.canvas.selectedNode
                                             ? std::vector<LX::Id>{editor.canvas.selectedNode}
                                             : editor.canvas.selectedNodes;
    ImGui::BeginDisabled(selected.empty());
    if (ImGui::Button("Group selection"))
    {
        const LX::Id instance =
            draft
                .Execute(LX::LXCollapseToGroup{selected, NextGroupName(body), parent.Graph().NextId()},
                         draft.Revision())
                .created;
        if (instance)
        {
            editor.canvas.selectedNode = instance;
            editor.canvas.selectedNodes.clear();
            editor.message = "Nested group created";
        }
        else
        {
            editor.message = "Selected nodes cannot form a nested group";
        }
    }
    ImGui::EndDisabled();
    if (gGroupEditorProbe)
    {
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        gGroupEditorProbe->groupSelectionPoint = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    }
    ImGui::EndChild();

    const float height = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("Group Library", {300.0f * dpi, height}, ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Nodes");
    ImGui::Separator();
    static const char* names[] = {"Base Color Texture", "Texture Sample",     "Tint Color",    "Multiply",
                                  "Normal Map",         "Principled Surface", "Normal Texture"};
    for (int index = 0; index < 7; ++index)
    {
        if (ImGui::Selectable(names[index]))
        {
            const float offset = static_cast<float>(body.Nodes().size() % 5) * 34.0f;
            editor.canvas.selectedNode =
                draft.Execute(LX::LXCreateNodeSpec{Spec(index), 150.0f + offset, 100.0f + offset}, draft.Revision())
                    .created;
            editor.canvas.selectedNodes.clear();
        }
    }
    if (!body.Groups().empty())
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Nested groups");
        for (const auto& [id, group] : body.Groups())
        {
            ImGui::PushID(static_cast<int>(id));
            if (ImGui::Selectable(group.name.c_str()))
            {
                const float offset = static_cast<float>(body.Nodes().size() % 5) * 34.0f;
                editor.canvas.selectedNode =
                    draft.Execute(LX::LXCreateGroupInstance{id, 150.0f + offset, 100.0f + offset}, draft.Revision())
                        .created;
                editor.canvas.selectedNodes.clear();
            }
            if (gGroupEditorProbe)
            {
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                gGroupEditorProbe->nestedGroupPoints[id] = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
            }
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Interface");
    const auto editText = [](const char* label, std::string& value) {
        std::array<char, 128> buffer{};
        std::copy_n(value.data(), std::min(value.size(), buffer.size() - 1), buffer.data());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText(label, buffer.data(), buffer.size()))
        {
            value = buffer.data();
        }
    };
    editText("Group name", editor.name);
    int removeIndex = -1;
    int moveIndex = -1;
    int moveDirection = 0;
    for (std::size_t index = 0; index < editor.sockets.size(); ++index)
    {
        LX::LXGroupSocket& socket = editor.sockets[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::Separator();
        ImGui::Text("%s %zu  %s", socket.direction == LX::Direction::Input ? "Input" : "Output", index + 1,
                    LX::PinTypeName(socket.type));
        editText("Name", socket.name);
        editText("Identifier", socket.identifier);
        const bool defaultOpen = ImGui::TreeNode("Default value");
        if (gGroupEditorProbe)
        {
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            gGroupEditorProbe->defaultTreePoints[socket.identifier] = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
        }
        if (defaultOpen)
        {
            DrawGroupSocketDefault(socket);
            ImGui::TreePop();
        }
        const LX::Pin* internal = body.FindPin(socket.internalPin);
        const std::string current = internal ? internal->name : "Missing pin";
        if (boundaryMode)
        {
            ImGui::TextDisabled("%s / %s", socket.direction == LX::Direction::Input ? "Group Input" : "Group Output",
                                current.c_str());
        }
        else if (ImGui::BeginCombo("Internal pin", current.c_str()))
        {
            for (const LX::Node& node : body.Nodes())
            {
                for (const LX::Pin& pin : node.pins)
                {
                    const bool usedElsewhere =
                        std::any_of(editor.sockets.begin(), editor.sockets.end(), [&](const LX::LXGroupSocket& other) {
                            return &other != &socket && other.internalPin == pin.id;
                        });
                    if (pin.direction != socket.direction || usedElsewhere)
                    {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(pin.id));
                    const std::string label = node.title + " / " + pin.name;
                    if (ImGui::Selectable(label.c_str(), pin.id == socket.internalPin))
                    {
                        socket.internalPin = pin.id;
                        socket.type = pin.type;
                        socket.value = pin.value;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::SmallButton("Up") && index > 0)
        {
            moveIndex = static_cast<int>(index);
            moveDirection = -1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Down") && index + 1 < editor.sockets.size())
        {
            moveIndex = static_cast<int>(index);
            moveDirection = 1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
        {
            removeIndex = static_cast<int>(index);
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0)
    {
        bool removed = true;
        if (boundaryMode)
        {
            const LX::Id pin = editor.sockets[removeIndex].internalPin;
            removed = draft.Execute(LX::LXRemoveGroupBoundaryPin{pin}, draft.Revision()).applied;
        }
        if (removed)
        {
            editor.sockets.erase(editor.sockets.begin() + removeIndex);
        }
        else
        {
            editor.message = "Cannot remove group interface socket";
        }
    }
    else if (moveIndex >= 0)
    {
        std::swap(editor.sockets[moveIndex], editor.sockets[moveIndex + moveDirection]);
    }
    const auto expose = [&](LX::Direction direction, const char* label) {
        ImGui::TextDisabled("%s", label);
        for (const LX::Node& node : body.Nodes())
        {
            for (const LX::Pin& pin : node.pins)
            {
                if (pin.direction != direction ||
                    std::any_of(editor.sockets.begin(), editor.sockets.end(),
                                [&](const LX::LXGroupSocket& socket) { return socket.internalPin == pin.id; }) ||
                    (direction == LX::Direction::Input &&
                     std::any_of(body.Links().begin(), body.Links().end(),
                                 [&](const LX::Link& link) { return link.input == pin.id; })))
                {
                    continue;
                }
                ImGui::PushID(static_cast<int>(pin.id));
                const std::string pinLabel = node.title + " / " + pin.name;
                if (ImGui::Selectable(("+ " + pinLabel).c_str()))
                {
                    std::string identifier = pin.Identifier();
                    for (unsigned suffix = 2; std::any_of(editor.sockets.begin(), editor.sockets.end(),
                                                          [&](const LX::LXGroupSocket& socket) {
                                                              return socket.direction == direction &&
                                                                     socket.identifier == identifier;
                                                          });
                         ++suffix)
                    {
                        identifier = pin.Identifier() + "_" + std::to_string(suffix);
                    }
                    editor.sockets.push_back({0, identifier, pin.name, direction, pin.type, pin.id, pin.value});
                }
                if (gGroupEditorProbe)
                {
                    const ImVec2 a = ImGui::GetItemRectMin();
                    const ImVec2 b = ImGui::GetItemRectMax();
                    auto& candidates = direction == LX::Direction::Input ? gGroupEditorProbe->inputCandidates
                                                                         : gGroupEditorProbe->outputCandidates;
                    candidates[pinLabel] = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
                }
                ImGui::PopID();
            }
        }
    };
    if (boundaryMode)
    {
        const auto addSocket = [&](LX::Direction direction, LX::Id boundary, LX::PinType& selectedType) {
            ImGui::PushID(static_cast<int>(direction));
            const char* label = direction == LX::Direction::Input ? "Input type" : "Output type";
            if (ImGui::BeginCombo(label, LX::PinTypeName(selectedType)))
            {
                for (LX::PinType type :
                     {LX::PinType::Bool, LX::PinType::Int, LX::PinType::Float, LX::PinType::Vector, LX::PinType::Color,
                      LX::PinType::Normal, LX::PinType::Texture, LX::PinType::Surface})
                {
                    if (ImGui::Selectable(LX::PinTypeName(type), selectedType == type))
                    {
                        selectedType = type;
                    }
                }
                ImGui::EndCombo();
            }
            const bool add = boundary && ImGui::Button(direction == LX::Direction::Input ? "Add input" : "Add output");
            if (gGroupEditorProbe && boundary)
            {
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                ImVec2& point = direction == LX::Direction::Input ? gGroupEditorProbe->addInputPoint
                                                                  : gGroupEditorProbe->addOutputPoint;
                point = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
            }
            if (add)
            {
                const char* prefix = direction == LX::Direction::Input ? "input" : "output";
                std::string identifier;
                for (unsigned index = 1;; ++index)
                {
                    identifier = std::string(prefix) + std::to_string(index);
                    if (std::none_of(editor.sockets.begin(), editor.sockets.end(),
                                     [&](const LX::LXGroupSocket& socket) {
                                         return socket.direction == direction && socket.identifier == identifier;
                                     }))
                    {
                        break;
                    }
                }
                const std::string name = direction == LX::Direction::Input ? "Input" : "Output";
                const LX::LXCommandResult added = draft.Execute(
                    LX::LXAddGroupBoundaryPin{boundary, identifier, name, selectedType, {}}, draft.Revision());
                if (added.applied)
                {
                    editor.sockets.push_back({0, identifier, name, direction, selectedType, added.created, {}});
                }
                else
                {
                    editor.message = "Cannot add group interface socket";
                }
            }
            ImGui::PopID();
        };
        addSocket(LX::Direction::Input, inputBoundary, editor.newInputType);
        addSocket(LX::Direction::Output, outputBoundary, editor.newOutputType);
    }
    else
    {
        expose(LX::Direction::Input, "Expose input");
        expose(LX::Direction::Output, "Expose output");
    }
    if (!editor.message.empty())
    {
        ImGui::TextWrapped("%s", editor.message.c_str());
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("Group Canvas", {0.0f, height}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    LX::DrawCanvas(body, editor.canvas, styles, items, &draft);
    DrawPortCreationPopup(body, editor.canvas, &draft);
    ImGui::EndChild();
    ImGui::End();

    if (cancel)
    {
        editor.Cancel();
    }
    else if (apply)
    {
        editor.Apply(parent);
    }
}

void DrawBehaviorUI(LX::LXGraph& graph, LX::CanvasState& canvas, LX::LXStyleSheet& styles,
                    const LX::LXNodeItemRegistry& items, const std::string& file, ExamplePage* page);

bool CanvasRenderTest(float dpi, ImVec2 displaySize, std::size_t extraNodes = 0)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    LoadExampleFont();
    ImGui::GetStyle().ScaleAllSizes(dpi);
    ImGui::GetStyle().FontScaleDpi = dpi;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.DisplaySize = displaySize;
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph graph = Fixture();
    for (std::size_t index = 0; index < extraNodes; ++index)
    {
        const float x = 100.0f + static_cast<float>(index % 20) * 190.0f;
        const float y = 850.0f + static_cast<float>(index / 20) * 140.0f;
        graph.AddNode(Spec(static_cast<int>(index % 7)), x, y);
    }
    LX::CanvasState canvas;
    canvas.selectedNode = graph.Nodes()[3].id;
    LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    ImGui::NewFrame();
    DrawUI(graph, canvas, styles, items, "LatticeExample.lxg");
    ImGui::Render();

    const bool rendered = ImGui::GetDrawData() && ImGui::GetDrawData()->TotalVtxCount > 1000;
    ImGui::DestroyContext();
    return rendered;
}

bool CanvasItemInteractionTest()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph initialGraph;
    const LX::Id nodeId = initialGraph.AddNode(Spec(3), 50.0f, 50.0f);
    const LX::Id colorId = initialGraph.AddNode(Spec(2), 300.0f, 50.0f);
    const std::filesystem::path file = std::filesystem::temp_directory_path() /
                                       ("lattice-ui-values-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    LX::LXDocument document(std::move(initialGraph), file.string());
    LX::LXGraph& graph = document.GraphForCanvas();
    LX::CanvasState canvas;
    canvas.pan = {0.0f, 0.0f};
    LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    ImVec2 sliderPoint{};
    ImVec2 colorPoint{};
    ImVec2 collapsePoint{};
    const auto frame = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Canvas interaction test", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const LX::Node* node = graph.FindNode(nodeId);
        const LX::LXNodeStyle& style = styles.ForNode(*node);
        const LX::NodeLayout& layout = Placement(graph, *node);
        const LX::LXNodeGeometry nodeGeometry = LX::MeasureNode(*node, layout, style, items);
        const float collapseHeight = layout.collapsed ? nodeGeometry.height : style.headerHeight;
        collapsePoint = {origin.x + canvas.pan.x + layout.x + 11.0f,
                         origin.y + canvas.pan.y + layout.y + collapseHeight * 0.5f};
        const LX::LXNodeItemRect rect = LX::ItemRect(*node, layout, "Factor", style, items);
        const float controlX = rect.minimum.x + (rect.maximum.x - rect.minimum.x) * 0.40f;
        const float controlWidth = rect.maximum.x - controlX - style.propertyMargin;
        sliderPoint = {origin.x + controlX + controlWidth * 0.20f, origin.y + (rect.minimum.y + rect.maximum.y) * 0.5f};
        const LX::Node* colorNode = graph.FindNode(colorId);
        const LX::LXNodeStyle& colorStyle = styles.ForNode(*colorNode);
        const LX::LXNodeItemRect colorRect =
            LX::ItemRect(*colorNode, Placement(graph, *colorNode), "Value", colorStyle, items);
        const float swatchSize = colorRect.maximum.y - colorRect.minimum.y - 6.0f;
        colorPoint = {origin.x + colorRect.maximum.x - colorStyle.propertyMargin - swatchSize * 0.5f,
                      origin.y + (colorRect.minimum.y + colorRect.maximum.y) * 0.5f};
        LX::DrawCanvas(graph, canvas, styles, items, &document);
        ImGui::End();
        ImGui::Render();
    };

    io.AddMousePosEvent(0.0f, 0.0f);
    frame();
    io.AddMousePosEvent(sliderPoint.x, sliderPoint.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();

    const LX::Node* node = graph.FindNode(nodeId);
    const LX::Id factor = node ? node->pins[2].id : 0;
    const bool changed =
        factor && graph.FindPin(factor)->value != LX::LXSocketValue{0.8} && canvas.dirty && document.Revision() == 1;
    const bool saved = changed && document.Execute(LX::LXSave{}, document.Revision()).applied;
    const auto reopened = saved ? LX::LXDocument::Open(file.string()) : std::nullopt;
    const bool roundTrip = reopened && graph.Equals(reopened->Graph()) &&
                           reopened->Graph().FindPin(factor)->value == graph.FindPin(factor)->value;
    const bool undo = changed && document.Execute(LX::LXUndo{}, document.Revision()).applied &&
                      graph.FindPin(factor)->value == LX::LXSocketValue{0.8};
    io.AddMousePosEvent(collapsePoint.x, collapsePoint.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    const bool collapsed = graph.FindLayout(nodeId)->collapsed;
    io.AddMousePosEvent(collapsePoint.x, collapsePoint.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    const bool expandedAgain = !graph.FindLayout(nodeId)->collapsed;
    io.AddMousePosEvent(colorPoint.x, colorPoint.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    const bool colorOpened = canvas.colorNode == colorId && canvas.colorKey == "Value";
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    if (!changed || !roundTrip || !undo || !colorOpened || !collapsed || !expandedAgain)
    {
        std::cerr << "Interaction failure: slider=" << changed << " roundTrip=" << roundTrip << " undo=" << undo
                  << " color=" << colorOpened << " collapse=" << collapsed << " expand=" << expandedAgain << std::endl;
    }
    ImGui::DestroyContext();
    return changed && roundTrip && undo && colorOpened && collapsed && expandedAgain;
}

bool InspectorInputTest()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440.0f, 900.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::NodeSpec spec{"UI_VALUES",
                      "Typed Inputs",
                      {WithValue(Input("Flag", LX::PinType::Bool), false),
                       WithValue(Input("Count", LX::PinType::Int), std::int64_t{2}),
                       WithValue(Input("Factor", LX::PinType::Float), 0.5),
                       WithValue(Input("Vector", LX::PinType::Vector), std::array<double, 3>{0.1, 0.2, 0.3}),
                       WithValue(Input("Color", LX::PinType::Color), std::array<double, 4>{0.1, 0.2, 0.3, 1.0}),
                       WithValue(Input("Image", LX::PinType::Texture), std::string("T_Old")),
                       WithValue(Input("Normal", LX::PinType::Normal), std::array<double, 3>{0.0, 0.0, 1.0})}};
    LX::LXGraph initial;
    const LX::Id nodeId = initial.AddNode(spec, 50.0f, 50.0f);
    const std::filesystem::path file = std::filesystem::temp_directory_path() /
                                       ("lattice-inspector-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    LX::LXDocument document(std::move(initial), file.string());
    LX::LXGraph& graph = document.GraphForCanvas();
    LX::CanvasState canvas;
    canvas.selectedNode = nodeId;
    LX::LXStyleSheet styles = ExampleStyles();
    LX::LXNodeItemRegistry items;
    for (const char* key : {"Flag", "Count", "Factor", "Vector", "Color", "Image", "Normal"})
    {
        items.Register("UI_VALUES", {key, LX::LXNodeItemKind::Text, 0.0f, 1.0f, 0.0f, "", "", "", key});
    }
    InspectorProbe probe;
    gInspectorProbe = &probe;
    const auto frame = [&]() {
        probe.controls.clear();
        ImGui::NewFrame();
        DrawUI(graph, canvas, styles, items, "LatticeInspectorTest.lxg", nullptr, &document);
        ImGui::Render();
    };
    const auto click = [&](ImVec2 point) {
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    const auto typeText = [&](const char* value) {
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
        io.AddInputCharactersUTF8(value);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    const auto pin = [&](std::size_t index) { return graph.FindNode(nodeId)->pins[index].id; };

    io.AddMousePosEvent(0.0f, 0.0f);
    frame();
    const bool allVisible = probe.controls.size() == 7;
    if (allVisible)
    {
        click(probe.controls.at("Flag"));
    }
    const bool booleanChanged = graph.FindPin(pin(0))->value == LX::LXSocketValue{true};
    if (allVisible)
    {
        click(probe.controls.at("Count"));
        typeText("17");
    }
    const bool integerChanged = graph.FindPin(pin(1))->value == LX::LXSocketValue{std::int64_t{17}};
    if (allVisible)
    {
        click(probe.controls.at("Factor"));
        typeText("0.75");
    }
    const bool floatChanged = graph.FindPin(pin(2))->value == LX::LXSocketValue{0.75};
    if (allVisible)
    {
        click(probe.controls.at("Vector"));
        typeText("0.9");
    }
    const bool vectorChanged = graph.FindPin(pin(3))->value != LX::LXSocketValue{std::array<double, 3>{0.1, 0.2, 0.3}};
    if (allVisible)
    {
        click(probe.controls.at("Normal"));
        typeText("0.6");
    }
    const bool normalChanged = graph.FindPin(pin(6))->value != LX::LXSocketValue{std::array<double, 3>{0.0, 0.0, 1.0}};
    if (allVisible)
    {
        click(probe.controls.at("Image"));
        typeText("T_New");
    }
    const bool textureChanged = graph.FindPin(pin(5))->value == LX::LXSocketValue{std::string("T_New")};
    if (allVisible)
    {
        const ImVec2 point = probe.controls.at("Color");
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMousePosEvent(point.x + 35.0f, point.y);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    }
    const bool colorChanged =
        graph.FindPin(pin(4))->value != LX::LXSocketValue{std::array<double, 4>{0.1, 0.2, 0.3, 1.0}};
    const bool saved = document.Execute(LX::LXSave{}, document.Revision()).applied;
    const auto reopened = saved ? LX::LXDocument::Open(file.string()) : std::nullopt;
    const bool roundTrip = reopened && reopened->Graph().Equals(graph) && !document.Dirty();
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    const bool passed = allVisible && booleanChanged && integerChanged && floatChanged && vectorChanged &&
                        normalChanged && textureChanged && colorChanged && roundTrip && document.Revision() >= 7;
    if (!passed)
    {
        std::cerr << "Inspector inputs failed visible=" << allVisible << " bool=" << booleanChanged
                  << " int=" << integerChanged << " float=" << floatChanged << " vector=" << vectorChanged
                  << " normal=" << normalChanged << " texture=" << textureChanged << " color=" << colorChanged
                  << " roundTrip=" << roundTrip << " revision=" << document.Revision() << std::endl;
    }
    gInspectorProbe = nullptr;
    ImGui::DestroyContext();
    return passed;
}

bool LibrarySearchTest()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440.0f, 840.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXDocument document(Fixture());
    LX::LXGraph& graph = document.GraphForCanvas();
    LX::CanvasState canvas;
    LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    LibraryProbe probe;
    gLibraryProbe = &probe;
    const auto frame = [&]() {
        probe.results.clear();
        ImGui::NewFrame();
        DrawUI(graph, canvas, styles, items, "LatticeSearchTest.lxg", nullptr, &document);
        ImGui::Render();
    };

    io.AddMousePosEvent(0.0f, 0.0f);
    frame();
    io.AddMousePosEvent(probe.searchPoint.x, probe.searchPoint.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    io.AddInputCharactersUTF8("normal");
    frame();
    const bool filtered = probe.results.size() == 2 && probe.results[0].first == 6 && probe.results[1].first == 4;
    if (filtered)
    {
        const ImVec2 target = probe.results[1].second;
        io.AddMousePosEvent(target.x, target.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    }
    const bool created = graph.Nodes().size() == 8 && graph.FindNode(canvas.selectedNode) &&
                         graph.FindNode(canvas.selectedNode)->type == "NORMAL" && document.Revision() == 1 &&
                         document.Dirty();
    gLibraryProbe = nullptr;
    if (!filtered || !created)
    {
        std::cerr << "Library search failure filtered=" << filtered << " created=" << created
                  << " results=" << probe.results.size() << std::endl;
    }
    ImGui::DestroyContext();
    return filtered && created;
}

bool GroupLibraryTest()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440.0f, 840.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph initial = Fixture();
    const LX::Id firstInstance = AddExampleGroup(initial, 690.0f, 570.0f);
    const LX::Id group = firstInstance ? initial.FindNode(firstInstance)->groupId : 0;
    LX::LXDocument document(std::move(initial));
    LX::LXGraph& graph = document.GraphForCanvas();
    LX::CanvasState canvas;
    LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    LibraryProbe probe;
    gLibraryProbe = &probe;
    const auto frame = [&]() {
        probe.groups.clear();
        ImGui::NewFrame();
        DrawUI(graph, canvas, styles, items, "LatticeGroupLibraryTest.lxg", nullptr, &document);
        ImGui::Render();
    };

    io.AddMousePosEvent(0.0f, 0.0f);
    frame();
    const bool listed = group && probe.groups.size() == 1 && probe.groups.front().first == group;
    if (listed)
    {
        const ImVec2 target = probe.groups.front().second;
        io.AddMousePosEvent(target.x, target.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    }
    const LX::Node* createdNode = graph.FindNode(canvas.selectedNode);
    const bool created = graph.Nodes().size() == 9 && createdNode && createdNode->groupId == group &&
                         document.Revision() == 1 && document.Dirty() && graph.Validate().empty();
    gLibraryProbe = nullptr;
    if (!listed || !created)
    {
        std::cerr << "Group library failure listed=" << listed << " created=" << created << std::endl;
    }
    ImGui::DestroyContext();
    return listed && created;
}

bool DynamicPinUiTest()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {500.0f, 300.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph graph;
    const LX::Id nodeId = graph.AddNode(Spec(3), 0.0f, 0.0f);
    LX::CanvasState canvas;
    DynamicPinProbe probe;
    gDynamicPinProbe = &probe;
    const auto frame = [&]() {
        probe.removePoints.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Dynamic pin UI", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        DrawDynamicPinControls(graph, canvas, nodeId);
        ImGui::End();
        ImGui::Render();
    };
    const auto click = [&](ImVec2 position) {
        io.AddMousePosEvent(position.x, position.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    io.AddMousePosEvent(0.0f, 0.0f);
    frame();
    click(probe.addPoint);
    const bool added = graph.FindNode(nodeId)->pins.size() == 5 && probe.removePoints.size() == 1 && canvas.dirty;
    if (added)
    {
        click(probe.removePoints.front());
    }
    const bool removed =
        graph.FindNode(nodeId)->pins.size() == 4 && graph.Undo() && graph.FindNode(nodeId)->pins.size() == 5;
    gDynamicPinProbe = nullptr;
    if (!added || !removed)
    {
        std::cerr << "Dynamic pin UI failure add=" << added << " remove=" << removed << std::endl;
    }
    ImGui::DestroyContext();
    return added && removed;
}

bool PortCreationTest()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {900.0f, 640.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph graph;
    const LX::Id source = graph.AddNode(Spec(2), 60.0f, 80.0f);
    LX::CanvasState canvas;
    canvas.pan = {20.0f, 20.0f};
    const LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    CreationProbe probe;
    gCreationProbe = &probe;
    ImVec2 origin{};
    const auto frame = [&]() {
        probe.results.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Port creation test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        origin = ImGui::GetCursorScreenPos();
        LX::DrawCanvas(graph, canvas, styles, items);
        DrawPortCreationPopup(graph, canvas);
        ImGui::End();
        ImGui::Render();
    };
    io.AddMousePosEvent(0.0f, 0.0f);
    frame();
    const LX::Node* node = graph.FindNode(source);
    const ImVec2 pin =
        LX::PinPosition(*node, Placement(graph, *node), node->pins.front(), styles.ForNode(*node), &items);
    const ImVec2 start{origin.x + canvas.pan.x + pin.x, origin.y + canvas.pan.y + pin.y};
    io.AddMousePosEvent(start.x, start.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(400.0f, 250.0f);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    const bool opened = canvas.creationPopupOpen && canvas.creationPin == node->pins.front().id;
    io.AddMousePosEvent(probe.searchPoint.x, probe.searchPoint.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    io.AddInputCharactersUTF8("Multiply");
    frame();
    const bool filtered = probe.results.size() == 1 && probe.results.front().first == 3;
    if (filtered)
    {
        const ImVec2 target = probe.results.front().second;
        io.AddMousePosEvent(target.x, target.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    }
    const bool created = graph.Nodes().size() == 2 && graph.Links().size() == 1 &&
                         graph.FindNode(canvas.selectedNode) &&
                         graph.FindNode(canvas.selectedNode)->type == "MULTIPLY" && graph.Validate().empty();
    const bool atomicUndo = created && graph.Undo() && graph.Nodes().size() == 1 && graph.Links().empty() &&
                            graph.Redo() && graph.Nodes().size() == 2 && graph.Links().size() == 1;
    gCreationProbe = nullptr;
    if (!opened || !filtered || !created || !atomicUndo)
    {
        std::cerr << "Port creation failure opened=" << opened << " filter=" << filtered << " create=" << created
                  << " atomicUndo=" << atomicUndo << " query=" << canvas.creationQuery.data()
                  << " results=" << probe.results.size() << std::endl;
    }
    ImGui::DestroyContext();
    return opened && filtered && created && atomicUndo;
}

bool ProblemNavigationTest()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {500.0f, 300.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph graph;
    const LX::Id nodeId =
        graph.AddNode({"INVALID", "Duplicate Inputs", {Input("A", LX::PinType::Color), Input("A", LX::PinType::Color)}},
                      600.0f, 300.0f);
    LX::CanvasState canvas;
    canvas.canvasSize = {500.0f, 400.0f};
    ProblemProbe probe;
    gProblemProbe = &probe;
    const auto frame = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Problem navigation test", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        DrawProblems(graph, canvas);
        ImGui::End();
        ImGui::Render();
    };
    io.AddMousePosEvent(0.0f, 0.0f);
    frame();
    const bool diagnosed = graph.Validate().size() == 2 && graph.Validate().front().code == "pin_identifier";
    io.AddMousePosEvent(probe.firstPoint.x, probe.firstPoint.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    const bool navigated = canvas.selectedNode == nodeId && canvas.pan.x == -350.0f && canvas.pan.y == -100.0f &&
                           canvas.message == "Pin identifier is empty or duplicated";
    gProblemProbe = nullptr;
    if (!diagnosed || !navigated)
    {
        std::cerr << "Problem navigation failure diagnosed=" << diagnosed << " navigated=" << navigated << std::endl;
    }
    ImGui::DestroyContext();
    return diagnosed && navigated;
}

bool DynamicPinTest()
{
    LX::LXGraph graph;
    const LX::Id color = graph.AddNode(Spec(2), 0.0f, 0.0f);
    const LX::Id multiply = graph.AddNode(Spec(3), 200.0f, 0.0f);
    const LX::Id first = graph.AddDynamicPin(multiply, "Extra 1");
    const LX::Id second = graph.AddDynamicPin(multiply, "Extra 2");
    const LX::Id third = graph.AddDynamicPin(multiply, "Extra 3");
    const LX::Id fourth = graph.AddDynamicPin(multiply, "Extra 4");
    if (!first || !second || !third || !fourth || graph.AddDynamicPin(multiply, "Extra 1") ||
        graph.AddDynamicPin(multiply, "Extra 5") || graph.AddDynamicPin(color, "Forbidden") ||
        !graph.MoveDynamicPin(second, -1) || graph.FindNode(multiply)->pins[4].id != second)
    {
        std::cerr << "Dynamic pin creation or ordering failed" << std::endl;
        return false;
    }
    const LX::Id output = graph.FindNode(color)->pins.front().id;
    if (!graph.Connect(output, first) || graph.Links().size() != 1 || !graph.RemoveDynamicPin(first) ||
        graph.FindPin(first) || !graph.Links().empty() || !graph.Undo() || !graph.FindPin(first) ||
        graph.Links().size() != 1 || !graph.Redo() || graph.FindPin(first) || !graph.Validate().empty())
    {
        std::cerr << "Dynamic pin link removal or undo failed" << std::endl;
        return false;
    }
    std::string error;
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / ("lattice-dynamic-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    const bool saved = graph.Save(file.string(), &error);
    const auto loaded = saved ? LX::LXGraph::Load(file.string(), &error) : std::nullopt;
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    if (!loaded || !graph.Equals(*loaded))
    {
        std::cerr << "Dynamic pin round-trip failed: " << error << std::endl;
        return false;
    }
    {
        std::ofstream legacy(file, std::ios::binary | std::ios::trunc);
        legacy << "LXG 2 \"material\" 2\nN 1\n1 \"LEGACY\" \"Legacy\" 0 0 0 0 0\nL 0\n";
    }
    auto migrated = LX::LXGraph::Load(file.string(), &error);
    const bool legacyLoaded = migrated && migrated->Nodes().size() == 1 && !migrated->Nodes().front().dynamicPins &&
                              migrated->Validate().empty();
    const bool legacySaved = legacyLoaded && migrated->Save(file.string(), &error);
    const auto reopened = legacySaved ? LX::LXGraph::Load(file.string(), &error) : std::nullopt;
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    if (!reopened || !migrated->Equals(*reopened))
    {
        std::cerr << "LXG 2 migration failed: " << error << std::endl;
        return false;
    }
    return true;
}

bool CanvasGestureTest(float dpi, ImVec2 displaySize, bool documentBacked = false)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGui::GetStyle().ScaleAllSizes(dpi);
    ImGui::GetStyle().FontScaleDpi = dpi;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = displaySize;
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph initialGraph;
    const LX::Id source = initialGraph.AddNode(Spec(2), 60.0f, 80.0f);
    const LX::Id target = initialGraph.AddNode(Spec(3), 330.0f, 90.0f);
    std::optional<LX::LXDocument> document;
    if (documentBacked)
    {
        document.emplace(std::move(initialGraph));
    }
    LX::LXGraph& graph = document ? document->GraphForCanvas() : initialGraph;
    LX::CanvasState canvas;
    canvas.pan = {20.0f * dpi, 20.0f * dpi};
    LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    ImVec2 origin{};
    const auto frame = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Canvas gesture test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        origin = ImGui::GetCursorScreenPos();
        LX::DrawCanvas(graph, canvas, styles, items, document ? &*document : nullptr);
        ImGui::End();
        ImGui::Render();
    };
    const auto point = [&](LX::Id nodeId, std::size_t pinIndex) {
        const LX::Node* node = graph.FindNode(nodeId);
        const ImVec2 position =
            LX::PinPosition(*node, Placement(graph, *node), node->pins[pinIndex], styles.ForNode(*node), &items);
        const float scale = canvas.zoom * dpi;
        return ImVec2{origin.x + canvas.pan.x + position.x * scale, origin.y + canvas.pan.y + position.y * scale};
    };
    const auto click = [&](ImVec2 position) {
        io.AddMousePosEvent(position.x, position.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };

    io.AddMousePosEvent(0.0f, 0.0f);
    frame();
    click(point(source, 0));
    click(point(target, 0));
    const bool connected = graph.Links().size() == 1 && canvas.pendingPin == 0;
    const bool connectRevision = !document || document->Revision() == 1;
    click(point(source, 0));
    click(point(target, 2));
    const bool rejected = graph.Links().size() == 1 && canvas.message == "Pin types differ";
    const bool rejectedRevision = !document || document->Revision() == 1;

    if (document)
    {
        document->Execute(LX::LXDisconnectLink{graph.Links().front().id}, document->Revision());
    }
    else
    {
        graph.Disconnect(graph.Links().front().id);
    }
    const ImVec2 from = point(source, 0);
    const ImVec2 to = point(target, 0);
    io.AddMousePosEvent(from.x, from.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(to.x, to.y);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    const bool dragConnected = graph.Links().size() == 1 && canvas.pendingPin == 0;
    const bool dragConnectRevision = !document || document->Revision() == 3;

    const LX::Node* node = graph.FindNode(target);
    const LX::NodeLayout& targetLayout = Placement(graph, *node);
    const ImVec2 start{origin.x + canvas.pan.x + (targetLayout.x + 55.0f) * dpi,
                       origin.y + canvas.pan.y + (targetLayout.y + 11.0f) * dpi};
    io.AddMousePosEvent(start.x, start.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(start.x + 42.0f * dpi, start.y + 27.0f * dpi);
    frame();
    const bool previewRevision = !document || document->Revision() == 3;
    io.AddMouseButtonEvent(0, false);
    frame();
    const auto atPosition = [&](float x, float y) {
        const LX::NodeLayout* moved = graph.FindLayout(target);
        const float pixel = 0.51f / dpi;
        return std::abs(moved->x - x) < pixel && std::abs(moved->y - y) < pixel;
    };
    const bool dragged = atPosition(372.0f, 117.0f);
    const bool dragRevision = !document || document->Revision() == 4;

    io.AddMousePosEvent(500.0f * dpi, 350.0f * dpi);
    frame();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Z, true);
    frame();
    const bool undone = atPosition(330.0f, 90.0f);
    io.AddKeyEvent(ImGuiKey_Z, false);
    frame();
    io.AddKeyEvent(ImGuiKey_Y, true);
    frame();
    const bool redone = atPosition(372.0f, 117.0f);
    io.AddKeyEvent(ImGuiKey_Y, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();

    const auto titlePoint = [&](LX::Id id) {
        const LX::NodeLayout* current = graph.FindLayout(id);
        const float scale = canvas.zoom * dpi;
        return ImVec2{origin.x + canvas.pan.x + (current->x + 55.0f) * scale,
                      origin.y + canvas.pan.y + (current->y + 11.0f) * scale};
    };
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    frame();
    click(titlePoint(source));
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    const bool multiSelected = canvas.selectedNodes.size() == 2;
    const LX::NodePosition sourceBefore{source, graph.FindLayout(source)->x, graph.FindLayout(source)->y};
    const LX::NodePosition targetBefore{target, graph.FindLayout(target)->x, graph.FindLayout(target)->y};
    const ImVec2 groupStart = titlePoint(target);
    io.AddMousePosEvent(groupStart.x, groupStart.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(groupStart.x + 20.0f * dpi, groupStart.y + 10.0f * dpi);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    const bool groupMoved = multiSelected &&
                            std::abs(graph.FindLayout(source)->x - sourceBefore.x - 20.0f) < 0.51f / dpi &&
                            std::abs(graph.FindLayout(target)->x - targetBefore.x - 20.0f) < 0.51f / dpi;

    const float oldZoom = canvas.zoom;
    io.AddMouseWheelEvent(0.0f, 1.0f);
    frame();
    const bool zoomed = canvas.zoom > oldZoom;
    const ImVec2 oldPan = canvas.pan;
    io.AddMouseButtonEvent(2, true);
    frame();
    io.AddMousePosEvent(525.0f * dpi, 365.0f * dpi);
    frame();
    io.AddMouseButtonEvent(2, false);
    frame();
    const bool panned = canvas.pan.x > oldPan.x && canvas.pan.y > oldPan.y;

    io.AddKeyEvent(ImGuiKey_Delete, true);
    frame();
    const bool deleted = !graph.FindNode(source) && !graph.FindNode(target);
    io.AddKeyEvent(ImGuiKey_Delete, false);
    frame();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Z, true);
    frame();
    const bool deleteUndone = deleted && graph.FindNode(source) && graph.FindNode(target);
    io.AddKeyEvent(ImGuiKey_Z, false);
    frame();
    io.AddKeyEvent(ImGuiKey_Z, true);
    frame();
    const bool groupUndo = deleteUndone && std::abs(graph.FindLayout(source)->x - sourceBefore.x) < 0.01f &&
                           std::abs(graph.FindLayout(target)->x - targetBefore.x) < 0.01f;
    io.AddKeyEvent(ImGuiKey_Z, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();

    click(titlePoint(source));
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    frame();
    click(titlePoint(target));
    const bool copySelection = canvas.selectedNodes.size() == 2;
    io.AddKeyEvent(ImGuiKey_C, true);
    frame();
    io.AddKeyEvent(ImGuiKey_C, false);
    frame();
    io.AddKeyEvent(ImGuiKey_V, true);
    frame();
    const bool pasted = copySelection && graph.Nodes().size() == 4 && graph.Links().size() == 2 &&
                        canvas.selectedNodes.size() == 2 && !graph.Validate().size();
    io.AddKeyEvent(ImGuiKey_V, false);
    frame();
    io.AddKeyEvent(ImGuiKey_Z, true);
    frame();
    const bool pasteUndone = pasted && graph.Nodes().size() == 2 && graph.Links().size() == 1;
    io.AddKeyEvent(ImGuiKey_Z, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();

    click(titlePoint(target));
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    frame();
    click(titlePoint(source));
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    const LX::NodePosition snapSource{source, graph.FindLayout(source)->x, graph.FindLayout(source)->y};
    const LX::NodePosition snapTarget{target, graph.FindLayout(target)->x, graph.FindLayout(target)->y};
    canvas.snapToGrid = true;
    const ImVec2 snapStart = titlePoint(target);
    io.AddMousePosEvent(snapStart.x, snapStart.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(snapStart.x + 23.0f * dpi, snapStart.y + 17.0f * dpi);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    const auto* snappedSource = graph.FindLayout(source);
    const auto* snappedTarget = graph.FindLayout(target);
    const float spacing = styles.canvas.gridSpacing;
    const bool snapped = std::abs(std::remainder(snappedTarget->x, spacing)) < 0.01f &&
                         std::abs(std::remainder(snappedTarget->y, spacing)) < 0.01f &&
                         std::abs((snappedTarget->x - snappedSource->x) - (snapTarget.x - snapSource.x)) < 0.01f &&
                         std::abs((snappedTarget->y - snappedSource->y) - (snapTarget.y - snapSource.y)) < 0.01f;
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_Z, true);
    frame();
    const bool snapUndone = std::abs(graph.FindLayout(target)->x - snapTarget.x) < 0.01f &&
                            std::abs(graph.FindLayout(target)->y - snapTarget.y) < 0.01f &&
                            std::abs(graph.FindLayout(source)->x - snapSource.x) < 0.01f &&
                            std::abs(graph.FindLayout(source)->y - snapSource.y) < 0.01f;
    io.AddKeyEvent(ImGuiKey_Z, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    canvas.snapToGrid = false;
    frame();

    const bool passed = connected && connectRevision && rejected && rejectedRevision && dragConnected &&
                        dragConnectRevision && dragged && previewRevision && dragRevision && undone && redone &&
                        zoomed && panned && multiSelected && groupMoved && snapped && snapUndone && deleted &&
                        deleteUndone && groupUndo && pasted && pasteUndone;
    if (!passed)
    {
        std::cerr << "Gesture failure dpi=" << dpi << " document=" << documentBacked << " size=" << displaySize.x << 'x'
                  << displaySize.y << " connect=" << connected << " reject=" << rejected
                  << " dragConnect=" << dragConnected << " drag=" << dragged << " revisions=" << connectRevision
                  << rejectedRevision << dragConnectRevision << previewRevision << dragRevision << " undo=" << undone
                  << " redo=" << redone << " zoom=" << zoomed << " pan=" << panned << " multi=" << multiSelected
                  << " groupMove=" << groupMoved << " snap=" << snapped << " snapUndo=" << snapUndone
                  << " delete=" << deleted << " deleteUndo=" << deleteUndone << " groupUndo=" << groupUndo
                  << " paste=" << pasted << " pasteUndo=" << pasteUndone << std::endl;
    }
    ImGui::DestroyContext();
    return passed;
}

bool NativeWindowMetricsTest()
{
    ImGui_ImplWin32_EnableDpiAwareness();
    std::vector<std::pair<HMONITOR, RECT>> monitors;
    const BOOL enumerated = EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT bounds, LPARAM context) -> BOOL {
            auto* entries = reinterpret_cast<std::vector<std::pair<HMONITOR, RECT>>*>(context);
            entries->emplace_back(monitor, *bounds);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));
    if (!enumerated || monitors.empty())
    {
        std::cerr << "Cannot enumerate display monitors" << std::endl;
        return false;
    }
    const wchar_t* className = L"LatticeHiddenMetricsTest";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass))
    {
        std::cerr << "Cannot register hidden metrics window" << std::endl;
        return false;
    }

    bool okay = true;
    for (const auto& [monitor, bounds] : monitors)
    {
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, className, L"Lattice Metrics", WS_POPUP,
                                      bounds.left + 20, bounds.top + 20, 640, 480, nullptr, nullptr,
                                      windowClass.hInstance, nullptr);
        if (!window)
        {
            okay = false;
            break;
        }
        const UINT windowDpi = GetDpiForWindow(window);
        const float scale = static_cast<float>(windowDpi) / 96.0f;
        const int width = static_cast<int>(900.0f * scale);
        const int height = static_cast<int>(640.0f * scale);
        const bool resized = SetWindowPos(window, nullptr, bounds.left + 20, bounds.top + 20, width, height,
                                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != 0;
        RECT client{};
        GetClientRect(window, &client);
        const bool metrics = resized && windowDpi > 0 &&
                             MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == monitor &&
                             client.right - client.left == width && client.bottom - client.top == height;
        DestroyWindow(window);
        std::cout << "LX_MONITOR dpi=" << windowDpi << " client=" << width << 'x' << height << " origin=" << bounds.left
                  << ',' << bounds.top << std::endl;
        if (!metrics || !CanvasGestureTest(scale, {static_cast<float>(width), static_cast<float>(height)}))
        {
            std::cerr << "Hidden monitor metrics failed dpi=" << windowDpi << " size=" << width << 'x' << height
                      << std::endl;
            okay = false;
            break;
        }
    }
    UnregisterClassW(className, windowClass.hInstance);
    if (okay)
    {
        std::cout << "LX_MONITOR_METRICS_OK " << monitors.size() << std::endl;
    }
    return okay;
}

bool PersistenceTest()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / (L"lattice-selftest-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const std::filesystem::path graphPath = directory / L"\uC655\uBCF5.lxg";
    const std::filesystem::path stylePath = directory / L"\uCC3D\uC2A4\uD0C0\uC77C.lxstyle";
    const auto name = [](const std::filesystem::path& path) {
        const auto utf8 = path.u8string();
        return std::string(utf8.begin(), utf8.end());
    };
    LX::LXGraph graph = Fixture();
    LX::LXStyleSheet styles = ExampleStyles();
    const LX::Id id = graph.Nodes()[0].id;
    styles.frame.header = Rgb(58, 77, 94);
    styles.frame.headerHeight = 27.0f;
    LX::LXNodeStyle nodeStyle = styles.ForNode(graph.Nodes()[0]);
    nodeStyle.pinLayout = LX::LXPinLayout::TopBottom;
    styles.SetNodeStyle(id, nodeStyle);
    LX::LXItemStyle itemStyle;
    itemStyle.fill = Rgb(21, 83, 141);
    styles.SetItemStyle("bt.decorator", itemStyle);
    graph.SetNodeCollapsed(id, true);
    const LX::LXNodeItemRegistry materialItems = ExampleItems();
    const LX::Node* collapsed = graph.FindNode(id);
    const LX::NodeLayout& collapsedLayout = Placement(graph, *collapsed);
    const LX::LXNodeGeometry geometry =
        LX::MeasureNode(*collapsed, collapsedLayout, styles.ForNode(*collapsed), materialItems);
    const ImVec2 output = LX::PinPosition(*collapsed, collapsedLayout, collapsed->pins.front(),
                                          styles.ForNode(*collapsed), &materialItems);
    const float collapsedY = collapsedLayout.y;
    const bool undo = graph.Undo();
    const bool undoState = undo && !graph.FindLayout(id)->collapsed;
    const bool redo = graph.Redo();
    const bool redoState = redo && graph.FindLayout(id)->collapsed;
    if (geometry.height > 30.0f || output.y != collapsedY + geometry.height || !undoState || !redoState)
    {
        std::cerr << "Collapse geometry or undo failed: height=" << geometry.height << " output=" << output.y
                  << " node=" << collapsedY << " undo=" << undoState << " redo=" << redoState << std::endl;
        std::filesystem::remove_all(directory);
        return false;
    }

    const auto normal = std::find_if(graph.Nodes().begin(), graph.Nodes().end(),
                                     [](const LX::Node& node) { return node.type == "NORMAL"; });
    if (normal == graph.Nodes().end())
    {
        std::cerr << "Normal Map fixture is missing" << std::endl;
        std::filesystem::remove_all(directory);
        return false;
    }
    graph.SetNodeCollapsed(normal->id, true);
    const LX::LXNodeStyle& normalStyle = styles.ForNode(*normal);
    const LX::NodeLayout& normalLayout = Placement(graph, *normal);
    const LX::LXNodeGeometry normalGeometry = LX::MeasureNode(*normal, normalLayout, normalStyle, materialItems);
    const ImVec2 inputPoint = LX::PinPosition(*normal, normalLayout, normal->pins.front(), normalStyle, &materialItems);
    const ImVec2 outputPoint = LX::PinPosition(*normal, normalLayout, normal->pins.back(), normalStyle, &materialItems);
    const float normalCenterY = normalLayout.y + normalGeometry.height * 0.5f;
    if (normalGeometry.height > 30.0f || inputPoint.y != normalCenterY || outputPoint.y != normalCenterY ||
        inputPoint.x != normalLayout.x || outputPoint.x != normalLayout.x + normalGeometry.width)
    {
        std::cerr << "Collapsed material socket alignment failed" << std::endl;
        std::filesystem::remove_all(directory);
        return false;
    }

    std::string error;
    bool okay = graph.Save(name(graphPath), &error);
    if (!okay)
    {
        std::cerr << "Graph initial save: " << error << std::endl;
    }
    const bool styleSaved = styles.Save(name(stylePath), &error);
    if (!styleSaved)
    {
        std::cerr << "Style initial save: " << error << std::endl;
    }
    okay = okay && styleSaved;
    auto graphCopy = LX::LXGraph::Load(name(graphPath), &error, ExampleRegistry());
    auto styleCopy = LX::LXStyleSheet::Load(name(stylePath), &error);
    okay = okay && graphCopy && graph.Equals(*graphCopy) && styleCopy && styles.Equals(*styleCopy) &&
           styleCopy->frame.headerHeight == 27.0f;
    const std::filesystem::path legacyV4Path = directory / "legacy-v4.lxstyle";
    {
        std::ifstream source(stylePath, std::ios::binary);
        std::ofstream legacy(legacyV4Path, std::ios::binary | std::ios::trunc);
        std::string line;
        std::size_t remainingStyles = 0;
        while (std::getline(source, line))
        {
            if (line == "LXS 6")
            {
                legacy << "LXS 4\n";
                continue;
            }
            if (line.starts_with("C "))
            {
                line.erase(line.rfind(' '));
                line.erase(line.rfind(' '));
            }
            if (line.starts_with("NT ") || line.starts_with("NI ") || line.starts_with("PT ") ||
                line.starts_with("PI ") || line.starts_with("WT ") || line.starts_with("WI "))
            {
                remainingStyles = static_cast<std::size_t>(std::stoull(line.substr(3)));
            }
            else if (line.starts_with("DN ") || line.starts_with("DP ") || line.starts_with("DW ") ||
                     remainingStyles != 0)
            {
                const std::size_t finalSpace = line.find_last_of(' ');
                line.erase(finalSpace);
                if (remainingStyles != 0)
                {
                    --remainingStyles;
                }
            }
            legacy << line << '\n';
        }
    }
    const auto legacyV4 = LX::LXStyleSheet::Load(name(legacyV4Path), &error);
    okay = okay && legacyV4 && legacyV4->Equals(styles);
    const std::filesystem::path legacyStylePath = directory / "legacy-v3.lxstyle";
    {
        std::ifstream source(legacyV4Path, std::ios::binary);
        std::ofstream legacy(legacyStylePath, std::ios::binary | std::ios::trunc);
        std::string line;
        while (std::getline(source, line))
        {
            if (line == "LXS 4")
            {
                legacy << "LXS 3\n";
            }
            else if (!line.starts_with("FR "))
            {
                legacy << line << '\n';
            }
        }
    }
    const auto legacyStyles = LX::LXStyleSheet::Load(name(legacyStylePath), &error);
    okay = okay && legacyStyles && legacyStyles->frame.headerHeight == LX::LXFrameStyle{}.headerHeight &&
           legacyStyles->defaultNode.width == styles.defaultNode.width;
    okay = okay && graph.Save(name(graphPath), &error) && styles.Save(name(stylePath), &error);
    LX::LXStyleSheet behaviorStyles = BehaviorStyles();
    LX::LXStyleSheet animationStyles = AnimationStyles();
    okay = okay && SaveWindowStyle(name(graphPath), "MaterialGraph", styles, &error) &&
           SaveWindowStyle(name(graphPath), "BehaviorTree", behaviorStyles, &error) &&
           SaveWindowStyle(name(graphPath), "AnimationFSM", animationStyles, &error);
    LX::LXStyleSheet materialWindow = ExampleStyles();
    LX::LXStyleSheet behaviorWindow = ExampleStyles();
    LX::LXStyleSheet animationWindow = ExampleStyles();
    okay = okay && LoadExampleStyles(name(graphPath), materialWindow, &error, "MaterialGraph") &&
           LoadExampleStyles(name(graphPath), behaviorWindow, &error, "BehaviorTree") &&
           LoadExampleStyles(name(graphPath), animationWindow, &error, "AnimationFSM") &&
           styles.Equals(materialWindow) && behaviorStyles.Equals(behaviorWindow) &&
           animationStyles.Equals(animationWindow) && !materialWindow.Equals(behaviorWindow) &&
           !materialWindow.Equals(animationWindow);
    const bool invalidSave = !graph.Save(name(directory / "missing" / "invalid.lxg"), &error);
    graphCopy = LX::LXGraph::Load(name(graphPath), &error, ExampleRegistry());
    okay = okay && invalidSave && graphCopy && graph.Equals(*graphCopy);
    if (!okay)
    {
        std::cerr << "Persistence round-trip: " << error << std::endl;
        std::filesystem::remove_all(directory);
        return false;
    }
    {
        std::ofstream corrupt(graphPath, std::ios::binary | std::ios::trunc);
        corrupt << "broken";
    }
    {
        std::ofstream corrupt(stylePath, std::ios::binary | std::ios::trunc);
        corrupt << "broken";
    }
    graphCopy = LX::LXGraph::Load(name(graphPath), &error, ExampleRegistry());
    const bool graphRecovered = graphCopy && graph.Equals(*graphCopy) && error.find("Recovered") != std::string::npos;
    styleCopy = LX::LXStyleSheet::Load(name(stylePath), &error);
    const bool styleRecovered = styleCopy && styles.Equals(*styleCopy) && error.find("Recovered") != std::string::npos;
    const bool saveRecovered = graph.Save(name(graphPath), &error) && styles.Save(name(stylePath), &error);
    const auto afterSave = LX::LXGraph::Load(name(graphPath), &error, ExampleRegistry());
    const bool intact = afterSave && graph.Equals(*afterSave);
    if (!graphRecovered || !styleRecovered || !saveRecovered || !intact)
    {
        std::cerr << "Recovery failure: graph=" << graphRecovered << " style=" << styleRecovered
                  << " saved=" << saveRecovered << " intact=" << intact << " error=" << error << std::endl;
    }
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    if (cleanupError)
    {
        std::cerr << "Persistence cleanup: " << cleanupError.message() << std::endl;
    }
    return graphRecovered && styleRecovered && saveRecovered && intact;
}

bool DefinitionRegistryTest()
{
    const auto registry = ExampleRegistry();
    auto custom = std::make_shared<LX::LXNodeDefinitionRegistry>();
    std::string error;
    if (custom->Register({"material", Spec(2)}, &error) == false || custom->Register({"material", Spec(2)}, &error) ||
        custom->Register({"material", {"FLOW_IN_MATERIAL", "Invalid", {Input("Exec", LX::PinType::Flow)}}}, &error) ||
        custom->Register({"behavior", {"SURFACE_IN_BEHAVIOR", "Invalid", {Output("Surface", LX::PinType::Surface)}}},
                         &error))
    {
        std::cerr << "Definition registration rules failed" << std::endl;
        return false;
    }

    LX::LXGraph material("material", registry);
    const LX::Id color = material.CreateNode("COLOR", 10.0f, 20.0f);
    LX::NodeSpec forged = Spec(2);
    forged.pins.front().name = "Wrong pin";
    if (!color || material.CreateNode("BT_ROOT", 0.0f, 0.0f) || material.CreateNode("UNKNOWN", 0.0f, 0.0f) ||
        material.AddNode(forged, 0.0f, 0.0f) ||
        material.AddNode({"FLOW_IN_MATERIAL", "Invalid", {Output("Exec", LX::PinType::Flow)}}, 0.0f, 0.0f) ||
        !material.Validate().empty() ||
        material.FindNode(color)->pins.front().value !=
            LX::LXSocketValue{std::array<double, 4>{0.824, 0.773, 0.675, 1.0}})
    {
        std::cerr << "Material definition or domain check failed" << std::endl;
        return false;
    }
    LX::GraphFragment invalidFragment = material.CopyNodes({color});
    invalidFragment.nodes.front().pins.front().type = LX::PinType::Flow;
    if (!material.PasteNodes(invalidFragment, 20.0f, 20.0f).empty() || material.Nodes().size() != 1 ||
        !material.Undo() || !material.Nodes().empty())
    {
        std::cerr << "Invalid fragment changed the material graph" << std::endl;
        return false;
    }
    LX::LXGraph behavior("behavior", registry);
    if (!behavior.CreateNode("BT_ROOT", 0.0f, 0.0f) || behavior.CreateNode("COLOR", 0.0f, 0.0f) ||
        !behavior.Validate().empty())
    {
        std::cerr << "Behavior domain check failed" << std::endl;
        return false;
    }

    const std::filesystem::path base =
        std::filesystem::temp_directory_path() / ("lattice-definition-" + std::to_string(GetCurrentProcessId()));
    const std::filesystem::path unknownPath = base.string() + "-unknown.lxg";
    const std::filesystem::path domainPath = base.string() + "-domain.lxg";
    const std::filesystem::path schemaPath = base.string() + "-schema.lxg";
    const std::filesystem::path legacyPath = base.string() + "-legacy.lxg";
    LX::LXGraph source;
    const LX::Id unknown = source.AddNode({"UNAVAILABLE",
                                           "Legacy Data",
                                           {Output("Color", LX::PinType::Color)},
                                           {{"Payload", "keep exactly"}},
                                           LX::DynamicPinRule{LX::Direction::Input, LX::PinType::Color, false, 2}},
                                          42.0f, 73.0f);
    const LX::Id known = source.AddNode(Spec(3), 250.0f, 73.0f);
    const LX::Id originalLink =
        source.Connect(source.FindNode(unknown)->pins.front().id, source.FindNode(known)->pins.front().id).value_or(0);
    const bool savedUnknown = unknown && known && originalLink && source.Save(unknownPath.string(), &error);
    auto imported = savedUnknown ? LX::LXGraph::Load(unknownPath.string(), &error, registry) : std::nullopt;
    const bool preserved =
        imported && source.Equals(*imported) && imported->Definitions() == registry &&
        imported->Validate().size() == 1 && imported->Validate().front().code == "unknown_node" &&
        imported->Validate().front().severity == LX::Issue::Severity::Warning && !imported->IsNodeEditable(unknown) &&
        !imported->SetProperty(unknown, "Payload", "changed") && !imported->AddDynamicPin(unknown, "Extra") &&
        !imported->RemoveNode(unknown) && !imported->Disconnect(originalLink) &&
        imported->PasteNodes(source.CopyNodes({unknown}), 5, 5).empty() && imported->Save(unknownPath.string(), &error);
    const auto reopened = preserved ? LX::LXGraph::Load(unknownPath.string(), &error, registry) : std::nullopt;
    const bool unknownRoundTrip = reopened && source.Equals(*reopened);

    LX::LXGraph wrongDomain("behavior");
    wrongDomain.AddNode(Spec(2), 0.0f, 0.0f);
    const bool domainSaved = wrongDomain.Save(domainPath.string(), &error);
    const auto rejectedDomain = domainSaved ? LX::LXGraph::Load(domainPath.string(), &error, registry) : std::nullopt;
    const bool domainRejected = !rejectedDomain && error.find("node_domain") != std::string::npos;

    LX::LXGraph wrongSchema;
    wrongSchema.AddNode(forged, 0.0f, 0.0f);
    const bool schemaSaved = wrongSchema.Save(schemaPath.string(), &error);
    const auto rejectedSchema = schemaSaved ? LX::LXGraph::Load(schemaPath.string(), &error, registry) : std::nullopt;
    const bool schemaRejected = !rejectedSchema && error.find("node_schema") != std::string::npos;

    {
        std::ofstream legacy(legacyPath, std::ios::binary | std::ios::trunc);
        legacy << "LXG 2 \"material\" 6\nN 1\n"
                  "1 \"MULTIPLY\" \"Multiply\" 0 0 4 1 0\n"
                  "2 \"A\" 0 5 0\n"
                  "3 \"B\" 0 5 0\n"
                  "4 \"Factor\" 0 3 0\n"
                  "5 \"Result\" 1 5 0\n"
                  "\"Factor\" \"0.80\"\nL 0\n";
    }
    auto migrated = LX::LXGraph::Load(legacyPath.string(), &error, registry);
    const bool legacyMigrated = migrated && migrated->Nodes().size() == 1 &&
                                migrated->Nodes().front().dynamicPins == Spec(3).dynamicPins &&
                                migrated->Validate().empty() && migrated->Save(legacyPath.string(), &error);
    const auto legacyReopened =
        legacyMigrated ? LX::LXGraph::Load(legacyPath.string(), &error, registry) : std::nullopt;
    const bool legacyRoundTrip = legacyReopened && migrated->Equals(*legacyReopened);

    for (const std::filesystem::path& file : {unknownPath, domainPath, schemaPath, legacyPath})
    {
        std::filesystem::remove(file);
        std::filesystem::remove(file.string() + ".bak");
    }
    if (!unknownRoundTrip || !domainRejected || !schemaRejected || !legacyRoundTrip)
    {
        std::cerr << "Definition load, unknown preservation, or rejection failed: " << error << std::endl;
    }
    return unknownRoundTrip && domainRejected && schemaRejected && legacyRoundTrip;
}

bool PinIdentifierTest()
{
    auto definitions = std::make_shared<LX::LXNodeDefinitionRegistry>();
    LX::NodeSpec mix = {"MIX_IDENTIFIER",
                        "Mix",
                        {Input("Factor", LX::PinType::Float), Input("Factor", LX::PinType::Vector),
                         Input("A", LX::PinType::Color), Input("A", LX::PinType::Vector),
                         Output("Result", LX::PinType::Color)}};
    mix.pins[0].identifier = "Factor_Float";
    mix.pins[1].identifier = "Factor_Vector";
    mix.pins[2].identifier = "A_Color";
    mix.pins[3].identifier = "A_Vector";
    std::string error;
    if (!definitions->Register({"material", mix}, &error))
    {
        std::cerr << "Repeated Blender socket labels were rejected: " << error << std::endl;
        return false;
    }
    LX::NodeSpec duplicate = mix;
    duplicate.type = "DUPLICATE_IDENTIFIER";
    duplicate.pins[1].identifier = duplicate.pins[0].identifier;
    if (definitions->Register({"material", duplicate}, &error))
    {
        std::cerr << "Duplicate socket identifiers were accepted" << std::endl;
        return false;
    }
    LX::LXGraph graph("material", definitions);
    const LX::Id node = graph.CreateNode(mix.type, 35.0f, 65.0f);
    if (!node || !graph.Validate().empty() || graph.FindNode(node)->pins[0].Identifier() != "Factor_Float" ||
        graph.FindNode(node)->pins[1].Identifier() != "Factor_Vector")
    {
        std::cerr << "Blender socket identifiers were not preserved" << std::endl;
        return false;
    }
    LX::LXNodeItemRegistry items;
    items.Register(mix.type, {"Vector Factor", LX::LXNodeItemKind::FloatSlider, 0.0f, 1.0f, 20.0f, "Factor_Vector"});
    const LX::Node& instance = *graph.FindNode(node);
    const LX::NodeLayout& instanceLayout = Placement(graph, instance);
    const LX::LXNodeStyle style;
    const LX::LXNodeItemRect rect = LX::ItemRect(instance, instanceLayout, "Vector Factor", style, items);
    const ImVec2 socket = LX::PinPosition(instance, instanceLayout, instance.pins[1], style, &items);
    if (std::abs((rect.minimum.y + rect.maximum.y) * 0.5f - socket.y) > 0.001f ||
        std::abs((rect.minimum.y + rect.maximum.y) * 0.5f -
                 LX::PinPosition(instance, instanceLayout, instance.pins[0], style, &items).y) < 0.001f)
    {
        std::cerr << "Item row did not follow the socket identifier" << std::endl;
        return false;
    }
    const std::filesystem::path file = std::filesystem::temp_directory_path() /
                                       ("lattice-identifier-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    const bool saved = graph.Save(file.string(), &error);
    const auto loaded = saved ? LX::LXGraph::Load(file.string(), &error, definitions) : std::nullopt;
    const bool roundTrip = loaded && graph.Equals(*loaded) && loaded->Validate().empty();
    {
        std::ofstream legacy(file, std::ios::binary | std::ios::trunc);
        legacy << "LXG 3 \"material\" 3\nN 1\n"
                  "1 \"LEGACY_SOCKET\" \"Legacy Socket\" 0 0 1 0 0 0 0 3 0 0\n"
                  "2 \"Color\" 1 5 0 0\nL 0\n";
    }
    const auto legacy = LX::LXGraph::Load(file.string(), &error);
    const bool migrated =
        legacy && legacy->Nodes().front().pins.front().Identifier() == "Color" && legacy->Save(file.string(), &error);
    const auto reopened = migrated ? LX::LXGraph::Load(file.string(), &error) : std::nullopt;
    const bool legacyRoundTrip = reopened && legacy->Equals(*reopened);
    {
        std::ofstream legacyFour(file, std::ios::binary | std::ios::trunc);
        legacyFour << "LXG 4 \"material\" 3\nN 1\n"
                      "1 \"LEGACY_SOCKET\" \"Legacy Socket\" 0 0 1 0 0 0 0 3 0 0\n"
                      "2 \"Color\" \"Color_RGBA\" 1 5 0 0\nL 0\n";
    }
    const auto previous = LX::LXGraph::Load(file.string(), &error);
    const bool versionFour = previous && previous->Nodes().front().pins.front().Identifier() == "Color_RGBA" &&
                             previous->Save(file.string(), &error);
    const auto previousReopened = versionFour ? LX::LXGraph::Load(file.string(), &error) : std::nullopt;
    const bool versionFourRoundTrip = previousReopened && previous->Equals(*previousReopened);
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    if (!roundTrip || !legacyRoundTrip || !versionFourRoundTrip)
    {
        std::cerr << "Socket identifier persistence failed: " << error << std::endl;
    }
    return roundTrip && legacyRoundTrip && versionFourRoundTrip;
}

bool SocketValueTest()
{
    LX::LXGraph graph;
    const LX::NodeSpec values = {"TYPED_VALUES",
                                 "Typed Values",
                                 {Input("Boolean", LX::PinType::Bool), Input("Count", LX::PinType::Int),
                                  Input("Factor", LX::PinType::Float), Input("Vector", LX::PinType::Vector),
                                  Input("Tint", LX::PinType::Color), Input("Image", LX::PinType::Texture)}};
    const LX::Id node = graph.AddNode(values, 10.0f, 20.0f);
    const LX::Id source =
        graph.AddNode({"FLOAT_SOURCE", "Source", {Output("Value", LX::PinType::Float)}}, 200.0f, 20.0f);
    if (!node || !source)
    {
        return false;
    }
    const auto& pins = graph.FindNode(node)->pins;
    const LX::Id boolean = pins[0].id;
    const LX::Id count = pins[1].id;
    const LX::Id factor = pins[2].id;
    const LX::Id vector = pins[3].id;
    const LX::Id tint = pins[4].id;
    const LX::Id image = pins[5].id;
    if (!graph.SetSocketValue(boolean, true) || !graph.SetSocketValue(count, std::int64_t{42}) ||
        !graph.SetSocketValue(factor, 0.12345678901234566) ||
        !graph.SetSocketValue(vector, std::array<double, 3>{1.0, 2.0, 3.0}) ||
        !graph.SetSocketValue(tint, std::array<double, 4>{0.1, 0.2, 0.3, 1.0}) ||
        !graph.SetSocketValue(image, std::string("Textures/Stone.png")) ||
        graph.SetSocketValue(factor, std::string("wrong type")) ||
        graph.SetSocketValue(factor, std::numeric_limits<double>::infinity()) || !graph.Validate().empty())
    {
        std::cerr << "Typed socket assignment failed" << std::endl;
        return false;
    }
    const LX::Id output = graph.FindNode(source)->pins.front().id;
    const LX::Id link = graph.Connect(output, factor).value_or(0);
    if (!link || graph.SetSocketValue(factor, 0.5) || !graph.Disconnect(link) ||
        graph.FindPin(factor)->value != LX::LXSocketValue{0.12345678901234566} || !graph.SetSocketValue(factor, 0.5) ||
        !graph.Undo() || graph.FindPin(factor)->value != LX::LXSocketValue{0.12345678901234566} || !graph.Redo() ||
        graph.FindPin(factor)->value != LX::LXSocketValue{0.5})
    {
        std::cerr << "Connected socket value or Undo/Redo failed" << std::endl;
        return false;
    }
    std::string error;
    const std::filesystem::path file = std::filesystem::temp_directory_path() /
                                       ("lattice-socket-value-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    const bool saved = graph.Save(file.string(), &error);
    const auto loaded = saved ? LX::LXGraph::Load(file.string(), &error) : std::nullopt;
    std::filesystem::remove(file.string() + ".bak");
    {
        std::ofstream invalid(file, std::ios::binary | std::ios::trunc);
        invalid << "LXG 5 \"material\" 3\nN 1\n"
                   "1 \"INVALID_VALUE\" \"Invalid Value\" 0 0 1 0 0 0 0 3 0 0\n"
                   "2 \"Color\" \"Color\" 0 5 0 0 3 0.5\nL 0\n";
    }
    const auto rejected = LX::LXGraph::Load(file.string(), &error);
    const bool invalidRejected = !rejected && error.find("socket_value") != std::string::npos;
    {
        std::ofstream previous(file, std::ios::binary | std::ios::trunc);
        previous << "LXG 5 \"material\" 3\nN 1\n"
                    "1 \"PREVIOUS_LAYOUT\" \"Previous Layout\" 27.5 39.25 1 0 1 0 0 3 0 0\n"
                    "2 \"Factor\" \"Factor\" 0 3 0 0 3 0.25\nL 0\n";
    }
    const auto previous = LX::LXGraph::Load(file.string(), &error);
    const bool migrated = previous && previous->FindLayout(1) && previous->FindLayout(1)->x == 27.5f &&
                          previous->FindLayout(1)->y == 39.25f && previous->FindLayout(1)->collapsed &&
                          previous->FindPin(2)->value == LX::LXSocketValue{0.25} &&
                          previous->Save(file.string(), &error);
    const auto reopened = migrated ? LX::LXGraph::Load(file.string(), &error) : std::nullopt;
    const bool layoutRoundTrip = reopened && previous->Equals(*reopened);
    std::filesystem::remove(file.string() + ".bak");
    {
        std::ofstream missing(file, std::ios::binary | std::ios::trunc);
        missing << "LXG 6 \"material\" 3\nN 1\n"
                   "1 \"MISSING_LAYOUT\" \"Missing Layout\" 1 0 0 0 3 0 0\n"
                   "2 \"Factor\" \"Factor\" 0 3 0 0 3 0.25\nP 0\nL 0\n";
    }
    const auto missing = LX::LXGraph::Load(file.string(), &error);
    const bool missingRejected = !missing && error.find("node_layout") != std::string::npos;
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    if (!loaded || !graph.Equals(*loaded) || !loaded->Validate().empty() || !invalidRejected || !layoutRoundTrip ||
        !missingRejected)
    {
        std::cerr << "Typed socket round-trip failed: " << error << std::endl;
        return false;
    }
    return true;
}

bool DocumentCommandTest()
{
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / ("lattice-document-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    LX::LXDocument document(LX::LXGraph("material", ExampleRegistry()), file.string());
    const LX::Id documentId = document.DocumentId();
    const auto color = document.Execute(LX::LXCreateNode{"COLOR", 10.0f, 20.0f}, 0);
    const auto multiply = document.Execute(LX::LXCreateNode{"MULTIPLY", 200.0f, 20.0f}, color.revision);
    if (!documentId || !color.applied || !multiply.applied || color.created == multiply.created || !document.Dirty())
    {
        return false;
    }
    const LX::Id output = document.Graph().FindNode(color.created)->pins.front().id;
    const LX::Node* multiplyNode = document.Graph().FindNode(multiply.created);
    const LX::Id input = multiplyNode->pins.front().id;
    const LX::Id factor = multiplyNode->pins[2].id;
    const std::uint64_t before = document.Revision();
    const auto conflict = document.Execute(LX::LXConnectPins{output, input}, before - 1);
    const auto connected = document.Execute(LX::LXConnectPins{output, input}, before);
    const auto rejected = document.Execute(LX::LXConnectPins{output, factor}, connected.revision);
    const auto value = document.Execute(LX::LXSetSocketValue{factor, 0.75}, connected.revision);
    const auto unchanged = document.Execute(LX::LXSetSocketValue{factor, 0.75}, value.revision);
    if (conflict.code != "revision_conflict" || document.Revision() != before + 2 || !connected.applied ||
        !connected.created || rejected.applied || rejected.code != "rejected_or_unchanged" ||
        rejected.message != "Pin types differ" || !value.applied || unchanged.applied)
    {
        std::cerr << "Document command revision or rejection failed" << std::endl;
        return false;
    }
    const auto saved = document.Execute(LX::LXSave{}, document.Revision());
    const auto changed = document.Execute(LX::LXSetSocketValue{factor, 0.5}, document.Revision());
    const auto undone = document.Execute(LX::LXUndo{}, document.Revision());
    const bool restoredClean =
        undone.applied && !document.Dirty() && document.Graph().FindPin(factor)->value == LX::LXSocketValue{0.75};
    const auto redone = document.Execute(LX::LXRedo{}, document.Revision());
    const bool redoDirty =
        redone.applied && document.Dirty() && document.Graph().FindPin(factor)->value == LX::LXSocketValue{0.5};
    const auto validated = document.Execute(LX::LXValidate{}, document.Revision());
    const auto savedAgain = document.Execute(LX::LXSave{}, document.Revision());
    std::string error;
    auto reopened = LX::LXDocument::Open(file.string(), &error, ExampleRegistry());
    const bool roundTrip = saved.applied && changed.applied && restoredClean && redoDirty && validated.issues.empty() &&
                           savedAgain.applied && !document.Dirty() && reopened &&
                           reopened->DocumentId() != documentId && reopened->Revision() == 0 && !reopened->Dirty() &&
                           document.Graph().Equals(reopened->Graph());
    bool previewAndReload = false;
    if (roundTrip)
    {
        const LX::NodeLayout* layout = document.Graph().FindLayout(color.created);
        const std::vector<LX::NodePosition> start{{color.created, layout->x, layout->y}};
        const std::vector<LX::NodePosition> final{{color.created, layout->x + 40.0f, layout->y + 25.0f}};
        const std::uint64_t beforePreview = document.Revision();
        const bool previewed = document.PreviewNodePositions(final);
        const auto blocked = document.Execute(LX::LXValidate{}, beforePreview);
        const auto finished = document.FinishNodePositionPreview(start, final);
        const std::uint64_t beforeReload = document.Revision();
        previewAndReload = previewed && blocked.code == "preview_in_progress" && finished.applied &&
                           finished.revision == beforePreview + 1 && document.Dirty() && document.Reload(&error) &&
                           document.Revision() == beforeReload + 1 && !document.Dirty() &&
                           document.Graph().Equals(reopened->Graph());
    }
    bool commandCoverage = false;
    if (roundTrip)
    {
        const LX::GraphFragment fragment = reopened->Graph().CopyNodes({color.created, multiply.created});
        const auto pasted = reopened->Execute(LX::LXPasteNodes{fragment, 30.0f, 40.0f}, reopened->Revision());
        const auto removed = reopened->Execute(LX::LXRemoveNodes{pasted.createdNodes}, reopened->Revision());
        const auto restored = reopened->Execute(LX::LXUndo{}, reopened->Revision());
        const auto connectedNode =
            reopened->Execute(LX::LXAddConnectedNode{"MULTIPLY", 400.0f, 20.0f, output, 0}, reopened->Revision());
        const auto firstPin = reopened->Execute(LX::LXAddDynamicPin{multiply.created, "Extra"}, reopened->Revision());
        const auto secondPin = reopened->Execute(LX::LXAddDynamicPin{multiply.created, "Extra2"}, reopened->Revision());
        const auto reordered = reopened->Execute(LX::LXMoveDynamicPin{firstPin.created, 1}, reopened->Revision());
        const auto pinRemoved = reopened->Execute(LX::LXRemoveDynamicPin{secondPin.created}, reopened->Revision());
        const auto moved = reopened->Execute(
            LX::LXSetNodePositions{{{color.created, 45.0f, 65.0f}, {multiply.created, 250.0f, 80.0f}}},
            reopened->Revision());
        const auto collapsed = reopened->Execute(LX::LXSetNodeCollapsed{color.created, true}, reopened->Revision());
        commandCoverage = pasted.applied && pasted.createdNodes.size() == 2 && removed.applied && restored.applied &&
                          connectedNode.applied && connectedNode.created && firstPin.applied && secondPin.applied &&
                          reordered.applied && pinRemoved.applied && moved.applied && collapsed.applied &&
                          reopened->Graph().FindLayout(color.created)->collapsed &&
                          reopened->Graph().Validate().empty();
    }
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    if (!roundTrip || !previewAndReload || !commandCoverage)
    {
        std::cerr << "Document command save or undo failed: " << error << std::endl;
    }
    return roundTrip && previewAndReload && commandCoverage;
}

bool FrameLayoutTest()
{
    const std::string suffix = std::to_string(GetCurrentProcessId());
    const std::filesystem::path path = std::filesystem::temp_directory_path() / ("lattice-frame-" + suffix + ".lxg");
    LX::LXGraph graph("material", ExampleRegistry());
    const LX::Id first = graph.CreateNode("COLOR", 100.0f, 120.0f);
    const LX::Id second = graph.CreateNode("NORMAL", 320.0f, 140.0f);
    LX::LXDocument document(std::move(graph), path.string());
    const LX::LXCommandResult added =
        document.Execute(LX::LXAddFrame{"Colors", 70.0f, 70.0f, 520.0f, 250.0f, {first, second}}, document.Revision());
    const LX::Id frame = added.created;
    const std::uint64_t beforeInvalid = document.Revision();
    const bool invalid =
        !document.Execute(LX::LXAddFrame{"", 0, 0, 100, 100, {}}, document.Revision()).applied &&
        !document.Execute(LX::LXRenameFrame{frame, ""}, document.Revision()).applied &&
        !document.Execute(LX::LXSetNodeFrame{first, frame + 1000}, document.Revision()).applied &&
        !document.Execute(LX::LXSetView{{true, 0, 0, std::numeric_limits<float>::infinity()}}, document.Revision())
             .applied &&
        document.Revision() == beforeInvalid;
    const bool previewed = document.PreviewFramePosition(frame, 90.0f, 80.0f) && document.Revision() == beforeInvalid &&
                           document.Graph().FindLayout(first)->x == 120.0f &&
                           document.Graph().FindLayout(second)->y == 150.0f;
    const LX::LXCommandResult moved = document.FinishFramePositionPreview(frame, 70.0f, 70.0f, 90.0f, 80.0f);
    const bool moveCommitted =
        moved.applied && moved.revision == beforeInvalid + 1 && document.Graph().FindLayout(first)->x == 120.0f;
    const bool resized = document.Execute(LX::LXResizeFrame{frame, 560.0f, 300.0f}, document.Revision()).applied;
    const bool renamed = document.Execute(LX::LXRenameFrame{frame, "Colors and normals"}, document.Revision()).applied;
    const bool detached = document.Execute(LX::LXSetNodeFrame{second, 0}, document.Revision()).applied &&
                          document.Graph().FindLayout(second)->frame == 0;
    const bool attached = document.Execute(LX::LXSetNodeFrame{second, frame}, document.Revision()).applied;
    const bool viewSet = document.Execute(LX::LXSetView{{true, 375.5f, -42.25f, 1.25f}}, document.Revision()).applied;
    const bool undo = document.Execute(LX::LXUndo{}, document.Revision()).applied &&
                      document.Graph().FindLayout(second)->frame == 0 &&
                      document.Graph().Layout().view.centerX == 375.5f;
    const bool redo = document.Execute(LX::LXRedo{}, document.Revision()).applied &&
                      document.Graph().FindLayout(second)->frame == frame &&
                      document.Graph().Layout().view.centerX == 375.5f;
    const bool saved = document.Execute(LX::LXSave{}, document.Revision()).applied && !document.Dirty();
    std::string error;
    auto reopened = LX::LXDocument::Open(path.string(), &error, ExampleRegistry());
    const bool roundTrip = reopened && document.Graph().Equals(reopened->Graph()) &&
                           reopened->Graph().Validate().empty() && reopened->Graph().FindFrame(frame) &&
                           reopened->Graph().FindLayout(first)->frame == frame &&
                           reopened->Graph().Layout().view.zoom == 1.25f &&
                           reopened->Graph().FindFrame(frame)->label == "Colors and normals";
    const std::filesystem::path invalidPath =
        std::filesystem::temp_directory_path() / ("lattice-frame-invalid-" + suffix + ".lxg");
    {
        std::ifstream source(path, std::ios::binary);
        std::ofstream invalidFile(invalidPath, std::ios::binary | std::ios::trunc);
        std::string line;
        bool replaceMember = false;
        while (std::getline(source, line))
        {
            if (replaceMember)
            {
                line.replace(line.rfind(' ') + 1, std::string::npos, "999999");
                replaceMember = false;
            }
            if (line.starts_with("P "))
            {
                replaceMember = true;
            }
            invalidFile << line << '\n';
        }
    }
    const bool invalidMembership = !LX::LXGraph::Load(invalidPath.string(), &error, ExampleRegistry());
    const LX::GraphFragment fragment = document.Graph().CopyNodes({first});
    const auto pasted = document.Execute(LX::LXPasteNodes{fragment, 30.0f, 0.0f}, document.Revision());
    const auto& sameGraph = pasted.createdNodes;
    LX::LXGraph other;
    bool collisionPrepared = true;
    for (LX::Id id = 1; id < frame; ++id)
    {
        collisionPrepared &= other.AddNode({"DUMMY", "Dummy", {}}, 0.0f, 0.0f) == id;
    }
    const LX::Id collision = other.AddFrame("Unrelated", -100.0f, -100.0f, 100.0f, 100.0f);
    const auto otherGraph = other.PasteNodes(fragment, 30.0f, 0.0f);
    const bool membership = sameGraph.size() == 1 && document.Graph().FindLayout(sameGraph.front())->frame == frame &&
                            collisionPrepared && collision == frame && otherGraph.size() == 1 &&
                            other.FindLayout(otherGraph.front())->frame == 0;
    const bool removed = document.Execute(LX::LXRemoveFrame{frame}, document.Revision()).applied &&
                         document.Graph().FindLayout(first)->frame == 0 &&
                         document.Graph().FindLayout(second)->frame == 0;
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
    std::filesystem::remove(invalidPath);
    const bool passed = added.applied && frame && invalid && previewed && moveCommitted && resized && renamed &&
                        detached && attached && viewSet && undo && redo && saved && roundTrip && invalidMembership &&
                        membership && removed;
    if (!passed)
    {
        std::cerr << "Frame layout save, preview, command, or membership failed: " << error << std::endl;
    }
    return passed;
}

bool GroupContractTest()
{
    const std::string suffix = std::to_string(GetCurrentProcessId());
    const std::filesystem::path path = std::filesystem::temp_directory_path() / ("lattice-group-" + suffix + ".lxg");
    const auto registry = ExampleRegistry();
    LX::LXGraph body("material", registry);
    const LX::Id operation = body.CreateNode("MULTIPLY", 100.0f, 80.0f);
    if (!operation)
    {
        return false;
    }
    const LX::Node* internal = body.FindNode(operation);
    const std::vector<LX::LXGroupSocket> sockets = {
        {0, "a", "A", LX::Direction::Input, LX::PinType::Color, internal->pins[0].id, {}},
        {0, "b", "B", LX::Direction::Input, LX::PinType::Color, internal->pins[1].id, {}},
        {0, "result", "Result", LX::Direction::Output, LX::PinType::Color, internal->pins[3].id, {}}};
    LX::LXGraph definitionOnly("material", registry);
    const LX::Id definitionOnlyGroup = definitionOnly.CreateGroup("Only", body, sockets);
    LX::LXDocument definitionOnlyDocument(std::move(definitionOnly));
    const bool definitionOnlyDirty = definitionOnlyGroup && definitionOnlyDocument.Dirty();
    LX::LXGraph root("material", registry);
    const LX::Id color = root.CreateNode("COLOR", 10.0f, 10.0f);
    const LX::Id surface = root.CreateNode("OUTPUT", 600.0f, 100.0f);
    LX::LXGraph nestedBody(body);
    const LX::Id inner = nestedBody.CreateGroup("Inner", body, sockets);
    const LX::Id nestedInstance = inner ? nestedBody.CreateGroupInstance(inner, 300.0f, 100.0f) : 0;
    const LX::Id outer = nestedInstance ? root.CreateGroup("Outer", nestedBody, sockets) : 0;
    const bool nestedSupported =
        inner && nestedInstance && outer && root.FindGroup(outer)->body->Groups().size() == 1 &&
        root.FindGroup(outer)->body->FindNode(nestedInstance)->groupId == inner && root.Validate().empty();
    LX::LXDocument document(std::move(root), path.string());
    const std::uint64_t initialRevision = document.Revision();
    const bool badInterface =
        !document.Execute(LX::LXCreateGroup{"No output", body, {sockets.front()}}, document.Revision()).applied &&
        !document.Execute(LX::LXCreateGroupInstance{999999, 0.0f, 0.0f}, document.Revision()).applied &&
        document.Revision() == initialRevision;
    const LX::LXCommandResult created =
        document.Execute(LX::LXCreateGroup{"Color group", body, sockets}, document.Revision());
    const LX::Id group = created.created;
    const LX::LXGroupDefinition* definition = document.Graph().FindGroup(group);
    const bool stableInterface = created.applied && definition && definition->sockets.size() == 3 &&
                                 definition->sockets[0].id != definition->sockets[1].id &&
                                 definition->sockets[0].internalPin == internal->pins[0].id;
    body.SetNodePosition(operation, 300.0f, 300.0f);
    const bool ownedBody = definition && definition->body->FindLayout(operation)->x == 100.0f;
    const LX::LXCommandResult instance =
        document.Execute(LX::LXCreateGroupInstance{group, 300.0f, 100.0f}, document.Revision());
    const LX::Node* node = document.Graph().FindNode(instance.created);
    const bool pins = instance.applied && node && node->groupId == group && node->pins.size() == 3 &&
                      node->pins[0].interfaceId == definition->sockets[0].id &&
                      node->pins[2].interfaceId == definition->sockets[2].id;
    const LX::Id inputPin = node ? node->pins[0].id : 0;
    const LX::Id outputPin = node ? node->pins[2].id : 0;
    const LX::Id colorPin = document.Graph().FindNode(color)->pins[0].id;
    const LX::Id surfacePin = document.Graph().FindNode(surface)->pins[0].id;
    const bool connected = document.Execute(LX::LXConnectPins{colorPin, inputPin}, document.Revision()).applied &&
                           document.Execute(LX::LXConnectPins{outputPin, surfacePin}, document.Revision()).applied &&
                           document.Graph().Validate().empty();
    const bool saved = document.Execute(LX::LXSave{}, document.Revision()).applied && !document.Dirty();
    std::string error;
    auto reopened = LX::LXDocument::Open(path.string(), &error, registry);
    const bool roundTrip =
        reopened && reopened->Graph().Equals(document.Graph()) && reopened->Graph().FindGroup(group) &&
        reopened->Graph().FindGroup(group)->body->FindNode(operation) &&
        reopened->Graph().FindNode(instance.created)->pins[0].interfaceId == definition->sockets[0].id;
    bool corruptRejected = false;
    const std::filesystem::path invalidPath =
        std::filesystem::temp_directory_path() / ("lattice-group-invalid-" + suffix + ".lxg");
    if (definition)
    {
        std::ifstream source(path, std::ios::binary);
        std::string contents{std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
        const std::string needle = "\n" + std::to_string(definition->sockets[0].id) + " \"a\"";
        const std::size_t position = contents.find(needle);
        if (position != std::string::npos)
        {
            contents.replace(position + 1, std::to_string(definition->sockets[0].id).size(), std::to_string(group));
            std::ofstream invalidFile(invalidPath, std::ios::binary | std::ios::trunc);
            invalidFile.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            invalidFile.close();
            corruptRejected = !LX::LXGraph::Load(invalidPath.string(), &error, registry);
        }
    }
    const bool undo = document.Execute(LX::LXUndo{}, document.Revision()).applied && document.Dirty() &&
                      document.Graph().Links().size() == 1;
    const bool redo = document.Execute(LX::LXRedo{}, document.Revision()).applied && !document.Dirty() &&
                      document.Graph().Links().size() == 2;
    const LX::GraphFragment fragment = document.Graph().CopyNodes({instance.created});
    const auto sameDocument = document.Execute(LX::LXPasteNodes{fragment, 40.0f, 20.0f}, document.Revision());
    LX::LXGraph unrelated("material", registry);
    const bool foreignRejected = unrelated.PasteNodes(fragment, 40.0f, 20.0f).empty();
    const bool copied = sameDocument.applied && sameDocument.createdNodes.size() == 1 &&
                        document.Graph().FindNode(sameDocument.createdNodes.front())->groupId == group;
    const std::filesystem::path legacyPath =
        std::filesystem::temp_directory_path() / ("lattice-group-legacy-" + suffix + ".lxg");
    {
        std::ofstream legacyFile(legacyPath, std::ios::binary | std::ios::trunc);
        legacyFile << "LXG 7 \"material\" 1\nN 0\nP 0\nF 0\nV 0 0 0 1\nL 0\n";
    }
    auto legacy = LX::LXGraph::Load(legacyPath.string(), &error, registry);
    const bool legacySaved = legacy && legacy->Save(legacyPath.string(), &error);
    std::ifstream migratedFile(legacyPath, std::ios::binary);
    std::string migratedHeader;
    std::getline(migratedFile, migratedHeader);
    migratedFile.close();
    auto migrated = LX::LXGraph::Load(legacyPath.string(), &error, registry);
    const bool legacyMigrated =
        legacySaved && migratedHeader == "LXG 9 \"material\" 1" && migrated && legacy->Equals(*migrated);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
    std::filesystem::remove(invalidPath);
    std::filesystem::remove(legacyPath);
    std::filesystem::remove(legacyPath.string() + ".bak");
    const bool passed = definitionOnlyDirty && badInterface && stableInterface && ownedBody && pins && connected &&
                        saved && roundTrip && corruptRejected && undo && redo && copied && foreignRejected &&
                        nestedSupported && legacyMigrated;
    if (!passed)
    {
        std::cerr << "Group definition, instance, or round-trip failed: " << error << " nested=" << nestedSupported
                  << " stable=" << stableInterface << " connected=" << connected << " saved=" << saved
                  << " roundTrip=" << roundTrip << " legacy=" << legacyMigrated << " copied=" << copied << std::endl;
    }
    return passed;
}

bool GroupBoundaryTest()
{
    const auto registry = ExampleRegistry();
    LX::LXGraph body("material", registry);
    const LX::Id groupInput = body.AddGroupBoundaryNode(LX::Direction::Input, 40.0f, 120.0f);
    const LX::Id groupOutput = body.AddGroupBoundaryNode(LX::Direction::Output, 490.0f, 120.0f);
    const LX::Id multiply = body.CreateNode("MULTIPLY", 260.0f, 120.0f);
    const LX::Id colorInput = body.AddGroupBoundaryPin(groupInput, "color", "Color", LX::PinType::Color);
    const LX::Id factorInput = body.AddGroupBoundaryPin(groupInput, "factor", "Factor", LX::PinType::Float, 0.25);
    const LX::Id colorOutput = body.AddGroupBoundaryPin(groupOutput, "result", "Result", LX::PinType::Color);
    if (!groupInput || !groupOutput || !multiply || !colorInput || !factorInput || !colorOutput ||
        body.AddGroupBoundaryNode(LX::Direction::Input, 0.0f, 0.0f) ||
        !body.Connect(colorInput, body.FindNode(multiply)->pins[0].id) ||
        !body.Connect(factorInput, body.FindNode(multiply)->pins[2].id) ||
        !body.Connect(body.FindNode(multiply)->pins[3].id, colorOutput) || !body.Validate().empty())
    {
        std::cerr << "Group boundary nodes or internal links failed" << std::endl;
        return false;
    }

    const std::vector<LX::LXGroupSocket> sockets = {
        {0, "color", "Color", LX::Direction::Input, LX::PinType::Color, colorInput, {}},
        {0, "factor", "Factor", LX::Direction::Input, LX::PinType::Float, factorInput, 0.25},
        {0, "result", "Result", LX::Direction::Output, LX::PinType::Color, colorOutput, {}}};
    LX::LXGraph root("material", registry);
    const LX::Id source = root.CreateNode("COLOR", 20.0f, 120.0f);
    const LX::Id sink = root.CreateNode("OUTPUT", 710.0f, 120.0f);
    const LX::Id badGroup = root.CreateGroup(
        "Wrong direction", body,
        {{0, "bad", "Bad", LX::Direction::Input, LX::PinType::Color, colorOutput, {}}, sockets.back()});
    const LX::Id group = root.CreateGroup("Boundary group", body, sockets);
    const LX::Id instance = root.CreateGroupInstance(group, 390.0f, 120.0f);
    if (!source || !sink || badGroup || !group || !instance)
    {
        std::cerr << "Group boundary definition failed" << std::endl;
        return false;
    }
    const LX::Id outerInput = root.FindNode(instance)->pins[0].id;
    const LX::Id outerFactor = root.FindNode(instance)->pins[1].id;
    const LX::Id outerOutput = root.FindNode(instance)->pins[2].id;
    const bool connected = root.Connect(root.FindNode(source)->pins[0].id, outerInput) &&
                           root.Connect(outerOutput, root.FindNode(sink)->pins[0].id) && root.Validate().empty();
    std::vector<LX::LXGroupSocket> edited = root.FindGroup(group)->sockets;
    edited[1].value = 0.75;
    const bool defaultChanged = root.UpdateGroup(group, "Boundary group", *root.FindGroup(group)->body, edited) &&
                                root.FindPin(outerFactor)->value == LX::LXSocketValue{0.75} &&
                                root.FindGroup(group)->body->FindPin(factorInput)->value == LX::LXSocketValue{0.75} &&
                                root.FindNode(instance)->pins[0].id == outerInput &&
                                root.FindNode(instance)->pins[2].id == outerOutput;
    const bool overridden = root.SetSocketValue(outerFactor, 0.6);
    edited = root.FindGroup(group)->sockets;
    edited[1].value = 0.9;
    const bool overridePreserved = root.UpdateGroup(group, "Boundary group", *root.FindGroup(group)->body, edited) &&
                                   root.FindPin(outerFactor)->value == LX::LXSocketValue{0.6} &&
                                   root.FindGroup(group)->body->FindPin(factorInput)->value == LX::LXSocketValue{0.9};
    LX::LXGraph missingPin(*root.FindGroup(group)->body);
    missingPin.RemoveGroupBoundaryPin(factorInput);
    const bool rejected = !root.UpdateGroup(group, "Invalid", missingPin, root.FindGroup(group)->sockets) &&
                          root.FindGroup(group)->name == "Boundary group";
    const bool undo = root.Undo() && root.FindGroup(group)->sockets[1].value == LX::LXSocketValue{0.75};
    const bool redo = root.Redo() && root.FindGroup(group)->sockets[1].value == LX::LXSocketValue{0.9};
    edited = root.FindGroup(group)->sockets;
    std::swap(edited[0], edited[1]);
    const bool reordered = root.UpdateGroup(group, "Boundary group", *root.FindGroup(group)->body, edited) &&
                           root.FindGroup(group)->body->FindNode(groupInput)->pins[0].id == factorInput &&
                           root.FindNode(instance)->pins[0].id == outerFactor &&
                           root.FindNode(instance)->pins[1].id == outerInput && root.Validate().empty();
    LX::LXDocument removalDraft(*root.FindGroup(group)->body);
    const bool pinRemoved =
        removalDraft.Execute(LX::LXRemoveGroupBoundaryPin{factorInput}, removalDraft.Revision()).applied;
    LX::LXGraph reduced(root);
    std::vector<LX::LXGroupSocket> reducedSockets = reduced.FindGroup(group)->sockets;
    reducedSockets.erase(reducedSockets.begin());
    const bool socketRemoved =
        pinRemoved && reduced.UpdateGroup(group, "Boundary group", removalDraft.Graph(), reducedSockets) &&
        reduced.FindNode(instance)->pins.size() == 2 && reduced.Links().size() == 2 && reduced.Validate().empty();
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("lattice-group-boundary-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    std::string error;
    const bool saved = root.Save(path.string(), &error);
    const auto reopened = saved ? LX::LXGraph::Load(path.string(), &error, registry) : std::nullopt;
    const bool roundTrip = reopened && reopened->Equals(root) && reopened->FindGroup(group)->body->Links().size() == 3;
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
    const bool passed = connected && defaultChanged && overridden && overridePreserved && rejected && undo && redo &&
                        reordered && socketRemoved && roundTrip;
    if (!passed)
    {
        std::cerr << "Group boundary default, edit, or round-trip failed: " << error << std::endl;
    }
    return passed;
}

bool GroupCollapseTest()
{
    LX::LXGraph initial = Fixture();
    const LX::Id sample = initial.Nodes()[1].id;
    const LX::Id multiply = initial.Nodes()[3].id;
    const LX::Id output = initial.Nodes()[6].id;
    const LX::LXSocketValue factorDefault = initial.FindNode(multiply)->pins[2].value;
    const LX::Id frame = initial.AddFrame("Selected operations", 240.0f, 50.0f, 480.0f, 440.0f, {sample, multiply});
    const LX::Id inboundTexture = initial.Links()[0].id;
    const LX::Id internalLink = initial.Links()[1].id;
    const LX::Id inboundColor = initial.Links()[2].id;
    const LX::Id outboundColor = initial.Links()[3].id;
    const LX::LXGraph original(initial);
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("lattice-group-collapse-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    LX::LXDocument document(std::move(initial), path.string());
    const std::uint64_t revision = document.Revision();
    const bool invalidRejected =
        frame && !document.Execute(LX::LXCollapseToGroup{{output}, "Invalid"}, revision).applied &&
        !document.Execute(LX::LXCollapseToGroup{{sample, sample}, "Duplicate"}, revision).applied &&
        document.Revision() == revision;
    const LX::LXCommandResult result =
        document.Execute(LX::LXCollapseToGroup{{sample, multiply}, "Texture tint"}, document.Revision());
    const LX::Id instance = result.created;
    if (!result.applied || !instance)
    {
        std::cerr << "Selected Material nodes did not collapse into a group" << std::endl;
        return false;
    }
    const LX::LXGraph& grouped = document.Graph();
    const LX::Node* groupedNode = grouped.FindNode(instance);
    const LX::LXGroupDefinition* definition = groupedNode ? grouped.FindGroup(groupedNode->groupId) : nullptr;
    const LX::NodeLayout* groupedLayout = grouped.FindLayout(instance);
    const LX::LXGroupSocket* factorSocket = nullptr;
    if (definition)
    {
        const auto found = std::find_if(definition->sockets.begin(), definition->sockets.end(),
                                        [](const LX::LXGroupSocket& socket) { return socket.name == "Factor"; });
        factorSocket = found == definition->sockets.end() ? nullptr : &*found;
    }
    const auto findLink = [&](LX::Id id) -> const LX::Link* {
        const auto found = std::find_if(grouped.Links().begin(), grouped.Links().end(),
                                        [id](const LX::Link& link) { return link.id == id; });
        return found == grouped.Links().end() ? nullptr : &*found;
    };
    const LX::Link* textureLink = findLink(inboundTexture);
    const LX::Link* colorLink = findLink(inboundColor);
    const LX::Link* resultLink = findLink(outboundColor);
    const bool structure =
        definition && groupedNode && groupedLayout && factorSocket && factorSocket->value == factorDefault &&
        definition->body->FindPin(factorSocket->internalPin) &&
        definition->body->FindPin(factorSocket->internalPin)->value == factorDefault &&
        definition->sockets.size() == 4 && groupedNode->pins.size() == 4 && definition->body->Nodes().size() == 4 &&
        definition->body->Links().size() == 5 && definition->body->Layout().frames.size() == 1 &&
        groupedLayout->frame == frame && !grouped.FindNode(sample) && !grouped.FindNode(multiply) &&
        grouped.Links().size() == 5 && !findLink(internalLink) && textureLink && colorLink && resultLink &&
        grouped.FindPin(textureLink->input) && grouped.FindPin(colorLink->input) &&
        grouped.FindPin(resultLink->output) && grouped.FindPin(textureLink->input)->node == instance &&
        grouped.FindPin(colorLink->input)->node == instance && grouped.FindPin(resultLink->output)->node == instance &&
        grouped.Validate().empty();
    const LX::LXGraph collapsed(grouped);
    const bool undone = document.Execute(LX::LXUndo{}, document.Revision()).applied &&
                        document.Graph().Equals(original) && document.Graph().FindNode(sample) &&
                        document.Graph().FindNode(multiply);
    const bool redone =
        document.Execute(LX::LXRedo{}, document.Revision()).applied && document.Graph().Equals(collapsed);
    const bool saved = document.Execute(LX::LXSave{}, document.Revision()).applied;
    std::string error;
    const auto reopened = saved ? LX::LXDocument::Open(path.string(), &error, ExampleRegistry()) : std::nullopt;
    const bool roundTrip = reopened && reopened->Graph().Equals(document.Graph());
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");

    LX::LXGraph fanout("material", ExampleRegistry());
    const LX::Id source = fanout.CreateNode("COLOR", 20.0f, 100.0f);
    const LX::Id target = fanout.CreateNode("MULTIPLY", 250.0f, 100.0f);
    const LX::Id sourcePin = fanout.FindNode(source)->pins[0].id;
    const bool fanoutLinks = fanout.Connect(sourcePin, fanout.FindNode(target)->pins[0].id) &&
                             fanout.Connect(sourcePin, fanout.FindNode(target)->pins[1].id);
    const LX::Id fanoutInstance = fanout.CollapseToGroup({target}, "Fanout");
    const LX::LXGroupDefinition* fanoutDefinition =
        fanoutInstance ? fanout.FindGroup(fanout.FindNode(fanoutInstance)->groupId) : nullptr;
    const bool fanoutPreserved = fanoutLinks && fanoutDefinition && fanout.Links().size() == 1 &&
                                 fanoutDefinition->body->Links().size() == 4 && fanout.Validate().empty();
    const bool passed = invalidRejected && structure && undone && redone && roundTrip && fanoutPreserved;
    if (!passed)
    {
        std::cerr << "Group collapse, links, Undo, or round-trip failed: " << error << std::endl;
    }
    return passed;
}

bool GroupNestingTest()
{
    const auto registry = ExampleRegistry();
    LX::LXGraph initial = Fixture();
    const LX::Id childInstance = AddExampleGroup(initial, 720.0f, 570.0f);
    const LX::Node* childNode = initial.FindNode(childInstance);
    if (!childNode || initial.Links().size() != 7)
    {
        return false;
    }
    const LX::Id childGroup = childNode->groupId;
    const LX::Id surfaceInput = initial.Nodes()[6].pins[0].id;
    const bool outputConnected =
        initial.Disconnect(initial.Links()[3].id) && initial.Connect(childNode->pins[2].id, surfaceInput).has_value();
    if (!outputConnected)
    {
        return false;
    }
    const LX::Id outerLink = initial.Links().back().id;
    const LX::LXGraph original(initial);
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("lattice-nested-groups-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    LX::LXDocument document(std::move(initial), path.string());
    const LX::LXCommandResult created =
        document.Execute(LX::LXCollapseToGroup{{childInstance}, "Outer"}, document.Revision());
    const LX::Node* outerInstance = document.Graph().FindNode(created.created);
    const LX::LXGroupDefinition* outer = outerInstance ? document.Graph().FindGroup(outerInstance->groupId) : nullptr;
    const LX::LXGroupDefinition* importedChild =
        outer && outer->body->Groups().size() == 1 ? &outer->body->Groups().begin()->second : nullptr;
    const LX::Node* nestedInstance = nullptr;
    if (outer)
    {
        const auto found = std::find_if(outer->body->Nodes().begin(), outer->body->Nodes().end(),
                                        [](const LX::Node& node) { return node.groupId != 0; });
        nestedInstance = found == outer->body->Nodes().end() ? nullptr : &*found;
    }
    const LX::Id nestedNodeId = nestedInstance ? nestedInstance->id : 0;
    const LX::Id outerGroup = outer ? outer->id : 0;
    const auto preservedOuterLink = std::find_if(document.Graph().Links().begin(), document.Graph().Links().end(),
                                                 [outerLink](const LX::Link& link) { return link.id == outerLink; });
    const bool nested = created.applied && outer && importedChild && nestedInstance &&
                        nestedInstance->groupId == importedChild->id && importedChild->id == childGroup &&
                        nestedInstance->pins.front().interfaceId == importedChild->sockets.front().id &&
                        preservedOuterLink != document.Graph().Links().end() &&
                        document.Graph().FindPin(preservedOuterLink->output)->node == created.created &&
                        document.Graph().Validate().empty();
    const LX::LXGraph collapsed(document.Graph());
    const bool undone =
        document.Execute(LX::LXUndo{}, document.Revision()).applied && document.Graph().Equals(original);
    const bool redone =
        document.Execute(LX::LXRedo{}, document.Revision()).applied && document.Graph().Equals(collapsed);
    std::string error;
    const bool saved = document.Execute(LX::LXSave{}, document.Revision()).applied;
    const auto opened = saved ? LX::LXDocument::Open(path.string(), &error, registry) : std::nullopt;
    const bool roundTrip = opened && opened->Graph().Equals(collapsed) && opened->Graph().Validate().empty();

    const LX::LXGroupDefinition* originalChild = document.Graph().FindGroup(childGroup);
    LX::LXGraph editedBody(*originalChild->body);
    const LX::Id operation = editedBody.Nodes().front().id;
    const bool moved = editedBody.SetNodePosition(operation, 140.0f, 90.0f);
    std::vector<LX::LXGroupSocket> editedSockets = originalChild->sockets;
    editedSockets.back().name = "Product";
    editedSockets.push_back({0, "factor", "Factor", LX::Direction::Input, LX::PinType::Float,
                             editedBody.FindNode(operation)->pins[2].id, 0.5});
    const std::uint64_t beforeUpdate = document.Revision();
    const bool updated =
        moved &&
        document.Execute(LX::LXUpdateGroup{childGroup, "Shared color", editedBody, editedSockets}, beforeUpdate)
            .applied &&
        document.Revision() == beforeUpdate + 1;
    const LX::LXGroupDefinition* updatedChild = document.Graph().FindGroup(childGroup);
    const LX::LXGroupDefinition* propagatedChild = document.Graph().FindGroup(outerGroup)->body->FindGroup(childGroup);
    const LX::Node* propagatedNode = document.Graph().FindGroup(outerGroup)->body->FindNode(nestedNodeId);
    const bool propagated =
        updated && updatedChild && propagatedChild && propagatedNode && propagatedChild->name == updatedChild->name &&
        propagatedChild->sockets == updatedChild->sockets && propagatedChild->body->Equals(*updatedChild->body) &&
        propagatedNode->title == "Shared color" && propagatedNode->pins.size() == 4 &&
        propagatedNode->pins[2].name == "Product" && propagatedNode->pins.back().name == "Factor" &&
        document.Graph().Validate().empty();
    const LX::LXGraph synchronized(document.Graph());
    const bool updateUndone =
        document.Execute(LX::LXUndo{}, document.Revision()).applied && document.Graph().Equals(collapsed);
    const bool updateRedone =
        document.Execute(LX::LXRedo{}, document.Revision()).applied && document.Graph().Equals(synchronized);
    const bool updatedSaved = document.Execute(LX::LXSave{}, document.Revision()).applied;
    const auto updatedOpened = updatedSaved ? LX::LXDocument::Open(path.string(), &error, registry) : std::nullopt;
    const bool updatedRoundTrip = updatedOpened && updatedOpened->Graph().Equals(synchronized);

    LX::LXGraph stale(synchronized);
    LX::LXGraph staleOuterBody(*stale.FindGroup(outerGroup)->body);
    auto& staleAliases = const_cast<std::map<LX::Id, LX::LXGroupDefinition>&>(staleOuterBody.Groups());
    staleAliases.at(childGroup).name = "Outdated copy";
    auto& staleGroups = const_cast<std::map<LX::Id, LX::LXGroupDefinition>&>(stale.Groups());
    staleGroups.at(outerGroup).body = std::make_shared<const LX::LXGraph>(std::move(staleOuterBody));
    const auto staleIssues = stale.Validate();
    const bool staleRejected =
        std::any_of(staleIssues.begin(), staleIssues.end(),
                    [](const LX::Issue& issue) { return issue.code == "group_shared_definition"; }) &&
        !stale.Save(path.string() + ".stale", &error);

    const LX::LXGroupDefinition* currentChild = document.Graph().FindGroup(childGroup);
    std::vector<LX::LXGroupSocket> removedSockets = currentChild->sockets;
    removedSockets.erase(removedSockets.begin());
    const std::uint64_t beforeRemoval = document.Revision();
    const bool linkedSocketRemovalRejected =
        !document
             .Execute(LX::LXUpdateGroup{childGroup, currentChild->name, *currentChild->body, removedSockets},
                      beforeRemoval)
             .applied &&
        document.Revision() == beforeRemoval && document.Graph().Equals(synchronized);

    LX::LXGraph cycleBody(document.Graph());
    const LX::Id cycleOperation = cycleBody.CreateNode("MULTIPLY", 110.0f, 110.0f);
    std::vector<LX::LXGroupSocket> cycleSockets = document.Graph().FindGroup(childGroup)->sockets;
    if (cycleOperation)
    {
        const LX::Node* cycleNode = cycleBody.FindNode(cycleOperation);
        cycleSockets[0].internalPin = cycleNode->pins[0].id;
        cycleSockets[1].internalPin = cycleNode->pins[1].id;
        cycleSockets[2].internalPin = cycleNode->pins[3].id;
        if (cycleSockets.size() > 3)
        {
            cycleSockets[3].internalPin = cycleNode->pins[2].id;
        }
    }
    const std::uint64_t beforeCycle = document.Revision();
    const bool referenceCycleRejected =
        cycleOperation &&
        !document.Execute(LX::LXUpdateGroup{childGroup, "Cycle", cycleBody, cycleSockets}, beforeCycle).applied &&
        document.Revision() == beforeCycle && document.Graph().Equals(synchronized);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
    std::filesystem::remove(path.string() + ".stale");

    LX::LXGraph current("material", registry);
    const LX::Id first = current.CreateNode("MULTIPLY", 80.0f, 80.0f);
    bool depthAccepted = first != 0;
    bool depthRejected = false;
    for (unsigned level = 1; depthAccepted && level <= 17; ++level)
    {
        LX::LXGraph parent("material", registry);
        const LX::Id parentOperation = parent.CreateNode("MULTIPLY", 80.0f, 80.0f);
        const LX::Node* internal = current.Nodes().empty() ? nullptr : &current.Nodes().front();
        if (!parentOperation || !internal || internal->pins.size() < 4)
        {
            depthAccepted = false;
            break;
        }
        const std::vector<LX::LXGroupSocket> sockets = {
            {0, "a", "A", LX::Direction::Input, LX::PinType::Color, internal->pins[0].id, {}},
            {0, "result", "Result", LX::Direction::Output, LX::PinType::Color, internal->pins[3].id, {}}};
        const LX::Id group = parent.CreateGroup("Layer", current, sockets);
        if (level == 17)
        {
            depthRejected = !group && parent.Validate().empty();
            break;
        }
        depthAccepted = group && parent.CreateGroupInstance(group, 300.0f, 80.0f) && parent.Validate().empty();
        current = std::move(parent);
    }

    LX::LXGraph corrupt("material", registry);
    auto& corruptGroups = const_cast<std::map<LX::Id, LX::LXGroupDefinition>&>(corrupt.Groups());
    std::shared_ptr<const LX::LXGraph> self(&corrupt, [](const LX::LXGraph*) {});
    corruptGroups.emplace(1, LX::LXGroupDefinition{1, "Cycle", {}, self});
    const std::vector<LX::Issue> cycleIssues = corrupt.Validate();
    const bool cycleRejected = cycleIssues.size() == 1 && cycleIssues.front().code == "group_cycle";
    corruptGroups.clear();

    const bool passed = nested && undone && redone && saved && roundTrip && propagated && updateUndone &&
                        updateRedone && updatedSaved && updatedRoundTrip && staleRejected &&
                        linkedSocketRemovalRejected && referenceCycleRejected && depthAccepted && depthRejected &&
                        cycleRejected;
    if (!passed)
    {
        std::cerr << "Nested group, shared update, cycle, or round-trip failed: " << error << " nested=" << nested
                  << " updated=" << updated << " propagated=" << propagated << " stale=" << staleRejected
                  << " linkedRemoval=" << linkedSocketRemovalRejected << " referenceCycle=" << referenceCycleRejected
                  << " updatedRoundTrip=" << updatedRoundTrip << std::endl;
    }
    return passed;
}

bool GroupNestingUiTest()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440.0f, 840.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph graph = Fixture();
    const LX::Id child = AddExampleGroup(graph, 720.0f, 570.0f);
    const LX::Id outerInstance = child ? graph.CollapseToGroup({child}, "Outer") : 0;
    const LX::Id outerGroup = outerInstance ? graph.FindNode(outerInstance)->groupId : 0;
    LX::LXDocument parent(std::move(graph));
    GroupEditorState editor;
    const bool opened = outerGroup && editor.Open(parent, outerGroup);
    LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    GroupEditorProbe probe;
    gGroupEditorProbe = &probe;
    const auto frame = [&]() {
        ImGui::NewFrame();
        DrawGroupEditor(editor, parent, styles, items);
        ImGui::Render();
    };
    const auto click = [&](ImVec2 point) {
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };

    bool listed = false;
    bool inserted = false;
    bool regrouped = false;
    bool applied = false;
    bool undone = false;
    if (opened)
    {
        io.AddMousePosEvent(0.0f, 0.0f);
        frame();
        listed = probe.nestedGroupPoints.size() == 1;
    }
    if (listed)
    {
        click(probe.nestedGroupPoints.begin()->second);
        inserted = editor.draft->Graph().Nodes().size() == 4 &&
                   editor.draft->Graph().FindNode(editor.canvas.selectedNode) &&
                   editor.draft->Graph().FindNode(editor.canvas.selectedNode)->groupId &&
                   editor.draft->Graph().Validate().empty();
    }
    if (inserted)
    {
        click(probe.groupSelectionPoint);
        regrouped = editor.draft->Graph().Groups().size() == 2 &&
                    editor.draft->Graph().FindNode(editor.canvas.selectedNode) &&
                    editor.draft->Graph().FindNode(editor.canvas.selectedNode)->groupId &&
                    editor.draft->Graph().Validate().empty();
    }
    if (regrouped)
    {
        click(probe.applyPoint);
        applied = !editor.draft && parent.Graph().FindGroup(outerGroup)->body->Nodes().size() == 4 &&
                  parent.Graph().FindGroup(outerGroup)->body->Groups().size() == 2 && parent.Graph().Validate().empty();
        undone = applied && parent.Execute(LX::LXUndo{}, parent.Revision()).applied &&
                 parent.Graph().FindGroup(outerGroup)->body->Nodes().size() == 3;
    }
    const bool passed = opened && listed && inserted && regrouped && applied && undone;
    if (!passed)
    {
        std::cerr << "Nested group editor UI failed opened=" << opened << " listed=" << listed
                  << " inserted=" << inserted << " regrouped=" << regrouped << " applied=" << applied
                  << " undone=" << undone << std::endl;
    }
    gGroupEditorProbe = nullptr;
    ImGui::DestroyContext();
    return passed;
}

bool GroupCollapseUiTest()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440.0f, 840.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph fixture = Fixture();
    const LX::Id sample = fixture.Nodes()[1].id;
    const LX::Id multiply = fixture.Nodes()[3].id;
    LX::LXDocument document(std::move(fixture));
    LX::LXGraph& graph = document.GraphForCanvas();
    LX::CanvasState canvas;
    canvas.selectedNodes = {sample, multiply};
    canvas.selectedNode = multiply;
    LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    GroupCollapseProbe probe;
    gGroupCollapseProbe = &probe;
    const auto frame = [&]() {
        ImGui::NewFrame();
        DrawUI(graph, canvas, styles, items, "LatticeGroupCollapseUiTest.lxg", nullptr, &document);
        ImGui::Render();
    };

    io.AddMousePosEvent(0.0f, 0.0f);
    frame();
    const bool visible = probe.buttonPoint.x > 0.0f && probe.buttonPoint.y > 0.0f;
    if (visible)
    {
        io.AddMousePosEvent(probe.buttonPoint.x, probe.buttonPoint.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    }
    const LX::Node* instance = graph.FindNode(canvas.selectedNode);
    const bool created = visible && instance && instance->groupId && graph.FindGroup(instance->groupId) &&
                         canvas.selectedNodes.empty() && canvas.dirty && document.Revision() == 1 &&
                         graph.Validate().empty();
    const bool undone = created && document.Execute(LX::LXUndo{}, document.Revision()).applied &&
                        graph.FindNode(sample) && graph.FindNode(multiply) && graph.Validate().empty();
    if (!created || !undone)
    {
        std::cerr << "Group collapse UI failed visible=" << visible << " created=" << created << " undone=" << undone
                  << std::endl;
    }
    gGroupCollapseProbe = nullptr;
    ImGui::DestroyContext();
    return created && undone;
}

bool GroupBodyEditTest()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("lattice-group-edit-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    LX::LXGraph initial = Fixture();
    const LX::Id firstInstance = AddExampleGroup(initial, 690.0f, 570.0f);
    if (!firstInstance)
    {
        return false;
    }
    const LX::Id group = initial.FindNode(firstInstance)->groupId;
    const LX::Id secondInstance = initial.CreateGroupInstance(group, 880.0f, 570.0f);
    LX::LXDocument document(std::move(initial), path.string());
    const bool initialSave = document.Execute(LX::LXSave{}, document.Revision()).applied;
    const LX::LXGroupDefinition* definition = document.Graph().FindGroup(group);
    const LX::Id internalNode = definition->body->Nodes().front().id;
    const float originalX = definition->body->FindLayout(internalNode)->x;
    const LX::Id interfaceId = definition->sockets.front().id;
    LX::LXGraph invalidBody(*definition->body);
    invalidBody.RemoveNode(internalNode);
    const std::uint64_t before = document.Revision();
    const bool invalidRejected = !document.Execute(LX::LXReplaceGroupBody{group, invalidBody}, before).applied &&
                                 document.Revision() == before && !document.Dirty();

    LX::LXDocument draft(*definition->body, {}, true);
    const bool draftChanged =
        draft.Execute(LX::LXSetNodePosition{internalNode, originalX + 45.0f, 80.0f}, draft.Revision()).applied;
    const bool staleRejected = !document.Execute(LX::LXReplaceGroupBody{group, draft.Graph()}, before + 1).applied &&
                               document.Revision() == before;
    const bool applied = document.Execute(LX::LXReplaceGroupBody{group, draft.Graph()}, before).applied &&
                         document.Revision() == before + 1 && document.Dirty() &&
                         document.Graph().FindGroup(group)->body->FindLayout(internalNode)->x == originalX + 45.0f;
    const LX::Node* first = document.Graph().FindNode(firstInstance);
    const LX::Node* second = document.Graph().FindNode(secondInstance);
    const bool stableInstances = first && second && first->groupId == group && second->groupId == group &&
                                 first->pins.front().interfaceId == interfaceId &&
                                 second->pins.front().interfaceId == interfaceId && document.Graph().Validate().empty();
    const bool undone = document.Execute(LX::LXUndo{}, document.Revision()).applied &&
                        document.Graph().FindGroup(group)->body->FindLayout(internalNode)->x == originalX &&
                        !document.Dirty();
    const bool redone = document.Execute(LX::LXRedo{}, document.Revision()).applied &&
                        document.Graph().FindGroup(group)->body->FindLayout(internalNode)->x == originalX + 45.0f;
    const bool saved = document.Execute(LX::LXSave{}, document.Revision()).applied;
    std::string error;
    auto reopened = LX::LXDocument::Open(path.string(), &error, ExampleRegistry());
    const bool roundTrip = reopened && reopened->Graph().Equals(document.Graph());
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
    const bool passed = initialSave && invalidRejected && draftChanged && staleRejected && applied && stableInstances &&
                        undone && redone && saved && roundTrip;
    if (!passed)
    {
        std::cerr << "Group body edit failed: " << error << std::endl;
    }
    return passed;
}

bool GroupInterfaceEditTest()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("lattice-group-interface-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    LX::LXGraph initial = Fixture();
    const LX::Id firstInstance = AddExampleGroup(initial, 690.0f, 570.0f);
    if (!firstInstance)
    {
        return false;
    }
    const LX::Id group = initial.FindNode(firstInstance)->groupId;
    const LX::Id secondInstance = initial.CreateGroupInstance(group, 880.0f, 570.0f);
    LX::LXDocument document(std::move(initial), path.string());
    const LX::LXGroupDefinition* original = document.Graph().FindGroup(group);
    const LX::Id aInterface = original->sockets[0].id;
    const LX::Id bInterface = original->sockets[1].id;
    const LX::Id aPin = document.Graph().FindNode(firstInstance)->pins[0].id;
    const LX::Id factorInternal = original->body->Nodes().front().pins[2].id;
    const bool noOp =
        !document
             .Execute(LX::LXUpdateGroup{group, original->name, *original->body, original->sockets}, document.Revision())
             .applied;
    std::vector<LX::LXGroupSocket> invalid = original->sockets;
    invalid.erase(invalid.begin());
    const std::uint64_t initialRevision = document.Revision();
    const bool connectedRemovalRejected =
        !document.Execute(LX::LXUpdateGroup{group, "Invalid", *original->body, invalid}, initialRevision).applied &&
        document.Revision() == initialRevision;
    std::vector<LX::LXGroupSocket> sockets = original->sockets;
    std::swap(sockets[0], sockets[1]);
    sockets[0].name = "Secondary";
    sockets.push_back({0, "factor", "Factor", LX::Direction::Input, LX::PinType::Float, factorInternal, 0.5});
    const bool updated =
        document.Execute(LX::LXUpdateGroup{group, "Mixed color", *original->body, sockets}, document.Revision())
            .applied;
    const LX::LXGroupDefinition* changed = document.Graph().FindGroup(group);
    const LX::Node* first = document.Graph().FindNode(firstInstance);
    const LX::Node* second = document.Graph().FindNode(secondInstance);
    const auto findPin = [](const LX::Node* node, LX::Id interfaceId) -> const LX::Pin* {
        const auto found = std::find_if(node->pins.begin(), node->pins.end(),
                                        [&](const LX::Pin& pin) { return pin.interfaceId == interfaceId; });
        return found == node->pins.end() ? nullptr : &*found;
    };
    const LX::Id factorInterface = changed->sockets.back().id;
    const LX::Pin* firstFactor = findPin(first, factorInterface);
    const LX::Pin* secondFactor = findPin(second, factorInterface);
    const bool synchronized = updated && changed->name == "Mixed color" && changed->sockets[0].id == bInterface &&
                              changed->sockets[1].id == aInterface && factorInterface && first->pins.size() == 4 &&
                              second->pins.size() == 4 && findPin(first, aInterface)->id == aPin && firstFactor &&
                              secondFactor && firstFactor->id != secondFactor->id &&
                              std::any_of(document.Graph().Links().begin(), document.Graph().Links().end(),
                                          [&](const LX::Link& link) { return link.input == aPin; }) &&
                              document.Graph().Validate().empty();
    const bool undone = document.Execute(LX::LXUndo{}, document.Revision()).applied &&
                        document.Graph().FindNode(firstInstance)->pins.size() == 3 &&
                        document.Graph().FindGroup(group)->name == "Color group";
    const bool redone = document.Execute(LX::LXRedo{}, document.Revision()).applied &&
                        document.Graph().FindNode(firstInstance)->pins.size() == 4;
    std::vector<LX::LXGroupSocket> reduced = document.Graph().FindGroup(group)->sockets;
    reduced.erase(reduced.begin());
    const bool removedUnlinked =
        document
            .Execute(LX::LXUpdateGroup{group, "Mixed color", *document.Graph().FindGroup(group)->body, reduced},
                     document.Revision())
            .applied &&
        document.Graph().FindNode(firstInstance)->pins.size() == 3 && document.Graph().Validate().empty();
    const bool saved = document.Execute(LX::LXSave{}, document.Revision()).applied;
    std::string error;
    auto reopened = LX::LXDocument::Open(path.string(), &error, ExampleRegistry());
    const bool roundTrip = reopened && reopened->Graph().Equals(document.Graph());
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
    const bool passed =
        noOp && connectedRemovalRejected && synchronized && undone && redone && removedUnlinked && saved && roundTrip;
    if (!passed)
    {
        std::cerr << "Group interface edit failed: " << error << std::endl;
    }
    return passed;
}

bool GroupEditorUiTest()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440.0f, 840.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph initial = Fixture();
    const LX::Id instance = AddExampleGroup(initial, 690.0f, 570.0f);
    const LX::Id group = instance ? initial.FindNode(instance)->groupId : 0;
    LX::LXDocument parent(std::move(initial));
    LX::CanvasState rootCanvas;
    rootCanvas.selectedNode = instance;
    GroupEditorState editor;
    LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    GroupEditorProbe probe;
    gGroupEditorProbe = &probe;
    const auto frame = [&]() {
        ImGui::NewFrame();
        DrawGroupEditor(editor, parent, styles, items);
        ImGui::Render();
    };
    const auto rootFrame = [&]() {
        ImGui::NewFrame();
        DrawUI(parent.GraphForCanvas(), rootCanvas, styles, items, "LatticeGroupEditorTest.lxg", nullptr, &parent,
               &editor);
        ImGui::Render();
    };
    const auto click = [&](ImVec2 point) {
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };

    bool opened = false;
    bool defaultEdited = false;
    bool exposed = false;
    bool applied = false;
    bool rejected = false;
    bool canceled = false;
    bool outputExposed = false;
    bool outputApplied = false;
    bool outputRoundTrip = false;
    if (group)
    {
        io.AddMousePosEvent(0.0f, 0.0f);
        rootFrame();
        io.AddMousePosEvent(probe.openPoint.x, probe.openPoint.y);
        rootFrame();
        io.AddMouseButtonEvent(0, true);
        rootFrame();
        io.AddMouseButtonEvent(0, false);
        rootFrame();
        opened = editor.draft.has_value() && editor.group == group;
    }
    if (opened)
    {
        const LX::Id internal = editor.draft->Graph().Nodes().front().id;
        const float originalX = editor.draft->Graph().FindLayout(internal)->x;
        io.AddMousePosEvent(0.0f, 0.0f);
        frame();
        const auto defaultTree = probe.defaultTreePoints.find("a");
        if (defaultTree != probe.defaultTreePoints.end())
        {
            click(defaultTree->second);
            const auto defaultCheckbox = probe.defaultCheckboxPoints.find("a");
            if (defaultCheckbox != probe.defaultCheckboxPoints.end())
            {
                click(defaultCheckbox->second);
                defaultEdited = editor.sockets[0].value == LX::LXSocketValue{std::array<double, 4>{0.0, 0.0, 0.0, 1.0}};
            }
            click(defaultTree->second);
        }
        io.AddMousePosEvent(0.0f, 0.0f);
        frame();
        const auto candidate = probe.inputCandidates.find("Multiply / Factor");
        if (candidate != probe.inputCandidates.end())
        {
            click(candidate->second);
            exposed = editor.sockets.size() == 4 && editor.sockets.back().name == "Factor";
        }
        editor.draft->Execute(LX::LXSetNodePosition{internal, originalX + 30.0f, 80.0f}, editor.draft->Revision());
        io.AddMousePosEvent(0.0f, 0.0f);
        frame();
        click(probe.applyPoint);
        applied = !editor.draft && parent.Revision() == 1 &&
                  parent.Graph().FindGroup(group)->body->FindLayout(internal)->x == originalX + 30.0f &&
                  parent.Graph().FindGroup(group)->sockets.size() == 4 &&
                  parent.Graph().FindGroup(group)->sockets[0].value ==
                      LX::LXSocketValue{std::array<double, 4>{0.0, 0.0, 0.0, 1.0}};
        if (editor.Open(parent, group))
        {
            editor.draft->Execute(LX::LXRemoveNode{internal}, editor.draft->Revision());
            io.AddMousePosEvent(0.0f, 0.0f);
            frame();
            click(probe.applyPoint);
            rejected = editor.draft.has_value() && editor.message.find("interface pins") != std::string::npos &&
                       parent.Revision() == 1;
            click(probe.cancelPoint);
            canceled =
                !editor.draft && parent.Revision() == 1 && parent.Graph().FindGroup(group)->body->FindNode(internal);
        }
    }
    LX::LXGraph outputInitial = Fixture();
    const LX::Id outputInstance = AddExampleGroup(outputInitial, 690.0f, 570.0f);
    const LX::Id outputGroup = outputInstance ? outputInitial.FindNode(outputInstance)->groupId : 0;
    LX::LXDocument outputParent(std::move(outputInitial));
    GroupEditorState outputEditor;
    if (outputGroup && outputEditor.Open(outputParent, outputGroup))
    {
        const LX::Id color =
            outputEditor.draft->Execute(LX::LXCreateNodeSpec{Spec(2), 300.0f, 100.0f}, outputEditor.draft->Revision())
                .created;
        const LX::Id internalOutput = color ? outputEditor.draft->Graph().FindNode(color)->pins.front().id : 0;
        const auto outputFrame = [&]() {
            ImGui::NewFrame();
            DrawGroupEditor(outputEditor, outputParent, styles, items);
            ImGui::Render();
        };
        const auto outputClick = [&](ImVec2 point) {
            io.AddMousePosEvent(point.x, point.y);
            outputFrame();
            io.AddMouseButtonEvent(0, true);
            outputFrame();
            io.AddMouseButtonEvent(0, false);
            outputFrame();
        };
        probe.outputCandidates.clear();
        io.AddMousePosEvent(0.0f, 0.0f);
        outputFrame();
        const auto candidate = probe.outputCandidates.find("Tint Color / Color");
        if (candidate != probe.outputCandidates.end())
        {
            outputClick(candidate->second);
            outputExposed = outputEditor.sockets.size() == 4 &&
                            outputEditor.sockets.back().direction == LX::Direction::Output &&
                            outputEditor.sockets.back().internalPin == internalOutput;
            outputClick(probe.applyPoint);
            const LX::LXGroupDefinition* definition = outputParent.Graph().FindGroup(outputGroup);
            outputApplied = !outputEditor.draft && outputParent.Revision() == 1 && definition &&
                            definition->sockets.size() == 4 &&
                            definition->sockets.back().internalPin == internalOutput &&
                            outputParent.Graph().FindNode(outputInstance)->pins.size() == 4 &&
                            outputParent.Graph().Validate().empty();
            if (outputApplied)
            {
                const std::filesystem::path path =
                    std::filesystem::temp_directory_path() /
                    ("lattice-group-output-" + std::to_string(GetCurrentProcessId()) + ".lxg");
                std::string error;
                const bool saved = outputParent.Graph().Save(path.string(), &error);
                const auto reopened =
                    saved ? LX::LXGraph::Load(path.string(), &error, ExampleRegistry()) : std::nullopt;
                outputRoundTrip = reopened && reopened->Equals(outputParent.Graph());
                std::filesystem::remove(path);
                std::filesystem::remove(path.string() + ".bak");
            }
        }
    }
    gGroupEditorProbe = nullptr;
    ImGui::DestroyContext();
    const bool passed = opened && defaultEdited && exposed && applied && rejected && canceled && outputExposed &&
                        outputApplied && outputRoundTrip;
    if (!passed)
    {
        std::cerr << "Group editor UI failure opened=" << opened << " defaultEdited=" << defaultEdited
                  << " exposed=" << exposed << " applied=" << applied << " rejected=" << rejected
                  << " canceled=" << canceled << " outputExposed=" << outputExposed
                  << " outputApplied=" << outputApplied << " outputRoundTrip=" << outputRoundTrip << std::endl;
    }
    return passed;
}

bool GroupBoundaryEditorUiTest()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440.0f, 840.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph body("material", ExampleRegistry());
    const LX::Id inputNode = body.AddGroupBoundaryNode(LX::Direction::Input, 40.0f, 80.0f);
    const LX::Id outputNode = body.AddGroupBoundaryNode(LX::Direction::Output, 420.0f, 80.0f);
    const LX::Id inputPin = body.AddGroupBoundaryPin(inputNode, "color", "Color", LX::PinType::Color);
    const LX::Id outputPin = body.AddGroupBoundaryPin(outputNode, "result", "Result", LX::PinType::Color);
    LX::LXGraph initial("material", ExampleRegistry());
    const LX::Id group =
        body.Connect(inputPin, outputPin)
            ? initial.CreateGroup("Boundary UI", body,
                                  {{0, "color", "Color", LX::Direction::Input, LX::PinType::Color, inputPin, {}},
                                   {0, "result", "Result", LX::Direction::Output, LX::PinType::Color, outputPin, {}}})
            : 0;
    const LX::Id instance = group ? initial.CreateGroupInstance(group, 670.0f, 410.0f) : 0;
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("lattice-boundary-ui-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    LX::LXDocument parent(std::move(initial), path.string());
    GroupEditorState editor;
    GroupEditorProbe probe;
    gGroupEditorProbe = &probe;
    const LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    const auto frame = [&]() {
        ImGui::NewFrame();
        DrawGroupEditor(editor, parent, styles, items);
        ImGui::Render();
    };
    const auto click = [&](ImVec2 point) {
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    bool defaultEdited = false;
    bool inputAdded = false;
    bool outputAdded = false;
    bool applied = false;
    bool roundTrip = false;
    if (instance && editor.Open(parent, group))
    {
        io.AddMousePosEvent(0.0f, 0.0f);
        frame();
        const auto tree = probe.defaultTreePoints.find("color");
        if (tree != probe.defaultTreePoints.end())
        {
            click(tree->second);
            const auto checkbox = probe.defaultCheckboxPoints.find("color");
            if (checkbox != probe.defaultCheckboxPoints.end())
            {
                click(checkbox->second);
                defaultEdited = editor.sockets[0].value == LX::LXSocketValue{std::array<double, 4>{0.0, 0.0, 0.0, 1.0}};
            }
            click(tree->second);
        }
        click(probe.addInputPoint);
        inputAdded = editor.sockets.size() == 3 && editor.sockets.back().direction == LX::Direction::Input &&
                     editor.draft->Graph().FindPin(editor.sockets.back().internalPin);
        io.AddMousePosEvent(150.0f, 700.0f);
        frame();
        io.AddMouseWheelEvent(0.0f, -5.0f);
        frame();
        click(probe.addOutputPoint);
        outputAdded = editor.sockets.size() == 4 && editor.sockets.back().direction == LX::Direction::Output &&
                      editor.draft->Graph().FindPin(editor.sockets.back().internalPin);
        click(probe.applyPoint);
        const LX::LXGroupDefinition* definition = parent.Graph().FindGroup(group);
        applied = !editor.draft && parent.Revision() == 1 && definition && definition->sockets.size() == 4 &&
                  definition->sockets[0].value == LX::LXSocketValue{std::array<double, 4>{0.0, 0.0, 0.0, 1.0}} &&
                  parent.Graph().FindNode(instance)->pins.size() == 4 && parent.Graph().Validate().empty();
        std::string error;
        const bool saved = applied && parent.Execute(LX::LXSave{}, parent.Revision()).applied;
        const auto reopened = saved ? LX::LXDocument::Open(path.string(), &error, ExampleRegistry()) : std::nullopt;
        roundTrip = reopened && reopened->Graph().Equals(parent.Graph());
    }
    gGroupEditorProbe = nullptr;
    ImGui::DestroyContext();
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
    const bool passed = defaultEdited && inputAdded && outputAdded && applied && roundTrip;
    if (!passed)
    {
        std::cerr << "Group boundary editor UI failed default=" << defaultEdited << " input=" << inputAdded
                  << " output=" << outputAdded << " apply=" << applied << " roundTrip=" << roundTrip
                  << " outputPoint=" << probe.addOutputPoint.x << ',' << probe.addOutputPoint.y << std::endl;
    }
    return passed;
}

bool CanvasFrameViewTest(float dpi, ImVec2 displaySize)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGui::GetStyle().ScaleAllSizes(dpi);
    ImGui::GetStyle().FontScaleDpi = dpi;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = displaySize;
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    LX::LXGraph graph;
    const LX::Id node = graph.AddNode(Spec(2), 100.0f, 120.0f);
    const LX::Id frameId = graph.AddFrame("Test frame", 70.0f, 70.0f, 320.0f, 240.0f, {node});
    LX::LXDocument document(std::move(graph));
    LX::CanvasState canvas;
    const LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    ImVec2 origin{};
    const auto draw = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Frame view test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        origin = ImGui::GetCursorScreenPos();
        LX::DrawCanvas(document.GraphForCanvas(), canvas, styles, items, &document);
        ImGui::End();
        ImGui::Render();
    };
    io.AddMousePosEvent(0.0f, 0.0f);
    draw();
    const ImVec2 header{origin.x + canvas.pan.x + 80.0f * dpi, origin.y + canvas.pan.y + 80.0f * dpi};
    io.AddMousePosEvent(header.x, header.y);
    draw();
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMousePosEvent(header.x + 40.0f * dpi, header.y + 25.0f * dpi);
    draw();
    const bool preview = document.Revision() == 0 && document.Graph().FindLayout(node)->x == 140.0f;
    io.AddMouseButtonEvent(0, false);
    draw();
    const bool moved = document.Revision() == 1 && canvas.selectedFrame == frameId &&
                       std::abs(document.Graph().FindFrame(frameId)->x - 110.0f) < 0.51f / dpi &&
                       std::abs(document.Graph().FindLayout(node)->y - 145.0f) < 0.51f / dpi;
    const bool undone = document.Execute(LX::LXUndo{}, document.Revision()).applied &&
                        document.Graph().FindFrame(frameId)->x == 70.0f &&
                        document.Graph().FindLayout(node)->y == 120.0f;
    io.AddKeyEvent(ImGuiKey_Delete, true);
    draw();
    const bool deleted = !document.Graph().FindFrame(frameId) && document.Graph().FindLayout(node)->frame == 0 &&
                         canvas.selectedFrame == 0;
    io.AddKeyEvent(ImGuiKey_Delete, false);
    draw();
    const bool deleteUndone = document.Execute(LX::LXUndo{}, document.Revision()).applied &&
                              document.Graph().FindFrame(frameId) &&
                              document.Graph().FindLayout(node)->frame == frameId;
    const bool viewSet = document.Execute(LX::LXSetView{{true, 375.5f, -42.25f, 1.25f}}, document.Revision()).applied;
    canvas = LX::CanvasState{};
    io.AddMousePosEvent(0.0f, 0.0f);
    draw();
    const float expectedX = canvas.canvasSize.x * 0.5f - 375.5f * 1.25f * dpi;
    const float expectedY = canvas.canvasSize.y * 0.5f + 42.25f * 1.25f * dpi;
    const bool restored = std::abs(canvas.pan.x - expectedX) < 0.01f && std::abs(canvas.pan.y - expectedY) < 0.01f &&
                          canvas.zoom == 1.25f;
    io.DisplaySize = {displaySize.x + 100.0f * dpi, displaySize.y + 60.0f * dpi};
    draw();
    const bool resized = std::abs(canvas.pan.x - (canvas.canvasSize.x * 0.5f - 375.5f * 1.25f * dpi)) < 0.01f &&
                         std::abs(canvas.pan.y - (canvas.canvasSize.y * 0.5f + 42.25f * 1.25f * dpi)) < 0.01f;
    const float changedDpi = dpi * 1.2f;
    ImGui::GetStyle().FontScaleDpi = changedDpi;
    draw();
    const bool dpiChanged =
        std::abs(canvas.pan.x - (canvas.canvasSize.x * 0.5f - 375.5f * 1.25f * changedDpi)) < 0.01f &&
        std::abs(canvas.pan.y - (canvas.canvasSize.y * 0.5f + 42.25f * 1.25f * changedDpi)) < 0.01f;
    const float userScale = 1.35f;
    ImGui::GetStyle().FontScaleMain = userScale;
    ImGui::GetStyle().ScaleAllSizes(userScale);
    draw();
    const float combinedScale = changedDpi * userScale;
    const bool userScaleChanged =
        std::abs(canvas.viewDpi - combinedScale) < 0.01f &&
        std::abs(canvas.pan.x - (canvas.canvasSize.x * 0.5f - 375.5f * 1.25f * combinedScale)) < 0.01f &&
        std::abs(canvas.pan.y - (canvas.canvasSize.y * 0.5f + 42.25f * 1.25f * combinedScale)) < 0.01f;
    const ImVec2 center{origin.x + canvas.canvasSize.x * 0.5f, origin.y + canvas.canvasSize.y * 0.5f};
    io.AddMousePosEvent(center.x, center.y);
    draw();
    const std::uint64_t beforeWheel = document.Revision();
    io.AddMouseWheelEvent(0.0f, 1.0f);
    draw();
    const bool zoomed = document.Revision() == beforeWheel + 1 && document.Graph().Layout().view.zoom == canvas.zoom &&
                        canvas.zoom > 1.25f;
    const std::uint64_t beforePan = document.Revision();
    io.AddMouseButtonEvent(2, true);
    draw();
    io.AddMousePosEvent(center.x + 30.0f * dpi, center.y + 20.0f * dpi);
    draw();
    const bool panPreview = document.Revision() == beforePan;
    io.AddMouseButtonEvent(2, false);
    draw();
    const bool panSaved = document.Revision() == beforePan + 1 && document.Graph().Layout().view.saved;
    ImGui::DestroyContext();
    const bool passed = frameId && preview && moved && undone && deleted && deleteUndone && viewSet && restored &&
                        resized && dpiChanged && userScaleChanged && zoomed && panPreview && panSaved;
    if (!passed)
    {
        std::cerr << "Frame or view gesture failed at dpi=" << dpi << " preview=" << preview << " moved=" << moved
                  << " undo=" << undone << " delete=" << deleted << " deleteUndo=" << deleteUndone
                  << " restored=" << restored << " resize=" << resized << " dpiChange=" << dpiChanged
                  << " userScale=" << userScaleChanged << " zoom=" << zoomed << " pan=" << panSaved << std::endl;
    }
    return passed;
}

bool AnimationStyleTest()
{
    LX::LXGraph graph = AnimationFixture();
    const LX::LXStyleSheet styles = AnimationStyles();
    const LX::LXNodeItemRegistry noItems;
    if (graph.Nodes().size() != 4 || graph.Links().size() != 5 || !graph.Validate().empty())
    {
        std::cerr << "Animation state fixture or return transition is invalid" << std::endl;
        return false;
    }
    LX::LXGraph dataGraph("animation");
    const LX::NodeSpec dataSpec = {
        "FSM_DATA", "Value", {Input("In", LX::PinType::Float), Output("Out", LX::PinType::Float)}};
    const LX::Id firstData = dataGraph.AddNode(dataSpec, 0.0f, 0.0f);
    const LX::Id secondData = dataGraph.AddNode(dataSpec, 100.0f, 0.0f);
    if (!firstData || !secondData ||
        !dataGraph.Connect(dataGraph.FindNode(firstData)->pins[1].id, dataGraph.FindNode(secondData)->pins[0].id) ||
        dataGraph.Connect(dataGraph.FindNode(secondData)->pins[1].id, dataGraph.FindNode(firstData)->pins[0].id))
    {
        std::cerr << "Animation flow cycle policy affected data links" << std::endl;
        return false;
    }
    for (const LX::Node& node : graph.Nodes())
    {
        const LX::NodeLayout& layout = Placement(graph, node);
        const LX::LXNodeStyle& style = styles.ForNode(node);
        const LX::LXNodeGeometry geometry = LX::MeasureNode(node, layout, style, noItems);
        if (!style.headerOnly || layout.collapsed || geometry.height != style.headerHeight + 4.0f)
        {
            return false;
        }
        for (const LX::Pin& pin : node.pins)
        {
            const ImVec2 point = LX::PinPosition(node, layout, pin, style, &noItems);
            const float edge = pin.direction == LX::Direction::Input ? layout.x : layout.x + geometry.width;
            if (point.x != edge || point.y != layout.y + geometry.height * 0.5f || !styles.ForPin(pin).visible)
            {
                return false;
            }
        }
    }
    const auto onCapsule = [](ImVec2 point, const LX::NodeLayout& layout, LX::LXNodeGeometry geometry) {
        const float radius = geometry.height * 0.5f;
        const float flatEnd = geometry.width * 0.5f - radius;
        const float x = std::abs(point.x - layout.x - geometry.width * 0.5f);
        const float y = std::abs(point.y - layout.y - radius);
        const float error = x <= flatEnd ? std::abs(y - radius) : std::abs(std::hypot(x - flatEnd, y) - radius);
        return error < 0.01f;
    };
    for (const LX::Link& link : graph.Links())
    {
        const LX::Node& source = *graph.FindNode(graph.FindPin(link.output)->node);
        const LX::Node& target = *graph.FindNode(graph.FindPin(link.input)->node);
        const LX::NodeLayout& sourceLayout = Placement(graph, source);
        const LX::NodeLayout& targetLayout = Placement(graph, target);
        const LX::LXNodeGeometry sourceSize = LX::MeasureNode(source, sourceLayout, styles.ForNode(source), noItems);
        const LX::LXNodeGeometry targetSize = LX::MeasureNode(target, targetLayout, styles.ForNode(target), noItems);
        const auto endpoints = LX::HeaderLinkEndpoints(graph, link, styles, noItems);
        const bool targetOnRight = targetLayout.x + targetSize.width * 0.5f >= sourceLayout.x + sourceSize.width * 0.5f;
        const bool correctSides =
            targetOnRight
                ? endpoints && endpoints->output.x >= sourceLayout.x + sourceSize.width - sourceSize.height * 0.5f &&
                      endpoints->input.x <= targetLayout.x + targetSize.height * 0.5f
                : endpoints && endpoints->output.x <= sourceLayout.x + sourceSize.height * 0.5f &&
                      endpoints->input.x >= targetLayout.x + targetSize.width - targetSize.height * 0.5f;
        if (!endpoints || !onCapsule(endpoints->output, sourceLayout, sourceSize) ||
            !onCapsule(endpoints->input, targetLayout, targetSize) || !correctSides)
        {
            std::cerr << "Animation transition port is not on the facing capsule edge" << std::endl;
            return false;
        }
    }
    const float idleTop = LX::HeaderLinkEndpoints(graph, graph.Links()[1], styles, noItems)->output.y;
    const float idleMiddle = LX::HeaderLinkEndpoints(graph, graph.Links()[2], styles, noItems)->input.y;
    const float idleBottom = LX::HeaderLinkEndpoints(graph, graph.Links()[4], styles, noItems)->input.y;
    const auto forward = LX::HeaderLinkEndpoints(graph, graph.Links()[1], styles, noItems);
    const auto backward = LX::HeaderLinkEndpoints(graph, graph.Links()[2], styles, noItems);
    const auto diagonal = LX::HeaderLinkEndpoints(graph, graph.Links()[4], styles, noItems);
    if (!(idleTop < idleMiddle && idleMiddle < idleBottom) || forward->output.y != forward->input.y ||
        backward->output.y != backward->input.y || diagonal->output.y == diagonal->input.y ||
        styles.ForWire(graph.Links()[2], LX::PinType::Flow).route != LX::LXWireStyle::Route::Straight)
    {
        std::cerr << "Animation facing ports or direct wire route are invalid" << std::endl;
        return false;
    }

    const std::filesystem::path graphPath =
        std::filesystem::temp_directory_path() /
        ("lattice-animation-style-" + std::to_string(GetCurrentProcessId()) + ".lxg");
    const std::filesystem::path stylePath = graphPath.string() + ".lxstyle";
    std::string error;
    const bool saved = graph.Save(graphPath.string(), &error) && styles.Save(stylePath.string(), &error);
    const auto loadedGraph = saved ? LX::LXGraph::Load(graphPath.string(), &error, ExampleRegistry()) : std::nullopt;
    const auto loadedStyle = saved ? LX::LXStyleSheet::Load(stylePath.string(), &error) : std::nullopt;
    const bool roundTrip = loadedGraph && graph.Equals(*loadedGraph) && loadedStyle && styles.Equals(*loadedStyle) &&
                           loadedStyle->ForNode(graph.Nodes()[1]).headerOnly &&
                           loadedStyle->ForPin(graph.Nodes()[1].pins[0]).visible &&
                           loadedStyle->ForWire(graph.Links()[0], LX::PinType::Flow).arrowAtMidpoint;
    std::filesystem::remove(graphPath);
    std::filesystem::remove(stylePath);
    std::filesystem::remove(graphPath.string() + ".bak");
    std::filesystem::remove(stylePath.string() + ".bak");
    if (!roundTrip)
    {
        std::cerr << "Animation graph/style round-trip failed: " << error << std::endl;
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ApplyExampleTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440.0f, 840.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    LX::CanvasState canvas;
    ImVec2 origin{};
    const auto frame = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Animation style test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        origin = ImGui::GetCursorScreenPos();
        LX::DrawCanvas(graph, canvas, styles, noItems);
        ImGui::End();
        ImGui::Render();
    };
    const auto click = [&](ImVec2 point) {
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    io.AddMousePosEvent(0.0f, 0.0f);
    frame();
    const LX::Node& idle = graph.Nodes()[1];
    const LX::NodeLayout& idleLayout = Placement(graph, idle);
    click({origin.x + canvas.pan.x + idleLayout.x + 18.0f, origin.y + canvas.pan.y + idleLayout.y + 18.0f});
    const bool headerSelected = canvas.selectedNode == idle.id && !Placement(graph, idle).collapsed;
    const ImVec2 from = LX::HeaderLinkEndpoints(graph, graph.Links()[0], styles, noItems)->output;
    const ImVec2 to = LX::HeaderLinkEndpoints(graph, graph.Links()[3], styles, noItems)->input;
    click({origin.x + canvas.pan.x + from.x, origin.y + canvas.pan.y + from.y});
    const bool previewStartsAtPort = canvas.pendingPin == graph.Links()[0].output &&
                                     std::abs(canvas.pendingAnchor.x - from.x) < 0.01f &&
                                     std::abs(canvas.pendingAnchor.y - from.y) < 0.01f;
    click({origin.x + canvas.pan.x + to.x, origin.y + canvas.pan.y + to.y});
    const bool connectedPortsConnect = graph.Links().size() == 6 && canvas.pendingPin == 0;
    const LX::Node& attack = graph.Nodes()[3];
    const ImVec2 freeSide =
        LX::PinPosition(attack, Placement(graph, attack), attack.pins.back(), styles.ForNode(attack), &noItems);
    click({origin.x + canvas.pan.x + freeSide.x, origin.y + canvas.pan.y + freeSide.y});
    const bool freeSidePort =
        canvas.pendingPin == attack.pins.back().id && std::abs(canvas.pendingAnchor.x - freeSide.x) < 0.01f;
    const bool rendered = ImGui::GetDrawData() && ImGui::GetDrawData()->TotalVtxCount > 100;
    ImGui::DestroyContext();
    if (!headerSelected || !previewStartsAtPort || !connectedPortsConnect || !freeSidePort || !rendered)
    {
        std::cerr << "Animation header or connected port hit test failed: selected=" << headerSelected
                  << " preview=" << previewStartsAtPort << " connected=" << connectedPortsConnect
                  << " freeSide=" << freeSidePort << " rendered=" << rendered << std::endl;
    }
    return headerSelected && previewStartsAtPort && connectedPortsConnect && freeSidePort && rendered;
}

bool MaterialNodeMenuInteractionTest()
{
    ImGui::CreateContext();
    ApplyExampleTheme();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440.0f, 840.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    const auto definitions = LX::CreateMaterialDefinitions();
    const auto entries = editor::material_editing::MaterialNodeMenuEntries(definitions, "Reroute");
    std::string search = "Reroute";
    std::optional<std::string> selected;
    const auto frame = [&](bool open) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20.0f, 20.0f});
        ImGui::SetNextWindowSize({700.0f, 650.0f});
        ImGui::Begin("Material Add menu interaction", nullptr, ImGuiWindowFlags_NoSavedSettings);
        if (open)
        {
            ImGui::OpenPopup("Add");
        }
        if (ImGui::BeginPopup("Add"))
        {
            if (open)
            {
                ImGui::OpenPopup("Reroute");
            }
            if (const auto type = editor::material_editing::DrawMaterialNodeAddMenu(definitions, search))
            {
                selected = type;
            }
            ImGui::EndPopup();
        }
        ImGui::End();
        ImGui::Render();
    };
    bool passed = true;
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        selected.reset();
        frame(true);
        frame(false);
        const auto& context = *ImGui::GetCurrentContext();
        if (context.OpenPopupStack.Size != 2 || !context.OpenPopupStack[1].Window)
        {
            passed = false;
            break;
        }
        const auto* menu = context.OpenPopupStack[1].Window;
        const float stride = ImGui::GetTextLineHeightWithSpacing();
        const ImVec2 point{menu->DC.CursorStartPos.x + 20.0f, menu->DC.CursorStartPos.y +
                                                                  static_cast<float>(index) * stride +
                          ImGui::GetFontSize() * 0.5f};
        io.AddMousePosEvent(point.x, point.y);
        frame(false);
        frame(false);
        if (context.HoveredIdPreviousFrameItemCount != 1)
        {
            passed = false;
            break;
        }
        io.AddMouseButtonEvent(0, true);
        frame(false);
        io.AddMouseButtonEvent(0, false);
        frame(false);
        if (!selected || *selected != entries[index].type)
        {
            std::cerr << "Material Reroute menu selected the wrong registered type: " << entries[index].type
                      << std::endl;
            passed = false;
            break;
        }
    }
    ImGui::DestroyContext();
    if (passed)
    {
        std::cout << "LX_MATERIAL_ADD_MENU_INTERACTION_OK 9 distinct clicks, hovered ID count 1" << std::endl;
    }
    else
    {
        std::cerr << "Material Add submenu interaction failed" << std::endl;
    }
    return passed;
}

bool SelfTest()
{
    if (!RunMaterialGraphTests())
    {
        return false;
    }
    {
        const auto definitions = LX::CreateMaterialDefinitions();
        const auto entries = editor::material_editing::MaterialNodeMenuEntries(definitions);
        std::set<std::pair<std::string, std::string>> labels;
        std::set<std::string> types;
        std::size_t reroutes = 0;
        std::size_t multiplies = 0;
        for (const auto& entry : entries)
        {
            if (!types.insert(entry.type).second || !labels.emplace(entry.group, entry.label).second)
            {
                std::cerr << "Material Add menu contains duplicate identities or ambiguous labels" << std::endl;
                return false;
            }
            if (entry.group == "Reroute")
            {
                ++reroutes;
                const auto* definition = definitions.nodes->Find(entry.type);
                if (entry.label != LX::PinTypeName(definition->defaults.pins.front().type))
                {
                    return false;
                }
            }
            if (entry.type.starts_with("LXMultiply"))
            {
                ++multiplies;
                if (entry.label == "Multiply")
                {
                    return false;
                }
            }
        }
        const auto filtered = editor::material_editing::MaterialNodeMenuEntries(definitions, "LXRerouteFloat");
        if (reroutes != 9 || multiplies != 2 || filtered.size() != 1 || filtered.front().group != "Reroute" ||
            filtered.front().type != "LXRerouteFloat" || filtered.front().label != "Float")
        {
            return false;
        }
        std::cout << "LX_MATERIAL_ADD_MENU_OK unique labels, 9 typed reroutes, 2 typed multiplies, filtered identity"
                  << std::endl;
    }
    if (!MaterialNodeMenuInteractionTest())
    {
        return false;
    }
    {
        LX::LXMaterialAsset material;
        const auto nodeId = material.CreateNode("ShaderNodeBsdfPrincipled", 10.0f, 20.0f);
        const auto outputId = material.CreateNode("ShaderNodeOutputMaterial", 500.0f, 20.0f);
        const auto* node = material.graph.FindNode(nodeId);
        auto items = editor::material_editing::MaterialItems(material.Definitions());
        const auto styles = editor::material_editing::BlenderStyles(material.Definitions());
        const auto& style = styles.ForNode(*node);
        const auto& layout = *material.graph.FindLayout(nodeId);
        const auto base =
            std::ranges::find_if(node->pins, [](const LX::Pin& pin) { return pin.Identifier() == "Base Color"; });
        const auto sss = std::ranges::find_if(
            node->pins, [](const LX::Pin& pin) { return pin.Identifier() == "Subsurface Weight"; });
        const auto height = LX::MeasureNode(*node, layout, style, items).height;
        const auto control = LX::ItemRect(*node, layout, "Base Color", style, items);
        const auto pin = LX::PinPosition(*node, layout, *base, style, &items);
        const auto* output = material.graph.FindNode(outputId);
        if (items.Find(*output, "target") ||
            std::ranges::any_of(items.Rows(*output), [](const LX::LXNodeRow& row) { return row.key == "target"; }))
        {
            std::cerr << "Material output exposed a Blender renderer selector" << std::endl;
            return false;
        }
        struct CopyObservedPredicate
        {
            int* copies;

            explicit CopyObservedPredicate(int& count) : copies(&count) {}
            CopyObservedPredicate(const CopyObservedPredicate& other) : copies(other.copies) { ++*copies; }
            bool operator()(const LX::Pin&) const { return true; }
        };
        int predicateCopies = 0;
        items.SetPinPredicates(CopyObservedPredicate(predicateCopies), {});
        predicateCopies = 0;
        for (int sample = 0; sample < 32; ++sample)
        {
            const auto measured = LX::PinPosition(*node, layout, *base, style, &items);
            if (measured.x != pin.x || measured.y != pin.y || predicateCopies != 0)
            {
                std::cerr << "Pin positioning copied the item registry or changed its geometry" << std::endl;
                return false;
            }
        }
        int predicateCalls = 0;
        items.SetPinPredicates(
            [&](const LX::Pin&) {
                ++predicateCalls;
                return true;
            },
            {});
        {
            const LX::LXNodeItemRegistry::RowCacheScope rowCache(items);
            LX::PinPosition(*node, layout, *base, style, &items);
            const int firstRowPass = predicateCalls;
            for (int sample = 0; sample < 32; ++sample)
            {
                LX::PinPosition(*node, layout, *base, style, &items);
            }
            if (firstRowPass == 0 || predicateCalls != firstRowPass)
            {
                std::cerr << "Visible rows were recomputed within the canvas frame" << std::endl;
                return false;
            }
            items.ToggleSection(*node, "Subsurface");
            if (!items.PinRow(*node, *sss))
            {
                std::cerr << "Section toggle did not invalidate visible rows" << std::endl;
                return false;
            }
            items.ToggleSection(*node, "Subsurface");
        }
        const int beforeNextFrame = predicateCalls;
        LX::PinPosition(*node, layout, *base, style, &items);
        if (predicateCalls == beforeNextFrame)
        {
            std::cerr << "Visible rows survived beyond their canvas frame" << std::endl;
            return false;
        }
        if (height >= 500.0f || pin.y != (control.minimum.y + control.maximum.y) * 0.5f || items.PinRow(*node, *sss))
        {
            return false;
        }
        items.ToggleSection(*node, "Subsurface");
        if (!items.PinRow(*node, *sss) || LX::MeasureNode(*node, layout, style, items).height <= height)
        {
            return false;
        }
        items.ToggleSection(*node, "Subsurface");
        auto compact = layout;
        compact.collapsed = true;
        if (LX::MeasureNode(*node, compact, style, items).height > 60.0f)
        {
            return false;
        }
        LX::LXDocument document(material.graph);
        const auto pinId = base->id;
        if (!document
                 .Execute(LX::LXSetSocketValue{pinId, std::array<double, 4>{0.2, 0.3, 0.4, 1.0}}, document.Revision())
                 .applied ||
            !document.AcceptSavedSnapshot() || document.Dirty() ||
            !document.Execute(LX::LXUndo{}, document.Revision()).applied || !document.Dirty() ||
            !document.Execute(LX::LXRedo{}, document.Revision()).applied || document.Dirty())
        {
            return false;
        }
        std::cout << "LX_MATERIAL_PRESENTATION_OK rows, sections, compact pins, saved Undo baseline" << std::endl;
    }
    if (!RunMaterialCompilerTests())
    {
        return false;
    }
    LX::LXGraph graph = Fixture();
    if (!graph.Validate().empty() || graph.Nodes().size() != 7 || graph.Links().size() != 6)
    {
        for (const LX::Issue& issue : graph.Validate())
        {
            std::cerr << issue.code << ": " << issue.message << " node=" << issue.node << std::endl;
        }
        std::cerr << "Fixture nodes=" << graph.Nodes().size() << " links=" << graph.Links().size() << std::endl;
        return false;
    }
    const auto& nodes = graph.Nodes();
    std::string reason;
    if (graph.Connect(nodes[2].pins[0].id, nodes[6].pins[1].id, &reason) || reason != "Pin types differ")
    {
        return false;
    }
    if (graph.Connect(nodes[3].pins[3].id, nodes[3].pins[0].id, &reason) || reason != "Self links are not allowed")
    {
        return false;
    }

    LX::LXGraph cycleGraph;
    const LX::NodeSpec passthrough = {
        "PASSTHROUGH", "Pass", {Input("In", LX::PinType::Color), Output("Out", LX::PinType::Color)}};
    const LX::Id first = cycleGraph.AddNode(passthrough, 0, 0);
    const LX::Id second = cycleGraph.AddNode(passthrough, 100, 100);
    const LX::Id firstInput = cycleGraph.FindNode(first)->pins[0].id;
    const LX::Id firstOutput = cycleGraph.FindNode(first)->pins[1].id;
    const LX::Id secondInput = cycleGraph.FindNode(second)->pins[0].id;
    const LX::Id secondOutput = cycleGraph.FindNode(second)->pins[1].id;
    if (!cycleGraph.Connect(firstOutput, secondInput) || cycleGraph.Connect(secondOutput, firstInput, &reason) ||
        reason != "Cycle is not allowed")
    {
        return false;
    }

    LX::LXStyleSheet styles;
    LX::LXNodeStyle typeStyle = styles.defaultNode;
    typeStyle.width = 240.0f;
    styles.SetTypeStyle("TEXTURE", typeStyle);
    if (styles.ForNode(nodes[0]).width != 240.0f || styles.ForNode(nodes[1]).width != styles.defaultNode.width)
    {
        return false;
    }
    LX::LXNodeStyle individualStyle = typeStyle;
    individualStyle.width = 280.0f;
    styles.SetNodeStyle(nodes[0].id, individualStyle);
    if (styles.ForNode(nodes[0]).width != 280.0f || styles.ForNode(nodes[4]).width != styles.defaultNode.width)
    {
        return false;
    }
    styles.ClearNodeStyle(nodes[0].id);
    if (styles.ForNode(nodes[0]).width != 240.0f)
    {
        return false;
    }

    const LX::Node& multiply = nodes[3];
    const LX::NodeLayout& multiplyLayout = Placement(graph, multiply);
    const LX::LXNodeStyle& multiplyStyle = styles.ForNode(multiply);
    const LX::LXNodeItemRegistry items = ExampleItems();
    const LX::LXStyleSheet exampleStyles = ExampleStyles();
    if (!multiply.properties.empty() || items.Keys(multiply) != std::vector<std::string>{"Factor"} ||
        items.ValuePin(multiply, "Factor") != &multiply.pins[2] ||
        items.ValuePin(nodes[2], "Value") != &nodes[2].pins[0] ||
        items.ValuePin(nodes[0], "Asset") != &nodes[0].pins[0] ||
        items.ValuePin(nodes[4], "Asset") != &nodes[4].pins[0])
    {
        std::cerr << "Material items did not bind to typed sockets" << std::endl;
        return false;
    }
    const LX::LXNodeStyle& sampleStyle = exampleStyles.ForNode(nodes[1]);
    if (LX::MeasureNode(nodes[1], Placement(graph, nodes[1]), sampleStyle, items).height !=
        sampleStyle.headerHeight + sampleStyle.rowHeight + sampleStyle.bodyBottomPadding)
    {
        return false;
    }
    const ImVec2 firstRow = LX::PinPosition(multiply, multiplyLayout, multiply.pins[0], multiplyStyle);
    const ImVec2 secondRow = LX::PinPosition(multiply, multiplyLayout, multiply.pins[1], multiplyStyle);
    const ImVec2 factorRow = LX::PinPosition(multiply, multiplyLayout, multiply.pins[2], multiplyStyle);
    const ImVec2 outputRow = LX::PinPosition(multiply, multiplyLayout, multiply.pins[3], multiplyStyle);
    if (firstRow.x != multiplyLayout.x || secondRow.y - firstRow.y != multiplyStyle.rowHeight ||
        outputRow.x != multiplyLayout.x + multiplyStyle.width || outputRow.y != firstRow.y ||
        LX::MeasureNode(multiply, multiplyLayout, multiplyStyle, items).height <
            multiplyStyle.headerHeight + 3.0f * multiplyStyle.rowHeight ||
        factorRow.y - secondRow.y != multiplyStyle.rowHeight ||
        LX::ItemRect(multiply, multiplyLayout, "Factor", multiplyStyle, items).minimum.y >= factorRow.y ||
        LX::ItemRect(multiply, multiplyLayout, "Factor", multiplyStyle, items).maximum.y <= factorRow.y ||
        items.Find(multiply, "Factor")->kind != LX::LXNodeItemKind::FloatSlider)
    {
        return false;
    }

    LX::LXPinStyle texturePin = styles.defaultPin;
    texturePin.shape = LX::LXPinShape::Diamond;
    styles.SetPinTypeStyle(LX::PinType::Texture, texturePin);
    if (styles.ForPin(nodes[0].pins[0]).shape != LX::LXPinShape::Diamond)
    {
        return false;
    }
    LX::LXPinStyle individualPin = texturePin;
    individualPin.shape = LX::LXPinShape::Triangle;
    styles.SetPinStyle(nodes[0].pins[0].id, individualPin);
    if (styles.ForPin(nodes[0].pins[0]).shape != LX::LXPinShape::Triangle ||
        styles.ForPin(nodes[4].pins[0]).shape != LX::LXPinShape::Diamond)
    {
        return false;
    }
    LX::LXWireStyle colorWire = styles.defaultWire;
    colorWire.thickness = 3.0f;
    styles.SetWireTypeStyle(LX::PinType::Color, colorWire);
    const LX::Link& colorLink = graph.Links()[1];
    if (styles.ForWire(colorLink, LX::PinType::Color).thickness != 3.0f)
    {
        return false;
    }
    LX::LXWireStyle individualWire = colorWire;
    individualWire.thickness = 4.0f;
    styles.SetWireStyle(colorLink.id, individualWire);
    if (styles.ForWire(colorLink, LX::PinType::Color).thickness != 4.0f)
    {
        return false;
    }

    styles.canvas.showGrid = true;
    styles.canvas.gridPattern = LX::LXGridPattern::Dots;
    styles.canvas.gridDotRadius = 1.15f;
    const std::filesystem::path styleFile = std::filesystem::temp_directory_path() / "lattice-example-selftest.lxstyle";
    if (!styles.Save(styleFile.string(), &reason))
    {
        return false;
    }
    const auto restoredStyles = LX::LXStyleSheet::Load(styleFile.string(), &reason);
    std::ostringstream legacyStyle;
    {
        std::ifstream source(styleFile);
        std::string line;
        while (std::getline(source, line))
        {
            if (line == "LXS 6")
            {
                line = "LXS 5";
            }
            else if (line.starts_with("C "))
            {
                line.erase(line.rfind(' '));
                line.erase(line.rfind(' '));
            }
            legacyStyle << line << '\n';
        }
    }
    std::filesystem::remove(styleFile.string() + ".bak");
    {
        std::ofstream legacy(styleFile, std::ios::trunc);
        legacy << legacyStyle.str();
    }
    const auto restoredLegacyStyle = LX::LXStyleSheet::Load(styleFile.string(), &reason);
    std::filesystem::remove(styleFile);
    std::filesystem::remove(styleFile.string() + ".bak");
    if (!restoredStyles || !restoredStyles->canvas.showGrid ||
        restoredStyles->canvas.gridPattern != LX::LXGridPattern::Dots ||
        restoredStyles->canvas.gridDotRadius != 1.15f || restoredStyles->SourceVersion() != 6 ||
        restoredStyles->ForNode(nodes[0]).width != 240.0f ||
        restoredStyles->ForPin(nodes[0].pins[0]).shape != LX::LXPinShape::Triangle ||
        restoredStyles->ForWire(colorLink, LX::PinType::Color).thickness != 4.0f)
    {
        return false;
    }
    if (!restoredLegacyStyle || restoredLegacyStyle->SourceVersion() != 5 ||
        restoredLegacyStyle->canvas.gridPattern != LX::LXGridPattern::Lines ||
        restoredLegacyStyle->ForNode(nodes[0]).width != 240.0f)
    {
        return false;
    }
    std::cout << "LX_GRID_STYLE_OK dots round-trip, legacy line styles" << std::endl;

    const auto extra = graph.AddNode(Spec(3), 100, 100);
    graph.Connect(nodes[2].pins[0].id, graph.FindNode(extra)->pins[0].id);
    const std::size_t before = graph.Links().size();
    if (!graph.Undo() || graph.Links().size() != before - 1 || !graph.Redo() || graph.Links().size() != before)
    {
        return false;
    }
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "lattice-example-selftest.lxg";
    if (!graph.Save(file.string(), &reason))
    {
        return false;
    }
    const auto restored = LX::LXGraph::Load(file.string(), &reason);
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    const bool initialRoundTrip = restored && graph.Equals(*restored) && restored->Validate().empty();
    if (!initialRoundTrip)
    {
        std::cerr << "Initial graph round-trip failed: " << reason << std::endl;
        return false;
    }
    if (!PersistenceTest())
    {
        return false;
    }
    if (!CanvasRenderTest(1.0f, {1440.0f, 840.0f}))
    {
        return false;
    }
    if (!CanvasRenderTest(1.5f, {2160.0f, 1260.0f}))
    {
        return false;
    }
    if (!CanvasRenderTest(1.0f, {800.0f, 600.0f}, 250) || !CanvasRenderTest(1.5f, {1350.0f, 960.0f}, 250))
    {
        return false;
    }
    return AnimationStyleTest() && DefinitionRegistryTest() && PinIdentifierTest() && SocketValueTest() &&
           DocumentCommandTest() && FrameLayoutTest() && GroupContractTest() && GroupBoundaryTest() &&
           GroupCollapseTest() && GroupNestingTest() && GroupNestingUiTest() && GroupCollapseUiTest() &&
           GroupBodyEditTest() && GroupInterfaceEditTest() && GroupEditorUiTest() && GroupBoundaryEditorUiTest() &&
           CanvasFrameViewTest(1.0f, {900.0f, 640.0f}) && CanvasFrameViewTest(1.5f, {1350.0f, 960.0f}) &&
           DynamicPinTest() && DynamicPinUiTest() && PortCreationTest() && ProblemNavigationTest() &&
           CanvasItemInteractionTest() && InspectorInputTest() && CanvasGestureTest(1.0f, {900.0f, 640.0f}) &&
           CanvasGestureTest(1.5f, {1350.0f, 960.0f}) && CanvasGestureTest(1.0f, {900.0f, 640.0f}, true) &&
           GroupLibraryTest() && LibrarySearchTest() && NativeWindowMetricsTest();
}

void DrawUI(LX::LXGraph& graph, LX::CanvasState& canvas, LX::LXStyleSheet& styles, const LX::LXNodeItemRegistry& items,
            const std::string& file, ExamplePage* page, LX::LXDocument* document, GroupEditorState* groupEditor)
{
    assert(!document || &graph == &document->GraphForCanvas());
    const auto createNode = [&](const LX::NodeSpec& spec, float x, float y) {
        return document ? document->Execute(LX::LXCreateNodeSpec{spec, x, y}, document->Revision()).created
                        : graph.AddNode(spec, x, y);
    };
    const auto undo = [&]() {
        return document ? document->Execute(LX::LXUndo{}, document->Revision()).applied : graph.Undo();
    };
    const auto redo = [&]() {
        return document ? document->Execute(LX::LXRedo{}, document->Revision()).applied : graph.Redo();
    };
    const auto setProperty = [&](LX::Id node, const std::string& key, const std::string& value) {
        return document ? document->Execute(LX::LXSetProperty{node, key, value}, document->Revision()).applied
                        : graph.SetProperty(node, key, value);
    };
    const auto setSocketValue = [&](LX::Id pin, LX::LXSocketValue value) {
        return document ? document->Execute(LX::LXSetSocketValue{pin, std::move(value)}, document->Revision()).applied
                        : graph.SetSocketValue(pin, std::move(value));
    };
    const auto removeNodes = [&](const std::vector<LX::Id>& nodes) {
        return document ? document->Execute(LX::LXRemoveNodes{nodes}, document->Revision()).applied
                        : graph.RemoveNodes(nodes);
    };
    const auto selectedNodes = [&]() {
        return canvas.selectedNodes.empty() && canvas.selectedNode ? std::vector<LX::Id>{canvas.selectedNode}
                                                                   : canvas.selectedNodes;
    };
    const auto createGroupFromSelection = [&]() {
        const std::vector<LX::Id> selected = selectedNodes();
        const LX::Id instance =
            document
                ? document->Execute(LX::LXCollapseToGroup{selected, NextGroupName(graph)}, document->Revision()).created
                : graph.CollapseToGroup(selected, NextGroupName(graph));
        if (!instance)
        {
            canvas.message = "Group creation failed; select editable nodes with an output";
            return;
        }
        canvas.selectedNode = instance;
        canvas.selectedFrame = 0;
        canvas.selectedNodes.clear();
        canvas.pendingPin = 0;
        canvas.creationPin = 0;
        canvas.creationPopupOpen = false;
        canvas.draggingNode = 0;
        canvas.dragStartNodes.clear();
        canvas.dirty = document ? document->Dirty() : true;
        canvas.message = "Group created";
    };
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Lattice", nullptr, flags);
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{9.0f * dpi, 4.0f * dpi});
    ImGui::BeginChild("Toolbar", {0.0f, 32.0f * dpi}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Lattice  /  Material Graph");
    ImGui::SameLine();
    if (page && ImGui::Button("Behavior Tree"))
    {
        *page = ExamplePage::Behavior;
    }
    ImGui::SameLine();
    if (page && ImGui::Button("Animation FSM"))
    {
        *page = ExamplePage::Animation;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        std::string error;
        bool saved = false;
        if (document)
        {
            const LX::LXCommandResult result = document->Execute(LX::LXSave{}, document->Revision());
            saved = result.applied;
            error = result.message.empty() ? result.code : result.message;
        }
        else
        {
            saved = graph.Save(file, &error);
        }
        if (saved)
        {
            canvas.message = "Graph saved";
            canvas.dirty = document ? document->Dirty() : false;
        }
        else
        {
            canvas.message = error.empty() ? "Graph save failed" : error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Window Style"))
    {
        std::string error;
        canvas.message = SaveWindowStyle(file, "MaterialGraph", styles, &error) ? "Window style saved" : error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
    {
        std::string error;
        LX::LXStyleSheet loadedStyles = ExampleStyles();
        const bool styleLoaded = LoadExampleStyles(file, loadedStyles, &error);
        auto loaded = !document && styleLoaded ? LX::LXGraph::Load(file, &error, ExampleRegistry()) : std::nullopt;
        const bool graphLoaded = styleLoaded && (document ? document->Reload(&error) : loaded.has_value());
        if (graphLoaded)
        {
            if (!document)
            {
                graph = std::move(*loaded);
            }
            styles = std::move(loadedStyles);
            canvas.selectedNode = 0;
            canvas.selectedFrame = 0;
            canvas.selectedNodes.clear();
            canvas.pendingPin = 0;
            canvas.creationPin = 0;
            canvas.creationPopupOpen = false;
            canvas.draggingNode = 0;
            canvas.draggingFrame = 0;
            canvas.panning = false;
            canvas.viewApplied = false;
            canvas.dragStartNodes.clear();
            canvas.sliderNode = 0;
            canvas.colorNode = 0;
            canvas.message = error.find("Recovered") != std::string::npos ? error : "Reloaded";
            canvas.dirty = document ? document->Dirty() : false;
        }
        else
        {
            canvas.message = error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo") && undo())
    {
        canvas.selectedNodes.clear();
        if (!graph.FindFrame(canvas.selectedFrame))
        {
            canvas.selectedFrame = 0;
        }
        if (!graph.FindNode(canvas.selectedNode))
        {
            canvas.selectedNode = 0;
        }
        canvas.dirty = document ? document->Dirty() : true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo") && redo())
    {
        canvas.selectedNodes.clear();
        if (!graph.FindFrame(canvas.selectedFrame))
        {
            canvas.selectedFrame = 0;
        }
        if (!graph.FindNode(canvas.selectedNode))
        {
            canvas.selectedNode = 0;
        }
        canvas.dirty = document ? document->Dirty() : true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate"))
    {
        const auto issues =
            document ? document->Execute(LX::LXValidate{}, document->Revision()).issues : graph.Validate();
        canvas.message = issues.empty() ? "No issues" : issues.front().message;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    const float height = ImGui::GetContentRegionAvail().y;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f * dpi, 8.0f * dpi});
    ImGui::BeginChild("Library", {216.0f * dpi, height}, ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Library");
    ImGui::Separator();
    static std::array<char, 96> search{};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##node_search", "Search nodes...", search.data(), search.size());
    if (gLibraryProbe)
    {
        const ImVec2 top = ImGui::GetItemRectMin();
        const ImVec2 bottom = ImGui::GetItemRectMax();
        gLibraryProbe->searchPoint = {(top.x + bottom.x) * 0.5f, (top.y + bottom.y) * 0.5f};
    }
    static const char* names[] = {"Base Color Texture", "Texture Sample",     "Tint Color",    "Multiply",
                                  "Normal Map",         "Principled Surface", "Normal Texture"};
    const auto drawCategory = [&](const char* category, std::initializer_list<int> indices) {
        bool categoryShown = false;
        for (const int index : indices)
        {
            if (!MatchesSearch(names[index], search.data()))
            {
                continue;
            }
            if (!categoryShown)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", category);
                categoryShown = true;
            }
            if (ImGui::Selectable(names[index], false))
            {
                const LX::NodeSpec spec = Spec(index);
                const ImVec2 placement = FindLibraryPlacement(graph, canvas, styles, items, spec);
                canvas.selectedNode = createNode(spec, placement.x, placement.y);
                if (RevealLibraryNode(graph, canvas, styles, items, canvas.selectedNode) && document)
                {
                    const float scale = canvas.zoom * ImGui::GetStyle().FontScaleDpi;
                    const LX::ViewLayout view{true, (canvas.canvasSize.x * 0.5f - canvas.pan.x) / scale,
                                              (canvas.canvasSize.y * 0.5f - canvas.pan.y) / scale, canvas.zoom};
                    document->Execute(LX::LXSetView{view}, document->Revision());
                }
                canvas.selectedFrame = 0;
                canvas.selectedNodes.clear();
                canvas.dirty = true;
            }
            if (gLibraryProbe)
            {
                const ImVec2 top = ImGui::GetItemRectMin();
                const ImVec2 bottom = ImGui::GetItemRectMax();
                gLibraryProbe->results.emplace_back(index,
                                                    ImVec2{(top.x + bottom.x) * 0.5f, (top.y + bottom.y) * 0.5f});
            }
        }
    };
    drawCategory("TEXTURES", {0, 1, 6});
    drawCategory("OPERATIONS", {2, 3, 4});
    drawCategory("OUTPUT", {5});
    bool groupHeading = false;
    for (const auto& [id, group] : graph.Groups())
    {
        if (!MatchesSearch(group.name.c_str(), search.data()))
        {
            continue;
        }
        if (!groupHeading)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("GROUPS");
            groupHeading = true;
        }
        if (ImGui::Selectable(group.name.c_str(), false))
        {
            const float offset = static_cast<float>(graph.Nodes().size() % 5) * 34.0f;
            canvas.selectedNode = document
                                      ? document
                                            ->Execute(LX::LXCreateGroupInstance{id, 150.0f + offset, 100.0f + offset},
                                                      document->Revision())
                                            .created
                                      : graph.CreateGroupInstance(id, 150.0f + offset, 100.0f + offset);
            canvas.selectedFrame = 0;
            canvas.selectedNodes.clear();
            canvas.dirty = canvas.selectedNode != 0;
        }
        if (gLibraryProbe)
        {
            const ImVec2 top = ImGui::GetItemRectMin();
            const ImVec2 bottom = ImGui::GetItemRectMax();
            gLibraryProbe->groups.emplace_back(id, ImVec2{(top.x + bottom.x) * 0.5f, (top.y + bottom.y) * 0.5f});
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Property Editor");
    static LX::Id editingFrame = 0;
    static std::array<char, 256> frameLabel{};
    static float frameSize[2]{};
    if (!graph.FindFrame(canvas.selectedFrame))
    {
        editingFrame = 0;
    }
    if (const LX::Node* node = graph.FindNode(canvas.selectedNode))
    {
        ImGui::TextWrapped("%s", node->title.c_str());
        ImGui::TextDisabled("Type: %s   ID: %llu", node->type.c_str(), static_cast<unsigned long long>(node->id));
        if (node->groupId)
        {
            ImGui::TextDisabled("Group ID: %llu", static_cast<unsigned long long>(node->groupId));
            if (document && groupEditor && ImGui::Button("Edit Group"))
            {
                if (!groupEditor->Open(*document, node->groupId))
                {
                    canvas.message = "Cannot open group";
                }
            }
            if (document && groupEditor && gGroupEditorProbe)
            {
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                gGroupEditorProbe->openPoint = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
            }
        }
        if (!graph.IsNodeEditable(node->id))
        {
            ImGui::TextDisabled("Definition unavailable or incompatible: read-only node data");
        }
        struct TextEditorState
        {
            std::string source;
            std::array<char, 256> value{};
        };
        static std::map<std::pair<LX::Id, std::string>, TextEditorState> textEditors;
        const std::vector<std::string> keys = items.Keys(*node);
        for (const std::string& key : keys)
        {
            ImGui::PushID(key.c_str());
            const auto property = node->properties.find(key);
            const std::string fallback = property == node->properties.end() ? std::string{} : property->second;
            const LX::Pin* valuePin = items.ValuePin(*node, key);
            const std::string textValue = valuePin && std::holds_alternative<std::string>(valuePin->value)
                                              ? std::get<std::string>(valuePin->value)
                                              : fallback;
            TextEditorState& editor = textEditors[{node->id, key}];
            if (editor.source != textValue && !ImGui::IsAnyItemActive())
            {
                editor.source = textValue;
                editor.value.fill(0);
                textValue.copy(editor.value.data(), std::min(textValue.size(), editor.value.size() - 1));
            }
            ImGui::TextUnformatted(key.c_str());
            bool linked = false;
            if (valuePin && valuePin->direction == LX::Direction::Input)
            {
                linked = std::any_of(graph.Links().begin(), graph.Links().end(),
                                     [&](const LX::Link& link) { return link.input == valuePin->id; });
            }
            ImGui::BeginDisabled(linked || !graph.IsNodeEditable(node->id));
            ImGui::SetNextItemWidth(-1);
            bool changed = false;
            if (valuePin && valuePin->type == LX::PinType::Bool)
            {
                bool value = std::get_if<bool>(&valuePin->value) ? std::get<bool>(valuePin->value) : false;
                changed = ImGui::Checkbox("##socket", &value) && setSocketValue(valuePin->id, value);
            }
            else if (valuePin && valuePin->type == LX::PinType::Int)
            {
                std::int64_t value =
                    std::get_if<std::int64_t>(&valuePin->value) ? std::get<std::int64_t>(valuePin->value) : 0;
                changed = ImGui::InputScalar("##socket", ImGuiDataType_S64, &value, nullptr, nullptr, nullptr,
                                             ImGuiInputTextFlags_EnterReturnsTrue) &&
                          setSocketValue(valuePin->id, value);
            }
            else if (valuePin && valuePin->type == LX::PinType::Float)
            {
                double value = std::get_if<double>(&valuePin->value) ? std::get<double>(valuePin->value) : 0.0;
                changed =
                    ImGui::InputDouble("##socket", &value, 0.0, 0.0, "%.6f", ImGuiInputTextFlags_EnterReturnsTrue) &&
                    setSocketValue(valuePin->id, value);
            }
            else if (valuePin && (valuePin->type == LX::PinType::Vector || valuePin->type == LX::PinType::Normal))
            {
                const auto* stored = std::get_if<std::array<double, 3>>(&valuePin->value);
                float value[3] = {stored ? static_cast<float>((*stored)[0]) : 0.0f,
                                  stored ? static_cast<float>((*stored)[1]) : 0.0f,
                                  stored ? static_cast<float>((*stored)[2]) : 0.0f};
                changed = ImGui::InputFloat3("##socket", value, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue) &&
                          setSocketValue(valuePin->id, std::array<double, 3>{value[0], value[1], value[2]});
            }
            else if (valuePin && valuePin->type == LX::PinType::Color)
            {
                const auto* stored = std::get_if<std::array<double, 4>>(&valuePin->value);
                float value[4] = {
                    stored ? static_cast<float>((*stored)[0]) : 1.0f, stored ? static_cast<float>((*stored)[1]) : 1.0f,
                    stored ? static_cast<float>((*stored)[2]) : 1.0f, stored ? static_cast<float>((*stored)[3]) : 1.0f};
                changed = ImGui::ColorEdit4("##socket", value) &&
                          setSocketValue(valuePin->id, std::array<double, 4>{value[0], value[1], value[2], value[3]});
            }
            else if (ImGui::InputText("##property", editor.value.data(), editor.value.size(),
                                      ImGuiInputTextFlags_EnterReturnsTrue))
            {
                changed = valuePin && valuePin->type == LX::PinType::Texture
                              ? setSocketValue(valuePin->id, std::string(editor.value.data()))
                              : setProperty(node->id, key, editor.value.data());
                editor.source = editor.value.data();
            }
            if (gInspectorProbe)
            {
                const ImVec2 top = ImGui::GetItemRectMin();
                const ImVec2 bottom = ImGui::GetItemRectMax();
                gInspectorProbe->controls[key] = {(top.x + bottom.x) * 0.5f, (top.y + bottom.y) * 0.5f};
            }
            if (changed)
            {
                canvas.dirty = true;
            }
            ImGui::EndDisabled();
            if (linked)
            {
                ImGui::TextDisabled("Value comes from the connected input");
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Node Style");
        static bool styleForType = true;
        ImGui::Checkbox("All of this type", &styleForType);
        LX::LXNodeStyle editedStyle = styleForType ? styles.ForNodeType(node->type) : styles.ForNode(*node);
        bool styleChanged = false;
        styleChanged |= StyleColor("Header top", editedStyle.header);
        styleChanged |= StyleColor("Header bottom", editedStyle.headerBottom);
        if (ImGui::TreeNodeEx("More node settings", ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            styleChanged |= StyleColor("Fill", editedStyle.fill);
            styleChanged |= StyleColor("Border", editedStyle.border);
            styleChanged |= StyleColor("Selected border", editedStyle.selectedBorder);
            styleChanged |= StyleColor("Text", editedStyle.text);
            styleChanged |= StyleSlider("Width", "##node_width", &editedStyle.width, 140.0f, 320.0f, dpi);
            styleChanged |= StyleSlider("Min height", "##node_height", &editedStyle.height, 0.0f, 160.0f, dpi);
            styleChanged |= StyleSlider("Row height", "##node_row", &editedStyle.rowHeight, 20.0f, 42.0f, dpi);
            styleChanged |= StyleSlider("Corners", "##node_rounding", &editedStyle.rounding, 0.0f, 18.0f, dpi);
            ImGui::TreePop();
        }
        if (styleChanged)
        {
            if (styleForType)
            {
                styles.SetTypeStyle(node->type, editedStyle);
            }
            else
            {
                styles.SetNodeStyle(node->id, editedStyle);
            }
            canvas.dirty = true;
        }
        if (ImGui::Button("Reset style"))
        {
            if (styleForType)
            {
                styles.ClearTypeStyle(node->type);
            }
            else
            {
                styles.ClearNodeStyle(node->id);
            }
            canvas.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Blender preset"))
        {
            styles = ExampleStyles();
            canvas.dirty = true;
        }

        if (!node->pins.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Pin and Wire Style");
            static int styledPinIndex = 0;
            styledPinIndex = std::clamp(styledPinIndex, 0, static_cast<int>(node->pins.size()) - 1);
            if (ImGui::BeginCombo("Pin", node->pins[styledPinIndex].name.c_str()))
            {
                for (int index = 0; index < static_cast<int>(node->pins.size()); ++index)
                {
                    ImGui::PushID(index);
                    if (ImGui::Selectable(node->pins[index].name.c_str(), index == styledPinIndex))
                    {
                        styledPinIndex = index;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

            const LX::Pin& pin = node->pins[styledPinIndex];
            LX::LXPinStyle pinStyle = styles.ForPin(pin);
            bool pinChanged = StyleColor("Pin color", pinStyle.fill);
            pinChanged |= StyleColor("Pin border", pinStyle.border);
            pinChanged |= StyleSlider("Pin radius", "##pin_radius", &pinStyle.radius, 3.0f, 12.0f, dpi);
            static constexpr const char* shapes[] = {"Circle", "Triangle", "Diamond"};
            int shape = static_cast<int>(pinStyle.shape);
            if (ImGui::Combo("Pin shape", &shape, shapes, 3))
            {
                pinStyle.shape = static_cast<LX::LXPinShape>(shape);
                pinChanged = true;
            }
            if (pinChanged)
            {
                styles.SetPinStyle(pin.id, pinStyle);
                canvas.dirty = true;
            }

            LX::LXWireStyle wireStyle = styles.ForWire({}, pin.type);
            bool wireChanged = StyleColor("Wire color", wireStyle.color);
            wireChanged |= StyleColor("Selected wire", wireStyle.selectedColor);
            wireChanged |= StyleSlider("Wire width", "##wire_width", &wireStyle.thickness, 1.0f, 6.0f, dpi);
            wireChanged |=
                StyleSlider("Selected width", "##wire_selected", &wireStyle.selectedThickness, 1.0f, 7.0f, dpi);
            wireChanged |= StyleSlider("Wire bend", "##wire_bend", &wireStyle.bend, 0.0f, 120.0f, dpi);
            bool straightWire = wireStyle.route == LX::LXWireStyle::Route::Straight;
            if (ImGui::Checkbox("Straight wire", &straightWire))
            {
                wireStyle.route = straightWire ? LX::LXWireStyle::Route::Straight : LX::LXWireStyle::Route::Bezier;
                wireChanged = true;
            }
            if (wireChanged)
            {
                styles.SetWireTypeStyle(pin.type, wireStyle);
                canvas.dirty = true;
            }
            if (ImGui::Button("Reset pin"))
            {
                styles.ClearPinStyle(pin.id);
                canvas.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset wire type"))
            {
                styles.ClearWireTypeStyle(pin.type);
                canvas.dirty = true;
            }
        }

        std::vector<const LX::Link*> connectedLinks;
        for (const LX::Link& link : graph.Links())
        {
            const LX::Pin* output = graph.FindPin(link.output);
            const LX::Pin* input = graph.FindPin(link.input);
            if (output && input && (output->node == node->id || input->node == node->id))
            {
                connectedLinks.push_back(&link);
            }
        }
        if (!connectedLinks.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Connection Style");
            static LX::Id styledLinkId = 0;
            const auto findLink = [&](LX::Id id) {
                return std::find_if(connectedLinks.begin(), connectedLinks.end(),
                                    [&](const LX::Link* link) { return link->id == id; });
            };
            if (findLink(styledLinkId) == connectedLinks.end())
            {
                styledLinkId = connectedLinks.front()->id;
            }
            const LX::Link* selectedLink = *findLink(styledLinkId);
            const auto linkLabel = [&](const LX::Link& link) {
                return graph.FindPin(link.output)->name + " -> " + graph.FindPin(link.input)->name;
            };
            const std::string selectedLabel = linkLabel(*selectedLink);
            if (ImGui::BeginCombo("Connection", selectedLabel.c_str()))
            {
                for (const LX::Link* link : connectedLinks)
                {
                    ImGui::PushID(static_cast<int>(link->id));
                    const std::string label = linkLabel(*link);
                    if (ImGui::Selectable(label.c_str(), link->id == styledLinkId))
                    {
                        styledLinkId = link->id;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            selectedLink = *findLink(styledLinkId);
            const LX::Pin* output = graph.FindPin(selectedLink->output);
            LX::LXWireStyle linkStyle = styles.ForWire(*selectedLink, output->type);
            ImGui::PushID("connection_style");
            bool linkChanged = StyleColor("Color", linkStyle.color);
            linkChanged |= StyleSlider("Width", "##link_width", &linkStyle.thickness, 1.0f, 6.0f, dpi);
            linkChanged |= StyleSlider("Bend", "##link_bend", &linkStyle.bend, 0.0f, 120.0f, dpi);
            bool straightLink = linkStyle.route == LX::LXWireStyle::Route::Straight;
            if (ImGui::Checkbox("Straight link", &straightLink))
            {
                linkStyle.route = straightLink ? LX::LXWireStyle::Route::Straight : LX::LXWireStyle::Route::Bezier;
                linkChanged = true;
            }
            if (linkChanged)
            {
                styles.SetWireStyle(selectedLink->id, linkStyle);
                canvas.dirty = true;
            }
            if (ImGui::Button("Reset connection"))
            {
                styles.ClearWireStyle(selectedLink->id);
                canvas.dirty = true;
            }
            ImGui::PopID();
        }

        DrawDynamicPinControls(graph, canvas, node->id, document);

        if (ImGui::Button("Delete selected"))
        {
            removeNodes(canvas.selectedNodes.empty() ? std::vector<LX::Id>{node->id} : canvas.selectedNodes);
            canvas.selectedNode = 0;
            canvas.selectedNodes.clear();
            canvas.dirty = true;
        }
    }
    else if (const LX::FrameLayout* frame = graph.FindFrame(canvas.selectedFrame))
    {
        if (editingFrame != canvas.selectedFrame)
        {
            editingFrame = canvas.selectedFrame;
            frameLabel.fill(0);
            std::copy_n(frame->label.data(), std::min(frame->label.size(), frameLabel.size() - 1), frameLabel.data());
            frameSize[0] = frame->width;
            frameSize[1] = frame->height;
        }
        ImGui::TextDisabled("Frame ID: %llu", static_cast<unsigned long long>(canvas.selectedFrame));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##frame_label", frameLabel.data(), frameLabel.size());
        if (ImGui::Button("Apply label"))
        {
            const bool changed = document ? document
                                                ->Execute(LX::LXRenameFrame{canvas.selectedFrame, frameLabel.data()},
                                                          document->Revision())
                                                .applied
                                          : graph.RenameFrame(canvas.selectedFrame, frameLabel.data());
            canvas.dirty = canvas.dirty || changed;
            canvas.message = changed ? "Frame label changed" : "Frame label unchanged or invalid";
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputFloat2("##frame_size", frameSize);
        if (ImGui::Button("Apply size"))
        {
            const bool changed =
                document ? document
                               ->Execute(LX::LXResizeFrame{canvas.selectedFrame, frameSize[0], frameSize[1]},
                                         document->Revision())
                               .applied
                         : graph.ResizeFrame(canvas.selectedFrame, frameSize[0], frameSize[1]);
            canvas.dirty = canvas.dirty || changed;
            canvas.message = changed ? "Frame resized" : "Frame size unchanged or invalid";
        }
        if (ImGui::TreeNode("Frame style"))
        {
            canvas.dirty |= ImGui::ColorEdit4("Fill", &styles.frame.fill.x);
            canvas.dirty |= ImGui::ColorEdit4("Header", &styles.frame.header.x);
            canvas.dirty |= ImGui::ColorEdit4("Border", &styles.frame.border.x);
            canvas.dirty |= ImGui::ColorEdit4("Selected", &styles.frame.selectedBorder.x);
            canvas.dirty |= ImGui::ColorEdit4("Text", &styles.frame.text.x);
            ImGui::TreePop();
        }
        if (ImGui::Button("Delete Frame"))
        {
            const bool removed =
                document ? document->Execute(LX::LXRemoveFrame{canvas.selectedFrame}, document->Revision()).applied
                         : graph.RemoveFrame(canvas.selectedFrame);
            if (removed)
            {
                canvas.selectedFrame = 0;
                canvas.dirty = true;
                canvas.message = "Frame deleted";
            }
        }
    }
    else
    {
        ImGui::TextDisabled("Select a node or frame");
    }
    ImGui::Separator();
    if (ImGui::Checkbox("Show grid", &styles.canvas.showGrid))
    {
        canvas.dirty = true;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SameLine(0.0f, 0.0f);

    const float rightWidth = 245.0f * dpi;
    const float canvasWidth = std::max(200.0f * dpi, ImGui::GetContentRegionAvail().x - rightWidth);
    ImGui::BeginChild("Canvas", {canvasWidth, height}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0f * dpi, 3.0f * dpi});
    ImGui::BeginChild("CanvasHeader", {0.0f, 26.0f * dpi}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (ImGui::Button("View"))
    {
        ImGui::OpenPopup("CanvasView");
    }
    if (ImGui::BeginPopup("CanvasView"))
    {
        if (ImGui::MenuItem("Grid", nullptr, styles.canvas.showGrid))
        {
            styles.canvas.showGrid = !styles.canvas.showGrid;
            canvas.dirty = true;
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add"))
    {
        ImGui::OpenPopup("CanvasAdd");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedNodes().empty());
    if (ImGui::Button("Group selection"))
    {
        createGroupFromSelection();
    }
    ImGui::EndDisabled();
    if (gGroupCollapseProbe)
    {
        const ImVec2 top = ImGui::GetItemRectMin();
        const ImVec2 bottom = ImGui::GetItemRectMax();
        gGroupCollapseProbe->buttonPoint = {(top.x + bottom.x) * 0.5f, (top.y + bottom.y) * 0.5f};
    }
    if (ImGui::BeginPopup("CanvasAdd"))
    {
        const std::vector<LX::Id> selected = selectedNodes();
        if (ImGui::MenuItem("Group selection", "Ctrl+G", false, !selected.empty()))
        {
            createGroupFromSelection();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Frame around selection", nullptr, false, !selected.empty()))
        {
            float left = std::numeric_limits<float>::max();
            float top = std::numeric_limits<float>::max();
            float right = std::numeric_limits<float>::lowest();
            float bottom = std::numeric_limits<float>::lowest();
            std::vector<LX::Id> members;
            for (LX::Id id : selected)
            {
                const LX::Node* node = graph.FindNode(id);
                const LX::NodeLayout* layout = graph.FindLayout(id);
                if (!node || !layout)
                {
                    continue;
                }
                const LX::LXNodeGeometry size = LX::MeasureNode(*node, *layout, styles.ForNode(*node), items);
                left = std::min(left, layout->x);
                top = std::min(top, layout->y);
                right = std::max(right, layout->x + size.width);
                bottom = std::max(bottom, layout->y + size.height);
                members.push_back(id);
            }
            if (!members.empty())
            {
                const LX::LXAddFrame request{
                    "Frame", left - 28.0f, top - 48.0f, right - left + 56.0f, bottom - top + 76.0f, members};
                canvas.selectedFrame = document ? document->Execute(request, document->Revision()).created
                                                : graph.AddFrame(request.label, request.x, request.y, request.width,
                                                                 request.height, request.members);
                canvas.selectedNode = 0;
                canvas.selectedNodes.clear();
                canvas.dirty = canvas.selectedFrame != 0;
                canvas.message = canvas.dirty ? "Frame created" : "Frame creation failed";
            }
        }
        ImGui::Separator();
        if (!graph.Groups().empty() && ImGui::BeginMenu("Group"))
        {
            for (const auto& [id, group] : graph.Groups())
            {
                if (ImGui::MenuItem(group.name.c_str()))
                {
                    const float offset = static_cast<float>(graph.Nodes().size() % 5) * 34.0f;
                    canvas.selectedNode =
                        document ? document
                                       ->Execute(LX::LXCreateGroupInstance{id, 150.0f + offset, 100.0f + offset},
                                                 document->Revision())
                                       .created
                                 : graph.CreateGroupInstance(id, 150.0f + offset, 100.0f + offset);
                    canvas.selectedFrame = 0;
                    canvas.selectedNodes.clear();
                    canvas.dirty = canvas.selectedNode != 0;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        for (int index = 0; index < 7; ++index)
        {
            if (ImGui::MenuItem(names[index]))
            {
                const float offset = static_cast<float>(graph.Nodes().size() % 5) * 34.0f;
                canvas.selectedNode = createNode(Spec(index), 150.0f + offset, 100.0f + offset);
                canvas.selectedFrame = 0;
                canvas.selectedNodes.clear();
                canvas.dirty = true;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Material Graph  |  %d%%", static_cast<int>(canvas.zoom * 100.0f));
    ImGui::EndChild();
    ImGui::PopStyleVar();
    LX::DrawCanvas(graph, canvas, styles, items, document);
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::GetIO().WantTextInput &&
        ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G) && !selectedNodes().empty())
    {
        createGroupFromSelection();
    }
    DrawPortCreationPopup(graph, canvas, document);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f * dpi, 8.0f * dpi});
    ImGui::BeginChild("Trace", {0, height}, ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Evaluation Trace");
    ImGui::Separator();
    const auto drawTraceGroup = [&](const char* label, const auto& include) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", label);
        for (const LX::Node& node : graph.Nodes())
        {
            if (!include(node))
            {
                continue;
            }
            ImGui::PushID(static_cast<int>(node.id));
            if (ImGui::Selectable(node.title.c_str(), canvas.selectedNode == node.id))
            {
                canvas.selectedNode = node.id;
                canvas.selectedFrame = 0;
                canvas.selectedNodes.clear();
            }
            ImGui::PopID();
        }
    };
    drawTraceGroup("INPUTS", [](const LX::Node& node) { return node.type == "TEXTURE"; });
    ImGui::Separator();
    drawTraceGroup("OPERATIONS", [](const LX::Node& node) { return node.type != "TEXTURE" && node.type != "OUTPUT"; });
    ImGui::Separator();
    drawTraceGroup("OUTPUT", [](const LX::Node& node) { return node.type == "OUTPUT"; });
    ImGui::Separator();
    ImGui::Text("Nodes %zu  Links %zu", graph.Nodes().size(), graph.Links().size());
    ImGui::Separator();
    DrawProblems(graph, canvas, document);
    if (!canvas.message.empty())
    {
        ImGui::TextWrapped("%s", canvas.message.c_str());
    }
    ImGui::TextWrapped("Material evaluation layout; no shader compile in this example.");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
}

void DrawBehaviorUI(LX::LXGraph& graph, LX::CanvasState& canvas, LX::LXStyleSheet& styles,
                    const LX::LXNodeItemRegistry& items, const std::string& file, ExamplePage* page)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("Lattice Behavior", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{9.0f * dpi, 4.0f * dpi});
    ImGui::BeginChild("BehaviorToolbar", {0.0f, 34.0f * dpi}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Lattice  /  Behavior Tree");
    ImGui::SameLine();
    if (page && ImGui::Button("Material Graph"))
    {
        *page = ExamplePage::Material;
    }
    ImGui::SameLine();
    if (page && ImGui::Button("Animation FSM"))
    {
        *page = ExamplePage::Animation;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Graph"))
    {
        std::string error;
        canvas.message = graph.Save(file, &error) ? "Behavior graph saved" : error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Window Style"))
    {
        std::string error;
        canvas.message = SaveWindowStyle(file, "BehaviorTree", styles, &error) ? "Behavior style saved" : error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
    {
        std::string error;
        auto loaded = LX::LXGraph::Load(file, &error, ExampleRegistry());
        LX::LXStyleSheet loadedStyles = BehaviorStyles();
        if (loaded && LoadExampleStyles(file, loadedStyles, &error, "BehaviorTree"))
        {
            graph = std::move(*loaded);
            styles = std::move(loadedStyles);
            canvas.selectedNode = 0;
            canvas.selectedNodes.clear();
            canvas.pendingPin = 0;
            canvas.creationPin = 0;
            canvas.creationPopupOpen = false;
            canvas.draggingNode = 0;
            canvas.dragStartNodes.clear();
            canvas.message = error.find("Recovered") != std::string::npos ? error : "Behavior graph reloaded";
        }
        else
        {
            canvas.message = error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate"))
    {
        const auto issues = graph.Validate();
        canvas.message = issues.empty() ? "No issues" : issues.front().message;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::BeginChild("BehaviorCanvas", ImGui::GetContentRegionAvail(), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    LX::DrawCanvas(graph, canvas, styles, items);
    ImGui::EndChild();
    ImGui::End();
}

void DrawAnimationUI(LX::LXGraph& graph, LX::CanvasState& canvas, LX::LXStyleSheet& styles, const std::string& file,
                     ExamplePage* page)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("Lattice Animation", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{9.0f * dpi, 4.0f * dpi});
    ImGui::BeginChild("AnimationToolbar", {0.0f, 34.0f * dpi}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Lattice  /  Animation FSM");
    ImGui::SameLine();
    if (page && ImGui::Button("Material Graph"))
    {
        *page = ExamplePage::Material;
    }
    ImGui::SameLine();
    if (page && ImGui::Button("Behavior Tree"))
    {
        *page = ExamplePage::Behavior;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Graph"))
    {
        std::string error;
        canvas.message = graph.Save(file, &error) ? "Animation graph saved" : error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Window Style"))
    {
        std::string error;
        canvas.message = SaveWindowStyle(file, "AnimationFSM", styles, &error) ? "Animation style saved" : error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
    {
        std::string error;
        auto loaded = LX::LXGraph::Load(file, &error, ExampleRegistry());
        LX::LXStyleSheet loadedStyles = AnimationStyles();
        if (loaded && LoadExampleStyles(file, loadedStyles, &error, "AnimationFSM"))
        {
            graph = std::move(*loaded);
            styles = std::move(loadedStyles);
            canvas = {};
            canvas.message = error.find("Recovered") != std::string::npos ? error : "Animation graph reloaded";
        }
        else
        {
            canvas.message = error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate"))
    {
        const auto issues = graph.Validate();
        canvas.message = issues.empty() ? "No issues" : issues.front().message;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::BeginChild("AnimationCanvas", ImGui::GetContentRegionAvail(), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const LX::LXNodeItemRegistry noItems;
    LX::DrawCanvas(graph, canvas, styles, noItems);
    ImGui::EndChild();
    ImGui::End();
}
} // namespace

void DrawProductMaterialPreview()
{
    static LX::LXMaterialAsset asset;
    static std::unique_ptr<LX::LXDocument> document;
    static auto styles = editor::material_editing::BlenderStyles(asset.Definitions());
    static auto items = editor::material_editing::MaterialItems(asset.Definitions());
    static LX::CanvasState canvas;
    static std::string materialName = "Material";
    static std::string nodeSearch;
    if (!document)
    {
        const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 260.0f, 160.0f);
        asset.activeOutput = asset.CreateNode("ShaderNodeOutputMaterial", 790.0f, 160.0f);
        const auto* source = asset.graph.FindNode(surface);
        const auto* target = asset.graph.FindNode(asset.activeOutput);
        asset.graph.Connect(source->pins.back().id, target->pins.front().id);
        document = std::make_unique<LX::LXDocument>(asset.graph);
    }
    ImGui::SetNextWindowPos({0.0f, 0.0f});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    {
        const editor::material_editing::MaterialHeaderScope header(styles.canvas.background);
        ImGui::Begin("Material Node Editor", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_MenuBar);
        if (ImGui::BeginMenuBar())
        {
            editor::material_editing::DrawMaterialContextSelector();
            for (const auto* menu : {"View", "Select", "Add", "Node"})
            {
                if (ImGui::BeginMenu(menu))
                {
                    if (std::string_view(menu) == "Add")
                    {
                        if (const auto type =
                                editor::material_editing::DrawMaterialNodeAddMenu(asset.Definitions(), nodeSearch))
                        {
                            const float scale = canvas.zoom * std::max(0.1f, canvas.viewDpi);
                            document->Execute(LX::LXCreateNode{*type,
                                                               (canvas.canvasSize.x * 0.5f - canvas.pan.x) / scale,
                                                               (canvas.canvasSize.y * 0.5f - canvas.pan.y) / scale},
                                              document->Revision());
                        }
                    }
                    else
                    {
                        ImGui::MenuItem("Material preview", nullptr, false, false);
                    }
                    ImGui::EndMenu();
                }
            }
            const auto action = editor::material_editing::DrawMaterialDataBar(
                materialName, false, true, styles.canvas.showGrid, canvas.snapToGrid,
                [] { ImGui::MenuItem("Material", nullptr, true); }, {});
            if (action == editor::material_editing::MaterialBarAction::ToggleSnap)
            {
                canvas.snapToGrid = !canvas.snapToGrid;
            }
            else if (action == editor::material_editing::MaterialBarAction::ToggleGrid)
            {
                styles.canvas.showGrid = !styles.canvas.showGrid;
            }
            ImGui::EndMenuBar();
        }
    }
    const auto origin = ImGui::GetCursorScreenPos();
    LX::DrawCanvas(document->GraphForCanvas(), canvas, styles, items, document.get());
    editor::material_editing::DrawMaterialBreadcrumb(origin, canvas.canvasSize, "Cube", "Cube", materialName);
    ImGui::End();
}

int wmain(int argc, wchar_t** argv)
{
#ifdef _DEBUG
    // Automated example checks must report assertions to stderr without a desktop dialog.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::vector<std::string> arguments;
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index)
    {
        const int bytes =
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1, nullptr, 0, nullptr, nullptr);
        if (bytes <= 0)
        {
            std::cerr << "Cannot decode command line path" << std::endl;
            return 4;
        }
        std::string value(static_cast<std::size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1, value.data(), bytes, nullptr, nullptr);
        value.pop_back();
        arguments.push_back(std::move(value));
    }
    if (argc > 1 && arguments[1] == "--self-test")
    {
        const bool passed = SelfTest();
        std::cout << (passed ? "LX_SELF_TEST_OK" : "LX_SELF_TEST_FAILED") << std::endl;
        return passed ? 0 : 1;
    }
    const bool behaviorCapture = argc > 1 && arguments[1] == "--capture-bt";
    const bool materialCapture = argc > 1 && arguments[1] == "--capture-material";
    const bool animationCapture = argc > 1 && arguments[1] == "--capture-fsm";
    const bool collapsedCapture = argc > 1 && arguments[1] == "--capture-collapsed";
    const bool frameCapture = argc > 1 && arguments[1] == "--capture-frame";
    const bool groupCapture = argc > 1 && arguments[1] == "--capture-group";
    const bool groupEditorCapture = argc > 1 && arguments[1] == "--capture-group-editor";
    const bool capture = materialCapture || behaviorCapture || animationCapture || collapsedCapture || frameCapture ||
                         groupCapture || groupEditorCapture || (argc > 1 && arguments[1] == "--capture");
    if (capture && argc < 3)
    {
        std::cerr
            << "Usage: LatticeExample --capture[-bt|-fsm|-collapsed|-frame|-group|-group-editor] output.png [graph.lxg]"
            << std::endl;
        return 4;
    }
    const std::string capturePath = capture ? arguments[2] : "";
    std::string file;
    if (materialCapture || behaviorCapture || animationCapture)
    {
        file = "LatticeExample.lxg";
    }
    else if (capture)
    {
        file = argc > 3 ? arguments[3] : "LatticeExample.lxg";
    }
    else
    {
        file = argc > 1 ? arguments[1] : DefaultExamplePath("LatticeExample.lxg");
    }
    std::string behaviorFile = capture ? "LatticeBehavior.lxg" : DefaultExamplePath("LatticeBehavior.lxg");
    if (behaviorCapture && argc > 3)
    {
        behaviorFile = arguments[3];
    }
    std::string animationFile = capture ? "LatticeAnimationFSM.lxg" : DefaultExamplePath("LatticeAnimationFSM.lxg");
    if (animationCapture && argc > 3)
    {
        animationFile = arguments[3];
    }
    std::string error;
    auto loaded = LX::LXGraph::Load(file, &error, ExampleRegistry());
    if (std::filesystem::exists(Utf8Path(file)) && !loaded && !behaviorCapture && !animationCapture)
    {
        std::cerr << "Cannot open graph: " << error << std::endl;
        return 5;
    }
    LX::LXGraph initialGraph = loaded ? std::move(*loaded) : Fixture();
    LX::Id capturedGroup = 0;
    if (groupCapture || groupEditorCapture)
    {
        capturedGroup = AddExampleGroup(initialGraph, 690.0f, 570.0f);
        if (!capturedGroup)
        {
            std::cerr << "Cannot create group fixture" << std::endl;
            return 5;
        }
    }
    if (frameCapture && initialGraph.Nodes().size() >= 4)
    {
        initialGraph.AddFrame("Color processing", 240.0f, 190.0f, 470.0f, 220.0f,
                              {initialGraph.Nodes()[2].id, initialGraph.Nodes()[3].id});
    }
    if (collapsedCapture)
    {
        const auto normal = std::find_if(initialGraph.Nodes().begin(), initialGraph.Nodes().end(),
                                         [](const LX::Node& node) { return node.type == "NORMAL"; });
        if (normal != initialGraph.Nodes().end())
        {
            initialGraph.SetNodeCollapsed(normal->id, true);
        }
    }
    LX::LXDocument materialDocument(std::move(initialGraph), file,
                                    loaded.has_value() && !collapsedCapture && !frameCapture && !groupCapture &&
                                        !groupEditorCapture);
    LX::LXGraph& graph = materialDocument.GraphForCanvas();
    LX::CanvasState canvas;
    canvas.dirty = materialDocument.Dirty();
    LX::LXStyleSheet styles = ExampleStyles();
    const LX::LXNodeItemRegistry items = ExampleItems();
    canvas.message = error.find("Recovered") != std::string::npos ? error
                     : loaded                                     ? "Document opened"
                                                                  : "Example fixture";
    if (!graph.Nodes().empty())
    {
        canvas.selectedNode = graph.Nodes()[std::min<std::size_t>(3, graph.Nodes().size() - 1)].id;
    }
    if (frameCapture && !graph.Layout().frames.empty())
    {
        canvas.selectedNode = 0;
        canvas.selectedFrame = graph.Layout().frames.begin()->first;
    }
    if (groupCapture || groupEditorCapture)
    {
        canvas.selectedNode = capturedGroup;
    }
    GroupEditorState groupEditor;
    if (groupEditorCapture && !groupEditor.Open(materialDocument, graph.FindNode(capturedGroup)->groupId))
    {
        std::cerr << "Cannot open group editor fixture" << std::endl;
        return 5;
    }
    if (!LoadExampleStyles(file, styles, &error))
    {
        std::cerr << "Cannot open window style: " << error << std::endl;
        return 6;
    }
    if (error.find("Recovered") != std::string::npos)
    {
        canvas.message = error;
    }
    auto behaviorLoaded = LX::LXGraph::Load(behaviorFile, &error, ExampleRegistry());
    if (std::filesystem::exists(Utf8Path(behaviorFile)) && !behaviorLoaded)
    {
        std::cerr << "Cannot open behavior graph: " << error << std::endl;
        return 5;
    }
    LX::LXGraph behaviorGraph = behaviorLoaded ? std::move(*behaviorLoaded) : BehaviorFixture();
    LX::CanvasState behaviorCanvas;
    behaviorCanvas.pan = {40.0f, 45.0f};
    LX::LXStyleSheet behaviorStyles = BehaviorStyles();
    const LX::LXNodeItemRegistry behaviorItems = BehaviorItems();
    if (!LoadExampleStyles(behaviorFile, behaviorStyles, &error, "BehaviorTree"))
    {
        std::cerr << "Cannot open behavior window style: " << error << std::endl;
        return 6;
    }
    if (error.find("Recovered") != std::string::npos)
    {
        behaviorCanvas.message = error;
    }
    auto animationLoaded = LX::LXGraph::Load(animationFile, &error, ExampleRegistry());
    if (std::filesystem::exists(Utf8Path(animationFile)) && !animationLoaded)
    {
        std::cerr << "Cannot open animation graph: " << error << std::endl;
        return 5;
    }
    LX::LXGraph animationGraph = animationLoaded ? std::move(*animationLoaded) : AnimationFixture();
    LX::CanvasState animationCanvas;
    animationCanvas.pan = {90.0f, 160.0f};
    LX::LXStyleSheet animationStyles = AnimationStyles();
    if (!LoadExampleStyles(animationFile, animationStyles, &error, "AnimationFSM"))
    {
        std::cerr << "Cannot open animation window style: " << error << std::endl;
        return 6;
    }
    if (error.find("Recovered") != std::string::npos)
    {
        animationCanvas.message = error;
    }
    ExamplePage page = behaviorCapture    ? ExamplePage::Behavior
                       : animationCapture ? ExamplePage::Animation
                                          : ExamplePage::Material;

    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW),       CS_CLASSDC, WindowProc, 0,       0,
                            GetModuleHandleW(nullptr), nullptr,    nullptr,    nullptr, nullptr,
                            L"LatticeExample",         nullptr};
    RegisterClassExW(&windowClass);
    const DWORD windowStyle = capture ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    HWND window =
        CreateWindowW(windowClass.lpszClassName, L"Lattice Example", windowStyle, 100, 100, capture ? 1440 : 1600,
                      capture ? 840 : 900, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window || !CreateDevice(window))
    {
        ReleaseDevice();
        return 2;
    }
    if (!capture)
    {
        ShowWindow(window, SW_SHOWDEFAULT);
        UpdateWindow(window);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ApplyExampleTheme();
    LoadExampleFont();
    if (materialCapture)
    {
        if (!LoadMaterialPreviewFont())
        {
            ImGui::DestroyContext();
            ReleaseDevice();
            DestroyWindow(window);
            UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
            return 6;
        }
    }
    // Win32 coordinates and ImGui style must use the same monitor scale.
    const float dpi = capture ? 1.0f : ImGui_ImplWin32_GetDpiScaleForHwnd(window);
    ImGui::GetStyle().FontScaleDpi = dpi;
    ImGui::GetStyle().ScaleAllSizes(dpi);
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(gDevice, gContext);

    bool done = false;
    int exitCode = 0;
    while (!done)
    {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT)
            {
                done = true;
            }
        }
        if (done)
        {
            break;
        }
        if (gResizeWidth && gResizeHeight)
        {
            ReleaseTarget();
            gSwapChain->ResizeBuffers(0, gResizeWidth, gResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            gResizeWidth = gResizeHeight = 0;
            CreateTarget();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        if (materialCapture)
        {
            DrawProductMaterialPreview();
        }
        else if (groupEditor.draft)
        {
            DrawGroupEditor(groupEditor, materialDocument, styles, items);
        }
        else if (page == ExamplePage::Behavior)
        {
            DrawBehaviorUI(behaviorGraph, behaviorCanvas, behaviorStyles, behaviorItems, behaviorFile,
                           capture ? nullptr : &page);
        }
        else if (page == ExamplePage::Animation)
        {
            DrawAnimationUI(animationGraph, animationCanvas, animationStyles, animationFile, capture ? nullptr : &page);
        }
        else
        {
            DrawUI(graph, canvas, styles, items, file, capture ? nullptr : &page, &materialDocument, &groupEditor);
        }
        ImGui::Render();
        const float clear[] = {0.11f, 0.11f, 0.11f, 1.0f};
        gContext->OMSetRenderTargets(1, &gTarget, nullptr);
        gContext->ClearRenderTargetView(gTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (capture)
        {
            if (!CaptureBackBuffer(capturePath))
            {
                std::cerr << "Failed to capture ImGui back buffer" << std::endl;
                exitCode = 3;
            }
            done = true;
        }
        else
        {
            gSwapChain->Present(1, 0);
        }
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ReleaseDevice();
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    return exitCode;
}
