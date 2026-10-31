#pragma once

#include "../App/Camera.h"
#include "../App/Settings.h"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <string>

namespace Wheeler
{
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    class Renderer
    {
    public:
        void Initialize(HWND window, unsigned width, unsigned height, bool warp, bool debug);
        void Resize(unsigned width, unsigned height);
        void Render(const Camera& camera, const Settings& settings, float time, unsigned sceneWidth, bool resetHistory);
        void Present(bool vsync);
        void ReloadShaders();
        void Capture(const std::filesystem::path& path);
        void ValidateHDR();
        unsigned DebugWarnings();
        ID3D11Device* Device() const { return m_Device.Get(); }
        ID3D11DeviceContext* Context() const { return m_Context.Get(); }
        float GpuMilliseconds() const { return m_GpuMilliseconds; }
        unsigned RenderWidth() const { return m_RenderWidth; }
        unsigned RenderHeight() const { return m_RenderHeight; }
        unsigned AccumulatedFrames() const { return m_HistoryFrames; }
        const std::string& AdapterName() const { return m_AdapterName; }
        bool HasDebugLayer() const { return m_DebugLayer; }

    private:
        struct Target
        {
            ComPtr<ID3D11Texture2D> texture;
            ComPtr<ID3D11RenderTargetView> rtv;
            ComPtr<ID3D11ShaderResourceView> srv;
            unsigned width = 0, height = 0;
        };
        struct Float4 { float x = 0, y = 0, z = 0, w = 0; };
        struct alignas(16) Constants
        {
            Float4 eyeTime, forwardFov, rightAspect, upExposure, resolution;
            Float4 diskGeometry, diskAppearance, relativity, background, post, integration, sampling, diskNormal, pass;
        } m_Constants;
        struct Timing
        {
            ComPtr<ID3D11Query> disjoint, start, end;
            bool pending = false;
        };

        Target CreateTarget(unsigned width, unsigned height);
        void CreateBackbuffer();
        void CreateSceneTargets(unsigned width, unsigned height);
        void UploadConstants();
        void Draw(ID3D11RenderTargetView* target, unsigned width, unsigned height, ID3D11PixelShader* shader,
                  ID3D11ShaderResourceView* const* resources = nullptr, unsigned resourceCount = 0);

        ComPtr<ID3D11Device> m_Device;
        ComPtr<ID3D11DeviceContext> m_Context;
        ComPtr<IDXGISwapChain> m_Swapchain;
        ComPtr<ID3D11RenderTargetView> m_Backbuffer;
        ComPtr<ID3D11Buffer> m_ConstantBuffer;
        ComPtr<ID3D11VertexShader> m_FullscreenVS;
        ComPtr<ID3D11PixelShader> m_BlackHolePS, m_AccumulatePS, m_DownsamplePS, m_CompositePS;
        ComPtr<ID3D11SamplerState> m_Sampler;
        ComPtr<ID3D11RasterizerState> m_Rasterizer;
        ComPtr<ID3D11DepthStencilState> m_DepthState;
        Target m_Scene;
        std::array<Target, 2> m_History;
        std::array<Target, 5> m_Bloom;
        std::array<Timing, 4> m_Timings;
        unsigned m_Width = 0, m_Height = 0, m_RenderWidth = 0, m_RenderHeight = 0;
        unsigned m_HistoryIndex = 0, m_HistoryFrames = 0, m_Frame = 0;
        float m_GpuMilliseconds = 0;
        std::string m_AdapterName;
        bool m_DebugLayer = false;
    };

    void Check(HRESULT hr, const char* operation);
    std::filesystem::path ExecutableDirectory();
}
