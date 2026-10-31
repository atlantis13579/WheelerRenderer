#pragma once
#include "Camera.h"
#include "Settings.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace Wheeler
{
    inline void ApplyPreset(Preset preset, Settings& settings, Camera& camera)
    {
        const bool vsync = settings.vsync;
        settings = Settings{}; settings.vsync = vsync; camera.Reset();
        if (preset == Preset::Scientific)
        {
            settings.doppler = settings.redshift = 1.0f;
            settings.bloom = 0.12f; settings.streak = 0;
            settings.diskTemperature = 11000; camera.pitch = 0.2f;
        }
        else if (preset == Preset::TopDown)
        {
            camera.pitch = 1.35f; camera.distance = 38;
            settings.doppler = 0.7f; settings.streak = 0;
        }
        else if (preset == Preset::StarLensing)
        {
            settings.disk = false; settings.starBrightness = 2.8f;
            settings.nebulaBrightness = 3.0f; settings.bloom = 0.15f;
            camera.distance = 12; camera.pitch = 0.3f;
        }
    }

    // Keep the text format explicit and tolerant of unknown future keys.
#define WHEELER_SETTING_FIELDS(X) \
    X(diskInner) X(diskOuter) X(diskTemperature) X(diskBrightness) X(diskOpacity) \
    X(diskTurbulence) X(diskRotation) X(diskTilt) X(doppler) X(redshift) \
    X(exposure) X(bloom) X(bloomThreshold) X(bloomRadius) X(streak) \
    X(starBrightness) X(nebulaBrightness) X(vignette) X(renderScale) X(timeScale) \
    X(massSolar) X(quality) X(debugView) X(disk) X(lensing) X(paused) X(vsync) X(temporalAA)

    inline void SaveConfiguration(const std::filesystem::path& path, const Settings& s, const Camera& c)
    {
        std::ofstream out(path);
        if (!out) throw std::runtime_error("Cannot save configuration");
        out << std::setprecision(9) << "version 1\n";
#define WRITE_FIELD(name) out << #name << ' ' << s.name << '\n';
        WHEELER_SETTING_FIELDS(WRITE_FIELD)
#undef WRITE_FIELD
        out << "cameraX " << c.target.x << "\ncameraY " << c.target.y << "\ncameraZ " << c.target.z
            << "\nyaw " << c.yaw << "\npitch " << c.pitch << "\ndistance " << c.distance
            << "\nfov " << c.fov << "\nspeed " << c.speed << '\n';
        if (!out) throw std::runtime_error("Failed to write configuration");
    }

    inline bool LoadConfiguration(const std::filesystem::path& path, Settings& settings, Camera& camera)
    {
        std::ifstream in(path);
        if (!in) return false;
        std::map<std::string, double> values;
        std::string key; double value;
        while (in >> key)
        {
            if (!(in >> value)) throw std::runtime_error("Malformed configuration");
            if (!std::isfinite(value)) throw std::runtime_error("Configuration contains nonfinite values");
            values[key] = value;
        }
        if (!in.eof()) throw std::runtime_error("Malformed configuration");
        Settings s; Camera c;
        auto get = [&](const char* name, double fallback) { auto it = values.find(name); return it == values.end() ? fallback : it->second; };
        auto assign = [&](auto& field, const char* name)
        {
            using T = std::decay_t<decltype(field)>;
            field = static_cast<T>(std::clamp(get(name, field), static_cast<double>(std::numeric_limits<T>::lowest()),
                                             static_cast<double>(std::numeric_limits<T>::max())));
        };
#define READ_FIELD(name) assign(s.name, #name);
        WHEELER_SETTING_FIELDS(READ_FIELD)
#undef READ_FIELD
        s.diskInner = std::clamp(s.diskInner, 3.0f, 10.0f);
        s.diskOuter = std::clamp(s.diskOuter, s.diskInner + 1.0f, 40.0f);
        s.diskTemperature = std::clamp(s.diskTemperature, 2000.0f, 30000.0f);
        s.diskBrightness = std::clamp(s.diskBrightness, 0.0f, 30.0f);
        s.diskOpacity = std::clamp(s.diskOpacity, 0.0f, 1.0f);
        s.diskTurbulence = std::clamp(s.diskTurbulence, 0.0f, 1.0f);
        s.diskRotation = std::clamp(s.diskRotation, -3.0f, 3.0f);
        s.diskTilt = std::clamp(s.diskTilt, -80.0f, 80.0f);
        s.doppler = std::clamp(s.doppler, 0.0f, 1.0f); s.redshift = std::clamp(s.redshift, 0.0f, 1.0f);
        s.exposure = std::clamp(s.exposure, 0.05f, 8.0f); s.bloom = std::clamp(s.bloom, 0.0f, 2.0f);
        s.bloomThreshold = std::clamp(s.bloomThreshold, 0.1f, 5.0f); s.bloomRadius = std::clamp(s.bloomRadius, 0.5f, 2.5f);
        s.streak = std::clamp(s.streak, 0.0f, 1.0f);
        s.starBrightness = std::clamp(s.starBrightness, 0.0f, 5.0f); s.nebulaBrightness = std::clamp(s.nebulaBrightness, 0.0f, 5.0f);
        s.vignette = std::clamp(s.vignette, 0.0f, 1.0f); s.renderScale = std::clamp(s.renderScale, 0.4f, 1.5f);
        s.timeScale = std::clamp(s.timeScale, 0.0f, 5.0f); s.massSolar = std::clamp(s.massSolar, 1.0f, 1e10f);
        s.quality = std::clamp(s.quality, 0, 2); s.debugView = std::clamp(s.debugView, 0, 3);
        c.target = { static_cast<float>(std::clamp(get("cameraX", 0), -1000.0, 1000.0)),
                     static_cast<float>(std::clamp(get("cameraY", 0), -1000.0, 1000.0)),
                     static_cast<float>(std::clamp(get("cameraZ", 0), -1000.0, 1000.0)) };
        c.yaw = static_cast<float>(std::remainder(get("yaw", c.yaw), 2.0 * Pi));
        c.pitch = static_cast<float>(std::clamp(get("pitch", c.pitch), -1.54, 1.54));
        c.distance = static_cast<float>(std::clamp(get("distance", c.distance), 2.2, 250.0));
        c.fov = static_cast<float>(std::clamp(get("fov", c.fov), 15.0, 100.0));
        c.speed = static_cast<float>(std::clamp(get("speed", c.speed), 0.02, 2.0));
        c.KeepOutsideHorizon(); settings = s; camera = c;
        return true;
    }
#undef WHEELER_SETTING_FIELDS
}
