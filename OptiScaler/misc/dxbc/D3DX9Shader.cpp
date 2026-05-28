#include "pch.h"
#include "D3DX9Shader.h"

#include <mutex>

namespace
{
    // Minimal ID3DXBuffer so we don't have to include <d3dx9.h> (which would
    // pull in the legacy D3DX SDK and a link-time dependency). The vtable
    // layout is fixed by the D3DX9 ABI: IUnknown methods, then
    // GetBufferPointer / GetBufferSize.
    struct ID3DXBuffer : public IUnknown
    {
        virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
        virtual DWORD STDMETHODCALLTYPE GetBufferSize() = 0;
    };

    using D3DXDisassembleShader_pfn = HRESULT(WINAPI*)(const DWORD* pShader, BOOL EnableColorCode,
                                                       LPCSTR pComments, ID3DXBuffer** ppDisassembly);

    // pDefines (D3DXMACRO*) and pInclude (LPD3DXINCLUDE) are always null for
    // us, so we type them as const void* / void* to avoid the SDK headers.
    using D3DXAssembleShader_pfn = HRESULT(WINAPI*)(LPCSTR pSrcData, UINT SrcDataLen,
                                                    const void* pDefines, void* pInclude, DWORD Flags,
                                                    ID3DXBuffer** ppShader, ID3DXBuffer** ppErrorMsgs);

    std::once_flag g_initOnce;
    HMODULE g_d3dx9 = nullptr;
    D3DXDisassembleShader_pfn g_disassemble = nullptr;
    D3DXAssembleShader_pfn g_assemble = nullptr;
    bool g_available = false;

    void InitOnce()
    {
        g_d3dx9 = LoadLibraryA("d3dx9_43.dll");
        if (g_d3dx9 == nullptr)
        {
            LOG_WARN("D3DX9Shader: d3dx9_43.dll not loadable — shader patching disabled "
                     "(install the DirectX End-User Runtime to enable it)");
            return;
        }

        g_disassemble = reinterpret_cast<D3DXDisassembleShader_pfn>(
            GetProcAddress(g_d3dx9, "D3DXDisassembleShader"));
        g_assemble = reinterpret_cast<D3DXAssembleShader_pfn>(
            GetProcAddress(g_d3dx9, "D3DXAssembleShader"));

        if (g_disassemble == nullptr || g_assemble == nullptr)
        {
            LOG_WARN("D3DX9Shader: d3dx9_43.dll missing D3DXDisassembleShader/D3DXAssembleShader "
                     "(disasm={}, asm={})",
                     reinterpret_cast<void*>(g_disassemble), reinterpret_cast<void*>(g_assemble));
            FreeLibrary(g_d3dx9);
            g_d3dx9 = nullptr;
            g_disassemble = nullptr;
            g_assemble = nullptr;
            return;
        }

        g_available = true;
        LOG_INFO("D3DX9Shader: d3dx9_43.dll loaded, disasm/asm entry points resolved");
    }
}

namespace D3DX9Shader
{
    bool Available()
    {
        std::call_once(g_initOnce, InitOnce);
        return g_available;
    }

    bool Disassemble(const DWORD* bytecode, std::string& outText)
    {
        outText.clear();
        if (!Available() || bytecode == nullptr)
            return false;

        ID3DXBuffer* buffer = nullptr;
        const HRESULT hr = g_disassemble(bytecode, FALSE, nullptr, &buffer);
        if (FAILED(hr) || buffer == nullptr)
        {
            LOG_WARN("D3DX9Shader::Disassemble failed: hr=0x{:08X}", static_cast<uint32_t>(hr));
            if (buffer != nullptr)
                buffer->Release();
            return false;
        }

        const char* text = static_cast<const char*>(buffer->GetBufferPointer());
        const DWORD size = buffer->GetBufferSize();
        if (text != nullptr && size > 0)
        {
            // D3DX buffers are NUL-terminated, but size includes the NUL; trim
            // any trailing NULs so the string compares cleanly.
            size_t len = size;
            while (len > 0 && text[len - 1] == '\0')
                --len;
            outText.assign(text, len);
        }

        buffer->Release();
        return !outText.empty();
    }

    bool Assemble(const std::string& asmText, std::vector<DWORD>& outBytecode, std::string& outError)
    {
        outBytecode.clear();
        outError.clear();
        if (!Available())
        {
            outError = "d3dx9_43.dll unavailable";
            return false;
        }
        if (asmText.empty())
        {
            outError = "empty assembly text";
            return false;
        }

        ID3DXBuffer* shader = nullptr;
        ID3DXBuffer* errors = nullptr;
        const HRESULT hr = g_assemble(asmText.c_str(), static_cast<UINT>(asmText.size()), nullptr, nullptr,
                                      0, &shader, &errors);

        if (FAILED(hr) || shader == nullptr)
        {
            if (errors != nullptr && errors->GetBufferPointer() != nullptr)
                outError.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            else
                outError = "hr=0x" + std::to_string(static_cast<uint32_t>(hr));

            if (shader != nullptr)
                shader->Release();
            if (errors != nullptr)
                errors->Release();
            return false;
        }

        const auto* words = static_cast<const DWORD*>(shader->GetBufferPointer());
        const DWORD byteSize = shader->GetBufferSize();
        if (words != nullptr && byteSize >= sizeof(DWORD))
            outBytecode.assign(words, words + (byteSize / sizeof(DWORD)));

        shader->Release();
        if (errors != nullptr)
            errors->Release();

        if (outBytecode.empty())
        {
            outError = "assembler returned empty bytecode";
            return false;
        }
        return true;
    }
}
