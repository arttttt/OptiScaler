#pragma once
// Ported from DXVK src/dxso/ (zlib/libpng license, see attribution below).
// We took only what's needed to decode SM2/SM3 vertex-shader bytecode well
// enough to find writes to oPos (RasterizerOut[0]). All Vulkan / SPIR-V /
// DxvkShader plumbing dropped; DxsoReader replaced by a raw uint32_t*
// pointer wrapper; SM1.x default-opcode-length table dropped because
// DX9-era games we target use SM2 or SM3 (the in-token length field is
// authoritative for those).
//
// DXVK is Copyright (c) 2017 Philip Rebohle / (c) 2019 Joshua Ashton /
// (c) 2019 Robin Kertels / (c) 2023 Jeffrey Ellison, zlib/libpng license.
// Files referenced:
//   src/dxso/dxso_enums.h
//   src/dxso/dxso_decoder.{h,cpp}
//   src/dxso/dxso_code.h
//   src/dxso/dxso_common.h

#include <array>
#include <cstdint>

namespace dxvk {

  // ---- Opcodes (subset; full enum from dxso_enums.h) -----------------------

  enum class DxsoOpcode : uint32_t {
    Nop = 0, Mov, Add, Sub, Mad, Mul, Rcp, Rsq,
    Dp3, Dp4, Min, Max, Slt, Sge, Exp, Log, Lit, Dst,
    Lrp, Frc, M4x4, M4x3, M3x4, M3x3, M3x2,
    Call, CallNz, Loop, Ret, EndLoop, Label,
    Dcl, Pow, Crs, Sgn, Abs, Nrm, SinCos,
    Rep, EndRep, If, Ifc, Else, EndIf,
    Break, BreakC, Mova, DefB, DefI,
    TexCoord = 64, TexKill, Tex, TexBem, TexBemL,
    TexReg2Ar, TexReg2Gb, TexM3x2Pad, TexM3x2Tex,
    TexM3x3Pad, TexM3x3Tex, Reserved0, TexM3x3Spec, TexM3x3VSpec,
    ExpP, LogP, Cnd, Def, TexReg2Rgb, TexDp3Tex, TexM3x2Depth,
    TexDp3, TexM3x3, TexDepth, Cmp, Bem, Dp2Add, DsX, DsY,
    TexLdd, SetP, TexLdl, BreakP,
    Phase = 0xfffd, Comment = 0xfffe, End = 0xffff
  };

  enum class DxsoRegisterType : uint32_t {
    Temp = 0, Input, Const, Addr /*VS*/,
    RasterizerOut, AttributeOut, TexcoordOut /*=Output VS3*/,
    ConstInt, ColorOut, DepthOut, Sampler,
    Const2, Const3, Const4, ConstBool, Loop,
    TempFloat16, MiscType, Label, Predicate, PixelTexcoord
  };

  enum DxsoRasterizerOutIndex : uint32_t {
    RasterOutPosition  = 0,
    RasterOutFog       = 1,
    RasterOutPointSize = 2,
  };

  enum class DxsoRegModifier : uint32_t {
    None = 0, Neg, Bias, BiasNeg, Sign, SignNeg, Comp,
    X2, X2Neg, Dz, Dw, Abs, AbsNeg, Not
  };

  enum class DxsoUsage : uint32_t {
    Position = 0, BlendWeight, BlendIndices, Normal, PointSize,
    Texcoord, Tangent, Binormal, TessFactor, PositionT, Color, Fog,
    Depth, Sample
  };

  enum class DxsoInstructionArgumentType : uint16_t { Source, Destination };

  // ---- Program info --------------------------------------------------------

  enum class DxsoProgramType : uint32_t { PixelShader = 0, VertexShader = 1 };

  class DxsoProgramInfo {
  public:
    DxsoProgramInfo() = default;
    DxsoProgramInfo(DxsoProgramType type, uint32_t minor, uint32_t major)
      : m_type(type), m_minorVersion(minor), m_majorVersion(major) {}

    DxsoProgramType type() const { return m_type; }
    uint32_t minorVersion() const { return m_minorVersion; }
    uint32_t majorVersion() const { return m_majorVersion; }

  private:
    DxsoProgramType m_type = DxsoProgramType::VertexShader;
    uint32_t m_minorVersion = 0;
    uint32_t m_majorVersion = 0;
  };

  // Parse a DXBC header token. Returns false if it isn't a valid SM header.
  inline bool DxsoDecodeHeader(uint32_t token, DxsoProgramInfo& out) {
    const uint32_t topId = (token & 0xFFFF0000u) >> 16;
    if (topId != 0xFFFEu && topId != 0xFFFFu)
      return false;
    const uint32_t major = (token & 0x0000FF00u) >> 8;
    const uint32_t minor = (token & 0x000000FFu);
    out = DxsoProgramInfo(topId == 0xFFFEu ? DxsoProgramType::VertexShader : DxsoProgramType::PixelShader,
                          minor, major);
    return true;
  }

  // ---- Code iterator (replaces DxvkReader) --------------------------------

  class DxsoCodeIter {
  public:
    DxsoCodeIter(const uint32_t* ptr) : m_ptr(ptr) {}
    const uint32_t* ptrAt(uint32_t id) const { return m_ptr + id; }
    uint32_t at(uint32_t id) const            { return m_ptr[id]; }
    uint32_t read()                           { return *m_ptr++; }
    DxsoCodeIter skip(uint32_t n) const       { return DxsoCodeIter(m_ptr + n); }
  private:
    const uint32_t* m_ptr;
  };

  // ---- Register descriptions ---------------------------------------------

  constexpr size_t DxsoMaxOperandCount = 8;
  constexpr uint32_t DxsoRegModifierShift = 24;

  struct DxsoRegisterId {
    DxsoRegisterType type;
    uint32_t         num;
    bool operator==(const DxsoRegisterId& o) const { return type == o.type && num == o.num; }
    bool operator!=(const DxsoRegisterId& o) const { return !(*this == o); }
  };

  class DxsoRegMask {
  public:
    DxsoRegMask(uint8_t mask = 0xF) : m_mask(mask) {}
    bool operator[](uint32_t id) const { return (m_mask & (1u << id)) != 0; }
    uint8_t raw() const { return m_mask; }
  private:
    uint8_t m_mask;
  };

  class DxsoRegSwizzle {
  public:
    DxsoRegSwizzle(uint8_t mask = 0xE4 /*0,1,2,3*/) : m_mask(mask) {}
    uint32_t operator[](uint32_t id) const { return (m_mask >> (id + id)) & 0x3; }
  private:
    uint8_t m_mask;
  };

  struct DxsoBaseRegister {
    DxsoRegisterId  id               = { DxsoRegisterType::Temp, 0 };
    bool            centroid         = false;
    bool            partialPrecision = false;
    bool            saturate         = false;
    DxsoRegModifier modifier         = DxsoRegModifier::None;
    DxsoRegMask     mask             = DxsoRegMask(0xF);
    DxsoRegSwizzle  swizzle          = DxsoRegSwizzle(0xE4);
    int8_t          shift            = 0;
  };

  struct DxsoRegister : public DxsoBaseRegister {
    bool             hasRelative = false;
    DxsoBaseRegister relative;
  };

  struct DxsoSemantic { DxsoUsage usage; uint32_t usageIndex; };
  struct DxsoDeclaration { DxsoSemantic semantic; uint32_t textureType; };

  union DxsoDefinition {
    float    float32[4];
    int32_t  int32[4];
    uint32_t uint32[4];
  };

  union DxsoOpcodeSpecificData {
    uint32_t uint32;
  };

  struct DxsoShaderInstruction {
    DxsoOpcode             opcode;
    bool                   predicated;
    bool                   coissue;
    DxsoOpcodeSpecificData specificData;
    uint32_t               tokenLength;
  };

  struct DxsoInstructionContext {
    uint32_t                                       instructionIdx;
    DxsoShaderInstruction                          instruction;
    DxsoRegister                                   pred;
    DxsoRegister                                   dst;
    std::array<DxsoRegister, DxsoMaxOperandCount>  src;
    DxsoDefinition                                 def;
    DxsoDeclaration                                dcl;
  };

  // ---- Decoder ------------------------------------------------------------

  class DxsoDecodeContext {
  public:
    DxsoDecodeContext(const DxsoProgramInfo& info) : m_programInfo(info) {
      m_ctx.instructionIdx = 0;
    }

    const DxsoInstructionContext& getInstructionContext() const { return m_ctx; }
    const DxsoProgramInfo& getProgramInfo() const               { return m_programInfo; }

    // Advances iter past the consumed tokens. Returns false on End opcode.
    bool decodeInstruction(DxsoCodeIter& iter);

  private:
    uint32_t decodeInstructionLength(uint32_t token);

    void decodeBaseRegister(DxsoBaseRegister& reg, uint32_t token);
    void decodeGenericRegister(DxsoRegister& reg, uint32_t token);
    void decodeRelativeRegister(DxsoBaseRegister& reg, uint32_t token);

    bool decodeDestinationRegister(DxsoCodeIter& iter);
    bool decodeSourceRegister(uint32_t i, DxsoCodeIter& iter);
    void decodePredicateRegister(DxsoCodeIter& iter);

    void decodeDeclaration(DxsoCodeIter& iter);
    void decodeDefinition(DxsoOpcode opcode, DxsoCodeIter& iter);

    bool relativeAddressingUsesToken(DxsoInstructionArgumentType type);

    const DxsoProgramInfo& m_programInfo;
    DxsoInstructionContext m_ctx {};
  };

}  // namespace dxvk
