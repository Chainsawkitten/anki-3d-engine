// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#pragma once

#include <anki/gr/Sampler.h>
#include <anki/gr/gl/GlObject.h>

namespace anki
{

/// @addtogroup opengl
/// @{

/// Sampler GL object.
class SamplerImpl final : public Sampler, public GlObject
{
public:
	SamplerImpl(GrManager* manager)
		: Sampler(manager)
	{
	}

	~SamplerImpl()
	{
		destroyDeferred(getManager(), glDeleteSamplers);
	}

	void init(const SamplerInitInfo& sinit);
};
/// @}

} // end namespace anki
