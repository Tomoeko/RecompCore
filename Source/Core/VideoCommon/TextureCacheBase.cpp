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
static const int TEXTURE_POOL_KILL_THRESHOLD = 3;

std::unique_ptr<TextureCacheBase> g_texture_cache;

void TextureCacheBase::CheckTempSize(size_t required_size)
{
  if (required_size <= m_temp_size)
    return;

  m_temp_size = required_size;
  Common::FreeAlignedMemory(m_temp);
  m_temp = static_cast<u8*>(Common::AllocateAlignedMemory(m_temp_size, 16));
}

TextureCacheBase::TextureCacheBase()
{
  SetBackupConfig(g_ActiveConfig);

  m_temp_size = 2048 * 2048 * 4;
  m_temp = static_cast<u8*>(Common::AllocateAlignedMemory(m_temp_size, 16));

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
  Common::FreeAlignedMemory(m_temp);
  m_temp = nullptr;
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

  m_texture_pool.clear();
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

void TextureCacheBase::Cleanup(int _frameCount)
{
  TexAddrCache::iterator iter = m_textures_by_address.begin();
  TexAddrCache::iterator tcend = m_textures_by_address.end();
  while (iter != tcend)
  {
    if (iter->second->frameCount == FRAMECOUNT_INVALID)
    {
      iter->second->frameCount = _frameCount;
      ++iter;
    }
    else if (_frameCount > TEXTURE_KILL_THRESHOLD + iter->second->frameCount)
    {
      if (iter->second->IsCopy())
      {
        if ((_frameCount - iter->second->frameCount) % TEXTURE_KILL_THRESHOLD == 1 &&
            iter->second->hash != iter->second->CalculateHash())
        {
          iter = InvalidateTexture(iter);
        }
        else
        {
          ++iter;
        }
      }
      else
      {
        iter = InvalidateTexture(iter);
      }
    }
    else
    {
      ++iter;
    }
  }

  TexPool::iterator iter2 = m_texture_pool.begin();
  TexPool::iterator tcend2 = m_texture_pool.end();
  while (iter2 != tcend2)
  {
    if (iter2->second.frameCount == FRAMECOUNT_INVALID)
    {
      iter2->second.frameCount = _frameCount;
    }
    if (_frameCount > TEXTURE_POOL_KILL_THRESHOLD + iter2->second.frameCount)
    {
      iter2 = m_texture_pool.erase(iter2);
    }
    else
    {
      ++iter2;
    }
  }
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

void TextureCacheBase::SerializeTexture(AbstractTexture* tex, const TextureConfig& config,
                                        PointerWrap& p)
{
  const bool skip_readback = p.IsMeasureMode();
  p.Do(config);

  if (skip_readback || CheckReadbackTexture(config.width, config.height, config.format))
  {
    u32 total_size = 0;
    for (u32 layer = 0; layer < config.layers; layer++)
    {
      for (u32 level = 0; level < config.levels; level++)
      {
        u32 level_width = std::max(config.width >> level, 1u);
        u32 level_height = std::max(config.height >> level, 1u);

        u32 stride = AbstractTexture::CalculateStrideForFormat(config.format, level_width);
        u32 size = stride * level_height;

        total_size += size;
      }
    }

    u8* texture_data = p.DoExternal(total_size);

    if (!skip_readback && p.IsMeasureMode())
    {
      ERROR_LOG_FMT(VIDEO, "Couldn't acquire {} bytes for serializing texture.", total_size);
      return;
    }

    if (!skip_readback)
    {
      for (u32 layer = 0; layer < config.layers; layer++)
      {
        for (u32 level = 0; level < config.levels; level++)
        {
          u32 level_width = std::max(config.width >> level, 1u);
          u32 level_height = std::max(config.height >> level, 1u);
          auto rect = tex->GetConfig().GetMipRect(level);
          m_readback_texture->CopyFromTexture(tex, rect, layer, level, rect);

          u32 stride = AbstractTexture::CalculateStrideForFormat(config.format, level_width);
          u32 size = stride * level_height;
          m_readback_texture->ReadTexels(rect, texture_data, stride);

          texture_data += size;
        }
      }
    }
  }
  else
  {
    PanicAlertFmt("Failed to create staging texture for serialization");
  }
}

std::optional<TextureCacheBase::TexPoolEntry> TextureCacheBase::DeserializeTexture(PointerWrap& p)
{
  TextureConfig config;
  p.Do(config);

  u32 total_size = 0;
  u8* texture_data = p.DoExternal(total_size);

  if (!p.IsReadMode() || total_size == 0)
    return std::nullopt;

  auto tex = AllocateTexture(config);
  if (!tex)
  {
    PanicAlertFmt("Failed to create texture for deserialization");
    return std::nullopt;
  }

  size_t start = 0;
  for (u32 layer = 0; layer < config.layers; layer++)
  {
    for (u32 level = 0; level < config.levels; level++)
    {
      const u32 level_width = std::max(config.width >> level, 1u);
      const u32 level_height = std::max(config.height >> level, 1u);
      const size_t stride = AbstractTexture::CalculateStrideForFormat(config.format, level_width);
      const size_t size = stride * level_height;
      if ((start + size) > total_size)
      {
        ERROR_LOG_FMT(VIDEO, "Insufficient texture data for layer {} level {}", layer, level);
        return tex;
      }

      tex->texture->Load(level, level_width, level_height, level_width, &texture_data[start], size);
      start += size;
    }
  }

  return tex;
}

void TextureCacheBase::DoState(PointerWrap& p)
{
  FlushEFBCopies();

  p.Do(m_last_entry_id);

  if (p.IsWriteMode() || p.IsMeasureMode())
    DoSaveState(p);
  else
    DoLoadState(p);
}

void TextureCacheBase::DoSaveState(PointerWrap& p)
{
  FlushStaleBinds();

  std::map<const TCacheEntry*, u32> entry_map;
  std::vector<TCacheEntry*> entries_to_save;
  auto ShouldSaveEntry = [](const RcTcacheEntry& entry) {
    return entry->IsCopy() || entry->invalidated;
  };
  auto AddCacheEntryToMap = [&entry_map, &entries_to_save](const RcTcacheEntry& entry) -> u32 {
    auto iter = entry_map.find(entry.get());
    if (iter != entry_map.end())
      return iter->second;

    u32 id = static_cast<u32>(entry_map.size());
    entry_map.emplace(entry.get(), id);
    entries_to_save.push_back(entry.get());
    return id;
  };
  auto GetCacheEntryId = [&entry_map](const TCacheEntry* entry) -> std::optional<u32> {
    auto iter = entry_map.find(entry);
    return iter != entry_map.end() ? std::make_optional(iter->second) : std::nullopt;
  };

  std::vector<std::pair<u32, u32>> textures_by_address_list;
  std::vector<std::pair<u64, u32>> textures_by_hash_list;
  std::vector<std::pair<u32, u32>> bound_textures_list;
  if (Config::Get(Config::GFX_SAVE_TEXTURE_CACHE_TO_STATE))
  {
    for (const auto& it : m_textures_by_address)
    {
      if (ShouldSaveEntry(it.second))
      {
        const u32 id = AddCacheEntryToMap(it.second);
        textures_by_address_list.emplace_back(it.first, id);
      }
    }
    for (const auto& it : m_textures_by_hash)
    {
      if (ShouldSaveEntry(it.second))
      {
        const u32 id = AddCacheEntryToMap(it.second);
        textures_by_hash_list.emplace_back(it.first, id);
      }
    }
    for (u32 i = 0; i < m_bound_textures.size(); i++)
    {
      const auto& tentry = m_bound_textures[i];
      if (m_bound_textures[i] && ShouldSaveEntry(tentry))
      {
        const u32 id = AddCacheEntryToMap(tentry);
        bound_textures_list.emplace_back(i, id);
      }
    }
  }

  u32 size = static_cast<u32>(entries_to_save.size());
  p.Do(size);
  for (TCacheEntry* entry : entries_to_save)
  {
    SerializeTexture(entry->texture.get(), entry->texture->GetConfig(), p);
    entry->DoState(p);
  }
  p.DoMarker("TextureCacheEntries");

  std::set<std::pair<u32, u32>> reference_pairs;
  for (const auto& it : entry_map)
  {
    const TCacheEntry* entry = it.first;
    auto id1 = GetCacheEntryId(entry);
    if (!id1)
      continue;

    for (const TCacheEntry* referenced_entry : entry->references)
    {
      auto id2 = GetCacheEntryId(referenced_entry);
      if (!id2)
        continue;

      auto refpair1 = std::make_pair(*id1, *id2);
      auto refpair2 = std::make_pair(*id2, *id1);
      if (!reference_pairs.contains(refpair2))
        reference_pairs.insert(std::move(refpair1));
    }
  }

  auto doList = [&p](const auto& list) {
    u32 list_size = static_cast<u32>(list.size());
    p.Do(list_size);
    for (const auto& it : list)
    {
      p.Do(it.first);
      p.Do(it.second);
    }
  };

  doList(reference_pairs);
  doList(textures_by_address_list);
  doList(textures_by_hash_list);
  doList(bound_textures_list);

  m_readback_texture.reset();
}

void TextureCacheBase::DoLoadState(PointerWrap& p)
{
  std::map<u32, RcTcacheEntry> id_map;
  RcTcacheEntry null_entry;
  auto GetEntry = [&id_map, &null_entry](u32 id) -> RcTcacheEntry& {
    auto iter = id_map.find(id);
    return iter == id_map.end() ? null_entry : iter->second;
  };

  const bool commit_state = p.IsReadMode();
  if (commit_state)
    Invalidate();

  u32 size = 0;
  p.Do(size);
  for (u32 i = 0; i < size; i++)
  {
    auto tex = DeserializeTexture(p);
    auto entry =
        std::make_shared<TCacheEntry>(std::move(tex->texture), std::move(tex->framebuffer));
    entry->textures_by_hash_iter = m_textures_by_hash.end();
    entry->DoState(p);
    if (entry->texture && commit_state)
      id_map.emplace(i, entry);
  }
  p.DoMarker("TextureCacheEntries");

  p.Do(size);
  for (u32 i = 0; i < size; i++)
  {
    u32 id1 = 0, id2 = 0;
    p.Do(id1);
    p.Do(id2);
    auto e1 = GetEntry(id1);
    auto e2 = GetEntry(id2);
    if (e1 && e2)
      e1->CreateReference(e2.get());
  }

  p.Do(size);
  for (u32 i = 0; i < size; i++)
  {
    u32 addr = 0;
    u32 id = 0;
    p.Do(addr);
    p.Do(id);

    auto& entry = GetEntry(id);
    if (entry)
      m_textures_by_address.emplace(addr, entry);
  }

  p.Do(size);
  for (u32 i = 0; i < size; i++)
  {
    u64 hash = 0;
    u32 id = 0;
    p.Do(hash);
    p.Do(id);

    auto& entry = GetEntry(id);
    if (entry)
      entry->textures_by_hash_iter = m_textures_by_hash.emplace(hash, entry);
  }

  for (u32 i = 0; i < m_bound_textures.size(); i++)
    m_bound_textures[i].reset();

  p.Do(size);
  for (u32 i = 0; i < size; i++)
  {
    u32 index = 0;
    u32 id = 0;
    p.Do(index);
    p.Do(id);

    auto& entry = GetEntry(id);
    if (entry)
      m_bound_textures[index] = entry;
  }
}

void TextureCacheBase::OnFrameEnd()
{
  FlushEFBCopies();

  Cleanup(g_presenter->FrameCount());
}

class ArbitraryMipmapDetector
{
private:
  using PixelRGBAf = std::array<float, 4>;
  using PixelRGBAu8 = std::array<u8, 4>;

public:
  explicit ArbitraryMipmapDetector() = default;

  void AddLevel(u32 width, u32 height, u32 row_length, const u8* buffer)
  {
    levels.push_back({{width, height, row_length}, buffer});
  }

  bool HasArbitraryMipmaps(u8* downsample_buffer) const
  {
    if (levels.size() < 2)
      return false;

    if (!g_ActiveConfig.bArbitraryMipmapDetection)
      return false;

    const auto threshold = g_ActiveConfig.fArbitraryMipmapDetectionThreshold;

    auto* src = downsample_buffer;
    auto* dst = downsample_buffer + levels[1].shape.row_length * levels[1].shape.height * 4;

    float total_diff = 0.f;

    for (std::size_t i = 0; i < levels.size() - 1; ++i)
    {
      const auto& level = levels[i];
      const auto& mip = levels[i + 1];

      u64 level_pixel_count = level.shape.width;
      level_pixel_count *= level.shape.height;

      ASSERT(level_pixel_count < (std::numeric_limits<u64>::max() / (255 * 255 * 4)));

      Level::Downsample(i ? src : level.pixels, level.shape, dst, mip.shape);

      auto diff = mip.AverageDiff(dst);
      total_diff += diff;

      std::swap(src, dst);
    }

    auto all_levels = total_diff / (levels.size() - 1);
    return all_levels > threshold;
  }

private:
  struct Shape
  {
    u32 width;
    u32 height;
    u32 row_length;
  };

  struct Level
  {
    Shape shape;
    const u8* pixels;

    static PixelRGBAu8 SampleLinear(const u8* src, const Shape& src_shape, u32 x, u32 y)
    {
      const auto* p = src + (x + y * src_shape.row_length) * 4;
      return {{p[0], p[1], p[2], p[3]}};
    }

    static void Downsample(const u8* src, const Shape& src_shape, u8* dst, const Shape& dst_shape)
    {
      for (u32 i = 0; i < dst_shape.height; ++i)
      {
        for (u32 j = 0; j < dst_shape.width; ++j)
         {
          auto x = j * 2;
          auto y = i * 2;
          const std::array<PixelRGBAu8, 4> samples{{
              SampleLinear(src, src_shape, x, y),
              SampleLinear(src, src_shape, x + 1, y),
              SampleLinear(src, src_shape, x, y + 1),
              SampleLinear(src, src_shape, x + 1, y + 1),
          }};

          auto* dst_pixel = dst + (j + i * dst_shape.row_length) * 4;
          for (int channel = 0; channel < 4; channel++)
          {
            uint32_t channel_value = samples[0][channel] + samples[1][channel] +
                                     samples[2][channel] + samples[3][channel];
            dst_pixel[channel] = (channel_value + 2) / 4;
          }
        }
      }
    }

    float AverageDiff(const u8* other) const
    {
      u64 current_diff_sum = 0;
      const auto* ptr1 = pixels;
      const auto* ptr2 = other;
      for (u32 i = 0; i < shape.height; ++i)
      {
        const auto* row1 = ptr1;
        const auto* row2 = ptr2;
        for (u32 j = 0; j < shape.width; ++j, row1 += 4, row2 += 4)
        {
          int pixel_diff = 0;
          for (int channel = 0; channel < 4; channel++)
          {
            const int diff = static_cast<int>(row1[channel]) - static_cast<int>(row2[channel]);
            const int diff_squared = diff * diff;
            pixel_diff += diff_squared;
          }
          current_diff_sum += pixel_diff;
        }
        ptr1 += shape.row_length;
        ptr2 += shape.row_length;
      }

      return std::sqrt(static_cast<float>(current_diff_sum) / (shape.width * shape.height * 4)) /
             2.56f;
    }
  };
  std::vector<Level> levels;
};

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

RcTcacheEntry TextureCacheBase::GetTexture(const int textureCacheSafetyColorSampleSize,
                                           const TextureInfo& texture_info)
{
  if (!texture_info.IsDataValid())
    return {};

  u64 base_hash = TEXHASH_INVALID;
  u64 full_hash = TEXHASH_INVALID;

  TextureAndTLUTFormat full_format(texture_info.GetTextureFormat(), texture_info.GetTlutFormat());

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

  base_hash = Common::GetHash64(texture_info.GetData(), texture_info.GetTextureSize(),
                                textureCacheSafetyColorSampleSize);
  u32 palette_size = 0;
  if (texture_info.GetPaletteSize())
  {
    palette_size = *texture_info.GetPaletteSize();
    full_hash =
        base_hash ^ Common::GetHash64(texture_info.GetTlutAddress(), *texture_info.GetPaletteSize(),
                                      textureCacheSafetyColorSampleSize);
  }
  else
  {
    full_hash = base_hash;
  }

  auto iter_range = m_textures_by_address.equal_range(texture_info.GetRawAddress());
  TexAddrCache::iterator iter = iter_range.first;
  TexAddrCache::iterator oldest_entry = iter;
  int temp_frameCount = 0x7fffffff;
  TexAddrCache::iterator unconverted_copy = m_textures_by_address.end();
  TexAddrCache::iterator unreinterpreted_copy = m_textures_by_address.end();

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

  if (textureCacheSafetyColorSampleSize == 0 ||
      std::max(texture_info.GetTextureSize(), palette_size) <=
          (u32)textureCacheSafetyColorSampleSize * 8)
  {
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

  if (temp_frameCount != 0x7fffffff)
  {
    InvalidateTexture(oldest_entry);
  }

  std::shared_ptr<HiresTexture> hires_texture;
  bool has_arbitrary_mipmaps = false;
  bool skip_texture_dump = false;
  std::shared_ptr<VideoCommon::CustomTextureData> custom_texture_data = nullptr;
  VideoCommon::CustomAsset::TimeType load_time = {};
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

  auto entry =
      CreateTextureEntry(TextureCreationInfo{base_hash, full_hash, bytes_per_block, palette_size},
                         texture_info, textureCacheSafetyColorSampleSize, custom_texture_data.get(),
                         has_arbitrary_mipmaps, skip_texture_dump);
  entry->hires_texture = std::move(hires_texture);
  entry->last_load_time = load_time;
  entry->texture_info_name = std::move(texture_name);
  return entry;
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
    entry = AllocateCacheEntry(config);
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
  }
  else
  {
    const u32 texLevels = no_mips ? 1 : texture_info.GetLevelCount();
    const u32 expanded_width = texture_info.GetExpandedWidth();
    const u32 expanded_height = texture_info.GetExpandedHeight();

    const u32 width = texture_info.GetRawWidth();
    const u32 height = texture_info.GetRawHeight();

    const TextureConfig config(width, height, texLevels, 1, 1, AbstractTextureFormat::RGBA8, 0,
                               AbstractTextureType::Texture_2DArray);
    entry = AllocateCacheEntry(config);
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
      dst_buffer = m_temp;
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

    entry->has_arbitrary_mips = arbitrary_mip_detector.HasArbitraryMipmaps(dst_buffer);

    if (g_ActiveConfig.bDumpTextures && !skip_texture_dump && texLevels > 0)
    {
      const std::string basename = texture_info.CalculateTextureName().GetFullName();
      if (g_ActiveConfig.bDumpBaseTextures)
      {
        m_texture_dumper.DumpTexture(*entry->texture, basename, 0, entry->has_arbitrary_mips);
      }
      if (g_ActiveConfig.bDumpMipmapTextures)
      {
        for (u32 level = 1; level < texLevels; ++level)
        {
          m_texture_dumper.DumpTexture(*entry->texture, basename, level, entry->has_arbitrary_mips);
        }
      }
    }
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
  entry->SetGeneralParameters(texture_info.GetRawAddress(), texture_info.GetTextureSize(),
                              full_format, false);
  entry->SetDimensions(texture_info.GetRawWidth(), texture_info.GetRawHeight(),
                       texture_info.GetLevelCount());
  entry->SetHashes(creation_info.base_hash, creation_info.full_hash);
  entry->memory_stride = entry->BytesPerRow();
  entry->SetNotCopy();

  INCSTAT(g_stats.num_textures_uploaded);
  SETSTAT(g_stats.num_textures_alive, static_cast<int>(m_textures_by_address.size()));

  entry = DoPartialTextureUpdates(iter->second, texture_info.GetTlutAddress(),
                                  texture_info.GetTlutFormat());

  entry->texture->FinishedRendering();

  return entry;
}

RcTcacheEntry TextureCacheBase::AllocateCacheEntry(const TextureConfig& config)
{
  std::optional<TexPoolEntry> alloc = AllocateTexture(config);
  if (!alloc)
    return {};

  auto cacheEntry =
      std::make_shared<TCacheEntry>(std::move(alloc->texture), std::move(alloc->framebuffer));
  cacheEntry->textures_by_hash_iter = m_textures_by_hash.end();
  cacheEntry->id = m_last_entry_id++;
  return cacheEntry;
}

std::optional<TextureCacheBase::TexPoolEntry>
TextureCacheBase::AllocateTexture(const TextureConfig& config)
{
  TexPool::iterator iter = FindMatchingTextureFromPool(config);
  if (iter != m_texture_pool.end())
  {
    auto entry = std::move(iter->second);
    m_texture_pool.erase(iter);
    return std::move(entry);
  }

  std::unique_ptr<AbstractTexture> texture = g_gfx->CreateTexture(config);
  if (!texture)
  {
    WARN_LOG_FMT(VIDEO, "Failed to allocate a {}x{}x{} texture", config.width, config.height,
                 config.layers);
    return {};
  }

  std::unique_ptr<AbstractFramebuffer> framebuffer;
  if (config.IsRenderTarget())
  {
    framebuffer = g_gfx->CreateFramebuffer(texture.get(), nullptr);
    if (!framebuffer)
    {
      WARN_LOG_FMT(VIDEO, "Failed to allocate a {}x{}x{} framebuffer", config.width, config.height,
                   config.layers);
      return {};
    }
  }

  INCSTAT(g_stats.num_textures_created);
  return TexPoolEntry(std::move(texture), std::move(framebuffer));
}

TextureCacheBase::TexPool::iterator
TextureCacheBase::FindMatchingTextureFromPool(const TextureConfig& config)
{
  auto range = m_texture_pool.equal_range(config);
  auto matching_iter = std::find_if(range.first, range.second, [](const auto& iter) {
    return iter.first.IsRenderTarget() || iter.second.frameCount != FRAMECOUNT_INVALID;
  });
  return matching_iter != range.second ? matching_iter : m_texture_pool.end();
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
  m_texture_pool.emplace(config,
                         TexPoolEntry(std::move(entry->texture), std::move(entry->framebuffer)));
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

TextureCacheBase::TexPoolEntry::TexPoolEntry(std::unique_ptr<AbstractTexture> tex,
                                             std::unique_ptr<AbstractFramebuffer> fb)
    : texture(std::move(tex)), framebuffer(std::move(fb))
{
}
