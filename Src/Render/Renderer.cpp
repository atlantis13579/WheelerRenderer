#include "Renderer.h"
#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <wincodec.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace Wheeler
{
    void Check(HRESULT hr, const char* operation)
    {
        if (FAILED(hr))
        {
            std::ostringstream text;
            text << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned>(hr) << ")";
            throw std::runtime_error(text.str());
        }
    }

    std::filesystem::path ExecutableDirectory()
    {
        wchar_t path[32768] = {};
        GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
        return std::filesystem::path(path).parent_path();
    }

    namespace
    {
        std::string Resource(unsigned id)
        {
            HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
            if (!resource) throw std::runtime_error("Embedded shader resource is missing");
            HGLOBAL data = LoadResource(nullptr, resource);
            return std::string(static_cast<const char*>(LockResource(data)), SizeofResource(nullptr, resource));
        }

        class ShaderIncludes : public ID3DInclude
        {
        public:
            HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* size) override
            {
                const std::string filename = std::filesystem::path(name).filename().string();
                unsigned id = filename == "Common.hlsli" ? 101 : filename == "GeodesicShared.h" ? 104 : 0;
                if (!id) return E_FAIL;
                std::string text = Resource(id);
                char* copy = new char[text.size()];
                memcpy(copy, text.data(), text.size());
                *data = copy; *size = static_cast<UINT>(text.size());
                return S_OK;
            }
            HRESULT __stdcall Close(LPCVOID data) override { delete[] static_cast<const char*>(data); return S_OK; }
        };

        ComPtr<ID3DBlob> Compile(unsigned resource, const char* entry, const char* profile, bool fromDisk)
        {
            ComPtr<ID3DBlob> bytecode, errors;
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
            HRESULT hr;
            if (fromDisk)
            {
                const auto path = ExecutableDirectory() / L"../../../Src/Shaders" /
                    (resource == 101 ? L"Common.hlsli" : resource == 102 ? L"BlackHole.hlsl" : L"Post.hlsl");
                hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, profile, flags, 0, &bytecode, &errors);
            }
            else
            {
                const std::string source = Resource(resource);
                ShaderIncludes includes;
                hr = D3DCompile(source.data(), source.size(), "Wheeler", nullptr, &includes, entry, profile, flags, 0, &bytecode, &errors);
            }
            if (FAILED(hr)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Shader compilation failed");
            return bytecode;
        }

        float Halton(unsigned index, unsigned base)
        {
            float result = 0, fraction = 1;
            while (index) { fraction /= base; result += fraction * (index % base); index /= base; }
            return result;
        }
    }

    void Renderer::Initialize(HWND window, unsigned width, unsigned height, bool warp, bool debug)
    {
        m_Width = width; m_Height = height;
        DXGI_SWAP_CHAIN_DESC swap = {};
        swap.BufferCount = 2;
        swap.BufferDesc.Width = width; swap.BufferDesc.Height = height;
        swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap.OutputWindow = window;
        swap.SampleDesc.Count = 1;
        swap.Windowed = TRUE;
        swap.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | (debug ? D3D11_CREATE_DEVICE_DEBUG : 0);
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
            nullptr, flags, levels, 1, D3D11_SDK_VERSION, &swap, &m_Swapchain, &m_Device, nullptr, &m_Context);
        if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING && debug)
        {
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDeviceAndSwapChain(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
                nullptr, flags, levels, 1, D3D11_SDK_VERSION, &swap, &m_Swapchain, &m_Device, nullptr, &m_Context);
        }
        Check(hr, "D3D11CreateDeviceAndSwapChain");
        m_DebugLayer = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;
        ComPtr<IDXGIDevice> dxgiDevice; Check(m_Device.As(&dxgiDevice), "Query DXGI device");
        ComPtr<IDXGIAdapter> adapter; Check(dxgiDevice->GetAdapter(&adapter), "Get adapter");
        DXGI_ADAPTER_DESC description = {}; adapter->GetDesc(&description);
        char name[256] = {}; WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name, sizeof(name), nullptr, nullptr);
        m_AdapterName = name;
        ComPtr<IDXGIFactory> factory; Check(adapter->GetParent(IID_PPV_ARGS(&factory)), "Get DXGI factory");
        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        CreateBackbuffer();
        D3D11_BUFFER_DESC buffer = {};
        buffer.ByteWidth = sizeof(Constants); buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER; buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        Check(m_Device->CreateBuffer(&buffer, nullptr, &m_ConstantBuffer), "Create constants");
        D3D11_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        Check(m_Device->CreateSamplerState(&sampler, &m_Sampler), "Create sampler");
        D3D11_RASTERIZER_DESC raster = {};
        raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
        Check(m_Device->CreateRasterizerState(&raster, &m_Rasterizer), "Create rasterizer");
        D3D11_DEPTH_STENCIL_DESC depth = {};
        Check(m_Device->CreateDepthStencilState(&depth, &m_DepthState), "Create depth state");
        ReloadShaders();
        for (Timing& timing : m_Timings)
        {
            D3D11_QUERY_DESC query = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
            Check(m_Device->CreateQuery(&query, &timing.disjoint), "Create GPU timer");
            query.Query = D3D11_QUERY_TIMESTAMP;
            Check(m_Device->CreateQuery(&query, &timing.start), "Create timestamp");
            Check(m_Device->CreateQuery(&query, &timing.end), "Create timestamp");
        }
    }

    void Renderer::ReloadShaders()
    {
        // Startup uses embedded sources; F5 reloads the checkout when available.
        const bool disk = m_FullscreenVS && std::filesystem::exists(ExecutableDirectory() / L"../../../Src/Shaders/BlackHole.hlsl");
        const auto vs = Compile(101, "FullscreenVS", "vs_5_0", disk);
        const auto ray = Compile(102, "BlackHolePS", "ps_5_0", disk);
        const auto accumulate = Compile(103, "AccumulatePS", "ps_5_0", disk);
        const auto downsample = Compile(103, "DownsamplePS", "ps_5_0", disk);
        const auto composite = Compile(103, "CompositePS", "ps_5_0", disk);
        ComPtr<ID3D11VertexShader> newVS;
        ComPtr<ID3D11PixelShader> newRay, newAccumulate, newDownsample, newComposite;
        Check(m_Device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &newVS), "Create VS");
        Check(m_Device->CreatePixelShader(ray->GetBufferPointer(), ray->GetBufferSize(), nullptr, &newRay), "Create ray shader");
        Check(m_Device->CreatePixelShader(accumulate->GetBufferPointer(), accumulate->GetBufferSize(), nullptr, &newAccumulate), "Create accumulation shader");
        Check(m_Device->CreatePixelShader(downsample->GetBufferPointer(), downsample->GetBufferSize(), nullptr, &newDownsample), "Create bloom shader");
        Check(m_Device->CreatePixelShader(composite->GetBufferPointer(), composite->GetBufferSize(), nullptr, &newComposite), "Create composite shader");
        m_FullscreenVS = newVS; m_BlackHolePS = newRay; m_AccumulatePS = newAccumulate;
        m_DownsamplePS = newDownsample; m_CompositePS = newComposite;
        m_HistoryFrames = 0;
    }

    void Renderer::CreateBackbuffer()
    {
        ComPtr<ID3D11Texture2D> buffer;
        Check(m_Swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "Get backbuffer");
        Check(m_Device->CreateRenderTargetView(buffer.Get(), nullptr, &m_Backbuffer), "Create backbuffer view");
    }

    void Renderer::Resize(unsigned width, unsigned height)
    {
        if (!width || !height || (width == m_Width && height == m_Height)) return;
        m_Context->OMSetRenderTargets(0, nullptr, nullptr);
        m_Backbuffer.Reset();
        Check(m_Swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "Resize swapchain");
        m_Width = width; m_Height = height; m_HistoryFrames = 0;
        CreateBackbuffer();
    }

    Renderer::Target Renderer::CreateTarget(unsigned width, unsigned height)
    {
        Target target; target.width = width; target.height = height;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        Check(m_Device->CreateTexture2D(&desc, nullptr, &target.texture), "Create HDR target");
        Check(m_Device->CreateRenderTargetView(target.texture.Get(), nullptr, &target.rtv), "Create HDR RTV");
        Check(m_Device->CreateShaderResourceView(target.texture.Get(), nullptr, &target.srv), "Create HDR SRV");
        return target;
    }

    void Renderer::CreateSceneTargets(unsigned width, unsigned height)
    {
        if (width == m_RenderWidth && height == m_RenderHeight) return;
        ID3D11ShaderResourceView* empty[8] = {};
        m_Context->PSSetShaderResources(0, 8, empty);
        m_Context->OMSetRenderTargets(0, nullptr, nullptr);
        m_Scene = CreateTarget(width, height);
        for (Target& target : m_History)
        {
            target = CreateTarget(width, height);
            const float clear[4] = {}; m_Context->ClearRenderTargetView(target.rtv.Get(), clear);
        }
        for (Target& target : m_Bloom)
        {
            width = std::max(1u, width / 2); height = std::max(1u, height / 2);
            target = CreateTarget(width, height);
        }
        m_RenderWidth = m_Scene.width; m_RenderHeight = m_Scene.height; m_HistoryFrames = 0;
    }

    void Renderer::UploadConstants()
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        Check(m_Context->Map(m_ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map constants");
        memcpy(mapped.pData, &m_Constants, sizeof(m_Constants));
        m_Context->Unmap(m_ConstantBuffer.Get(), 0);
    }

    void Renderer::Draw(ID3D11RenderTargetView* target, unsigned width, unsigned height, ID3D11PixelShader* shader,
                        ID3D11ShaderResourceView* const* resources, unsigned resourceCount)
    {
        ID3D11ShaderResourceView* empty[8] = {};
        m_Context->PSSetShaderResources(0, 8, empty);
        m_Context->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
        m_Context->RSSetViewports(1, &viewport);
        if (resourceCount) m_Context->PSSetShaderResources(0, resourceCount, resources);
        m_Context->PSSetShader(shader, nullptr, 0);
        UploadConstants();
        m_Context->Draw(3, 0);
    }

    void Renderer::Render(const Camera& camera, const Settings& s, float time, unsigned sceneWidth, bool resetHistory)
    {
        CreateSceneTargets(std::max(8u, static_cast<unsigned>(sceneWidth * s.renderScale)),
                           std::max(8u, static_cast<unsigned>(m_Height * s.renderScale)));
        if (resetHistory || !s.temporalAA) m_HistoryFrames = 0;
        for (Timing& timer : m_Timings)
        {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data = {};
            UINT64 start = 0, end = 0;
            if (timer.pending && m_Context->GetData(timer.disjoint.Get(), &data, sizeof(data), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
                && m_Context->GetData(timer.start.Get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
                && m_Context->GetData(timer.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
            {
                if (!data.Disjoint) m_GpuMilliseconds = static_cast<float>(1000.0 * (end - start) / data.Frequency);
                timer.pending = false;
            }
        }
        Timing& timer = m_Timings[m_Frame % m_Timings.size()];
        const bool measure = !timer.pending;
        if (measure) { m_Context->Begin(timer.disjoint.Get()); m_Context->End(timer.start.Get()); }

        const Vec3 eye = camera.Eye(), forward = camera.Forward(), right = camera.Right(), up = camera.Up();
        const float tilt = s.diskTilt * Pi / 180.0f;
        m_Constants.eyeTime = { eye.x, eye.y, eye.z, time };
        m_Constants.forwardFov = { forward.x, forward.y, forward.z, std::tan(camera.fov * Pi / 360.0f) };
        m_Constants.rightAspect = { right.x, right.y, right.z, static_cast<float>(sceneWidth) / m_Height };
        m_Constants.upExposure = { up.x, up.y, up.z, s.exposure };
        m_Constants.resolution = { static_cast<float>(m_RenderWidth), static_cast<float>(m_RenderHeight), 1.0f / m_RenderWidth, 1.0f / m_RenderHeight };
        m_Constants.diskGeometry = { s.diskInner, s.diskOuter, s.diskTemperature, s.diskBrightness };
        m_Constants.diskAppearance = { s.diskOpacity, 0, s.diskTurbulence, s.diskRotation };
        m_Constants.relativity = { s.doppler, s.redshift, s.disk ? 1.0f : 0.0f, s.lensing ? 1.0f : 0.0f };
        m_Constants.background = { s.starBrightness, s.nebulaBrightness, s.vignette, 0 };
        m_Constants.post = { s.debugView ? 0.0f : s.bloom, s.bloomThreshold, s.bloomRadius, s.debugView ? 0.0f : s.streak };
        m_Constants.integration = { s.StepAngle(), static_cast<float>(s.MaxSteps()), static_cast<float>(s.debugView), 0 };
        const unsigned samples = s.paused ? 32 : 8;
        m_Constants.sampling = { m_HistoryFrames ? Halton(m_Frame % 32 + 1, 2) - 0.5f : 0.0f,
                                m_HistoryFrames ? Halton(m_Frame % 32 + 1, 3) - 0.5f : 0.0f,
                                1.0f / std::min(m_HistoryFrames + 1, samples), 0 };
        m_Constants.diskNormal = { 0, std::cos(tilt), std::sin(tilt), 0 };

        m_Context->IASetInputLayout(nullptr);
        m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_Context->VSSetShader(m_FullscreenVS.Get(), nullptr, 0);
        m_Context->GSSetShader(nullptr, nullptr, 0);
        m_Context->RSSetState(m_Rasterizer.Get());
        m_Context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        m_Context->OMSetDepthStencilState(m_DepthState.Get(), 0);
        m_Context->PSSetConstantBuffers(0, 1, m_ConstantBuffer.GetAddressOf());
        m_Context->PSSetSamplers(0, 1, m_Sampler.GetAddressOf());
        Draw(m_Scene.rtv.Get(), m_RenderWidth, m_RenderHeight, m_BlackHolePS.Get());
        const unsigned next = 1 - m_HistoryIndex;
        ID3D11ShaderResourceView* accumulation[] = { m_Scene.srv.Get(), m_History[m_HistoryIndex].srv.Get() };
        Draw(m_History[next].rtv.Get(), m_RenderWidth, m_RenderHeight, m_AccumulatePS.Get(), accumulation, 2);
        m_HistoryIndex = next;
        Target* source = &m_History[m_HistoryIndex];
        for (unsigned level = 0; level < m_Bloom.size(); ++level)
        {
            Target& bloom = m_Bloom[level];
            m_Constants.pass = { 1.0f / source->width, 1.0f / source->height, level == 0 ? 1.0f : 0.0f, 0 };
            Draw(bloom.rtv.Get(), bloom.width, bloom.height, m_DownsamplePS.Get(), source->srv.GetAddressOf(), 1);
            source = &bloom;
        }
        const float clear[4] = { 0.023f, 0.028f, 0.039f, 1 };
        m_Context->ClearRenderTargetView(m_Backbuffer.Get(), clear);
        ID3D11ShaderResourceView* composite[] = { m_History[next].srv.Get(), nullptr, m_Bloom[0].srv.Get(), m_Bloom[1].srv.Get(),
                                                m_Bloom[2].srv.Get(), m_Bloom[3].srv.Get(), m_Bloom[4].srv.Get() };
        Draw(m_Backbuffer.Get(), sceneWidth, m_Height, m_CompositePS.Get(), composite, 7);
        ID3D11ShaderResourceView* empty[8] = {}; m_Context->PSSetShaderResources(0, 8, empty);
        if (measure) { m_Context->End(timer.end.Get()); m_Context->End(timer.disjoint.Get()); timer.pending = true; }
        ++m_Frame; m_HistoryFrames = std::min(m_HistoryFrames + 1, 1024u);
    }

    void Renderer::Present(bool vsync) { Check(m_Swapchain->Present(vsync ? 1 : 0, 0), "Present"); }

    void Renderer::Capture(const std::filesystem::path& path)
    {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        ComPtr<ID3D11Texture2D> backbuffer;
        Check(m_Swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)), "Get capture buffer");
        D3D11_TEXTURE2D_DESC desc = {}; backbuffer->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging; Check(m_Device->CreateTexture2D(&desc, nullptr, &staging), "Create capture staging");
        m_Context->CopyResource(staging.Get(), backbuffer.Get());
        ComPtr<IWICImagingFactory> factory;
        Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "Create WIC factory");
        ComPtr<IWICStream> stream; Check(factory->CreateStream(&stream), "Create PNG stream");
        Check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "Open PNG");
        ComPtr<IWICBitmapEncoder> encoder; Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "Create PNG encoder");
        Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize PNG encoder");
        ComPtr<IWICBitmapFrameEncode> frame; Check(encoder->CreateNewFrame(&frame, nullptr), "Create PNG frame");
        Check(frame->Initialize(nullptr), "Initialize PNG frame"); Check(frame->SetSize(desc.Width, desc.Height), "Set PNG size");
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        Check(frame->SetPixelFormat(&format), "Set PNG format");
        if (!IsEqualGUID(format, GUID_WICPixelFormat24bppBGR)) throw std::runtime_error("WIC did not accept BGR");
        std::vector<BYTE> pixels(static_cast<size_t>(desc.Width) * desc.Height * 3);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        Check(m_Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read capture");
        for (unsigned y = 0; y < desc.Height; ++y)
        {
            const BYTE* source = static_cast<const BYTE*>(mapped.pData) + y * mapped.RowPitch;
            BYTE* destination = pixels.data() + static_cast<size_t>(y) * desc.Width * 3;
            for (unsigned x = 0; x < desc.Width; ++x)
            {
                destination[x * 3] = source[x * 4 + 2];
                destination[x * 3 + 1] = source[x * 4 + 1];
                destination[x * 3 + 2] = source[x * 4];
            }
        }
        m_Context->Unmap(staging.Get(), 0);
        const HRESULT write = frame->WritePixels(desc.Height, desc.Width * 3, static_cast<UINT>(pixels.size()), pixels.data());
        Check(write, "Write PNG pixels"); Check(frame->Commit(), "Commit PNG frame"); Check(encoder->Commit(), "Commit PNG");
    }

    void Renderer::ValidateHDR()
    {
        D3D11_TEXTURE2D_DESC desc = {}; m_Scene.texture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging; Check(m_Device->CreateTexture2D(&desc, nullptr, &staging), "Create HDR readback");
        m_Context->CopyResource(staging.Get(), m_Scene.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {}; Check(m_Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read HDR");
        bool invalid = false;
        for (unsigned y = 0; y < desc.Height; ++y)
        {
            const auto* row = reinterpret_cast<const unsigned short*>(static_cast<const BYTE*>(mapped.pData) + y * mapped.RowPitch);
            for (unsigned x = 0; x < desc.Width; ++x)
            {
                invalid |= row[x * 4 + 3] != 0x3c00;
                for (unsigned c = 0; c < 3; ++c)
                {
                    const unsigned short half = row[x * 4 + c];
                    invalid |= (half & 0x7c00) == 0x7c00 || (half & 0x8000) != 0;
                }
            }
        }
        m_Context->Unmap(staging.Get(), 0);
        // An observer close to the horizon can legitimately see an entirely black frame.
        if (invalid) throw std::runtime_error("HDR validation failed: nonfinite/negative radiance or invalid coverage");
    }

    unsigned Renderer::DebugWarnings()
    {
        ComPtr<ID3D11InfoQueue> queue;
        if (FAILED(m_Device.As(&queue))) return 0;
        unsigned count = 0;
        for (UINT64 i = 0; i < queue->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0; queue->GetMessage(i, nullptr, &size);
            std::vector<BYTE> storage(size);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            queue->GetMessage(i, message, &size);
            if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
            {
                OutputDebugStringA(message->pDescription);
                std::ofstream log(ExecutableDirectory() / "d3d11-validation.log", std::ios::app);
                log << message->pDescription << '\n'; ++count;
            }
        }
        queue->ClearStoredMessages();
        return count;
    }
}
