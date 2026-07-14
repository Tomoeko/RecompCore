// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/x64Emitter.h"

#include <cstring>

#include "Common/CPUDetect.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/x64Reg.h"

namespace Gen

#define DEFINE_SSE_OP(name, prefix, op) \
  void XEmitter::name(X64Reg regOp, const OpArg& arg) \
  { \
    WriteSSEOp(prefix, op, regOp, arg); \
  }

#define DEFINE_SSE_TO_RM_OP(name, prefix, op) \
  void XEmitter::name(const OpArg& arg, X64Reg regOp) \
  { \
    WriteSSEOp(prefix, op, regOp, arg); \
  }

{

void XEmitter::PREFETCH(PrefetchLevel level, OpArg arg)
{
  ASSERT_MSG(DYNA_REC, !arg.IsImm(), "PREFETCH - Imm argument");
  arg.operandReg = static_cast<u8>(level);
  arg.WriteREX(this, 0, 0);
  Write8(0x0F);
  Write8(0x18);
  arg.WriteRest(this);
}

void XEmitter::WriteSSEOp(u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes)
{
  if (opPrefix)
    Write8(opPrefix);
  arg.operandReg = regOp;
  arg.WriteREX(this, 0, 0);
  Write8(0x0F);
  if (op > 0xFF)
    Write8((op >> 8) & 0xFF);
  Write8(op & 0xFF);
  arg.WriteRest(this, extrabytes);
}

void XEmitter::MOVD_xmm(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0x6E, dest, arg, 0);
}

void XEmitter::MOVD_xmm(const OpArg& arg, X64Reg src)
{
  WriteSSEOp(0x66, 0x7E, src, arg, 0);
}

void XEmitter::MOVQ_xmm(X64Reg dest, OpArg arg)
{
  // Alternate encoding
  // This does not display correctly in MSVC's debugger, it thinks it's a MOVD
  arg.operandReg = dest;
  Write8(0x66);
  arg.WriteREX(this, 64, 0);
  Write8(0x0f);
  Write8(0x6E);
  arg.WriteRest(this, 0);
}

void XEmitter::MOVQ_xmm(OpArg arg, X64Reg src)
{
  if (src > 7 || arg.IsSimpleReg())
  {
    // Alternate encoding
    // This does not display correctly in MSVC's debugger, it thinks it's a MOVD
    arg.operandReg = src;
    Write8(0x66);
    arg.WriteREX(this, 64, 0);
    Write8(0x0f);
    Write8(0x7E);
    arg.WriteRest(this, 0);
  }
  else
  {
    arg.operandReg = src;
    arg.WriteREX(this, 0, 0);
    Write8(0x66);
    Write8(0x0f);
    Write8(0xD6);
    arg.WriteRest(this, 0);
  }
}

void XEmitter::WriteMXCSR(OpArg arg, int ext)
{
  if (arg.IsImm() || arg.IsSimpleReg())
    ASSERT_MSG(DYNA_REC, 0, "MXCSR - invalid operand");

  arg.operandReg = ext;
  arg.WriteREX(this, 0, 0);
  Write8(0x0F);
  Write8(0xAE);
  arg.WriteRest(this);
}

void XEmitter::STMXCSR(const OpArg& memloc)
{
  WriteMXCSR(memloc, 3);
}

void XEmitter::LDMXCSR(const OpArg& memloc)
{
  WriteMXCSR(memloc, 2);
}

void XEmitter::MOVNTPS(const OpArg& arg, X64Reg regOp)
{
  WriteSSEOp(0x00, sseMOVNTP, regOp, arg);
}

void XEmitter::MOVNTPD(const OpArg& arg, X64Reg regOp)
{
  WriteSSEOp(0x66, sseMOVNTP, regOp, arg);
}

DEFINE_SSE_OP(ADDSS, 0xF3, sseADD)

DEFINE_SSE_OP(ADDSD, 0xF2, sseADD)

DEFINE_SSE_OP(SUBSS, 0xF3, sseSUB)

DEFINE_SSE_OP(SUBSD, 0xF2, sseSUB)

void XEmitter::CMPSS(X64Reg regOp, const OpArg& arg, u8 compare)
{
  WriteSSEOp(0xF3, sseCMP, regOp, arg, 1);
  Write8(compare);
}

void XEmitter::CMPSD(X64Reg regOp, const OpArg& arg, u8 compare)
{
  WriteSSEOp(0xF2, sseCMP, regOp, arg, 1);
  Write8(compare);
}

DEFINE_SSE_OP(MULSS, 0xF3, sseMUL)

DEFINE_SSE_OP(MULSD, 0xF2, sseMUL)

DEFINE_SSE_OP(DIVSS, 0xF3, sseDIV)

DEFINE_SSE_OP(DIVSD, 0xF2, sseDIV)

DEFINE_SSE_OP(MINSS, 0xF3, sseMIN)

DEFINE_SSE_OP(MINSD, 0xF2, sseMIN)

DEFINE_SSE_OP(MAXSS, 0xF3, sseMAX)

DEFINE_SSE_OP(MAXSD, 0xF2, sseMAX)

DEFINE_SSE_OP(SQRTSS, 0xF3, sseSQRT)

DEFINE_SSE_OP(SQRTSD, 0xF2, sseSQRT)

DEFINE_SSE_OP(RCPSS, 0xF3, sseRCP)

DEFINE_SSE_OP(RSQRTSS, 0xF3, sseRSQRT)

DEFINE_SSE_OP(ADDPS, 0x00, sseADD)

DEFINE_SSE_OP(ADDPD, 0x66, sseADD)

DEFINE_SSE_OP(SUBPS, 0x00, sseSUB)

DEFINE_SSE_OP(SUBPD, 0x66, sseSUB)

void XEmitter::CMPPS(X64Reg regOp, const OpArg& arg, u8 compare)
{
  WriteSSEOp(0x00, sseCMP, regOp, arg, 1);
  Write8(compare);
}

void XEmitter::CMPPD(X64Reg regOp, const OpArg& arg, u8 compare)
{
  WriteSSEOp(0x66, sseCMP, regOp, arg, 1);
  Write8(compare);
}

DEFINE_SSE_OP(ANDPS, 0x00, sseAND)

DEFINE_SSE_OP(ANDPD, 0x66, sseAND)

DEFINE_SSE_OP(ANDNPS, 0x00, sseANDN)

DEFINE_SSE_OP(ANDNPD, 0x66, sseANDN)

DEFINE_SSE_OP(ORPS, 0x00, sseOR)

DEFINE_SSE_OP(ORPD, 0x66, sseOR)

DEFINE_SSE_OP(XORPS, 0x00, sseXOR)

DEFINE_SSE_OP(XORPD, 0x66, sseXOR)

DEFINE_SSE_OP(MULPS, 0x00, sseMUL)

DEFINE_SSE_OP(MULPD, 0x66, sseMUL)

DEFINE_SSE_OP(DIVPS, 0x00, sseDIV)

DEFINE_SSE_OP(DIVPD, 0x66, sseDIV)

DEFINE_SSE_OP(MINPS, 0x00, sseMIN)

DEFINE_SSE_OP(MINPD, 0x66, sseMIN)

DEFINE_SSE_OP(MAXPS, 0x00, sseMAX)

DEFINE_SSE_OP(MAXPD, 0x66, sseMAX)

DEFINE_SSE_OP(SQRTPS, 0x00, sseSQRT)

DEFINE_SSE_OP(SQRTPD, 0x66, sseSQRT)

DEFINE_SSE_OP(RCPPS, 0x00, sseRCP)

DEFINE_SSE_OP(RSQRTPS, 0x00, sseRSQRT)

void XEmitter::SHUFPS(X64Reg regOp, const OpArg& arg, u8 shuffle)
{
  WriteSSEOp(0x00, sseSHUF, regOp, arg, 1);
  Write8(shuffle);
}

void XEmitter::SHUFPD(X64Reg regOp, const OpArg& arg, u8 shuffle)
{
  WriteSSEOp(0x66, sseSHUF, regOp, arg, 1);
  Write8(shuffle);
}

DEFINE_SSE_OP(COMISS, 0x00, sseCOMIS)

DEFINE_SSE_OP(COMISD, 0x66, sseCOMIS)

DEFINE_SSE_OP(UCOMISS, 0x00, sseUCOMIS)

DEFINE_SSE_OP(UCOMISD, 0x66, sseUCOMIS)

DEFINE_SSE_OP(MOVAPS, 0x00, sseMOVAPfromRM)

DEFINE_SSE_OP(MOVAPD, 0x66, sseMOVAPfromRM)

DEFINE_SSE_TO_RM_OP(MOVAPS, 0x00, sseMOVAPtoRM)

DEFINE_SSE_TO_RM_OP(MOVAPD, 0x66, sseMOVAPtoRM)

DEFINE_SSE_OP(MOVUPS, 0x00, sseMOVUPfromRM)

DEFINE_SSE_OP(MOVUPD, 0x66, sseMOVUPfromRM)

DEFINE_SSE_TO_RM_OP(MOVUPS, 0x00, sseMOVUPtoRM)

DEFINE_SSE_TO_RM_OP(MOVUPD, 0x66, sseMOVUPtoRM)

DEFINE_SSE_OP(MOVSS, 0xF3, 0x10)

DEFINE_SSE_OP(MOVSD, 0xF2, 0x10)

DEFINE_SSE_TO_RM_OP(MOVSS, 0xF3, 0x11)

DEFINE_SSE_TO_RM_OP(MOVSD, 0xF2, 0x11)

DEFINE_SSE_OP(MOVLPS, 0x00, sseMOVLPfromRM)

DEFINE_SSE_OP(MOVLPD, 0x66, sseMOVLPfromRM)

DEFINE_SSE_TO_RM_OP(MOVLPS, 0x00, sseMOVLPtoRM)

DEFINE_SSE_TO_RM_OP(MOVLPD, 0x66, sseMOVLPtoRM)

DEFINE_SSE_OP(MOVHPS, 0x00, sseMOVHPfromRM)

DEFINE_SSE_OP(MOVHPD, 0x66, sseMOVHPfromRM)

DEFINE_SSE_TO_RM_OP(MOVHPS, 0x00, sseMOVHPtoRM)

DEFINE_SSE_TO_RM_OP(MOVHPD, 0x66, sseMOVHPtoRM)

void XEmitter::MOVHLPS(X64Reg regOp1, X64Reg regOp2)
{
  WriteSSEOp(0x00, sseMOVHLPS, regOp1, R(regOp2));
}

void XEmitter::MOVLHPS(X64Reg regOp1, X64Reg regOp2)
{
  WriteSSEOp(0x00, sseMOVLHPS, regOp1, R(regOp2));
}

DEFINE_SSE_OP(CVTPS2PD, 0x00, 0x5A)

DEFINE_SSE_OP(CVTPD2PS, 0x66, 0x5A)

DEFINE_SSE_OP(CVTSD2SS, 0xF2, 0x5A)

DEFINE_SSE_OP(CVTSS2SD, 0xF3, 0x5A)

void XEmitter::CVTSD2SI(X64Reg regOp, const OpArg& arg)
{
  WriteSSEOp(0xF2, 0x2D, regOp, arg);
}

void XEmitter::CVTSS2SI(X64Reg regOp, const OpArg& arg)
{
  WriteSSEOp(0xF3, 0x2D, regOp, arg);
}

DEFINE_SSE_OP(CVTSI2SD, 0xF2, 0x2A)

DEFINE_SSE_OP(CVTSI2SS, 0xF3, 0x2A)

DEFINE_SSE_OP(CVTDQ2PD, 0xF3, 0xE6)

DEFINE_SSE_OP(CVTDQ2PS, 0x00, 0x5B)

DEFINE_SSE_OP(CVTPD2DQ, 0xF2, 0xE6)

DEFINE_SSE_OP(CVTPS2DQ, 0x66, 0x5B)

void XEmitter::CVTTSD2SI(X64Reg regOp, const OpArg& arg)
{
  WriteSSEOp(0xF2, 0x2C, regOp, arg);
}

void XEmitter::CVTTSS2SI(X64Reg regOp, const OpArg& arg)
{
  WriteSSEOp(0xF3, 0x2C, regOp, arg);
}

DEFINE_SSE_OP(CVTTPS2DQ, 0xF3, 0x5B)

DEFINE_SSE_OP(CVTTPD2DQ, 0x66, 0xE6)

void XEmitter::MOVMSKPS(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x00, 0x50, dest, arg);
}

void XEmitter::MOVMSKPD(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0x50, dest, arg);
}

void XEmitter::LDDQU(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0xF2, sseLDDQU, dest, arg);
}  // For integer data only

DEFINE_SSE_OP(UNPCKLPS, 0x00, 0x14)

DEFINE_SSE_OP(UNPCKHPS, 0x00, 0x15)

DEFINE_SSE_OP(UNPCKLPD, 0x66, 0x14)

DEFINE_SSE_OP(UNPCKHPD, 0x66, 0x15)

DEFINE_SSE_OP(MOVSLDUP, 0xF3, 0x12)

DEFINE_SSE_OP(MOVSHDUP, 0xF3, 0x16)

DEFINE_SSE_OP(MOVDDUP, 0xF2, 0x12)

void XEmitter::PACKSSDW(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0x6B, dest, arg);
}

void XEmitter::PACKSSWB(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0x63, dest, arg);
}

void XEmitter::PSRLW(X64Reg reg, u8 shift)
{
  WriteSSEOp(0x66, 0x71, (X64Reg)2, R(reg));
  Write8(shift);
}

void XEmitter::PSRLD(X64Reg reg, u8 shift)
{
  WriteSSEOp(0x66, 0x72, (X64Reg)2, R(reg));
  Write8(shift);
}

void XEmitter::PSRLQ(X64Reg reg, u8 shift)
{
  WriteSSEOp(0x66, 0x73, (X64Reg)2, R(reg));
  Write8(shift);
}

void XEmitter::PSRLQ(X64Reg reg, const OpArg& arg)
{
  WriteSSEOp(0x66, 0xd3, reg, arg);
}

void XEmitter::PSRLDQ(X64Reg reg, u8 shift)
{
  WriteSSEOp(0x66, 0x73, (X64Reg)3, R(reg));
  Write8(shift);
}

void XEmitter::PSLLW(X64Reg reg, u8 shift)
{
  WriteSSEOp(0x66, 0x71, (X64Reg)6, R(reg));
  Write8(shift);
}

void XEmitter::PSLLD(X64Reg reg, u8 shift)
{
  WriteSSEOp(0x66, 0x72, (X64Reg)6, R(reg));
  Write8(shift);
}

void XEmitter::PSLLQ(X64Reg reg, u8 shift)
{
  WriteSSEOp(0x66, 0x73, (X64Reg)6, R(reg));
  Write8(shift);
}

void XEmitter::PSLLDQ(X64Reg reg, u8 shift)
{
  WriteSSEOp(0x66, 0x73, (X64Reg)7, R(reg));
  Write8(shift);
}

void XEmitter::PSRAW(X64Reg reg, u8 shift)
{
  if (reg > 7)
    PanicAlertFmt("The PSRAW-emitter does not support regs above 7");
  Write8(0x66);
  Write8(0x0f);
  Write8(0x71);
  Write8(0xE0 | reg);
  Write8(shift);
}

void XEmitter::PSRAD(X64Reg reg, u8 shift)
{
  if (reg > 7)
    PanicAlertFmt("The PSRAD-emitter does not support regs above 7");
  Write8(0x66);
  Write8(0x0f);
  Write8(0x72);
  Write8(0xE0 | reg);
  Write8(shift);
}

void XEmitter::WriteSSSE3Op(u8 opPrefix, u16 op, X64Reg regOp, const OpArg& arg, int extrabytes)
{
  if (!cpu_info.bSSSE3)
    PanicAlertFmt("Trying to use SSSE3 on a system that doesn't support it. Bad programmer.");
  WriteSSEOp(opPrefix, op, regOp, arg, extrabytes);
}

void XEmitter::WriteSSE41Op(u8 opPrefix, u16 op, X64Reg regOp, const OpArg& arg, int extrabytes)
{
  if (!cpu_info.bSSE4_1)
    PanicAlertFmt("Trying to use SSE4.1 on a system that doesn't support it. Bad programmer.");
  WriteSSEOp(opPrefix, op, regOp, arg, extrabytes);
}

void XEmitter::PSHUFB(X64Reg dest, const OpArg& arg)
{
  WriteSSSE3Op(0x66, 0x3800, dest, arg);
}

void XEmitter::PACKUSDW(X64Reg dest, const OpArg& arg)
{
  WriteSSE41Op(0x66, 0x382b, dest, arg);
}

void XEmitter::BLENDVPS(X64Reg dest, const OpArg& arg)
{
  WriteSSE41Op(0x66, 0x3814, dest, arg);
}

void XEmitter::BLENDVPD(X64Reg dest, const OpArg& arg)
{
  WriteSSE41Op(0x66, 0x3815, dest, arg);
}

void XEmitter::BLENDPS(X64Reg dest, const OpArg& arg, u8 blend)
{
  WriteSSE41Op(0x66, 0x3A0C, dest, arg, 1);
  Write8(blend);
}

void XEmitter::BLENDPD(X64Reg dest, const OpArg& arg, u8 blend)
{
  WriteSSE41Op(0x66, 0x3A0D, dest, arg, 1);
  Write8(blend);
}

void XEmitter::PSUBB(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0xF8, dest, arg);
}

void XEmitter::PSUBW(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0xF9, dest, arg);
}

void XEmitter::PSUBD(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0xFA, dest, arg);
}

void XEmitter::PSUBQ(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0xFB, dest, arg);
}

void XEmitter::PSUBSB(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0xE8, dest, arg);
}

void XEmitter::PSUBSW(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0xE9, dest, arg);
}

void XEmitter::PSUBUSB(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0xD8, dest, arg);
}

void XEmitter::PSUBUSW(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0xD9, dest, arg);
}

void XEmitter::PSADBW(X64Reg dest, const OpArg& arg)
{
  WriteSSEOp(0x66, 0xF6, dest, arg);
}

void XEmitter::PSHUFD(X64Reg regOp, const OpArg& arg, u8 shuffle)
{
  WriteSSEOp(0x66, 0x70, regOp, arg, 1);
  Write8(shuffle);
}

void XEmitter::PSHUFLW(X64Reg regOp, const OpArg& arg, u8 shuffle)
{
  WriteSSEOp(0xF2, 0x70, regOp, arg, 1);
  Write8(shuffle);
}

void XEmitter::PSHUFHW(X64Reg regOp, const OpArg& arg, u8 shuffle)
{
  WriteSSEOp(0xF3, 0x70, regOp, arg, 1);
  Write8(shuffle);
}

void XEmitter::PDEP(int bits, X64Reg regOp1, X64Reg regOp2, const OpArg& arg)
{
  WriteBMI2Op(bits, 0xF2, 0x38F5, regOp1, regOp2, arg);
}

}  // namespace Gen

#define DEFINE_SSE_OP(name, prefix, op) \
  void XEmitter::name(X64Reg regOp, const OpArg& arg) \
  { \
    WriteSSEOp(prefix, op, regOp, arg); \
  }

#define DEFINE_SSE_TO_RM_OP(name, prefix, op) \
  void XEmitter::name(const OpArg& arg, X64Reg regOp) \
  { \
    WriteSSEOp(prefix, op, regOp, arg); \
  }

