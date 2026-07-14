// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/x64Emitter.h"

#include <cstring>

#include "Common/CPUDetect.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/x64Reg.h"

namespace Gen
{

void XEmitter::SetCodePtr(u8* ptr, u8* end, bool write_failed)
{
  code = ptr;
  m_code_end = end;
  m_write_failed = write_failed;
}

void XEmitter::Write8(u8 value)
{
  if (code < m_code_end)
  {
    *code++ = value;
  }
  else
  {
    m_write_failed = true;
  }
}

void XEmitter::Write16(u16 value)
{
  if (code + 1 < m_code_end)
  {
    std::memcpy(code, &value, sizeof(u16));
    code += 2;
  }
  else
  {
    code = m_code_end;
    m_write_failed = true;
  }
}

void XEmitter::Write32(u32 value)
{
  if (code + 3 < m_code_end)
  {
    std::memcpy(code, &value, sizeof(u32));
    code += 4;
  }
  else
  {
    code = m_code_end;
    m_write_failed = true;
  }
}

void XEmitter::Write64(u64 value)
{
  if (code + 7 < m_code_end)
  {
    std::memcpy(code, &value, sizeof(u64));
    code += 8;
  }
  else
  {
    code = m_code_end;
    m_write_failed = true;
  }
}

void XEmitter::ReserveCodeSpace(int bytes)
{
  if (code + bytes <= m_code_end)
  {
    code += bytes;
  }
  else
  {
    code = m_code_end;
    m_write_failed = true;
  }
}

u8* XEmitter::AlignCodeTo(size_t alignment)
{
  DEBUG_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0);
  size_t offset = reinterpret_cast<size_t>(code) & (alignment - 1);
  if (offset != 0)
    NOP(alignment - offset);
  return code;
}

u8* XEmitter::AlignCode4()
{
  return AlignCodeTo(4);
}

u8* XEmitter::AlignCode16()
{
  return AlignCodeTo(16);
}

u8* XEmitter::AlignCodePage()
{
  return AlignCodeTo(4096);
}

void XEmitter::WriteModRM(int mod, int reg, int rm)
{
  Write8((u8)((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
}

void XEmitter::WriteSIB(int scale, int index, int base)
{
  Write8((u8)((scale << 6) | ((index & 7) << 3) | (base & 7)));
}

void XEmitter::CheckFlags()
{
  // Do nothing. Left here for compatibility with some JIT configs.
}

void XEmitter::Rex(int w, int r, int x, int b)
{
  w = w ? 1 : 0;
  r = r ? 1 : 0;
  x = x ? 1 : 0;
  b = b ? 1 : 0;
  u8 rx = (u8)(0x40 | (w << 3) | (r << 2) | (x << 1) | (b));
  if (rx != 0x40)
    Write8(rx);
}

void XEmitter::INT3()
{
  Write8(0xCC);
}

void XEmitter::NOP(size_t size)
{
  DEBUG_ASSERT((int)size > 0);
  while (true)
  {
    switch (size)
    {
    case 0:
      return;
    case 1:
      Write8(0x90);
      return;
    case 2:
      Write8(0x66);
      Write8(0x90);
      return;
    case 3:
      Write8(0x0F);
      Write8(0x1F);
      Write8(0x00);
      return;
    case 4:
      Write8(0x0F);
      Write8(0x1F);
      Write8(0x40);
      Write8(0x00);
      return;
    case 5:
      Write8(0x0F);
      Write8(0x1F);
      Write8(0x44);
      Write8(0x00);
      Write8(0x00);
      return;
    case 6:
      Write8(0x66);
      Write8(0x0F);
      Write8(0x1F);
      Write8(0x44);
      Write8(0x00);
      Write8(0x00);
      return;
    case 7:
      Write8(0x0F);
      Write8(0x1F);
      Write8(0x80);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      return;
    case 8:
      Write8(0x0F);
      Write8(0x1F);
      Write8(0x84);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      return;
    case 9:
      Write8(0x66);
      Write8(0x0F);
      Write8(0x1F);
      Write8(0x84);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      return;
    case 10:
      Write8(0x66);
      Write8(0x66);
      Write8(0x0F);
      Write8(0x1F);
      Write8(0x84);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      return;
    default:
      // Even though x86 instructions are allowed to be up to 15 bytes long,
      // AMD advises against using NOPs longer than 11 bytes because they
      // carry a performance penalty on CPUs older than AMD family 16h.
      Write8(0x66);
      Write8(0x66);
      Write8(0x66);
      Write8(0x0F);
      Write8(0x1F);
      Write8(0x84);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      Write8(0x00);
      size -= 11;
      continue;
    }
  }
}

void XEmitter::PAUSE()
{
  Write8(0xF3);
  NOP();
}  // use in tight spinloops for energy saving on some CPU

void XEmitter::CLC()
{
  CheckFlags();
  Write8(0xF8);
}  // clear carry

void XEmitter::CMC()
{
  CheckFlags();
  Write8(0xF5);
}  // flip carry

void XEmitter::STC()
{
  CheckFlags();
  Write8(0xF9);
}  // set carry

void XEmitter::LAHF()
{
  Write8(0x9F);
}

void XEmitter::SAHF()
{
  CheckFlags();
  Write8(0x9E);
}

void XEmitter::PUSHF()
{
  Write8(0x9C);
}

void XEmitter::POPF()
{
  CheckFlags();
  Write8(0x9D);
}

void XEmitter::WriteSimple1Byte(int bits, u8 byte, X64Reg reg)
{
  if (bits == 16)
    Write8(0x66);
  Rex(bits == 64, 0, 0, (int)reg >> 3);
  Write8(byte + ((int)reg & 7));
}

void XEmitter::WriteSimple2Byte(int bits, u8 byte1, u8 byte2, X64Reg reg)
{
  if (bits == 16)
    Write8(0x66);
  Rex(bits == 64, 0, 0, (int)reg >> 3);
  Write8(byte1);
  Write8(byte2 + ((int)reg & 7));
}

void XEmitter::PUSH(X64Reg reg)
{
  WriteSimple1Byte(32, 0x50, reg);
}

void XEmitter::POP(X64Reg reg)
{
  WriteSimple1Byte(32, 0x58, reg);
}

void XEmitter::PUSH(int bits, const OpArg& reg)
{
  if (reg.IsSimpleReg())
    PUSH(reg.GetSimpleReg());
  else if (reg.IsImm())
  {
    switch (reg.GetImmBits())
    {
    case 8:
      Write8(0x6A);
      Write8((u8)(s8)reg.offset);
      break;
    case 16:
      Write8(0x66);
      Write8(0x68);
      Write16((u16)(s16)(s32)reg.offset);
      break;
    case 32:
      Write8(0x68);
      Write32((u32)reg.offset);
      break;
    default:
      ASSERT_MSG(DYNA_REC, 0, "PUSH - Bad imm bits");
      break;
    }
  }
  else
  {
    if (bits == 16)
      Write8(0x66);
    reg.WriteREX(this, bits, bits);
    Write8(0xFF);
    reg.WriteRest(this, 0, (X64Reg)6);
  }
}

void XEmitter::POP(int /*bits*/, const OpArg& reg)
{
  if (reg.IsSimpleReg())
    POP(reg.GetSimpleReg());
  else
    ASSERT_MSG(DYNA_REC, 0, "POP - Unsupported encoding");
}

}  // namespace Gen
