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

#include "Common/Arm64EmitterTypes.h"

namespace Arm64Gen
{
class ARM64XEmitter
{
  friend class ARM64FloatEmitter;

private:
  struct RegisterMove
  {
    ARM64Reg dst;
    ARM64Reg src;
  };

  // Pointer to memory where code will be emitted to.
  u8* m_code = nullptr;

  // Pointer past the end of the memory region we're allowed to emit to.
  // Writes that would reach this memory are refused and will set the m_write_failed flag instead.
  u8* m_code_end = nullptr;

  u8* m_lastCacheFlushEnd = nullptr;

  // Set to true when a write request happens that would write past m_code_end.
  // Must be cleared with SetCodePtr() afterwards.
  bool m_write_failed = false;

  void AddImmediate(ARM64Reg Rd, ARM64Reg Rn, u64 imm, bool shift, bool negative, bool flags);
  void EncodeCompareBranchInst(u32 op, ARM64Reg Rt, const void* ptr);
  void EncodeTestBranchInst(u32 op, ARM64Reg Rt, u8 bits, const void* ptr);
  void EncodeUnconditionalBranchInst(u32 op, const void* ptr);
  void EncodeUnconditionalBranchInst(u32 opc, u32 op2, u32 op3, u32 op4, ARM64Reg Rn);
  void EncodeExceptionInst(u32 instenc, u32 imm);
  void EncodeSystemInst(u32 op0, u32 op1, u32 CRn, u32 CRm, u32 op2, ARM64Reg Rt);
  void EncodeArithmeticInst(u32 instenc, bool flags, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm,
                            ArithOption Option);
  void EncodeArithmeticCarryInst(u32 op, bool flags, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void EncodeCondCompareImmInst(u32 op, ARM64Reg Rn, u32 imm, u32 nzcv, CCFlags cond);
  void EncodeCondCompareRegInst(u32 op, ARM64Reg Rn, ARM64Reg Rm, u32 nzcv, CCFlags cond);
  void EncodeCondSelectInst(u32 instenc, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, CCFlags cond);
  void EncodeData1SrcInst(u32 instenc, ARM64Reg Rd, ARM64Reg Rn);
  void EncodeData2SrcInst(u32 instenc, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void EncodeData3SrcInst(u32 instenc, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra);
  void EncodeLogicalInst(u32 instenc, ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift);
  void EncodeLoadRegisterInst(u32 bitop, ARM64Reg Rt, u32 imm);
  void EncodeLoadStoreExcInst(u32 instenc, ARM64Reg Rs, ARM64Reg Rt2, ARM64Reg Rn, ARM64Reg Rt);
  void EncodeLoadStorePairedInst(u32 op, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, u32 imm);
  void EncodeLoadStoreIndexedInst(u32 op, u32 op2, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void EncodeLoadStoreIndexedInst(u32 op, ARM64Reg Rt, ARM64Reg Rn, s32 imm, u8 size);
  void EncodeMOVWideInst(u32 op, ARM64Reg Rd, u32 imm, ShiftAmount pos);
  void EncodeBitfieldMOVInst(u32 op, ARM64Reg Rd, ARM64Reg Rn, u32 immr, u32 imms);
  void EncodeLoadStoreRegisterOffset(u32 size, u32 opc, ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void EncodeAddSubImmInst(u32 op, bool flags, u32 shift, u32 imm, ARM64Reg Rn, ARM64Reg Rd);
  void EncodeLogicalImmInst(u32 op, ARM64Reg Rd, ARM64Reg Rn, LogicalImm imm);
  void EncodeLoadStorePair(u32 op, u32 load, IndexType type, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn,
                           s32 imm);
  void EncodeAddressInst(u32 op, ARM64Reg Rd, s32 imm);
  void EncodeLoadStoreUnscaled(u32 size, u32 op, ARM64Reg Rt, ARM64Reg Rn, s32 imm);

  [[nodiscard]] FixupBranch WriteFixupBranch();

  // This function solves the "parallel moves" problem common in compilers.
  // The arguments are mutated!
  void ParallelMoves(RegisterMove* begin, RegisterMove* end, std::array<u8, 32>* source_gpr_usages);

  template <typename T>
  void MOVI2RImpl(ARM64Reg Rd, T imm);

protected:
  void Write32(u32 value);

public:
  ARM64XEmitter() = default;
  ARM64XEmitter(u8* code, u8* code_end)
      : m_code(code), m_code_end(code_end), m_lastCacheFlushEnd(code)
  {
  }

  virtual ~ARM64XEmitter() {}

  void SetCodePtr(u8* ptr, u8* end, bool write_failed = false);

  void SetCodePtrUnsafe(u8* ptr, u8* end, bool write_failed = false);
  const u8* GetCodePtr() const { return m_code; }
  u8* GetWritableCodePtr() { return m_code; }
  const u8* GetCodeEnd() const { return m_code_end; }
  u8* GetWritableCodeEnd() { return m_code_end; }
  void ReserveCodeSpace(u32 bytes);
  u8* AlignCode16();
  u8* AlignCodePage();
  void FlushIcache();
  void FlushIcacheSection(u8* start, u8* end);

  // Should be checked after a block of code has been generated to see if the code has been
  // successfully written to memory. Do not call the generated code when this returns true!
  bool HasWriteFailed() const { return m_write_failed; }

  // FixupBranch branching
  void SetJumpTarget(FixupBranch const& branch);
  [[nodiscard]] FixupBranch CBZ(ARM64Reg Rt);
  [[nodiscard]] FixupBranch CBNZ(ARM64Reg Rt);
  [[nodiscard]] FixupBranch B(CCFlags cond);
  [[nodiscard]] FixupBranch TBZ(ARM64Reg Rt, u8 bit);
  [[nodiscard]] FixupBranch TBNZ(ARM64Reg Rt, u8 bit);
  [[nodiscard]] FixupBranch B();
  [[nodiscard]] FixupBranch BL();

  // Compare and Branch
  void CBZ(ARM64Reg Rt, const void* ptr);
  void CBNZ(ARM64Reg Rt, const void* ptr);

  // Conditional Branch
  void B(CCFlags cond, const void* ptr);

  // Test and Branch
  void TBZ(ARM64Reg Rt, u8 bits, const void* ptr);
  void TBNZ(ARM64Reg Rt, u8 bits, const void* ptr);

  // Unconditional Branch
  void B(const void* ptr);
  void BL(const void* ptr);

  // Unconditional Branch (register)
  void BR(ARM64Reg Rn);
  void BLR(ARM64Reg Rn);
  void RET(ARM64Reg Rn = ARM64Reg::X30);
  void ERET();
  void DRPS();

  // Exception generation
  void SVC(u32 imm);
  void HVC(u32 imm);
  void SMC(u32 imm);
  void BRK(u32 imm);
  void HLT(u32 imm);
  void DCPS1(u32 imm);
  void DCPS2(u32 imm);
  void DCPS3(u32 imm);

  // System
  void _MSR(PStateField field, u8 imm);
  void _MSR(PStateField field, ARM64Reg Rt);
  void MRS(ARM64Reg Rt, PStateField field);
  void CNTVCT(ARM64Reg Rt);

  void HINT(SystemHint op);
  void NOP() { HINT(SystemHint::NOP); }
  void SEV() { HINT(SystemHint::SEV); }
  void SEVL() { HINT(SystemHint::SEVL); }
  void WFE() { HINT(SystemHint::WFE); }
  void WFI() { HINT(SystemHint::WFI); }
  void YIELD() { HINT(SystemHint::YIELD); }

  void CLREX();
  void DSB(BarrierType type);
  void DMB(BarrierType type);
  void ISB(BarrierType type);

  // Add/Subtract (Extended/Shifted register)
  void ADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void ADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Option);
  void ADDS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void ADDS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Option);
  void SUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void SUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Option);
  void SUBS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void SUBS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Option);
  void CMN(ARM64Reg Rn, ARM64Reg Rm);
  void CMN(ARM64Reg Rn, ARM64Reg Rm, ArithOption Option);
  void CMP(ARM64Reg Rn, ARM64Reg Rm);
  void CMP(ARM64Reg Rn, ARM64Reg Rm, ArithOption Option);

  // Add/Subtract (with carry)
  void ADC(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void ADCS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void SBC(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void SBCS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);

  // Conditional Compare (immediate)
  void CCMN(ARM64Reg Rn, u32 imm, u32 nzcv, CCFlags cond);
  void CCMP(ARM64Reg Rn, u32 imm, u32 nzcv, CCFlags cond);

  // Conditional Compare (register)
  void CCMN(ARM64Reg Rn, ARM64Reg Rm, u32 nzcv, CCFlags cond);
  void CCMP(ARM64Reg Rn, ARM64Reg Rm, u32 nzcv, CCFlags cond);

  // Conditional Select
  void CSEL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, CCFlags cond);
  void CSINC(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, CCFlags cond);
  void CSINV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, CCFlags cond);
  void CSNEG(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, CCFlags cond);

  // Aliases
  void CSET(ARM64Reg Rd, CCFlags cond)
  {
    ARM64Reg zr = Is64Bit(Rd) ? ARM64Reg::ZR : ARM64Reg::WZR;
    CSINC(Rd, zr, zr, (CCFlags)((u32)cond ^ 1));
  }
  void CSETM(ARM64Reg Rd, CCFlags cond)
  {
    ARM64Reg zr = Is64Bit(Rd) ? ARM64Reg::ZR : ARM64Reg::WZR;
    CSINV(Rd, zr, zr, (CCFlags)((u32)cond ^ 1));
  }
  void CNEG(ARM64Reg Rd, ARM64Reg Rn, CCFlags cond) { CSNEG(Rd, Rn, Rn, (CCFlags)((u32)cond ^ 1)); }
  void NEG(ARM64Reg Rd, ARM64Reg Rs) { SUB(Rd, Is64Bit(Rd) ? ARM64Reg::ZR : ARM64Reg::WZR, Rs); }
  void NEG(ARM64Reg Rd, ARM64Reg Rs, ArithOption Option)
  {
    SUB(Rd, Is64Bit(Rd) ? ARM64Reg::ZR : ARM64Reg::WZR, Rs, Option);
  }
  void NEGS(ARM64Reg Rd, ARM64Reg Rs) { SUBS(Rd, Is64Bit(Rd) ? ARM64Reg::ZR : ARM64Reg::WZR, Rs); }
  void NEGS(ARM64Reg Rd, ARM64Reg Rs, ArithOption Option)
  {
    SUBS(Rd, Is64Bit(Rd) ? ARM64Reg::ZR : ARM64Reg::WZR, Rs, Option);
  }
  // Data-Processing 1 source
  void RBIT(ARM64Reg Rd, ARM64Reg Rn);
  void REV16(ARM64Reg Rd, ARM64Reg Rn);
  void REV32(ARM64Reg Rd, ARM64Reg Rn);
  void REV64(ARM64Reg Rd, ARM64Reg Rn);
  void CLZ(ARM64Reg Rd, ARM64Reg Rn);
  void CLS(ARM64Reg Rd, ARM64Reg Rn);

  // Data-Processing 2 source
  void UDIV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void SDIV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void LSLV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void LSRV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void ASRV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void RORV(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CRC32B(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CRC32H(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CRC32W(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CRC32CB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CRC32CH(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CRC32CW(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CRC32X(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void CRC32CX(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);

  // Data-Processing 3 source
  void MADD(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra);
  void MSUB(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra);
  void SMADDL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra);
  void SMULL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void SMSUBL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra);
  void SMULH(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void UMADDL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra);
  void UMULL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void UMSUBL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ARM64Reg Ra);
  void UMULH(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void MUL(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);
  void MNEG(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm);

  // Logical (shifted register)
  void AND(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift);
  void BIC(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift);
  void ORR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift);
  void ORN(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift);
  void EOR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift);
  void EON(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift);
  void ANDS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift);
  void BICS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift);
  void TST(ARM64Reg Rn, ARM64Reg Rm) { ANDS(Is64Bit(Rn) ? ARM64Reg::ZR : ARM64Reg::WZR, Rn, Rm); }
  void TST(ARM64Reg Rn, ARM64Reg Rm, ArithOption Shift)
  {
    ANDS(Is64Bit(Rn) ? ARM64Reg::ZR : ARM64Reg::WZR, Rn, Rm, Shift);
  }

  // Wrap the above for saner syntax
  void AND(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
  {
    AND(Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
  }
  void BIC(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
  {
    BIC(Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
  }
  void ORR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
  {
    ORR(Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
  }
  void ORN(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
  {
    ORN(Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
  }
  void EOR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
  {
    EOR(Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
  }
  void EON(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
  {
    EON(Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
  }
  void ANDS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
  {
    ANDS(Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
  }
  void BICS(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm)
  {
    BICS(Rd, Rn, Rm, ArithOption(Rd, ShiftType::LSL, 0));
  }
  // Convenience wrappers around ORR. These match the official convenience syntax.
  void MOV(ARM64Reg Rd, ARM64Reg Rm, ArithOption Shift);
  void MOV(ARM64Reg Rd, ARM64Reg Rm);
  void MVN(ARM64Reg Rd, ARM64Reg Rm);

  // Convenience wrappers around UBFM/EXTR.
  void LSR(ARM64Reg Rd, ARM64Reg Rm, int shift);
  void LSL(ARM64Reg Rd, ARM64Reg Rm, int shift);
  void ASR(ARM64Reg Rd, ARM64Reg Rm, int shift);
  void ROR(ARM64Reg Rd, ARM64Reg Rm, int shift);

  // Logical (immediate)
  void AND(ARM64Reg Rd, ARM64Reg Rn, LogicalImm imm);
  void ANDS(ARM64Reg Rd, ARM64Reg Rn, LogicalImm imm);
  void EOR(ARM64Reg Rd, ARM64Reg Rn, LogicalImm imm);
  void ORR(ARM64Reg Rd, ARM64Reg Rn, LogicalImm imm);
  void TST(ARM64Reg Rn, LogicalImm imm);
  // Add/subtract (immediate)
  void ADD(ARM64Reg Rd, ARM64Reg Rn, u32 imm, bool shift = false);
  void ADDS(ARM64Reg Rd, ARM64Reg Rn, u32 imm, bool shift = false);
  void SUB(ARM64Reg Rd, ARM64Reg Rn, u32 imm, bool shift = false);
  void SUBS(ARM64Reg Rd, ARM64Reg Rn, u32 imm, bool shift = false);
  void CMP(ARM64Reg Rn, u32 imm, bool shift = false);
  void CMN(ARM64Reg Rn, u32 imm, bool shift = false);

  // Data Processing (Immediate)
  void MOVZ(ARM64Reg Rd, u32 imm, ShiftAmount pos = ShiftAmount::Shift0);
  void MOVN(ARM64Reg Rd, u32 imm, ShiftAmount pos = ShiftAmount::Shift0);
  void MOVK(ARM64Reg Rd, u32 imm, ShiftAmount pos = ShiftAmount::Shift0);

  // Bitfield move
  void BFM(ARM64Reg Rd, ARM64Reg Rn, u32 immr, u32 imms);
  void SBFM(ARM64Reg Rd, ARM64Reg Rn, u32 immr, u32 imms);
  void UBFM(ARM64Reg Rd, ARM64Reg Rn, u32 immr, u32 imms);
  void BFI(ARM64Reg Rd, ARM64Reg Rn, u32 lsb, u32 width);
  void BFXIL(ARM64Reg Rd, ARM64Reg Rn, u32 lsb, u32 width);
  void UBFIZ(ARM64Reg Rd, ARM64Reg Rn, u32 lsb, u32 width);

  // Extract register (ROR with two inputs, if same then faster on A67)
  void EXTR(ARM64Reg Rd, ARM64Reg Rn, ARM64Reg Rm, u32 shift);

  // Aliases
  void SXTB(ARM64Reg Rd, ARM64Reg Rn);
  void SXTH(ARM64Reg Rd, ARM64Reg Rn);
  void SXTW(ARM64Reg Rd, ARM64Reg Rn);
  void UXTB(ARM64Reg Rd, ARM64Reg Rn);
  void UXTH(ARM64Reg Rd, ARM64Reg Rn);

  void UBFX(ARM64Reg Rd, ARM64Reg Rn, int lsb, int width) { UBFM(Rd, Rn, lsb, lsb + width - 1); }
  // Load Register (Literal)
  void LDR(ARM64Reg Rt, u32 imm);
  void LDRSW(ARM64Reg Rt, u32 imm);
  void PRFM(ARM64Reg Rt, u32 imm);

  // Load/Store Exclusive
  void STXRB(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn);
  void STLXRB(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn);
  void LDXRB(ARM64Reg Rt, ARM64Reg Rn);
  void LDAXRB(ARM64Reg Rt, ARM64Reg Rn);
  void STLRB(ARM64Reg Rt, ARM64Reg Rn);
  void LDARB(ARM64Reg Rt, ARM64Reg Rn);
  void STXRH(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn);
  void STLXRH(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn);
  void LDXRH(ARM64Reg Rt, ARM64Reg Rn);
  void LDAXRH(ARM64Reg Rt, ARM64Reg Rn);
  void STLRH(ARM64Reg Rt, ARM64Reg Rn);
  void LDARH(ARM64Reg Rt, ARM64Reg Rn);
  void STXR(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn);
  void STLXR(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn);
  void STXP(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn);
  void STLXP(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn);
  void LDXR(ARM64Reg Rt, ARM64Reg Rn);
  void LDAXR(ARM64Reg Rt, ARM64Reg Rn);
  void LDXP(ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn);
  void LDAXP(ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn);
  void STLR(ARM64Reg Rt, ARM64Reg Rn);
  void LDAR(ARM64Reg Rt, ARM64Reg Rn);

  // Load/Store no-allocate pair (offset)
  void STNP(ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, u32 imm);
  void LDNP(ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, u32 imm);

  // Load/Store register (immediate indexed)
  void STRB(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDRB(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDRSB(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void STRH(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDRH(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDRSH(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void STR(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDR(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDRSW(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm);

  // Load/Store register (register offset)
  void STRB(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void LDRB(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void LDRSB(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void STRH(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void LDRH(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void LDRSH(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void STR(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void LDR(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void LDRSW(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);
  void PRFM(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm);

  // Load/Store register (unscaled offset)
  void STURB(ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDURB(ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDURSB(ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void STURH(ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDURH(ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDURSH(ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void STUR(ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDUR(ARM64Reg Rt, ARM64Reg Rn, s32 imm);
  void LDURSW(ARM64Reg Rt, ARM64Reg Rn, s32 imm);

  // Load/Store pair
  void LDP(IndexType type, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, s32 imm);
  void LDPSW(IndexType type, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, s32 imm);
  void STP(IndexType type, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, s32 imm);

  // Address of label/page PC-relative
  void ADR(ARM64Reg Rd, s32 imm);
  void ADRP(ARM64Reg Rd, s64 imm);

  // Wrapper around ADR/ADRP/MOVZ/MOVN/MOVK
  void MOVI2R(ARM64Reg Rd, u64 imm);
  bool MOVI2R2(ARM64Reg Rd, u64 imm1, u64 imm2);
  template <class P>
  void MOVP2R(ARM64Reg Rd, P* ptr)
  {
    ASSERT_MSG(DYNA_REC, Is64Bit(Rd), "Can't store pointers in 32-bit registers");
    MOVI2R(Rd, reinterpret_cast<uintptr_t>(ptr));
  }
  template <class P>
  // Given an address, stores the page address into a register and returns the page-relative offset
  s32 MOVPage2R(ARM64Reg Rd, P* ptr)
  {
    ASSERT_MSG(DYNA_REC, Is64Bit(Rd), "Can't store pointers in 32-bit registers");
    MOVI2R(Rd, reinterpret_cast<uintptr_t>(ptr) & ~0xFFFULL);
    return static_cast<s32>(reinterpret_cast<uintptr_t>(ptr) & 0xFFFULL);
  }

  // Wrappers around bitwise operations with an immediate. If you're sure an imm can be encoded
  // without a scratch register, preferably construct a LogicalImm directly instead,
  // since that is constexpr and thus can be done at compile time for constant values.
  void ANDI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch);
  void ANDSI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch);
  void TSTI2R(ARM64Reg Rn, u64 imm, ARM64Reg scratch)
  {
    ANDSI2R(Is64Bit(Rn) ? ARM64Reg::ZR : ARM64Reg::WZR, Rn, imm, scratch);
  }
  void ORRI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch);
  void EORI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch);

  // Wrappers around arithmetic operations with an immediate.
  void ADDI2R_internal(ARM64Reg Rd, ARM64Reg Rn, u64 imm, bool negative, bool flags,
                       ARM64Reg scratch);
  void ADDI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch = ARM64Reg::INVALID_REG);
  void ADDSI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch = ARM64Reg::INVALID_REG);
  void SUBI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch = ARM64Reg::INVALID_REG);
  void SUBSI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm, ARM64Reg scratch = ARM64Reg::INVALID_REG);
  void CMPI2R(ARM64Reg Rn, u64 imm, ARM64Reg scratch = ARM64Reg::INVALID_REG);

  bool TryADDI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm);
  bool TrySUBI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm);
  bool TryCMPI2R(ARM64Reg Rn, u64 imm);

  bool TryANDI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm);
  bool TryORRI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm);
  bool TryEORI2R(ARM64Reg Rd, ARM64Reg Rn, u64 imm);

  // ABI related
  static constexpr BitSet32 CALLER_SAVED_GPRS = BitSet32(0x4007FFFF);
  static constexpr BitSet32 CALLER_SAVED_FPRS = BitSet32(0xFFFF00FF);
  void ABI_PushRegisters(BitSet32 registers);
  void ABI_PopRegisters(BitSet32 registers, BitSet32 ignore_mask = BitSet32(0));

  // Plain function call
  void QuickCallFunction(ARM64Reg scratchreg, const void* func);
  template <typename T>
  void QuickCallFunction(ARM64Reg scratchreg, T func)
  {
    QuickCallFunction(scratchreg, (const void*)func);
  }

  template <typename FuncRet, typename... FuncArgs, typename... Args>
  void ABI_CallFunction(FuncRet (*func)(FuncArgs...), Args... args)
  {
    static_assert(sizeof...(FuncArgs) == sizeof...(Args), "Wrong number of arguments");
    static_assert(sizeof...(FuncArgs) <= 8, "Passing arguments on the stack is not supported");

    if constexpr (!std::is_void_v<FuncRet>)
      static_assert(sizeof(FuncRet) <= 16, "Large return types are not supported");

    std::array<u8, 32> source_gpr_uses{};

    auto check_argument = [&](auto& arg) {
      using Arg = std::decay_t<decltype(arg)>;

      if constexpr (std::is_same_v<Arg, ARM64Reg>)
      {
        ASSERT(IsGPR(arg));
        source_gpr_uses[DecodeReg(arg)]++;
      }
      else
      {
        // To be more correct, we should be checking FuncArgs here rather than Args, but that's a
        // lot more effort to implement. Let's just do these best-effort checks for now.
        static_assert(!std::is_floating_point_v<Arg>, "Floating-point arguments are not supported");
        static_assert(sizeof(Arg) <= 8, "Arguments bigger than a register are not supported");
      }
    };

    (check_argument(args), ...);

    {
      Common::SmallVector<RegisterMove, sizeof...(Args)> pending_moves;

      size_t i = 0;

      auto handle_register_argument = [&](auto& arg) {
        using Arg = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<Arg, ARM64Reg>)
        {
          const ARM64Reg dst_reg =
              (Is64Bit(arg) ? EncodeRegTo64 : EncodeRegTo32)(static_cast<ARM64Reg>(i));

          if (dst_reg == arg)
          {
            // The value is already in the right register.
            source_gpr_uses[DecodeReg(arg)]--;
          }
          else if (source_gpr_uses[i] == 0)
          {
            // The destination register isn't used as the source of another move.
            // We can go ahead and do the move right away.
            MOV(dst_reg, arg);
            source_gpr_uses[DecodeReg(arg)]--;
          }
          else
          {
            // The destination register is used as the source of a move we haven't gotten to yet.
            // Let's record that we need to deal with this move later.
            pending_moves.emplace_back(dst_reg, arg);
          }
        }

        ++i;
      };

      (handle_register_argument(args), ...);

      if (!pending_moves.empty())
      {
        ParallelMoves(pending_moves.data(), pending_moves.data() + pending_moves.size(),
                      &source_gpr_uses);
      }
    }

    {
      size_t i = 0;

      auto handle_immediate_argument = [&](auto& arg) {
        using Arg = std::decay_t<decltype(arg)>;

        if constexpr (!std::is_same_v<Arg, ARM64Reg>)
        {
          const ARM64Reg dst_reg =
              (sizeof(arg) == 8 ? EncodeRegTo64 : EncodeRegTo32)(static_cast<ARM64Reg>(i));
          if constexpr (std::is_pointer_v<Arg>)
            MOVP2R(dst_reg, arg);
          else
            MOVI2R(dst_reg, arg);
        }

        ++i;
      };

      (handle_immediate_argument(args), ...);
    }

    QuickCallFunction(ARM64Reg::X8, func);
  }

  // Utility to generate a call to a std::function object.
  //
  // Unfortunately, calling operator() directly is undefined behavior in C++
  // (this method might be a thunk in the case of multi-inheritance) so we
  // have to go through a trampoline function.
  template <typename T, typename... Args>
  static T CallLambdaTrampoline(const std::function<T(Args...)>* f, Args... args)
  {
    return (*f)(args...);
  }

  template <typename FuncRet, typename... FuncArgs, typename... Args>
  void ABI_CallLambdaFunction(const std::function<FuncRet(FuncArgs...)>* f, Args... args)
  {
    auto trampoline = &ARM64XEmitter::CallLambdaTrampoline<FuncRet, FuncArgs...>;
    ABI_CallFunction(trampoline, f, args...);
  }
};

class ARM64CodeBlock : public Common::CodeBlock<ARM64XEmitter>
{
private:
  void PoisonMemory() override
  {
    // If our memory isn't a multiple of u32 then this won't write the last remaining bytes with
    // anything
    // Less than optimal, but there would be nothing we could do but throw a runtime warning anyway.
    // AArch64: 0xD4200000 = BRK 0
    constexpr u32 brk_0 = 0xD4200000;

    for (size_t i = 0; i < region_size; i += sizeof(u32))
    {
      std::memcpy(region + i, &brk_0, sizeof(u32));
    }
  }
};
}  // namespace Arm64Gen

#include "Common/Arm64FloatEmitter.h"
