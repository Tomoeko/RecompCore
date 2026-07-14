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

void ARM64XEmitter::QuickCallFunction(ARM64Reg scratchreg, const void* func)
{
  s64 distance = (s64)func - (s64)m_code;
  distance >>= 2;  // Can only branch to opcode-aligned (4) addresses
  if (!IsInRangeImm26(distance))
  {
    MOVI2R(scratchreg, (uintptr_t)func);
    BLR(scratchreg);
  }
  else
  {
    BL(func);
  }
}

void ARM64XEmitter::ParallelMoves(RegisterMove* begin, RegisterMove* end,
                                  std::array<u8, 32>* source_gpr_usages)
{
  // X0-X7 are used for passing arguments.
  // X18-X31 are either callee saved or used for special purposes.
  constexpr size_t temp_reg_begin = 8;
  constexpr size_t temp_reg_end = 18;

  while (begin != end)
  {
    bool removed_moves_during_this_loop_iteration = false;

    RegisterMove* current_move = end;
    while (current_move != begin)
    {
      RegisterMove* prev_move = current_move;
      --current_move;
      if ((*source_gpr_usages)[DecodeReg(current_move->dst)] == 0)
      {
        MOV(current_move->dst, current_move->src);
        (*source_gpr_usages)[DecodeReg(current_move->src)]--;
        std::move(prev_move, end, current_move);
        --end;
        removed_moves_during_this_loop_iteration = true;
      }
    }

    if (!removed_moves_during_this_loop_iteration)
    {
      // We need to break a cycle using a temporary register.

      size_t temp_reg = temp_reg_begin;
      while ((*source_gpr_usages)[temp_reg] != 0)
      {
        ++temp_reg;
        ASSERT_MSG(DYNA_REC, temp_reg != temp_reg_end, "Out of registers");
      }

      const ARM64Reg src = begin->src;
      const ARM64Reg dst =
          (Is64Bit(src) ? EncodeRegTo64 : EncodeRegTo32)(static_cast<ARM64Reg>(temp_reg));

      MOV(dst, src);
      (*source_gpr_usages)[DecodeReg(dst)] = (*source_gpr_usages)[DecodeReg(src)];
      (*source_gpr_usages)[DecodeReg(src)] = 0;

      std::for_each(begin, end, [src, dst](RegisterMove& move) {
        if (move.src == src)
          move.src = dst;
      });
    }
  }
}

void ARM64XEmitter::ABI_PushRegisters(BitSet32 registers)
{
  int num_regs = registers.Count();
  int stack_size = (num_regs + (num_regs & 1)) * 8;
  auto it = registers.begin();

  if (!num_regs)
    return;

  // 8 byte per register, but 16 byte alignment, so we may have to pad one register.
  // Only update the SP on the last write to avoid the dependency between those stores.

  // The first push must adjust the SP, else a context switch may invalidate everything below SP.
  if (num_regs & 1)
  {
    STR(IndexType::Pre, ARM64Reg::X0 + *it++, ARM64Reg::SP, -stack_size);
  }
  else
  {
    ARM64Reg first_reg = ARM64Reg::X0 + *it++;
    ARM64Reg second_reg = ARM64Reg::X0 + *it++;
    STP(IndexType::Pre, first_reg, second_reg, ARM64Reg::SP, -stack_size);
  }

  // Fast store for all other registers, this is always an even number.
  for (int i = 0; i < (num_regs - 1) / 2; i++)
  {
    ARM64Reg odd_reg = ARM64Reg::X0 + *it++;
    ARM64Reg even_reg = ARM64Reg::X0 + *it++;
    STP(IndexType::Signed, odd_reg, even_reg, ARM64Reg::SP, 16 * (i + 1));
  }

  ASSERT_MSG(DYNA_REC, it == registers.end(), "Registers don't match: {:b}", registers.m_val);
}

void ARM64XEmitter::ABI_PopRegisters(BitSet32 registers, BitSet32 ignore_mask)
{
  int num_regs = registers.Count();
  int stack_size = (num_regs + (num_regs & 1)) * 8;
  auto it = registers.begin();

  if (!num_regs)
    return;

  // We must adjust the SP in the end, so load the first (two) registers at least.
  ARM64Reg first = ARM64Reg::X0 + *it++;
  ARM64Reg second;
  if (!(num_regs & 1))
    second = ARM64Reg::X0 + *it++;
  else
    second = {};

  // 8 byte per register, but 16 byte alignment, so we may have to pad one register.
  // Only update the SP on the last load to avoid the dependency between those loads.

  // Fast load for all but the first (two) registers, this is always an even number.
  for (int i = 0; i < (num_regs - 1) / 2; i++)
  {
    ARM64Reg odd_reg = ARM64Reg::X0 + *it++;
    ARM64Reg even_reg = ARM64Reg::X0 + *it++;
    LDP(IndexType::Signed, odd_reg, even_reg, ARM64Reg::SP, 16 * (i + 1));
  }

  // Post loading the first (two) registers.
  if (num_regs & 1)
    LDR(IndexType::Post, first, ARM64Reg::SP, stack_size);
  else
    LDP(IndexType::Post, first, second, ARM64Reg::SP, stack_size);

  ASSERT_MSG(DYNA_REC, it == registers.end(), "Registers don't match: {:b}", registers.m_val);
}

}  // namespace Arm64Gen
