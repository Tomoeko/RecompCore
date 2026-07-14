// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

  void ADDSS(X64Reg regOp, const OpArg& arg);
  void ADDSD(X64Reg regOp, const OpArg& arg);
  void SUBSS(X64Reg regOp, const OpArg& arg);
  void SUBSD(X64Reg regOp, const OpArg& arg);
  void MULSS(X64Reg regOp, const OpArg& arg);
  void MULSD(X64Reg regOp, const OpArg& arg);
  void DIVSS(X64Reg regOp, const OpArg& arg);
  void DIVSD(X64Reg regOp, const OpArg& arg);
  void MINSS(X64Reg regOp, const OpArg& arg);
  void MINSD(X64Reg regOp, const OpArg& arg);
  void MAXSS(X64Reg regOp, const OpArg& arg);
  void MAXSD(X64Reg regOp, const OpArg& arg);
  void SQRTSS(X64Reg regOp, const OpArg& arg);
  void SQRTSD(X64Reg regOp, const OpArg& arg);
  void RCPSS(X64Reg regOp, const OpArg& arg);
  void RSQRTSS(X64Reg regOp, const OpArg& arg);

  // SSE/SSE2: Floating point bitwise (yes)
  void CMPSS(X64Reg regOp, const OpArg& arg, u8 compare);
  void CMPSD(X64Reg regOp, const OpArg& arg, u8 compare);

  // SSE/SSE2: Floating point packed arithmetic (x4 for float, x2 for double)
  void ADDPS(X64Reg regOp, const OpArg& arg);
  void ADDPD(X64Reg regOp, const OpArg& arg);
  void SUBPS(X64Reg regOp, const OpArg& arg);
  void SUBPD(X64Reg regOp, const OpArg& arg);
  void CMPPS(X64Reg regOp, const OpArg& arg, u8 compare);
  void CMPPD(X64Reg regOp, const OpArg& arg, u8 compare);
  void MULPS(X64Reg regOp, const OpArg& arg);
  void MULPD(X64Reg regOp, const OpArg& arg);
  void DIVPS(X64Reg regOp, const OpArg& arg);
  void DIVPD(X64Reg regOp, const OpArg& arg);
  void MINPS(X64Reg regOp, const OpArg& arg);
  void MINPD(X64Reg regOp, const OpArg& arg);
  void MAXPS(X64Reg regOp, const OpArg& arg);
  void MAXPD(X64Reg regOp, const OpArg& arg);
  void SQRTPS(X64Reg regOp, const OpArg& arg);
  void SQRTPD(X64Reg regOp, const OpArg& arg);
  void RCPPS(X64Reg regOp, const OpArg& arg);
  void RSQRTPS(X64Reg regOp, const OpArg& arg);

  // SSE/SSE2: Floating point packed bitwise (x4 for float, x2 for double)
  void ANDPS(X64Reg regOp, const OpArg& arg);
  void ANDPD(X64Reg regOp, const OpArg& arg);
  void ANDNPS(X64Reg regOp, const OpArg& arg);
  void ANDNPD(X64Reg regOp, const OpArg& arg);
  void ORPS(X64Reg regOp, const OpArg& arg);
  void ORPD(X64Reg regOp, const OpArg& arg);
  void XORPS(X64Reg regOp, const OpArg& arg);
  void XORPD(X64Reg regOp, const OpArg& arg);

  // SSE/SSE2: Shuffle components. These are tricky - see Intel documentation.
  void SHUFPS(X64Reg regOp, const OpArg& arg, u8 shuffle);
  void SHUFPD(X64Reg regOp, const OpArg& arg, u8 shuffle);

  // SSE3
  void MOVSLDUP(X64Reg regOp, const OpArg& arg);
  void MOVSHDUP(X64Reg regOp, const OpArg& arg);
  void MOVDDUP(X64Reg regOp, const OpArg& arg);

  // SSE/SSE2: Useful alternative to shuffle in some cases.
  void UNPCKLPS(X64Reg dest, const OpArg& src);
  void UNPCKHPS(X64Reg dest, const OpArg& src);
  void UNPCKLPD(X64Reg dest, const OpArg& src);
  void UNPCKHPD(X64Reg dest, const OpArg& src);

  // SSE/SSE2: Compares.
  void COMISS(X64Reg regOp, const OpArg& arg);
  void COMISD(X64Reg regOp, const OpArg& arg);
  void UCOMISS(X64Reg regOp, const OpArg& arg);
  void UCOMISD(X64Reg regOp, const OpArg& arg);

  // SSE/SSE2: Moves. Use the right data type for your data, in most cases.
  void MOVAPS(X64Reg regOp, const OpArg& arg);
  void MOVAPD(X64Reg regOp, const OpArg& arg);
  void MOVAPS(const OpArg& arg, X64Reg regOp);
  void MOVAPD(const OpArg& arg, X64Reg regOp);

  void MOVUPS(X64Reg regOp, const OpArg& arg);
  void MOVUPD(X64Reg regOp, const OpArg& arg);
  void MOVUPS(const OpArg& arg, X64Reg regOp);
  void MOVUPD(const OpArg& arg, X64Reg regOp);

  void MOVDQA(X64Reg regOp, const OpArg& arg);
  void MOVDQA(const OpArg& arg, X64Reg regOp);
  void MOVDQU(X64Reg regOp, const OpArg& arg);
  void MOVDQU(const OpArg& arg, X64Reg regOp);

  void MOVSS(X64Reg regOp, const OpArg& arg);
  void MOVSD(X64Reg regOp, const OpArg& arg);
  void MOVSS(const OpArg& arg, X64Reg regOp);
  void MOVSD(const OpArg& arg, X64Reg regOp);

  void MOVLPS(X64Reg regOp, const OpArg& arg);
  void MOVLPD(X64Reg regOp, const OpArg& arg);
  void MOVLPS(const OpArg& arg, X64Reg regOp);
  void MOVLPD(const OpArg& arg, X64Reg regOp);

  void MOVHPS(X64Reg regOp, const OpArg& arg);
  void MOVHPD(X64Reg regOp, const OpArg& arg);
  void MOVHPS(const OpArg& arg, X64Reg regOp);
  void MOVHPD(const OpArg& arg, X64Reg regOp);

  void MOVHLPS(X64Reg regOp1, X64Reg regOp2);
  void MOVLHPS(X64Reg regOp1, X64Reg regOp2);

  // Be careful when using these overloads for reg <--> xmm moves.
  // The one you cast to OpArg with R(reg) is the x86 reg, the other
  // one is the xmm reg.
  // ie: "MOVD_xmm(eax, R(xmm1))" generates incorrect code (movd xmm0, rcx)
  //     use "MOVD_xmm(R(eax), xmm1)" instead.
  void MOVD_xmm(X64Reg dest, const OpArg& arg);
  void MOVQ_xmm(X64Reg dest, OpArg arg);
  void MOVD_xmm(const OpArg& arg, X64Reg src);
  void MOVQ_xmm(OpArg arg, X64Reg src);

  // SSE/SSE2: Generates a mask from the high bits of the components of the packed register in
  // question.
  void MOVMSKPS(X64Reg dest, const OpArg& arg);
  void MOVMSKPD(X64Reg dest, const OpArg& arg);

  // SSE2: Selective byte store, mask in src register. EDI/RDI specifies store address. This is a
  // weird one.
  void MASKMOVDQU(X64Reg dest, X64Reg src);
  void LDDQU(X64Reg dest, const OpArg& src);

  // SSE/SSE2: Data type conversions.
  void CVTPS2PD(X64Reg dest, const OpArg& src);
  void CVTPD2PS(X64Reg dest, const OpArg& src);
  void CVTSS2SD(X64Reg dest, const OpArg& src);
  void CVTSI2SS(X64Reg dest, const OpArg& src);
  void CVTSD2SS(X64Reg dest, const OpArg& src);
  void CVTSI2SD(X64Reg dest, const OpArg& src);
  void CVTDQ2PD(X64Reg regOp, const OpArg& arg);
  void CVTPD2DQ(X64Reg regOp, const OpArg& arg);
  void CVTDQ2PS(X64Reg regOp, const OpArg& arg);
  void CVTPS2DQ(X64Reg regOp, const OpArg& arg);

  void CVTTPS2DQ(X64Reg regOp, const OpArg& arg);
  void CVTTPD2DQ(X64Reg regOp, const OpArg& arg);

  // Destinations are X64 regs (rax, rbx, ...) for these instructions.
  void CVTSS2SI(X64Reg xregdest, const OpArg& src);
  void CVTSD2SI(X64Reg xregdest, const OpArg& src);
  void CVTTSS2SI(X64Reg xregdest, const OpArg& arg);
  void CVTTSD2SI(X64Reg xregdest, const OpArg& arg);

  // SSE2: Packed integer instructions
  void PACKSSDW(X64Reg dest, const OpArg& arg);
  void PACKSSWB(X64Reg dest, const OpArg& arg);
  void PACKUSDW(X64Reg dest, const OpArg& arg);
  void PACKUSWB(X64Reg dest, const OpArg& arg);

  void PUNPCKLBW(X64Reg dest, const OpArg& arg);
  void PUNPCKLWD(X64Reg dest, const OpArg& arg);
  void PUNPCKLDQ(X64Reg dest, const OpArg& arg);
  void PUNPCKLQDQ(X64Reg dest, const OpArg& arg);

  void PTEST(X64Reg dest, const OpArg& arg);
  void PAND(X64Reg dest, const OpArg& arg);
  void PANDN(X64Reg dest, const OpArg& arg);
  void PXOR(X64Reg dest, const OpArg& arg);
  void POR(X64Reg dest, const OpArg& arg);

  void PADDB(X64Reg dest, const OpArg& arg);
  void PADDW(X64Reg dest, const OpArg& arg);
  void PADDD(X64Reg dest, const OpArg& arg);
  void PADDQ(X64Reg dest, const OpArg& arg);

  void PADDSB(X64Reg dest, const OpArg& arg);
  void PADDSW(X64Reg dest, const OpArg& arg);
  void PADDUSB(X64Reg dest, const OpArg& arg);
  void PADDUSW(X64Reg dest, const OpArg& arg);

  void PSUBB(X64Reg dest, const OpArg& arg);
  void PSUBW(X64Reg dest, const OpArg& arg);
  void PSUBD(X64Reg dest, const OpArg& arg);
  void PSUBQ(X64Reg dest, const OpArg& arg);

  void PSUBSB(X64Reg dest, const OpArg& arg);
  void PSUBSW(X64Reg dest, const OpArg& arg);
  void PSUBUSB(X64Reg dest, const OpArg& arg);
  void PSUBUSW(X64Reg dest, const OpArg& arg);

  void PAVGB(X64Reg dest, const OpArg& arg);
  void PAVGW(X64Reg dest, const OpArg& arg);

  void PCMPEQB(X64Reg dest, const OpArg& arg);
  void PCMPEQW(X64Reg dest, const OpArg& arg);
  void PCMPEQD(X64Reg dest, const OpArg& arg);

  void PCMPGTB(X64Reg dest, const OpArg& arg);
  void PCMPGTW(X64Reg dest, const OpArg& arg);
  void PCMPGTD(X64Reg dest, const OpArg& arg);

  void PEXTRW(X64Reg dest, const OpArg& arg, u8 subreg);
  void PINSRW(X64Reg dest, const OpArg& arg, u8 subreg);
  void PINSRD(X64Reg dest, const OpArg& arg, u8 subreg);

  void PMADDWD(X64Reg dest, const OpArg& arg);
  void PSADBW(X64Reg dest, const OpArg& arg);

  void PMAXSW(X64Reg dest, const OpArg& arg);
  void PMAXUB(X64Reg dest, const OpArg& arg);
  void PMINSW(X64Reg dest, const OpArg& arg);
  void PMINUB(X64Reg dest, const OpArg& arg);

  void PMOVMSKB(X64Reg dest, const OpArg& arg);
  void PSHUFD(X64Reg dest, const OpArg& arg, u8 shuffle);
  void PSHUFB(X64Reg dest, const OpArg& arg);

  void PSHUFLW(X64Reg dest, const OpArg& arg, u8 shuffle);
  void PSHUFHW(X64Reg dest, const OpArg& arg, u8 shuffle);

  void PSRLW(X64Reg reg, u8 shift);
  void PSRLD(X64Reg reg, u8 shift);
  void PSRLQ(X64Reg reg, u8 shift);
  void PSRLQ(X64Reg reg, const OpArg& arg);
  void PSRLDQ(X64Reg reg, u8 shift);

  void PSLLW(X64Reg reg, u8 shift);
  void PSLLD(X64Reg reg, u8 shift);
  void PSLLQ(X64Reg reg, u8 shift);
  void PSLLDQ(X64Reg reg, u8 shift);

  void PSRAW(X64Reg reg, u8 shift);
  void PSRAD(X64Reg reg, u8 shift);

  // SSE4: data type conversions
  void PMOVSXBW(X64Reg dest, const OpArg& arg);
  void PMOVSXBD(X64Reg dest, const OpArg& arg);
  void PMOVSXBQ(X64Reg dest, const OpArg& arg);
  void PMOVSXWD(X64Reg dest, const OpArg& arg);
  void PMOVSXWQ(X64Reg dest, const OpArg& arg);
  void PMOVSXDQ(X64Reg dest, const OpArg& arg);
  void PMOVZXBW(X64Reg dest, const OpArg& arg);
  void PMOVZXBD(X64Reg dest, const OpArg& arg);
  void PMOVZXBQ(X64Reg dest, const OpArg& arg);
  void PMOVZXWD(X64Reg dest, const OpArg& arg);
  void PMOVZXWQ(X64Reg dest, const OpArg& arg);
  void PMOVZXDQ(X64Reg dest, const OpArg& arg);

  // SSE4: blend instructions
  void PBLENDVB(X64Reg dest, const OpArg& arg);
  void BLENDVPS(X64Reg dest, const OpArg& arg);
  void BLENDVPD(X64Reg dest, const OpArg& arg);
  void BLENDPS(X64Reg dest, const OpArg& arg, u8 blend);
  void BLENDPD(X64Reg dest, const OpArg& arg, u8 blend);

  // SSE4: compare instructions
  void PCMPEQQ(X64Reg dest, const OpArg& arg);

