// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <vector>

#include "Common/CommonTypes.h"

class ArbitraryMipmapDetector
{
private:
  using PixelRGBAu8 = std::array<u8, 4>;

public:
  explicit ArbitraryMipmapDetector() = default;

  void AddLevel(u32 width, u32 height, u32 row_length, const u8* buffer);
  bool HasArbitraryMipmaps(u8* downsample_buffer) const;

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

    static PixelRGBAu8 SampleLinear(const u8* src, const Shape& src_shape, u32 x, u32 y);
    static void Downsample(const u8* src, const Shape& src_shape, u8* dst, const Shape& dst_shape);
    float AverageDiff(const u8* other) const;
  };

  std::vector<Level> m_levels;
};
