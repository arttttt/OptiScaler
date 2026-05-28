#pragma once
#include <d3d9.h>
#include <string>

// DXBC SM2/SM3 vertex-shader bytecode utilities built on the ported DXVK
// dxso decoder. Phase 7a shipped Analyze (read-only oPos-write logging);
// Session 1+ added length/hash helpers for the wrapper, and Session 2
// added an instruction-stream comparator for round-trip verification.
// None of these mutate bytecode — re-emission goes through D3DX9Shader
// (D3DXAssembleShader), and the jitter transform lives in the device.
//
// Bytecode reference comes from DXVK's dxso decoder (zlib/libpng):
//   /tmp/dxvk/src/dxso/dxso_decoder.{h,cpp} for token bit layouts
//   /tmp/dxvk/src/dxso/dxso_enums.h        for opcode / register-type values
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

    struct StreamCompareResult
    {
        bool equal = false;
        int instructionCount = 0;     // instructions walked in `a` (excludes comments)
        int firstDivergenceIndex = -1; // instruction index where streams differ, -1 if equal
        const char* divergenceKind = ""; // short human-readable reason for the divergence
    };

    // Walks two bytecode streams with the DXVK decoder and compares them at
    // the instruction level: opcode, destination register (type/num/mask),
    // and each source register (type/num/swizzle/modifier). Comments / CTAB
    // are ignored — only executable instructions are compared, so a
    // disassemble->reassemble roundtrip that regenerates the constant table
    // still compares equal as long as the actual ops match. Used by the
    // Session 2 roundtrip verifier and (Session 3) to confirm a transform
    // changed exactly what it intended.
    static StreamCompareResult CompareInstructionStreams(const DWORD* a, const DWORD* b);

    struct RegisterUsage
    {
        bool isVertexShader = false;
        uint32_t major = 0;
        uint32_t minor = 0;
        int maxTempRegister = -1;  // highest rN referenced (-1 if none)
        int maxConstRegister = -1; // highest cN referenced (-1 if none)
        int posWrites = 0;         // writes to the clip-space position output
    };

    // Decoder pass over the original bytecode that reports the register
    // high-water marks and position-write count. Reliable numeric analysis
    // (vs. text scraping) that BuildJitteredAsm uses to pick a free temp and
    // confirm the jitter constant register doesn't collide.
    static RegisterUsage AnalyzeRegisterUsage(const DWORD* code);

    struct JitterTransformResult
    {
        bool ok = false;
        std::string asmText;       // transformed assembly (valid only if ok)
        const char* failReason = "";
        int chosenTemp = -1;       // temp register the position was redirected into
        int posWritesRedirected = 0;
    };

    // Phase 7 Session 3: text transform that injects sub-pixel jitter. Given
    // the disassembly of a vertex shader and its register usage, it:
    //   1. picks a free temp register (maxTemp+1, must fit the SM temp limit),
    //   2. redirects every write to the clip-space position output (oPos for
    //      vs_2_0, the dcl_position oN register for vs_3_0) into that temp,
    //   3. appends `mad <temp>.xy, c<jitterReg>.xy, <temp>.w, <temp>.xy` then
    //      `mov <pos>, <temp>` so the position is offset by jitter * w (clip
    //      space, before the perspective divide).
    // Fails (ok=false, original used) if there's no free temp or the shader
    // already references c<jitterReg>. The result is meant to be reassembled
    // by D3DX9Shader::Assemble.
    static JitterTransformResult BuildJitteredAsm(const std::string& disasm, const RegisterUsage& usage,
                                                  uint32_t jitterReg);
};
