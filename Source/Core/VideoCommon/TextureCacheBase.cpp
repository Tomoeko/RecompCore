// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/TextureCacheBase.h"
#include "VideoCommon/ArbitraryMipmapDetector.h"

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
#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/Hash.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"
#include "Common/MemoryUtil.h"

#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/ConfigManager.h"
#include "Core/FifoPlayer/FifoPlayer.h"
#include "Core/FifoPlayer/FifoRecorder.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"

#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractStagingTexture.h"
#include "VideoCommon/Assets/CustomTextureData.h"
#include "VideoCommon/Assets/TextureAssetUtils.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/GraphicsModSystem/Runtime/FBInfo.h"
#include "VideoCommon/GraphicsModSystem/Runtime/GraphicsModActionData.h"
#include "VideoCommon/GraphicsModSystem/Runtime/GraphicsModManager.h"
#include "VideoCommon/OpcodeDecoding.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/Resources/CustomResourceManager.h"
#include "VideoCommon/ShaderCache.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/TMEM.h"
#include "VideoCommon/TextureConversionShader.h"
#include "VideoCommon/TextureConverterShaderGen.h"
#include "VideoCommon/TextureDecoder.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"

static const u64 TEXHASH_INVALID = 0;
static const int TEXTURE_KILL_THRESHOLD = 64;

std::unique_ptr<TextureCacheBase> g_texture_cache;

void AlignedDeleter::operator()(u8* ptr) const
{
  Common::FreeAlignedMemory(ptr);
}

void TextureCacheBase::CheckTempSize(size_t required_size)
{
  if (required_size <= m_temp_size)
    return;

  m_temp_size = required_size;
  m_temp.reset(static_cast<u8*>(Common::AllocateAlignedMemory(m_temp_size, 16)));
}

TextureCacheBase::TextureCacheBase()
{
  SetBackupConfig(g_ActiveConfig);

  m_temp_size = 2048 * 2048 * 4;
  m_temp.reset(static_cast<u8*>(Common::AllocateAlignedMemory(m_temp_size, 16)));

  TexDecoder_SetTexFmtOverlayOptions(m_backup_config.texfmt_overlay,
                                     m_backup_config.texfmt_overlay_center);

  TMEM::InvalidateAll();
}

void TextureCacheBase::Shutdown()
{
  m_pending_efb_copies.clear();

  HiresTexture::Shutdown();

  Invalidate();
}

TextureCacheBase::~TextureCacheBase()
{
}

bool TextureCacheBase::Initialize()
{
  if (!CreateUtilityTextures())
  {
    PanicAlertFmt("Failed to create utility textures.");
    return false;
  }

  return true;
}

void TextureCacheBase::Invalidate()
{
  FlushEFBCopies();
  TMEM::InvalidateAll();

  for (auto& bind : m_bound_textures)
    bind.reset();
  m_textures_by_hash.clear();
  m_textures_by_address.clear();

  m_texture_pool.Clear();
}

void TextureCacheBase::OnConfigChanged(const VideoConfig& config)
{
  if (config.bHiresTextures != m_backup_config.hires_textures ||
      config.bCacheHiresTextures != m_backup_config.cache_hires_textures)
  {
    HiresTexture::Update();
  }

  const u32 change_count =
      config.graphics_mod_config ? config.graphics_mod_config->GetChangeCount() : 0;

  if (config.iSafeTextureCache_ColorSamples != m_backup_config.color_samples ||
      config.bTexFmtOverlayEnable != m_backup_config.texfmt_overlay ||
      config.bTexFmtOverlayCenter != m_backup_config.texfmt_overlay_center ||
      config.bHiresTextures != m_backup_config.hires_textures ||
      config.bEnableGPUTextureDecoding != m_backup_config.gpu_texture_decoding ||
      config.bDisableCopyToVRAM != m_backup_config.disable_vram_copies ||
      config.bArbitraryMipmapDetection != m_backup_config.arbitrary_mipmap_detection ||
      config.bGraphicMods != m_backup_config.graphics_mods ||
      change_count != m_backup_config.graphics_mod_change_count)
  {
    Invalidate();
    TexDecoder_SetTexFmtOverlayOptions(config.bTexFmtOverlayEnable, config.bTexFmtOverlayCenter);
  }

  SetBackupConfig(config);
}

bool TextureCacheBase::ShouldKillTexture(const TCacheEntry& entry, int frame_count) const
{
  if (entry.frameCount == FRAMECOUNT_INVALID)
    return false;

  if (frame_count <= TEXTURE_KILL_THRESHOLD + entry.frameCount)
    return false;

  if (!entry.IsCopy())
    return true;

  const bool time_to_check = (frame_count - entry.frameCount) % TEXTURE_KILL_THRESHOLD == 1;
  if (time_to_check && entry.hash != entry.CalculateHash())
    return true;

  return false;
}

void TextureCacheBase::Cleanup(int _frameCount)
{
  TexAddrCache::iterator iter = m_textures_by_address.begin();
  while (iter != m_textures_by_address.end())
  {
    auto& entry = iter->second;
    if (entry->frameCount == FRAMECOUNT_INVALID)
    {
      entry->frameCount = _frameCount;
      ++iter;
    }
    else if (ShouldKillTexture(*entry, _frameCount))
    {
      iter = InvalidateTexture(iter);
    }
    else
    {
      ++iter;
    }
  }

  m_texture_pool.Cleanup(_frameCount);
}

void TextureCacheBase::SetBackupConfig(const VideoConfig& config)
{
  m_backup_config.color_samples = config.iSafeTextureCache_ColorSamples;
  m_backup_config.texfmt_overlay = config.bTexFmtOverlayEnable;
  m_backup_config.texfmt_overlay_center = config.bTexFmtOverlayCenter;
  m_backup_config.hires_textures = config.bHiresTextures;
  m_backup_config.cache_hires_textures = config.bCacheHiresTextures;
  m_backup_config.stereo_3d = config.stereo_mode != StereoMode::Off;
  m_backup_config.efb_mono_depth = config.bStereoEFBMonoDepth;
  m_backup_config.gpu_texture_decoding = config.bEnableGPUTextureDecoding;
  m_backup_config.disable_vram_copies = config.bDisableCopyToVRAM;
  m_backup_config.arbitrary_mipmap_detection = config.bArbitraryMipmapDetection;
  m_backup_config.graphics_mods = config.bGraphicMods;
  m_backup_config.graphics_mod_change_count =
      config.graphics_mod_config ? config.graphics_mod_config->GetChangeCount() : 0;
}

bool TextureCacheBase::DidLinkedAssetsChange(const TCacheEntry& entry)
{
  if (!entry.hires_texture)
    return false;

  const auto* resource = entry.hires_texture->LoadTexture();

  return resource->GetLoadTime() > entry.last_load_time;
}

// Serialization routines have been moved to TextureCacheState.cpp

void TextureCacheBase::OnFrameEnd()
{
  FlushEFBCopies();

  Cleanup(g_presenter->FrameCount());
}

// ArbitraryMipmapDetector has been moved to ArbitraryMipmapDetector.h/.cpp

TCacheEntry* TextureCacheBase::Load(u32 stage)
{
  if (auto entry = LoadImpl(stage, false))
  {
    if (!DidLinkedAssetsChange(*entry))
    {
      return entry;
    }

    InvalidateTexture(GetTexCacheIter(entry));
    return LoadImpl(stage, true);
  }

  return nullptr;
}

TCacheEntry* TextureCacheBase::LoadImpl(u32 stage, bool force_reload)
{
  if (!force_reload && TMEM::IsValid(stage) && m_bound_textures[stage])
  {
    TCacheEntry* entry = m_bound_textures[stage].get();
    if (TMEM::IsCached(stage))
    {
      return entry;
    }

    if (!entry->invalidated && entry->base_hash == entry->CalculateHash())
    {
      return entry;
    }
  }

  const TextureInfo texture_info = TextureInfo::FromStage(stage);
  auto entry = GetTexture(g_ActiveConfig.iSafeTextureCache_ColorSamples, texture_info);

  if (!entry)
    return nullptr;

  entry->frameCount = FRAMECOUNT_INVALID;
  if (entry->texture_info_name.empty() && g_ActiveConfig.bGraphicMods)
  {
    entry->texture_info_name = texture_info.CalculateTextureName().GetFullName();

    GraphicsModActionData::TextureLoad texture_load{entry->texture_info_name};
    for (const auto& action :
         g_graphics_mod_manager->GetTextureLoadActions(entry->texture_info_name))
    {
      action->OnTextureLoad(&texture_load);
    }
  }
  m_bound_textures[texture_info.GetStage()] = entry;

  TMEM::Bind(texture_info.GetStage(), entry->NumBlocksX(), entry->NumBlocksY(),
             entry->GetNumLevels() > 1, entry->format == TextureFormat::RGBA8);

  return entry.get();
}

void TextureCacheBase::CalculateHashes(const TextureInfo& texture_info, u64& out_base_hash, u64& out_full_hash) const
{
  out_base_hash = Common::GetHash64(texture_info.GetData(), texture_info.GetTextureSize(),
                                    g_ActiveConfig.iSafeTextureCache_ColorSamples);
  if (texture_info.GetPaletteSize())
  {
    out_full_hash =
        out_base_hash ^ Common::GetHash64(texture_info.GetTlutAddress(), *texture_info.GetPaletteSize(),
                                          g_ActiveConfig.iSafeTextureCache_ColorSamples);
  }
  else
  {
    out_full_hash = out_base_hash;
  }
}

RcTcacheEntry TextureCacheBase::FindCompatibleTextureByAddress(const TextureInfo& texture_info, u64 base_hash, u64 full_hash,
                                                               TexAddrCache::iterator& oldest_entry, int& temp_frameCount)
{
  auto iter_range = m_textures_by_address.equal_range(texture_info.GetRawAddress());
  TexAddrCache::iterator iter = iter_range.first;
  oldest_entry = iter;
  temp_frameCount = 0x7fffffff;
  TexAddrCache::iterator unconverted_copy = m_textures_by_address.end();
  TexAddrCache::iterator unreinterpreted_copy = m_textures_by_address.end();

  const TextureAndTLUTFormat full_format(texture_info.GetTextureFormat(), texture_info.GetTlutFormat());

  while (iter != iter_range.second)
  {
    RcTcacheEntry& entry = iter->second;

    if (entry->IsEfbCopy() && entry->native_width == texture_info.GetRawWidth() &&
        entry->native_height == texture_info.GetRawHeight() &&
        entry->memory_stride == entry->BytesPerRow() && !entry->may_have_overlapping_textures)
    {
      if ((base_hash == entry->hash &&
           (!texture_info.GetPaletteSize() || g_backend_info.bSupportsPaletteConversion)) ||
          IsPlayingBackFifologWithBrokenEFBCopies)
      {
        if (!IsCompatibleTextureFormat(entry->format.texfmt, texture_info.GetTextureFormat()))
        {
          if (CanReinterpretTextureOnGPU(entry->format.texfmt, texture_info.GetTextureFormat()))
          {
            unreinterpreted_copy = iter++;
            continue;
          }
          else
          {
            ++iter;
            continue;
          }
        }
        else
        {
          unconverted_copy = m_textures_by_address.end();
        }

        if (!texture_info.GetPaletteSize() || !g_backend_info.bSupportsPaletteConversion)
          return entry;

        unconverted_copy = iter;
      }
      else
      {
        iter = InvalidateTexture(iter);
        continue;
      }
    }
    else
    {
      if (!entry->IsEfbCopy() && entry->hash == full_hash && entry->format == full_format &&
          entry->native_levels >= texture_info.GetLevelCount() &&
          entry->native_width == texture_info.GetRawWidth() &&
          entry->native_height == texture_info.GetRawHeight())
      {
        entry = DoPartialTextureUpdates(iter->second, texture_info.GetTlutAddress(),
                                        texture_info.GetTlutFormat());
        if (entry)
        {
          entry->texture->FinishedRendering();
          return entry;
        }
      }
    }

    if (entry->frameCount != FRAMECOUNT_INVALID && entry->frameCount < temp_frameCount &&
        !entry->IsCopy() && !(texture_info.GetPaletteSize() && entry->base_hash == base_hash))
    {
      temp_frameCount = entry->frameCount;
      oldest_entry = iter;
    }
    ++iter;
  }

  if (unreinterpreted_copy != m_textures_by_address.end())
  {
    auto decoded_entry =
        ReinterpretEntry(unreinterpreted_copy->second, texture_info.GetTextureFormat());

    if (unreinterpreted_copy == unconverted_copy && decoded_entry)
      decoded_entry = ApplyPaletteToEntry(decoded_entry, texture_info.GetTlutAddress(),
                                          texture_info.GetTlutFormat());

    if (decoded_entry)
      return decoded_entry;
  }

  if (unconverted_copy != m_textures_by_address.end())
  {
    auto decoded_entry = ApplyPaletteToEntry(
        unconverted_copy->second, texture_info.GetTlutAddress(), texture_info.GetTlutFormat());

    if (decoded_entry)
    {
      return decoded_entry;
    }
  }

  return {};
}

RcTcacheEntry TextureCacheBase::FindCompatibleTextureByHash(const TextureInfo& texture_info, u64 full_hash,
                                                            int safety_color_sample_size)
{
  const u32 palette_size = texture_info.GetPaletteSize() ? *texture_info.GetPaletteSize() : 0;
  if (safety_color_sample_size == 0 ||
      std::max(texture_info.GetTextureSize(), palette_size) <=
          (u32)safety_color_sample_size * 8)
  {
    const TextureAndTLUTFormat full_format(texture_info.GetTextureFormat(), texture_info.GetTlutFormat());
    auto hash_range = m_textures_by_hash.equal_range(full_hash);
    TexHashCache::iterator hash_iter = hash_range.first;
    while (hash_iter != hash_range.second)
    {
      RcTcacheEntry& entry = hash_iter->second;
      if (entry->format == full_format && entry->native_levels >= texture_info.GetLevelCount() &&
          entry->native_width == texture_info.GetRawWidth() &&
          entry->native_height == texture_info.GetRawHeight())
      {
        entry = DoPartialTextureUpdates(hash_iter->second, texture_info.GetTlutAddress(),
                                        texture_info.GetTlutFormat());
        if (entry)
        {
          entry->texture->FinishedRendering();
          return entry;
        }
      }
      ++hash_iter;
    }
  }
  return {};
}

std::shared_ptr<HiresTexture> TextureCacheBase::LoadCustomHiresTexture(const TextureInfo& texture_info,
                                                                       std::shared_ptr<VideoCommon::CustomTextureData>& custom_texture_data,
                                                                       VideoCommon::CustomAsset::TimeType& load_time,
                                                                       bool& has_arbitrary_mipmaps, bool& skip_texture_dump)
{
  std::shared_ptr<HiresTexture> hires_texture;
  has_arbitrary_mipmaps = false;
  skip_texture_dump = false;
  custom_texture_data = nullptr;
  load_time = {};

  if (g_ActiveConfig.bHiresTextures)
  {
    hires_texture = HiresTexture::Search(texture_info);
    if (hires_texture)
    {
      has_arbitrary_mipmaps = hires_texture->HasArbitraryMipmaps();
      const auto resource = hires_texture->LoadTexture();
      load_time = resource->GetLoadTime();
      custom_texture_data = resource->GetData();
      if (custom_texture_data && !VideoCommon::ValidateTextureData(
                                     hires_texture->GetId(), *custom_texture_data,
                                     texture_info.GetRawWidth(), texture_info.GetRawHeight()))
      {
        custom_texture_data = nullptr;
        load_time = {};
      }
      skip_texture_dump = true;
    }
  }
  return hires_texture;
}

std::string TextureCacheBase::TriggerGraphicsModActions(const TextureInfo& texture_info)
{
  std::string texture_name = "";
  if (g_ActiveConfig.bGraphicMods)
  {
    u32 height = texture_info.GetRawHeight();
    u32 width = texture_info.GetRawWidth();
    texture_name = texture_info.CalculateTextureName().GetFullName();
    GraphicsModActionData::TextureCreate texture_create{texture_name, width, height, nullptr,
                                                        nullptr};
    for (const auto& action : g_graphics_mod_manager->GetTextureCreateActions(texture_name))
    {
      action->OnTextureCreate(&texture_create);
    }
  }
  return texture_name;
}

RcTcacheEntry TextureCacheBase::GetTexture(const int textureCacheSafetyColorSampleSize,
                                           const TextureInfo& texture_info)
{
  if (!texture_info.IsDataValid())
    return {};

  if (texture_info.GetPaletteSize() && !IsValidTLUTFormat(texture_info.GetTlutFormat()))
    return {};

  u32 bytes_per_block = (texture_info.GetBlockWidth() * texture_info.GetBlockHeight() *
                         TexDecoder_GetTexelSizeInNibbles(texture_info.GetTextureFormat())) /
                        2;

  if (OpcodeDecoder::g_record_fifo_data && !texture_info.IsFromTmem())
  {
    Core::System::GetInstance().GetFifoRecorder().UseMemory(texture_info.GetRawAddress(),
                                                            texture_info.GetFullLevelSize(),
                                                            MemoryUpdate::Type::TextureMap);
  }

  u64 base_hash = TEXHASH_INVALID;
  u64 full_hash = TEXHASH_INVALID;
  CalculateHashes(texture_info, base_hash, full_hash);

  TexAddrCache::iterator oldest_entry;
  int temp_frameCount = 0x7fffffff;
  if (auto entry = FindCompatibleTextureByAddress(texture_info, base_hash, full_hash, oldest_entry, temp_frameCount))
  {
    return entry;
  }

  if (auto entry = FindCompatibleTextureByHash(texture_info, full_hash, textureCacheSafetyColorSampleSize))
  {
    return entry;
  }

  if (temp_frameCount != 0x7fffffff)
  {
    InvalidateTexture(oldest_entry);
  }

  std::shared_ptr<HiresTexture> hires_texture;
  bool has_arbitrary_mipmaps = false;
  bool skip_texture_dump = false;
  std::shared_ptr<VideoCommon::CustomTextureData> custom_texture_data = nullptr;
  VideoCommon::CustomAsset::TimeType load_time = {};
  hires_texture = LoadCustomHiresTexture(texture_info, custom_texture_data, load_time, has_arbitrary_mipmaps, skip_texture_dump);

  std::string texture_name = TriggerGraphicsModActions(texture_info);

  const u32 palette_size = texture_info.GetPaletteSize() ? *texture_info.GetPaletteSize() : 0;
  auto entry =
      CreateTextureEntry(TextureCreationInfo{base_hash, full_hash, bytes_per_block, palette_size},
                         texture_info, textureCacheSafetyColorSampleSize, custom_texture_data.get(),
                         has_arbitrary_mipmaps, skip_texture_dump);
  entry->hires_texture = std::move(hires_texture);
  entry->last_load_time = load_time;
  entry->texture_info_name = std::move(texture_name);
  return entry;
}

RcTcacheEntry TextureCacheBase::AllocateAndDecodeCustomTexture(const TextureConfig& config,
                                                               VideoCommon::CustomTextureData* custom_texture_data,
                                                               bool custom_arbitrary_mipmaps, u32 texLevels)
{
  auto entry = AllocateCacheEntry(config);
  if (!entry) [[unlikely]]
    return entry;
  const auto& slice = custom_texture_data->m_slices[0];
  for (u32 level_index = 0;
       level_index < std::min(texLevels, static_cast<u32>(slice.m_levels.size())); ++level_index)
  {
    const auto& level = slice.m_levels[level_index];
    entry->texture->Load(level_index, level.width, level.height, level.row_length,
                         level.data.data(), level.data.size());
  }

  entry->has_arbitrary_mips = custom_arbitrary_mipmaps;
  entry->is_custom_tex = true;
  return entry;
}

RcTcacheEntry TextureCacheBase::AllocateAndDecodeStandardTexture(const TextureInfo& texture_info,
                                                                 const TextureCreationInfo& creation_info,
                                                                 bool& out_has_arbitrary_mips,
                                                                 u32 texLevels, bool skip_texture_dump)
{
  const u32 expanded_width = texture_info.GetExpandedWidth();
  const u32 expanded_height = texture_info.GetExpandedHeight();

  const u32 width = texture_info.GetRawWidth();
  const u32 height = texture_info.GetRawHeight();

  const TextureConfig config(width, height, texLevels, 1, 1, AbstractTextureFormat::RGBA8, 0,
                             AbstractTextureType::Texture_2DArray);
  auto entry = AllocateCacheEntry(config);
  if (!entry) [[unlikely]]
    return entry;

  const bool decode_on_gpu =
      g_ActiveConfig.UseGPUTextureDecoding() &&
      !(texture_info.IsFromTmem() && texture_info.GetTextureFormat() == TextureFormat::RGBA8);

  ArbitraryMipmapDetector arbitrary_mip_detector;

  u8* dst_buffer = nullptr;

  if (!decode_on_gpu ||
      !DecodeTextureOnGPU(
          entry, 0, texture_info.GetData(), texture_info.GetTextureSize(),
          texture_info.GetTextureFormat(), width, height, expanded_width, expanded_height,
          creation_info.bytes_per_block * (expanded_width / texture_info.GetBlockWidth()),
          texture_info.GetTlutAddress(), texture_info.GetTlutFormat()))
  {
    size_t decoded_texture_size = expanded_width * sizeof(u32) * expanded_height;

    size_t total_texture_size = decoded_texture_size;

    size_t mip_downsample_buffer_size = decoded_texture_size * 5 / 16;

    size_t prev_level_size = decoded_texture_size;
    for (u32 i = 1; i < texture_info.GetLevelCount(); ++i)
    {
      prev_level_size /= 4;
      total_texture_size += prev_level_size;
    }

    total_texture_size += mip_downsample_buffer_size;

    CheckTempSize(total_texture_size);
    dst_buffer = m_temp.get();
    if (!(texture_info.GetTextureFormat() == TextureFormat::RGBA8 && texture_info.IsFromTmem()))
    {
      TexDecoder_Decode(dst_buffer, texture_info.GetData(), expanded_width, expanded_height,
                        texture_info.GetTextureFormat(), texture_info.GetTlutAddress(),
                        texture_info.GetTlutFormat());
    }
    else
    {
      TexDecoder_DecodeRGBA8FromTmem(dst_buffer, texture_info.GetData(),
                                     texture_info.GetTmemOddAddress(), expanded_width,
                                     expanded_height);
    }

    entry->texture->Load(0, width, height, expanded_width, dst_buffer, decoded_texture_size);

    arbitrary_mip_detector.AddLevel(width, height, expanded_width, dst_buffer);

    dst_buffer += decoded_texture_size;
  }

  for (const auto& mip_level : texture_info.GetMipMapLevels())
  {
#ifdef __APPLE__
    const bool no_mips = g_ActiveConfig.bNoMipmapping;
#else
    const bool no_mips = false;
#endif
    if (no_mips)
      break;
    if (!mip_level.IsDataValid())
    {
      ERROR_LOG_FMT(VIDEO, "Trying to use an invalid mipmap address {:#010x}",
                    texture_info.GetRawAddress());
      continue;
    }

    if (!decode_on_gpu ||
        !DecodeTextureOnGPU(entry, mip_level.GetLevel(), mip_level.GetData(),
                            mip_level.GetTextureSize(), texture_info.GetTextureFormat(),
                            mip_level.GetRawWidth(), mip_level.GetRawHeight(),
                            mip_level.GetExpandedWidth(), mip_level.GetExpandedHeight(),
                            creation_info.bytes_per_block *
                                (mip_level.GetExpandedWidth() / texture_info.GetBlockWidth()),
                            texture_info.GetTlutAddress(), texture_info.GetTlutFormat()))
    {
      const u32 decoded_mip_size =
          mip_level.GetExpandedWidth() * sizeof(u32) * mip_level.GetExpandedHeight();
      TexDecoder_Decode(dst_buffer, mip_level.GetData(), mip_level.GetExpandedWidth(),
                        mip_level.GetExpandedHeight(), texture_info.GetTextureFormat(),
                        texture_info.GetTlutAddress(), texture_info.GetTlutFormat());
      entry->texture->Load(mip_level.GetLevel(), mip_level.GetRawWidth(),
                           mip_level.GetRawHeight(), mip_level.GetExpandedWidth(), dst_buffer,
                           decoded_mip_size);

      arbitrary_mip_detector.AddLevel(mip_level.GetRawWidth(), mip_level.GetRawHeight(),
                                      mip_level.GetExpandedWidth(), dst_buffer);

      dst_buffer += decoded_mip_size;
    }
  }

  out_has_arbitrary_mips = arbitrary_mip_detector.HasArbitraryMipmaps(dst_buffer);
  return entry;
}

void TextureCacheBase::DumpTextureIfEnabled(const TCacheEntry& entry, const TextureInfo& texture_info, u32 levels)
{
  if (g_ActiveConfig.bDumpTextures && levels > 0)
  {
    const std::string basename = texture_info.CalculateTextureName().GetFullName();
    if (g_ActiveConfig.bDumpBaseTextures)
    {
      m_texture_dumper.DumpTexture(*entry.texture, basename, 0, entry.has_arbitrary_mips);
    }
    if (g_ActiveConfig.bDumpMipmapTextures)
    {
      for (u32 level = 1; level < levels; ++level)
      {
        m_texture_dumper.DumpTexture(*entry.texture, basename, level, entry.has_arbitrary_mips);
      }
    }
  }
}

RcTcacheEntry TextureCacheBase::CreateTextureEntry(
    const TextureCreationInfo& creation_info, const TextureInfo& texture_info,
    const int safety_color_sample_size, VideoCommon::CustomTextureData* custom_texture_data,
    const bool custom_arbitrary_mipmaps, bool skip_texture_dump)
{
#ifdef __APPLE__
  const bool no_mips = g_ActiveConfig.bNoMipmapping;
#else
  const bool no_mips = false;
#endif

  RcTcacheEntry entry;
  if (custom_texture_data)
  {
    const u32 texLevels = no_mips ? 1 : (u32)custom_texture_data->m_slices[0].m_levels.size();
    const auto& first_level = custom_texture_data->m_slices[0].m_levels[0];
    const TextureConfig config(first_level.width, first_level.height, texLevels, 1, 1,
                               first_level.format, 0, AbstractTextureType::Texture_2DArray);
    entry = AllocateAndDecodeCustomTexture(config, custom_texture_data, custom_arbitrary_mipmaps, texLevels);
  }
  else
  {
    const u32 texLevels = no_mips ? 1 : texture_info.GetLevelCount();
    bool has_arbitrary_mips = false;
    entry = AllocateAndDecodeStandardTexture(texture_info, creation_info, has_arbitrary_mips, texLevels, skip_texture_dump);
    if (entry)
      entry->has_arbitrary_mips = has_arbitrary_mips;
  }

  if (!entry) [[unlikely]]
    return entry;

  const u32 texLevels = no_mips ? 1 : (custom_texture_data ? (u32)custom_texture_data->m_slices[0].m_levels.size() : texture_info.GetLevelCount());
  if (!skip_texture_dump)
  {
    DumpTextureIfEnabled(*entry, texture_info, texLevels);
  }

  const auto iter = m_textures_by_address.emplace(texture_info.GetRawAddress(), entry);
  if (safety_color_sample_size == 0 ||
      std::max(texture_info.GetTextureSize(), creation_info.palette_size) <=
          (u32)safety_color_sample_size * 8)
  {
    entry->textures_by_hash_iter = m_textures_by_hash.emplace(creation_info.full_hash, entry);
  }

  const TextureAndTLUTFormat full_format(texture_info.GetTextureFormat(),
                                         texture_info.GetTlutFormat());
  entry->SetParameters(texture_info.GetRawAddress(), texture_info.GetTextureSize(),
                       full_format, texture_info.GetRawWidth(), texture_info.GetRawHeight(),
                       texture_info.GetLevelCount(), creation_info.base_hash, creation_info.full_hash);

  INCSTAT(g_stats.num_textures_uploaded);
  SETSTAT(g_stats.num_textures_alive, static_cast<int>(m_textures_by_address.size()));

  entry = DoPartialTextureUpdates(iter->second, texture_info.GetTlutAddress(),
                                  texture_info.GetTlutFormat());

  entry->texture->FinishedRendering();

  return entry;
}

RcTcacheEntry TextureCacheBase::AllocateCacheEntry(const TextureConfig& config)
{
  std::optional<TexPoolEntry> alloc = m_texture_pool.Allocate(config);
  if (!alloc)
    return {};

  auto cacheEntry =
      std::make_shared<TCacheEntry>(std::move(alloc->texture), std::move(alloc->framebuffer));
  cacheEntry->textures_by_hash_iter = m_textures_by_hash.end();
  cacheEntry->id = m_last_entry_id++;
  return cacheEntry;
}

TextureCacheBase::TexAddrCache::iterator TextureCacheBase::GetTexCacheIter(TCacheEntry* entry)
{
  auto iter_range = m_textures_by_address.equal_range(entry->addr);
  TexAddrCache::iterator iter = iter_range.first;
  while (iter != iter_range.second)
  {
    if (iter->second.get() == entry)
    {
      return iter;
    }
    ++iter;
  }
  return m_textures_by_address.end();
}

std::pair<TextureCacheBase::TexAddrCache::iterator, TextureCacheBase::TexAddrCache::iterator>
TextureCacheBase::FindOverlappingTextures(u32 addr, u32 size_in_bytes)
{
  constexpr u32 max_texture_size = 1024 * 1024 * 4;
  u32 lower_addr = addr > max_texture_size ? addr - max_texture_size : 0;
  auto begin = m_textures_by_address.lower_bound(lower_addr);
  auto end = m_textures_by_address.upper_bound(addr + size_in_bytes);

  return std::make_pair(begin, end);
}

TextureCacheBase::TexAddrCache::iterator
TextureCacheBase::InvalidateTexture(TexAddrCache::iterator iter, bool discard_pending_efb_copy)
{
  if (iter == m_textures_by_address.end())
    return m_textures_by_address.end();

  RcTcacheEntry& entry = iter->second;

  if (entry->textures_by_hash_iter != m_textures_by_hash.end())
  {
    m_textures_by_hash.erase(entry->textures_by_hash_iter);
    entry->textures_by_hash_iter = m_textures_by_hash.end();
  }

  if (entry->pending_efb_copy)
  {
    if (discard_pending_efb_copy)
    {
      ReleaseEFBCopyStagingTexture(std::move(entry->pending_efb_copy));
      auto pending_it = std::ranges::find(m_pending_efb_copies, entry);
      if (pending_it != m_pending_efb_copies.end())
        m_pending_efb_copies.erase(pending_it);
    }
    else
    {
      if (!entry->IsLocked())
        ReleaseToPool(entry.get());
    }
  }
  entry->invalidated = true;

  return m_textures_by_address.erase(iter);
}

void TextureCacheBase::ReleaseToPool(TCacheEntry* entry)
{
  if (!entry->texture)
    return;
  auto config = entry->texture->GetConfig();
  m_texture_pool.Release(TexPoolEntry(std::move(entry->texture), std::move(entry->framebuffer)), config);
}

bool TextureCacheBase::CreateUtilityTextures()
{
  constexpr TextureConfig encoding_texture_config(
      EFB_WIDTH * 4, 1024, 1, 1, 1, AbstractTextureFormat::BGRA8, AbstractTextureFlag_RenderTarget,
      AbstractTextureType::Texture_2DArray);
  m_efb_encoding_texture = g_gfx->CreateTexture(encoding_texture_config, "EFB encoding texture");
  if (!m_efb_encoding_texture)
    return false;

  m_efb_encoding_framebuffer = g_gfx->CreateFramebuffer(m_efb_encoding_texture.get(), nullptr);
  if (!m_efb_encoding_framebuffer)
    return false;

  if (g_backend_info.bSupportsGPUTextureDecoding)
  {
    constexpr TextureConfig decoding_texture_config(
        1024, 1024, 1, 1, 1, AbstractTextureFormat::RGBA8, AbstractTextureFlag_ComputeImage,
        AbstractTextureType::Texture_2DArray);
    m_decoding_texture =
        g_gfx->CreateTexture(decoding_texture_config, "GPU texture decoding texture");
    if (!m_decoding_texture)
      return false;
  }

  return true;
}

void TextureCacheBase::FlushStaleBinds()
{
  for (u32 i = 0; i < m_bound_textures.size(); i++)
  {
    if (!TMEM::IsCached(i))
      m_bound_textures[i].reset();
  }
}

