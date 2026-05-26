#pragma once
#include "SysUtils.h"
#include "Sharpen_Common.h"
#include <d3d11.h>

// Simple sharpen-pass dispatcher used by the Dx9wDx11 bridge as a stand-in
// for the FSR2 Evaluate that will replace it once an x86 FFX bundle exists
// (see project memory). Reads the shared color via SRV, writes to an
// internal DX11-only UAV-bound texture; the bridge then CopyResource's that
// into the shared output (formats match by construction).
class Sharpen_Dx11
{
  private:
    std::string _name = "";
    bool _init = false;

    ID3D11Device* _device = nullptr;

    ID3D11ComputeShader* _computeShader = nullptr;
    ID3D11Buffer* _constantBuffer = nullptr;

    // Internal output. Created with B8G8R8X8/R8G8B8A8 + UAV bind. Shared
    // resources can't have UAV (DX9 has no equivalent flag), hence the
    // intermediate texture + CopyResource later.
    ID3D11Texture2D* _output = nullptr;
    ID3D11ShaderResourceView* _srvIn = nullptr;
    ID3D11UnorderedAccessView* _uavOut = nullptr;

    ID3D11Texture2D* _currentInput = nullptr;
    UINT _bufferWidth = 0;
    UINT _bufferHeight = 0;
    DXGI_FORMAT _bufferFormat = DXGI_FORMAT_UNKNOWN;

    static constexpr uint32_t InNumThreadsX = 8;
    static constexpr uint32_t InNumThreadsY = 8;

    bool InitializeViews(ID3D11Texture2D* input);

  public:
    bool CreateBufferResource(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format);
    bool Dispatch(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* input, float sharpness,
                  int debugMode);

    ID3D11Texture2D* Output() { return _output; }
    bool IsInit() const { return _init; }
    bool CanRender() const { return _init && _output != nullptr; }

    Sharpen_Dx11(std::string name, ID3D11Device* device);
    ~Sharpen_Dx11();
};
