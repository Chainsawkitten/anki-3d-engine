// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#pragma once

#include <anki/gr/GrObject.h>

namespace anki
{

/// @addtogroup graphics
/// @{

/// Texture initializer.
class alignas(4) TextureInitInfo : public GrBaseInitInfo
{
public:
	U32 m_width = 0;
	U32 m_height = 0;
	U32 m_depth = 1; //< Relevant only for 3D textures.
	U32 m_layerCount = 1; ///< Relevant only for texture arrays.

	TextureUsageBit m_usage = TextureUsageBit::NONE; ///< How the texture will be used.
	TextureUsageBit m_initialUsage = TextureUsageBit::NONE; ///< Its initial usage.
	TextureType m_type = TextureType::_2D;

	U8 m_mipmapsCount = 1;

	PixelFormat m_format;
	U8 m_samples = 1;

	U8 _m_padding = 0;

	TextureInitInfo() = default;

	TextureInitInfo(CString name)
		: GrBaseInitInfo(name)
	{
	}

	U64 computeHash() const
	{
		const U8* const first = reinterpret_cast<const U8* const>(&m_width);
		const U8* const last = reinterpret_cast<const U8* const>(&m_samples) + sizeof(m_samples);
		const U size = last - first;
		ANKI_ASSERT(size
			== sizeof(U32) * 4 + sizeof(TextureUsageBit) * 2 + sizeof(TextureType) + sizeof(U8) + sizeof(PixelFormat)
				+ sizeof(U8));
		return anki::computeHash(first, size);
	}
};

/// GPU texture.
class Texture : public GrObject
{
	ANKI_GR_OBJECT

public:
	static const GrObjectType CLASS_TYPE = GrObjectType::TEXTURE;

	U32 getWidth() const
	{
		ANKI_ASSERT(m_width);
		return m_width;
	}

	U32 getHeight() const
	{
		ANKI_ASSERT(m_height);
		return m_height;
	}

	U32 getDepth() const
	{
		ANKI_ASSERT(m_depth);
		return m_depth;
	}

	U32 getLayercount() const
	{
		ANKI_ASSERT(m_layerCount);
		return m_layerCount;
	}

	U32 getMipmapCount() const
	{
		ANKI_ASSERT(m_mipCount);
		return m_mipCount;
	}

	TextureType getTextureType() const
	{
		ANKI_ASSERT(m_texType != TextureType::COUNT);
		return m_texType;
	}

	TextureUsageBit getTextureUsage() const
	{
		ANKI_ASSERT(!!m_usage);
		return m_usage;
	}

	const PixelFormat& getPixelFormat() const
	{
		return m_format;
	}

protected:
	U32 m_width = 0;
	U32 m_height = 0;
	U32 m_depth = 0;
	U32 m_layerCount = 0;
	U32 m_mipCount = 0;
	TextureType m_texType = TextureType::COUNT;
	TextureUsageBit m_usage = TextureUsageBit::NONE;
	PixelFormat m_format;

	/// Construct.
	Texture(GrManager* manager)
		: GrObject(manager, CLASS_TYPE)
	{
	}

	/// Destroy.
	~Texture()
	{
	}

private:
	/// Allocate and initialize new instance.
	static ANKI_USE_RESULT Texture* newInstance(GrManager* manager, const TextureInitInfo& init);
};
/// @}

} // end namespace anki
