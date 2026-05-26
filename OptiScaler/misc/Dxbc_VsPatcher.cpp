#include "pch.h"
#include "Dxbc_VsPatcher.h"

#include "dxbc/dxso_decoder.h"

void DxbcVsPatcher::Analyze(const DWORD* code, const char* tag)
{
    if (code == nullptr)
        return;

    dxvk::DxsoProgramInfo info;
    if (!dxvk::DxsoDecodeHeader(code[0], info))
    {
        LOG_WARN("Phase 7a [{}]: header not VS/PS — skipping", tag);
        return;
    }
    if (info.type() != dxvk::DxsoProgramType::VertexShader)
        return;

    LOG_INFO("Phase 7a [{}]: vs_{}_{} bytecode", tag, info.majorVersion(), info.minorVersion());

    dxvk::DxsoDecodeContext decoder(info);
    dxvk::DxsoCodeIter iter(code + 1);

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
