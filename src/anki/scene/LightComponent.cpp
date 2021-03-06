// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <anki/scene/LightComponent.h>

namespace anki
{

LightComponent::LightComponent(SceneNode* node, LightComponentType type)
	: SceneComponent(CLASS_TYPE, node)
	, m_type(type)
{
	setInnerAngle(toRad(45.0));
	setOuterAngle(toRad(30.0));
	m_radius = 1.0;
}

Error LightComponent::update(SceneNode&, Second, Second, Bool& updated)
{
	updated = false;

	if(m_flags.get(DIRTY))
	{
		updated = true;
	}

	if(m_flags.get(TRF_DIRTY))
	{
		updated = true;

		if(m_type == LightComponentType::SPOT)
		{
			static const Mat4 biasMat4(0.5, 0.0, 0.0, 0.5, 0.0, 0.5, 0.0, 0.5, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
			Mat4 proj =
				Mat4::calculatePerspectiveProjectionMatrix(m_outerAngle, m_outerAngle, FRUSTUM_NEAR_PLANE, m_distance);
			m_spotTextureMatrix = biasMat4 * proj * Mat4(m_trf.getInverse());
		}
	}

	m_flags.unset(DIRTY | TRF_DIRTY);

	return Error::NONE;
}

} // end namespace anki
