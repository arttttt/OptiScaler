#pragma once

#include <cmath>
#include <cstdint>

// Halton(2,3) sub-pixel jitter, matching AMD FSR2 reference.
// Sources:
//   https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-temporal/
//   https://github.com/GPUOpen-Effects/FidelityFX-FSR2/blob/master/README.md
namespace HaltonSequence
{
    inline float Halton(int32_t index, int32_t base)
    {
        float f = 1.0f, result = 0.0f;

        for (int32_t currentIndex = index; currentIndex > 0;)
        {
            f /= static_cast<float>(base);
            result = result + f * static_cast<float>(currentIndex % base);
            currentIndex = static_cast<int32_t>(std::floor(static_cast<float>(currentIndex) / static_cast<float>(base)));
        }

        return result;
    }

    // FSR2 phase count: ceil(8 * scale^2). DLAA (scale 1.0) = 8.
    inline int32_t GetPhaseCount(float scaleRatio)
    {
        return static_cast<int32_t>(std::ceil(8.0f * scaleRatio * scaleRatio));
    }

    // Returns sub-pixel jitter in pixel space, range [-0.5, +0.5].
    // Halton index starts at 1 so (0,0) is never produced — FSR2 requires this.
    inline void GetJitterOffset(int32_t frameIndex, int32_t phaseCount, float* outPixelX, float* outPixelY)
    {
        const int32_t haltonIndex = (frameIndex % phaseCount) + 1;
        *outPixelX = Halton(haltonIndex, 2) - 0.5f;
        *outPixelY = Halton(haltonIndex, 3) - 0.5f;
    }
}
