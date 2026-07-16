// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/TextureCacheBase.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_M_X86_64)
#include <pmmintrin.h>
#elif defined(_M_ARM64) || defined(__aarch64__)
#include <arm_neon.h>
#endif

#include <fmt/format.h>

#include "Common/Assert.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"
#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/TextureDecoder.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"

static void GetDisplayRectForXFBEntry(const TCacheEntry* entry, u32 width, u32 height,
                                     MathUtil::Rectangle<int>* display_rect)
{
  display_rect->left = 0;
  display_rect->top = 0;
  display_rect->right = static_cast<int>(width * entry->GetWidth() / entry->native_width);
  display_rect->bottom = static_cast<int>(height * entry->GetHeight() / entry->native_height);
}

RcTcacheEntry TextureCacheBase::GetXFBTexture(u32 address, u32 width, u32 height, u32 stride,
                                              MathUtil::Rectangle<int>* display_rect)
{
  // Compute total texture size. XFB textures aren't tiled, so this is simple.
  const u32 total_size = height * stride;

  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();
  const u8* src_data = memory.GetPointerForRange(address, total_size);
  if (!src_data)
  {
    ERROR_LOG_FMT(VIDEO, "Trying to load XFB texture from invalid address {:#010x}", address);
    return {};
  }

  // Do we currently have a mutable version of this XFB copy in VRAM?
  RcTcacheEntry entry = GetXFBFromCache(address, width, height, stride);
  if (entry && !entry->IsLocked())
  {
    if (entry->is_xfb_container)
    {
      StitchXFBCopy(entry);
      entry->texture->FinishedRendering();
    }

    GetDisplayRectForXFBEntry(entry.get(), width, height, display_rect);
    return entry;
  }

  // Create a new VRAM texture, and fill it with the data from guest RAM.
  entry = AllocateCacheEntry(TextureConfig(width, height, 1, 1, 1, AbstractTextureFormat::RGBA8,
                                           AbstractTextureFlag_RenderTarget,
                                           AbstractTextureType::Texture_2DArray));

  entry->SetGeneralParameters(address, total_size,
                              TextureAndTLUTFormat(TextureFormat::XFB, TLUTFormat::IA8), true);
  entry->SetDimensions(width, height, 1);
  entry->SetXfbCopy(stride);

  const u64 hash = entry->CalculateHash();
  entry->SetHashes(hash, hash);
  entry->is_xfb_container = true;
  entry->is_custom_tex = false;
  entry->may_have_overlapping_textures = false;
  entry->frameCount = FRAMECOUNT_INVALID;
  if (!g_ActiveConfig.UseGPUTextureDecoding() ||
      !DecodeTextureOnGPU(entry, 0, src_data, total_size, entry->format.texfmt, width, height,
                          width, height, stride, s_tex_mem.data(), entry->format.tlutfmt))
  {
    const u32 decoded_size = width * height * sizeof(u32);
    CheckTempSize(decoded_size);
    TexDecoder_DecodeXFB(m_temp.get(), src_data, width, height, stride);
    entry->texture->Load(0, width, height, width, m_temp.get(), decoded_size);
  }

  // Stitch any VRAM copies into the new RAM copy.
  StitchXFBCopy(entry);
  entry->texture->FinishedRendering();

  // Insert into the texture cache so we can re-use it next frame, if needed.
  m_textures_by_address.emplace(entry->addr, entry);
  SETSTAT(g_stats.num_textures_alive, static_cast<int>(m_textures_by_address.size()));
  INCSTAT(g_stats.num_textures_uploaded);

  if (g_ActiveConfig.bDumpXFBTarget || g_ActiveConfig.bGraphicMods)
  {
    const std::string id = fmt::format("{}x{}", width, height);
    if (g_ActiveConfig.bGraphicMods)
    {
      entry->texture_info_name = fmt::format("{}_{}", XFB_DUMP_PREFIX, id);
    }

    if (g_ActiveConfig.bDumpXFBTarget)
    {
      entry->texture->Save(fmt::format("{}{}_n{:06}_{}.png", File::GetUserPath(D_DUMPTEXTURES_IDX),
                                       XFB_DUMP_PREFIX, m_xfb_count++, id),
                           0);
    }
  }

  GetDisplayRectForXFBEntry(entry.get(), width, height, display_rect);
  return entry;
}

RcTcacheEntry TextureCacheBase::GetXFBFromCache(u32 address, u32 width, u32 height, u32 stride)
{
  auto iter_range = m_textures_by_address.equal_range(address);
  TexAddrCache::iterator iter = iter_range.first;

  while (iter != iter_range.second)
  {
    auto& entry = iter->second;

    // The only thing which has to match exactly is the stride. We can use a partial rectangle if
    // the VI width/height differs from that of the XFB copy.
    if (entry->is_xfb_copy && entry->memory_stride == stride && entry->native_width >= width &&
        entry->native_height >= height && !entry->may_have_overlapping_textures)
    {
      if (entry->hash == entry->CalculateHash() && !entry->reference_changed)
      {
        return entry;
      }
      else
      {
        // At this point, we either have an xfb copy that has changed its hash
        // or an xfb created by stitching or from memory that has been changed
        // we are safe to invalidate this
        iter = InvalidateTexture(iter);
        continue;
      }
    }

    ++iter;
  }

  return {};
}

void TextureCacheBase::StitchXFBCopy(RcTcacheEntry& stitched_entry)
{
  // It is possible that some of the overlapping textures overlap each other. This behavior has been
  // seen with XFB copies in Rogue Leader. To get the correct result, we apply the texture updates
  // in the order the textures were originally loaded. This ensures that the parts of the texture
  // that would have been overwritten in memory on real hardware get overwritten the same way here
  // too. This should work, but it may be a better idea to keep track of partial XFB copy
  // invalidations instead, which would reduce the amount of copying work here.
  std::vector<TCacheEntry*> candidates;
  bool create_upscaled_copy = false;

  auto iter = FindOverlappingTextures(stitched_entry->addr, stitched_entry->size_in_bytes);
  while (iter.first != iter.second)
  {
    // Currently, this checks the stride of the VRAM copy against the VI request. Therefore, for
    // interlaced modes, VRAM copies won't be considered candidates. This is okay for now, because
    // our force progressive hack means that an XFB copy should always have a matching stride. If
    // the hack is disabled, XFB2RAM should also be enabled. Should we wish to implement interlaced
    // stitching in the future, this would require a shader which grabs every second line.
    auto& entry = iter.first->second;
    if (entry != stitched_entry && entry->IsCopy() &&
        entry->OverlapsMemoryRange(stitched_entry->addr, stitched_entry->size_in_bytes) &&
        entry->memory_stride == stitched_entry->memory_stride)
    {
      if (entry->hash == entry->CalculateHash())
      {
        // Can't check the height here because of Y scaling.
        if (entry->native_width != entry->GetWidth())
          create_upscaled_copy = true;

        candidates.emplace_back(entry.get());
      }
      else
      {
        // If the hash does not match, this EFB copy will not be used for anything, so remove it
        iter.first = InvalidateTexture(iter.first);
        continue;
      }
    }
    ++iter.first;
  }

  if (candidates.empty())
    return;

  std::ranges::sort(candidates, {}, &TCacheEntry::id);

  // We only upscale when necessary to preserve resolution. i.e. when there are upscaled partial
  // copies to be stitched together.
  if (create_upscaled_copy)
  {
    ScaleTextureCacheEntryTo(stitched_entry,
                             g_framebuffer_manager->EFBToScaledX(stitched_entry->native_width),
                             g_framebuffer_manager->EFBToScaledY(stitched_entry->native_height));
  }

  for (TCacheEntry* entry : candidates)
  {
    int src_x, src_y, dst_x, dst_y;
    if (entry->addr >= stitched_entry->addr)
    {
      int pixel_offset = (entry->addr - stitched_entry->addr) / 2;
      src_x = 0;
      src_y = 0;
      dst_x = pixel_offset % stitched_entry->native_width;
      dst_y = pixel_offset / stitched_entry->native_width;
    }
    else
    {
      int pixel_offset = (stitched_entry->addr - entry->addr) / 2;
      src_x = pixel_offset % entry->native_width;
      src_y = pixel_offset / entry->native_width;
      dst_x = 0;
      dst_y = 0;
    }

    const int native_width =
        std::min(entry->native_width - src_x, stitched_entry->native_width - dst_x);
    const int native_height =
        std::min(entry->native_height - src_y, stitched_entry->native_height - dst_y);
    int src_width = native_width;
    int src_height = native_height;
    int dst_width = native_width;
    int dst_height = native_height;

    // Scale to internal resolution.
    if (entry->native_width != entry->GetWidth())
    {
      src_x = g_framebuffer_manager->EFBToScaledX(src_x);
      src_y = g_framebuffer_manager->EFBToScaledY(src_y);
      src_width = g_framebuffer_manager->EFBToScaledX(src_width);
      src_height = g_framebuffer_manager->EFBToScaledY(src_height);
    }
    if (create_upscaled_copy)
    {
      dst_x = g_framebuffer_manager->EFBToScaledX(dst_x);
      dst_y = g_framebuffer_manager->EFBToScaledY(dst_y);
      dst_width = g_framebuffer_manager->EFBToScaledX(dst_width);
      dst_height = g_framebuffer_manager->EFBToScaledY(dst_height);
    }

    // If the source rectangle is outside of what we actually have in VRAM, skip the copy.
    // The backend doesn't do any clamping, so if we don't, we'd pass out-of-range coordinates
    // to the graphics driver, which can cause GPU resets.
    if (static_cast<u32>(src_x + src_width) > entry->GetWidth() ||
        static_cast<u32>(src_y + src_height) > entry->GetHeight() ||
        static_cast<u32>(dst_x + dst_width) > stitched_entry->GetWidth() ||
        static_cast<u32>(dst_y + dst_height) > stitched_entry->GetHeight())
    {
      continue;
    }

    MathUtil::Rectangle<int> srcrect, dstrect;
    srcrect.left = src_x;
    srcrect.top = src_y;
    srcrect.right = (src_x + src_width);
    srcrect.bottom = (src_y + src_height);
    dstrect.left = dst_x;
    dstrect.top = dst_y;
    dstrect.right = (dst_x + dst_width);
    dstrect.bottom = (dst_y + dst_height);

    // We may have to scale if one of the copies is not internal resolution.
    if (srcrect.GetWidth() != dstrect.GetWidth() || srcrect.GetHeight() != dstrect.GetHeight())
    {
      g_gfx->ScaleTexture(stitched_entry->framebuffer.get(), dstrect, entry->texture.get(),
                          srcrect);
    }
    else
    {
      // If one copy is stereo, and the other isn't... not much we can do here :/
      const u32 layers_to_copy = std::min(entry->GetNumLayers(), stitched_entry->GetNumLayers());
      for (u32 layer = 0; layer < layers_to_copy; layer++)
      {
        stitched_entry->texture->CopyRectangleFromTexture(entry->texture.get(), srcrect, layer, 0,
                                                          dstrect, layer, 0);
      }
    }

    // Link the two textures together, so we won't apply this partial update again
    entry->CreateReference(stitched_entry.get());

    // Mark the texture update as used, as if it was loaded directly
    entry->frameCount = FRAMECOUNT_INVALID;
  }
}

void TextureCacheBase::UninitializeXFBMemory(u8* dst, u32 stride, u32 bytes_per_row,
                                             u32 num_blocks_y)
{
#if defined(_M_X86_64)
  __m128i sixteenBytes = _mm_set1_epi16((s16)(u16)0xFE01);
#elif defined(_M_ARM64) || defined(__aarch64__)
  uint16x8_t eightWords = vdupq_n_u16(0xFE01);
#endif

  for (u32 i = 0; i < num_blocks_y; i++)
  {
    u32 size = bytes_per_row;
    u8* rowdst = dst;
#if defined(_M_X86_64)
    while (size >= 16)
    {
      _mm_storeu_si128((__m128i*)rowdst, sixteenBytes);
      size -= 16;
      rowdst += 16;
    }
#elif defined(_M_ARM64) || defined(__aarch64__)
    while (size >= 16)
    {
      vst1q_u8(rowdst, vreinterpretq_u8_u16(eightWords));
      size -= 16;
      rowdst += 16;
    }
#endif
    for (u32 offset = 0; offset < size; offset++)
    {
      if (offset & 1)
      {
        rowdst[offset] = 254;
      }
      else
      {
        rowdst[offset] = 1;
      }
    }
    dst += stride;
  }
}
