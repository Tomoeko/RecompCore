// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

  void SETcc(CCFlags flag, OpArg dest);
  // Note: CMOV brings small if any benefit on current CPUs.
  void CMOVcc(int bits, X64Reg dest, OpArg src, CCFlags flag);

  // Fences
  void LFENCE();
  void MFENCE();
  void SFENCE();

  // Bit scan
  void BSF(int bits, X64Reg dest, const OpArg& src);  // Bottom bit to top bit
  void BSR(int bits, X64Reg dest, const OpArg& src);  // Top bit to bottom bit

  // Cache control
  enum class PrefetchLevel : u8
  {
    NTA = 0,  // Non-temporal (data used once and only once)
    T0 = 1,   // All cache levels
    T1 = 2,   // Levels 2+ (aliased to T0 on AMD)
    T2 = 3,   // Levels 3+ (aliased to T0 on AMD)
  };
  void PREFETCH(PrefetchLevel level, OpArg arg);
  void MOVNTI(int bits, const OpArg& dest, X64Reg src);
  void MOVNTDQ(const OpArg& arg, X64Reg regOp);
  void MOVNTPS(const OpArg& arg, X64Reg regOp);
  void MOVNTPD(const OpArg& arg, X64Reg regOp);

  // Multiplication / division
  void MUL(int bits, const OpArg& src);   // UNSIGNED
  void IMUL(int bits, const OpArg& src);  // SIGNED
  void IMUL(int bits, X64Reg regOp, const OpArg& src);
  void IMUL(int bits, X64Reg regOp, const OpArg& src, const OpArg& imm);
  void DIV(int bits, const OpArg& src);
  void IDIV(int bits, const OpArg& src);

  // Shift
  void ROL(int bits, const OpArg& dest, const OpArg& shift);
  void ROR(int bits, const OpArg& dest, const OpArg& shift);
  void RCL(int bits, const OpArg& dest, const OpArg& shift);
  void RCR(int bits, const OpArg& dest, const OpArg& shift);
  void SHL(int bits, const OpArg& dest, const OpArg& shift);
  void SHR(int bits, const OpArg& dest, const OpArg& shift);
  void SAR(int bits, const OpArg& dest, const OpArg& shift);

  // Bit Test
  void BT(int bits, const OpArg& dest, const OpArg& index);
  void BTS(int bits, const OpArg& dest, const OpArg& index);
  void BTR(int bits, const OpArg& dest, const OpArg& index);
  void BTC(int bits, const OpArg& dest, const OpArg& index);

  // Double-Precision Shift
  void SHRD(int bits, const OpArg& dest, const OpArg& src, const OpArg& shift);
  void SHLD(int bits, const OpArg& dest, const OpArg& src, const OpArg& shift);

  // Extend EAX into EDX in various ways
  void CWD(int bits = 16);
  inline void CDQ() { CWD(32); }
  inline void CQO() { CWD(64); }
  void CBW(int bits = 8);
  inline void CWDE() { CBW(16); }
  inline void CDQE() { CBW(32); }
  // Load effective address
  void LEA(int bits, X64Reg dest, OpArg src);

  // Integer arithmetic
  void NEG(int bits, const OpArg& src);
  void ADD(int bits, const OpArg& a1, const OpArg& a2);
  void ADC(int bits, const OpArg& a1, const OpArg& a2);
  void SUB(int bits, const OpArg& a1, const OpArg& a2);
  void SBB(int bits, const OpArg& a1, const OpArg& a2);
  void AND(int bits, const OpArg& a1, const OpArg& a2);
  void CMP(int bits, const OpArg& a1, const OpArg& a2);

  // Bit operations
  void NOT(int bits, const OpArg& src);
  void OR(int bits, const OpArg& a1, const OpArg& a2);
  void XOR(int bits, const OpArg& a1, const OpArg& a2);
  void MOV(int bits, const OpArg& a1, const OpArg& a2);
  void TEST(int bits, const OpArg& a1, const OpArg& a2);

  void CMP_or_TEST(int bits, const OpArg& a1, const OpArg& a2);
  void MOV_sum(int bits, X64Reg dest, const OpArg& a1, const OpArg& a2);

  // Are these useful at all? Consider removing.
  void XCHG(int bits, const OpArg& a1, const OpArg& a2);
  void XCHG_AHAL();

  // Byte swapping (32 and 64-bit only).
  void BSWAP(int bits, X64Reg reg);

  // Sign/zero extension
  void MOVSX(int dbits, int sbits, X64Reg dest,
             OpArg src);  // automatically uses MOVSXD if necessary
  void MOVZX(int dbits, int sbits, X64Reg dest, OpArg src);

  // Available only on Atom or >= Haswell so far. Test with cpu_info.bMOVBE.
  void MOVBE(int bits, X64Reg dest, const OpArg& src);
  void MOVBE(int bits, const OpArg& dest, X64Reg src);
  void LoadAndSwap(int size, X64Reg dst, const OpArg& src, bool sign_extend = false,
                   MovInfo* info = nullptr);
  void SwapAndStore(int size, const OpArg& dst, X64Reg src, MovInfo* info = nullptr);

  // Available only on AMD >= Phenom or Intel >= Haswell
  void LZCNT(int bits, X64Reg dest, const OpArg& src);
  // Note: this one is actually part of BMI1
  void TZCNT(int bits, X64Reg dest, const OpArg& src);

  // WARNING - These two take 11-13 cycles and are VectorPath! (AMD64)
  void STMXCSR(const OpArg& memloc);
  void LDMXCSR(const OpArg& memloc);
  // VEX GPR instructions
  void SARX(int bits, X64Reg regOp1, const OpArg& arg, X64Reg regOp2);
  void SHLX(int bits, X64Reg regOp1, const OpArg& arg, X64Reg regOp2);
  void SHRX(int bits, X64Reg regOp1, const OpArg& arg, X64Reg regOp2);
  void RORX(int bits, X64Reg regOp, const OpArg& arg, u8 rotate);
  void PEXT(int bits, X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void PDEP(int bits, X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void MULX(int bits, X64Reg regOp1, X64Reg regOp2, const OpArg& arg);
  void BZHI(int bits, X64Reg regOp1, const OpArg& arg, X64Reg regOp2);
  void BLSR(int bits, X64Reg regOp, const OpArg& arg);
  void BLSMSK(int bits, X64Reg regOp, const OpArg& arg);
  void BLSI(int bits, X64Reg regOp, const OpArg& arg);
  void BEXTR(int bits, X64Reg regOp1, const OpArg& arg, X64Reg regOp2);
  void ANDN(int bits, X64Reg regOp1, X64Reg regOp2, const OpArg& arg);

