// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// WARNING - THIS LIBRARY IS NOT THREAD SAFE!!!

#pragma once

#include <cstddef>
#include <cstring>
#include <functional>
#include <tuple>
#include <type_traits>

#include "Common/Assert.h"
#include "Common/BitSet.h"
#include "Common/CodeBlock.h"
#include "Common/CommonTypes.h"
#include "Common/x64ABI.h"

#include "Common/x64EmitterTypes.h"

namespace Gen
{
class XEmitter
{
  friend struct OpArg;  // for Write8 etc
private:
  // Pointer to memory where code will be emitted to.
  u8* code = nullptr;

  // Pointer past the end of the memory region we're allowed to emit to.
  // Writes that would reach this memory are refused and will set the m_write_failed flag instead.
  u8* m_code_end = nullptr;

  bool flags_locked = false;

  // Set to true when a write request happens that would write past m_code_end.
  // Must be cleared with SetCodePtr() afterwards.
  bool m_write_failed = false;

  void CheckFlags();

  void Rex(int w, int r, int x, int b);
  void WriteModRM(int mod, int reg, int rm);
  void WriteSIB(int scale, int index, int base);
  void WriteSimple1Byte(int bits, u8 byte, X64Reg reg);
  void WriteSimple2Byte(int bits, u8 byte1, u8 byte2, X64Reg reg);
  void WriteMulDivType(int bits, OpArg src, int ext);
  void WriteBitSearchType(int bits, X64Reg dest, OpArg src, u8 byte2, bool rep = false);
  void WriteShift(int bits, OpArg dest, const OpArg& shift, int ext);
  void WriteBitTest(int bits, const OpArg& dest, const OpArg& index, int ext);
  void WriteMXCSR(OpArg arg, int ext);
  void WriteSSEOp(u8 opPrefix, u16 op, X64Reg regOp, OpArg arg, int extrabytes = 0);
  void WriteSSSE3Op(u8 opPrefix, u16 op, X64Reg regOp, const OpArg& arg, int extrabytes = 0);
  void WriteSSE41Op(u8 opPrefix, u16 op, X64Reg regOp, const OpArg& arg, int extrabytes = 0);
  void WriteVEXOp(u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg, int W = 0,
                  int extrabytes = 0);
  void WriteVEXOp4(u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg,
                   X64Reg regOp3, int W = 0);
  void WriteAVXOp(u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg, int W = 0,
                  int extrabytes = 0);
  void WriteAVXOp4(u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg,
                   X64Reg regOp3, int W = 0);
  void WriteFMA3Op(u8 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg, int W = 0);
  void WriteFMA4Op(u8 op, X64Reg dest, X64Reg regOp1, X64Reg regOp2, const OpArg& arg, int W = 0);
  void WriteBMIOp(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg,
                  int extrabytes = 0);
  void WriteBMI1Op(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg,
                   int extrabytes = 0);
  void WriteBMI2Op(int size, u8 opPrefix, u16 op, X64Reg regOp1, X64Reg regOp2, const OpArg& arg,
                   int extrabytes = 0);
  void WriteMOVBE(int bits, u8 op, X64Reg regOp, const OpArg& arg);
  void WriteNormalOp(int bits, NormalOp op, const OpArg& a1, const OpArg& a2);

  void ABI_CalculateFrameSize(BitSet32 mask, size_t rsp_alignment, size_t needed_frame_size,
                              size_t* shadowp, size_t* subtractionp, size_t* xmm_offsetp);

protected:
  void Write8(u8 value);
  void Write16(u16 value);
  void Write32(u32 value);
  void Write64(u64 value);

public:
  XEmitter() = default;
  explicit XEmitter(u8* code_ptr, u8* code_end) : code(code_ptr), m_code_end(code_end) {}
  virtual ~XEmitter() = default;
  void SetCodePtr(u8* ptr, u8* end, bool write_failed = false);
  void ReserveCodeSpace(int bytes);
  u8* AlignCodeTo(size_t alignment);
  u8* AlignCode4();
  u8* AlignCode16();
  u8* AlignCodePage();
  const u8* GetCodePtr() const { return code; }
  u8* GetWritableCodePtr() { return code; }
  const u8* GetCodeEnd() const { return m_code_end; }
  u8* GetWritableCodeEnd() { return m_code_end; }

  void LockFlags() { flags_locked = true; }
  void UnlockFlags() { flags_locked = false; }

  // Should be checked after a block of code has been generated to see if the code has been
  // successfully written to memory. Do not call the generated code when this returns true!
  bool HasWriteFailed() const { return m_write_failed; }

  // Looking for one of these? It's BANNED!! Some instructions are slow on modern CPU
  // INC, DEC, LOOP, LOOPNE, LOOPE, ENTER, LEAVE, XCHG, XLAT, REP MOVSB/MOVSD, REP SCASD + other
  // string instr.,
  // INC and DEC are slow on Intel Core, but not on AMD. They create a
  // false flag dependency because they only update a subset of the flags.
  // XCHG is SLOW and should be avoided.

  // Debug breakpoint
  void INT3();

  // Do nothing
  void NOP(size_t count = 1);

  // Save energy in wait-loops on P4 only. Probably not too useful.
  void PAUSE();

  // Read Time-Stamp Counter
  void RDTSC();

  // Flag control
  void STC();
  void CLC();
  void CMC();

  // These two can not be executed in 64-bit mode on early Intel 64-bit CPU:s, only on Core2 and
  // AMD!
  void LAHF();  // 3 cycle vector path
  void SAHF();  // direct path fast

  // Stack control
  void PUSH(X64Reg reg);
  void POP(X64Reg reg);
  void PUSH(int bits, const OpArg& reg);
  void POP(int bits, const OpArg& reg);
  void PUSHF();
  void POPF();

  enum class Jump
  {
    Short,
    Near,
  };
  static const int SHORT_JMP_LEN = 2;
  static const int NEAR_JMP_LEN = 5;

  // Flow control
  void RET();
  void RET_FAST();
  void UD2();
  [[nodiscard]] FixupBranch J(Jump jump = Jump::Short);

  void JMP(const u8* addr, bool force_near_padding = false);
  void JMPptr(const OpArg& arg);
  void JMPself();  // infinite loop!
#ifdef CALL
#undef CALL
#endif
  void CALL(const void* fnptr);
  [[nodiscard]] FixupBranch CALL();
  void CALLptr(OpArg arg);

  [[nodiscard]] FixupBranch J_CC(CCFlags conditionCode, Jump jump = Jump::Short);
  void J_CC(CCFlags conditionCode, const u8* addr);

  void SetJumpTarget(const FixupBranch& branch);


  #include "Common/x64EmitterGPR.h"
  #include "Common/x64EmitterSSE.h"
  #include "Common/x64EmitterAVX.h"


  // Prefixes
  void LOCK();
  void REP();
  void REPNE();
  void FSOverride();
  void GSOverride();



  // Utility functions
  // The difference between this and CALL is that this aligns the stack
  // where appropriate.
  template <typename FunctionPointer>
  void ABI_CallFunction(FunctionPointer func)
  {
    static_assert(std::is_pointer<FunctionPointer>() &&
                      std::is_function<std::remove_pointer_t<FunctionPointer>>(),
                  "Supplied type must be a function pointer.");

    const void* ptr = reinterpret_cast<const void*>(func);
    const u64 address = reinterpret_cast<u64>(ptr);
    const u64 distance = address - (reinterpret_cast<u64>(code) + 5);

    if (distance >= 0x0000000080000000ULL && distance < 0xFFFFFFFF80000000ULL)
    {
      // Far call
      MOV(64, R(RAX), Imm64(address));
      CALLptr(R(RAX));
    }
    else
    {
      CALL(ptr);
    }
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionC16(FunctionPointer func, u16 param1)
  {
    MOV(32, R(ABI_PARAM1), Imm32(param1));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionCC16(FunctionPointer func, u32 param1, u16 param2)
  {
    MOV(32, R(ABI_PARAM1), Imm32(param1));
    MOV(32, R(ABI_PARAM2), Imm32(param2));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionC(FunctionPointer func, u32 param1)
  {
    MOV(32, R(ABI_PARAM1), Imm32(param1));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionCC(FunctionPointer func, u32 param1, u32 param2)
  {
    MOV(32, R(ABI_PARAM1), Imm32(param1));
    MOV(32, R(ABI_PARAM2), Imm32(param2));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionCP(FunctionPointer func, u32 param1, const void* param2)
  {
    MOV(32, R(ABI_PARAM1), Imm32(param1));
    MOV(64, R(ABI_PARAM2), Imm64(reinterpret_cast<u64>(param2)));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionCCC(FunctionPointer func, u32 param1, u32 param2, u32 param3)
  {
    MOV(32, R(ABI_PARAM1), Imm32(param1));
    MOV(32, R(ABI_PARAM2), Imm32(param2));
    MOV(32, R(ABI_PARAM3), Imm32(param3));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionCCP(FunctionPointer func, u32 param1, u32 param2, const void* param3)
  {
    MOV(32, R(ABI_PARAM1), Imm32(param1));
    MOV(32, R(ABI_PARAM2), Imm32(param2));
    MOV(64, R(ABI_PARAM3), Imm64(reinterpret_cast<u64>(param3)));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionCCCP(FunctionPointer func, u32 param1, u32 param2, u32 param3,
                            const void* param4)
  {
    MOV(32, R(ABI_PARAM1), Imm32(param1));
    MOV(32, R(ABI_PARAM2), Imm32(param2));
    MOV(32, R(ABI_PARAM3), Imm32(param3));
    MOV(64, R(ABI_PARAM4), Imm64(reinterpret_cast<u64>(param4)));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionP(FunctionPointer func, const void* param1)
  {
    MOV(64, R(ABI_PARAM1), Imm64(reinterpret_cast<u64>(param1)));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionPP(FunctionPointer func, const void* param1, const void* param2)
  {
    MOV(64, R(ABI_PARAM1), Imm64(reinterpret_cast<u64>(param1)));
    MOV(64, R(ABI_PARAM2), Imm64(reinterpret_cast<u64>(param2)));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionPC(FunctionPointer func, const void* param1, u32 param2)
  {
    MOV(64, R(ABI_PARAM1), Imm64(reinterpret_cast<u64>(param1)));
    MOV(32, R(ABI_PARAM2), Imm32(param2));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionPPC(FunctionPointer func, const void* param1, const void* param2, u32 param3)
  {
    MOV(64, R(ABI_PARAM1), Imm64(reinterpret_cast<u64>(param1)));
    MOV(64, R(ABI_PARAM2), Imm64(reinterpret_cast<u64>(param2)));
    MOV(32, R(ABI_PARAM3), Imm32(param3));
    ABI_CallFunction(func);
  }

  // Pass a register as a parameter.
  template <typename FunctionPointer>
  void ABI_CallFunctionR(FunctionPointer func, X64Reg reg1)
  {
    if (reg1 != ABI_PARAM1)
      MOV(32, R(ABI_PARAM1), R(reg1));
    ABI_CallFunction(func);
  }

  // Pass a pointer and register as a parameter.
  template <typename FunctionPointer>
  void ABI_CallFunctionPR(FunctionPointer func, const void* ptr, X64Reg reg1)
  {
    if (reg1 != ABI_PARAM2)
      MOV(64, R(ABI_PARAM2), R(reg1));
    MOV(64, R(ABI_PARAM1), Imm64(reinterpret_cast<u64>(ptr)));
    ABI_CallFunction(func);
  }

  // Pass two registers as parameters.
  template <typename FunctionPointer>
  void ABI_CallFunctionRR(FunctionPointer func, X64Reg reg1, X64Reg reg2)
  {
    MOVTwo(64, ABI_PARAM1, reg1, 0, ABI_PARAM2, reg2);
    ABI_CallFunction(func);
  }

  // Pass a pointer and two registers as parameters.
  template <typename FunctionPointer>
  void ABI_CallFunctionPRR(FunctionPointer func, const void* ptr, X64Reg reg1, X64Reg reg2)
  {
    MOVTwo(64, ABI_PARAM2, reg1, 0, ABI_PARAM3, reg2);
    MOV(64, R(ABI_PARAM1), Imm64(reinterpret_cast<u64>(ptr)));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionAC(int bits, FunctionPointer func, const Gen::OpArg& arg1, u32 param2)
  {
    if (!arg1.IsSimpleReg(ABI_PARAM1))
      MOV(bits, R(ABI_PARAM1), arg1);
    MOV(32, R(ABI_PARAM2), Imm32(param2));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionPAC(int bits, FunctionPointer func, const void* ptr1, const Gen::OpArg& arg2,
                           u32 param3)
  {
    if (!arg2.IsSimpleReg(ABI_PARAM2))
      MOV(bits, R(ABI_PARAM2), arg2);
    MOV(32, R(ABI_PARAM3), Imm32(param3));
    MOV(64, R(ABI_PARAM1), Imm64(reinterpret_cast<u64>(ptr1)));
    ABI_CallFunction(func);
  }

  template <typename FunctionPointer>
  void ABI_CallFunctionA(int bits, FunctionPointer func, const Gen::OpArg& arg1)
  {
    if (!arg1.IsSimpleReg(ABI_PARAM1))
      MOV(bits, R(ABI_PARAM1), arg1);
    ABI_CallFunction(func);
  }

  // Helper method for ABI functions related to calling functions. May be used by itself as well.
  void MOVTwo(int bits, X64Reg dst1, X64Reg src1, s32 offset, X64Reg dst2, X64Reg src2);

  // Saves/restores the registers and adjusts the stack to be aligned as
  // required by the ABI, where the previous alignment was as specified.
  // Push returns the size of the shadow space, i.e. the offset of the frame.
  size_t ABI_PushRegistersAndAdjustStack(BitSet32 mask, size_t rsp_alignment,
                                         size_t needed_frame_size = 0);
  void ABI_PopRegistersAndAdjustStack(BitSet32 mask, size_t rsp_alignment,
                                      size_t needed_frame_size = 0);

  // Utility to generate a call to a std::function object.
  //
  // Unfortunately, calling operator() directly is undefined behavior in C++
  // (this method might be a thunk in the case of multi-inheritance) so we
  // have to go through a trampoline function.
  template <typename T, typename... Args>
  static T CallLambdaTrampoline(const std::function<T(Args...)>* f, Args... args)
  {
    return (*f)(args...);
  }

  template <typename T, typename... Args>
  void ABI_CallLambdaPC(const std::function<T(Args...)>* f, void* p1, u32 p2)
  {
    auto trampoline = &XEmitter::CallLambdaTrampoline<T, Args...>;
    ABI_CallFunctionPPC(trampoline, reinterpret_cast<const void*>(f), p1, p2);
  }
};  // class XEmitter

class X64CodeBlock : public Common::CodeBlock<XEmitter>
{
private:
  void PoisonMemory() override
  {
    // x86/64: 0xCC = breakpoint
    memset(region, 0xCC, region_size);
  }
};

}  // namespace Gen
