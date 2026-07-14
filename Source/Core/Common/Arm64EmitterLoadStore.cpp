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

static const u32 LoadStoreExcEnc[][5] = {
    {0, 0, 0, 0, 0},  // STXRB
    {0, 0, 0, 0, 1},  // STLXRB
    {0, 0, 1, 0, 0},  // LDXRB
    {0, 0, 1, 0, 1},  // LDAXRB
    {0, 1, 0, 0, 1},  // STLRB
    {0, 1, 1, 0, 1},  // LDARB
    {1, 0, 0, 0, 0},  // STXRH
    {1, 0, 0, 0, 1},  // STLXRH
    {1, 0, 1, 0, 0},  // LDXRH
    {1, 0, 1, 0, 1},  // LDAXRH
    {1, 1, 0, 0, 1},  // STLRH
    {1, 1, 1, 0, 1},  // LDARH
    {2, 0, 0, 0, 0},  // STXR
    {3, 0, 0, 0, 0},  // (64bit) STXR
    {2, 0, 0, 0, 1},  // STLXR
    {3, 0, 0, 0, 1},  // (64bit) STLXR
    {2, 0, 0, 1, 0},  // STXP
    {3, 0, 0, 1, 0},  // (64bit) STXP
    {2, 0, 0, 1, 1},  // STLXP
    {3, 0, 0, 1, 1},  // (64bit) STLXP
    {2, 0, 1, 0, 0},  // LDXR
    {3, 0, 1, 0, 0},  // (64bit) LDXR
    {2, 0, 1, 0, 1},  // LDAXR
    {3, 0, 1, 0, 1},  // (64bit) LDAXR
    {2, 0, 1, 1, 0},  // LDXP
    {3, 0, 1, 1, 0},  // (64bit) LDXP
    {2, 0, 1, 1, 1},  // LDAXP
    {3, 0, 1, 1, 1},  // (64bit) LDAXP
    {2, 1, 0, 0, 1},  // STLR
    {3, 1, 0, 0, 1},  // (64bit) STLR
    {2, 1, 1, 0, 1},  // LDAR
    {3, 1, 1, 0, 1},  // (64bit) LDAR
};

void ARM64XEmitter::EncodeLoadRegisterInst(u32 bitop, ARM64Reg Rt, u32 imm)
{
  bool b64Bit = Is64Bit(Rt);
  bool bVec = IsVector(Rt);

  ASSERT_MSG(DYNA_REC, !(imm & 0xFFFFF), "offset too large {}", imm);

  if (b64Bit && bitop != 0x2)  // LDRSW(0x2) uses 64bit reg, doesn't have 64bit bit set
    bitop |= 0x1;
  Write32((bitop << 30) | (bVec << 26) | (0x18 << 24) | (imm << 5) | DecodeReg(Rt));
}

void ARM64XEmitter::EncodeLoadStoreExcInst(u32 instenc, ARM64Reg Rs, ARM64Reg Rt2, ARM64Reg Rn,
                                           ARM64Reg Rt)
{
  Write32((LoadStoreExcEnc[instenc][0] << 30) | (0x8 << 24) | (LoadStoreExcEnc[instenc][1] << 23) |
          (LoadStoreExcEnc[instenc][2] << 22) | (LoadStoreExcEnc[instenc][3] << 21) |
          (DecodeReg(Rs) << 16) | (LoadStoreExcEnc[instenc][4] << 15) | (DecodeReg(Rt2) << 10) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rt));
}

void ARM64XEmitter::EncodeLoadStorePairedInst(u32 op, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn,
                                              u32 imm)
{
  bool b64Bit = Is64Bit(Rt);
  bool b128Bit = IsQuad(Rt);
  bool bVec = IsVector(Rt);

  if (b128Bit)
  {
    ASSERT_MSG(DYNA_REC, (imm & 0xf) == 0, "128-bit load/store must use aligned offset: {}", imm);
    imm >>= 4;
  }
  else if (b64Bit)
  {
    ASSERT_MSG(DYNA_REC, (imm & 0x7) == 0, "64-bit load/store must use aligned offset: {}", imm);
    imm >>= 3;
  }
  else
  {
    ASSERT_MSG(DYNA_REC, (imm & 0x3) == 0, "32-bit load/store must use aligned offset: {}", imm);
    imm >>= 2;
  }

  ASSERT_MSG(DYNA_REC, (imm & ~0xF) == 0, "offset too large {}", imm);

  u32 opc = 0;
  if (b128Bit)
    opc = 2;
  else if (b64Bit && bVec)
    opc = 1;
  else if (b64Bit && !bVec)
    opc = 2;

  Write32((opc << 30) | (bVec << 26) | (op << 22) | (imm << 15) | (DecodeReg(Rt2) << 10) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rt));
}

void ARM64XEmitter::EncodeLoadStoreIndexedInst(u32 op, u32 op2, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  bool b64Bit = Is64Bit(Rt);
  bool bVec = IsVector(Rt);

  u32 offset = imm & 0x1FF;

  ASSERT_MSG(DYNA_REC, !(imm < -256 || imm > 255), "offset too large {}", imm);

  Write32((b64Bit << 30) | (op << 22) | (bVec << 26) | (offset << 12) | (op2 << 10) |
          (DecodeReg(Rn) << 5) | DecodeReg(Rt));
}

void ARM64XEmitter::EncodeLoadStoreIndexedInst(u32 op, ARM64Reg Rt, ARM64Reg Rn, s32 imm, u8 size)
{
  bool b64Bit = Is64Bit(Rt);
  bool bVec = IsVector(Rt);

  if (size == 64)
  {
    ASSERT_MSG(DYNA_REC, (imm & 0x7) == 0, "64-bit load/store must use aligned offset: {}", imm);
    imm >>= 3;
  }
  else if (size == 32)
  {
    ASSERT_MSG(DYNA_REC, (imm & 0x3) == 0, "32-bit load/store must use aligned offset: {}", imm);
    imm >>= 2;
  }
  else if (size == 16)
  {
    ASSERT_MSG(DYNA_REC, (imm & 0x1) == 0, "16-bit load/store must use aligned offset: {}", imm);
    imm >>= 1;
  }

  ASSERT_MSG(DYNA_REC, imm >= 0, "(IndexType::Unsigned): offset must be positive {}", imm);
  ASSERT_MSG(DYNA_REC, !(imm & ~0xFFF), "(IndexType::Unsigned): offset too large {}", imm);

  Write32((b64Bit << 30) | (op << 22) | (bVec << 26) | (imm << 10) | (DecodeReg(Rn) << 5) |
          DecodeReg(Rt));
}

void ARM64XEmitter::EncodeLoadStoreRegisterOffset(u32 size, u32 opc, ARM64Reg Rt, ARM64Reg Rn,
                                                  ArithOption Rm)
{
  const int decoded_Rm = DecodeReg(Rm.GetReg());

  Write32((size << 30) | (opc << 22) | (0x1C1 << 21) | (decoded_Rm << 16) | Rm.GetData() |
          (1 << 11) | (DecodeReg(Rn) << 5) | DecodeReg(Rt));
}

void ARM64XEmitter::EncodeLoadStorePair(u32 op, u32 load, IndexType type, ARM64Reg Rt, ARM64Reg Rt2,
                                        ARM64Reg Rn, s32 imm)
{
  bool b64Bit = Is64Bit(Rt);
  u32 type_encode = 0;

  switch (type)
  {
  case IndexType::Signed:
    type_encode = 0b010;
    break;
  case IndexType::Post:
    type_encode = 0b001;
    break;
  case IndexType::Pre:
    type_encode = 0b011;
    break;
  case IndexType::Unsigned:
    ASSERT_MSG(DYNA_REC, false, "IndexType::Unsigned is not supported!");
    break;
  }

  if (b64Bit)
  {
    op |= 0b10;
    ASSERT_MSG(DYNA_REC, (imm & 0x7) == 0, "64-bit load/store must use aligned offset: {}", imm);
    imm >>= 3;
  }
  else
  {
    ASSERT_MSG(DYNA_REC, (imm & 0x3) == 0, "32-bit load/store must use aligned offset: {}", imm);
    imm >>= 2;
  }

  ASSERT_MSG(DYNA_REC, imm >= -64 && imm < 64, "imm too large for load/store pair! {}", imm);

  Write32((op << 30) | (0b101 << 27) | (type_encode << 23) | (load << 22) | ((imm & 0x7F) << 15) |
          (DecodeReg(Rt2) << 10) | (DecodeReg(Rn) << 5) | DecodeReg(Rt));
}

void ARM64XEmitter::EncodeAddressInst(u32 op, ARM64Reg Rd, s32 imm)
{
  Write32((op << 31) | ((imm & 0x3) << 29) | (0x10 << 24) | ((imm & 0x1FFFFC) << 3) |
          DecodeReg(Rd));
}

void ARM64XEmitter::EncodeLoadStoreUnscaled(u32 size, u32 op, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  ASSERT_MSG(DYNA_REC, !(imm < -256 || imm > 255), "offset too large: {}", imm);

  Write32((size << 30) | (0b111 << 27) | (op << 22) | ((imm & 0x1FF) << 12) | (DecodeReg(Rn) << 5) |
          DecodeReg(Rt));
}

void ARM64XEmitter::LDR(ARM64Reg Rt, u32 imm)
{
  EncodeLoadRegisterInst(0, Rt, imm);
}

void ARM64XEmitter::LDRSW(ARM64Reg Rt, u32 imm)
{
  EncodeLoadRegisterInst(2, Rt, imm);
}

void ARM64XEmitter::PRFM(ARM64Reg Rt, u32 imm)
{
  EncodeLoadRegisterInst(3, Rt, imm);
}

void ARM64XEmitter::LDP(IndexType type, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, s32 imm)
{
  EncodeLoadStorePair(0, 1, type, Rt, Rt2, Rn, imm);
}

void ARM64XEmitter::LDPSW(IndexType type, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, s32 imm)
{
  EncodeLoadStorePair(1, 1, type, Rt, Rt2, Rn, imm);
}

void ARM64XEmitter::STP(IndexType type, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, s32 imm)
{
  EncodeLoadStorePair(0, 0, type, Rt, Rt2, Rn, imm);
}

void ARM64XEmitter::STXRB(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(0, Rs, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::STLXRB(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(1, Rs, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::LDXRB(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(2, ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::LDAXRB(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(3, ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::STLRB(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(4, ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::LDARB(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(5, ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::STXRH(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(6, Rs, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::STLXRH(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(7, Rs, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::LDXRH(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(8, ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::LDAXRH(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(9, ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::STLRH(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(10, ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::LDARH(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(11, ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::STXR(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(12 + Is64Bit(Rt), Rs, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::STLXR(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(14 + Is64Bit(Rt), Rs, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::STXP(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(16 + Is64Bit(Rt), Rs, Rt2, Rt, Rn);
}

void ARM64XEmitter::STLXP(ARM64Reg Rs, ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(18 + Is64Bit(Rt), Rs, Rt2, Rt, Rn);
}

void ARM64XEmitter::LDXR(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(20 + Is64Bit(Rt), ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::LDAXR(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(22 + Is64Bit(Rt), ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::LDXP(ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(24 + Is64Bit(Rt), ARM64Reg::SP, Rt2, Rt, Rn);
}

void ARM64XEmitter::LDAXP(ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(26 + Is64Bit(Rt), ARM64Reg::SP, Rt2, Rt, Rn);
}

void ARM64XEmitter::STLR(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(28 + Is64Bit(Rt), ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::LDAR(ARM64Reg Rt, ARM64Reg Rn)
{
  EncodeLoadStoreExcInst(30 + Is64Bit(Rt), ARM64Reg::SP, ARM64Reg::SP, Rt, Rn);
}

void ARM64XEmitter::STNP(ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, u32 imm)
{
  EncodeLoadStorePairedInst(0xA0, Rt, Rt2, Rn, imm);
}

void ARM64XEmitter::LDNP(ARM64Reg Rt, ARM64Reg Rt2, ARM64Reg Rn, u32 imm)
{
  EncodeLoadStorePairedInst(0xA1, Rt, Rt2, Rn, imm);
}

void ARM64XEmitter::STRB(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  if (type == IndexType::Unsigned)
    EncodeLoadStoreIndexedInst(0x0E4, Rt, Rn, imm, 8);
  else
    EncodeLoadStoreIndexedInst(0x0E0, type == IndexType::Post ? 1 : 3, Rt, Rn, imm);
}

void ARM64XEmitter::LDRB(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  if (type == IndexType::Unsigned)
    EncodeLoadStoreIndexedInst(0x0E5, Rt, Rn, imm, 8);
  else
    EncodeLoadStoreIndexedInst(0x0E1, type == IndexType::Post ? 1 : 3, Rt, Rn, imm);
}

void ARM64XEmitter::LDRSB(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  if (type == IndexType::Unsigned)
    EncodeLoadStoreIndexedInst(Is64Bit(Rt) ? 0x0E6 : 0x0E7, Rt, Rn, imm, 8);
  else
    EncodeLoadStoreIndexedInst(Is64Bit(Rt) ? 0x0E2 : 0x0E3, type == IndexType::Post ? 1 : 3, Rt, Rn,
                               imm);
}

void ARM64XEmitter::STRH(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  if (type == IndexType::Unsigned)
    EncodeLoadStoreIndexedInst(0x1E4, Rt, Rn, imm, 16);
  else
    EncodeLoadStoreIndexedInst(0x1E0, type == IndexType::Post ? 1 : 3, Rt, Rn, imm);
}

void ARM64XEmitter::LDRH(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  if (type == IndexType::Unsigned)
    EncodeLoadStoreIndexedInst(0x1E5, Rt, Rn, imm, 16);
  else
    EncodeLoadStoreIndexedInst(0x1E1, type == IndexType::Post ? 1 : 3, Rt, Rn, imm);
}

void ARM64XEmitter::LDRSH(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  if (type == IndexType::Unsigned)
    EncodeLoadStoreIndexedInst(Is64Bit(Rt) ? 0x1E6 : 0x1E7, Rt, Rn, imm, 16);
  else
    EncodeLoadStoreIndexedInst(Is64Bit(Rt) ? 0x1E2 : 0x1E3, type == IndexType::Post ? 1 : 3, Rt, Rn,
                               imm);
}

void ARM64XEmitter::STR(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  if (type == IndexType::Unsigned)
    EncodeLoadStoreIndexedInst(Is64Bit(Rt) ? 0x3E4 : 0x2E4, Rt, Rn, imm, Is64Bit(Rt) ? 64 : 32);
  else
    EncodeLoadStoreIndexedInst(Is64Bit(Rt) ? 0x3E0 : 0x2E0, type == IndexType::Post ? 1 : 3, Rt, Rn,
                               imm);
}

void ARM64XEmitter::LDR(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  if (type == IndexType::Unsigned)
    EncodeLoadStoreIndexedInst(Is64Bit(Rt) ? 0x3E5 : 0x2E5, Rt, Rn, imm, Is64Bit(Rt) ? 64 : 32);
  else
    EncodeLoadStoreIndexedInst(Is64Bit(Rt) ? 0x3E1 : 0x2E1, type == IndexType::Post ? 1 : 3, Rt, Rn,
                               imm);
}

void ARM64XEmitter::LDRSW(IndexType type, ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  if (type == IndexType::Unsigned)
    EncodeLoadStoreIndexedInst(0x2E6, Rt, Rn, imm, 32);
  else
    EncodeLoadStoreIndexedInst(0x2E2, type == IndexType::Post ? 1 : 3, Rt, Rn, imm);
}

void ARM64XEmitter::STRB(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  EncodeLoadStoreRegisterOffset(0, 0, Rt, Rn, Rm);
}

void ARM64XEmitter::LDRB(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  EncodeLoadStoreRegisterOffset(0, 1, Rt, Rn, Rm);
}

void ARM64XEmitter::LDRSB(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  bool b64Bit = Is64Bit(Rt);
  EncodeLoadStoreRegisterOffset(0, 3 - b64Bit, Rt, Rn, Rm);
}

void ARM64XEmitter::STRH(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  EncodeLoadStoreRegisterOffset(1, 0, Rt, Rn, Rm);
}

void ARM64XEmitter::LDRH(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  EncodeLoadStoreRegisterOffset(1, 1, Rt, Rn, Rm);
}

void ARM64XEmitter::LDRSH(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  bool b64Bit = Is64Bit(Rt);
  EncodeLoadStoreRegisterOffset(1, 3 - b64Bit, Rt, Rn, Rm);
}

void ARM64XEmitter::STR(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  bool b64Bit = Is64Bit(Rt);
  EncodeLoadStoreRegisterOffset(2 + b64Bit, 0, Rt, Rn, Rm);
}

void ARM64XEmitter::LDR(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  bool b64Bit = Is64Bit(Rt);
  EncodeLoadStoreRegisterOffset(2 + b64Bit, 1, Rt, Rn, Rm);
}

void ARM64XEmitter::LDRSW(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  EncodeLoadStoreRegisterOffset(2, 2, Rt, Rn, Rm);
}

void ARM64XEmitter::PRFM(ARM64Reg Rt, ARM64Reg Rn, ArithOption Rm)
{
  EncodeLoadStoreRegisterOffset(3, 2, Rt, Rn, Rm);
}

void ARM64XEmitter::STURB(ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EncodeLoadStoreUnscaled(0, 0, Rt, Rn, imm);
}

void ARM64XEmitter::LDURB(ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EncodeLoadStoreUnscaled(0, 1, Rt, Rn, imm);
}

void ARM64XEmitter::LDURSB(ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EncodeLoadStoreUnscaled(0, Is64Bit(Rt) ? 2 : 3, Rt, Rn, imm);
}

void ARM64XEmitter::STURH(ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EncodeLoadStoreUnscaled(1, 0, Rt, Rn, imm);
}

void ARM64XEmitter::LDURH(ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EncodeLoadStoreUnscaled(1, 1, Rt, Rn, imm);
}

void ARM64XEmitter::LDURSH(ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EncodeLoadStoreUnscaled(1, Is64Bit(Rt) ? 2 : 3, Rt, Rn, imm);
}

void ARM64XEmitter::STUR(ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EncodeLoadStoreUnscaled(Is64Bit(Rt) ? 3 : 2, 0, Rt, Rn, imm);
}

void ARM64XEmitter::LDUR(ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  EncodeLoadStoreUnscaled(Is64Bit(Rt) ? 3 : 2, 1, Rt, Rn, imm);
}

void ARM64XEmitter::LDURSW(ARM64Reg Rt, ARM64Reg Rn, s32 imm)
{
  ASSERT_MSG(DYNA_REC, !Is64Bit(Rt), "Must have a 64bit destination register!");
  EncodeLoadStoreUnscaled(2, 2, Rt, Rn, imm);
}

void ARM64XEmitter::ADR(ARM64Reg Rd, s32 imm)
{
  EncodeAddressInst(0, Rd, imm);
}

void ARM64XEmitter::ADRP(ARM64Reg Rd, s64 imm)
{
  EncodeAddressInst(1, Rd, static_cast<s32>(imm >> 12));
}

}  // namespace Arm64Gen
