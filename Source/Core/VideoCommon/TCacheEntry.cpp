// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/TextureCacheBase.h"

#include <algorithm>

#include "Common/Align.h"
#include "Common/Assert.h"
#include "Common/ChunkFile.h"
#include "Common/Hash.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"
#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractStagingTexture.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/TextureDecoder.h"
#include "VideoCommon/VideoConfig.h"

TCacheEntry::TCacheEntry(std::unique_ptr<AbstractTexture> tex,
                         std::unique_ptr<AbstractFramebuffer> fb)
    : texture(std::move(tex)), framebuffer(std::move(fb))
{
}

TCacheEntry::~TCacheEntry()
{
  for (auto& reference : references)
    reference->references.erase(this);
  ASSERT_MSG(VIDEO, g_texture_cache, "Texture cache destroyed before TCacheEntry was destroyed");
  g_texture_cache->ReleaseToPool(this);
}

bool TCacheEntry::OverlapsMemoryRange(u32 range_address, u32 range_size) const
{
  if (addr + size_in_bytes <= range_address)
    return false;

  if (addr >= range_address + range_size)
    return false;

  return true;
}

void TCacheEntry::DoState(PointerWrap& p)
{
  p.Do(addr);
  p.Do(size_in_bytes);
  p.Do(base_hash);
  p.Do(hash);
  p.Do(format);
  p.Do(memory_stride);
  p.Do(is_efb_copy);
  p.Do(is_custom_tex);
  p.Do(may_have_overlapping_textures);
  p.Do(invalidated);
  p.Do(has_arbitrary_mips);
  p.Do(should_force_safe_hashing);
  p.Do(is_xfb_copy);
  p.Do(is_xfb_container);
  p.Do(id);
  p.Do(reference_changed);
  p.Do(native_width);
  p.Do(native_height);
  p.Do(native_levels);
  p.Do(frameCount);
}

u32 TCacheEntry::BytesPerRow() const
{
  // RGBA takes two cache lines per block; all others take one
  const u32 bytes_per_block = format == TextureFormat::RGBA8 ? 64 : 32;

  return NumBlocksX() * bytes_per_block;
}

u32 TCacheEntry::NumBlocksX() const
{
  const u32 blockW = TexDecoder_GetBlockWidthInTexels(format.texfmt);

  // Round up source height to multiple of block size
  const u32 actualWidth = Common::AlignUp(native_width, blockW);

  return actualWidth / blockW;
}

u32 TCacheEntry::NumBlocksY() const
{
  u32 blockH = TexDecoder_GetBlockHeightInTexels(format.texfmt);
  // Round up source height to multiple of block size
  u32 actualHeight = Common::AlignUp(native_height, blockH);

  return actualHeight / blockH;
}

void TCacheEntry::SetXfbCopy(u32 stride)
{
  is_efb_copy = false;
  is_xfb_copy = true;
  is_xfb_container = false;
  memory_stride = stride;

  ASSERT_MSG(VIDEO, memory_stride >= BytesPerRow(), "Memory stride is too small");

  size_in_bytes = memory_stride * NumBlocksY();
}

void TCacheEntry::SetEfbCopy(u32 stride)
{
  is_efb_copy = true;
  is_xfb_copy = false;
  is_xfb_container = false;
  memory_stride = stride;

  ASSERT_MSG(VIDEO, memory_stride >= BytesPerRow(), "Memory stride is too small");

  size_in_bytes = memory_stride * NumBlocksY();
}

void TCacheEntry::SetNotCopy()
{
  is_efb_copy = false;
  is_xfb_copy = false;
  is_xfb_container = false;
}

int TCacheEntry::HashSampleSize() const
{
  if (should_force_safe_hashing)
  {
    return 0;
  }

  return g_ActiveConfig.iSafeTextureCache_ColorSamples;
}

u64 TCacheEntry::CalculateHash() const
{
  const u32 bytes_per_row = BytesPerRow();
  const u32 hash_sample_size = HashSampleSize();

  // FIXME: textures from tmem won't get the correct hash.
  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();
  u8* ptr = memory.GetPointerForRange(addr, size_in_bytes);
  if (memory_stride == bytes_per_row)
  {
    return Common::GetHash64(ptr, size_in_bytes, hash_sample_size);
  }
  else
  {
    const u32 num_blocks_y = NumBlocksY();
    u64 temp_hash = size_in_bytes;

    u32 samples_per_row = 0;
    if (hash_sample_size != 0)
    {
      // Hash at least 4 samples per row to avoid hashing in a bad pattern, like just on the left
      // side of the efb copy
      samples_per_row = std::max(hash_sample_size / num_blocks_y, 4u);
    }

    for (u32 i = 0; i < num_blocks_y; i++)
    {
      // Multiply by a prime number to mix the hash up a bit. This prevents identical blocks from
      // canceling each other out
      temp_hash = (temp_hash * 397) ^ Common::GetHash64(ptr, bytes_per_row, samples_per_row);
      ptr += memory_stride;
    }
    return temp_hash;
  }
}
