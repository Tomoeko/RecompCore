// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/x64Emitter.h"

#include <cstring>

#include "Common/CPUDetect.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/x64Reg.h"

namespace Gen

#define DEFINE_SSE_INT_OP(name, op) \
  void XEmitter::name(X64Reg dest, const OpArg& arg) \
  { \
    WriteSSEOp(0x66, op, dest, arg); \
  }

{

void XEmitter::MOVNTDQ(const OpArg& arg, X64Reg regOp)
{
  WriteSSEOp(0x66, sseMOVNTDQ, regOp, arg);
}

void XEmitter::MASKMOVDQU(X64Reg dest, X64Reg src)
{
  WriteSSEOp(0x66, sseMASKMOVDQU, dest, R(src));
}

DEFINE_SSE_INT_OP(PACKUSWB, 0x67)

DEFINE_SSE_INT_OP(PUNPCKLBW, 0x60)

DEFINE_SSE_INT_OP(PUNPCKLWD, 0x61)

DEFINE_SSE_INT_OP(PUNPCKLDQ, 0x62)

DEFINE_SSE_INT_OP(PUNPCKLQDQ, 0x6C)

DEFINE_SSE_INT_OP(PTEST, 0x17)

DEFINE_SSE_INT_OP(PMOVSXBW, 0x3820)

DEFINE_SSE_INT_OP(PMOVSXBD, 0x3821)

DEFINE_SSE_INT_OP(PMOVSXBQ, 0x3822)

DEFINE_SSE_INT_OP(PMOVSXWD, 0x3823)

DEFINE_SSE_INT_OP(PMOVSXWQ, 0x3824)

DEFINE_SSE_INT_OP(PMOVSXDQ, 0x3825)

DEFINE_SSE_INT_OP(PMOVZXBW, 0x3830)

DEFINE_SSE_INT_OP(PMOVZXBD, 0x3831)

DEFINE_SSE_INT_OP(PMOVZXBQ, 0x3832)

DEFINE_SSE_INT_OP(PMOVZXWD, 0x3833)

DEFINE_SSE_INT_OP(PMOVZXWQ, 0x3834)

DEFINE_SSE_INT_OP(PMOVZXDQ, 0x3835)

DEFINE_SSE_INT_OP(PBLENDVB, 0x3810)

DEFINE_SSE_INT_OP(PCMPEQQ, 0x3829)

DEFINE_SSE_INT_OP(PAND, 0xDB)

DEFINE_SSE_INT_OP(PANDN, 0xDF)

DEFINE_SSE_INT_OP(PXOR, 0xEF)

DEFINE_SSE_INT_OP(POR, 0xEB)

DEFINE_SSE_INT_OP(PADDB, 0xFC)

DEFINE_SSE_INT_OP(PADDW, 0xFD)

DEFINE_SSE_INT_OP(PADDD, 0xFE)

DEFINE_SSE_INT_OP(PADDQ, 0xD4)

DEFINE_SSE_INT_OP(PADDSB, 0xEC)

DEFINE_SSE_INT_OP(PADDSW, 0xED)

DEFINE_SSE_INT_OP(PADDUSB, 0xDC)

DEFINE_SSE_INT_OP(PADDUSW, 0xDD)

DEFINE_SSE_INT_OP(PAVGB, 0xE0)

DEFINE_SSE_INT_OP(PAVGW, 0xE3)

DEFINE_SSE_INT_OP(PCMPEQB, 0x74)

DEFINE_SSE_INT_OP(PCMPEQW, 0x75)

DEFINE_SSE_INT_OP(PCMPEQD, 0x76)

DEFINE_SSE_INT_OP(PCMPGTB, 0x64)

DEFINE_SSE_INT_OP(PCMPGTW, 0x65)

DEFINE_SSE_INT_OP(PCMPGTD, 0x66)

void XEmitter::PEXTRW(X64Reg dest, const OpArg& arg, u8 subreg)
{
  WriteSSEOp(0x66, 0xC5, dest, arg);
  Write8(subreg);
}

void XEmitter::PINSRW(X64Reg dest, const OpArg& arg, u8 subreg)
{
  WriteSSEOp(0x66, 0xC4, dest, arg);
  Write8(subreg);
}

void XEmitter::PINSRD(X64Reg dest, const OpArg& arg, u8 subreg)
{
  WriteSSE41Op(0x66, 0x3A22, dest, arg);
  Write8(subreg);
}

DEFINE_SSE_INT_OP(PMADDWD, 0xF5)

DEFINE_SSE_INT_OP(PMAXSW, 0xEE)

DEFINE_SSE_INT_OP(PMAXUB, 0xDE)

DEFINE_SSE_INT_OP(PMINSW, 0xEA)

DEFINE_SSE_INT_OP(PMINUB, 0xDA)

DEFINE_SSE_INT_OP(PMOVMSKB, 0xD7)

void XEmitter::PEXT(int bits, X64Reg regOp1, X64Reg regOp2, const OpArg& arg)
{
  WriteBMI2Op(bits, 0xF3, 0x38F5, regOp1, regOp2, arg);
}

}  // namespace Gen

#define DEFINE_SSE_INT_OP(name, op) \
  void XEmitter::name(X64Reg dest, const OpArg& arg) \
  { \
    WriteSSEOp(0x66, op, dest, arg); \
  }

