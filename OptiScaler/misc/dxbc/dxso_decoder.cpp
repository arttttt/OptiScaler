// Ported from DXVK src/dxso/dxso_decoder.cpp (zlib/libpng license, see
// dxso_decoder.h for full attribution). Two functional deviations from
// the original:
//   - DxvkError -> we just return false from decodeInstruction; the caller
//     can decide what to do. Real DX9 games we target don't hit the bad
//     path (source-register overrun is reserved for malformed bytecode).
//   - SM1.x default opcode length table is gone — DX9-era games use SM2
//     or SM3 where the length is in token bits 24-27.

#include "pch.h"
#include "dxso_decoder.h"

namespace dxvk {

  uint32_t DxsoDecodeContext::decodeInstructionLength(uint32_t token) {
    const auto opcode = m_ctx.instruction.opcode;

    if (opcode == DxsoOpcode::Comment)
      return (token & 0x7fff0000) >> 16;

    if (opcode == DxsoOpcode::End)
      return 0;

    if (opcode == DxsoOpcode::Phase)
      return 0;

    // SM2/SM3 always carry the length in bits 24-27.
    return (token & 0x0f000000) >> 24;
  }

  bool DxsoDecodeContext::relativeAddressingUsesToken(DxsoInstructionArgumentType type) {
    const auto& info = this->getProgramInfo();
    return (info.majorVersion() >= 2 && type == DxsoInstructionArgumentType::Source)
        || (info.majorVersion() >= 3 && type == DxsoInstructionArgumentType::Destination);
  }

  void DxsoDecodeContext::decodeDeclaration(DxsoCodeIter& iter) {
    uint32_t dclToken = iter.read();
    m_ctx.dcl.textureType         = (dclToken & 0x78000000) >> 27;
    m_ctx.dcl.semantic.usage      = static_cast<DxsoUsage>(dclToken & 0x0000000f);
    m_ctx.dcl.semantic.usageIndex = (dclToken & 0x000f0000) >> 16;
  }

  void DxsoDecodeContext::decodeDefinition(DxsoOpcode /*opcode*/, DxsoCodeIter& iter) {
    const uint32_t n = (m_ctx.instruction.tokenLength > 1u)
                        ? (m_ctx.instruction.tokenLength - 1u) : 0u;
    const uint32_t cap = n < 4u ? n : 4u;
    for (uint32_t i = 0; i < cap; i++)
      m_ctx.def.uint32[i] = iter.read();
  }

  void DxsoDecodeContext::decodeBaseRegister(DxsoBaseRegister& reg, uint32_t token) {
    reg.id.type = static_cast<DxsoRegisterType>(
        ((token & 0x00001800) >> 8)
      | ((token & 0x70000000) >> 28));
    reg.id.num = token & 0x000007ff;
  }

  void DxsoDecodeContext::decodeGenericRegister(DxsoRegister& reg, uint32_t token) {
    this->decodeBaseRegister(reg, token);
    reg.hasRelative      = (token & (1 << 13)) == 8192;
    reg.relative.id      = DxsoRegisterId{ DxsoRegisterType::Addr, 0 };
    reg.relative.swizzle = DxsoRegSwizzle(0xE4);
    reg.centroid         = (token & (4 << 20)) != 0;
    reg.partialPrecision = (token & (2 << 20)) != 0;
  }

  void DxsoDecodeContext::decodeRelativeRegister(DxsoBaseRegister& reg, uint32_t token) {
    this->decodeBaseRegister(reg, token);
    reg.swizzle = DxsoRegSwizzle(uint8_t((token & 0x00ff0000) >> 16));
  }

  bool DxsoDecodeContext::decodeDestinationRegister(DxsoCodeIter& iter) {
    uint32_t token = iter.read();
    this->decodeGenericRegister(m_ctx.dst, token);

    m_ctx.dst.mask     = DxsoRegMask(uint8_t((token & 0x000f0000) >> 16));
    m_ctx.dst.saturate = (token & (1 << 20)) != 0;
    m_ctx.dst.shift    = (token & 0x0f000000) >> 24;
    m_ctx.dst.shift    = (m_ctx.dst.shift & 0x7) - (m_ctx.dst.shift & 0x8);

    const bool extraToken = relativeAddressingUsesToken(DxsoInstructionArgumentType::Destination);
    if (m_ctx.dst.hasRelative && extraToken) {
      this->decodeRelativeRegister(m_ctx.dst.relative, iter.read());
      return true;
    }
    return false;
  }

  bool DxsoDecodeContext::decodeSourceRegister(uint32_t i, DxsoCodeIter& iter) {
    if (i >= m_ctx.src.size()) {
      LOG_ERROR("DxsoDecodeContext::decodeSourceRegister: index {} out of range", i);
      return false;
    }

    uint32_t token = iter.read();
    this->decodeGenericRegister(m_ctx.src[i], token);
    m_ctx.src[i].swizzle  = DxsoRegSwizzle(uint8_t((token & 0x00ff0000) >> 16));
    m_ctx.src[i].modifier = static_cast<DxsoRegModifier>((token & 0x0f000000) >> 24);

    const bool extraToken = relativeAddressingUsesToken(DxsoInstructionArgumentType::Source);
    if (m_ctx.src[i].hasRelative && extraToken) {
      this->decodeRelativeRegister(m_ctx.src[i].relative, iter.read());
      return true;
    }
    return false;
  }

  void DxsoDecodeContext::decodePredicateRegister(DxsoCodeIter& iter) {
    uint32_t token = iter.read();
    this->decodeGenericRegister(m_ctx.pred, token);
    m_ctx.pred.swizzle  = DxsoRegSwizzle(uint8_t((token & 0x00ff0000) >> 16));
    m_ctx.pred.modifier = static_cast<DxsoRegModifier>((token & 0x0f000000) >> 24);
  }

  bool DxsoDecodeContext::decodeInstruction(DxsoCodeIter& iter) {
    uint32_t token = iter.read();
    m_ctx.instructionIdx++;

    m_ctx.instruction.opcode             = static_cast<DxsoOpcode>(token & 0x0000ffff);
    m_ctx.instruction.predicated         = (token & (1 << 28)) != 0;
    m_ctx.instruction.coissue            = (token & 0x40000000) != 0;
    m_ctx.instruction.specificData.uint32 = (token & 0x00ff0000) >> 16;
    m_ctx.instruction.tokenLength        = this->decodeInstructionLength(token);
    m_ctx.srcCount                       = 0;

    const uint32_t tokenLength = m_ctx.instruction.tokenLength;

    switch (m_ctx.instruction.opcode) {
      case DxsoOpcode::If:
      case DxsoOpcode::Ifc:
      case DxsoOpcode::Rep:
      case DxsoOpcode::Loop:
      case DxsoOpcode::BreakC:
      case DxsoOpcode::BreakP: {
        uint32_t sourceIdx = 0;
        for (uint32_t i = 0; i < tokenLength; i++) {
          if (this->decodeSourceRegister(sourceIdx, iter))
            i++;
          sourceIdx++;
        }
        m_ctx.srcCount = sourceIdx;
        return true;
      }

      case DxsoOpcode::Dcl:
        this->decodeDeclaration(iter);
        this->decodeDestinationRegister(iter);
        return true;

      case DxsoOpcode::Def:
      case DxsoOpcode::DefI:
      case DxsoOpcode::DefB:
        this->decodeDestinationRegister(iter);
        this->decodeDefinition(m_ctx.instruction.opcode, iter);
        return true;

      case DxsoOpcode::Comment:
        iter = iter.skip(tokenLength);
        return true;

      case DxsoOpcode::End:
        return false;

      default: {
        uint32_t sourceIdx = 0;
        for (uint32_t i = 0; i < tokenLength; i++) {
          if (i == 0) {
            if (this->decodeDestinationRegister(iter))
              i++;
          }
          else if (i == 1 && m_ctx.instruction.predicated) {
            this->decodePredicateRegister(iter);
          }
          else {
            if (this->decodeSourceRegister(sourceIdx, iter))
              i++;
            sourceIdx++;
          }
        }
        m_ctx.srcCount = sourceIdx;
        return true;
      }
    }
  }

}  // namespace dxvk
