#pragma once

#include <d3d9.h>
#include <cstdint>
#include <string>
#include <vector>

// Thin wrapper over D3DXDisassembleShader / D3DXAssembleShader from
// d3dx9_43.dll, loaded dynamically so we never link against the legacy
// D3DX SDK. This is the disassemble<->assemble pipeline 3DMigoto and
// HelixMod use for D3D9 shader patching: bytecode -> asm text -> bytecode,
// with the assembler validating our edits for us.
//
// d3dx9_43.dll ships with the DirectX End-User Runtime (June 2010) and is
// present on the vast majority of machines that run DX9-era games. If it
// isn't loadable, Available() returns false and the caller falls back to
// using the original (unpatched) shader.
namespace D3DX9Shader
{
    // Lazily loads d3dx9_43.dll and resolves the two entry points on first
    // call. Result is cached for the process lifetime. Thread-safe.
    bool Available();

    // bytecode -> assembly text. Returns false (and leaves outText empty)
    // if D3DX9 is unavailable or the disassembler rejects the input.
    bool Disassemble(const DWORD* bytecode, std::string& outText);

    // assembly text -> bytecode. On failure returns false and fills
    // outError with the assembler's message (or an hr string). The DWORD
    // vector receives the assembled shader on success.
    bool Assemble(const std::string& asmText, std::vector<DWORD>& outBytecode, std::string& outError);
}
