#pragma once

namespace Wheeler
{
    enum class Preset { Cinematic, Scientific, TopDown, StarLensing };

    struct Settings
    {
        float diskInner = 3.0f;
        float diskOuter = 14.0f;
        float diskTemperature = 6200.0f;
        float diskBrightness = 3.2f;
        float diskOpacity = 0.97f;
        float diskTurbulence = 0.85f;
        float diskRotation = 1.0f;
        float diskTilt = 0.0f;
        float doppler = 0.3f;
        float redshift = 0.65f;
        float exposure = 1.1f;
        float bloom = 0.65f;
        float bloomThreshold = 1.0f;
        float bloomRadius = 1.0f;
        float streak = 0.13f;
        float starBrightness = 0.12f;
        float nebulaBrightness = 0.14f;
        float vignette = 0.28f;
        float renderScale = 1.0f;
        float timeScale = 1.0f;
        float massSolar = 100000000.0f;
        int quality = 1;
        int debugView = 0;
        bool disk = true;
        bool lensing = true;
        bool paused = false;
        bool vsync = true;
        bool temporalAA = true;

        float StepAngle() const { return quality == 0 ? 0.07f : quality == 1 ? 0.04f : 0.022f; }
        int MaxSteps() const { return quality == 0 ? 180 : quality == 1 ? 320 : 600; }
    };
}
