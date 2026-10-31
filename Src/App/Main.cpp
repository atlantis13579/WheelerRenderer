#include "Configuration.h"
#include "../Render/Renderer.h"
#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/backends/imgui_impl_dx11.h"
#include "../ThirdParty/imgui/backends/imgui_impl_win32.h"
#include <shellapi.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace Wheeler
{
    struct Options
    {
        unsigned width = 1600, height = 900, frames = 0;
        bool headless = false, noUI = false, warp = false, debug = false, validate = false;
        bool fixedTime = false, flat = false, noDisk = false, paused = false, help = false;
        bool hasYaw = false;
        float time = 12.0f, scale = 0, pitch = 1000, yaw = 0, distance = 0;
        int quality = -1, view = -1, preset = -1;
        std::filesystem::path capture, report;
    };

    Options ParseOptions()
    {
        Options o;
        int count = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!argv) throw std::runtime_error("Cannot parse command line");
        std::vector<std::wstring> args(argv, argv + count); LocalFree(argv);
        for (size_t i = 1; i < args.size(); ++i)
        {
            const auto& a = args[i];
            auto next = [&]() -> const std::wstring& { if (++i == args.size()) throw std::runtime_error("Missing command-line value"); return args[i]; };
            auto number = [&]() { const auto& text = next(); size_t n = 0; double v = std::stod(text, &n); if (n != text.size() || !std::isfinite(v)) throw std::runtime_error("Invalid numeric option"); return v; };
            auto integer = [&](int low, int high) { double v = number(); if (v != std::floor(v) || v < low || v > high) throw std::runtime_error("Integer option out of range"); return static_cast<int>(v); };
            if (a == L"--headless") o.headless = true;
            else if (a == L"--no-ui") o.noUI = true;
            else if (a == L"--warp") o.warp = true;
            else if (a == L"--debug-gpu") o.debug = true;
            else if (a == L"--validate") o.validate = true;
            else if (a == L"--flat") o.flat = true;
            else if (a == L"--no-disk") o.noDisk = true;
            else if (a == L"--paused") o.paused = true;
            else if (a == L"--help") o.help = true;
            else if (a == L"--width") o.width = integer(320, 7680);
            else if (a == L"--height") o.height = integer(240, 4320);
            else if (a == L"--frames") o.frames = integer(1, 100000);
            else if (a == L"--capture") o.capture = next();
            else if (a == L"--report") o.report = next();
            else if (a == L"--time")
            {
                const double time = number();
                if (std::abs(time) > 1e6) throw std::runtime_error("Time must be in [-1000000, 1000000] seconds");
                o.time = static_cast<float>(time); o.fixedTime = true;
            }
            else if (a == L"--scale") { o.scale = static_cast<float>(number()); if (o.scale < 0.4f || o.scale > 1.5f) throw std::runtime_error("Scale must be in [0.4, 1.5]"); }
            else if (a == L"--quality") o.quality = integer(0, 2);
            else if (a == L"--debug-view") o.view = integer(0, 3);
            else if (a == L"--pitch") { o.pitch = static_cast<float>(number()); if (std::abs(o.pitch) > 88) throw std::runtime_error("Pitch must be in [-88, 88] degrees"); }
            else if (a == L"--yaw") { o.yaw = static_cast<float>(std::remainder(number(), 360.0)); o.hasYaw = true; }
            else if (a == L"--distance") { o.distance = static_cast<float>(number()); if (o.distance < 2.2f || o.distance > 250) throw std::runtime_error("Distance must be in [2.2, 250] Rs"); }
            else if (a == L"--preset")
            {
                const auto name = next();
                if (name == L"cinematic") o.preset = 0;
                else if (name == L"scientific") o.preset = 1;
                else if (name == L"top") o.preset = 2;
                else if (name == L"lensing") o.preset = 3;
                else throw std::runtime_error("Unknown preset");
            }
            else throw std::runtime_error("Unknown command-line option");
        }
        if (o.headless && !o.frames) o.frames = 64;
        if (o.validate) { o.debug = true; if (!o.frames) o.frames = 90; }
        return o;
    }

    class Application
    {
    public:
        ~Application()
        {
            if (m_ImGuiDX11) ImGui_ImplDX11_Shutdown();
            if (m_ImGuiWin32) ImGui_ImplWin32_Shutdown();
            if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
            if (m_Window && IsWindow(m_Window)) DestroyWindow(m_Window);
        }
        int Run(const Options& options);
        LRESULT Message(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);

    private:
        bool DrawUI();
        void ToggleFullscreen();
        void Snapshot();
        void SetupStyle();
        HWND m_Window = nullptr;
        Renderer m_Renderer;
        Settings m_Settings;
        Camera m_Camera;
        Options m_Options;
        unsigned m_Width = 0, m_Height = 0, m_PendingWidth = 0, m_PendingHeight = 0;
        bool m_ShowUI = true, m_Minimized = false, m_Fullscreen = false;
        bool m_ImGuiDX11 = false, m_ImGuiWin32 = false;
        bool m_RequestCapture = false;
        float m_Time = 0, m_DpiScale = 1;
        std::string m_Notice;
        WINDOWPLACEMENT m_Placement = { sizeof(WINDOWPLACEMENT) };
        ImFont* m_TitleFont = nullptr;
    };

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
        auto* app = reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            app = static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        return app ? app->Message(hwnd, message, wp, lp) : DefWindowProcW(hwnd, message, wp, lp);
    }

    LRESULT Application::Message(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
        if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, message, wp, lp)) return 1;
        switch (message)
        {
        case WM_SIZE:
            m_Minimized = wp == SIZE_MINIMIZED;
            if (!m_Minimized) { m_PendingWidth = LOWORD(lp); m_PendingHeight = HIWORD(lp); }
            return 0;
        case WM_GETMINMAXINFO:
            if (!m_Options.headless) reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = { 720, 480 };
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DPICHANGED:
        {
            const RECT* rect = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }

    void Application::SetupStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::StyleColorsDark();
        style.WindowRounding = 0; style.FrameRounding = 4; style.GrabRounding = 4;
        style.WindowPadding = ImVec2(20, 18); style.FramePadding = ImVec2(8, 5);
        style.ItemSpacing = ImVec2(8, 9); style.WindowBorderSize = 0;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.023f, 0.028f, 0.039f, 1);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.075f, 0.087f, 0.11f, 1);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.16f, 0.20f, 1);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.19f, 0.22f, 0.25f, 1);
        style.Colors[ImGuiCol_Header] = ImVec4(0.095f, 0.11f, 0.14f, 1);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.17f, 0.20f, 0.24f, 1);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.26f, 0.29f, 1);
        style.Colors[ImGuiCol_Button] = ImVec4(0.11f, 0.13f, 0.17f, 1);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.26f, 0.30f, 1);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.4f, 0.32f, 0.19f, 1);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.88f, 0.64f, 0.31f, 1);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.79f, 0.46f, 1);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.93f, 0.70f, 0.38f, 1);
        style.Colors[ImGuiCol_Text] = ImVec4(0.87f, 0.89f, 0.93f, 1);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.46f, 0.51f, 0.59f, 1);
        style.ScaleAllSizes(m_DpiScale);
        ImGuiIO& io = ImGui::GetIO();
        const auto font = std::filesystem::path(L"C:/Windows/Fonts/segoeui.ttf");
        if (std::filesystem::exists(font))
        {
            io.Fonts->AddFontFromFileTTF(font.string().c_str(), 16.0f * m_DpiScale);
            m_TitleFont = io.Fonts->AddFontFromFileTTF(font.string().c_str(), 29.0f * m_DpiScale);
        }
        else { io.Fonts->AddFontDefault(); m_TitleFont = io.FontDefault; }
    }

    bool Application::DrawUI()
    {
        if (!m_ShowUI) return false;
        bool changed = false;
        const float panel = std::min(350.0f * m_DpiScale, m_Width * 0.48f);
        const float sceneWidth = m_Width - panel;
        ImGui::SetNextWindowPos(ImVec2(sceneWidth, 0));
        ImGui::SetNextWindowSize(ImVec2(panel, static_cast<float>(m_Height)));
        ImGui::Begin("Control panel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(ImVec4(0.91f,0.68f,0.36f,1), "W H E E L E R");
        ImGui::TextDisabled("GENERAL RELATIVITY LAB");
        ImGui::Spacing();
        ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate); ImGui::SameLine();
        ImGui::TextDisabled(" /  GPU %.2f ms", m_Renderer.GpuMilliseconds());
        ImGui::TextDisabled("%u x %u  |  DX11 / SM 5.0", m_Renderer.RenderWidth(), m_Renderer.RenderHeight());
        ImGui::Separator();
        const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Cinematic", ImVec2(buttonWidth, 0))) { ApplyPreset(Preset::Cinematic, m_Settings, m_Camera); changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("Scientific", ImVec2(buttonWidth, 0))) { ApplyPreset(Preset::Scientific, m_Settings, m_Camera); changed = true; }
        if (ImGui::Button("Polar view", ImVec2(buttonWidth, 0))) { ApplyPreset(Preset::TopDown, m_Settings, m_Camera); changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("Star lensing", ImVec2(buttonWidth, 0))) { ApplyPreset(Preset::StarLensing, m_Settings, m_Camera); changed = true; }
        ImGui::Spacing();
        ImGui::PushItemWidth(-125.0f * m_DpiScale);
        if (ImGui::CollapsingHeader("Spacetime", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Schwarzschild / non-spinning");
            changed |= ImGui::Checkbox("Gravitational lensing", &m_Settings.lensing);
            changed |= ImGui::SliderFloat("Doppler", &m_Settings.doppler, 0, 1, "%.2f");
            changed |= ImGui::SliderFloat("Redshift", &m_Settings.redshift, 0, 1, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("1 = physical frequency shift; 0 = cinematic suppression.");
            ImGui::SliderFloat("Mass / solar", &m_Settings.massSolar, 1.0f, 1e10f, "%.2e", ImGuiSliderFlags_Logarithmic);
            ImGui::TextDisabled("Rs = %.3g km  |  photon sphere = 1.5 Rs", 2.95325 * m_Settings.massSolar);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mass sets the physical length/time scale. Geometry uses Rs units.\nThe disk animation clock is artistic, not a physical clock.");
        }
        if (ImGui::CollapsingHeader("Accretion disk", ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= ImGui::Checkbox("Visible disk", &m_Settings.disk);
            changed |= ImGui::SliderFloat("Inner / Rs", &m_Settings.diskInner, 3, 10, "%.2f");
            m_Settings.diskOuter = std::max(m_Settings.diskOuter, m_Settings.diskInner + 1);
            changed |= ImGui::SliderFloat("Outer / Rs", &m_Settings.diskOuter, m_Settings.diskInner + 1, 40, "%.1f");
            changed |= ImGui::SliderFloat("Kelvin", &m_Settings.diskTemperature, 2000, 30000, "%.0f");
            changed |= ImGui::SliderFloat("Emission", &m_Settings.diskBrightness, 0, 30, "%.2f");
            changed |= ImGui::SliderFloat("Structure", &m_Settings.diskTurbulence, 0, 1, "%.2f");
            changed |= ImGui::SliderFloat("Opacity", &m_Settings.diskOpacity, 0, 1, "%.2f");
            changed |= ImGui::SliderFloat("Rotation", &m_Settings.diskRotation, -3, 3, "%.2f");
            changed |= ImGui::SliderFloat("Tilt", &m_Settings.diskTilt, -80, 80, "%.1f deg");
        }
        if (ImGui::CollapsingHeader("Light & lens", ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= ImGui::SliderFloat("Exposure", &m_Settings.exposure, 0.05f, 8, "%.2f", ImGuiSliderFlags_Logarithmic);
            changed |= ImGui::SliderFloat("Bloom", &m_Settings.bloom, 0, 2, "%.2f");
            changed |= ImGui::SliderFloat("Threshold", &m_Settings.bloomThreshold, 0.1f, 5, "%.2f");
            changed |= ImGui::SliderFloat("Radius", &m_Settings.bloomRadius, 0.5f, 2.5f, "%.2f");
            changed |= ImGui::SliderFloat("Anamorphic", &m_Settings.streak, 0, 1, "%.2f");
            changed |= ImGui::SliderFloat("Stars", &m_Settings.starBrightness, 0, 5, "%.2f");
            changed |= ImGui::SliderFloat("Milky Way", &m_Settings.nebulaBrightness, 0, 5, "%.2f");
            changed |= ImGui::SliderFloat("Vignette", &m_Settings.vignette, 0, 1, "%.2f");
        }
        if (ImGui::CollapsingHeader("Camera & playback"))
        {
            changed |= ImGui::SliderFloat("Distance / Rs", &m_Camera.distance, 2.2f, 250, "%.2f", ImGuiSliderFlags_Logarithmic);
            changed |= ImGui::SliderAngle("Elevation", &m_Camera.pitch, -88, 88);
            changed |= ImGui::SliderAngle("Azimuth", &m_Camera.yaw, -180, 180);
            changed |= ImGui::SliderFloat("FOV", &m_Camera.fov, 15, 100, "%.1f deg");
            ImGui::SliderFloat("Move speed", &m_Camera.speed, 0.02f, 2, "%.2f");
            ImGui::Checkbox("Auto orbit", &m_Camera.autoOrbit);
            ImGui::Checkbox("Pause animation", &m_Settings.paused);
            ImGui::SliderFloat("Time scale", &m_Settings.timeScale, 0, 5, "%.2f");
            if (ImGui::Button("Reset camera")) { m_Camera.Reset(); changed = true; }
            ImGui::TextDisabled("Observer radius: %.2f Rs", Length(m_Camera.Eye()));
        }
        if (ImGui::CollapsingHeader("Renderer & diagnostics"))
        {
            changed |= ImGui::Combo("Quality", &m_Settings.quality, "Fast\0Balanced\0High\0");
            changed |= ImGui::SliderFloat("Scale", &m_Settings.renderScale, 0.4f, 1.5f, "%.2fx");
            changed |= ImGui::Checkbox("Progressive antialiasing", &m_Settings.temporalAA);
            ImGui::Checkbox("VSync", &m_Settings.vsync);
            changed |= ImGui::Combo("View", &m_Settings.debugView, "Beauty\0Ray winding\0Iteration cost\0Capture mask\0");
            ImGui::TextDisabled("RK4  |  step %.3f rad  |  max %d", m_Settings.StepAngle(), m_Settings.MaxSteps());
            ImGui::TextWrapped("%s", m_Renderer.AdapterName().c_str());
            ImGui::TextDisabled("Magenta in diagnostics = step budget reached.");
        }
        if (ImGui::CollapsingHeader("Controls & files"))
        {
            ImGui::TextWrapped("RMB: orbit   MMB: pan   Wheel: zoom\nWASD: move   Q/E: down/up   Shift: fast\nR: reset   Space: pause   1-4: presets\nTab: interface   F11: fullscreen\nF12: screenshot   F5: reload shaders");
            if (ImGui::Button("Save view"))
            {
                try { SaveConfiguration(ExecutableDirectory() / "Wheeler.ini", m_Settings, m_Camera); m_Notice = "View saved."; }
                catch (const std::exception& e) { m_Notice = e.what(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Load view"))
            {
                try { changed |= LoadConfiguration(ExecutableDirectory() / "Wheeler.ini", m_Settings, m_Camera); m_Notice = changed ? "View loaded." : "No saved view."; }
                catch (const std::exception& e) { m_Notice = e.what(); }
            }
            if (ImGui::Button("Capture PNG")) m_RequestCapture = true;
            ImGui::TextWrapped("Thin emitting disk; procedural matter and sky. Camera rotation is supported. Kerr spin and fluid dynamics are not simulated.");
        }
        ImGui::PopItemWidth();
        if (!m_Notice.empty()) { ImGui::Separator(); ImGui::TextWrapped("%s", m_Notice.c_str()); }
        ImGui::End();

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        const float margin = 32.0f * m_DpiScale;
        draw->AddText(m_TitleFont, 29.0f * m_DpiScale, ImVec2(margin, margin), IM_COL32(229, 222, 209, 255), "Into the event horizon");
        draw->AddText(ImVec2(margin, margin + 42 * m_DpiScale), IM_COL32(116, 126, 141, 255), "NULL GEODESICS  /  REAL-TIME OBSERVATORY");
        draw->AddLine(ImVec2(margin, m_Height - 62 * m_DpiScale), ImVec2(sceneWidth - margin, m_Height - 62 * m_DpiScale), IM_COL32(68, 74, 86, 150));
        draw->AddText(ImVec2(margin, m_Height - 43 * m_DpiScale), IM_COL32(139, 148, 162, 255), "RMB  ORBIT     SCROLL  ZOOM     WASD  MOVE");
        return changed;
    }

    void Application::ToggleFullscreen()
    {
        if (!m_Fullscreen)
        {
            GetWindowPlacement(m_Window, &m_Placement);
            MONITORINFO monitor = { sizeof(monitor) }; GetMonitorInfoW(MonitorFromWindow(m_Window, MONITOR_DEFAULTTONEAREST), &monitor);
            SetWindowLongPtrW(m_Window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(m_Window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED);
        }
        else
        {
            SetWindowLongPtrW(m_Window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            SetWindowPlacement(m_Window, &m_Placement);
            SetWindowPos(m_Window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
        m_Fullscreen = !m_Fullscreen;
    }

    void Application::Snapshot()
    {
        SYSTEMTIME time; GetLocalTime(&time);
        wchar_t filename[128];
        swprintf_s(filename, L"Wheeler-%04u%02u%02u-%02u%02u%02u-%03u.png", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
        m_Renderer.Capture(ExecutableDirectory() / "Captures" / filename);
        m_Notice = "PNG saved in Binaries/.../Captures.";
        m_RequestCapture = false;
    }

    int Application::Run(const Options& options)
    {
        m_Options = options; m_Width = options.width; m_Height = options.height; m_ShowUI = !options.noUI;
        if (!options.headless && options.preset < 0)
        {
            try { LoadConfiguration(ExecutableDirectory() / "Wheeler.ini", m_Settings, m_Camera); }
            catch (const std::exception& e) { m_Notice = e.what(); }
        }
        if (options.preset >= 0) ApplyPreset(static_cast<Preset>(options.preset), m_Settings, m_Camera);
        if (options.scale) m_Settings.renderScale = options.scale;
        if (options.quality >= 0) m_Settings.quality = options.quality;
        if (options.view >= 0) m_Settings.debugView = options.view;
        if (options.pitch != 1000) m_Camera.pitch = options.pitch * Pi / 180;
        if (options.hasYaw) m_Camera.yaw = options.yaw * Pi / 180;
        if (options.distance) m_Camera.distance = options.distance;
        if (options.flat) m_Settings.lensing = false;
        if (options.noDisk) m_Settings.disk = false;
        if (options.headless) m_Settings.vsync = false;
        if (options.paused || options.fixedTime) m_Settings.paused = true;
        m_Time = options.time;

        WNDCLASSEXW windowClass = { sizeof(windowClass) };
        windowClass.style = CS_CLASSDC; windowClass.lpfnWndProc = WindowProc; windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW); windowClass.lpszClassName = L"WheelerObservatory";
        if (!RegisterClassExW(&windowClass)) throw std::runtime_error("Cannot register window class");
        RECT rect = { 0, 0, static_cast<LONG>(m_Width), static_cast<LONG>(m_Height) };
        AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
        m_Window = CreateWindowExW(0, windowClass.lpszClassName, L"Wheeler | Black Hole Observatory", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, windowClass.hInstance, this);
        if (!m_Window) throw std::runtime_error("Cannot create window");
        m_Renderer.Initialize(m_Window, m_Width, m_Height, options.warp, options.debug);
        IMGUI_CHECKVERSION(); ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        m_DpiScale = std::clamp(GetDpiForWindow(m_Window) / 96.0f, 1.0f, 2.0f);
        SetupStyle();
        m_ImGuiWin32 = ImGui_ImplWin32_Init(m_Window);
        m_ImGuiDX11 = ImGui_ImplDX11_Init(m_Renderer.Device(), m_Renderer.Context());
        if (!m_ImGuiDX11 || !m_ImGuiWin32) throw std::runtime_error("ImGui initialization failed");
        if (!options.headless) ShowWindow(m_Window, SW_SHOWDEFAULT);

        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();
        const auto started = previous;
        unsigned frame = 0;
        double gpuSum = 0; unsigned gpuSamples = 0;
        bool quit = false, reset = true;
        while (!quit && (!options.frames || frame < options.frames))
        {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message); DispatchMessageW(&message);
                if (message.message == WM_QUIT) quit = true;
            }
            if (quit) break;
            if (m_Minimized) { WaitMessage(); previous = Clock::now(); continue; }
            if (m_PendingWidth && m_PendingHeight)
            {
                m_Width = m_PendingWidth; m_Height = m_PendingHeight;
                m_Renderer.Resize(m_Width, m_Height); m_PendingWidth = m_PendingHeight = 0; reset = true;
            }
            // Exercise swapchain and all HDR target lifetimes during automated validation.
            if (options.validate && (frame == options.frames / 3 || frame == options.frames * 2 / 3))
            {
                bool shrink = frame == options.frames / 3;
                m_Width = options.width - (shrink ? 64 : 0); m_Height = options.height - (shrink ? 48 : 0);
                m_Renderer.Resize(m_Width, m_Height); reset = true;
            }
            const auto now = Clock::now();
            const float dt = std::clamp(std::chrono::duration<float>(now - previous).count(), 0.00001f, 0.05f);
            previous = now;
            ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame();
            io.DisplaySize = ImVec2(static_cast<float>(m_Width), static_cast<float>(m_Height));
            ImGui::NewFrame();
            if (!options.headless && GetForegroundWindow() == m_Window)
            {
                if (!io.WantTextInput)
                {
                    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false))
                    {
                        m_ShowUI = !m_ShowUI; reset = true;
                        if (!m_ShowUI) ImGui::SetWindowFocus(nullptr);
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) ToggleFullscreen();
                    if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) m_RequestCapture = true;
                }
                if (!io.WantCaptureKeyboard)
                {
                    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { m_Camera.Reset(); reset = true; }
                    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) m_Settings.paused = !m_Settings.paused;
                    for (int p = 0; p < 4; ++p)
                        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + p), false)) { ApplyPreset(static_cast<Preset>(p), m_Settings, m_Camera); reset = true; }
                    if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
                    {
                        try { m_Renderer.ReloadShaders(); m_Notice = "Shaders reloaded."; reset = true; }
                        catch (const std::exception& e) { m_Notice = e.what(); }
                    }
                    const Vec3 before = m_Camera.Eye();
                    m_Camera.Move(static_cast<float>(ImGui::IsKeyDown(ImGuiKey_W)) - ImGui::IsKeyDown(ImGuiKey_S),
                        static_cast<float>(ImGui::IsKeyDown(ImGuiKey_D)) - ImGui::IsKeyDown(ImGuiKey_A),
                        static_cast<float>(ImGui::IsKeyDown(ImGuiKey_E)) - ImGui::IsKeyDown(ImGuiKey_Q), dt, io.KeyShift);
                    reset |= Length(before - m_Camera.Eye()) > 0;
                }
                if (!io.WantCaptureMouse)
                {
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0) && (io.MouseDelta.x || io.MouseDelta.y))
                    { m_Camera.Orbit(io.MouseDelta.x, io.MouseDelta.y); reset = true; }
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0) && (io.MouseDelta.x || io.MouseDelta.y))
                    { m_Camera.Pan(io.MouseDelta.x, io.MouseDelta.y, static_cast<float>(m_Height)); reset = true; }
                    if (io.MouseWheel) { m_Camera.Zoom(io.MouseWheel); reset = true; }
                }
            }
            reset |= DrawUI();
            if (m_Camera.autoOrbit) { m_Camera.yaw += dt * 0.10f; reset = true; }
            m_Camera.KeepOutsideHorizon();
            if (!m_Settings.paused) m_Time += dt * m_Settings.timeScale;
            const unsigned sceneWidth = m_ShowUI ? m_Width - static_cast<unsigned>(std::min(350.0f * m_DpiScale, m_Width * 0.48f)) : m_Width;
            m_Renderer.Render(m_Camera, m_Settings, m_Time, sceneWidth, reset); reset = false;
            ImGui::Render(); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if (m_RequestCapture) Snapshot();
            if (options.frames && frame + 1 == options.frames)
            {
                if (!options.capture.empty()) m_Renderer.Capture(options.capture);
                m_Renderer.ValidateHDR();
            }
            m_Renderer.Present(m_Settings.vsync);
            if (frame > 12 && m_Renderer.GpuMilliseconds() > 0) { gpuSum += m_Renderer.GpuMilliseconds(); ++gpuSamples; }
            ++frame;
        }
        const unsigned warnings = m_Renderer.DebugWarnings();
        if (!options.report.empty())
        {
            if (options.report.has_parent_path()) std::filesystem::create_directories(options.report.parent_path());
            std::ofstream report(options.report);
            if (!report) throw std::runtime_error("Cannot write validation report");
            report << "{\n  \"adapter\": \"" << m_Renderer.AdapterName() << "\",\n  \"frames\": " << frame
                << ",\n  \"width\": " << m_Width << ",\n  \"height\": " << m_Height
                << ",\n  \"render_width\": " << m_Renderer.RenderWidth() << ",\n  \"render_height\": " << m_Renderer.RenderHeight()
                << ",\n  \"gpu_ms\": " << (gpuSamples ? gpuSum / gpuSamples : m_Renderer.GpuMilliseconds())
                << ",\n  \"elapsed_seconds\": " << std::chrono::duration<double>(Clock::now() - started).count()
                << ",\n  \"debug_layer\": " << (m_Renderer.HasDebugLayer() ? "true" : "false")
                << ",\n  \"debug_warnings\": " << warnings << ",\n  \"hdr_validated\": " << (options.frames && frame == options.frames ? "true" : "false") << "\n}\n";
        }
        if (warnings) throw std::runtime_error("D3D11 validation reported warnings; see d3d11-validation.log");
        return 0;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    bool headless = false;
    try
    {
        const auto options = Wheeler::ParseOptions(); headless = options.headless;
        if (options.help)
        {
            const char* text = "Wheeler: --headless --frames N --width N --height N --no-ui --capture file.png --report file.json\n"
                "--preset cinematic|scientific|top|lensing --quality 0|1|2 --scale 0.4..1.5 --time seconds --paused\n"
                "--pitch degrees --yaw degrees --distance Rs --flat --no-disk --debug-view 0..3 --debug-gpu --validate --warp\n";
            std::ofstream(Wheeler::ExecutableDirectory() / "command-line-help.txt") << text;
            OutputDebugStringA(text);
        }
        else { Wheeler::Application app; result = app.Run(options); }
    }
    catch (const std::exception& e)
    {
        std::ofstream(Wheeler::ExecutableDirectory() / "Wheeler-error.log") << e.what() << '\n';
        OutputDebugStringA(e.what());
        if (!headless && wcsstr(GetCommandLineW(), L"--headless") == nullptr) MessageBoxA(nullptr, e.what(), "Wheeler", MB_ICONERROR);
        result = 1;
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
