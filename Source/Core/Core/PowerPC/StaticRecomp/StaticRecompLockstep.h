// RecompCore: StaticRecomp lockstep differential hook.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shared between the StaticRecomp core and the MMU write funnel. The lockstep
// harness re-runs each native basic block on Dolphin's interpreter and compares
// register/memory results. During that shadow re-run it installs the sink
// below so the interpreter's MMIO / gather-pipe writes are captured but NOT
// committed: Dolphin already performed those hardware side effects for the
// native run, and re-issuing them (a second GX FIFO burst, a second PI write)
// would corrupt the machine. RAM/L1 writes are left to commit normally; the
// harness restores RAM around the shadow. The pointer is null except while a
// shadow re-run is in progress, so the normal interpreter/JIT paths pay only a
// single predictable not-taken branch (and module-less runs never set it, so
// the game-agnostic invariant is untouched).

#pragma once

#include "Common/CommonTypes.h"

namespace StaticRecompLockstep
{
// physical_address is post-translation; data is the raw store value; size in
// bytes. Installed/cleared by StaticRecompCore around interpreter single-steps.
using HwWriteSink = void (*)(u32 physical_address, u32 data, u32 size, void* user);

extern HwWriteSink g_hw_write_sink;
extern void* g_hw_write_sink_user;

// Read replay: native reads each hardware register once; the shadow re-reading
// live hardware would drift (a changed status/counter) and double-fire read
// side effects. While a shadow is active MMU::ReadFromHardware returns the value
// native observed (recorded in program order) instead of touching hardware.
using HwReadSink = u32 (*)(u32 physical_address, u32 size, void* user);
extern HwReadSink g_hw_read_sink;
extern void* g_hw_read_sink_user;

// Timebase pinning: native's mftb returns a cached per-burst ctx->timebase, so
// the shadow interpreter's live GetFakeTimeBase() would drift by the cycles
// elapsed since the burst start. While a shadow is active the interpreter reads
// this pinned value instead, feeding both engines identical timebase inputs.
extern bool g_tb_override_active;
extern u64 g_tb_override_value;

// Shadow guest-RAM (MEM1) write journal: the shadow re-run commits its RAM
// stores onto the block's restored pre-image so the write comparison can read
// them, but those stores must NOT persist — the canonical run is native, and a
// leaked shadow store would corrupt guest RAM for the rest of the session (e.g.
// a later strcmp reading a differently-written buffer). While a shadow is active
// MMU records each first-touched MEM1 offset here so StaticRecompCore can undo
// every shadow store afterward. Called with the physical MEM1 offset + size,
// BEFORE the store commits. Null outside a shadow (module-less runs never set
// it), so the game-agnostic invariant is untouched.
using RamWriteJournal = void (*)(u32 ram_offset, u32 size, void* user);
extern RamWriteJournal g_ram_write_journal;
extern void* g_ram_write_journal_user;

// Shadow locked-cache (L1Cache, 0xE0000000) write journal. Locked cache is
// memory, not a hardware side effect, so — like MEM1 — the shadow re-run's LC
// stores commit normally (intra-block read-modify-write must see them), but they
// must NOT persist: the canonical run is native, whose LC values were committed
// before the shadow ran. A leaked shadow LC store would corrupt the locked cache
// for the rest of the session (e.g. a later THP-decode block reading a
// differently-written row). While a shadow is active the MMU records each
// first-touched L1Cache offset here so StaticRecompCore can undo every shadow LC
// store afterward. Called with the L1Cache byte offset (em_address & 0x0FFFFFFF)
// + size, BEFORE the store commits. Null outside a shadow (module-less runs never
// set it), so the game-agnostic invariant is untouched.
using LcWriteJournal = void (*)(u32 lc_offset, u32 size, void* user);
extern LcWriteJournal g_lc_write_journal;
extern void* g_lc_write_journal_user;

// Shadow Fake-VMEM write journal. Dolphin maps the guest virtual-memory window
// [0x7E000000, 0x80000000) — which Strikers demand-allocates via nlMemory VMAlloc
// and drives through BAT translation — into a SEPARATE Fake-VMEM buffer, not MEM1
// (MMU.cpp WriteToHardware's FakeVMEM branch). Those writes are memory, not a
// hardware side effect, so — like MEM1 and locked cache — the shadow re-run's
// Fake-VMEM stores commit normally (intra-block read-modify-write must see them)
// but must NOT persist: the canonical run is native, whose Fake-VMEM values were
// committed before the shadow ran. A leaked shadow store would corrupt the window
// for the rest of the session, and without journaling native's own store the
// shadow reads native's POST-write value instead of the block pre-image (the
// glSetRasterState / nlListAddStart stale-read divergences). While a shadow is
// active the MMU records each first-touched Fake-VMEM offset here so
// StaticRecompCore can undo every store afterward. Called with the Fake-VMEM byte
// offset (em_address & GetFakeVMemMask()) + size, BEFORE the store commits. Null
// outside a shadow (module-less runs never set it), so the invariant is untouched.
using VmemWriteJournal = void (*)(u32 vmem_offset, u32 size, void* user);
extern VmemWriteJournal g_vmem_write_journal;
extern void* g_vmem_write_journal_user;
}  // namespace StaticRecompLockstep
