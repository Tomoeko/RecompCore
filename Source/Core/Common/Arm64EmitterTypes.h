// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <bit>
#include <cstring>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

#include "Common/ArmCommon.h"
#include "Common/Assert.h"
#include "Common/BitSet.h"
#include "Common/BitUtils.h"
#include "Common/CodeBlock.h"
#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/MathUtil.h"
#include "Common/SmallVector.h"

namespace Arm64Gen
{
// X30 serves a dual purpose as a link register
// Encoded as <u3:type><u5:reg>
// Types:
// 000 - 32bit GPR
// 001 - 64bit GPR
// 010 - VFP single precision
// 100 - VFP double precision
// 110 - VFP quad precision
enum class ARM64Reg
{
  // 32bit registers
  W0 = 0,
  W1,
  W2,
  W3,
  W4,
  W5,
  W6,
  W7,
  W8,
  W9,
  W10,
  W11,
  W12,
  W13,
  W14,
  W15,
  W16,
  W17,
  W18,
  W19,
  W20,
  W21,
  W22,
  W23,
  W24,
  W25,
  W26,
  W27,
  W28,
  W29,
  W30,

  WSP,  // 32bit stack pointer

  // 64bit registers
  X0 = 0x20,
  X1,
  X2,
  X3,
  X4,
  X5,
  X6,
  X7,
  X8,
  X9,
  X10,
  X11,
  X12,
  X13,
  X14,
  X15,
  X16,
  X17,
  X18,
  X19,
  X20,
  X21,
  X22,
  X23,
  X24,
  X25,
  X26,
  X27,
  X28,
  X29,
  X30,

  SP,  // 64bit stack pointer

  // VFP single precision registers
  S0 = 0x40,
  S1,
  S2,
  S3,
  S4,
  S5,
  S6,
  S7,
  S8,
  S9,
  S10,
  S11,
  S12,
  S13,
  S14,
  S15,
  S16,
  S17,
  S18,
  S19,
  S20,
  S21,
  S22,
  S23,
  S24,
  S25,
  S26,
  S27,
  S28,
  S29,
  S30,
  S31,

  // VFP Double Precision registers
  D0 = 0x80,
  D1,
  D2,
  D3,
  D4,
  D5,
  D6,
  D7,
  D8,
  D9,
  D10,
  D11,
  D12,
  D13,
  D14,
  D15,
  D16,
  D17,
  D18,
  D19,
  D20,
  D21,
  D22,
  D23,
  D24,
  D25,
  D26,
  D27,
  D28,
  D29,
  D30,
  D31,

  // ASIMD Quad-Word registers
  Q0 = 0xC0,
  Q1,
  Q2,
  Q3,
  Q4,
  Q5,
  Q6,
  Q7,
  Q8,
  Q9,
  Q10,
  Q11,
  Q12,
  Q13,
  Q14,
  Q15,
  Q16,
  Q17,
  Q18,
  Q19,
  Q20,
  Q21,
  Q22,
  Q23,
  Q24,
  Q25,
  Q26,
  Q27,
  Q28,
  Q29,
  Q30,
  Q31,

  // For PRFM(prefetch memory) encoding
  // This is encoded in the Rt register
  // Data preload
  PLDL1KEEP = 0,
  PLDL1STRM,
  PLDL2KEEP,
  PLDL2STRM,
  PLDL3KEEP,
  PLDL3STRM,
  // Instruction preload
  PLIL1KEEP = 8,
  PLIL1STRM,
  PLIL2KEEP,
  PLIL2STRM,
  PLIL3KEEP,
  PLIL3STRM,
  // Prepare for store
  PLTL1KEEP = 16,
  PLTL1STRM,
  PLTL2KEEP,
  PLTL2STRM,
  PLTL3KEEP,
  PLTL3STRM,

  WZR = WSP,
  ZR = SP,

  INVALID_REG = -1,
};

constexpr int operator&(const ARM64Reg& reg, const int mask)
{
  return static_cast<int>(reg) & mask;
}
constexpr int operator|(const ARM64Reg& reg, const int mask)
{
  return static_cast<int>(reg) | mask;
}
constexpr ARM64Reg operator+(const ARM64Reg& reg, const int addend)
{
  return static_cast<ARM64Reg>(static_cast<int>(reg) + addend);
}
constexpr bool Is64Bit(ARM64Reg reg)
{
  return (reg & 0x20) != 0;
}
constexpr bool IsSingle(ARM64Reg reg)
{
  return (reg & 0xC0) == 0x40;
}
constexpr bool IsDouble(ARM64Reg reg)
{
  return (reg & 0xC0) == 0x80;
}
constexpr bool IsScalar(ARM64Reg reg)
{
  return IsSingle(reg) || IsDouble(reg);
}
constexpr bool IsQuad(ARM64Reg reg)
{
  return (reg & 0xC0) == 0xC0;
}
constexpr bool IsVector(ARM64Reg reg)
{
  return (reg & 0xC0) != 0;
}
constexpr bool IsGPR(ARM64Reg reg)
{
  return static_cast<int>(reg) < 0x40;
}

constexpr int DecodeReg(ARM64Reg reg)
{
  return reg & 0x1F;
}
constexpr ARM64Reg EncodeRegTo32(ARM64Reg reg)
{
  return static_cast<ARM64Reg>(DecodeReg(reg));
}
constexpr ARM64Reg EncodeRegTo64(ARM64Reg reg)
{
  return static_cast<ARM64Reg>(reg | 0x20);
}
constexpr ARM64Reg EncodeRegToSingle(ARM64Reg reg)
{
  return static_cast<ARM64Reg>(ARM64Reg::S0 | DecodeReg(reg));
}
constexpr ARM64Reg EncodeRegToDouble(ARM64Reg reg)
{
  return static_cast<ARM64Reg>((reg & ~0xC0) | 0x80);
}
constexpr ARM64Reg EncodeRegToQuad(ARM64Reg reg)
{
  return static_cast<ARM64Reg>(reg | 0xC0);
}

enum class ShiftType
{
  // Logical Shift Left
  LSL = 0,
  // Logical Shift Right
  LSR = 1,
  // Arithmetic Shift Right
  ASR = 2,
  // Rotate Right
  ROR = 3,
};

enum class ExtendSpecifier
{
  UXTB = 0x0,
  UXTH = 0x1,
  UXTW = 0x2, /* Also LSL on 32bit width */
  UXTX = 0x3, /* Also LSL on 64bit width */
  SXTB = 0x4,
  SXTH = 0x5,
  SXTW = 0x6,
  SXTX = 0x7,
};

enum class IndexType
{
  Unsigned,
  Post,
  Pre,
  Signed,  // used in LDP/STP
};

enum class ShiftAmount
{
  Shift0,
  Shift16,
  Shift32,
  Shift48,
};

enum class RoundingMode
{
  A,  // round to nearest, ties to away
  M,  // round towards -inf
  N,  // round to nearest, ties to even
  P,  // round towards +inf
  Z,  // round towards zero
};

enum class GPRSize
{
  B32,
  B64,
};

struct FixupBranch
{
  enum class Type : u32
  {
    CBZ,
    CBNZ,
    BConditional,
    TBZ,
    TBNZ,
    B,
    BL,
  };

  u8* ptr;
  Type type;
  // Used with B.cond
  CCFlags cond;
  // Used with TBZ/TBNZ
  u8 bit;
  // Used with Test/Compare and Branch
  ARM64Reg reg;
};

enum class PStateField
{
  SPSel = 0,
  DAIFSet,
  DAIFClr,
  NZCV,  // The only system registers accessible from EL0 (user space)
  PMCR_EL0,
  PMCCNTR_EL0,
  FPCR = 0x340,
  FPSR = 0x341,
};

enum class SystemHint
{
  NOP,
  YIELD,
  WFE,
  WFI,
  SEV,
  SEVL,
};

enum class BarrierType
{
  OSHLD = 1,
  OSHST = 2,
  OSH = 3,
  NSHLD = 5,
  NSHST = 6,
  NSH = 7,
  ISHLD = 9,
  ISHST = 10,
  ISH = 11,
  LD = 13,
  ST = 14,
  SY = 15,
};

class ArithOption
{
private:
  enum class WidthSpecifier
  {
    Default,
    Width32Bit,
    Width64Bit,
  };

  enum class TypeSpecifier
  {
    ExtendedReg,
    Immediate,
    ShiftedReg,
  };

  ARM64Reg m_destReg;
  WidthSpecifier m_width;
  ExtendSpecifier m_extend;
  TypeSpecifier m_type;
  ShiftType m_shifttype;
  u32 m_shift;

public:
  ArithOption(ARM64Reg Rd, bool index = false)
  {
    // Indexed registers are a certain feature of AARch64
    // On Loadstore instructions that use a register offset
    // We can have the register as an index
    // If we are indexing then the offset register will
    // be shifted to the left so we are indexing at intervals
    // of the size of what we are loading
    // 8-bit: Index does nothing
    // 16-bit: Index LSL 1
    // 32-bit: Index LSL 2
    // 64-bit: Index LSL 3
    if (index)
      m_shift = 4;
    else
      m_shift = 0;

    m_destReg = Rd;
    m_type = TypeSpecifier::ExtendedReg;
    if (Is64Bit(Rd))
    {
      m_width = WidthSpecifier::Width64Bit;
      m_extend = ExtendSpecifier::UXTX;
    }
    else
    {
      m_width = WidthSpecifier::Width32Bit;
      m_extend = ExtendSpecifier::UXTW;
    }
    m_shifttype = ShiftType::LSL;
  }
  ArithOption(ARM64Reg Rd, ExtendSpecifier extend_type, u32 shift = 0)
  {
    m_destReg = Rd;
    m_width = Is64Bit(Rd) ? WidthSpecifier::Width64Bit : WidthSpecifier::Width32Bit;
    m_extend = extend_type;
    m_type = TypeSpecifier::ExtendedReg;
    m_shifttype = ShiftType::LSL;
    m_shift = shift;
  }
  ArithOption(ARM64Reg Rd, ShiftType shift_type, u32 shift)
  {
    m_destReg = Rd;
    m_shift = shift;
    m_shifttype = shift_type;
    m_type = TypeSpecifier::ShiftedReg;
    if (Is64Bit(Rd))
    {
      m_width = WidthSpecifier::Width64Bit;
      if (shift == 64)
        m_shift = 0;
      m_extend = ExtendSpecifier::UXTX;
    }
    else
    {
      m_width = WidthSpecifier::Width32Bit;
      if (shift == 32)
        m_shift = 0;
      m_extend = ExtendSpecifier::UXTW;
    }
  }
  ARM64Reg GetReg() const { return m_destReg; }
  u32 GetData() const
  {
    switch (m_type)
    {
    case TypeSpecifier::ExtendedReg:
      return (static_cast<u32>(m_extend) << 13) | (m_shift << 10);
    case TypeSpecifier::ShiftedReg:
      return (static_cast<u32>(m_shifttype) << 22) | (m_shift << 10);
    default:
      DEBUG_ASSERT_MSG(DYNA_REC, false, "Invalid type in GetData");
      break;
    }
    return 0;
  }

  bool IsExtended() const { return m_type == TypeSpecifier::ExtendedReg; }
};

struct LogicalImm
{
  constexpr LogicalImm() {}

  constexpr LogicalImm(u8 r_, u8 s_, bool n_) : r(r_), s(s_), n(n_), valid(true) {}

  constexpr LogicalImm(u64 value, GPRSize size)
  {
    // Logical immediates are encoded using parameters n, imm_s and imm_r using
    // the following table:
    //
    //    N   imms    immr    size        S             R
    //    1  ssssss  rrrrrr    64    UInt(ssssss)  UInt(rrrrrr)
    //    0  0sssss  xrrrrr    32    UInt(sssss)   UInt(rrrrr)
    //    0  10ssss  xxrrrr    16    UInt(ssss)    UInt(rrrr)
    //    0  110sss  xxxrrr     8    UInt(sss)     UInt(rrr)
    //    0  1110ss  xxxxrr     4    UInt(ss)      UInt(rr)
    //    0  11110s  xxxxxr     2    UInt(s)       UInt(r)
    // (s bits must not be all set)
    //
    // A pattern is constructed of size bits, where the least significant S+1 bits
    // are set. The pattern is rotated right by R, and repeated across a 32 or
    // 64-bit value, depending on destination register width.

    if (size == GPRSize::B32)
    {
      // To handle 32-bit logical immediates, the very easiest thing is to repeat
      // the input value twice to make a 64-bit word. The correct encoding of that
      // as a logical immediate will also be the correct encoding of the 32-bit
      // value.

      value = (value << 32) | (value & 0xFFFFFFFF);
    }

    if (value == 0 || (~value) == 0)
    {
      valid = false;
      return;
    }

    // Normalize value, rotating it such that the LSB is 1:
    // If LSB is already one, we mask away the trailing sequence of ones and
    // pick the next sequence of ones. This ensures we get a complete element
    // that has not been cut-in-half due to rotation across the word boundary.

    const int rotation = std::countr_zero(value & (value + 1));
    const u64 normalized = std::rotr(value, rotation);

    const int element_size = std::countr_zero(normalized & (normalized + 1));
    const int ones = std::countr_one(normalized);

    // Check the value is repeating; also ensures element size is a power of two.

    if (std::rotr(value, element_size) != value)
    {
      valid = false;
      return;
    }

    // Now we're done. We just have to encode the S output in such a way that
    // it gives both the number of set bits and the length of the repeated
    // segment.

    r = static_cast<u8>((element_size - rotation) & (element_size - 1));
    s = static_cast<u8>((((~element_size + 1) << 1) | (ones - 1)) & 0x3f);
    n = Common::ExtractBit<6>(element_size);

    valid = true;
  }

  constexpr operator bool() const { return valid; }

  u8 r = 0;
  u8 s = 0;
  bool n = false;
  bool valid = false;
};

constexpr bool IsInRangeImm19(s64 distance) { return (distance >= -0x40000 && distance <= 0x3FFFF); }
constexpr bool IsInRangeImm14(s64 distance) { return (distance >= -0x2000 && distance <= 0x1FFF); }
constexpr bool IsInRangeImm26(s64 distance) { return (distance >= -0x2000000 && distance <= 0x1FFFFFF); }
constexpr u32 MaskImm19(s64 distance) { return distance & 0x7FFFF; }
constexpr u32 MaskImm14(s64 distance) { return distance & 0x3FFF; }
constexpr u32 MaskImm26(s64 distance) { return distance & 0x3FFFFFF; }
}  // namespace Arm64Gen
