// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// WARNING - THIS LIBRARY IS NOT THREAD SAFE!!!

#pragma once

#include <cstddef>
#include <cstring>
#include <functional>
#include <tuple>
#include <type_traits>

#include "Common/Assert.h"
#include "Common/BitSet.h"
#include "Common/CodeBlock.h"
#include "Common/CommonTypes.h"
#include "Common/x64ABI.h"

namespace Gen
{
enum CCFlags
{
  CC_O = 0,
  CC_NO = 1,
  CC_B = 2,
  CC_C = 2,
  CC_NAE = 2,
  CC_NB = 3,
  CC_NC = 3,
  CC_AE = 3,
  CC_Z = 4,
  CC_E = 4,
  CC_NZ = 5,
  CC_NE = 5,
  CC_BE = 6,
  CC_NA = 6,
  CC_NBE = 7,
  CC_A = 7,
  CC_S = 8,
  CC_NS = 9,
  CC_P = 0xA,
  CC_PE = 0xA,
  CC_NP = 0xB,
  CC_PO = 0xB,
  CC_L = 0xC,
  CC_NGE = 0xC,
  CC_NL = 0xD,
  CC_GE = 0xD,
  CC_LE = 0xE,
  CC_NG = 0xE,
  CC_NLE = 0xF,
  CC_G = 0xF
};

enum
{
  NUMGPRs = 16,
  NUMXMMs = 16,
};

enum
{
  SCALE_NONE = 0,
  SCALE_1 = 1,
  SCALE_2 = 2,
  SCALE_4 = 4,
  SCALE_8 = 8,
  SCALE_ATREG = 16,
  // SCALE_NOBASE_1 is not supported and can be replaced with SCALE_ATREG
  SCALE_NOBASE_2 = 34,
  SCALE_NOBASE_4 = 36,
  SCALE_NOBASE_8 = 40,
  SCALE_RIP = 0xFF,
  SCALE_IMM8 = 0xF0,
  SCALE_IMM16 = 0xF1,
  SCALE_IMM32 = 0xF2,
  SCALE_IMM64 = 0xF3,
};

enum SSECompare
{
  CMP_EQ = 0,
  CMP_LT = 1,
  CMP_LE = 2,
  CMP_UNORD = 3,
  CMP_NEQ = 4,
  CMP_NLT = 5,
  CMP_NLE = 6,
  CMP_ORD = 7,
};

class XEmitter;
enum class FloatOp;
enum class NormalOp;

// Information about a generated MOV op
struct MovInfo final
{
  u8* address;
  bool nonAtomicSwapStore;
  // valid iff nonAtomicSwapStore is true
  X64Reg nonAtomicSwapStoreSrc;
};

// RIP addressing does not benefit from micro op fusion on Core arch
struct OpArg
{
  // For accessing offset and operandReg.
  // This also allows us to keep the op writing functions private.
  friend class XEmitter;

  // dummy op arg, used for storage
  constexpr OpArg() = default;
  constexpr OpArg(u64 offset_, int scale_, X64Reg rm_reg = RAX, X64Reg scaled_reg = RAX)
      : offset{offset_}, offsetOrBaseReg{static_cast<u16>(rm_reg)},
        indexReg{static_cast<u16>(scaled_reg)}, scale{static_cast<u8>(scale_)}
  {
  }
  constexpr bool operator==(const OpArg& b) const
  {
    return std::tie(scale, offsetOrBaseReg, indexReg, offset, operandReg) ==
           std::tie(b.scale, b.offsetOrBaseReg, b.indexReg, b.offset, b.operandReg);
  }
  u64 Imm64() const
  {
    DEBUG_ASSERT(scale == SCALE_IMM64);
    return (u64)offset;
  }
  u32 Imm32() const
  {
    DEBUG_ASSERT(scale == SCALE_IMM32);
    return (u32)offset;
  }
  u16 Imm16() const
  {
    DEBUG_ASSERT(scale == SCALE_IMM16);
    return (u16)offset;
  }
  u8 Imm8() const
  {
    DEBUG_ASSERT(scale == SCALE_IMM8);
    return (u8)offset;
  }

  s64 SImm64() const
  {
    DEBUG_ASSERT(scale == SCALE_IMM64);
    return (s64)offset;
  }
  s32 SImm32() const
  {
    DEBUG_ASSERT(scale == SCALE_IMM32);
    return (s32)offset;
  }
  s16 SImm16() const
  {
    DEBUG_ASSERT(scale == SCALE_IMM16);
    return (s16)offset;
  }
  s8 SImm8() const
  {
    DEBUG_ASSERT(scale == SCALE_IMM8);
    return (s8)offset;
  }

  OpArg AsImm64() const
  {
    DEBUG_ASSERT(IsImm());
    return OpArg((u64)offset, SCALE_IMM64);
  }
  OpArg AsImm32() const
  {
    DEBUG_ASSERT(IsImm());
    return OpArg((u32)offset, SCALE_IMM32);
  }
  OpArg AsImm16() const
  {
    DEBUG_ASSERT(IsImm());
    return OpArg((u16)offset, SCALE_IMM16);
  }
  OpArg AsImm8() const
  {
    DEBUG_ASSERT(IsImm());
    return OpArg((u8)offset, SCALE_IMM8);
  }

  constexpr bool IsImm() const
  {
    return scale == SCALE_IMM8 || scale == SCALE_IMM16 || scale == SCALE_IMM32 ||
           scale == SCALE_IMM64;
  }
  constexpr bool IsSimpleReg() const { return scale == SCALE_NONE; }
  constexpr bool IsSimpleReg(X64Reg reg) const { return IsSimpleReg() && GetSimpleReg() == reg; }
  constexpr bool IsZero() const { return IsImm() && offset == 0; }
  constexpr int GetImmBits() const
  {
    switch (scale)
    {
    case SCALE_IMM8:
      return 8;
    case SCALE_IMM16:
      return 16;
    case SCALE_IMM32:
      return 32;
    case SCALE_IMM64:
      return 64;
    default:
      return -1;
    }
  }

  constexpr X64Reg GetSimpleReg() const
  {
    if (scale == SCALE_NONE)
      return static_cast<X64Reg>(offsetOrBaseReg);

    return INVALID_REG;
  }

  void AddMemOffset(int val)
  {
    DEBUG_ASSERT_MSG(DYNA_REC, scale == SCALE_RIP || (scale <= SCALE_ATREG && scale > SCALE_NONE),
                     "Tried to increment an OpArg which doesn't have an offset");
    offset += val;
  }

private:
  void WriteREX(XEmitter* emit, int opBits, int bits, int customOp = -1) const;
  void WriteVEX(XEmitter* emit, X64Reg regOp1, X64Reg regOp2, int L, int pp, int mmmmm,
                int W = 0) const;
  void WriteRest(XEmitter* emit, int extraBytes = 0, X64Reg operandReg = INVALID_REG,
                 bool warn_64bit_offset = true) const;
  void WriteSingleByteOp(XEmitter* emit, u8 op, X64Reg operandReg, int bits);
  void WriteNormalOp(XEmitter* emit, bool toRM, NormalOp op, const OpArg& operand, int bits) const;

  u64 offset = 0;  // Also used to store immediates.
  u16 offsetOrBaseReg = 0;
  u16 indexReg = 0;
  u16 operandReg = 0;
  u8 scale = 0;
};

template <typename T>
inline OpArg M(const T* ptr)
{
  return OpArg((u64)(const void*)ptr, (int)SCALE_RIP);
}
constexpr OpArg R(X64Reg value)
{
  return OpArg(0, SCALE_NONE, value);
}
constexpr OpArg MatR(X64Reg value)
{
  return OpArg(0, SCALE_ATREG, value);
}

constexpr OpArg MDisp(X64Reg value, int offset)
{
  return OpArg(static_cast<u32>(offset), SCALE_ATREG, value);
}

constexpr OpArg MComplex(X64Reg base, X64Reg scaled, int scale, int offset)
{
  return OpArg(offset, scale, base, scaled);
}

constexpr OpArg MScaled(X64Reg scaled, int scale, int offset)
{
  if (scale == SCALE_1)
    return OpArg(offset, SCALE_ATREG, scaled);

  return OpArg(offset, scale | 0x20, RAX, scaled);
}

constexpr OpArg MRegSum(X64Reg base, X64Reg offset)
{
  return MComplex(base, offset, 1, 0);
}

constexpr OpArg Imm8(u8 imm)
{
  return OpArg(imm, SCALE_IMM8);
}
constexpr OpArg Imm16(u16 imm)
{
  return OpArg(imm, SCALE_IMM16);
}  // rarely used
constexpr OpArg Imm32(u32 imm)
{
  return OpArg(imm, SCALE_IMM32);
}
constexpr OpArg Imm64(u64 imm)
{
  return OpArg(imm, SCALE_IMM64);
}
inline OpArg ImmPtr(const void* imm)
{
  return Imm64(reinterpret_cast<u64>(imm));
}

inline u32 PtrOffset(const void* ptr, const void* base = nullptr)
{
  s64 distance = (s64)ptr - (s64)base;
  if (distance >= 0x80000000LL || distance < -0x80000000LL)
  {
    ASSERT_MSG(DYNA_REC, 0, "pointer offset out of range");
    return 0;
  }

  return (u32)distance;
}

struct FixupBranch
{
  enum class Type
  {
    Branch8Bit,
    Branch32Bit
  };

  u8* ptr;
  Type type;
};


enum class NormalOp
{
  ADD,
  ADC,
  SUB,
  SBB,
  AND,
  OR,
  XOR,
  MOV,
  TEST,
  CMP,
  XCHG,
};

enum class FloatOp
{
  LD = 0,
  ST = 2,
  STP = 3,
  LD80 = 5,
  STP80 = 7,

  Invalid = -1,
};

enum NormalSSEOps
{
  sseCMP = 0xC2,
  sseADD = 0x58,   // ADD
  sseSUB = 0x5C,   // SUB
  sseAND = 0x54,   // AND
  sseANDN = 0x55,  // ANDN
  sseOR = 0x56,
  sseXOR = 0x57,
  sseMUL = 0x59,          // MUL
  sseDIV = 0x5E,          // DIV
  sseMIN = 0x5D,          // MIN
  sseMAX = 0x5F,          // MAX
  sseCOMIS = 0x2F,        // COMIS
  sseUCOMIS = 0x2E,       // UCOMIS
  sseSQRT = 0x51,         // SQRT
  sseRCP = 0x53,          // RCP
  sseRSQRT = 0x52,        // RSQRT (NO DOUBLE PRECISION!!!)
  sseMOVAPfromRM = 0x28,  // MOVAP from RM
  sseMOVAPtoRM = 0x29,    // MOVAP to RM
  sseMOVUPfromRM = 0x10,  // MOVUP from RM
  sseMOVUPtoRM = 0x11,    // MOVUP to RM
  sseMOVLPfromRM = 0x12,
  sseMOVLPtoRM = 0x13,
  sseMOVHPfromRM = 0x16,
  sseMOVHPtoRM = 0x17,
  sseMOVHLPS = 0x12,
  sseMOVLHPS = 0x16,
  sseMOVDQfromRM = 0x6F,
  sseMOVDQtoRM = 0x7F,
  sseMASKMOVDQU = 0xF7,
  sseLDDQU = 0xF0,
  sseSHUF = 0xC6,
  sseMOVNTDQ = 0xE7,
  sseMOVNTP = 0x2B,
};

}  // namespace Gen
