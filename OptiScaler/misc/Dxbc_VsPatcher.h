#pragma once
#include <d3d9.h>

// Phase 7a: DXBC SM2/SM3 vertex shader bytecode reader. Read-only —
// walks the token stream and logs where each shader writes its clip-
// space output position (oPos = RasterizerOut[0]). No modification.
//
// Bytecode reference comes from DXVK's dxso decoder (BSD-2-Clause):
//   /tmp/dxvk/src/dxso/dxso_decoder.{h,cpp} for token bit layouts
//   /tmp/dxvk/src/dxso/dxso_enums.h        for opcode / register-type values
//
// Once Phase 7b lands a transformer that re-emits patched bytecode, this
// module gains an Analyze→Patch path that uses the same parser.
class DxbcVsPatcher
{
  public:
    // Walk the bytecode pointed to by `code` (terminated by an End token
    // 0xFFFF). `tag` is a short label for log lines so multiple shaders
    // can be distinguished. Safe to call with nullptr — early-returns.
    static void Analyze(const DWORD* code, const char* tag);

    // Walks the token stream until the End token (0xFFFF) and returns the
    // total DWORD count including that token. Returns 0 if the bytecode is
    // malformed (no End within sanity limit or null input).
    static size_t BytecodeDwordLength(const DWORD* code);

    // FNV-64 hash of the bytecode. 3DMigoto uses the same hash as cache /
    // logging key — collisions are essentially impossible for shader-sized
    // payloads, so we use it the same way for shader-identifying logs.
    static uint64_t HashBytecode(const DWORD* code, size_t dwordLen);
};
