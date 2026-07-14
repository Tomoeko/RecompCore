// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

  void VADDSS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VSUBSS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VMULSS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VDIVSS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VADDPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VSUBPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VMULPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VDIVPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VADDSD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VSUBSD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VMULSD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VDIVSD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VADDPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VSUBPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VMULPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VDIVPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VSQRTSD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VCMPPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg, u8 compare);
  void VSHUFPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg, u8 shuffle);
  void VSHUFPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg, u8 shuffle);
  void VUNPCKLPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VUNPCKLPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VUNPCKHPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VBLENDVPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg, X64Reg mask);
  void VBLENDPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg, u8 blend);
  void VBLENDPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg, u8 blend);

  void VANDPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VANDPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VANDNPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VANDNPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VORPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VORPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VXORPS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VXORPD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);

  void VPAND(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VPANDN(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VPOR(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VPXOR(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);

  void VPSLLQ(X64Reg regOp1, X64Reg regOp2, u8 shift);

  void VMOVAPS(const OpArg& arg, X64Reg regOp);

  void VZEROUPPER();

  // FMA3
  void VFMADD132PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADD213PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADD231PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADD132PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADD213PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADD231PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADD132SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADD213SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADD231SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADD132SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADD213SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADD231SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB132PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB213PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB231PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB132PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB213PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB231PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB132SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB213SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB231SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB132SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB213SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUB231SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD132PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD213PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD231PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD132PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD213PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD231PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD132SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD213SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD231SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD132SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD213SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMADD231SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB132PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB213PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB231PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB132PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB213PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB231PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB132SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB213SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB231SS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB132SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB213SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFNMSUB231SD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADDSUB132PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADDSUB213PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADDSUB231PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADDSUB132PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADDSUB213PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMADDSUB231PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUBADD132PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUBADD213PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUBADD231PS(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUBADD132PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUBADD213PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void VFMSUBADD231PD(X64Reg regOp1, X64Reg regOp2, const OpArg& arg);

#define FMA4(name)                                                                                 \
  void name(X64Reg dest, X64Reg regOp1, X64Reg regOp2, const OpArg& arg);                          \
  void name(X64Reg dest, X64Reg regOp1, const OpArg& arg, X64Reg regOp2);

  FMA4(VFMADDSUBPS)
  FMA4(VFMADDSUBPD)
  FMA4(VFMSUBADDPS)
  FMA4(VFMSUBADDPD)
  FMA4(VFMADDPS)
  FMA4(VFMADDPD)
  FMA4(VFMADDSS)
  FMA4(VFMADDSD)
  FMA4(VFMSUBPS)
  FMA4(VFMSUBPD)
  FMA4(VFMSUBSS)
  FMA4(VFMSUBSD)
  FMA4(VFNMADDPS)
  FMA4(VFNMADDPD)
  FMA4(VFNMADDSS)
  FMA4(VFNMADDSD)
  FMA4(VFNMSUBPS)
  FMA4(VFNMSUBPD)
  FMA4(VFNMSUBSS)
  FMA4(VFNMSUBSD)
#undef FMA4
