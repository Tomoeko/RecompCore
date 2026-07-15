// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/TextureCacheBase.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_M_X86_64)
#include <pmmintrin.h>
#endif

#include <fmt/format.h>

#include "Common/Align.h"
#include "Common/Assert.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/FifoPlayer/FifoRecorder.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"
#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractStagingTexture.h"
#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/GraphicsModSystem/Runtime/FBInfo.h"
#include "VideoCommon/GraphicsModSystem/Runtime/GraphicsModActionData.h"
#include "VideoCommon/GraphicsModSystem/Runtime/GraphicsModManager.h"
#include "VideoCommon/OpcodeDecoding.h"
#include "VideoCommon/ShaderCache.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/TMEM.h"
#include "VideoCommon/TextureConversionShader.h"
#include "VideoCommon/TextureConverterShaderGen.h"
#include "VideoCommon/TextureDecoder.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"

bool TextureCacheBase::CheckReadbackTexture(u32 width, u32 height, AbstractTextureFormat format)
{
  if (m_readback_texture && m_readback_texture->GetConfig().width >= width &&
      m_readback_texture->GetConfig().height >= height &&
      m_readback_texture->GetConfig().format == format)
  {
    return true;
  }

  TextureConfig staging_config(std::max(width, 128u), std::max(height, 128u), 1, 1, 1, format, 0,
                               AbstractTextureType::Texture_2DArray);
  m_readback_texture.reset();
  m_readback_texture = g_gfx->CreateStagingTexture(StagingTextureType::Readback, staging_config);
  return m_readback_texture != nullptr;
}

std::array<u32, 3>
TextureCacheBase::GetRAMCopyFilterCoefficients(const CopyFilterCoefficients::Values& coefficients)
{
  // To simplify the backend, we precalculate the three coefficients in common. Coefficients 0, 1
  // are for the row above, 2, 3, 4 are for the current pixel, and 5, 6 are for the row below.
  return {
      static_cast<u32>(coefficients[0]) + static_cast<u32>(coefficients[1]),
      static_cast<u32>(coefficients[2]) + static_cast<u32>(coefficients[3]) +
          static_cast<u32>(coefficients[4]),
      static_cast<u32>(coefficients[5]) + static_cast<u32>(coefficients[6]),
  };
}

std::array<u32, 3>
TextureCacheBase::GetVRAMCopyFilterCoefficients(const CopyFilterCoefficients::Values& coefficients)
{
  // If the user disables the copy filter, only apply it to the VRAM copy.
  // This way games which are sensitive to changes to the RAM copy of the XFB will be unaffected.
  std::array<u32, 3> res = GetRAMCopyFilterCoefficients(coefficients);
  if (!g_ActiveConfig.bDisableCopyFilter)
    return res;

  // Disabling the copy filter in options should not ignore the values the game sets completely,
  // as some games use the filter coefficients to control the brightness of the screen. Instead,
  // add all coefficients to the middle sample, so the deflicker/vertical filter has no effect.
  res[1] = res[0] + res[1] + res[2];
  res[0] = 0;
  res[2] = 0;
  return res;
}

bool TextureCacheBase::AllCopyFilterCoefsNeeded(const std::array<u32, 3>& coefficients)
{
  // If the top/bottom coefficients are zero, no point sampling/blending from these rows.
  return coefficients[0] != 0 || coefficients[2] != 0;
}

bool TextureCacheBase::CopyFilterCanOverflow(const std::array<u32, 3>& coefficients)
{
  // Normally, the copy filter coefficients will sum to at most 64.  If the sum is higher than that,
  // colors are clamped to the range [0, 255], but if the sum is higher than 128, that clamping
  // breaks (as colors end up >= 512, which wraps back to 0).
  return coefficients[0] + coefficients[1] + coefficients[2] >= 128;
}

void TextureCacheBase::CopyRenderTargetToTexture(
    u32 dstAddr, EFBCopyFormat dstFormat, u32 width, u32 height, u32 dstStride, bool is_depth_copy,
    const MathUtil::Rectangle<int>& srcRect, bool isIntensity, bool scaleByHalf, float y_scale,
    float gamma, bool clamp_top, bool clamp_bottom,
    const CopyFilterCoefficients::Values& filter_coefficients)
{
  const bool is_xfb_copy = !is_depth_copy && !isIntensity && dstFormat == EFBCopyFormat::XFB;
  bool copy_to_vram = g_backend_info.bSupportsCopyToVram && !g_ActiveConfig.bDisableCopyToVRAM;
  bool copy_to_ram =
      !(is_xfb_copy ? g_ActiveConfig.bSkipXFBCopyToRam : g_ActiveConfig.bSkipEFBCopyToRam) ||
      !copy_to_vram;

  // tex_w and tex_h are the native size of the texture in the GC memory.
  // The size scaled_* represents the emulated texture. Those differ
  // because of upscaling and because of yscaling of XFB copies.
  // For the latter, we keep the EFB resolution for the virtual XFB blit.
  u32 tex_w = width;
  u32 tex_h = height;
  u32 scaled_tex_w = g_framebuffer_manager->EFBToScaledX(width);
  u32 scaled_tex_h = g_framebuffer_manager->EFBToScaledY(height);

  if (scaleByHalf)
  {
    tex_w /= 2;
    tex_h /= 2;
    scaled_tex_w /= 2;
    scaled_tex_h /= 2;
  }

  if (!is_xfb_copy && !g_ActiveConfig.bCopyEFBScaled)
  {
    // No upscaling
    scaled_tex_w = tex_w;
    scaled_tex_h = tex_h;
  }

  // Get the base (in memory) format of this efb copy.
  TextureFormat baseFormat = TexDecoder_GetEFBCopyBaseFormat(dstFormat);

  u32 blockH = TexDecoder_GetBlockHeightInTexels(baseFormat);
  const u32 blockW = TexDecoder_GetBlockWidthInTexels(baseFormat);

  // Round up source height to multiple of block size
  u32 actualHeight = Common::AlignUp(tex_h, blockH);
  const u32 actualWidth = Common::AlignUp(tex_w, blockW);

  u32 num_blocks_y = actualHeight / blockH;
  const u32 num_blocks_x = actualWidth / blockW;

  // RGBA takes two cache lines per block; all others take one
  const u32 bytes_per_block = baseFormat == TextureFormat::RGBA8 ? 64 : 32;

  const u32 bytes_per_row = num_blocks_x * bytes_per_block;
  const u32 covered_range = num_blocks_y * dstStride;

  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();
  u8* dst = memory.GetPointerForRange(dstAddr, covered_range);
  if (dst == nullptr)
  {
    ERROR_LOG_FMT(VIDEO, "Trying to copy from EFB to invalid address {:#010x}", dstAddr);
    return;
  }

  if (g_ActiveConfig.bGraphicMods)
  {
    FBInfo info;
    info.m_width = tex_w;
    info.m_height = tex_h;
    info.m_texture_format = baseFormat;
    if (is_xfb_copy)
    {
      for (const auto& action : g_graphics_mod_manager->GetXFBActions(info))
      {
        action->BeforeXFB();
      }
    }
    else
    {
      bool skip = false;
      GraphicsModActionData::PreEFB efb{tex_w, tex_h, &skip, &scaled_tex_w, &scaled_tex_h};
      for (const auto& action : g_graphics_mod_manager->GetEFBActions(info))
      {
        action->BeforeEFB(&efb);
      }
      if (skip == true)
      {
        if (copy_to_ram)
          UninitializeEFBMemory(dst, dstStride, bytes_per_row, num_blocks_y);
        return;
      }
    }
  }

  if (dstStride < bytes_per_row)
  {
    ERROR_LOG_FMT(VIDEO, "Memory stride too small ({} < {})", dstStride, bytes_per_row);
    copy_to_vram = false;
  }

  const bool linear_filter =
      !is_depth_copy &&
      (scaleByHalf || g_framebuffer_manager->GetEFBScale() != 1 || y_scale > 1.0f);

  RcTcacheEntry entry;
  if (copy_to_vram)
  {
    // create the texture
    const TextureConfig config(scaled_tex_w, scaled_tex_h, 1, g_framebuffer_manager->GetEFBLayers(),
                               1, AbstractTextureFormat::RGBA8, AbstractTextureFlag_RenderTarget,
                               AbstractTextureType::Texture_2DArray);
    entry = AllocateCacheEntry(config);
    if (entry)
    {
      entry->SetGeneralParameters(dstAddr, 0, baseFormat, is_xfb_copy);
      entry->SetDimensions(tex_w, tex_h, 1);
      entry->frameCount = FRAMECOUNT_INVALID;
      if (is_xfb_copy)
      {
        entry->should_force_safe_hashing = is_xfb_copy;
        entry->SetXfbCopy(dstStride);
      }
      else
      {
        entry->SetEfbCopy(dstStride);
      }
      entry->may_have_overlapping_textures = false;
      entry->is_custom_tex = false;

      CopyEFBToCacheEntry(entry, is_depth_copy, srcRect, scaleByHalf, linear_filter, dstFormat,
                          isIntensity, gamma, clamp_top, clamp_bottom,
                          GetVRAMCopyFilterCoefficients(filter_coefficients));

      if (g_ActiveConfig.bGraphicMods)
      {
        FBInfo info;
        info.m_width = tex_w;
        info.m_height = tex_h;
        info.m_texture_format = baseFormat;
        if (!is_xfb_copy)
        {
          GraphicsModActionData::PostEFB efb;
          for (const auto& action : g_graphics_mod_manager->GetEFBActions(info))
          {
            action->AfterEFB(&efb);
            if (efb.material)
            {
              ApplyMaterialToCacheEntry(*efb.material, entry.get());
            }
          }
        }
      }

      if (is_xfb_copy && (g_ActiveConfig.bDumpXFBTarget || g_ActiveConfig.bGraphicMods))
      {
        const std::string id = fmt::format("{}x{}", tex_w, tex_h);
        if (g_ActiveConfig.bGraphicMods)
        {
          entry->texture_info_name = fmt::format("{}_{}", XFB_DUMP_PREFIX, id);
        }

        if (g_ActiveConfig.bDumpXFBTarget)
        {
          entry->texture->Save(fmt::format("{}{}_n{:06}_{}.png",
                                           File::GetUserPath(D_DUMPTEXTURES_IDX), XFB_DUMP_PREFIX,
                                           m_xfb_count++, id),
                               0);
        }
      }
      else if (g_ActiveConfig.bDumpEFBTarget || g_ActiveConfig.bGraphicMods)
      {
        const std::string id = fmt::format("{}x{}_{}", tex_w, tex_h, static_cast<int>(baseFormat));
        if (g_ActiveConfig.bGraphicMods)
        {
          entry->texture_info_name = fmt::format("{}_{}", EFB_DUMP_PREFIX, id);
        }

        if (g_ActiveConfig.bDumpEFBTarget)
        {
          static int efb_count = 0;
          entry->texture->Save(fmt::format("{}{}_n{:06}_{}.png",
                                           File::GetUserPath(D_DUMPTEXTURES_IDX), EFB_DUMP_PREFIX,
                                           efb_count++, id),
                               0);
        }
      }
    }
  }

  if (copy_to_ram)
  {
    const std::array<u32, 3> coefficients = GetRAMCopyFilterCoefficients(filter_coefficients);
    PixelFormat srcFormat = bpmem.zcontrol.pixel_format;
    EFBCopyParams format(srcFormat, dstFormat, is_depth_copy, isIntensity,
                         AllCopyFilterCoefsNeeded(coefficients),
                         CopyFilterCanOverflow(coefficients), gamma != 1.0);

    std::unique_ptr<AbstractStagingTexture> staging_texture = GetEFBCopyStagingTexture();
    if (staging_texture)
    {
      CopyEFB(staging_texture.get(), format, tex_w, bytes_per_row, num_blocks_y, dstStride, srcRect,
              scaleByHalf, linear_filter, y_scale, gamma, clamp_top, clamp_bottom, coefficients);

      // We can't defer if there is no VRAM copy (since we need to update the hash).
      if (!copy_to_vram || !g_ActiveConfig.bDeferEFBCopies)
      {
        // Immediately flush it.
        WriteEFBCopyToRAM(dst, bytes_per_row / sizeof(u32), num_blocks_y, dstStride,
                          std::move(staging_texture));
      }
      else
      {
        // Defer the flush until later.
        entry->pending_efb_copy = std::move(staging_texture);
        entry->pending_efb_copy_width = bytes_per_row / sizeof(u32);
        entry->pending_efb_copy_height = num_blocks_y;
        m_pending_efb_copies.push_back(entry);
      }
    }
  }
  else
  {
    if (is_xfb_copy)
    {
      UninitializeXFBMemory(dst, dstStride, bytes_per_row, num_blocks_y);
    }
    else
    {
      UninitializeEFBMemory(dst, dstStride, bytes_per_row, num_blocks_y);
    }
  }

  // Invalidate all textures, if they are either fully overwritten by our efb copy, or if they
  // have a different stride than our efb copy. Partly overwritten textures with the same stride
  // as our efb copy are marked to check them for partial texture updates.
  bool strided_efb_copy = dstStride != bytes_per_row;
  auto iter = FindOverlappingTextures(dstAddr, covered_range);
  while (iter.first != iter.second)
  {
    RcTcacheEntry& overlapping_entry = iter.first->second;

    if (overlapping_entry->addr == dstAddr && overlapping_entry->is_xfb_copy)
    {
      for (auto& reference : overlapping_entry->references)
      {
        reference->reference_changed = true;
      }
    }

    if (overlapping_entry->OverlapsMemoryRange(dstAddr, covered_range))
    {
      u32 overlap_range = std::min(overlapping_entry->addr + overlapping_entry->size_in_bytes,
                                   dstAddr + covered_range) -
                          std::max(overlapping_entry->addr, dstAddr);
      if (!copy_to_vram || overlapping_entry->memory_stride != dstStride ||
          (!strided_efb_copy && overlapping_entry->size_in_bytes == overlap_range) ||
          (strided_efb_copy && overlapping_entry->size_in_bytes == overlap_range &&
           overlapping_entry->addr == dstAddr))
      {
        iter.first = InvalidateTexture(iter.first, true);
        continue;
      }

      if (!overlapping_entry->is_xfb_container)
        overlapping_entry->may_have_overlapping_textures = true;

      if (overlapping_entry->is_xfb_copy && copy_to_ram)
      {
        overlapping_entry->hash = overlapping_entry->CalculateHash();
      }

      if (overlapping_entry->textures_by_hash_iter != m_textures_by_hash.end())
      {
        m_textures_by_hash.erase(overlapping_entry->textures_by_hash_iter);
        overlapping_entry->textures_by_hash_iter = m_textures_by_hash.end();
      }
    }
    ++iter.first;
  }

  if (OpcodeDecoder::g_record_fifo_data)
  {
    u32 address = dstAddr;
    for (u32 i = 0; i < num_blocks_y; i++)
    {
      Core::System::GetInstance().GetFifoRecorder().UseMemory(address, bytes_per_row,
                                                              MemoryUpdate::Type::TextureMap, true);
      address += dstStride;
    }
  }

  if (entry)
  {
    const u64 hash = entry->CalculateHash();
    entry->SetHashes(hash, hash);
    m_textures_by_address.emplace(dstAddr, std::move(entry));
  }
}

void TextureCacheBase::FlushEFBCopies()
{
  if (m_pending_efb_copies.empty())
    return;

  for (auto& entry : m_pending_efb_copies)
    FlushEFBCopy(entry.get());
  m_pending_efb_copies.clear();
}

void TextureCacheBase::WriteEFBCopyToRAM(u8* dst_ptr, u32 width, u32 height, u32 stride,
                                         std::unique_ptr<AbstractStagingTexture> staging_texture)
{
  MathUtil::Rectangle<int> copy_rect(0, 0, static_cast<int>(width), static_cast<int>(height));
  staging_texture->ReadTexels(copy_rect, dst_ptr, stride);
  ReleaseEFBCopyStagingTexture(std::move(staging_texture));
}

void TextureCacheBase::FlushEFBCopy(TCacheEntry* entry)
{
  const u32 covered_range = entry->pending_efb_copy_height * entry->memory_stride;

  auto& system = Core::System::GetInstance();
  auto& memory = system.GetMemory();
  u8* const dst = memory.GetPointerForRange(entry->addr, covered_range);
  WriteEFBCopyToRAM(dst, entry->pending_efb_copy_width, entry->pending_efb_copy_height,
                    entry->memory_stride, std::move(entry->pending_efb_copy));

  if (entry->invalidated)
    return;

  const u64 hash = entry->CalculateHash();
  entry->SetHashes(hash, hash);

  if (entry->is_xfb_copy)
  {
    auto range = FindOverlappingTextures(entry->addr, covered_range);
    for (auto iter = range.first; iter != range.second; ++iter)
    {
      auto& overlapping_entry = iter->second;
      if (overlapping_entry->may_have_overlapping_textures && overlapping_entry->is_xfb_copy &&
          overlapping_entry->OverlapsMemoryRange(entry->addr, covered_range))
      {
        const u64 overlapping_hash = overlapping_entry->CalculateHash();
        entry->SetHashes(overlapping_hash, overlapping_hash);
      }
    }
  }
}

std::unique_ptr<AbstractStagingTexture> TextureCacheBase::GetEFBCopyStagingTexture()
{
  if (!m_efb_copy_staging_texture_pool.empty())
  {
    auto ptr = std::move(m_efb_copy_staging_texture_pool.back());
    m_efb_copy_staging_texture_pool.pop_back();
    return ptr;
  }

  std::unique_ptr<AbstractStagingTexture> tex = g_gfx->CreateStagingTexture(
      StagingTextureType::Readback, m_efb_encoding_texture->GetConfig());
  if (!tex)
    WARN_LOG_FMT(VIDEO, "Failed to create EFB copy staging texture");

  return tex;
}

void TextureCacheBase::ReleaseEFBCopyStagingTexture(std::unique_ptr<AbstractStagingTexture> tex)
{
  m_efb_copy_staging_texture_pool.push_back(std::move(tex));
}

void TextureCacheBase::UninitializeEFBMemory(u8* dst, u32 stride, u32 bytes_per_row,
                                             u32 num_blocks_y)
{
  u8* ptr = dst;
  for (u32 i = 0; i < num_blocks_y; i++)
  {
    std::memset(ptr, 0, bytes_per_row);
    ptr += stride;
  }
}



void TextureCacheBase::CopyEFBToCacheEntry(RcTcacheEntry& entry, bool is_depth_copy,
                                           const MathUtil::Rectangle<int>& src_rect,
                                           bool scale_by_half, bool linear_filter,
                                           EFBCopyFormat dst_format, bool is_intensity, float gamma,
                                           bool clamp_top, bool clamp_bottom,
                                           const std::array<u32, 3>& filter_coefficients)
{
  g_framebuffer_manager->FlushEFBPokes();

  const AbstractPipeline* copy_pipeline = g_shader_cache->GetEFBCopyToVRAMPipeline(
      TextureConversionShaderGen::GetShaderUid(dst_format, is_depth_copy, is_intensity,
                                               scale_by_half, 1.0f / gamma, filter_coefficients));
  if (!copy_pipeline)
  {
    WARN_LOG_FMT(VIDEO, "Skipping EFB copy to VRAM due to missing pipeline.");
    return;
  }

  const auto scaled_src_rect = g_framebuffer_manager->ConvertEFBRectangle(src_rect);
  const auto framebuffer_rect = g_gfx->ConvertFramebufferRectangle(
      scaled_src_rect, g_framebuffer_manager->GetEFBFramebuffer());
  AbstractTexture* src_texture =
      is_depth_copy ? g_framebuffer_manager->ResolveEFBDepthTexture(framebuffer_rect) :
                      g_framebuffer_manager->ResolveEFBColorTexture(framebuffer_rect);

  g_gfx->BeginUtilityDrawing();
  src_texture->FinishedRendering();

  struct Uniforms
  {
    float src_left, src_top, src_width, src_height;
    std::array<u32, 3> filter_coefficients;
    float gamma_rcp;
    float clamp_top;
    float clamp_bottom;
    float pixel_height;
    u32 padding;
  };
  Uniforms uniforms;
  const float rcp_efb_width = 1.0f / static_cast<float>(g_framebuffer_manager->GetEFBWidth());
  const u32 efb_height = g_framebuffer_manager->GetEFBHeight();
  const float rcp_efb_height = 1.0f / static_cast<float>(efb_height);
  uniforms.src_left = framebuffer_rect.left * rcp_efb_width;
  uniforms.src_top = framebuffer_rect.top * rcp_efb_height;
  uniforms.src_width = framebuffer_rect.GetWidth() * rcp_efb_width;
  uniforms.src_height = framebuffer_rect.GetHeight() * rcp_efb_height;
  uniforms.filter_coefficients = filter_coefficients;
  uniforms.gamma_rcp = 1.0f / gamma;
  const u32 top_coord = clamp_top ? framebuffer_rect.top : 0;
  uniforms.clamp_top = (static_cast<float>(top_coord) + .5f) * rcp_efb_height;
  const u32 bottom_coord = (clamp_bottom ? framebuffer_rect.bottom : efb_height) - 1;
  uniforms.clamp_bottom = (static_cast<float>(bottom_coord) + .5f) * rcp_efb_height;
  uniforms.pixel_height = g_ActiveConfig.bCopyEFBScaled ? rcp_efb_height : 1.0f / EFB_HEIGHT;
  uniforms.padding = 0;
  g_vertex_manager->UploadUtilityUniforms(&uniforms, sizeof(uniforms));

  g_gfx->SetAndDiscardFramebuffer(entry->framebuffer.get());
  g_gfx->SetViewportAndScissor(entry->framebuffer->GetRect());
  g_gfx->SetPipeline(copy_pipeline);
  g_gfx->SetTexture(0, src_texture);
  g_gfx->SetSamplerState(0, linear_filter ? RenderState::GetLinearSamplerState() :
                                            RenderState::GetPointSamplerState());
  g_gfx->Draw(0, 3);
  g_gfx->EndUtilityDrawing();
  entry->texture->FinishedRendering();
}

void TextureCacheBase::CopyEFB(AbstractStagingTexture* dst, const EFBCopyParams& params,
                               u32 native_width, u32 bytes_per_row, u32 num_blocks_y,
                               u32 memory_stride, const MathUtil::Rectangle<int>& src_rect,
                               bool scale_by_half, bool linear_filter, float y_scale, float gamma,
                               bool clamp_top, bool clamp_bottom,
                               const std::array<u32, 3>& filter_coefficients)
{
  g_framebuffer_manager->FlushEFBPokes();

  const AbstractPipeline* copy_pipeline = g_shader_cache->GetEFBCopyToRAMPipeline(params);
  if (!copy_pipeline)
  {
    WARN_LOG_FMT(VIDEO, "Skipping EFB copy to VRAM due to missing pipeline.");
    return;
  }

  const auto scaled_src_rect = g_framebuffer_manager->ConvertEFBRectangle(src_rect);
  const auto framebuffer_rect = g_gfx->ConvertFramebufferRectangle(
      scaled_src_rect, g_framebuffer_manager->GetEFBFramebuffer());
  AbstractTexture* src_texture =
      params.depth ? g_framebuffer_manager->ResolveEFBDepthTexture(framebuffer_rect) :
                     g_framebuffer_manager->ResolveEFBColorTexture(framebuffer_rect);

  g_gfx->BeginUtilityDrawing();
  src_texture->FinishedRendering();

  struct Uniforms
  {
    std::array<s32, 4> position_uniform;
    float y_scale;
    float gamma_rcp;
    float clamp_top;
    float clamp_bottom;
    std::array<u32, 3> filter_coefficients;
    u32 padding;
  };
  Uniforms encoder_params;
  const u32 efb_height = g_framebuffer_manager->GetEFBHeight();
  const float rcp_efb_height = 1.0f / static_cast<float>(efb_height);
  encoder_params.position_uniform[0] = src_rect.left;
  encoder_params.position_uniform[1] = src_rect.top;
  encoder_params.position_uniform[2] = static_cast<s32>(native_width);
  encoder_params.position_uniform[3] = scale_by_half ? 2 : 1;
  encoder_params.y_scale = y_scale;
  encoder_params.gamma_rcp = 1.0f / gamma;
  const u32 top_coord = clamp_top ? framebuffer_rect.top : 0;
  encoder_params.clamp_top = (static_cast<float>(top_coord) + .5f) * rcp_efb_height;
  const u32 bottom_coord = (clamp_bottom ? framebuffer_rect.bottom : efb_height) - 1;
  encoder_params.clamp_bottom = (static_cast<float>(bottom_coord) + .5f) * rcp_efb_height;
  encoder_params.filter_coefficients = filter_coefficients;
  g_vertex_manager->UploadUtilityUniforms(&encoder_params, sizeof(encoder_params));

  const u32 render_width = bytes_per_row / sizeof(u32);
  const u32 render_height = num_blocks_y;
  const auto encode_rect = MathUtil::Rectangle<int>(0, 0, render_width, render_height);

  g_gfx->SetAndDiscardFramebuffer(m_efb_encoding_framebuffer.get());
  g_gfx->SetViewportAndScissor(encode_rect);
  g_gfx->SetPipeline(copy_pipeline);
  g_gfx->SetTexture(0, src_texture);
  g_gfx->SetSamplerState(0, linear_filter ? RenderState::GetLinearSamplerState() :
                                            RenderState::GetPointSamplerState());
  g_gfx->Draw(0, 3);
  dst->CopyFromTexture(m_efb_encoding_texture.get(), encode_rect, 0, 0, encode_rect);
  g_gfx->EndUtilityDrawing();

  g_vertex_manager->OnEFBCopyToRAM();
}
