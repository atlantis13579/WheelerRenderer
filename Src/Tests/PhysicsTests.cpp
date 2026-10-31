#include "../Physics/GeodesicShared.h"
#include "../App/Configuration.h"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    int failures = 0;
    void Expect(bool condition, const char* name)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        failures += !condition;
    }
    struct Result { bool captured; double phi; double invariantError; };
    Result Trace(double b, double h)
    {
        Wheeler::OrbitState state{ 0, 1 / b };
        const double invariant = 1 / (b * b);
        double phi = 0, error = 0;
        for (int i = 0; i < 200000; ++i)
        {
            auto old = state;
            state = Wheeler::StepOrbit(state, h);
            error = std::max(error, std::abs(state.v * state.v + state.u * state.u - state.u * state.u * state.u - invariant));
            if (state.u >= 1) return { true, phi + h, error };
            if (state.u < 0)
            {
                double lo = 0, hi = h;
                for (int k = 0; k < 40; ++k)
                {
                    const double mid = (lo + hi) * 0.5;
                    if (Wheeler::StepOrbit(old, mid).u > 0) lo = mid; else hi = mid;
                }
                return { false, phi + (lo + hi) * 0.5, error };
            }
            phi += h;
        }
        return { false, 0, 1e10 };
    }
}

int main(int argc, char** argv)
{
    using namespace Wheeler;
    constexpr double pi = 3.14159265358979323846;
    constexpr double critical = 2.59807621135331594;
    const auto capture = Trace(critical * 0.999, 0.02);
    const auto escape = Trace(critical * 1.001, 0.02);
    Expect(capture.captured, "Capture below b_critical = 3 sqrt(3) Rs / 2");
    Expect(!escape.captured && escape.phi > 6, "Strong lensing and escape above b_critical");
    Expect(escape.invariantError < 2e-8, "Null-orbit invariant conservation near the photon sphere");
    auto sphere = OrbitState{ 2.0 / 3.0, 0 };
    for (int i = 0; i < 100; ++i) sphere = StepOrbit(sphere, 0.04);
    Expect(std::abs(sphere.u - 2.0 / 3.0) < 1e-12, "Photon sphere fixed point r = 1.5 Rs");
    const auto weak = Trace(1000, 0.01);
    const double expected = 2.0 / 1000.0 + 15.0 * pi / (16.0 * 1000.0 * 1000.0);
    Expect(std::abs((weak.phi - pi) - expected) < 2e-8, "Weak-field bending matches 2 Rs/b plus second order");
    const double reference = Trace(3.1, 0.001).phi;
    const double e1 = std::abs(Trace(3.1, 0.08).phi - reference);
    const double e2 = std::abs(Trace(3.1, 0.04).phi - reference);
    Expect(e2 < e1 / 10.0 && e2 < 1e-5, "Fourth-order convergence of shared CPU/HLSL RK4");
    for (double radius : { 1.1, 2.0, 6.0, 28.0, 100.0 })
    {
        const double u = 1 / radius, mu = -0.7, sine = std::sqrt(1 - mu * mu);
        const double b = radius * sine / std::sqrt(1 - u);
        const double v = -u * mu * std::sqrt(1 - u) / sine;
        Expect(std::abs(v * v + u * u - u * u * u - 1 / (b * b)) < 1e-12, "Static observer tetrad satisfies null constraint");
    }
    Camera camera;
    Expect(std::abs(Dot(camera.Forward(), camera.Right())) < 1e-6f && std::abs(Dot(camera.Right(), camera.Up())) < 1e-6f,
        "Orthonormal camera basis");
    camera.Orbit(0, 100000); Expect(camera.pitch < Pi / 2, "Orbit pole clamp");
    camera.Zoom(10000); Expect(camera.distance >= 2.2f, "Zoom lower bound");
    camera.target = camera.Eye() * -1.0f + camera.target; camera.KeepOutsideHorizon();
    Expect(Length(camera.Eye()) >= 1.0999f, "Camera cannot enter the horizon");
    camera.Reset(); Vec3 before = camera.Eye(); camera.Move(1, 0, 0, 0.1f, false);
    Expect(Length(camera.Eye() - before) > 0.1f, "Continuous WASD motion");
    before = camera.Eye(); camera.Pan(10, 10, 900);
    Expect(Length(camera.Eye() - before) > 0.01f, "Middle-button pan");
    Settings settings; ApplyPreset(Preset::Scientific, settings, camera);
    Expect(settings.doppler == 1 && settings.redshift == 1 && settings.diskInner >= 3, "Scientific preset physical settings");
    const auto path = (argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path()) / "Wheeler-test-view.ini";
    std::filesystem::create_directories(path.parent_path());
    try
    {
        camera.target = { 1, 2, 3 }; camera.yaw = 1.25f;
        settings.renderScale = 1.25f; settings.massSolar = 1e10f;
        SaveConfiguration(path, settings, camera);
        Settings loaded; Camera loadedCamera;
        Expect(LoadConfiguration(path, loaded, loadedCamera) && loaded.renderScale == 1.25f && loadedCamera.target.y == 2
            && loadedCamera.yaw == 1.25f && loaded.massSolar == 1e10f,
            "Configuration round trip");
        std::ofstream(path) << "diskInner -50\ndiskOuter 0\nrenderScale 500\npitch 300\n";
        LoadConfiguration(path, loaded, loadedCamera);
        Expect(loaded.diskInner == 3 && loaded.diskOuter >= 4 && loaded.renderScale == 1.5f && loadedCamera.pitch <= 1.54f,
            "Configuration range validation");
        std::ofstream(path) << "exposure invalid\n";
        bool rejected = false;
        try { LoadConfiguration(path, loaded, loadedCamera); } catch (const std::exception&) { rejected = true; }
        Expect(rejected, "Malformed configuration rejected");
        std::ofstream(path) << "exposure";
        rejected = false;
        try { LoadConfiguration(path, loaded, loadedCamera); } catch (const std::exception&) { rejected = true; }
        Expect(rejected, "Truncated configuration rejected");
        std::filesystem::remove(path);
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; ++failures; }
    std::cout << (failures ? "FAILED" : "ALL TESTS PASSED") << " (" << failures << " failures)\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
