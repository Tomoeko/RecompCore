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

static const u32 ExcEnc[][3] = {
    {0, 0, 1},  // SVC
    {0, 0, 2},  // HVC
    {0, 0, 3},  // SMC
    {1, 0, 0},  // BRK
    {2, 0, 0},  // HLT
    {5, 0, 1},  // DCPS1
    {5, 0, 2},  // DCPS2
    {5, 0, 3},  // DCPS3
};

static void GetSystemReg(PStateField field, int& o0, int& op1, int& CRn, int& CRm, int& op2)
{
  switch (field)
  {
  case PStateField::NZCV:
    o0 = 3;
    op1 = 3;
    CRn = 4;
    CRm = 2;
    op2 = 0;
    break;
  case PStateField::FPCR:
    o0 = 3;
    op1 = 3;
    CRn = 4;
    CRm = 4;
    op2 = 0;
    break;
  case PStateField::FPSR:
    o0 = 3;
    op1 = 3;
    CRn = 4;
    CRm = 4;
    op2 = 1;
    break;
  case PStateField::PMCR_EL0:
    o0 = 3;
    op1 = 3;
    CRn = 9;
    CRm = 6;
    op2 = 0;
    break;
  case PStateField::PMCCNTR_EL0:
    o0 = 3;
    op1 = 3;
    CRn = 9;
    CRm = 7;
    op2 = 0;
    break;
  default:
    ASSERT_MSG(DYNA_REC, false, "Invalid PStateField to do a register move from/to");
    break;
  }
}

void ARM64XEmitter::SetCodePtrUnsafe(u8* ptr, u8* end, bool write_failed)
{
  m_code = ptr;
  m_code_end = end;
  m_write_failed = write_failed;
}

void ARM64XEmitter::SetCodePtr(u8* ptr, u8* end, bool write_failed)
{
  SetCodePtrUnsafe(ptr, end, write_failed);
  m_lastCacheFlushEnd = ptr;
}

void ARM64XEmitter::ReserveCodeSpace(u32 bytes)
{
  for (u32 i = 0; i < bytes / 4; i++)
    BRK(0);
}

u8* ARM64XEmitter::AlignCode16()
{
  int c = int((u64)m_code & 15);
  if (c)
    ReserveCodeSpace(16 - c);
  return m_code;
}

u8* ARM64XEmitter::AlignCodePage()
{
  int c = int((u64)m_code & 4095);
  if (c)
    ReserveCodeSpace(4096 - c);
  return m_code;
}

void ARM64XEmitter::Write32(u32 value)
{
  if (m_code + sizeof(u32) > m_code_end)
  {
    m_code = m_code_end;
    m_write_failed = true;
    return;
  }

  std::memcpy(m_code, &value, sizeof(u32));
  m_code += sizeof(u32);
}

void ARM64XEmitter::FlushIcache()
{
  FlushIcacheSection(m_lastCacheFlushEnd, m_code);
  m_lastCacheFlushEnd = m_code;
}

void ARM64XEmitter::FlushIcacheSection(u8* start, u8* end)
{
  if (start == end)
    return;

#if defined(IOS) || defined(__APPLE__)
  // Header file says this is equivalent to: sys_icache_invalidate(start, end - start);
  sys_cache_control(kCacheFunctionPrepareForExecution, start, end - start);
#elif defined(WIN32)
  FlushInstructionCache(GetCurrentProcess(), start, end - start);
#else
  // Don't rely on GCC's __clear_cache implementation, as it caches
  // icache/dcache cache line sizes, that can vary between cores on
  // big.LITTLE architectures.
  u64 addr, ctr_el0;
  static size_t icache_line_size = 0xffff, dcache_line_size = 0xffff;
  size_t isize, dsize;

  __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr_el0));
  isize = 4 << ((ctr_el0 >> 0) & 0xf);
  dsize = 4 << ((ctr_el0 >> 16) & 0xf);

  // use the global minimum cache line size
  icache_line_size = isize = icache_line_size < isize ? icache_line_size : isize;
  dcache_line_size = dsize = dcache_line_size < dsize ? dcache_line_size : dsize;

  addr = (u64)start & ~(u64)(dsize - 1);
  for (; addr < (u64)end; addr += dsize)
    // use "civac" instead of "cvau", as this is the suggested workaround for
    // Cortex-A53 errata 819472, 826319, 827319 and 824069.
    __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
  __asm__ volatile("dsb ish" : : : "memory");

  addr = (u64)start & ~(u64)(isize - 1);
  for (; addr < (u64)end; addr += isize)
    __asm__ volatile("ic ivau, %0" : : "r"(addr) : "memory");

  __asm__ volatile("dsb ish" : : : "memory");
  __asm__ volatile("isb" : : : "memory");
#endif
}

void ARM64XEmitter::EncodeCompareBranchInst(u32 op, ARM64Reg Rt, const void* ptr)
{
  bool b64Bit = Is64Bit(Rt);
  s64 distance = (s64)ptr - (s64)m_code;

  ASSERT_MSG(DYNA_REC, !(distance & 0x3), "Distance must be a multiple of 4: {}", distance);

  distance >>= 2;

  ASSERT_MSG(DYNA_REC, distance >= -0x40000 && distance <= 0x3FFFF,
             "Received too large distance: {}", distance);

  Write32((b64Bit << 31) | (0x34 << 24) | (op << 24) | (((u32)distance << 5) & 0xFFFFE0) |
          DecodeReg(Rt));
}

void ARM64XEmitter::EncodeTestBranchInst(u32 op, ARM64Reg Rt, u8 bits, const void* ptr)
{
  u8 b40 = bits & 0x1F;
  u8 b5 = (bits >> 5) & 0x1;
  s64 distance = (s64)ptr - (s64)m_code;

  ASSERT_MSG(DYNA_REC, !(distance & 0x3), "distance must be a multiple of 4: {}", distance);

  distance >>= 2;

  ASSERT_MSG(DYNA_REC, distance >= -0x3FFF && distance < 0x3FFF, "Received too large distance: {}",
             distance);

  Write32((b5 << 31) | (0x36 << 24) | (op << 24) | (b40 << 19) |
          ((static_cast<u32>(distance) << 5) & 0x7FFE0) | DecodeReg(Rt));
}

void ARM64XEmitter::EncodeUnconditionalBranchInst(u32 op, const void* ptr)
{
  s64 distance = (s64)ptr - s64(m_code);

  ASSERT_MSG(DYNA_REC, !(distance & 0x3), "distance must be a multiple of 4: {}", distance);

  distance >>= 2;

  ASSERT_MSG(DYNA_REC, distance >= -0x2000000LL && distance <= 0x1FFFFFFLL,
             "Received too large distance: {}", distance);

  Write32((op << 31) | (0x5 << 26) | (distance & 0x3FFFFFF));
}

void ARM64XEmitter::EncodeUnconditionalBranchInst(u32 opc, u32 op2, u32 op3, u32 op4, ARM64Reg Rn)
{
  Write32((0x6B << 25) | (opc << 21) | (op2 << 16) | (op3 << 10) | (DecodeReg(Rn) << 5) | op4);
}

void ARM64XEmitter::EncodeExceptionInst(u32 instenc, u32 imm)
{
  ASSERT_MSG(DYNA_REC, !(imm & ~0xFFFF), "Exception instruction too large immediate: {}", imm);

  Write32((0xD4 << 24) | (ExcEnc[instenc][0] << 21) | (imm << 5) | (ExcEnc[instenc][1] << 2) |
          ExcEnc[instenc][2]);
}

void ARM64XEmitter::EncodeSystemInst(u32 op0, u32 op1, u32 CRn, u32 CRm, u32 op2, ARM64Reg Rt)
{
  Write32((0x354 << 22) | (op0 << 19) | (op1 << 16) | (CRn << 12) | (CRm << 8) | (op2 << 5) |
          DecodeReg(Rt));
}

void ARM64XEmitter::SetJumpTarget(FixupBranch const& branch)
{
  if (!branch.ptr)
    return;

  bool Not = false;
  u32 inst = 0;
  s64 distance = (s64)(m_code - branch.ptr);
  distance >>= 2;

  switch (branch.type)
  {
  case FixupBranch::Type::CBNZ:
    Not = true;
    [[fallthrough]];
  case FixupBranch::Type::CBZ:
  {
    ASSERT_MSG(DYNA_REC, IsInRangeImm19(distance),
               "Branch type {}: Received too large distance: {}", static_cast<int>(branch.type),
               distance);
    const bool b64Bit = Is64Bit(branch.reg);
    inst = (b64Bit << 31) | (0x1A << 25) | (Not << 24) | (MaskImm19(distance) << 5) |
           DecodeReg(branch.reg);
  }
  break;
  case FixupBranch::Type::BConditional:
    ASSERT_MSG(DYNA_REC, IsInRangeImm19(distance),
               "Branch type {}: Received too large distance: {}", static_cast<int>(branch.type),
               distance);
    inst = (0x2A << 25) | (MaskImm19(distance) << 5) | branch.cond;
    break;
  case FixupBranch::Type::TBNZ:
    Not = true;
    [[fallthrough]];
  case FixupBranch::Type::TBZ:
  {
    ASSERT_MSG(DYNA_REC, IsInRangeImm14(distance),
               "Branch type {}: Received too large distance: {}", static_cast<int>(branch.type),
               distance);
    inst = ((branch.bit & 0x20) << 26) | (0x1B << 25) | (Not << 24) | ((branch.bit & 0x1F) << 19) |
           (MaskImm14(distance) << 5) | DecodeReg(branch.reg);
  }
  break;
  case FixupBranch::Type::B:
    ASSERT_MSG(DYNA_REC, IsInRangeImm26(distance),
               "Branch type {}: Received too large distance: {}", static_cast<int>(branch.type),
               distance);
    inst = (0x5 << 26) | MaskImm26(distance);
    break;
  case FixupBranch::Type::BL:
    ASSERT_MSG(DYNA_REC, IsInRangeImm26(distance),
               "Branch type {}: Received too large distance: {}", static_cast<int>(branch.type),
               distance);
    inst = (0x25 << 26) | MaskImm26(distance);
    break;
  }

  std::memcpy(branch.ptr, &inst, sizeof(inst));
}

FixupBranch ARM64XEmitter::WriteFixupBranch()
{
  FixupBranch branch{};
  branch.ptr = m_code;
  BRK(0);

  // If we couldn't write the full jump instruction, indicate that in the returned FixupBranch by
  // setting the branch's address to null. This will prevent a later SetJumpTarget() from writing to
  // invalid memory.
  if (HasWriteFailed())
    branch.ptr = nullptr;

  return branch;
}

FixupBranch ARM64XEmitter::CBZ(ARM64Reg Rt)
{
  FixupBranch branch = WriteFixupBranch();
  branch.type = FixupBranch::Type::CBZ;
  branch.reg = Rt;
  return branch;
}

FixupBranch ARM64XEmitter::CBNZ(ARM64Reg Rt)
{
  FixupBranch branch = WriteFixupBranch();
  branch.type = FixupBranch::Type::CBNZ;
  branch.reg = Rt;
  return branch;
}

FixupBranch ARM64XEmitter::B(CCFlags cond)
{
  FixupBranch branch = WriteFixupBranch();
  branch.type = FixupBranch::Type::BConditional;
  branch.cond = cond;
  return branch;
}

FixupBranch ARM64XEmitter::TBZ(ARM64Reg Rt, u8 bit)
{
  FixupBranch branch = WriteFixupBranch();
  branch.type = FixupBranch::Type::TBZ;
  branch.reg = Rt;
  branch.bit = bit;
  return branch;
}

FixupBranch ARM64XEmitter::TBNZ(ARM64Reg Rt, u8 bit)
{
  FixupBranch branch = WriteFixupBranch();
  branch.type = FixupBranch::Type::TBNZ;
  branch.reg = Rt;
  branch.bit = bit;
  return branch;
}

FixupBranch ARM64XEmitter::B()
{
  FixupBranch branch = WriteFixupBranch();
  branch.type = FixupBranch::Type::B;
  return branch;
}

FixupBranch ARM64XEmitter::BL()
{
  FixupBranch branch = WriteFixupBranch();
  branch.type = FixupBranch::Type::BL;
  return branch;
}

void ARM64XEmitter::CBZ(ARM64Reg Rt, const void* ptr)
{
  EncodeCompareBranchInst(0, Rt, ptr);
}

void ARM64XEmitter::CBNZ(ARM64Reg Rt, const void* ptr)
{
  EncodeCompareBranchInst(1, Rt, ptr);
}

void ARM64XEmitter::B(CCFlags cond, const void* ptr)
{
  s64 distance = (s64)ptr - (s64)m_code;

  distance >>= 2;

  ASSERT_MSG(DYNA_REC, IsInRangeImm19(distance),
             "Received too large distance: {}->{} (dist {} {:#x})", fmt::ptr(m_code), fmt::ptr(ptr),
             distance, distance);
  Write32((0x54 << 24) | (MaskImm19(distance) << 5) | cond);
}

void ARM64XEmitter::TBZ(ARM64Reg Rt, u8 bits, const void* ptr)
{
  EncodeTestBranchInst(0, Rt, bits, ptr);
}

void ARM64XEmitter::TBNZ(ARM64Reg Rt, u8 bits, const void* ptr)
{
  EncodeTestBranchInst(1, Rt, bits, ptr);
}

void ARM64XEmitter::B(const void* ptr)
{
  EncodeUnconditionalBranchInst(0, ptr);
}

void ARM64XEmitter::BL(const void* ptr)
{
  EncodeUnconditionalBranchInst(1, ptr);
}

void ARM64XEmitter::BR(ARM64Reg Rn)
{
  EncodeUnconditionalBranchInst(0, 0x1F, 0, 0, Rn);
}

void ARM64XEmitter::BLR(ARM64Reg Rn)
{
  EncodeUnconditionalBranchInst(1, 0x1F, 0, 0, Rn);
}

void ARM64XEmitter::RET(ARM64Reg Rn)
{
  EncodeUnconditionalBranchInst(2, 0x1F, 0, 0, Rn);
}

void ARM64XEmitter::ERET()
{
  EncodeUnconditionalBranchInst(4, 0x1F, 0, 0, ARM64Reg::SP);
}

void ARM64XEmitter::DRPS()
{
  EncodeUnconditionalBranchInst(5, 0x1F, 0, 0, ARM64Reg::SP);
}

void ARM64XEmitter::SVC(u32 imm)
{
  EncodeExceptionInst(0, imm);
}

void ARM64XEmitter::HVC(u32 imm)
{
  EncodeExceptionInst(1, imm);
}

void ARM64XEmitter::SMC(u32 imm)
{
  EncodeExceptionInst(2, imm);
}

void ARM64XEmitter::BRK(u32 imm)
{
  EncodeExceptionInst(3, imm);
}

void ARM64XEmitter::HLT(u32 imm)
{
  EncodeExceptionInst(4, imm);
}

void ARM64XEmitter::DCPS1(u32 imm)
{
  EncodeExceptionInst(5, imm);
}

void ARM64XEmitter::DCPS2(u32 imm)
{
  EncodeExceptionInst(6, imm);
}

void ARM64XEmitter::DCPS3(u32 imm)
{
  EncodeExceptionInst(7, imm);
}

void ARM64XEmitter::_MSR(PStateField field, u8 imm)
{
  u32 op1 = 0, op2 = 0;
  switch (field)
  {
  case PStateField::SPSel:
    op1 = 0;
    op2 = 5;
    break;
  case PStateField::DAIFSet:
    op1 = 3;
    op2 = 6;
    break;
  case PStateField::DAIFClr:
    op1 = 3;
    op2 = 7;
    break;
  default:
    ASSERT_MSG(DYNA_REC, false, "Invalid PStateField to do a imm move to");
    break;
  }
  EncodeSystemInst(0, op1, 4, imm, op2, ARM64Reg::WSP);
}

void ARM64XEmitter::_MSR(PStateField field, ARM64Reg Rt)
{
  int o0 = 0, op1 = 0, CRn = 0, CRm = 0, op2 = 0;
  ASSERT_MSG(DYNA_REC, Is64Bit(Rt), "MSR: Rt must be 64-bit");
  GetSystemReg(field, o0, op1, CRn, CRm, op2);
  EncodeSystemInst(o0, op1, CRn, CRm, op2, Rt);
}

void ARM64XEmitter::MRS(ARM64Reg Rt, PStateField field)
{
  int o0 = 0, op1 = 0, CRn = 0, CRm = 0, op2 = 0;
  ASSERT_MSG(DYNA_REC, Is64Bit(Rt), "MRS: Rt must be 64-bit");
  GetSystemReg(field, o0, op1, CRn, CRm, op2);
  EncodeSystemInst(o0 | 4, op1, CRn, CRm, op2, Rt);
}

void ARM64XEmitter::CNTVCT(Arm64Gen::ARM64Reg Rt)
{
  ASSERT_MSG(DYNA_REC, Is64Bit(Rt), "CNTVCT: Rt must be 64-bit");

  // MRS <Xt>, CNTVCT_EL0 ; Read CNTVCT_EL0 into Xt
  EncodeSystemInst(3 | 4, 3, 0xe, 0, 2, Rt);
}

void ARM64XEmitter::HINT(SystemHint op)
{
  EncodeSystemInst(0, 3, 2, 0, static_cast<u32>(op), ARM64Reg::WSP);
}

void ARM64XEmitter::CLREX()
{
  EncodeSystemInst(0, 3, 3, 0, 2, ARM64Reg::WSP);
}

void ARM64XEmitter::DSB(BarrierType type)
{
  EncodeSystemInst(0, 3, 3, static_cast<u32>(type), 4, ARM64Reg::WSP);
}

void ARM64XEmitter::DMB(BarrierType type)
{
  EncodeSystemInst(0, 3, 3, static_cast<u32>(type), 5, ARM64Reg::WSP);
}

void ARM64XEmitter::ISB(BarrierType type)
{
  EncodeSystemInst(0, 3, 3, static_cast<u32>(type), 6, ARM64Reg::WSP);
}

}  // namespace Arm64Gen
