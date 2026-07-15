// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/TextureCacheBase.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "Common/Align.h"
#include "Common/Assert.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/System.h"
#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/Assets/CustomTextureData.h"
#include "VideoCommon/Assets/TextureAssetUtils.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/ShaderCache.h"
#include "VideoCommon/TMEM.h"
#include "VideoCommon/AbstractStagingTexture.h"
#include "VideoCommon/Resources/MaterialResource.h"
#include "VideoCommon/TextureDecoder.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"

RcTcacheEntry TextureCacheBase::ApplyPaletteToEntry(RcTcacheEntry& entry, const u8* palette,
                                                    TLUTFormat tlutfmt)
{
  DEBUG_ASSERT(g_backend_info.bSupportsPaletteConversion);

  const AbstractPipeline* pipeline = g_shader_cache->GetPaletteConversionPipeline(tlutfmt);
  if (!pipeline)
  {
    ERROR_LOG_FMT(VIDEO, "Failed to get conversion pipeline for format {}", tlutfmt);
    return {};
  }

  TextureConfig new_config = entry->texture->GetConfig();
  new_config.levels = 1;
  new_config.flags |= AbstractTextureFlag_RenderTarget;

  RcTcacheEntry decoded_entry = AllocateCacheEntry(new_config);
  if (!decoded_entry)
    return decoded_entry;

  decoded_entry->SetGeneralParameters(entry->addr, entry->size_in_bytes, entry->format,
                                      entry->should_force_safe_hashing);
  decoded_entry->SetDimensions(entry->native_width, entry->native_height, 1);
  decoded_entry->SetHashes(entry->base_hash, entry->hash);
  decoded_entry->frameCount = FRAMECOUNT_INVALID;
  decoded_entry->should_force_safe_hashing = false;
  decoded_entry->SetNotCopy();
  decoded_entry->may_have_overlapping_textures = entry->may_have_overlapping_textures;

  g_gfx->BeginUtilityDrawing();

  const u32 palette_size = entry->format == TextureFormat::I4 ? 32 : 512;
  u32 texel_buffer_offset;
  if (g_vertex_manager->UploadTexelBuffer(palette, palette_size,
                                          TexelBufferFormat::TEXEL_BUFFER_FORMAT_R16_UINT,
                                          &texel_buffer_offset))
  {
    struct Uniforms
    {
      float multiplier;
      u32 texel_buffer_offset;
      u32 pad[2];
    };
    static_assert(std::is_standard_layout<Uniforms>::value);
    Uniforms uniforms = {};
    uniforms.multiplier = entry->format == TextureFormat::I4 ? 15.0f : 255.0f;
    uniforms.texel_buffer_offset = texel_buffer_offset;
    g_vertex_manager->UploadUtilityUniforms(&uniforms, sizeof(uniforms));

    g_gfx->SetAndDiscardFramebuffer(decoded_entry->framebuffer.get());
    g_gfx->SetViewportAndScissor(decoded_entry->texture->GetRect());
    g_gfx->SetPipeline(pipeline);
    g_gfx->SetTexture(1, entry->texture.get());
    g_gfx->SetSamplerState(1, RenderState::GetPointSamplerState());
    g_gfx->Draw(0, 3);
    g_gfx->EndUtilityDrawing();
    decoded_entry->texture->FinishedRendering();
  }
  else
  {
    ERROR_LOG_FMT(VIDEO, "Texel buffer upload of {} bytes failed", palette_size);
    g_gfx->EndUtilityDrawing();
  }

  m_textures_by_address.emplace(decoded_entry->addr, decoded_entry);

  return decoded_entry;
}

RcTcacheEntry TextureCacheBase::ReinterpretEntry(const RcTcacheEntry& existing_entry,
                                                 TextureFormat new_format)
{
  const AbstractPipeline* pipeline =
      g_shader_cache->GetTextureReinterpretPipeline(existing_entry->format.texfmt, new_format);
  if (!pipeline)
  {
    ERROR_LOG_FMT(VIDEO, "Failed to obtain texture reinterpreting pipeline from format {} to {}",
                  existing_entry->format.texfmt, new_format);
    return {};
  }

  TextureConfig new_config = existing_entry->texture->GetConfig();
  new_config.levels = 1;
  new_config.flags |= AbstractTextureFlag_RenderTarget;

  RcTcacheEntry reinterpreted_entry = AllocateCacheEntry(new_config);
  if (!reinterpreted_entry)
    return {};

  reinterpreted_entry->SetGeneralParameters(existing_entry->addr, existing_entry->size_in_bytes,
                                             new_format, existing_entry->should_force_safe_hashing);
  reinterpreted_entry->SetDimensions(existing_entry->native_width, existing_entry->native_height,
                                     1);
  reinterpreted_entry->SetHashes(existing_entry->base_hash, existing_entry->hash);
  reinterpreted_entry->frameCount = existing_entry->frameCount;
  reinterpreted_entry->SetNotCopy();
  reinterpreted_entry->is_efb_copy = existing_entry->is_efb_copy;
  reinterpreted_entry->may_have_overlapping_textures =
      existing_entry->may_have_overlapping_textures;

  g_gfx->BeginUtilityDrawing();
  g_gfx->SetAndDiscardFramebuffer(reinterpreted_entry->framebuffer.get());
  g_gfx->SetViewportAndScissor(reinterpreted_entry->texture->GetRect());
  g_gfx->SetPipeline(pipeline);
  g_gfx->SetTexture(0, existing_entry->texture.get());
  g_gfx->SetSamplerState(1, RenderState::GetPointSamplerState());
  g_gfx->Draw(0, 3);
  g_gfx->EndUtilityDrawing();
  reinterpreted_entry->texture->FinishedRendering();

  m_textures_by_address.emplace(reinterpreted_entry->addr, reinterpreted_entry);

  return reinterpreted_entry;
}

void TextureCacheBase::ScaleTextureCacheEntryTo(RcTcacheEntry& entry, u32 new_width, u32 new_height)
{
  if (entry->GetWidth() == new_width && entry->GetHeight() == new_height)
  {
    return;
  }

  const u32 max = g_backend_info.MaxTextureSize;
  if (max < new_width || max < new_height)
  {
    ERROR_LOG_FMT(VIDEO, "Texture too big, width = {}, height = {}", new_width, new_height);
    return;
  }

  const TextureConfig newconfig(new_width, new_height, 1, entry->GetNumLayers(), 1,
                                AbstractTextureFormat::RGBA8, AbstractTextureFlag_RenderTarget,
                                AbstractTextureType::Texture_2DArray);
  std::optional<TexPoolEntry> new_texture = m_texture_pool.Allocate(newconfig);
  if (!new_texture)
  {
    ERROR_LOG_FMT(VIDEO, "Scaling failed due to texture allocation failure");
    return;
  }

  g_gfx->ScaleTexture(new_texture->framebuffer.get(), new_texture->texture->GetConfig().GetRect(),
                      entry->texture.get(), entry->texture->GetConfig().GetRect());
  entry->texture.swap(new_texture->texture);
  entry->framebuffer.swap(new_texture->framebuffer);

  auto config = new_texture->texture->GetConfig();
  m_texture_pool.Release(
      TexPoolEntry(std::move(new_texture->texture), std::move(new_texture->framebuffer)), config);
}

RcTcacheEntry TextureCacheBase::DoPartialTextureUpdates(RcTcacheEntry& entry_to_update,
                                                        const u8* palette, TLUTFormat tlutfmt)
{
  if (!entry_to_update->may_have_overlapping_textures)
    return entry_to_update;
  entry_to_update->may_have_overlapping_textures = false;

  const bool isPaletteTexture = IsColorIndexed(entry_to_update->format.texfmt);

  if (entry_to_update->IsCopy())
    return entry_to_update;

  if (entry_to_update->IsLocked())
  {
    PanicAlertFmt("TextureCache: PartialTextureUpdates of locked textures is not implemented");
    return {};
  }

  u32 block_width = TexDecoder_GetBlockWidthInTexels(entry_to_update->format.texfmt);
  u32 block_height = TexDecoder_GetBlockHeightInTexels(entry_to_update->format.texfmt);
  u32 block_size = block_width * block_height *
                   TexDecoder_GetTexelSizeInNibbles(entry_to_update->format.texfmt) / 2;

  u32 numBlocksX = (entry_to_update->native_width + block_width - 1) / block_width;

  auto iter = FindOverlappingTextures(entry_to_update->addr, entry_to_update->size_in_bytes);
  while (iter.first != iter.second)
  {
    auto& entry = iter.first->second;
    if (entry != entry_to_update && entry->IsCopy() &&
        !entry->references.contains(entry_to_update.get()) &&
        entry->OverlapsMemoryRange(entry_to_update->addr, entry_to_update->size_in_bytes) &&
        entry->memory_stride == numBlocksX * block_size)
    {
      if (entry->hash == entry->CalculateHash())
      {
        if (!IsCompatibleTextureFormat(entry_to_update->format.texfmt, entry->format.texfmt))
        {
          if (!CanReinterpretTextureOnGPU(entry_to_update->format.texfmt, entry->format.texfmt))
          {
            ++iter.first;
            continue;
          }

          auto reinterpreted_entry = ReinterpretEntry(entry, entry_to_update->format.texfmt);
          if (reinterpreted_entry)
            entry = reinterpreted_entry;
        }

        if (isPaletteTexture)
        {
          auto decoded_entry = ApplyPaletteToEntry(entry, palette, tlutfmt);
          if (decoded_entry)
          {
            entry->CreateReference(entry_to_update.get());
            entry->frameCount = FRAMECOUNT_INVALID;
            entry = decoded_entry;
          }
          else
          {
            ++iter.first;
            continue;
          }
        }

        u32 src_x, src_y, dst_x, dst_y;

        if (entry->addr >= entry_to_update->addr)
        {
          u32 block_offset = (entry->addr - entry_to_update->addr) / block_size;
          u32 block_x = block_offset % numBlocksX;
          u32 block_y = block_offset / numBlocksX;
          src_x = 0;
          src_y = 0;
          dst_x = block_x * block_width;
          dst_y = block_y * block_height;
        }
        else
        {
          u32 block_offset = (entry_to_update->addr - entry->addr) / block_size;
          u32 block_x = (~block_offset + 1) % numBlocksX;
          u32 block_y = (block_offset + block_x) / numBlocksX;
          src_x = 0;
          src_y = block_y * block_height;
          dst_x = block_x * block_width;
          dst_y = 0;
        }

        u32 copy_width =
            std::min(entry->native_width - src_x, entry_to_update->native_width - dst_x);
        u32 copy_height =
            std::min(entry->native_height - src_y, entry_to_update->native_height - dst_y);

        if (entry_to_update->native_width != entry_to_update->GetWidth() ||
            entry_to_update->native_height != entry_to_update->GetHeight() ||
            entry->native_width != entry->GetWidth() || entry->native_height != entry->GetHeight())
        {
          ScaleTextureCacheEntryTo(
              entry_to_update, g_framebuffer_manager->EFBToScaledX(entry_to_update->native_width),
              g_framebuffer_manager->EFBToScaledY(entry_to_update->native_height));
          ScaleTextureCacheEntryTo(entry, g_framebuffer_manager->EFBToScaledX(entry->native_width),
                                   g_framebuffer_manager->EFBToScaledY(entry->native_height));

          src_x = g_framebuffer_manager->EFBToScaledX(src_x);
          src_y = g_framebuffer_manager->EFBToScaledY(src_y);
          dst_x = g_framebuffer_manager->EFBToScaledX(dst_x);
          dst_y = g_framebuffer_manager->EFBToScaledY(dst_y);
          copy_width = g_framebuffer_manager->EFBToScaledX(copy_width);
          copy_height = g_framebuffer_manager->EFBToScaledY(copy_height);
        }

        if (static_cast<u32>(src_x + copy_width) > entry->GetWidth() ||
            static_cast<u32>(src_y + copy_height) > entry->GetHeight() ||
            static_cast<u32>(dst_x + copy_width) > entry_to_update->GetWidth() ||
            static_cast<u32>(dst_y + copy_height) > entry_to_update->GetHeight())
        {
          ++iter.first;
          continue;
        }

        MathUtil::Rectangle<int> srcrect, dstrect;
        srcrect.left = src_x;
        srcrect.top = src_y;
        srcrect.right = (src_x + copy_width);
        srcrect.bottom = (src_y + copy_height);
        dstrect.left = dst_x;
        dstrect.top = dst_y;
        dstrect.right = (dst_x + copy_width);
        dstrect.bottom = (dst_y + copy_height);

        const u32 layers_to_copy = std::min(entry->GetNumLayers(), entry_to_update->GetNumLayers());
        for (u32 layer = 0; layer < layers_to_copy; layer++)
        {
          entry_to_update->texture->CopyRectangleFromTexture(entry->texture.get(), srcrect, layer,
                                                             0, dstrect, layer, 0);
        }

        if (isPaletteTexture)
        {
          iter.first = InvalidateTexture(iter.first);
          continue;
        }
        else
        {
          entry->CreateReference(entry_to_update.get());
          entry->frameCount = FRAMECOUNT_INVALID;
        }
      }
      else
      {
        iter.first = InvalidateTexture(iter.first);
        continue;
      }
    }
    ++iter.first;
  }

  return entry_to_update;
}

static bool IsAnisotropicEnhancementSafe(const TexMode0& tm0)
{
  return !(tm0.min_filter == FilterMode::Near && tm0.mag_filter == FilterMode::Near);
}

SamplerState TextureCacheBase::GetSamplerState(u32 index, float custom_tex_scale, bool custom_tex,
                                               bool has_arbitrary_mips)
{
  const TexMode0& tm0 = bpmem.tex.GetUnit(index).texMode0;

  SamplerState state = {};
  state.Generate(bpmem, index);

  if (g_ActiveConfig.texture_filtering_mode == TextureFilteringMode::Nearest)
  {
    state.tm0.min_filter = FilterMode::Near;
    state.tm0.mag_filter = FilterMode::Near;
    state.tm0.mipmap_filter = FilterMode::Near;
  }
  else if (g_ActiveConfig.texture_filtering_mode == TextureFilteringMode::Linear)
  {
    state.tm0.min_filter = FilterMode::Linear;
    state.tm0.mag_filter = FilterMode::Linear;
    state.tm0.mipmap_filter =
        tm0.mipmap_filter != MipMode::None ? FilterMode::Linear : FilterMode::Near;
  }

  if (custom_tex)
    state.tm1.max_lod = 255;

  if (g_ActiveConfig.iMaxAnisotropy != AnisotropicFilteringMode::Default &&
      IsAnisotropicEnhancementSafe(tm0))
  {
    state.tm0.anisotropic_filtering = std::to_underlying(g_ActiveConfig.iMaxAnisotropy);
  }

  if (state.tm0.anisotropic_filtering != 0)
  {
    state.tm0.min_filter = FilterMode::Linear;
    state.tm0.mag_filter = FilterMode::Linear;
    if (tm0.mipmap_filter != MipMode::None)
      state.tm0.mipmap_filter = FilterMode::Linear;
  }

  if (has_arbitrary_mips && tm0.mipmap_filter != MipMode::None)
  {
    s32 lod_offset = std::log2(g_framebuffer_manager->GetEFBScale() / custom_tex_scale) * 256.f;
    state.tm0.lod_bias = std::clamp<s32>(state.tm0.lod_bias + lod_offset, -32768, 32767);
    state.tm0.anisotropic_filtering = 0;
  }

  return state;
}

void TextureCacheBase::BindTextures(BitSet32 used_textures,
                                    const std::array<SamplerState, 8>& samplers)
{
  auto& system = Core::System::GetInstance();
  auto& pixel_shader_manager = system.GetPixelShaderManager();
  for (u32 i = 0; i < m_bound_textures.size(); i++)
  {
    const RcTcacheEntry& tentry = m_bound_textures[i];
    if (used_textures[i] && tentry)
    {
      g_gfx->SetTexture(i, tentry->texture.get());
      pixel_shader_manager.SetTexDims(i, tentry->native_width, tentry->native_height);

      auto& state = samplers[i];
      g_gfx->SetSamplerState(i, state);
      pixel_shader_manager.SetSamplerState(i, state.tm0.hex, state.tm1.hex);
    }
  }

  TMEM::FinalizeBinds(used_textures);
}

bool TextureCacheBase::DecodeTextureOnGPU(RcTcacheEntry& entry, u32 dst_level, const u8* data,
                                          u32 data_size, TextureFormat format, u32 width,
                                          u32 height, u32 aligned_width, u32 aligned_height,
                                          u32 row_stride, const u8* palette,
                                          TLUTFormat palette_format)
{
  const auto* info = TextureConversionShaderTiled::GetDecodingShaderInfo(format);
  if (!info)
    return false;

  const AbstractShader* shader = g_shader_cache->GetTextureDecodingShader(
      format, info->palette_size != 0 ? std::make_optional(palette_format) : std::nullopt);
  if (!shader)
    return false;

  const u32 bytes_per_buffer_elem =
      VertexManagerBase::GetTexelBufferElementSize(info->buffer_format);

  u32 src_offset = 0, palette_offset = 0;
  if (info->palette_size > 0)
  {
    if (!g_vertex_manager->UploadTexelBuffer(data, data_size, info->buffer_format, &src_offset,
                                             palette, info->palette_size,
                                             TEXEL_BUFFER_FORMAT_R16_UINT, &palette_offset))
    {
      return false;
    }
  }
  else
  {
    if (!g_vertex_manager->UploadTexelBuffer(data, data_size, info->buffer_format, &src_offset))
      return false;
  }

  struct Uniforms
  {
    u32 dst_width, dst_height;
    u32 src_width, src_height;
    u32 src_offset, src_row_stride;
    u32 palette_offset, unused;
  } uniforms = {width,          height,     aligned_width,
                aligned_height, src_offset, row_stride / bytes_per_buffer_elem,
                palette_offset};
  g_vertex_manager->UploadUtilityUniforms(&uniforms, sizeof(uniforms));
  g_gfx->SetComputeImageTexture(0, m_decoding_texture.get(), false, true);

  auto dispatch_groups =
      TextureConversionShaderTiled::GetDispatchCount(info, aligned_width, aligned_height);
  g_gfx->DispatchComputeShader(shader, info->group_size_x, info->group_size_y, 1,
                               dispatch_groups.first, dispatch_groups.second, 1);

  const auto copy_rect = entry->texture->GetConfig().GetMipRect(dst_level);
  entry->texture->CopyRectangleFromTexture(m_decoding_texture.get(), copy_rect, 0, 0, copy_rect, 0,
                                           dst_level);
  entry->texture->FinishedRendering();
  return true;
}

void TextureCacheBase::ApplyMaterialToCacheEntry(const VideoCommon::MaterialResource& material,
                                                 TCacheEntry* entry)
{
  const auto material_data = material.GetData();
  if (!material_data) [[unlikely]]
    return;

  auto new_entry = AllocateCacheEntry(entry->texture->GetConfig());
  new_entry->SetGeneralParameters(entry->addr, entry->size_in_bytes, entry->format,
                                  entry->should_force_safe_hashing);
  new_entry->SetDimensions(entry->native_width, entry->native_height, 1);
  new_entry->SetEfbCopy(entry->memory_stride);
  new_entry->may_have_overlapping_textures = false;
  new_entry->frameCount = FRAMECOUNT_INVALID;

  g_gfx->BeginUtilityDrawing();
  entry->texture->FinishedRendering();

  const auto custom_uniforms = material_data->GetUniforms();

  struct Uniforms
  {
    std::array<float, 4> source_resolution;
    std::array<float, 4> target_resolution;
    std::array<float, 4> window_resolution;
    std::array<float, 4> source_rectangle;
    s32 source_layer;
    s32 source_layer_pad[3];
    u32 time;
    u32 time_pad[3];
    s32 graphics_api;
    s32 graphics_api_pad[3];
    u32 efb_scale;
    u32 efb_scale_pad[3];
  } uniforms;

  const float rcp_src_width = 1.0f / entry->texture->GetWidth();
  const float rcp_src_height = 1.0f / entry->texture->GetHeight();

  uniforms.source_resolution = {static_cast<float>(entry->texture->GetWidth()),
                                 static_cast<float>(entry->texture->GetHeight()), rcp_src_width,
                                 rcp_src_height};

  uniforms.target_resolution = uniforms.source_resolution;

  const auto present_rect = g_presenter->GetTargetRectangle();
  uniforms.window_resolution = {static_cast<float>(present_rect.GetWidth()),
                                static_cast<float>(present_rect.GetHeight()),
                                1.0f / static_cast<float>(present_rect.GetWidth()),
                                1.0f / static_cast<float>(present_rect.GetHeight())};

  uniforms.source_rectangle = {0, 0, 1, 1};
  uniforms.source_layer = 0;
  uniforms.time = 0;
  uniforms.graphics_api = static_cast<s32>(g_backend_info.api_type);
  uniforms.efb_scale = g_framebuffer_manager->GetEFBScale();

  Common::UniqueBuffer<u8> uniform_buffer(custom_uniforms.size() + sizeof(uniforms));
  std::memcpy(uniform_buffer.data(), &uniforms, sizeof(uniforms));
  std::memcpy(uniform_buffer.data() + sizeof(uniforms), custom_uniforms.data(),
              custom_uniforms.size());
  g_vertex_manager->UploadUtilityUniforms(uniform_buffer.data(),
                                          static_cast<u32>(uniform_buffer.size()));

  g_gfx->SetAndDiscardFramebuffer(new_entry->framebuffer.get());
  g_gfx->SetViewportAndScissor(new_entry->framebuffer->GetRect());
  g_gfx->SetPipeline(material_data->GetPipeline());

  g_gfx->SetTexture(0, entry->texture.get());
  g_gfx->SetSamplerState(0, RenderState::GetPointSamplerState());

  for (const auto& texture : material_data->GetTextures())
  {
    g_gfx->SetTexture(texture.sampler_index, texture.texture);
    g_gfx->SetSamplerState(texture.sampler_index, texture.sampler);
  }

  g_gfx->Draw(0, 3);
  g_gfx->EndUtilityDrawing();

  new_entry->texture->FinishedRendering();

  std::swap(entry->texture, new_entry->texture);
  std::swap(entry->framebuffer, new_entry->framebuffer);

  ReleaseToPool(new_entry.get());

  if (auto* const next_material = material_data->GetNextMaterial(); next_material)
  {
    ApplyMaterialToCacheEntry(*next_material, entry);
  }
}
