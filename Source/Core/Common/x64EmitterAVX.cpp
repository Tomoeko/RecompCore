// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/x64Emitter.h"

#include <cstring>

#include "Common/CPUDetect.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/x64Reg.h"

namespace Gen

#define DEFINE_VEX_OP(name, prefix, op, W) \
  void XEmitter::name(X64Reg regOp1, X64Reg regOp2, const OpArg& arg) \
  { \
    WriteVEXOp(prefix, op, regOp1, regOp2, arg, W); \
  }

#define DEFINE_FMA3_OP(name, op, W) \
  void XEmitter::name(X64Reg regOp1, X64Reg regOp2, const OpArg& arg) \
  { \
    WriteFMA3Op(op, regOp1, regOp2, arg, W); \
  }

#define DEFINE_AVX_CMP_SHUF_OP(name, prefix, op) \
  void XEmitter::name(X64Reg regOp1, X64Reg regOp2, const OpArg& arg, u8 val) \
  { \
    WriteAVXOp(prefix, op, regOp1, regOp2, arg, 0, 1); \
    Write8(val); \
  }

{


static int GetVEXmmmmm(u16 op)
{
  // Currently, only 0x38 and 0x3A are used as secondary escape byte.
  if ((op >> 8) == 0x3A)
    return 3;
  else if ((op >> 8) == 0x38)
    return 2;
  else
    return 1;
}

static int GetVEXpp(u8 opPrefix)
{
  if (opPrefix == 0x66)
    return 1;
  else if (opPrefix == 0xF3)
    return 2;
  else if (opPrefix == 0xF2)
    return 3;
  else
    return 0;
}

static void CheckAVXSupport()
{
  if (!cpu_info.bAVX)
    PanicAlertFmt("Trying to use AVX on a system that doesn't support it. Bad programmer.");
}

void XEmitter::WriteVEXOp(u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg,
                          int W, int extrabytes)
{
  int mmmmm = GetVEXmmmmm(op);
  int pp = GetVEXpp(opPrefix);
  // Note that mixing an XMM register with a YMM register is invalid, which isn't checked here.
  int L = (regOp1 != INVALID_REG && regOp1 & 0x100) || (regOp2 != INVALID_REG && regOp2 & 0x100);
  arg.WriteVEX(this, regOp1, regOp2, L, pp, mmmmm, W);
  Write8(op & 0xFF);
  arg.WriteRest(this, extrabytes, regOp1);
}

void XEmitter::WriteVEXOp4(u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg,
                           X64Reg regOp3, int W)
{
  WriteVEXOp(opPrefix, op, regOp1, regOp2, arg, W, 1);
  Write8((u8)regOp3 << 4);
}

void XEmitter::WriteAVXOp(u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg,
                          int W, int extrabytes)
{
  CheckAVXSupport();
  WriteVEXOp(opPrefix, op, regOp1, regOp2, arg, W, extrabytes);
}

void XEmitter::WriteAVXOp4(u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg,
                           X64Reg regOp3, int W)
{
  CheckAVXSupport();
  WriteVEXOp4(opPrefix, op, regOp1, regOp2, arg, regOp3, W);
}

void XEmitter::WriteFMA3Op(u8 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg, int W)
{
  if (!cpu_info.bFMA)
  {
    PanicAlertFmt(
        "Trying to use FMA3 on a system that doesn't support it. Computer is v. f'n madd.");
  }
  WriteVEXOp(0x66, 0x3800 | op, regOp1, regOp2, arg, W);
}

void XEmitter::WriteFMA4Op(u8 op, X64Reg dest, X64Reg regOp1, X64Reg regOp2, const OpArg& arg,
                           int W)
{
  if (!cpu_info.bFMA4)
  {
    PanicAlertFmt(
        "Trying to use FMA4 on a system that doesn't support it. Computer is v. f'n madd.");
  }
  WriteVEXOp4(0x66, 0x3A00 | op, dest, regOp1, arg, regOp2, W);
}

void XEmitter::WriteBMIOp(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2,
                          const OpArg& arg, int extrabytes)
{
  if (arg.IsImm())
    PanicAlertFmt("BMI1/2 instructions don't support immediate operands.");
  if (size != 32 && size != 64)
    PanicAlertFmt("BMI1/2 instructions only support 32-bit and 64-bit modes!");
  const int W = size == 64;
  WriteVEXOp(opPrefix, op, regOp1, regOp2, arg, W, extrabytes);
}

void XEmitter::WriteBMI1Op(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2,
                           const OpArg& arg, int extrabytes)
{
  CheckFlags();
  if (!cpu_info.bBMI1)
    PanicAlertFmt("Trying to use BMI1 on a system that doesn't support it. Bad programmer.");
  WriteBMIOp(size, opPrefix, op, regOp1, regOp2, arg, extrabytes);
}

void XEmitter::WriteBMI2Op(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2,
                           const OpArg& arg, int extrabytes)
{
  if (!cpu_info.bBMI2)
    PanicAlertFmt("Trying to use BMI2 on a system that doesn't support it. Bad programmer.");
  WriteBMIOp(size, opPrefix, op, regOp1, regOp2, arg, extrabytes);
}

DEFINE_VEX_OP(VADDSS, 0xF3, sseADD, 0)

DEFINE_VEX_OP(VSUBSS, 0xF3, sseSUB, 0)

DEFINE_VEX_OP(VMULSS, 0xF3, sseMUL, 0)

DEFINE_VEX_OP(VDIVSS, 0xF3, sseDIV, 0)

DEFINE_VEX_OP(VADDPS, 0x00, sseADD, 0)

DEFINE_VEX_OP(VSUBPS, 0x00, sseSUB, 0)

DEFINE_VEX_OP(VMULPS, 0x00, sseMUL, 0)

DEFINE_VEX_OP(VDIVPS, 0x00, sseDIV, 0)

DEFINE_VEX_OP(VADDSD, 0xF2, sseADD, 0)

DEFINE_VEX_OP(VSUBSD, 0xF2, sseSUB, 0)

DEFINE_VEX_OP(VMULSD, 0xF2, sseMUL, 0)

DEFINE_VEX_OP(VDIVSD, 0xF2, sseDIV, 0)

DEFINE_VEX_OP(VADDPD, 0x66, sseADD, 0)

DEFINE_VEX_OP(VSUBPD, 0x66, sseSUB, 0)

DEFINE_VEX_OP(VMULPD, 0x66, sseMUL, 0)

DEFINE_VEX_OP(VDIVPD, 0x66, sseDIV, 0)

DEFINE_VEX_OP(VSQRTSD, 0xF2, sseSQRT, 0)

DEFINE_AVX_CMP_SHUF_OP(VCMPPD, 0x66, sseCMP)
DEFINE_AVX_CMP_SHUF_OP(VSHUFPS, 0x00, sseSHUF)
DEFINE_AVX_CMP_SHUF_OP(VSHUFPD, 0x66, sseSHUF)

DEFINE_VEX_OP(VUNPCKLPS, 0x00, 0x14, 0)

DEFINE_VEX_OP(VUNPCKLPD, 0x66, 0x14, 0)

DEFINE_VEX_OP(VUNPCKHPD, 0x66, 0x15, 0)

void XEmitter::VBLENDVPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg, X64Reg regOp3)
{
  WriteAVXOp4(0x66, 0x3A4B, regOp1, regOp2, arg, regOp3);
}

void XEmitter::VBLENDPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg, u8 blend)
{
  WriteAVXOp(0x66, 0x3A0C, regOp1, regOp2, arg, 0, 1);
  Write8(blend);
}

void XEmitter::VBLENDPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg, u8 blend)
{
  WriteAVXOp(0x66, 0x3A0D, regOp1, regOp2, arg, 0, 1);
  Write8(blend);
}

DEFINE_VEX_OP(VANDPS, 0x00, sseAND, 0)

DEFINE_VEX_OP(VANDPD, 0x66, sseAND, 0)

DEFINE_VEX_OP(VANDNPS, 0x00, sseANDN, 0)

DEFINE_VEX_OP(VANDNPD, 0x66, sseANDN, 0)

DEFINE_VEX_OP(VORPS, 0x00, sseOR, 0)

DEFINE_VEX_OP(VORPD, 0x66, sseOR, 0)

DEFINE_VEX_OP(VXORPS, 0x00, sseXOR, 0)

DEFINE_VEX_OP(VXORPD, 0x66, sseXOR, 0)

DEFINE_VEX_OP(VPAND, 0x66, 0xDB, 0)

DEFINE_VEX_OP(VPANDN, 0x66, 0xDF, 0)

DEFINE_VEX_OP(VPOR, 0x66, 0xEB, 0)

DEFINE_VEX_OP(VPXOR, 0x66, 0xEF, 0)

void XEmitter::VPSLLQ(X64Reg regOp1, X64Reg regOp2, u8 shift)
{
  WriteAVXOp(0x66, 0x73, (X64Reg)6, regOp1, R(regOp2));
  Write8(shift);
}

void XEmitter::VMOVAPS(const OpArg& arg, X64Reg regOp)
{
  WriteAVXOp(0x00, 0x29, regOp, X64Reg::INVALID_REG, arg);
}

void XEmitter::VZEROUPPER()
{
  CheckAVXSupport();
  Write8(0xC5);
  Write8(0xF8);
  Write8(0x77);
}

DEFINE_FMA3_OP(VFMADD132PS, 0x98, 0)

DEFINE_FMA3_OP(VFMADD213PS, 0xA8, 0)

DEFINE_FMA3_OP(VFMADD231PS, 0xB8, 0)

DEFINE_FMA3_OP(VFMADD132PD, 0x98, 1)

DEFINE_FMA3_OP(VFMADD213PD, 0xA8, 1)

DEFINE_FMA3_OP(VFMADD231PD, 0xB8, 1)

DEFINE_FMA3_OP(VFMADD132SS, 0x99, 0)

DEFINE_FMA3_OP(VFMADD213SS, 0xA9, 0)

DEFINE_FMA3_OP(VFMADD231SS, 0xB9, 0)

DEFINE_FMA3_OP(VFMADD132SD, 0x99, 1)

DEFINE_FMA3_OP(VFMADD213SD, 0xA9, 1)

DEFINE_FMA3_OP(VFMADD231SD, 0xB9, 1)

DEFINE_FMA3_OP(VFMSUB132PS, 0x9A, 0)

DEFINE_FMA3_OP(VFMSUB213PS, 0xAA, 0)

DEFINE_FMA3_OP(VFMSUB231PS, 0xBA, 0)

DEFINE_FMA3_OP(VFMSUB132PD, 0x9A, 1)

DEFINE_FMA3_OP(VFMSUB213PD, 0xAA, 1)

DEFINE_FMA3_OP(VFMSUB231PD, 0xBA, 1)

DEFINE_FMA3_OP(VFMSUB132SS, 0x9B, 0)

DEFINE_FMA3_OP(VFMSUB213SS, 0xAB, 0)

DEFINE_FMA3_OP(VFMSUB231SS, 0xBB, 0)

DEFINE_FMA3_OP(VFMSUB132SD, 0x9B, 1)

DEFINE_FMA3_OP(VFMSUB213SD, 0xAB, 1)

DEFINE_FMA3_OP(VFMSUB231SD, 0xBB, 1)

DEFINE_FMA3_OP(VFNMADD132PS, 0x9C, 0)

DEFINE_FMA3_OP(VFNMADD213PS, 0xAC, 0)

DEFINE_FMA3_OP(VFNMADD231PS, 0xBC, 0)

DEFINE_FMA3_OP(VFNMADD132PD, 0x9C, 1)

DEFINE_FMA3_OP(VFNMADD213PD, 0xAC, 1)

DEFINE_FMA3_OP(VFNMADD231PD, 0xBC, 1)

DEFINE_FMA3_OP(VFNMADD132SS, 0x9D, 0)

DEFINE_FMA3_OP(VFNMADD213SS, 0xAD, 0)

DEFINE_FMA3_OP(VFNMADD231SS, 0xBD, 0)

DEFINE_FMA3_OP(VFNMADD132SD, 0x9D, 1)

DEFINE_FMA3_OP(VFNMADD213SD, 0xAD, 1)

DEFINE_FMA3_OP(VFNMADD231SD, 0xBD, 1)

DEFINE_FMA3_OP(VFNMSUB132PS, 0x9E, 0)

DEFINE_FMA3_OP(VFNMSUB213PS, 0xAE, 0)

DEFINE_FMA3_OP(VFNMSUB231PS, 0xBE, 0)

DEFINE_FMA3_OP(VFNMSUB132PD, 0x9E, 1)

DEFINE_FMA3_OP(VFNMSUB213PD, 0xAE, 1)

DEFINE_FMA3_OP(VFNMSUB231PD, 0xBE, 1)

DEFINE_FMA3_OP(VFNMSUB132SS, 0x9F, 0)

DEFINE_FMA3_OP(VFNMSUB213SS, 0xAF, 0)

DEFINE_FMA3_OP(VFNMSUB231SS, 0xBF, 0)

DEFINE_FMA3_OP(VFNMSUB132SD, 0x9F, 1)

DEFINE_FMA3_OP(VFNMSUB213SD, 0xAF, 1)

DEFINE_FMA3_OP(VFNMSUB231SD, 0xBF, 1)

DEFINE_FMA3_OP(VFMADDSUB132PS, 0x96, 0)

DEFINE_FMA3_OP(VFMADDSUB213PS, 0xA6, 0)

DEFINE_FMA3_OP(VFMADDSUB231PS, 0xB6, 0)

DEFINE_FMA3_OP(VFMADDSUB132PD, 0x96, 1)

DEFINE_FMA3_OP(VFMADDSUB213PD, 0xA6, 1)

DEFINE_FMA3_OP(VFMADDSUB231PD, 0xB6, 1)

DEFINE_FMA3_OP(VFMSUBADD132PS, 0x97, 0)

DEFINE_FMA3_OP(VFMSUBADD213PS, 0xA7, 0)

DEFINE_FMA3_OP(VFMSUBADD231PS, 0xB7, 0)

DEFINE_FMA3_OP(VFMSUBADD132PD, 0x97, 1)

DEFINE_FMA3_OP(VFMSUBADD213PD, 0xA7, 1)

DEFINE_FMA3_OP(VFMSUBADD231PD, 0xB7, 1)

#define FMA4(name, op)                                                                             \
  void XEmitter::name(X64Reg dest, X64Reg regOp1, X64Reg regOp2, const OpArg& arg)                 \
  {                                                                                                \
    WriteFMA4Op(op, dest, regOp1, regOp2, arg, 1);                                                 \
  }                                                                                                \
  void XEmitter::name(X64Reg dest, X64Reg regOp1, const OpArg& arg, X64Reg regOp2)                 \
  {                                                                                                \
    WriteFMA4Op(op, dest, regOp1, regOp2, arg, 0);                                                 \
  }

FMA4(VFMADDSUBPS, 0x5C)
FMA4(VFMADDSUBPD, 0x5D)
FMA4(VFMSUBADDPS, 0x5E)
FMA4(VFMSUBADDPD, 0x5F)
FMA4(VFMADDPS, 0x68)
FMA4(VFMADDPD, 0x69)
FMA4(VFMADDSS, 0x6A)
FMA4(VFMADDSD, 0x6B)
FMA4(VFMSUBPS, 0x6C)
FMA4(VFMSUBPD, 0x6D)
FMA4(VFMSUBSS, 0x6E)
FMA4(VFMSUBSD, 0x6F)
FMA4(VFNMADDPS, 0x78)
FMA4(VFNMADDPD, 0x79)
FMA4(VFNMADDSS, 0x7A)
FMA4(VFNMADDSD, 0x7B)
FMA4(VFNMSUBPS, 0x7C)
FMA4(VFNMSUBPD, 0x7D)
FMA4(VFNMSUBSS, 0x7E)
FMA4(VFNMSUBSD, 0x7F)
#undef FMA4

}  // namespace Gen


