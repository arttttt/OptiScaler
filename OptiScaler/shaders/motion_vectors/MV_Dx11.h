#pragma once
#include "SysUtils.h"
#include "MV_Common.h"
#include <d3d11.h>

class MotionVectors_Dx11
{
  private:
    std::string _name = "";
    bool _init = false;

    ID3D11Device* _device = nullptr;

    ID3D11ComputeShader* _computeShader = nullptr;
    ID3D11Buffer* _constantBuffer = nullptr;

    ID3D11Texture2D* _buffer = nullptr;
    ID3D11ShaderResourceView* _srvDepth = nullptr;
    ID3D11UnorderedAccessView* _uavMv = nullptr;

    ID3D11Texture2D* _currentDepthResource = nullptr;
    UINT _bufferWidth = 0;
    UINT _bufferHeight = 0;

    static constexpr uint32_t InNumThreadsX = 16;
    static constexpr uint32_t InNumThreadsY = 16;

    bool InitializeViews(ID3D11Texture2D* depthResource);

  public:
    bool CreateBufferResource(ID3D11Device* device, UINT width, UINT height);
    bool Dispatch(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* depthResource,
                  const MVConstants& constants);

    ID3D11Texture2D* Buffer() { return _buffer; }
    UINT Width() const { return _bufferWidth; }
    UINT Height() const { return _bufferHeight; }
    bool IsInit() const { return _init; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    MotionVectors_Dx11(std::string name, ID3D11Device* device);
    ~MotionVectors_Dx11();
};
