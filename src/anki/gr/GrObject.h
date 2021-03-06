// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#pragma once

#include <anki/gr/Common.h>
#include <anki/util/Atomic.h>
#include <anki/util/NonCopyable.h>

namespace anki
{

/// @addtogroup graphics
/// @{

/// Graphics object type.
enum GrObjectType : U16
{
	BUFFER,
	COMMAND_BUFFER,
	FRAMEBUFFER,
	OCCLUSION_QUERY,
	SAMPLER,
	SHADER,
	TEXTURE,
	SHADER_PROGRAM,
	FENCE,
	RENDER_GRAPH,
	COUNT
};

/// Base of all graphics objects.
class GrObject : public NonCopyable
{
public:
	GrObject(GrManager* manager, GrObjectType type);

	virtual ~GrObject();

	GrObjectType getType() const
	{
		return m_type;
	}

	GrManager& getManager()
	{
		return *m_manager;
	}

	const GrManager& getManager() const
	{
		return *m_manager;
	}

	GrAllocator<U8> getAllocator() const;

	Atomic<I32>& getRefcount()
	{
		return m_refcount;
	}

	/// A unique identifier for caching objects.
	U64 getUuid() const
	{
		return m_uuid;
	}

private:
	Atomic<I32> m_refcount;
	GrManager* m_manager;
	U64 m_uuid;
	GrObjectType m_type;
};
/// @}

} // end namespace anki
