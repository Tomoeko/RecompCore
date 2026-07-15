// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <unordered_map>

#include "VideoCommon/TextureCacheStructs.h"

class TexturePool
{
public:
  TexturePool();
  ~TexturePool();

  void Clear();
  void Cleanup(int frameCount);
  void Release(TexPoolEntry&& entry, const TextureConfig& config);
  std::optional<TexPoolEntry> Allocate(const TextureConfig& config);

private:
  using TexPoolMap = std::unordered_multimap<TextureConfig, TexPoolEntry>;
  TexPoolMap::iterator FindMatching(const TextureConfig& config);

  TexPoolMap m_pool;
};
