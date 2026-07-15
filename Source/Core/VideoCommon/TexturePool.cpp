// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/TexturePool.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "Common/Logging/Log.h"
#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/Statistics.h"



TexturePool::TexturePool() = default;
TexturePool::~TexturePool() = default;

TexPoolEntry::TexPoolEntry(std::unique_ptr<AbstractTexture> tex,
                           std::unique_ptr<AbstractFramebuffer> fb)
    : texture(std::move(tex)), framebuffer(std::move(fb))
{
}

TexPoolEntry::~TexPoolEntry() = default;
TexPoolEntry::TexPoolEntry(TexPoolEntry&&) noexcept = default;
TexPoolEntry& TexPoolEntry::operator=(TexPoolEntry&&) noexcept = default;

void TexturePool::Clear()
{
  m_pool.clear();
}

void TexturePool::Cleanup(int frameCount)
{
  static const int TEXTURE_POOL_KILL_THRESHOLD = 3;
  auto iter = m_pool.begin();
  auto end = m_pool.end();
  while (iter != end)
  {
    if (iter->second.frameCount == FRAMECOUNT_INVALID)
    {
      iter->second.frameCount = frameCount;
    }
    if (frameCount > TEXTURE_POOL_KILL_THRESHOLD + iter->second.frameCount)
    {
      iter = m_pool.erase(iter);
    }
    else
    {
      ++iter;
    }
  }
}

void TexturePool::Release(TexPoolEntry&& entry, const TextureConfig& config)
{
  m_pool.emplace(config, std::move(entry));
}

std::optional<TexPoolEntry> TexturePool::Allocate(const TextureConfig& config)
{
  auto iter = FindMatching(config);
  if (iter != m_pool.end())
  {
    auto entry = std::move(iter->second);
    m_pool.erase(iter);
    return std::move(entry);
  }

  std::unique_ptr<AbstractTexture> texture = g_gfx->CreateTexture(config);
  if (!texture)
  {
    WARN_LOG_FMT(VIDEO, "Failed to allocate a {}x{}x{} texture", config.width, config.height,
                 config.layers);
    return std::nullopt;
  }

  std::unique_ptr<AbstractFramebuffer> framebuffer;
  if (config.IsRenderTarget())
  {
    framebuffer = g_gfx->CreateFramebuffer(texture.get(), nullptr);
    if (!framebuffer)
    {
      WARN_LOG_FMT(VIDEO, "Failed to allocate a {}x{}x{} framebuffer", config.width, config.height,
                   config.layers);
      return std::nullopt;
    }
  }

  INCSTAT(g_stats.num_textures_created);
  return TexPoolEntry(std::move(texture), std::move(framebuffer));
}

TexturePool::TexPoolMap::iterator TexturePool::FindMatching(const TextureConfig& config)
{
  auto range = m_pool.equal_range(config);
  auto matching_iter = std::find_if(range.first, range.second, [](const auto& iter) {
    return iter.first.IsRenderTarget() || iter.second.frameCount != FRAMECOUNT_INVALID;
  });
  return matching_iter != range.second ? matching_iter : m_pool.end();
}
