// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

  // Fences
  void LFENCE();
  void MFENCE();
  void SFENCE();

  // Conditional Operations
  void SETcc(CCFlags flag, OpArg dest);
  // Note: CMOV brings small if any benefit on current CPUs.
  void CMOVcc(int bits, X64Reg dest, OpArg src, CCFlags flag);

  // Data Transfer / Moves
  void MOV(int bits, const OpArg& a1, const OpArg& a2);
  void MOV_sum(int bits, X64Reg dest, const OpArg& a1, const OpArg& a2);
  void XCHG(int bits, const OpArg& a1, const OpArg& a2);
  void XCHG_AHAL();
  void BSWAP(int bits, X64Reg reg);

  // Sign / Zero Extension
  void MOVSX(int dbits, int sbits, X64Reg dest, OpArg src);  // automatically uses MOVSXD if necessary
  void MOVZX(int dbits, int sbits, X64Reg dest, OpArg src);
  void CWD(int bits = 16);
  inline void CDQ() { CWD(32); }
  inline void CQO() { CWD(64); }
  void CBW(int bits = 8);
  inline void CWDE() { CBW(16); }
  inline void CDQE() { CBW(32); }

  // Load Effective Address
  void LEA(int bits, X64Reg dest, OpArg src);

  // Byte-Swap Moves (MOVBE)
  void MOVBE(int bits, X64Reg dest, const OpArg& src);
  void MOVBE(int bits, const OpArg& dest, X64Reg src);
  void LoadAndSwap(int size, X64Reg dst, const OpArg& src, bool sign_extend = false,
                   MovInfo* info = nullptr);
  void SwapAndStore(int size, const OpArg& dst, X64Reg src, MovInfo* info = nullptr);

  // Basic Integer Arithmetic
  void ADD(int bits, const OpArg& a1, const OpArg& a2);
  void ADC(int bits, const OpArg& a1, const OpArg& a2);
  void SUB(int bits, const OpArg& a1, const OpArg& a2);
  void SBB(int bits, const OpArg& a1, const OpArg& a2);
  void NEG(int bits, const OpArg& src);
  void MUL(int bits, const OpArg& src);   // UNSIGNED
  void IMUL(int bits, const OpArg& src);  // SIGNED
  void IMUL(int bits, X64Reg regOp, const OpArg& src);
  void IMUL(int bits, X64Reg regOp, const OpArg& src, const OpArg& imm);
  void DIV(int bits, const OpArg& src);
  void IDIV(int bits, const OpArg& src);

  // Logical Operations
  void AND(int bits, const OpArg& a1, const OpArg& a2);
  void OR(int bits, const OpArg& a1, const OpArg& a2);
  void XOR(int bits, const OpArg& a1, const OpArg& a2);
  void NOT(int bits, const OpArg& src);

  // Bitwise Scan / Count
  void BSF(int bits, X64Reg dest, const OpArg& src);  // Bottom bit to top bit
  void BSR(int bits, X64Reg dest, const OpArg& src);  // Top bit to bottom bit
  void LZCNT(int bits, X64Reg dest, const OpArg& src);
  void TZCNT(int bits, X64Reg dest, const OpArg& src);  // Note: this one is actually part of BMI1

  // Compares and Tests
  void CMP(int bits, const OpArg& a1, const OpArg& a2);
  void TEST(int bits, const OpArg& a1, const OpArg& a2);
  void CMP_or_TEST(int bits, const OpArg& a1, const OpArg& a2);

  // Bit Tests
  void BT(int bits, const OpArg& dest, const OpArg& index);
  void BTS(int bits, const OpArg& dest, const OpArg& index);
  void BTR(int bits, const OpArg& dest, const OpArg& index);
  void BTC(int bits, const OpArg& dest, const OpArg& index);

  // Shifts and Rotates
  void ROL(int bits, const OpArg& dest, const OpArg& shift);
  void ROR(int bits, const OpArg& dest, const OpArg& shift);
  void RCL(int bits, const OpArg& dest, const OpArg& shift);
  void RCR(int bits, const OpArg& dest, const OpArg& shift);
  void SHL(int bits, const OpArg& dest, const OpArg& shift);
  void SHR(int bits, const OpArg& dest, const OpArg& shift);
  void SAR(int bits, const OpArg& dest, const OpArg& shift);
  void SHRD(int bits, const OpArg& dest, const OpArg& src, const OpArg& shift);
  void SHLD(int bits, const OpArg& dest, const OpArg& src, const OpArg& shift);

  // VEX GPR Instructions (BMI1 / BMI2)
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

  // Cache Control
  enum class PrefetchLevel : u8
  {
    NTA = 0,  // Non-temporal (data used once and only once)
    T0 = 1,   // All cache levels
    T1 = 2,   // Levels 2+ (aliased to T0 on AMD)
    T2 = 3,   // Levels 3+ (aliased to T0 on AMD)
  };
  void PREFETCH(PrefetchLevel level, OpArg arg);
  void MOVNTI(int bits, const OpArg& dest, X64Reg src);
