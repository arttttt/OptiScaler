#pragma once
#include <string>

/**
 * @brief Common strings and identifiers used internally by OptiScaler
 */
namespace OptiKeys
{
using CString = const char[];

// Application name provided to upscalers
inline constexpr CString ProjectID = "OptiScaler";

// ID code used for the Vulkan input provider
inline constexpr CString VkProvider = "OptiVk";
// ID code used for the DX11 input provider
inline constexpr CString Dx11Provider = "OptiDx11";
// ID code used for the DX12 input provider
inline constexpr CString Dx12Provider = "OptiDx12";
// ID code used for the DX9 input provider
inline constexpr CString Dx9Provider = "OptiDx9";

inline constexpr CString FSR_UpscaleWidth = "FSR.upscaleSize.width";
inline constexpr CString FSR_UpscaleHeight = "FSR.upscaleSize.height";

inline constexpr CString FSR_NearPlane = "FSR.cameraNear";
inline constexpr CString FSR_FarPlane = "FSR.cameraFar";
inline constexpr CString FSR_CameraFovVertical = "FSR.cameraFovAngleVertical";
inline constexpr CString FSR_FrameTimeDelta = "FSR.frameTimeDelta";
inline constexpr CString FSR_ViewSpaceToMetersFactor = "FSR.viewSpaceToMetersFactor";
inline constexpr CString FSR_TransparencyAndComp = "FSR.transparencyAndComposition";
inline constexpr CString FSR_Reactive = "FSR.reactive";

} // namespace OptiKeys

typedef enum API
{
    NotSelected = 0,
    DX11,
    DX12,
    Vulkan,
    DX9,
} API;

enum class Upscaler
{

    XeSS, // "xess", used for the XeSS upscaler backend

    XeSS_11on12, // "xess_12", used for the XeSS upscaler backend used with the DirectX 11 on 12 compatibility
                 // layer

    FSR21, // "fsr21", used for the FSR 2.1.x upscaler backend

    FSR21_11on12, // "fsr21_12", used for the FSR 2.1.x upscaler backend used with the DirectX 11 on 12
                  // compatibility layer

    FSR22, // "fsr22", used for the FSR 2.2.x upscaler backend

    FSR22_11on12, // "fsr22_12", used for the FSR 2.2.x upscaler backend used with the DirectX 11 on 12
                  // compatibility layer

    FSR31, // "fsr31", used for the FSR 3.1+ upscaler backend

    FSR31_11on12, // "fsr31_12", used for the FSR 3.1+ upscaler backend used with the DirectX 11 on 12
                  // compatibility layer

    DLSS, // "dlss", used for the DLSS upscaler backend

    DLSSD, // "dlssd", used for the DLSS-D/Ray Reconstruction upscaler+denoiser backend
    Reset
};

std::string UpscalerDisplayName(Upscaler upscaler, API api = API::NotSelected);

// Converts enum to the string codes for config
std::string UpscalerToCode(Upscaler upscaler);

// Converts string codes into enum for config
Upscaler CodeToUpscaler(const std::string& code);