// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <anki/gr/OcclusionQuery.h>
#include <anki/gr/vulkan/OcclusionQueryImpl.h>
#include <anki/gr/GrManager.h>

namespace anki
{

OcclusionQuery* OcclusionQuery::newInstance(GrManager* manager)
{
	return OcclusionQueryImpl::newInstanceHelper(manager);
}

} // end namespace anki
