#include "pch.h"
#include "Sharpen_Dx11.h"

#include <Config.h>

bool Sharpen_Dx11::CreateBufferResource(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format)
{
    if (device == nullptr || width == 0 || height == 0 || format == DXGI_FORMAT_UNKNOWN)
        return false;

    if (_output != nullptr)
    {
        if (_bufferWidth == width && _bufferHeight == height && _bufferFormat == format)
            return true;
        _output->Release();
        _output = nullptr;
        if (_uavOut)
        {
            _uavOut->Release();
            _uavOut = nullptr;
        }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &_output);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateTexture2D error: {1:x}", _name, (UINT) hr);
        return false;
    }

    _bufferWidth = width;
    _bufferHeight = height;
    _bufferFormat = format;
    return true;
}

bool Sharpen_Dx11::InitializeViews(ID3D11Texture2D* input)
{
    if (!_init || _output == nullptr || input == nullptr)
        return false;

    if (input != _currentInput || _srvIn == nullptr)
    {
        if (_srvIn != nullptr)
        {
            _srvIn->Release();
            _srvIn = nullptr;
        }

        D3D11_TEXTURE2D_DESC desc;
        input->GetDesc(&desc);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        HRESULT hr = _device->CreateShaderResourceView(input, &srvDesc, &_srvIn);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateShaderResourceView error: {1:x}", _name, (UINT) hr);
            return false;
        }
        _currentInput = input;
    }

    if (_uavOut == nullptr)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = _bufferFormat;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

        HRESULT hr = _device->CreateUnorderedAccessView(_output, &uavDesc, &_uavOut);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateUnorderedAccessView error: {1:x}", _name, (UINT) hr);
            return false;
        }
    }

    return true;
}

bool Sharpen_Dx11::Dispatch(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* input,
                            float sharpness)
{
    if (!_init || device == nullptr || context == nullptr || input == nullptr || _output == nullptr)
        return false;

    _device = device;

    if (!InitializeViews(input))
        return false;

    SharpenConstants constants {};
    constants.Sharpness = sharpness;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] Map cbuffer error: {1:x}", _name, (UINT) hr);
        return false;
    }
    memcpy(mapped.pData, &constants, sizeof(SharpenConstants));
    context->Unmap(_constantBuffer, 0);

    context->CSSetShader(_computeShader, nullptr, 0);
    context->CSSetConstantBuffers(0, 1, &_constantBuffer);
    context->CSSetShaderResources(0, 1, &_srvIn);
    context->CSSetUnorderedAccessViews(0, 1, &_uavOut, nullptr);

    const UINT dispatchWidth = (_bufferWidth + InNumThreadsX - 1) / InNumThreadsX;
    const UINT dispatchHeight = (_bufferHeight + InNumThreadsY - 1) / InNumThreadsY;
    context->Dispatch(dispatchWidth, dispatchHeight, 1);

    ID3D11UnorderedAccessView* nullUAV = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->CSSetShaderResources(0, 1, &nullSRV);

    return true;
}

Sharpen_Dx11::Sharpen_Dx11(std::string name, ID3D11Device* device) : _name(std::move(name)), _device(device)
{
    if (device == nullptr)
    {
        LOG_ERROR("device is nullptr!");
        return;
    }

    ID3DBlob* shaderBlob = Sharpen_CompileShader(sharpenShaderCode.c_str(), "CSMain", "cs_5_0");
    if (shaderBlob == nullptr)
    {
        LOG_ERROR("[{0}] Sharpen_CompileShader error", _name);
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
    cbDesc.ByteWidth = sizeof(SharpenConstants);
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

Sharpen_Dx11::~Sharpen_Dx11()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    if (_computeShader) _computeShader->Release();
    if (_constantBuffer) _constantBuffer->Release();
    if (_srvIn) _srvIn->Release();
    if (_uavOut) _uavOut->Release();
    if (_output) _output->Release();
}
