// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#pragma once

#include <anki/ui/UiObject.h>
#include <anki/gr/CommandBuffer.h>
#include <anki/resource/ShaderProgramResource.h>

namespace anki
{

/// @addtogroup ui
/// @{

/// UI canvas.
class Canvas : public UiObject
{
public:
	Canvas(UiManager* manager);

	virtual ~Canvas();

	ANKI_USE_RESULT Error init(FontPtr font, U32 fontHeight);

	FontPtr getDefaultFont() const
	{
		return m_font;
	}

	nk_context& getNkContext()
	{
		return m_nkCtx;
	}

	/// Handle input.
	virtual void handleInput();

	/// @name building commands
	/// @{

	/// Begin building the UI.
	void beginBuilding();

	/// End building.
	void endBuilding();

	void pushFont(FontPtr font, U32 fontHeight);

	void popFont()
	{
		ANKI_ASSERT(m_building);
		nk_style_pop_font(&m_nkCtx);
	}
	/// @}

	void appendToCommandBuffer(CommandBufferPtr cmdb);

private:
	FontPtr m_font;
	nk_context m_nkCtx = {};
	nk_buffer m_nkCmdsBuff = {};

	enum SHADER_TYPE
	{
		NO_TEX,
		RGBA_TEX,
		SHADER_COUNT
	};

	ShaderProgramResourcePtr m_prog;
	Array<ShaderProgramPtr, SHADER_COUNT> m_grProgs;
	SamplerPtr m_sampler;

	StackAllocator<U8> m_stackAlloc;

	List<IntrusivePtr<UiObject>> m_references;

#if ANKI_EXTRA_CHECKS
	Bool8 m_building = false;
#endif

	void reset();
};
/// @}

} // end namespace anki
