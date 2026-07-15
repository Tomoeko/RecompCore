// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/Arm64Emitter.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "Common/Align.h"
#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/MathUtil.h"
#include "Common/SmallVector.h"

#ifdef _WIN32
#include <Windows.h>
#endif
#ifdef __APPLE__
#include <libkern/OSCacheControl.h>
#endif

namespace Arm64Gen
{

namespace
{
std::optional<std::pair<u32, bool>> IsImmArithmetic(uint64_t input)
{
  if (input < 4096)
    return std::pair{static_cast<u32>(input), false};

  if ((input & 0xFFF000) == input)
    return std::pair{static_cast<u32>(input >> 12), true};

  return std::nullopt;
}
}  // Anonymous namespace

static const u32 ArithEnc[] = {
    0x058,  // ADD
    0x258,  // SUB
};

static const u32 CondSelectEnc[][2] = {
    {0, 0},  // CSEL
    {0, 1},  // CSINC
    {1, 0},  // CSINV
    {1, 1},  // CSNEG
};

static const u32 Data1SrcEnc[][2] = {
    {0, 0},  // RBIT
    {0, 1},  // REV16
    {0, 2},  // REV32
    {0, 3},  // REV64
    {0, 4},  // CLZ
    {0, 5},  // CLS
};

static const u32 Data2SrcEnc[] = {
    0x02,  // UDIV
    0x03,  // SDIV
    0x08,  // LSLV
    0x09,  // LSRV
    0x0A,  // ASRV
    0x0B,  // RORV
    0x10,  // CRC32B
    0x11,  // CRC32H
    0x12,  // CRC32W
    0x14,  // CRC32CB
    0x15,  // CRC32CH
    0x16,  // CRC32CW
    0x13,  // CRC32X (64bit Only)
    0x17,  // XRC32CX (64bit Only)
};

static const u32 Data3SrcEnc[][2] = {
    {0, 0},  // MADD
    {0, 1},  // MSUB
    {1, 0},  // SMADDL (64Bit Only)
    {1, 1},  // SMSUBL (64Bit Only)
    {2, 0},  // SMULH (64Bit Only)
    {5, 0},  // UMADDL (64Bit Only)
    {5, 1},  // UMSUBL (64Bit Only)
    {6, 0},  // UMULH (64Bit Only)
};

static const u32 LogicalEnc[][2] = {
    {0, 0},  // AND
    {0, 1},  // BIC
    {1, 0},  // OOR
    {1, 1},  // ORN
    {2, 0},  // EOR
    {2, 1},  // EON
    {3, 0},  // ANDS
    {3, 1},  // BICS
};

void ARM64XEmitter::EncodeArithmeticInst(u32 instenc, bool flags, ARM64Reg Rd, ARM64Reg Rn,
                                         ARM64Reg Rm, ArithOption Option)
{
  bool b64Bit = Is64Bit(Rd);

  Write32((b64Bit << 31) | (flags << 29) | (ArithEnc[instenc] << 21) |
          (Option.IsExtended() ? (1 << 21) : 0) | (DecodeReg(Rm) << 16) | Option.GetData() |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64XEmitter::EncodeArithmeticCarryInst(u32 op, bool flags, ARM64Reg Rd, ARM64Reg Rn,
                                              ARM64Reg Rm)
{
  bool b64Bit = Is64Bit(Rd);

  Write32((b64Bit << 31) | (op << 30) | (flags << 29) | (0xD0 << 21) | (DecodeReg(Rm) << 16) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64XEmitter::EncodeCondCompareImmInst(u32 op, ARM64Reg Rn, u32 imm, u32 nzcv, CCFlags cond)
{
  bool b64Bit = Is64Bit(Rn);

  ASSERT_MSG(DYNA_REC, !(imm & ~0x1F), "too large immediate: {}", imm);
  ASSERT_MSG(DYNA_REC, !(nzcv & ~0xF), "Flags out of range: {}", nzcv);

  Write32((b64Bit << 31) | (op << 30) | (1 << 29) | (0xD2 << 21) | (imm << 16) | (cond << 12) |
          (1 << 11) | (DecodeReg(Rn) << 5) | nzcv);
}

void ARM64XEmitter::EncodeCondCompareRegInst(u32 op, ARM64Reg Rn, ARM64Reg Rm, u32 nzcv,
                                             CCFlags cond)
{
  bool b64Bit = Is64Bit(Rm);

  ASSERT_MSG(DYNA_REC, !(nzcv & ~0xF), "Flags out of range: {}", nzcv);

  Write32((b64Bit << 31) | (op << 30) | (1 << 29) | (0xD2 << 21) | (DecodeReg(Rm) << 16) |
          (cond << 12) | (DecodeReg(Rn) << 5) | nzcv);
}

void ARM64XEmitter::EncodeCondSelectInst(u32 instenc, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm,
                                         CCFlags cond)
{
  bool b64Bit = Is64Bit(Rd);

  Write32((b64Bit << 31) | (CondSelectEnc[instenc][0] << 30) | (0xD4 << 21) |
          (DecodeReg(Rm) << 16) | (cond << 12) | (CondSelectEnc[instenc][1] << 10) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64XEmitter::EncodeData1SrcInst(u32 instenc, ARM64Reg Rd, ARM64Reg Rn)
{
  bool b64Bit = Is64Bit(Rd);

  Write32((b64Bit << 31) | (0x2D6 << 21) | (Data1SrcEnc[instenc][0] << 16) |
          (Data1SrcEnc[instenc][1] << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64XEmitter::EncodeData2SrcInst(u32 instenc, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  bool b64Bit = Is64Bit(Rd);

  Write32((b64Bit << 31) | (0x0D6 << 21) | (DecodeReg(Rm) << 16) | (Data2SrcEnc[instenc] << 10) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64XEmitter::EncodeData3SrcInst(u32 instenc, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm,
                                       ARM64Reg Ra)
{
  bool b64Bit = Is64Bit(Rd);

  Write32((b64Bit << 31) | (0xD8 << 21) | (Data3SrcEnc[instenc][0] << 21) | (DecodeReg(Rm) << 16) |
          (Data3SrcEnc[instenc][1] << 15) | (DecodeReg(Ra) << 10) | (DecodeReg(Rn) << 5) |
          DecodeReg(Rd));
}

void ARM64XEmitter::EncodeLogicalInst(u32 instenc, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm,
                                      ArithOption Shift)
{
  bool b64Bit = Is64Bit(Rd);

  Write32((b64Bit << 31) | (LogicalEnc[instenc][0] << 29) | (0x5 << 25) |
          (LogicalEnc[instenc][1] << 21) | Shift.GetData() | (DecodeReg(Rm) << 16) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64XEmitter::EncodeMOVWideInst(u32 op, ARM64Reg Rd, u32 imm, ShiftAmount pos)
{
  bool b64Bit = Is64Bit(Rd);

  ASSERT_MSG(DYNA_REC, !(imm & ~0xFFFF), "immediate out of range: {}", imm);

  Write32((b64Bit << 31) | (op << 29) | (0x25 << 23) | (static_cast<u32>(pos) << 21) | (imm << 5) |
          DecodeReg(Rd));
}

void ARM64XEmitter::EncodeBitfieldMOVInst(u32 op, ARM64Reg Rd, ARM64Reg Rn, u32 immr, u32 imms)
{
  bool b64Bit = Is64Bit(Rd);

  Write32((b64Bit << 31) | (op << 29) | (0x26 << 23) | (b64Bit << 22) | (immr << 16) |
          (imms << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64XEmitter::EncodeAddSubImmInst(u32 op, bool flags, u32 shift, u32 imm, ARM64Reg Rn,
                                        ARM64Reg Rd)
{
  bool b64Bit = Is64Bit(Rd);

  ASSERT_MSG(DYNA_REC, !(imm & ~0xFFF), "immediate too large: {}", imm);

  Write32((b64Bit << 31) | (op << 30) | (flags << 29) | (0x11 << 24) | (shift << 22) | (imm << 10) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64XEmitter::EncodeLogicalImmInst(u32 op, ARM64Reg Rd, ARM64Reg Rn, LogicalImm imm)
{
  ASSERT_MSG(DYNA_REC, imm.valid, "Invalid logical immediate");

  // Sometimes Rd is fixed to SP, but can still be 32bit or 64bit.
  // Use Rn to determine bitness here.
  bool b64Bit = Is64Bit(Rn);

  ASSERT_MSG(DYNA_REC, b64Bit || !imm.n,
             "64-bit logical immediate does not fit in 32-bit register");

  Write32((b64Bit << 31) | (op << 29) | (0x24 << 23) | (imm.n << 22) | (imm.r << 16) |
          (imm.s << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rd));
}

void ARM64XEmitter::ADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  ADD(Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
}

void ARM64XEmitter::ADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Option)
{
  EncodeArithmeticInst(0, false, Rd, Rn, Rm, Option);
}

void ARM64XEmitter::ADDS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeArithmeticInst(0, true, Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
}

void ARM64XEmitter::ADDS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Option)
{
  EncodeArithmeticInst(0, true, Rd, Rn, Rm, Option);
}

void ARM64XEmitter::SUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  SUB(Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
}

void ARM64XEmitter::SUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Option)
{
  EncodeArithmeticInst(1, false, Rd, Rn, Rm, Option);
}

void ARM64XEmitter::SUBS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeArithmeticInst(1, true, Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
}

void ARM64XEmitter::SUBS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Option)
{
  EncodeArithmeticInst(1, true, Rd, Rn, Rm, Option);
}

void ARM64XEmitter::CMN(ARM64Reg Rn, ARM64Reg Rm)
{
  CMN(Rn, Rm, ArithOption(Rn, ShiftType::LSL, 0));
}

void ARM64XEmitter::CMN(ARM64Reg Rn, ARM64Reg Rm, ArithOption Option)
{
  EncodeArithmeticInst(0, true, Is64Bit(Rn) ? ARM64Reg::ZR : ARM64Reg::WZR, Rn, Rm, Option);
}

void ARM64XEmitter::CMP(ARM64Reg Rn, ARM64Reg Rm)
{
  CMP(Rn, Rm, ArithOption(Rn, ShiftType::LSL, 0));
}

void ARM64XEmitter::CMP(ARM64Reg Rn, ARM64Reg Rm, ArithOption Option)
{
  EncodeArithmeticInst(1, true, Is64Bit(Rn) ? ARM64Reg::ZR : ARM64Reg::WZR, Rn, Rm, Option);
}

void ARM64XEmitter::ADC(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeArithmeticCarryInst(0, false, Rd, Rn, Rm);
}

void ARM64XEmitter::ADCS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeArithmeticCarryInst(0, true, Rd, Rn, Rm);
}

void ARM64XEmitter::SBC(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeArithmeticCarryInst(1, false, Rd, Rn, Rm);
}

void ARM64XEmitter::SBCS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeArithmeticCarryInst(1, true, Rd, Rn, Rm);
}

void ARM64XEmitter::CCMN(ARM64Reg Rn, u32 imm, u32 nzcv, CCFlags cond)
{
  EncodeCondCompareImmInst(0, Rn, imm, nzcv, cond);
}

void ARM64XEmitter::CCMP(ARM64Reg Rn, u32 imm, u32 nzcv, CCFlags cond)
{
  EncodeCondCompareImmInst(1, Rn, imm, nzcv, cond);
}

void ARM64XEmitter::CCMN(ARM64Reg Rn, ARM64Reg Rm, u32 nzcv, CCFlags cond)
{
  EncodeCondCompareRegInst(0, Rn, Rm, nzcv, cond);
}

void ARM64XEmitter::CCMP(ARM64Reg Rn, ARM64Reg Rm, u32 nzcv, CCFlags cond)
{
  EncodeCondCompareRegInst(1, Rn, Rm, nzcv, cond);
}

void ARM64XEmitter::CSEL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, CCFlags cond)
{
  EncodeCondSelectInst(0, Rd, Rn, Rm, cond);
}

void ARM64XEmitter::CSINC(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, CCFlags cond)
{
  EncodeCondSelectInst(1, Rd, Rn, Rm, cond);
}

void ARM64XEmitter::CSINV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, CCFlags cond)
{
  EncodeCondSelectInst(2, Rd, Rn, Rm, cond);
}

void ARM64XEmitter::CSNEG(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, CCFlags cond)
{
  EncodeCondSelectInst(3, Rd, Rn, Rm, cond);
}

void ARM64XEmitter::RBIT(ARM64Reg Rd, ARM64Reg Rn)
{
  EncodeData1SrcInst(0, Rd, Rn);
}

void ARM64XEmitter::REV16(ARM64Reg Rd, ARM64Reg Rn)
{
  EncodeData1SrcInst(1, Rd, Rn);
}

void ARM64XEmitter::REV32(ARM64Reg Rd, ARM64Reg Rn)
{
  EncodeData1SrcInst(2, Rd, Rn);
}

void ARM64XEmitter::REV64(ARM64Reg Rd, ARM64Reg Rn)
{
  EncodeData1SrcInst(3, Rd, Rn);
}

void ARM64XEmitter::CLZ(ARM64Reg Rd, ARM64Reg Rn)
{
  EncodeData1SrcInst(4, Rd, Rn);
}

void ARM64XEmitter::CLS(ARM64Reg Rd, ARM64Reg Rn)
{
  EncodeData1SrcInst(5, Rd, Rn);
}

void ARM64XEmitter::UDIV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(0, Rd, Rn, Rm);
}

void ARM64XEmitter::SDIV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(1, Rd, Rn, Rm);
}

void ARM64XEmitter::LSLV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(2, Rd, Rn, Rm);
}

void ARM64XEmitter::LSRV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(3, Rd, Rn, Rm);
}

void ARM64XEmitter::ASRV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(4, Rd, Rn, Rm);
}

void ARM64XEmitter::RORV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(5, Rd, Rn, Rm);
}

void ARM64XEmitter::CRC32B(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(6, Rd, Rn, Rm);
}

void ARM64XEmitter::CRC32H(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(7, Rd, Rn, Rm);
}

void ARM64XEmitter::CRC32W(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(8, Rd, Rn, Rm);
}

void ARM64XEmitter::CRC32CB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(9, Rd, Rn, Rm);
}

void ARM64XEmitter::CRC32CH(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(10, Rd, Rn, Rm);
}

void ARM64XEmitter::CRC32CW(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(11, Rd, Rn, Rm);
}

void ARM64XEmitter::CRC32X(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(12, Rd, Rn, Rm);
}

void ARM64XEmitter::CRC32CX(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData2SrcInst(13, Rd, Rn, Rm);
}

void ARM64XEmitter::MADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra)
{
  EncodeData3SrcInst(0, Rd, Rn, Rm, Ra);
}

void ARM64XEmitter::MSUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra)
{
  EncodeData3SrcInst(1, Rd, Rn, Rm, Ra);
}

void ARM64XEmitter::SMADDL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra)
{
  EncodeData3SrcInst(2, Rd, Rn, Rm, Ra);
}

void ARM64XEmitter::SMULL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  SMADDL(Rd, Rn, Rm, ARM64Reg::SP);
}

void ARM64XEmitter::SMSUBL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra)
{
  EncodeData3SrcInst(3, Rd, Rn, Rm, Ra);
}

void ARM64XEmitter::SMULH(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData3SrcInst(4, Rd, Rn, Rm, ARM64Reg::SP);
}

void ARM64XEmitter::UMADDL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra)
{
  EncodeData3SrcInst(5, Rd, Rn, Rm, Ra);
}

void ARM64XEmitter::UMULL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  UMADDL(Rd, Rn, Rm, ARM64Reg::SP);
}

void ARM64XEmitter::UMSUBL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra)
{
  EncodeData3SrcInst(6, Rd, Rn, Rm, Ra);
}

void ARM64XEmitter::UMULH(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData3SrcInst(7, Rd, Rn, Rm, ARM64Reg::SP);
}

void ARM64XEmitter::MUL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData3SrcInst(0, Rd, Rn, Rm, ARM64Reg::SP);
}

void ARM64XEmitter::MNEG(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
{
  EncodeData3SrcInst(1, Rd, Rn, Rm, ARM64Reg::SP);
}

void ARM64XEmitter::AND(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift)
{
  EncodeLogicalInst(0, Rd, Rn, Rm, Shift);
}

void ARM64XEmitter::BIC(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift)
{
  EncodeLogicalInst(1, Rd, Rn, Rm, Shift);
}

void ARM64XEmitter::ORR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift)
{
  EncodeLogicalInst(2, Rd, Rn, Rm, Shift);
}

void ARM64XEmitter::ORN(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift)
{
  EncodeLogicalInst(3, Rd, Rn, Rm, Shift);
}

void ARM64XEmitter::EOR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift)
{
  EncodeLogicalInst(4, Rd, Rn, Rm, Shift);
}

void ARM64XEmitter::EON(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift)
{
  EncodeLogicalInst(5, Rd, Rn, Rm, Shift);
}

void ARM64XEmitter::ANDS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift)
{
  EncodeLogicalInst(6, Rd, Rn, Rm, Shift);
}

void ARM64XEmitter::BICS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift)
{
  EncodeLogicalInst(7, Rd, Rn, Rm, Shift);
}

void ARM64XEmitter::MOV(ARM64Reg Rd, ARM64Reg Rm, ArithOption Shift)
{
  ORR(Rd, Is64Bit(Rd) ? ARM64Reg::ZR : ARM64Reg::WZR, Rm, Shift);
}

void ARM64XEmitter::MOV(ARM64Reg Rd, ARM64Reg Rm)
{
  if (IsGPR(Rd) && IsGPR(Rm))
    ORR(Rd, Is64Bit(Rd) ? ARM64Reg::ZR : ARM64Reg::WZR, Rm, ArithOption(Rm, ShiftType::LSL, 0));
  else
    ASSERT_MSG(DYNA_REC, false, "Non-GPRs not supported in MOV");
}

void ARM64XEmitter::MVN(ARM64Reg Rd, ARM64Reg Rm)
{
  ORN(Rd, Is64Bit(Rd) ? ARM64Reg::ZR : ARM64Reg::WZR, Rm, ArithOption(Rm, ShiftType::LSL, 0));
}

void ARM64XEmitter::LSL(ARM64Reg Rd, ARM64Reg Rm, int shift)
{
  int bits = Is64Bit(Rd) ? 64 : 32;
  UBFM(Rd, Rm, (bits - shift) & (bits - 1), bits - shift - 1);
}

void ARM64XEmitter::LSR(ARM64Reg Rd, ARM64Reg Rm, int shift)
{
  int bits = Is64Bit(Rd) ? 64 : 32;
  UBFM(Rd, Rm, shift, bits - 1);
}

void ARM64XEmitter::ASR(ARM64Reg Rd, ARM64Reg Rm, int shift)
{
  int bits = Is64Bit(Rd) ? 64 : 32;
  SBFM(Rd, Rm, shift, bits - 1);
}

void ARM64XEmitter::ROR(ARM64Reg Rd, ARM64Reg Rm, int shift)
{
  EXTR(Rd, Rm, Rm, shift);
}

void ARM64XEmitter::AND(ARM64Reg Rd, ARM64Reg Rn, LogicalImm imm)
{
  EncodeLogicalImmInst(0, Rd, Rn, imm);
}

void ARM64XEmitter::ANDS(ARM64Reg Rd, ARM64Reg Rn, LogicalImm imm)
{
  EncodeLogicalImmInst(3, Rd, Rn, imm);
}

void ARM64XEmitter::EOR(ARM64Reg Rd, ARM64Reg Rn, LogicalImm imm)
{
  EncodeLogicalImmInst(2, Rd, Rn, imm);
}

void ARM64XEmitter::ORR(ARM64Reg Rd, ARM64Reg Rn, LogicalImm imm)
{
  EncodeLogicalImmInst(1, Rd, Rn, imm);
}

void ARM64XEmitter::TST(ARM64Reg Rn, LogicalImm imm)
{
  EncodeLogicalImmInst(3, Is64Bit(Rn) ? ARM64Reg::ZR : ARM64Reg::WZR, Rn, imm);
}

void ARM64XEmitter::ADD(ARM64Reg Rd, ARM64Reg Rn, u32 imm, bool shift)
{
  EncodeAddSubImmInst(0, false, shift, imm, Rn, Rd);
}

void ARM64XEmitter::ADDS(ARM64Reg Rd, ARM64Reg Rn, u32 imm, bool shift)
{
  EncodeAddSubImmInst(0, true, shift, imm, Rn, Rd);
}

void ARM64XEmitter::SUB(ARM64Reg Rd, ARM64Reg Rn, u32 imm, bool shift)
{
  EncodeAddSubImmInst(1, false, shift, imm, Rn, Rd);
}

void ARM64XEmitter::SUBS(ARM64Reg Rd, ARM64Reg Rn, u32 imm, bool shift)
{
  EncodeAddSubImmInst(1, true, shift, imm, Rn, Rd);
}

void ARM64XEmitter::CMP(ARM64Reg Rn, u32 imm, bool shift)
{
  EncodeAddSubImmInst(1, true, shift, imm, Rn, Is64Bit(Rn) ? ARM64Reg::SP : ARM64Reg::WSP);
}

void ARM64XEmitter::CMN(ARM64Reg Rn, u32 imm, bool shift)
{
  EncodeAddSubImmInst(0, true, shift, imm, Rn, Is64Bit(Rn) ? ARM64Reg::SP : ARM64Reg::WSP);
}

void ARM64XEmitter::MOVZ(ARM64Reg Rd, u32 imm, ShiftAmount pos)
{
  EncodeMOVWideInst(2, Rd, imm, pos);
}

void ARM64XEmitter::MOVN(ARM64Reg Rd, u32 imm, ShiftAmount pos)
{
  EncodeMOVWideInst(0, Rd, imm, pos);
}

void ARM64XEmitter::MOVK(ARM64Reg Rd, u32 imm, ShiftAmount pos)
{
  EncodeMOVWideInst(3, Rd, imm, pos);
}

void ARM64XEmitter::BFM(ARM64Reg Rd, ARM64Reg Rn, u32 immr, u32 imms)
{
  EncodeBitfieldMOVInst(1, Rd, Rn, immr, imms);
}

void ARM64XEmitter::SBFM(ARM64Reg Rd, ARM64Reg Rn, u32 immr, u32 imms)
{
  EncodeBitfieldMOVInst(0, Rd, Rn, immr, imms);
}

void ARM64XEmitter::UBFM(ARM64Reg Rd, ARM64Reg Rn, u32 immr, u32 imms)
{
  EncodeBitfieldMOVInst(2, Rd, Rn, immr, imms);
}

void ARM64XEmitter::BFI(ARM64Reg Rd, ARM64Reg Rn, u32 lsb, u32 width)
{
  u32 size = Is64Bit(Rn) ? 64 : 32;
  ASSERT_MSG(DYNA_REC, lsb < size && width >= 1 && width <= size - lsb,
             "lsb {} and width {} is greater than the register size {}!", lsb, width, size);
  BFM(Rd, Rn, (size - lsb) % size, width - 1);
}

void ARM64XEmitter::BFXIL(ARM64Reg Rd, ARM64Reg Rn, u32 lsb, u32 width)
{
  u32 size = Is64Bit(Rn) ? 64 : 32;
  ASSERT_MSG(DYNA_REC, lsb < size && width >= 1 && width <= size - lsb,
             "lsb {} and width {} is greater than the register size {}!", lsb, width, size);
  BFM(Rd, Rn, lsb, lsb + width - 1);
}

void ARM64XEmitter::UBFIZ(ARM64Reg Rd, ARM64Reg Rn, u32 lsb, u32 width)
{
  u32 size = Is64Bit(Rn) ? 64 : 32;
  ASSERT_MSG(DYNA_REC, lsb < size && width >= 1 && width <= size - lsb,
             "lsb {} and width {} is greater than the register size {}!", lsb, width, size);
  UBFM(Rd, Rn, (size - lsb) % size, width - 1);
}

void ARM64XEmitter::EXTR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, u32 shift)
{
  bool sf = Is64Bit(Rd);
  bool N = sf;

  Write32((sf << 31) | (0x27 << 23) | (N << 22) | (DecodeReg(Rm) << 16) | (shift << 10) |
          (DecodeReg(Rm) << 5) | DecodeReg(Rd));
}

void ARM64XEmitter::SXTB(ARM64Reg Rd, ARM64Reg Rn)
{
  SBFM(Rd, Rn, 0, 7);
}

void ARM64XEmitter::SXTH(ARM64Reg Rd, ARM64Reg Rn)
{
  SBFM(Rd, Rn, 0, 15);
}

void ARM64XEmitter::SXTW(ARM64Reg Rd, ARM64Reg Rn)
{
  ASSERT_MSG(DYNA_REC, Is64Bit(Rd), "64bit register required as destination");
  SBFM(Rd, Rn, 0, 31);
}

void ARM64XEmitter::UXTB(ARM64Reg Rd, ARM64Reg Rn)
{
  UBFM(Rd, Rn, 0, 7);
}

void ARM64XEmitter::UXTH(ARM64Reg Rd, ARM64Reg Rn)
{
  UBFM(Rd, Rn, 0, 15);
}

template <typename T>
void ARM64XEmitter::MOVI2RImpl(ARM64Reg Rd, T imm)
{
  enum class Approach
  {
    MOVZBase,
    MOVNBase,
    ADRBase,
    ADRPBase,
    ORRBase,
  };

  struct Part
  {
    Part() = default;
    Part(u16 imm_, ShiftAmount shift_) : imm(imm_), shift(shift_) {}

    u16 imm;
    ShiftAmount shift;
  };

  constexpr size_t max_parts = sizeof(T) / 2;

  Common::SmallVector<Part, max_parts> best_parts;
  Approach best_approach;
  u64 best_base;

  const auto instructions_required = [](const Common::SmallVector<Part, max_parts>& parts,
                                        Approach approach) {
    return parts.size() + (approach > Approach::MOVNBase);
  };

  const auto try_base = [&](T base, Approach approach, bool first_time) {
    Common::SmallVector<Part, max_parts> parts;

    for (size_t i = 0; i < max_parts; ++i)
    {
      const size_t shift = i * 16;
      const u16 imm_shifted = static_cast<u16>(imm >> shift);
      const u16 base_shifted = static_cast<u16>(base >> shift);
      if (imm_shifted != base_shifted)
        parts.emplace_back(imm_shifted, static_cast<ShiftAmount>(i));
    }

    if (first_time ||
        instructions_required(parts, approach) < instructions_required(best_parts, best_approach))
    {
      best_parts = std::move(parts);
      best_approach = approach;
      best_base = base;
    }
  };

  // Try MOVZ/MOVN
  try_base(T(0), Approach::MOVZBase, true);
  try_base(~T(0), Approach::MOVNBase, false);

  // Try PC-relative approaches
  const auto sext_21_bit = [](u64 x) {
    return static_cast<s64>((x & 0x1FFFFF) | (x & 0x100000 ? ~0x1FFFFF : 0));
  };
  const u64 pc = reinterpret_cast<u64>(GetCodePtr());
  const s64 adrp_offset = sext_21_bit((imm >> 12) - (pc >> 12)) << 12;
  const s64 adr_offset = sext_21_bit(imm - pc);
  const u64 adrp_base = (pc & ~0xFFF) + adrp_offset;
  const u64 adr_base = pc + adr_offset;
  if constexpr (sizeof(T) == 8)
  {
    try_base(adrp_base, Approach::ADRPBase, false);
    try_base(adr_base, Approach::ADRBase, false);
  }

  // Try ORR (or skip it if we already have a 1-instruction encoding - these tests are non-trivial)
  if (instructions_required(best_parts, best_approach) > 1)
  {
    if constexpr (sizeof(T) == 8)
    {
      for (u64 orr_imm : {imm, (imm << 32) | (imm & 0x0000'0000'FFFF'FFFF),
                          (imm & 0xFFFF'FFFF'0000'0000) | (imm >> 32),
                          (imm << 48) | (imm & 0x0000'FFFF'FFFF'0000) | (imm >> 48)})
      {
        if (LogicalImm(orr_imm, GPRSize::B64))
        {
          try_base(orr_imm, Approach::ORRBase, false);

          if (instructions_required(best_parts, best_approach) <= 1)
            break;
        }
      }
    }
    else
    {
      if (LogicalImm(imm, GPRSize::B32))
        try_base(imm, Approach::ORRBase, false);
    }
  }

  size_t parts_uploaded = 0;

  // To kill any dependencies, we start with an instruction that overwrites the entire register
  switch (best_approach)
  {
  case Approach::MOVZBase:
    if (best_parts.empty())
      best_parts.emplace_back(u16(0), ShiftAmount::Shift0);

    MOVZ(Rd, best_parts[0].imm, best_parts[0].shift);
    ++parts_uploaded;
    break;

  case Approach::MOVNBase:
    if (best_parts.empty())
      best_parts.emplace_back(u16(0xFFFF), ShiftAmount::Shift0);

    MOVN(Rd, static_cast<u16>(~best_parts[0].imm), best_parts[0].shift);
    ++parts_uploaded;
    break;

  case Approach::ADRBase:
    ADR(Rd, adr_offset);
    break;

  case Approach::ADRPBase:
    ADRP(Rd, adrp_offset);
    break;

  case Approach::ORRBase:
    constexpr ARM64Reg zero_reg = sizeof(T) == 8 ? ARM64Reg::ZR : ARM64Reg::WZR;
    const bool success = TryORRI2R(Rd, zero_reg, best_base);
    ASSERT(success);
    break;
  }

  // And then we use MOVK for the remaining parts
  for (; parts_uploaded < best_parts.size(); ++parts_uploaded)
  {
    const Part& part = best_parts[parts_uploaded];

    if (best_approach == Approach::ADRPBase && part.shift == ShiftAmount::Shift0)
    {
      // The combination of ADRP followed by ADD immediate is specifically optimized in hardware
      ASSERT(part.imm == (adrp_base & 0xF000) + (part.imm & 0xFFF));
      ADD(Rd, Rd, part.imm & 0xFFF);
    }
    else
    {
      MOVK(Rd, part.imm, part.shift);
    }
  }
}

template void ARM64XEmitter::MOVI2RImpl(ARM64Reg Rd, u64 imm);
template void ARM64XEmitter::MOVI2RImpl(ARM64Reg Rd, u32 imm);

void ARM64XEmitter::MOVI2R(ARM64Reg Rd, u64 imm)
{
  if (Is64Bit(Rd))
    MOVI2RImpl<u64>(Rd, imm);
  else
    MOVI2RImpl<u32>(Rd, static_cast<u32>(imm));
}

bool ARM64XEmitter::MOVI2R2(ARM64Reg Rd, u64 imm1, u64 imm2)
{
  // TODO: Also optimize for performance, not just for code size.
  u8* start_pointer = GetWritableCodePtr();

  MOVI2R(Rd, imm1);
  int size1 = GetCodePtr() - start_pointer;

  m_code = start_pointer;

  MOVI2R(Rd, imm2);
  int size2 = GetCodePtr() - start_pointer;

  m_code = start_pointer;

  bool element = size1 > size2;

  MOVI2R(Rd, element ? imm2 : imm1);

  return element;
}

void ARM64XEmitter::CMPI2R(ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  ADDI2R_internal(Is64Bit(Rn) ? ARM64Reg::ZR : ARM64Reg::WZR, Rn, imm, true, true, scratch);
}

bool ARM64XEmitter::TryADDI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm)
{
  if (const auto result = IsImmArithmetic(imm))
  {
    const auto [val, shift] = *result;
    ADD(Rd, Rn, val, shift);
    return true;
  }

  return false;
}

bool ARM64XEmitter::TrySUBI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm)
{
  if (const auto result = IsImmArithmetic(imm))
  {
    const auto [val, shift] = *result;
    SUB(Rd, Rn, val, shift);
    return true;
  }

  return false;
}

bool ARM64XEmitter::TryCMPI2R(ARM64Reg Rn, u64 imm)
{
  if (const auto result = IsImmArithmetic(imm))
  {
    const auto [val, shift] = *result;
    CMP(Rn, val, shift);
    return true;
  }

  return false;
}

bool ARM64XEmitter::TryANDI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm)
{
  if (const auto result = LogicalImm(imm, Is64Bit(Rd) ? GPRSize::B64 : GPRSize::B32))
  {
    AND(Rd, Rn, result);
    return true;
  }

  return false;
}

bool ARM64XEmitter::TryORRI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm)
{
  if (const auto result = LogicalImm(imm, Is64Bit(Rd) ? GPRSize::B64 : GPRSize::B32))
  {
    ORR(Rd, Rn, result);
    return true;
  }

  return false;
}

bool ARM64XEmitter::TryEORI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm)
{
  if (const auto result = LogicalImm(imm, Is64Bit(Rd) ? GPRSize::B64 : GPRSize::B32))
  {
    EOR(Rd, Rn, result);
    return true;
  }

  return false;
}



void ARM64XEmitter::ANDI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  if (!Is64Bit(Rn))
  {
    // To handle 32-bit logical immediates, the very easiest thing is to repeat
    // the input value twice to make a 64-bit word. The correct encoding of that
    // as a logical immediate will also be the correct encoding of the 32-bit
    // value.
    //
    // Doing this here instead of in the LogicalImm constructor makes it easier
    // to check if the input is all ones.

    imm = (imm << 32) | (imm & 0xFFFFFFFF);
  }

  if (imm == 0)
  {
    MOVZ(Rd, 0);
  }
  else if ((~imm) == 0)
  {
    if (Rd != Rn)
      MOV(Rd, Rn);
  }
  else if (const auto result = LogicalImm(imm, GPRSize::B64))
  {
    AND(Rd, Rn, result);
  }
  else
  {
    ASSERT_MSG(DYNA_REC, scratch != ARM64Reg::INVALID_REG,
               "ANDI2R - failed to construct logical immediate value from {:#10x}, need scratch",
               imm);
    MOVI2R(scratch, imm);
    AND(Rd, Rn, scratch);
  }
}

void ARM64XEmitter::ORRI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  if (!Is64Bit(Rn))
  {
    // To handle 32-bit logical immediates, the very easiest thing is to repeat
    // the input value twice to make a 64-bit word. The correct encoding of that
    // as a logical immediate will also be the correct encoding of the 32-bit
    // value.
    //
    // Doing this here instead of in the LogicalImm constructor makes it easier
    // to check if the input is all ones.

    imm = (imm << 32) | (imm & 0xFFFFFFFF);
  }

  if (imm == 0)
  {
    if (Rd != Rn)
      MOV(Rd, Rn);
  }
  else if ((~imm) == 0)
  {
    MOVN(Rd, 0);
  }
  else if (const auto result = LogicalImm(imm, GPRSize::B64))
  {
    ORR(Rd, Rn, result);
  }
  else
  {
    ASSERT_MSG(DYNA_REC, scratch != ARM64Reg::INVALID_REG,
               "ORRI2R - failed to construct logical immediate value from {:#10x}, need scratch",
               imm);
    MOVI2R(scratch, imm);
    ORR(Rd, Rn, scratch);
  }
}

void ARM64XEmitter::EORI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  if (!Is64Bit(Rn))
  {
    // To handle 32-bit logical immediates, the very easiest thing is to repeat
    // the input value twice to make a 64-bit word. The correct encoding of that
    // as a logical immediate will also be the correct encoding of the 32-bit
    // value.
    //
    // Doing this here instead of in the LogicalImm constructor makes it easier
    // to check if the input is all ones.

    imm = (imm << 32) | (imm & 0xFFFFFFFF);
  }

  if (imm == 0)
  {
    if (Rd != Rn)
      MOV(Rd, Rn);
  }
  else if ((~imm) == 0)
  {
    MVN(Rd, Rn);
  }
  else if (const auto result = LogicalImm(imm, GPRSize::B64))
  {
    EOR(Rd, Rn, result);
  }
  else
  {
    ASSERT_MSG(DYNA_REC, scratch != ARM64Reg::INVALID_REG,
               "EORI2R - failed to construct logical immediate value from {:#10x}, need scratch",
               imm);
    MOVI2R(scratch, imm);
    EOR(Rd, Rn, scratch);
  }
}

void ARM64XEmitter::ANDSI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  if (!Is64Bit(Rn))
  {
    // To handle 32-bit logical immediates, the very easiest thing is to repeat
    // the input value twice to make a 64-bit word. The correct encoding of that
    // as a logical immediate will also be the correct encoding of the 32-bit
    // value.
    //
    // Doing this here instead of in the LogicalImm constructor makes it easier
    // to check if the input is all ones.

    imm = (imm << 32) | (imm & 0xFFFFFFFF);
  }

  if (imm == 0)
  {
    ANDS(Rd, Is64Bit(Rn) ? ARM64Reg::ZR : ARM64Reg::WZR,
         Is64Bit(Rn) ? ARM64Reg::ZR : ARM64Reg::WZR);
  }
  else if ((~imm) == 0)
  {
    ANDS(Rd, Rn, Rn);
  }
  else if (const auto result = LogicalImm(imm, GPRSize::B64))
  {
    ANDS(Rd, Rn, result);
  }
  else
  {
    ASSERT_MSG(DYNA_REC, scratch != ARM64Reg::INVALID_REG,
               "ANDSI2R - failed to construct logical immediate value from {:#10x}, need scratch",
               imm);
    MOVI2R(scratch, imm);
    ANDS(Rd, Rn, scratch);
  }
}

void ARM64XEmitter::AddImmediate(ARM64Reg Rd, ARM64Reg Rn, u64 imm, bool shift, bool negative,
                                 bool flags)
{
  if (!negative)
  {
    if (!flags)
      ADD(Rd, Rn, imm, shift);
    else
      ADDS(Rd, Rn, imm, shift);
  }
  else
  {
    if (!flags)
      SUB(Rd, Rn, imm, shift);
    else
      SUBS(Rd, Rn, imm, shift);
  }
}

void ARM64XEmitter::ADDI2R_internal(ARM64Reg Rd, ARM64Reg Rn, u64 imm, bool negative, bool flags,
                                    ARM64Reg scratch)
{
  DEBUG_ASSERT(Is64Bit(Rd) == Is64Bit(Rn));

  if (!Is64Bit(Rd))
    imm &= 0xFFFFFFFFULL;

  bool has_scratch = scratch != ARM64Reg::INVALID_REG;
  u64 imm_neg = Is64Bit(Rd) ? u64(-s64(imm)) : u64(-s64(imm)) & 0xFFFFFFFFuLL;
  bool neg_neg = negative ? false : true;

  // Special path for zeroes
  if (imm == 0 && !flags)
  {
    if (Rd == Rn)
    {
      return;
    }
    else if (DecodeReg(Rd) != DecodeReg(ARM64Reg::SP) && DecodeReg(Rn) != DecodeReg(ARM64Reg::SP))
    {
      MOV(Rd, Rn);
      return;
    }
  }

  // Regular fast paths, aarch64 immediate instructions
  // Try them all first
  if (imm <= 0xFFF)
  {
    AddImmediate(Rd, Rn, imm, false, negative, flags);
    return;
  }
  if (imm <= 0xFFFFFF && (imm & 0xFFF) == 0)
  {
    AddImmediate(Rd, Rn, imm >> 12, true, negative, flags);
    return;
  }
  if (imm_neg <= 0xFFF)
  {
    AddImmediate(Rd, Rn, imm_neg, false, neg_neg, flags);
    return;
  }
  if (imm_neg <= 0xFFFFFF && (imm_neg & 0xFFF) == 0)
  {
    AddImmediate(Rd, Rn, imm_neg >> 12, true, neg_neg, flags);
    return;
  }

  // ADD+ADD is slower than MOVK+ADD, but inplace.
  // But it supports a few more bits, so use it to avoid MOVK+MOVK+ADD.
  // As this splits the addition in two parts, this must not be done on setting flags.
  if (!flags && (imm >= 0x10000u || !has_scratch) && imm < 0x1000000u)
  {
    AddImmediate(Rd, Rn, imm & 0xFFF, false, negative, false);
    AddImmediate(Rd, Rd, imm >> 12, true, negative, false);
    return;
  }
  if (!flags && (imm_neg >= 0x10000u || !has_scratch) && imm_neg < 0x1000000u)
  {
    AddImmediate(Rd, Rn, imm_neg & 0xFFF, false, neg_neg, false);
    AddImmediate(Rd, Rd, imm_neg >> 12, true, neg_neg, false);
    return;
  }

  ASSERT_MSG(DYNA_REC, has_scratch,
             "ADDI2R - failed to construct arithmetic immediate value from {:#10x}, need scratch",
             imm);

  negative ^= MOVI2R2(scratch, imm, imm_neg);
  if (!negative)
  {
    if (!flags)
      ADD(Rd, Rn, scratch);
    else
      ADDS(Rd, Rn, scratch);
  }
  else
  {
    if (!flags)
      SUB(Rd, Rn, scratch);
    else
      SUBS(Rd, Rn, scratch);
  }
}

void ARM64XEmitter::ADDI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  ADDI2R_internal(Rd, Rn, imm, false, false, scratch);
}

void ARM64XEmitter::ADDSI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  ADDI2R_internal(Rd, Rn, imm, false, true, scratch);
}

void ARM64XEmitter::SUBI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  ADDI2R_internal(Rd, Rn, imm, true, false, scratch);
}

void ARM64XEmitter::SUBSI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch)
{
  ADDI2R_internal(Rd, Rn, imm, true, true, scratch);
}

}  // namespace Arm64Gen
