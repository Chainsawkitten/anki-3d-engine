// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#pragma once

#include <anki/scene/SceneNode.h>
#include <anki/collision/Obb.h>

namespace anki
{

/// @addtogroup scene
/// @{

/// Node that has a decal component.
class DecalNode : public SceneNode
{
	friend class DecalMoveFeedbackComponent;
	friend class DecalShapeFeedbackComponent;

public:
	DecalNode(SceneGraph* scene, CString name)
		: SceneNode(scene, name)
	{
	}

	~DecalNode();

	ANKI_USE_RESULT Error init();

private:
	void onMove(MoveComponent& movec);
	void onDecalUpdated();
};
/// @}

} // end namespace anki
