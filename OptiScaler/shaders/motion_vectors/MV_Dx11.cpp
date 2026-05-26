#include "pch.h"
#include "MV_Dx11.h"

#include <Config.h>

bool MotionVectors_Dx11::CreateBufferResource(ID3D11Device* device, UINT width, UINT height)
{
    if (device == nullptr || width == 0 || height == 0)
        return false;

    if (_buffer != nullptr)
    {
        if (_bufferWidth == width && _bufferHeight == height)
            return true;

        _buffer->Release();
        _buffer = nullptr;
        if (_uavMv != nullptr)
        {
            _uavMv->Release();
            _uavMv = nullptr;
        }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &_buffer);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateTexture2D error: {1:x}", _name, (UINT) hr);
        return false;
    }

    _bufferWidth = width;
    _bufferHeight = height;
    return true;
}

bool MotionVectors_Dx11::InitializeViews(ID3D11Texture2D* depthResource)
{
    if (!_init || depthResource == nullptr || _buffer == nullptr)
        return false;

    if (depthResource != _currentDepthResource || _srvDepth == nullptr)
    {
        if (_srvDepth != nullptr)
        {
            _srvDepth->Release();
            _srvDepth = nullptr;
        }

        D3D11_TEXTURE2D_DESC desc;
        depthResource->GetDesc(&desc);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        // We currently feed R32F (Phase 3 readback uploaded as R32F); typeless
        // depth formats would need the equivalent translation done in DT_Dx11.
        srvDesc.Format = (desc.Format == DXGI_FORMAT_R32_TYPELESS) ? DXGI_FORMAT_R32_FLOAT : desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        HRESULT hr = _device->CreateShaderResourceView(depthResource, &srvDesc, &_srvDepth);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateShaderResourceView (depth) error: {1:x}", _name, (UINT) hr);
            return false;
        }
        _currentDepthResource = depthResource;
    }

    if (_uavMv == nullptr)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

        HRESULT hr = _device->CreateUnorderedAccessView(_buffer, &uavDesc, &_uavMv);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateUnorderedAccessView (mv) error: {1:x}", _name, (UINT) hr);
            return false;
        }
    }

    return true;
}

bool MotionVectors_Dx11::Dispatch(ID3D11Device* device, ID3D11DeviceContext* context,
                                  ID3D11Texture2D* depthResource, const MVConstants& constants)
{
    if (!_init || device == nullptr || context == nullptr || depthResource == nullptr)
        return false;

    _device = device;

    if (!InitializeViews(depthResource))
        return false;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] Map cbuffer error: {1:x}", _name, (UINT) hr);
        return false;
    }
    memcpy(mapped.pData, &constants, sizeof(MVConstants));
    context->Unmap(_constantBuffer, 0);

    context->CSSetShader(_computeShader, nullptr, 0);
    context->CSSetConstantBuffers(0, 1, &_constantBuffer);
    context->CSSetShaderResources(0, 1, &_srvDepth);
    context->CSSetUnorderedAccessViews(0, 1, &_uavMv, nullptr);

    const UINT dispatchWidth = (_bufferWidth + InNumThreadsX - 1) / InNumThreadsX;
    const UINT dispatchHeight = (_bufferHeight + InNumThreadsY - 1) / InNumThreadsY;
    context->Dispatch(dispatchWidth, dispatchHeight, 1);

    ID3D11UnorderedAccessView* nullUAV = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->CSSetShaderResources(0, 1, &nullSRV);

    return true;
}

MotionVectors_Dx11::MotionVectors_Dx11(std::string name, ID3D11Device* device)
    : _name(std::move(name)), _device(device)
{
    if (device == nullptr)
    {
        LOG_ERROR("device is nullptr!");
        return;
    }

    ID3DBlob* shaderBlob = MV_CompileShader(mvShaderCode.c_str(), "CSMain", "cs_5_0");
    if (shaderBlob == nullptr)
    {
        LOG_ERROR("[{0}] MV_CompileShader error", _name);
        return;
    }

    HRESULT hr = _device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr,
                                              &_computeShader);
    shaderBlob->Release();

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateComputeShader error: {1:X}", _name, (UINT) hr);
        return;
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(MVConstants);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cbDesc, nullptr, &_constantBuffer);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateBuffer (cbuffer) error: {1:X}", _name, (UINT) hr);
        return;
    }

    _init = true;
}

MotionVectors_Dx11::~MotionVectors_Dx11()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    if (_computeShader != nullptr)
        _computeShader->Release();
    if (_constantBuffer != nullptr)
        _constantBuffer->Release();
    if (_srvDepth != nullptr)
        _srvDepth->Release();
    if (_uavMv != nullptr)
        _uavMv->Release();
    if (_buffer != nullptr)
        _buffer->Release();
}
