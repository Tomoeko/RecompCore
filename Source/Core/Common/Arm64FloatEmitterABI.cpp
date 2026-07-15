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
void ARM64FloatEmitter::ABI_PushRegisters(BitSet32 registers, ARM64Reg tmp)
{
  bool bundled_loadstore = false;

  for (int i = 0; i < 32; ++i)
  {
    if (!registers[i])
      continue;

    int count = 0;
    while (++count < 4 && (i + count) < 32 && registers[i + count])
    {
    }
    if (count > 1)
    {
      bundled_loadstore = true;
      break;
    }
  }

  if (bundled_loadstore && tmp != ARM64Reg::INVALID_REG)
  {
    DEBUG_ASSERT_MSG(DYNA_REC, Is64Bit(tmp), "Expected a 64-bit temporary register!");

    int num_regs = registers.Count();
    m_emit->SUB(ARM64Reg::SP, ARM64Reg::SP, num_regs * 16);
    m_emit->ADD(tmp, ARM64Reg::SP, 0);
    std::vector<ARM64Reg> island_regs;
    for (int i = 0; i < 32; ++i)
    {
      if (!registers[i])
        continue;

      int count = 0;

      // 0 = true
      // 1 < 4 && registers[i + 1] true!
      // 2 < 4 && registers[i + 2] true!
      // 3 < 4 && registers[i + 3] true!
      // 4 < 4 && registers[i + 4] false!
      while (++count < 4 && (i + count) < 32 && registers[i + count])
      {
      }

      if (count == 1)
        island_regs.push_back(ARM64Reg::Q0 + i);
      else
        ST1(64, count, IndexType::Post, ARM64Reg::Q0 + i, tmp);

      i += count - 1;
    }

    // Handle island registers
    std::vector<ARM64Reg> pair_regs;
    for (auto& it : island_regs)
    {
      pair_regs.push_back(it);
      if (pair_regs.size() == 2)
      {
        STP(128, IndexType::Post, pair_regs[0], pair_regs[1], tmp, 32);
        pair_regs.clear();
      }
    }
    if (pair_regs.size())
      STR(128, IndexType::Post, pair_regs[0], tmp, 16);
  }
  else
  {
    std::vector<ARM64Reg> pair_regs;
    for (auto it : registers)
    {
      pair_regs.push_back(ARM64Reg::Q0 + it);
      if (pair_regs.size() == 2)
      {
        STP(128, IndexType::Pre, pair_regs[0], pair_regs[1], ARM64Reg::SP, -32);
        pair_regs.clear();
      }
    }
    if (pair_regs.size())
      STR(128, IndexType::Pre, pair_regs[0], ARM64Reg::SP, -16);
  }
}

void ARM64FloatEmitter::ABI_PopRegisters(BitSet32 registers, ARM64Reg tmp)
{
  bool bundled_loadstore = false;
  int num_regs = registers.Count();

  for (int i = 0; i < 32; ++i)
  {
    if (!registers[i])
      continue;

    int count = 0;
    while (++count < 4 && (i + count) < 32 && registers[i + count])
    {
    }
    if (count > 1)
    {
      bundled_loadstore = true;
      break;
    }
  }

  if (bundled_loadstore && tmp != ARM64Reg::INVALID_REG)
  {
    // The temporary register is only used to indicate that we can use this code path
    std::vector<ARM64Reg> island_regs;
    for (int i = 0; i < 32; ++i)
    {
      if (!registers[i])
        continue;

      int count = 0;
      while (++count < 4 && (i + count) < 32 && registers[i + count])
      {
      }

      if (count == 1)
        island_regs.push_back(ARM64Reg::Q0 + i);
      else
        LD1(64, count, IndexType::Post, ARM64Reg::Q0 + i, ARM64Reg::SP);

      i += count - 1;
    }

    // Handle island registers
    std::vector<ARM64Reg> pair_regs;
    for (auto& it : island_regs)
    {
      pair_regs.push_back(it);
      if (pair_regs.size() == 2)
      {
        LDP(128, IndexType::Post, pair_regs[0], pair_regs[1], ARM64Reg::SP, 32);
        pair_regs.clear();
      }
    }
    if (pair_regs.size())
      LDR(128, IndexType::Post, pair_regs[0], ARM64Reg::SP, 16);
  }
  else
  {
    bool odd = num_regs % 2;
    std::vector<ARM64Reg> pair_regs;
    for (int i = 31; i >= 0; --i)
    {
      if (!registers[i])
        continue;

      if (odd)
      {
        // First load must be a regular LDR if odd
        odd = false;
        LDR(128, IndexType::Post, ARM64Reg::Q0 + i, ARM64Reg::SP, 16);
      }
      else
      {
        pair_regs.push_back(ARM64Reg::Q0 + i);
        if (pair_regs.size() == 2)
        {
          LDP(128, IndexType::Post, pair_regs[1], pair_regs[0], ARM64Reg::SP, 32);
          pair_regs.clear();
        }
      }
    }
  }
}
}  // namespace Arm64Gen
