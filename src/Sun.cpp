#include "Sun.h"
#include <cmath>
#include <algorithm>

static constexpr float PI = 3.14159265358979f;

void Sun::Init() {
    Update(timeOfDay);
}

// ---------------------------------------------------------------------------
//  Kelvin to approximate RGB blackbody (Tanner Helland algorithm)
// ---------------------------------------------------------------------------
XMFLOAT3 Sun::KelvinToRGB(float K) {
    K = std::clamp(K, 1000.0f, 40000.0f) / 100.0f;
    float r, g, b;

    // Red
    if (K <= 66.0f)  r = 1.0f;
    else             r = std::clamp(329.698727446f * powf(K - 60.0f, -0.1332047592f) / 255.0f, 0.0f, 1.0f);

    // Green
    if (K <= 66.0f)  g = std::clamp((99.4708025861f * logf(K) - 161.1195681661f) / 255.0f, 0.0f, 1.0f);
    else             g = std::clamp(288.1221695283f * powf(K - 60.0f, -0.0755148492f) / 255.0f, 0.0f, 1.0f);

    // Blue
    if (K >= 66.0f)  b = 1.0f;
    else if (K <= 19.0f) b = 0.0f;
    else             b = std::clamp((138.5177312231f * logf(K - 10.0f) - 305.0447927307f) / 255.0f, 0.0f, 1.0f);

    return { r, g, b };
}

// ---------------------------------------------------------------------------
//  Approximate zenith and horizon sky colors
// ---------------------------------------------------------------------------
XMFLOAT3 Sun::ComputeZenithColor(float elevation, float) {
    // Blue shifts with elevation, dims at night
    float t = std::clamp(elevation / (PI * 0.5f), 0.0f, 1.0f);
    float nightFade = std::clamp(elevation / (PI * 0.1f), 0.0f, 1.0f);
    return {
        0.05f * nightFade + 0.2f * t,
        0.08f * nightFade + 0.45f * t,
        0.12f * nightFade + 0.85f * t
    };
}

XMFLOAT3 Sun::ComputeHorizonColor(float elevation, float) {
    float t = std::clamp(elevation / (PI * 0.5f), 0.0f, 1.0f);
    float sunriseGlow = std::clamp(1.0f - fabsf(t - 0.07f) / 0.12f, 0.0f, 1.0f);
    float nightFade = std::clamp(elevation / (PI * 0.1f), 0.0f, 1.0f);
    // Sunrise/sunset horizontal glow
    XMFLOAT3 midday   = { 0.55f, 0.73f, 0.97f };
    XMFLOAT3 sunrise  = { 1.0f,  0.55f, 0.2f  };
    XMFLOAT3 night    = { 0.02f, 0.03f, 0.08f };
    XMFLOAT3 result;
    result.x = night.x + (midday.x - night.x) * nightFade + sunrise.x * sunriseGlow * 0.8f;
    result.y = night.y + (midday.y - night.y) * nightFade + sunrise.y * sunriseGlow * 0.5f;
    result.z = night.z + (midday.z - night.z) * nightFade + sunrise.z * sunriseGlow * 0.1f;
    return result;
}

// ---------------------------------------------------------------------------
//  Update: convert time-of-day to sun direction + all light colors
// ---------------------------------------------------------------------------
void Sun::Update(float tod) {
    timeOfDay = tod;
    m_data.timeOfDay = tod;

    // Hour angle from solar noon (12:00)
    float hourAngle = (tod - 12.0f) * (PI / 12.0f);  // radians

    // Elevation angle (sun height above horizon): cosine of latitude=45 deg assumed
    float elevAngle = asinf(std::clamp(
        sinf(XMConvertToRadians(45.0f)) * sinf(0.0f) +
        cosf(XMConvertToRadians(45.0f)) * cosf(0.0f) * cosf(hourAngle),
        -1.0f, 1.0f));

    // Azimuth
    float cosAz = (sinf(0.0f) - sinf(elevAngle) * sinf(XMConvertToRadians(45.0f)))
                / (cosf(elevAngle) * cosf(XMConvertToRadians(45.0f)));
    float azimuth = acosf(std::clamp(cosAz, -1.0f, 1.0f));
    if (hourAngle > 0) azimuth = 2.0f * PI - azimuth;

    // Direction FROM surface TO sun (in world-space: +Y up, +Z forward)
    XMFLOAT3 dirToSun = {
        sinf(azimuth) * cosf(elevAngle),
        sinf(elevAngle),
        cosf(azimuth) * cosf(elevAngle)
    };
    m_data.dirToSun = dirToSun;

    // Sun color: blackbody from Kelvin temperature curve
    float sunElev  = std::clamp(elevAngle / (0.5f * PI), 0.0f, 1.0f);  // 0..1
    float kelvin   = 2600.0f + sunElev * 4100.0f;  // 2600K at horizon to 6700K at zenith
    m_data.sunColor = KelvinToRGB(kelvin);

    // Sun intensity: 0 at night, ramps up around sunrise/sunset
    float elevAbove = std::max(0.0f, sinf(elevAngle));
    m_data.sunIntensity  = sunIntensity * std::clamp(sqrtf(elevAbove) * 3.5f, 0.0f, 1.0f);

    // Ambient: low blue-tinted on day, near zero at night
    float ambientScale = std::clamp(sinf(elevAngle) * 4.0f + 0.1f, 0.0f, 1.0f) * 0.4f;
    m_data.ambientColor = { ambientScale * 0.85f, ambientScale * 0.9f, ambientScale };
    m_data.ambientIntensity = ambientScale;

    // Fog — tint toward horizon sky colour so distant haze matches the atmosphere.
    // At noon: pale sky blue.  At sunrise/sunset: warm orange.  At night: very dark.
    XMFLOAT3 hc = ComputeHorizonColor(elevAngle, 2.0f);
    float nightFade = std::clamp(sinf(elevAngle) * 4.0f + 0.4f, 0.0f, 1.0f);
    m_data.fogColor   = { hc.x * 0.75f * nightFade,
                          hc.y * 0.75f * nightFade,
                          hc.z * 0.75f * nightFade };
    m_data.fogDensity = fogDensity;

    // Sky colors (used by Preetham sky shader for gradient fallback)
    m_data.zenithColor  = ComputeZenithColor(elevAngle, 2.0f);
    m_data.horizonColor = ComputeHorizonColor(elevAngle, 2.0f);
}
