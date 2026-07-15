// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/MathUtil.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/Assets/CustomAsset.h"
#include "VideoCommon/HiresTextures.h"
#include "VideoCommon/TextureConfig.h"
#include "VideoCommon/TextureDecoder.h"
#include "VideoCommon/VideoCommon.h"

class AbstractFramebuffer;
class AbstractStagingTexture;
class PointerWrap;

constexpr std::string_view EFB_DUMP_PREFIX = "efb1";
constexpr std::string_view XFB_DUMP_PREFIX = "xfb1";

static constexpr int FRAMECOUNT_INVALID = 0;

struct TextureAndTLUTFormat
{
  TextureAndTLUTFormat(TextureFormat texfmt_ = TextureFormat::I4,
                       TLUTFormat tlutfmt_ = TLUTFormat::IA8)
      : texfmt(texfmt_), tlutfmt(tlutfmt_)
  {
  }

  bool operator==(const TextureAndTLUTFormat& other) const
  {
    if (IsColorIndexed(texfmt))
      return texfmt == other.texfmt && tlutfmt == other.tlutfmt;

    return texfmt == other.texfmt;
  }

  TextureFormat texfmt;
  TLUTFormat tlutfmt;
};

struct EFBCopyParams
{
  EFBCopyParams(PixelFormat efb_format_, EFBCopyFormat copy_format_, bool depth_, bool yuv_,
                bool all_copy_filter_coefs_needed_, bool copy_filter_can_overflow_,
                bool apply_gamma_)
      : efb_format(efb_format_), copy_format(copy_format_), depth(depth_), yuv(yuv_),
        all_copy_filter_coefs_needed(all_copy_filter_coefs_needed_),
        copy_filter_can_overflow(copy_filter_can_overflow_), apply_gamma(apply_gamma_)
  {
  }

  bool operator<(const EFBCopyParams& rhs) const
  {
    return std::tie(efb_format, copy_format, depth, yuv, all_copy_filter_coefs_needed,
                    copy_filter_can_overflow,
                    apply_gamma) < std::tie(rhs.efb_format, rhs.copy_format, rhs.depth, rhs.yuv,
                                            rhs.all_copy_filter_coefs_needed,
                                            rhs.copy_filter_can_overflow, rhs.apply_gamma);
  }

  PixelFormat efb_format;
  EFBCopyFormat copy_format;
  bool depth;
  bool yuv;
  bool all_copy_filter_coefs_needed;
  bool copy_filter_can_overflow;
  bool apply_gamma;
};

template <>
struct fmt::formatter<EFBCopyParams>
{
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  template <typename FormatContext>
  auto format(const EFBCopyParams& uid, FormatContext& ctx) const
  {
    std::string copy_format;
    if (uid.copy_format == EFBCopyFormat::XFB)
      copy_format = "XFB";
    else
      copy_format = fmt::to_string(uid.copy_format);
    return fmt::format_to(ctx.out(),
                          "format: {}, copy format: {}, depth: {}, yuv: {}, apply_gamma: {}, "
                          "all_copy_filter_coefs_needed: {}, copy_filter_can_overflow: {}",
                          uid.efb_format, copy_format, uid.depth, uid.yuv, uid.apply_gamma,
                          uid.all_copy_filter_coefs_needed, uid.copy_filter_can_overflow);
  }
};

struct TCacheEntry
{
  // common members
  std::unique_ptr<AbstractTexture> texture;
  std::unique_ptr<AbstractFramebuffer> framebuffer;
  u32 addr = 0;
  u32 size_in_bytes = 0;
  u64 base_hash = 0;
  u64 hash = 0;  // for paletted textures, hash = base_hash ^ palette_hash
  TextureAndTLUTFormat format;
  u32 memory_stride = 0;
  bool is_efb_copy = false;
  bool is_custom_tex = false;
  bool may_have_overlapping_textures = true;
  // indicates that the mips in this texture are arbitrary content, aren't just downscaled
  bool has_arbitrary_mips = false;
  bool should_force_safe_hashing = false;  // for XFB
  bool is_xfb_copy = false;
  bool is_xfb_container = false;
  u64 id = 0;
  u32 content_semaphore = 0;  // Counts up

  // Indicates that this TCacheEntry has been invalided from m_textures_by_address
  bool invalidated = false;

  bool reference_changed = false;  // used by xfb to determine when a reference xfb changed

  // Texture dimensions from the GameCube's point of view
  u32 native_width = 0;
  u32 native_height = 0;
  u32 native_levels = 0;

  // used to delete textures which haven't been used for TEXTURE_KILL_THRESHOLD frames
  int frameCount = FRAMECOUNT_INVALID;

  // Keep an iterator to the entry in m_textures_by_hash, so it does not need to be searched when
  // removing the cache entry
  std::multimap<u64, std::shared_ptr<TCacheEntry>>::iterator textures_by_hash_iter;

  // This is used to keep track of both:
  //   * efb copies used by this partially updated texture
  //   * partially updated textures which refer to this efb copy
  std::unordered_set<TCacheEntry*> references;

  // Pending EFB copy
  std::unique_ptr<AbstractStagingTexture> pending_efb_copy;
  u32 pending_efb_copy_width = 0;
  u32 pending_efb_copy_height = 0;

  std::string texture_info_name = "";

  VideoCommon::CustomAsset::TimeType last_load_time;
  std::shared_ptr<HiresTexture> hires_texture;

  explicit TCacheEntry(std::unique_ptr<AbstractTexture> tex,
                       std::unique_ptr<AbstractFramebuffer> fb);

  ~TCacheEntry();

  void SetGeneralParameters(u32 _addr, u32 _size, TextureAndTLUTFormat _format,
                            bool force_safe_hashing)
  {
    addr = _addr;
    size_in_bytes = _size;
    format = _format;
    should_force_safe_hashing = force_safe_hashing;
  }

  void SetDimensions(unsigned int _native_width, unsigned int _native_height,
                     unsigned int _native_levels)
  {
    native_width = _native_width;
    native_height = _native_height;
    native_levels = _native_levels;
    memory_stride = _native_width;
  }

  void SetHashes(u64 _base_hash, u64 _hash)
  {
    base_hash = _base_hash;
    hash = _hash;
  }

  // This texture entry is used by the other entry as a sub-texture
  void CreateReference(TCacheEntry* other_entry)
  {
    // References are two-way, so they can easily be destroyed later
    this->references.emplace(other_entry);
    other_entry->references.emplace(this);
  }

  // Acquiring a content lock will lock the current contents and prevent texture cache from
  // reusing the same entry for a newer version of the texture.
  void AcquireContentLock() { content_semaphore++; }
  void ReleaseContentLock() { content_semaphore--; }

  // Can this be mutated?
  bool IsLocked() const { return content_semaphore > 0; }

  void SetXfbCopy(u32 stride);
  void SetEfbCopy(u32 stride);
  void SetNotCopy();

  bool OverlapsMemoryRange(u32 range_address, u32 range_size) const;

  bool IsEfbCopy() const { return is_efb_copy; }
  bool IsCopy() const { return is_xfb_copy || is_efb_copy; }
  u32 NumBlocksX() const;
  u32 NumBlocksY() const;
  u32 BytesPerRow() const;

  u64 CalculateHash() const;

  int HashSampleSize() const;
  u32 GetWidth() const { return texture->GetConfig().width; }
  u32 GetHeight() const { return texture->GetConfig().height; }
  u32 GetNumLevels() const { return texture->GetConfig().levels; }
  u32 GetNumLayers() const { return texture->GetConfig().layers; }
  AbstractTextureFormat GetFormat() const { return texture->GetConfig().format; }
  void DoState(PointerWrap& p);
};

using RcTcacheEntry = std::shared_ptr<TCacheEntry>;

struct TexPoolEntry
{
  std::unique_ptr<AbstractTexture> texture;
  std::unique_ptr<AbstractFramebuffer> framebuffer;
  int frameCount = FRAMECOUNT_INVALID;

  TexPoolEntry(std::unique_ptr<AbstractTexture> tex, std::unique_ptr<AbstractFramebuffer> fb);
  ~TexPoolEntry();
  TexPoolEntry(TexPoolEntry&&) noexcept;
  TexPoolEntry& operator=(TexPoolEntry&&) noexcept;
};

struct TextureCreationInfo
{
  u64 base_hash;
  u64 full_hash;
  u32 bytes_per_block;
  u32 palette_size;
};
