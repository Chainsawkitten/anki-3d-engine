// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#pragma once

#include <anki/gr/vulkan/Common.h>

namespace anki
{

/// @addtogroup vulkan
/// @{

/// Halper class that extends the XXXImpl objects.
template<typename TBaseClass, typename TImplClass>
class VulkanObject
{
public:
	VkDevice getDevice() const;

	GrManagerImpl& getGrManagerImpl();

	const GrManagerImpl& getGrManagerImpl() const;

	/// Convenience method to allocate and initialize an XXXImpl.
	template<typename TGrManager, typename... TArgs>
	static ANKI_USE_RESULT TBaseClass* newInstanceHelper(TGrManager* manager, TArgs&&... args);
};

// Do this trick to avoid including heavy headers
#define ANKI_INSTANTIATE_GR_OBJECT(type_)                                \
	template<>                                                           \
	VkDevice VulkanObject<type_, type_##Impl>::getDevice() const;        \
	template<>                                                           \
	GrManagerImpl& VulkanObject<type_, type_##Impl>::getGrManagerImpl(); \
	template<>                                                           \
	const GrManagerImpl& VulkanObject<type_, type_##Impl>::getGrManagerImpl() const;
#define ANKI_INSTANTIATE_GR_OBJECT_DELIMITER()
#include <anki/gr/common/InstantiationMacros.h>
#undef ANKI_INSTANTIATE_GR_OBJECT_DELIMITER
#undef ANKI_INSTANTIATE_GR_OBJECT
/// @}

} // end namespace anki

#include <anki/gr/vulkan/VulkanObject.inl.h>
