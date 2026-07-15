// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <filesystem>
#include <fmt/format.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Common/BitSet.h"
#include "Common/CommonTypes.h"
#include "Common/Flag.h"
#include "Common/MathUtil.h"

#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/Assets/CustomAsset.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/HiresTextures.h"
#include "VideoCommon/TextureInfo.h"
#include "VideoCommon/TextureUtils.h"
#include "VideoCommon/VideoEvents.h"
#include "VideoCommon/TextureCacheStructs.h"
#include "VideoCommon/TexturePool.h"

class AbstractFramebuffer;
class AbstractStagingTexture;
class PointerWrap;
struct SamplerState;
struct VideoConfig;

namespace VideoCommon
{
class CustomTextureData;
class GameTextureAsset;
class MaterialResource;
}  // namespace VideoCommon

struct AlignedDeleter
{
  void operator()(u8* ptr) const;
};

class TextureCacheBase
{
public:
  TextureCacheBase();
  virtual ~TextureCacheBase();

  // === Core Lifecycle & Configuration ===
  bool Initialize();
  void Shutdown();
  void OnConfigChanged(const VideoConfig& config);
  void Invalidate();
  void Cleanup(int _frameCount);

  // === Texture Loading & Cache Queries ===
  TCacheEntry* Load(u32 stage);
  RcTcacheEntry GetTexture(const int textureCacheSafetyColorSampleSize,
                           const TextureInfo& texture_info);

  // === XFB (External Frame Buffer) Operations ===
  RcTcacheEntry GetXFBTexture(u32 address, u32 width, u32 height, u32 stride,
                              MathUtil::Rectangle<int>* display_rect);

  // === Rendering & Bind Operations ===
  virtual void BindTextures(BitSet32 used_textures, const std::array<SamplerState, 8>& samplers);
  static SamplerState GetSamplerState(u32 index, float custom_tex_scale, bool custom_tex,
                                      bool has_arbitrary_mips);
  void ScaleTextureCacheEntryTo(RcTcacheEntry& entry, u32 new_width, u32 new_height);

  // === EFB Render Target & Copy Operations ===
  void CopyRenderTargetToTexture(u32 dstAddr, EFBCopyFormat dstFormat, u32 width, u32 height,
                                 u32 dstStride, bool is_depth_copy,
                                 const MathUtil::Rectangle<int>& srcRect, bool isIntensity,
                                 bool scaleByHalf, float y_scale, float gamma, bool clamp_top,
                                 bool clamp_bottom,
                                 const CopyFilterCoefficients::Values& filter_coefficients);
  void FlushEFBCopies();
  void FlushStaleBinds();

  // === Save States & Serialization ===
  void SerializeTexture(AbstractTexture* tex, const TextureConfig& config, PointerWrap& p);
  std::optional<TexPoolEntry> DeserializeTexture(PointerWrap& p);
  void DoState(PointerWrap& p);

  // === Static Helpers ===
  static bool AllCopyFilterCoefsNeeded(const std::array<u32, 3>& coefficients);
  static bool CopyFilterCanOverflow(const std::array<u32, 3>& coefficients);

  // === Memory Pool Management ===
  void ReleaseToPool(TCacheEntry* entry);

protected:
  // === GPU Texture Decoding ===
  bool DecodeTextureOnGPU(RcTcacheEntry& entry, u32 dst_level, const u8* data, u32 data_size,
                          TextureFormat format, u32 width, u32 height, u32 aligned_width,
                          u32 aligned_height, u32 row_stride, const u8* palette,
                          TLUTFormat palette_format);

  // === Internal EFB Copy Operations ===
  virtual void CopyEFB(AbstractStagingTexture* dst, const EFBCopyParams& params, u32 native_width,
                       u32 bytes_per_row, u32 num_blocks_y, u32 memory_stride,
                       const MathUtil::Rectangle<int>& src_rect, bool scale_by_half,
                       bool linear_filter, float y_scale, float gamma, bool clamp_top,
                       bool clamp_bottom, const std::array<u32, 3>& filter_coefficients);
  virtual void CopyEFBToCacheEntry(RcTcacheEntry& entry, bool is_depth_copy,
                                   const MathUtil::Rectangle<int>& src_rect, bool scale_by_half,
                                   bool linear_filter, EFBCopyFormat dst_format, bool is_intensity,
                                   float gamma, bool clamp_top, bool clamp_bottom,
                                   const std::array<u32, 3>& filter_coefficients);

  // === Staging Buffer Memory ===
  std::unique_ptr<u8[], AlignedDeleter> m_temp;
  size_t m_temp_size = 0;

private:
  using TexAddrCache = std::multimap<u32, RcTcacheEntry>;
  using TexHashCache = std::multimap<u64, RcTcacheEntry>;

  // === Private Load & Configuration Helpers ===
  static bool DidLinkedAssetsChange(const TCacheEntry& entry);
  bool ShouldKillTexture(const TCacheEntry& entry, int frame_count) const;
  TCacheEntry* LoadImpl(u32 stage, bool force_reload);
  bool CreateUtilityTextures();
  void SetBackupConfig(const VideoConfig& config);

  // === Private Creation & Decoding Helpers ===
  RcTcacheEntry CreateTextureEntry(const TextureCreationInfo& creation_info,
                                   const TextureInfo& texture_info, int safety_color_sample_size,
                                   VideoCommon::CustomTextureData* custom_texture_data,
                                   bool custom_arbitrary_mipmaps, bool skip_texture_dump);
  void DumpTextureIfEnabled(const TCacheEntry& entry, const TextureInfo& texture_info, u32 levels);
  void CalculateHashes(const TextureInfo& texture_info, u64& out_base_hash, u64& out_full_hash) const;

  RcTcacheEntry AllocateAndDecodeCustomTexture(const TextureConfig& config,
                                               VideoCommon::CustomTextureData* custom_texture_data,
                                               bool custom_arbitrary_mipmaps, u32 texLevels);
  RcTcacheEntry AllocateAndDecodeStandardTexture(const TextureInfo& texture_info,
                                                 const TextureCreationInfo& creation_info,
                                                 bool& out_has_arbitrary_mips,
                                                 u32 texLevels, bool skip_texture_dump);

  // === Private Compatibility & Match Resolution Helpers ===
  RcTcacheEntry FindCompatibleTextureByAddress(const TextureInfo& texture_info, u64 base_hash, u64 full_hash,
                                                TexAddrCache::iterator& oldest_entry, int& temp_frameCount);
  RcTcacheEntry FindCompatibleTextureByHash(const TextureInfo& texture_info, u64 full_hash,
                                            int safety_color_sample_size);
  std::shared_ptr<HiresTexture> LoadCustomHiresTexture(const TextureInfo& texture_info,
                                                       std::shared_ptr<VideoCommon::CustomTextureData>& custom_texture_data,
                                                       VideoCommon::CustomAsset::TimeType& load_time,
                                                       bool& has_arbitrary_mipmaps, bool& skip_texture_dump);
  std::string TriggerGraphicsModActions(const TextureInfo& texture_info);

  // === Private EFB/XFB Memory & Reference Helpers ===
  void CheckTempSize(size_t required_size);
  RcTcacheEntry AllocateCacheEntry(const TextureConfig& config);
  TexAddrCache::iterator GetTexCacheIter(TCacheEntry* entry);

  std::pair<TexAddrCache::iterator, TexAddrCache::iterator>
  FindOverlappingTextures(u32 addr, u32 size_in_bytes);

  TexAddrCache::iterator InvalidateTexture(TexAddrCache::iterator t_iter,
                                           bool discard_pending_efb_copy = false);

  void UninitializeEFBMemory(u8* dst, u32 stride, u32 bytes_per_row, u32 num_blocks_y);
  void UninitializeXFBMemory(u8* dst, u32 stride, u32 bytes_per_row, u32 num_blocks_y);

  static std::array<u32, 3> GetRAMCopyFilterCoefficients(const CopyFilterCoefficients::Values& coefficients);
  static std::array<u32, 3> GetVRAMCopyFilterCoefficients(const CopyFilterCoefficients::Values& coefficients);

  void WriteEFBCopyToRAM(u8* dst_ptr, u32 width, u32 height, u32 stride,
                         std::unique_ptr<AbstractStagingTexture> staging_texture);
  void FlushEFBCopy(TCacheEntry* entry);

  std::unique_ptr<AbstractStagingTexture> GetEFBCopyStagingTexture();
  void ReleaseEFBCopyStagingTexture(std::unique_ptr<AbstractStagingTexture> tex);

  bool CheckReadbackTexture(u32 width, u32 height, AbstractTextureFormat format);
  void DoSaveState(PointerWrap& p);
  void DoLoadState(PointerWrap& p);

  void ApplyMaterialToCacheEntry(const VideoCommon::MaterialResource& material, TCacheEntry* entry);

  // === Private XFB Helper Methods ===
  RcTcacheEntry GetXFBFromCache(u32 address, u32 width, u32 height, u32 stride);
  void StitchXFBCopy(RcTcacheEntry& entry_to_update);

  // === Private Rendering Helpers (from TextureCacheRender.cpp) ===
  RcTcacheEntry ApplyPaletteToEntry(RcTcacheEntry& entry, const u8* palette, TLUTFormat tlutfmt);
  RcTcacheEntry ReinterpretEntry(const RcTcacheEntry& existing_entry, TextureFormat new_format);
  RcTcacheEntry DoPartialTextureUpdates(RcTcacheEntry& entry_to_update, const u8* palette,
                                        TLUTFormat tlutfmt);

  // === Private State Members ===
  // Cache Views & Storage
  TexAddrCache m_textures_by_address;
  TexHashCache m_textures_by_hash;
  std::array<RcTcacheEntry, 8> m_bound_textures{};
  TexturePool m_texture_pool;
  u64 m_last_entry_id = 0;

  // EFB State Members
  std::unique_ptr<AbstractTexture> m_efb_encoding_texture;
  std::unique_ptr<AbstractFramebuffer> m_efb_encoding_framebuffer;
  std::vector<std::unique_ptr<AbstractStagingTexture>> m_efb_copy_staging_texture_pool;
  std::vector<RcTcacheEntry> m_pending_efb_copies;

  // GPU Decoding & Readback Members
  std::unique_ptr<AbstractTexture> m_decoding_texture;
  std::unique_ptr<AbstractStagingTexture> m_readback_texture;

  // Configuration Backups & Event Hooks
  struct BackupConfig
  {
    int color_samples;
    bool texfmt_overlay;
    bool texfmt_overlay_center;
    bool hires_textures;
    bool cache_hires_textures;
    bool copy_cache_enable;
    bool stereo_3d;
    bool efb_mono_depth;
    bool gpu_texture_decoding;
    bool disable_vram_copies;
    bool arbitrary_mipmap_detection;
    bool graphics_mods;
    u32 graphics_mod_change_count;
  };
  BackupConfig m_backup_config = {};
  u32 m_xfb_count = 0;

  void OnFrameEnd();
  Common::EventHook m_frame_event =
      GetVideoEvents().after_frame_event.Register([this](Core::System&) { OnFrameEnd(); });

  VideoCommon::TextureUtils::TextureDumper m_texture_dumper;
};

extern std::unique_ptr<TextureCacheBase> g_texture_cache;
