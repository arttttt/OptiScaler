#include "pch.h"
#include "Dxbc_VsPatcher.h"

#include "dxbc/dxso_decoder.h"

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
