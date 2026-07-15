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

class TextureCacheBase
{
public:
  TextureCacheBase();
  virtual ~TextureCacheBase();

  bool Initialize();
  void Shutdown();

  void OnConfigChanged(const VideoConfig& config);

  // Removes textures which aren't used for more than TEXTURE_KILL_THRESHOLD frames,
  // frameCount is the current frame number.
  void Cleanup(int _frameCount);

  void Invalidate();
  void ReleaseToPool(TCacheEntry* entry);

  TCacheEntry* Load(u32 stage);
  RcTcacheEntry GetTexture(const int textureCacheSafetyColorSampleSize,
                           const TextureInfo& texture_info);
  RcTcacheEntry GetXFBTexture(u32 address, u32 width, u32 height, u32 stride,
                              MathUtil::Rectangle<int>* display_rect);

  virtual void BindTextures(BitSet32 used_textures, const std::array<SamplerState, 8>& samplers);
  void CopyRenderTargetToTexture(u32 dstAddr, EFBCopyFormat dstFormat, u32 width, u32 height,
                                 u32 dstStride, bool is_depth_copy,
                                 const MathUtil::Rectangle<int>& srcRect, bool isIntensity,
                                 bool scaleByHalf, float y_scale, float gamma, bool clamp_top,
                                 bool clamp_bottom,
                                 const CopyFilterCoefficients::Values& filter_coefficients);

  void ScaleTextureCacheEntryTo(RcTcacheEntry& entry, u32 new_width, u32 new_height);

  // Flushes all pending EFB copies to emulated RAM.
  void FlushEFBCopies();

  // Flush any Bound textures that can't be reused
  void FlushStaleBinds();

  // Texture Serialization
  void SerializeTexture(AbstractTexture* tex, const TextureConfig& config, PointerWrap& p);
  std::optional<TexPoolEntry> DeserializeTexture(PointerWrap& p);

  // Save States
  void DoState(PointerWrap& p);

  static bool AllCopyFilterCoefsNeeded(const std::array<u32, 3>& coefficients);
  static bool CopyFilterCanOverflow(const std::array<u32, 3>& coefficients);

  // Get a new sampler state
  static SamplerState GetSamplerState(u32 index, float custom_tex_scale, bool custom_tex,
                                      bool has_arbitrary_mips);

protected:
  // Decodes the specified data to the GPU texture specified by entry.
  // Returns false if the configuration is not supported.
  // width, height are the size of the image in pixels.
  // aligned_width, aligned_height are the size of the image in pixels, aligned to the block size.
  // row_stride is the number of bytes for a row of blocks, not pixels.
  bool DecodeTextureOnGPU(RcTcacheEntry& entry, u32 dst_level, const u8* data, u32 data_size,
                          TextureFormat format, u32 width, u32 height, u32 aligned_width,
                          u32 aligned_height, u32 row_stride, const u8* palette,
                          TLUTFormat palette_format);

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

  alignas(16) u8* m_temp = nullptr;
  size_t m_temp_size = 0;

private:
  using TexAddrCache = std::multimap<u32, RcTcacheEntry>;
  using TexHashCache = std::multimap<u64, RcTcacheEntry>;

  static bool DidLinkedAssetsChange(const TCacheEntry& entry);

  TCacheEntry* LoadImpl(u32 stage, bool force_reload);

  bool CreateUtilityTextures();

  void SetBackupConfig(const VideoConfig& config);

  RcTcacheEntry CreateTextureEntry(const TextureCreationInfo& creation_info,
                                   const TextureInfo& texture_info, int safety_color_sample_size,
                                   VideoCommon::CustomTextureData* custom_texture_data,
                                   bool custom_arbitrary_mipmaps, bool skip_texture_dump);

  RcTcacheEntry GetXFBFromCache(u32 address, u32 width, u32 height, u32 stride);

  RcTcacheEntry ApplyPaletteToEntry(RcTcacheEntry& entry, const u8* palette, TLUTFormat tlutfmt);

  RcTcacheEntry ReinterpretEntry(const RcTcacheEntry& existing_entry, TextureFormat new_format);

  RcTcacheEntry DoPartialTextureUpdates(RcTcacheEntry& entry_to_update, const u8* palette,
                                        TLUTFormat tlutfmt);
  void StitchXFBCopy(RcTcacheEntry& entry_to_update);

  void CheckTempSize(size_t required_size);

  RcTcacheEntry AllocateCacheEntry(const TextureConfig& config);
  TexAddrCache::iterator GetTexCacheIter(TCacheEntry* entry);

  // Return all possible overlapping textures. As addr+size of the textures is not
  // indexed, this may return false positives.
  std::pair<TexAddrCache::iterator, TexAddrCache::iterator>
  FindOverlappingTextures(u32 addr, u32 size_in_bytes);

  // Removes and unlinks texture from texture cache and returns it to the pool
  TexAddrCache::iterator InvalidateTexture(TexAddrCache::iterator t_iter,
                                           bool discard_pending_efb_copy = false);

  void UninitializeEFBMemory(u8* dst, u32 stride, u32 bytes_per_row, u32 num_blocks_y);
  void UninitializeXFBMemory(u8* dst, u32 stride, u32 bytes_per_row, u32 num_blocks_y);

  // Precomputing the coefficients for the previous, current, and next lines for the copy filter.
  static std::array<u32, 3>
  GetRAMCopyFilterCoefficients(const CopyFilterCoefficients::Values& coefficients);
  static std::array<u32, 3>
  GetVRAMCopyFilterCoefficients(const CopyFilterCoefficients::Values& coefficients);

  // Flushes a pending EFB copy to RAM from the host to the guest RAM.
  void WriteEFBCopyToRAM(u8* dst_ptr, u32 width, u32 height, u32 stride,
                         std::unique_ptr<AbstractStagingTexture> staging_texture);
  void FlushEFBCopy(TCacheEntry* entry);

  // Returns a staging texture of the maximum EFB copy size.
  std::unique_ptr<AbstractStagingTexture> GetEFBCopyStagingTexture();

  // Returns an EFB copy staging texture to the pool, so it can be re-used.
  void ReleaseEFBCopyStagingTexture(std::unique_ptr<AbstractStagingTexture> tex);

  bool CheckReadbackTexture(u32 width, u32 height, AbstractTextureFormat format);
  void DoSaveState(PointerWrap& p);
  void DoLoadState(PointerWrap& p);

  void ApplyMaterialToCacheEntry(const VideoCommon::MaterialResource& material, TCacheEntry* entry);

  // m_textures_by_address is the authoritive version of what's actually "in" the texture cache
  // but it's possible for invalidated TCache entries to live on elsewhere
  TexAddrCache m_textures_by_address;

  // m_textures_by_hash is an alternative view of the texture cache
  // All textures in here will also be in m_textures_by_address
  TexHashCache m_textures_by_hash;

  // m_bound_textures are actually active in the current draw
  // It's valid for textures to be in here after they've been invalidated
  std::array<RcTcacheEntry, 8> m_bound_textures{};

  TexturePool m_texture_pool;
  u64 m_last_entry_id = 0;

  // Backup configuration values
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

  // Encoding texture used for EFB copies to RAM.
  std::unique_ptr<AbstractTexture> m_efb_encoding_texture;
  std::unique_ptr<AbstractFramebuffer> m_efb_encoding_framebuffer;

  // Decoding texture used for GPU texture decoding.
  std::unique_ptr<AbstractTexture> m_decoding_texture;

  // Pool of readback textures used for deferred EFB copies.
  std::vector<std::unique_ptr<AbstractStagingTexture>> m_efb_copy_staging_texture_pool;

  // List of pending EFB copies. It is important that the order is preserved for these,
  // so that overlapping textures are written to guest RAM in the order they are issued.
  // It's valid for textures to live be in here after they've been invalidated
  std::vector<RcTcacheEntry> m_pending_efb_copies;

  // Staging texture used for readbacks.
  // We store this in the class so that the same staging texture can be used for multiple
  // readbacks, saving the overhead of allocating a new buffer every time.
  std::unique_ptr<AbstractStagingTexture> m_readback_texture;

  void OnFrameEnd();

  Common::EventHook m_frame_event =
      GetVideoEvents().after_frame_event.Register([this](Core::System&) { OnFrameEnd(); });

  VideoCommon::TextureUtils::TextureDumper m_texture_dumper;
};

extern std::unique_ptr<TextureCacheBase> g_texture_cache;
