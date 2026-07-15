// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/TextureCacheBase.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "Common/Assert.h"
#include "Common/ChunkFile.h"
#include "Common/Logging/Log.h"
#include "Core/Config/GraphicsSettings.h"
#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractStagingTexture.h"
#include "VideoCommon/AbstractTexture.h"

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

std::optional<TexPoolEntry> TextureCacheBase::DeserializeTexture(PointerWrap& p)
{
  TextureConfig config;
  p.Do(config);

  u32 total_size = 0;
  u8* texture_data = p.DoExternal(total_size);

  if (!p.IsReadMode() || total_size == 0)
    return std::nullopt;

  auto tex = m_texture_pool.Allocate(config);
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
