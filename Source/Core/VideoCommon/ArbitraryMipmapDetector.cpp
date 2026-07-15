// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/ArbitraryMipmapDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "Common/Assert.h"
#include "VideoCommon/VideoConfig.h"

void ArbitraryMipmapDetector::AddLevel(u32 width, u32 height, u32 row_length, const u8* buffer)
{
  m_levels.push_back({{width, height, row_length}, buffer});
}

bool ArbitraryMipmapDetector::HasArbitraryMipmaps(u8* downsample_buffer) const
{
  if (m_levels.size() < 2)
    return false;

  if (!g_ActiveConfig.bArbitraryMipmapDetection)
    return false;

  const auto threshold = g_ActiveConfig.fArbitraryMipmapDetectionThreshold;

  auto* src = downsample_buffer;
  auto* dst = downsample_buffer + m_levels[1].shape.row_length * m_levels[1].shape.height * 4;

  float total_diff = 0.f;

  for (std::size_t i = 0; i < m_levels.size() - 1; ++i)
  {
    const auto& level = m_levels[i];
    const auto& mip = m_levels[i + 1];

    u64 level_pixel_count = level.shape.width;
    level_pixel_count *= level.shape.height;

    ASSERT(level_pixel_count < (std::numeric_limits<u64>::max() / (255 * 255 * 4)));

    Level::Downsample(i ? src : level.pixels, level.shape, dst, mip.shape);

    auto diff = mip.AverageDiff(dst);
    total_diff += diff;

    std::swap(src, dst);
  }

  auto all_levels = total_diff / (m_levels.size() - 1);
  return all_levels > threshold;
}

ArbitraryMipmapDetector::PixelRGBAu8 ArbitraryMipmapDetector::Level::SampleLinear(const u8* src,
                                                                                  const Shape& src_shape,
                                                                                  u32 x, u32 y)
{
  const auto* p = src + (x + y * src_shape.row_length) * 4;
  return {{p[0], p[1], p[2], p[3]}};
}

void ArbitraryMipmapDetector::Level::Downsample(const u8* src, const Shape& src_shape, u8* dst,
                                                const Shape& dst_shape)
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

float ArbitraryMipmapDetector::Level::AverageDiff(const u8* other) const
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
