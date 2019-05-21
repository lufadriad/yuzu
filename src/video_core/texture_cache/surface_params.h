// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>

#include "common/alignment.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/shader/shader_ir.h"
#include "video_core/surface.h"
#include "video_core/textures/decoders.h"

namespace VideoCommon {

using VideoCore::Surface::SurfaceCompression;

class SurfaceParams {
public:
    /// Creates SurfaceCachedParams from a texture configuration.
    static SurfaceParams CreateForTexture(Core::System& system,
                                          const Tegra::Texture::FullTextureInfo& config,
                                          const VideoCommon::Shader::Sampler& entry);

    /// Creates SurfaceCachedParams for a depth buffer configuration.
    static SurfaceParams CreateForDepthBuffer(
        Core::System& system, u32 zeta_width, u32 zeta_height, Tegra::DepthFormat format,
        u32 block_width, u32 block_height, u32 block_depth,
        Tegra::Engines::Maxwell3D::Regs::InvMemoryLayout type);

    /// Creates SurfaceCachedParams from a framebuffer configuration.
    static SurfaceParams CreateForFramebuffer(Core::System& system, std::size_t index);

    /// Creates SurfaceCachedParams from a Fermi2D surface configuration.
    static SurfaceParams CreateForFermiCopySurface(
        const Tegra::Engines::Fermi2D::Regs::Surface& config);

    std::size_t Hash() const;

    bool operator==(const SurfaceParams& rhs) const;

    bool operator!=(const SurfaceParams& rhs) const {
        return !operator==(rhs);
    }

    std::size_t GetGuestSizeInBytes() const {
        return GetInnerMemorySize(false, false, false);
    }

    std::size_t GetHostSizeInBytes() const {
        std::size_t host_size_in_bytes;
        if (GetCompressionType() == SurfaceCompression::Converted) {
            constexpr std::size_t rgb8_bpp = 4ULL;
            // ASTC is uncompressed in software, in emulated as RGBA8
            host_size_in_bytes = 0;
            for (u32 level = 0; level < num_levels; ++level) {
                host_size_in_bytes += GetConvertedMipmapSize(level);
            }
        } else {
            host_size_in_bytes = GetInnerMemorySize(true, false, false);
        }
        return host_size_in_bytes;
    }

    u32 GetBlockAlignedWidth() const {
        return Common::AlignUp(width, 64 / GetBytesPerPixel());
    }

    /// Returns the width of a given mipmap level.
    u32 GetMipWidth(u32 level) const {
        return std::max(1U, width >> level);
    }

    /// Returns the height of a given mipmap level.
    u32 GetMipHeight(u32 level) const {
        return std::max(1U, height >> level);
    }

    /// Returns the depth of a given mipmap level.
    u32 GetMipDepth(u32 level) const {
        return is_layered ? depth : std::max(1U, depth >> level);
    }

    /// Returns the block height of a given mipmap level.
    u32 GetMipBlockHeight(u32 level) const;

    /// Returns the block depth of a given mipmap level.
    u32 GetMipBlockDepth(u32 level) const;

    u32 GetRowAlignment(u32 level) const {
        const u32 bpp =
            GetCompressionType() == SurfaceCompression::Converted ? 4 : GetBytesPerPixel();
        return 1U << Common::CountTrailingZeroes32(GetMipWidth(level) * bpp);
    }

    // Helper used for out of class size calculations
    static std::size_t AlignLayered(const std::size_t out_size, const u32 block_height,
                                    const u32 block_depth) {
        return Common::AlignBits(out_size,
                                 Tegra::Texture::GetGOBSizeShift() + block_height + block_depth);
    }

    /// Returns the offset in bytes in guest memory of a given mipmap level.
    std::size_t GetGuestMipmapLevelOffset(u32 level) const;

    /// Returns the offset in bytes in host memory (linear) of a given mipmap level.
    std::size_t GetHostMipmapLevelOffset(u32 level) const;

    std::size_t GetConvertedMipmapOffset(u32 level) const;

    /// Returns the size in bytes in guest memory of a given mipmap level.
    std::size_t GetGuestMipmapSize(u32 level) const;

    /// Returns the size in bytes in host memory (linear) of a given mipmap level.
    std::size_t GetHostMipmapSize(u32 level) const;

    std::size_t GetConvertedMipmapSize(u32 level) const;

    /// Returns the size of a layer in bytes in guest memory.
    std::size_t GetGuestLayerSize() const;

    /// Returns the size of a layer in bytes in host memory for a given mipmap level.
    std::size_t GetHostLayerSize(u32 level) const;

    static u32 ConvertWidth(u32 width, VideoCore::Surface::PixelFormat pixel_format_from,
                            VideoCore::Surface::PixelFormat pixel_format_to) {
        const u32 bw1 = VideoCore::Surface::GetDefaultBlockWidth(pixel_format_from);
        const u32 bw2 = VideoCore::Surface::GetDefaultBlockWidth(pixel_format_to);
        return (width * bw2 + bw1 - 1) / bw1;
    }

    static u32 ConvertHeight(u32 height, VideoCore::Surface::PixelFormat pixel_format_from,
                             VideoCore::Surface::PixelFormat pixel_format_to) {
        const u32 bh1 = VideoCore::Surface::GetDefaultBlockHeight(pixel_format_from);
        const u32 bh2 = VideoCore::Surface::GetDefaultBlockHeight(pixel_format_to);
        return (height * bh2 + bh1 - 1) / bh1;
    }

    /// Returns the default block width.
    u32 GetDefaultBlockWidth() const {
        return VideoCore::Surface::GetDefaultBlockWidth(pixel_format);
    }

    /// Returns the default block height.
    u32 GetDefaultBlockHeight() const {
        return VideoCore::Surface::GetDefaultBlockHeight(pixel_format);
    }

    /// Returns the bits per pixel.
    u32 GetBitsPerPixel() const {
        return VideoCore::Surface::GetFormatBpp(pixel_format);
    }

    /// Returns the bytes per pixel.
    u32 GetBytesPerPixel() const {
        return VideoCore::Surface::GetBytesPerPixel(pixel_format);
    }

    /// Returns true if the pixel format is a depth and/or stencil format.
    bool IsPixelFormatZeta() const;

    SurfaceCompression GetCompressionType() const {
        return VideoCore::Surface::GetFormatCompressionType(pixel_format);
    }

    bool IsBuffer() const {
        return target == VideoCore::Surface::SurfaceTarget::TextureBuffer;
    }

    std::string TargetName() const;

    bool is_tiled;
    bool srgb_conversion;
    bool is_layered;
    u32 block_width;
    u32 block_height;
    u32 block_depth;
    u32 tile_width_spacing;
    u32 width;
    u32 height;
    u32 depth;
    u32 pitch;
    u32 num_levels;
    VideoCore::Surface::PixelFormat pixel_format;
    VideoCore::Surface::ComponentType component_type;
    VideoCore::Surface::SurfaceType type;
    VideoCore::Surface::SurfaceTarget target;

private:
    /// Returns the size of a given mipmap level inside a layer.
    std::size_t GetInnerMipmapMemorySize(u32 level, bool as_host_size, bool uncompressed) const;

    /// Returns the size of all mipmap levels and aligns as needed.
    std::size_t GetInnerMemorySize(bool as_host_size, bool layer_only, bool uncompressed) const;

    /// Returns the size of a layer
    std::size_t GetLayerSize(bool as_host_size, bool uncompressed) const;

    std::size_t GetNumLayers() const {
        return is_layered ? depth : 1;
    }

    /// Returns true if these parameters are from a layered surface.
    bool IsLayered() const;
};

} // namespace VideoCommon

namespace std {

template <>
struct hash<VideoCommon::SurfaceParams> {
    std::size_t operator()(const VideoCommon::SurfaceParams& k) const noexcept {
        return k.Hash();
    }
};

} // namespace std