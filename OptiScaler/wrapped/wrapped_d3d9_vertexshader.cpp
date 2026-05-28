#include <pch.h>
#include "wrapped_d3d9_vertexshader.h"

WrappedVertexShader9::WrappedVertexShader9(IDirect3DVertexShader9* real, const DWORD* origBytecode,
                                           size_t bytecodeDwordLen, uint64_t fnv64)
    : _real(real), _hash(fnv64)
{
    if (origBytecode != nullptr && bytecodeDwordLen > 0)
    {
        _origBytecode.assign(origBytecode, origBytecode + bytecodeDwordLen);
    }
}

WrappedVertexShader9::~WrappedVertexShader9()
{
    // Release in the opposite order to the AddRef chain: patched first
    // (it's a sibling resource we own), then the original.
    if (_patched != nullptr)
    {
        _patched->Release();
        _patched = nullptr;
    }
    if (_real != nullptr)
    {
        _real->Release();
        _real = nullptr;
    }
}

HRESULT STDMETHODCALLTYPE WrappedVertexShader9::QueryInterface(REFIID riid, void** ppvObj)
{
    if (ppvObj == nullptr)
        return E_POINTER;

    // Our own IID lets the device unwrap us back to _real / _patched before
    // forwarding into the underlying device. Standard shader IIDs return
    // ourselves so the game sees the wrapper uniformly.
    if (riid == __uuidof(WrappedVertexShader9) ||
        riid == __uuidof(IUnknown) ||
        riid == __uuidof(IDirect3DVertexShader9))
    {
        AddRef();
        *ppvObj = this;
        return S_OK;
    }

    return _real->QueryInterface(riid, ppvObj);
}

ULONG STDMETHODCALLTYPE WrappedVertexShader9::AddRef()
{
    // We do NOT mirror AddRef into _real here. _real's refcount is held by us
    // at +1 across the wrapper's lifetime (acquired by CreateVertexShader);
    // wrapper destruction is the only event that drops _real's ref.
    return InterlockedIncrement(&_refcount);
}

ULONG STDMETHODCALLTYPE WrappedVertexShader9::Release()
{
    const ULONG ref = InterlockedDecrement(&_refcount);
    if (ref == 0)
        delete this;
    return ref;
}

HRESULT STDMETHODCALLTYPE WrappedVertexShader9::GetDevice(IDirect3DDevice9** ppDevice)
{
    return _real->GetDevice(ppDevice);
}

HRESULT STDMETHODCALLTYPE WrappedVertexShader9::GetFunction(void* pData, UINT* pSizeOfData)
{
    // Always return the original bytecode the game gave us, never the
    // patched variant. Games that hash GetFunction output to detect shader
    // tampering must see what they uploaded.
    return _real->GetFunction(pData, pSizeOfData);
}

void WrappedVertexShader9::SetPatched(IDirect3DVertexShader9* patched, uint32_t jitterConstSlot, bool jittered)
{
    if (_patched != nullptr)
        _patched->Release();
    _patched = patched;
    _jitterConstSlot = jitterConstSlot;
    _jittered = jittered;
    _state = PatchState::PatchedOk;
}

void WrappedVertexShader9::MarkPatchFailed()
{
    _state = PatchState::PatchFailed;
}
