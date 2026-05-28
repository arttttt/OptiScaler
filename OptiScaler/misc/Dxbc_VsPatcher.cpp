#include "pch.h"
#include "Dxbc_VsPatcher.h"

#include "dxbc/dxso_decoder.h"

#include <algorithm>
#include <cctype>
#include <sstream>

size_t DxbcVsPatcher::BytecodeDwordLength(const DWORD* code)
{
    if (code == nullptr)
        return 0;

    constexpr size_t kSanityLimit = 65536u;
    size_t i = 0;
    // Skip header.
    if (i >= kSanityLimit)
        return 0;
    ++i;

    while (i < kSanityLimit)
    {
        const uint32_t token = static_cast<uint32_t>(code[i]);
        const uint32_t opcode = token & 0x0000FFFFu;

        if (opcode == 0xFFFFu)
            return i + 1; // include the End token

        if (opcode == 0xFFFEu)
        {
            // Comment encodes its length in upper bits of the opcode token.
            i += 1u + ((token & 0x7FFF0000u) >> 16);
            continue;
        }

        if (opcode == 0xFFFDu)
        {
            // Phase: lone token, no operands.
            i += 1u;
            continue;
        }

        // SM2+ instruction length lives in bits 24-27 of the opcode token.
        const uint32_t length = (token & 0x0F000000u) >> 24;
        i += 1u + length;
    }

    return 0; // no End token within sanity bound
}

uint64_t DxbcVsPatcher::HashBytecode(const DWORD* code, size_t dwordLen)
{
    if (code == nullptr || dwordLen == 0)
        return 0;

    // FNV-1a 64-bit. Same hash family 3DMigoto uses for its shader cache.
    constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ull;
    constexpr uint64_t kFnvPrime       = 0x00000100000001B3ull;

    uint64_t hash = kFnvOffsetBasis;
    const auto* bytes = reinterpret_cast<const unsigned char*>(code);
    const size_t byteLen = dwordLen * sizeof(DWORD);
    for (size_t i = 0; i < byteLen; ++i)
    {
        hash ^= static_cast<uint64_t>(bytes[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

void DxbcVsPatcher::Analyze(const DWORD* code, const char* tag)
{
    if (code == nullptr)
        return;

    // DWORD (unsigned long) and uint32_t (unsigned int) are the same size on
    // Windows but distinct types under MSVC, so the pointer cast is required.
    const uint32_t* tokens = reinterpret_cast<const uint32_t*>(code);

    dxvk::DxsoProgramInfo info;
    if (!dxvk::DxsoDecodeHeader(tokens[0], info))
    {
        LOG_WARN("Phase 7a [{}]: header not VS/PS — skipping", tag);
        return;
    }
    if (info.type() != dxvk::DxsoProgramType::VertexShader)
        return;

    LOG_INFO("Phase 7a [{}]: vs_{}_{} bytecode", tag, info.majorVersion(), info.minorVersion());

    dxvk::DxsoDecodeContext decoder(info);
    dxvk::DxsoCodeIter iter(tokens + 1);

    uint32_t opCount = 0;
    uint32_t oPosWrites = 0;

    while (decoder.decodeInstruction(iter))
    {
        const auto& ctx = decoder.getInstructionContext();
        ++opCount;

        const bool isDst = ctx.dst.id.type == dxvk::DxsoRegisterType::RasterizerOut &&
                           ctx.dst.id.num == dxvk::RasterOutPosition;
        if (!isDst)
            continue;

        ++oPosWrites;
        const auto opcodeNum = static_cast<uint32_t>(ctx.instruction.opcode);
        const uint32_t mask  = ctx.dst.mask.raw();
        LOG_INFO("Phase 7a [{}]:   oPos write #{}: opcode={}, mask=0x{:X}", tag, oPosWrites, opcodeNum, mask);
    }

    LOG_INFO("Phase 7a [{}]: end, {} ops, {} oPos writes", tag, opCount, oPosWrites);
}

namespace
{
    // Advances `decoder`/`iter` to the next executable instruction, skipping
    // Comment (and Phase) tokens. On success returns true and reports the
    // instruction's token span [spanBegin, spanBegin+spanLen). Returns false
    // when the End token is reached.
    bool NextRealInstruction(dxvk::DxsoDecodeContext& decoder, dxvk::DxsoCodeIter& iter,
                             const uint32_t** spanBegin, uint32_t* spanLen)
    {
        while (true)
        {
            const uint32_t* before = iter.ptrAt(0);
            if (!decoder.decodeInstruction(iter))
                return false; // End

            const auto opcode = decoder.getInstructionContext().instruction.opcode;
            if (opcode == dxvk::DxsoOpcode::Comment || opcode == dxvk::DxsoOpcode::Phase)
                continue; // metadata, not executable — skip

            const uint32_t* after = iter.ptrAt(0);
            *spanBegin = before;
            *spanLen = static_cast<uint32_t>(after - before);
            return true;
        }
    }
}

DxbcVsPatcher::StreamCompareResult DxbcVsPatcher::CompareInstructionStreams(const DWORD* a, const DWORD* b)
{
    StreamCompareResult result;

    if (a == nullptr || b == nullptr)
    {
        result.divergenceKind = "null bytecode";
        return result;
    }

    const uint32_t* ta = reinterpret_cast<const uint32_t*>(a);
    const uint32_t* tb = reinterpret_cast<const uint32_t*>(b);

    dxvk::DxsoProgramInfo infoA;
    dxvk::DxsoProgramInfo infoB;
    if (!dxvk::DxsoDecodeHeader(ta[0], infoA) || !dxvk::DxsoDecodeHeader(tb[0], infoB))
    {
        result.divergenceKind = "invalid header";
        return result;
    }
    if (infoA.type() != infoB.type() ||
        infoA.majorVersion() != infoB.majorVersion() ||
        infoA.minorVersion() != infoB.minorVersion())
    {
        result.divergenceKind = "version/type mismatch";
        return result;
    }

    dxvk::DxsoDecodeContext decA(infoA);
    dxvk::DxsoDecodeContext decB(infoB);
    dxvk::DxsoCodeIter itA(ta + 1);
    dxvk::DxsoCodeIter itB(tb + 1);

    int index = 0;
    while (true)
    {
        const uint32_t* beginA = nullptr;
        const uint32_t* beginB = nullptr;
        uint32_t lenA = 0;
        uint32_t lenB = 0;

        const bool hasA = NextRealInstruction(decA, itA, &beginA, &lenA);
        const bool hasB = NextRealInstruction(decB, itB, &beginB, &lenB);

        if (!hasA && !hasB)
        {
            // Both streams ended at the same point — equal.
            result.equal = true;
            result.instructionCount = index;
            return result;
        }
        if (hasA != hasB)
        {
            result.firstDivergenceIndex = index;
            result.instructionCount = index;
            result.divergenceKind = hasA ? "b ended early" : "a ended early";
            return result;
        }

        // Both have an instruction. A faithful encoding of the same op
        // produces identical tokens, so compare the spans byte-for-byte.
        if (lenA != lenB)
        {
            result.firstDivergenceIndex = index;
            result.instructionCount = index;
            result.divergenceKind = "instruction length differs";
            return result;
        }
        for (uint32_t i = 0; i < lenA; ++i)
        {
            if (beginA[i] != beginB[i])
            {
                result.firstDivergenceIndex = index;
                result.instructionCount = index;
                result.divergenceKind = "instruction tokens differ";
                return result;
            }
        }

        ++index;
    }
}

DxbcVsPatcher::RegisterUsage DxbcVsPatcher::AnalyzeRegisterUsage(const DWORD* code)
{
    RegisterUsage usage;
    if (code == nullptr)
        return usage;

    const uint32_t* tokens = reinterpret_cast<const uint32_t*>(code);
    dxvk::DxsoProgramInfo info;
    if (!dxvk::DxsoDecodeHeader(tokens[0], info))
        return usage;

    usage.isVertexShader = (info.type() == dxvk::DxsoProgramType::VertexShader);
    usage.major = info.majorVersion();
    usage.minor = info.minorVersion();

    dxvk::DxsoDecodeContext decoder(info);
    dxvk::DxsoCodeIter iter(tokens + 1);

    auto consider = [&usage](const dxvk::DxsoRegister& reg)
    {
        if (reg.id.type == dxvk::DxsoRegisterType::Temp)
            usage.maxTempRegister = std::max(usage.maxTempRegister, static_cast<int>(reg.id.num));
        else if (reg.id.type == dxvk::DxsoRegisterType::Const)
            usage.maxConstRegister = std::max(usage.maxConstRegister, static_cast<int>(reg.id.num));
    };

    while (decoder.decodeInstruction(iter))
    {
        const auto& ctx = decoder.getInstructionContext();
        const auto op = ctx.instruction.opcode;
        if (op == dxvk::DxsoOpcode::Comment || op == dxvk::DxsoOpcode::Phase)
            continue;

        // dst: for `def cN, ...` the destination IS a const register, so this
        // correctly counts defined constants toward maxConstRegister.
        consider(ctx.dst);
        if (ctx.dst.id.type == dxvk::DxsoRegisterType::RasterizerOut &&
            ctx.dst.id.num == dxvk::RasterOutPosition)
            usage.posWrites++;

        for (uint32_t i = 0; i < ctx.srcCount && i < ctx.src.size(); ++i)
            consider(ctx.src[i]);
    }

    return usage;
}

namespace
{
    bool IsAsmTokenChar(char c)
    {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    // Replaces every whole-token occurrence of `token` in `s` with `repl`,
    // returning how many were replaced. A "whole token" is bounded by chars
    // that aren't part of an identifier, so replacing "oPos" never touches a
    // substring, and replacing "o0" never matches "o1".
    int ReplaceWholeToken(std::string& s, const std::string& token, const std::string& repl)
    {
        int count = 0;
        size_t pos = 0;
        while ((pos = s.find(token, pos)) != std::string::npos)
        {
            const bool leftOk = (pos == 0) || !IsAsmTokenChar(s[pos - 1]);
            const size_t end = pos + token.size();
            const bool rightOk = (end >= s.size()) || !IsAsmTokenChar(s[end]);
            if (leftOk && rightOk)
            {
                s.replace(pos, token.size(), repl);
                pos += repl.size();
                ++count;
            }
            else
            {
                pos += token.size();
            }
        }
        return count;
    }
}

int DxbcVsPatcher::FindViewProjRegister(const std::string& disasm)
{
    // The D3DXDisassembleShader header lists constants under "Registers:" as
    //   //   <name>   c<reg>   <size>
    // Pick a 4-register (4x4 matrix) float constant whose name looks like a
    // view-projection. Prefer a plain view-projection over a world/model one
    // if both appear, but for the camera VP we want the matrix world geometry
    // multiplies by — which on Model=identity draws is the same thing.
    std::istringstream stream(disasm);
    std::string line;
    bool inRegisters = false;

    while (std::getline(stream, line))
    {
        if (line.find("Registers:") != std::string::npos)
        {
            inRegisters = true;
            continue;
        }
        if (!inRegisters)
            continue;

        const size_t comment = line.find("//");
        if (comment == std::string::npos)
            continue;

        // Tokenise the comment body: [name, cReg, ..., size].
        std::istringstream ls(line.substr(comment + 2));
        std::vector<std::string> toks;
        std::string t;
        while (ls >> t)
            toks.push_back(t);
        if (toks.size() < 3)
            continue;

        const std::string& name = toks.front();
        const std::string& regTok = toks[1];
        const int size = std::atoi(toks.back().c_str());

        if (size < 4 || regTok.size() < 2 || regTok[0] != 'c')
            continue;

        std::string low = name;
        for (auto& ch : low)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const bool looksVP = low.find("viewproj") != std::string::npos || low.find("worldviewproj") != std::string::npos ||
                             low.find("wvp") != std::string::npos || low.find("mvp") != std::string::npos;
        if (!looksVP)
            continue;

        const int reg = std::atoi(regTok.c_str() + 1);
        if (reg >= 0)
            return reg;
    }

    return -1;
}

DxbcVsPatcher::JitterTransformResult DxbcVsPatcher::BuildJitteredAsm(
    const std::string& disasm, const RegisterUsage& usage, uint32_t jitterReg)
{
    JitterTransformResult r;

    if (!usage.isVertexShader)
    {
        r.failReason = "not a vertex shader";
        return r;
    }

    // vs_2_0 has 12 temp registers (r0-r11), vs_3_0 has 32 (r0-r31).
    const int tempLimit = (usage.major >= 3) ? 32 : 12;
    const int freeTemp = usage.maxTempRegister + 1;
    if (freeTemp >= tempLimit)
    {
        r.failReason = "no free temp register";
        return r;
    }
    if (usage.maxConstRegister >= static_cast<int>(jitterReg))
    {
        r.failReason = "shader references jitter const register";
        return r;
    }

    // Position OUTPUT register. vs_2_0 always writes the named, undeclared
    // oPos register. vs_3_0 writes a generic o-register declared with
    // dcl_position — but note vs_3_0 ALSO declares the INPUT position with
    // `dcl_position v0`, so we must pick the declaration whose register is an
    // output (starts with 'o'), not the input (v). Getting this wrong
    // redirects the input read and writes a read-only input register, which
    // the runtime rejects (the bug that made Session 3's first build fail).
    std::string posReg;
    if (usage.major >= 3)
    {
        size_t p = disasm.find("dcl_position");
        while (p != std::string::npos)
        {
            const size_t after = p + 12; // strlen("dcl_position")
            // Reject dcl_positiont (PositionT) — the next char must be space.
            if (after < disasm.size() && (disasm[after] == ' ' || disasm[after] == '\t'))
            {
                size_t s = after;
                while (s < disasm.size() && (disasm[s] == ' ' || disasm[s] == '\t'))
                    ++s;
                // Output registers begin with 'o'; inputs begin with 'v'.
                if (s < disasm.size() && disasm[s] == 'o')
                {
                    size_t e = s;
                    while (e < disasm.size() && IsAsmTokenChar(disasm[e]))
                        ++e;
                    posReg = disasm.substr(s, e - s);
                    break;
                }
            }
            p = disasm.find("dcl_position", p + 1);
        }
        if (posReg.empty())
        {
            r.failReason = "vs_3_0 position output register not found";
            return r;
        }
    }
    else
    {
        posReg = "oPos";
    }

    const std::string tempName = "r" + std::to_string(freeTemp);

    std::string out;
    out.reserve(disasm.size() + 128);
    int redirected = 0;

    std::istringstream stream(disasm);
    std::string line;
    while (std::getline(stream, line))
    {
        // Don't rewrite inside comments or the dcl_position declaration (the
        // declaration must keep naming the real output register).
        const size_t commentPos = line.find("//");
        std::string code = (commentPos == std::string::npos) ? line : line.substr(0, commentPos);
        const std::string comment = (commentPos == std::string::npos) ? std::string() : line.substr(commentPos);

        if (code.find("dcl_") == std::string::npos)
            redirected += ReplaceWholeToken(code, posReg, tempName);

        out += code;
        out += comment;
        out += '\n';
    }

    if (redirected == 0)
    {
        r.failReason = "no position writes found";
        return r;
    }

    // Offset the clip-space position by jitter * w (so the post-divide NDC
    // shift is constant in pixels), then write it out. oPos/oN are write-only
    // in SM2/SM3, which is exactly why the value had to be staged in a temp.
    out += "mad " + tempName + ".xy, c" + std::to_string(jitterReg) + ".xy, " + tempName + ".w, " + tempName +
           ".xy\n";
    out += "mov " + posReg + ", " + tempName + "\n";

    r.ok = true;
    r.asmText = std::move(out);
    r.chosenTemp = freeTemp;
    r.posWritesRedirected = redirected;
    return r;
}
