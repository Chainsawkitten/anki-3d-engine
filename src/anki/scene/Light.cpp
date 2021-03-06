// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <anki/scene/Light.h>
#include <anki/scene/LensFlareComponent.h>
#include <anki/scene/MoveComponent.h>
#include <anki/scene/SpatialComponent.h>
#include <anki/scene/FrustumComponent.h>

namespace anki
{

/// Feedback component.
class Light::MovedFeedbackComponent : public SceneComponent
{
public:
	MovedFeedbackComponent(SceneNode* node)
		: SceneComponent(SceneComponentType::NONE, node)
	{
	}

	Error update(SceneNode& node, Second, Second, Bool& updated) override
	{
		updated = false;
		Light& lnode = static_cast<Light&>(node);

		const MoveComponent& move = node.getComponentAt<MoveComponent>(0);
		if(move.getTimestamp() == node.getGlobalTimestamp())
		{
			// Move updated
			lnode.onMoveUpdate(move);
		}

		return Error::NONE;
	}
};

/// Feedback component.
class Light::LightChangedFeedbackComponent : public SceneComponent
{
public:
	LightChangedFeedbackComponent(SceneNode* node)
		: SceneComponent(SceneComponentType::NONE, node)
	{
	}

	Error update(SceneNode& node, Second, Second, Bool& updated) override
	{
		updated = false;
		Light& lnode = static_cast<Light&>(node);

		LightComponent& light = node.getComponentAt<LightComponent>(getIndex() - 1);
		if(light.getTimestamp() == node.getGlobalTimestamp())
		{
			// Shape updated
			lnode.onShapeUpdate(light);
		}

		return Error::NONE;
	}
};

Light::Light(SceneGraph* scene, CString name)
	: SceneNode(scene, name)
{
}

Light::~Light()
{
}

Error Light::init(LightComponentType type, CollisionShape* shape)
{
	// Move component
	newComponent<MoveComponent>(this);

	// Feedback component
	newComponent<MovedFeedbackComponent>(this);

	// Light component
	newComponent<LightComponent>(this, type);

	// Feedback component
	newComponent<LightChangedFeedbackComponent>(this);

	// Spatial component
	newComponent<SpatialComponent>(this, shape);

	return Error::NONE;
}

void Light::frameUpdateCommon()
{
	// Update frustum comps shadow info
	const LightComponent& lc = getComponent<LightComponent>();
	Bool castsShadow = lc.getShadowEnabled();

	Error err = iterateComponentsOfType<FrustumComponent>([&](FrustumComponent& frc) -> Error {
		if(castsShadow)
		{
			frc.setEnabledVisibilityTests(FrustumComponentVisibilityTestFlag::SHADOW_CASTERS);
		}
		else
		{
			frc.setEnabledVisibilityTests(FrustumComponentVisibilityTestFlag::NONE);
		}

		return Error::NONE;
	});
	(void)err;
}

void Light::onMoveUpdateCommon(const MoveComponent& move)
{
	// Update the spatial
	SpatialComponent& sp = getComponent<SpatialComponent>();
	sp.markForUpdate();
	sp.setSpatialOrigin(move.getWorldTransform().getOrigin());

	// Update the lens flare
	LensFlareComponent* lf = tryGetComponent<LensFlareComponent>();
	if(lf)
	{
		lf->setWorldPosition(move.getWorldTransform().getOrigin());
	}

	// Update light component
	getComponent<LightComponent>().updateWorldTransform(move.getWorldTransform());
}

void Light::onShapeUpdateCommon(LightComponent& light)
{
	// Update the frustums
	Error err = iterateComponentsOfType<FrustumComponent>([&](FrustumComponent& fr) -> Error {
		fr.markShapeForUpdate();
		return Error::NONE;
	});

	(void)err;

	// Mark the spatial for update
	SpatialComponent& sp = getComponent<SpatialComponent>();
	sp.markForUpdate();
}

Error Light::loadLensFlare(const CString& filename)
{
	ANKI_ASSERT(tryGetComponent<LensFlareComponent>() == nullptr);

	LensFlareComponent* flareComp = newComponent<LensFlareComponent>(this);

	Error err = flareComp->init(filename);
	if(err)
	{
		ANKI_ASSERT(!"TODO: Remove component");
		return err;
	}

	return Error::NONE;
}

PointLight::PointLight(SceneGraph* scene, CString name)
	: Light(scene, name)
{
}

PointLight::~PointLight()
{
	m_shadowData.destroy(getSceneAllocator());
}

Error PointLight::init()
{
	return Light::init(LightComponentType::POINT, &m_sphereW);
}

void PointLight::onMoveUpdate(const MoveComponent& move)
{
	onMoveUpdateCommon(move);

	// Update the frustums
	U count = 0;
	Error err = iterateComponentsOfType<FrustumComponent>([&](FrustumComponent& fr) -> Error {
		Transform trf = m_shadowData[count].m_localTrf;
		trf.setOrigin(move.getWorldTransform().getOrigin());

		fr.getFrustum().resetTransform(trf);
		fr.markTransformForUpdate();
		++count;

		return Error::NONE;
	});

	(void)err;

	m_sphereW.setCenter(move.getWorldTransform().getOrigin());
}

void PointLight::onShapeUpdate(LightComponent& light)
{
	for(ShadowCombo& c : m_shadowData)
	{
		c.m_frustum.setFar(light.getRadius());
	}

	m_sphereW.setRadius(light.getRadius());

	onShapeUpdateCommon(light);
}

Error PointLight::frameUpdate(Second prevUpdateTime, Second crntTime)
{
	if(getComponent<LightComponent>().getShadowEnabled() && m_shadowData.isEmpty())
	{
		m_shadowData.create(getSceneAllocator(), 6);

		const F32 ang = toRad(90.0);
		const F32 dist = m_sphereW.getRadius();
		const F32 zNear = LightComponent::FRUSTUM_NEAR_PLANE;

		Mat3 rot;

		rot = Mat3(Euler(0.0, -PI / 2.0, 0.0)) * Mat3(Euler(0.0, 0.0, PI));
		m_shadowData[0].m_localTrf.setRotation(Mat3x4(rot));
		rot = Mat3(Euler(0.0, PI / 2.0, 0.0)) * Mat3(Euler(0.0, 0.0, PI));
		m_shadowData[1].m_localTrf.setRotation(Mat3x4(rot));
		rot = Mat3(Euler(PI / 2.0, 0.0, 0.0));
		m_shadowData[2].m_localTrf.setRotation(Mat3x4(rot));
		rot = Mat3(Euler(-PI / 2.0, 0.0, 0.0));
		m_shadowData[3].m_localTrf.setRotation(Mat3x4(rot));
		rot = Mat3(Euler(0.0, PI, 0.0)) * Mat3(Euler(0.0, 0.0, PI));
		m_shadowData[4].m_localTrf.setRotation(Mat3x4(rot));
		rot = Mat3(Euler(0.0, 0.0, PI));
		m_shadowData[5].m_localTrf.setRotation(Mat3x4(rot));

		const Vec4& origin = getComponent<MoveComponent>().getWorldTransform().getOrigin();
		for(U i = 0; i < 6; i++)
		{
			m_shadowData[i].m_frustum.setAll(ang, ang, zNear, dist);

			Transform trf = m_shadowData[i].m_localTrf;
			trf.setOrigin(origin);
			m_shadowData[i].m_frustum.resetTransform(trf);

			newComponent<FrustumComponent>(this, &m_shadowData[i].m_frustum);
		}
	}

	frameUpdateCommon();

	return Error::NONE;
}

SpotLight::SpotLight(SceneGraph* scene, CString name)
	: Light(scene, name)
{
}

Error SpotLight::init()
{
	ANKI_CHECK(Light::init(LightComponentType::SPOT, &m_frustum));

	FrustumComponent* fr = newComponent<FrustumComponent>(this, &m_frustum);
	fr->setEnabledVisibilityTests(FrustumComponentVisibilityTestFlag::NONE);

	return Error::NONE;
}

void SpotLight::onMoveUpdate(const MoveComponent& move)
{
	// Update the frustums
	Error err = iterateComponentsOfType<FrustumComponent>([&](FrustumComponent& fr) -> Error {
		fr.markTransformForUpdate();
		fr.getFrustum().resetTransform(move.getWorldTransform());

		return Error::NONE;
	});

	(void)err;

	onMoveUpdateCommon(move);
}

void SpotLight::onShapeUpdate(LightComponent& light)
{
	onShapeUpdateCommon(light);
	m_frustum.setAll(
		light.getOuterAngle(), light.getOuterAngle(), LightComponent::FRUSTUM_NEAR_PLANE, light.getDistance());
}

Error SpotLight::frameUpdate(Second prevUpdateTime, Second crntTime)
{
	frameUpdateCommon();
	return Error::NONE;
}

} // end namespace anki
