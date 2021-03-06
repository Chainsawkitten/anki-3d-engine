// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <anki/gr/vulkan/FramebufferImpl.h>
#include <anki/gr/Framebuffer.h>
#include <anki/gr/vulkan/GrManagerImpl.h>
#include <anki/gr/vulkan/TextureImpl.h>

namespace anki
{

FramebufferImpl::~FramebufferImpl()
{
	if(m_noDflt.m_fb)
	{
		vkDestroyFramebuffer(getDevice(), m_noDflt.m_fb, nullptr);
	}

	for(auto it : m_noDflt.m_rpasses)
	{
		VkRenderPass rpass = it;
		ANKI_ASSERT(rpass);
		vkDestroyRenderPass(getDevice(), rpass, nullptr);
	}

	m_noDflt.m_rpasses.destroy(getAllocator());

	if(m_noDflt.m_compatibleRpass)
	{
		vkDestroyRenderPass(getDevice(), m_noDflt.m_compatibleRpass, nullptr);
	}
}

Error FramebufferImpl::init(const FramebufferInitInfo& init)
{
	// Init common
	m_defaultFb = init.refersToDefaultFramebuffer();
	strcpy(&m_name[0], (init.getName()) ? init.getName().cstr() : "");

	for(U i = 0; i < init.m_colorAttachmentCount; ++i)
	{
		m_colorAttachmentMask.set(i);
		m_colorAttCount = i + 1;
	}

	if(!m_defaultFb && init.m_depthStencilAttachment.m_texture)
	{
		const TextureImpl& tex = static_cast<const TextureImpl&>(*init.m_depthStencilAttachment.m_texture);

		if(!!(tex.m_workarounds & TextureImplWorkaround::S8_TO_D24S8))
		{
			m_aspect = DepthStencilAspectBit::STENCIL;
		}
		else if(tex.m_akAspect == DepthStencilAspectBit::DEPTH)
		{
			m_aspect = DepthStencilAspectBit::DEPTH;
		}
		else if(tex.m_akAspect == DepthStencilAspectBit::STENCIL)
		{
			m_aspect = DepthStencilAspectBit::STENCIL;
		}
		else
		{
			ANKI_ASSERT(!!init.m_depthStencilAttachment.m_aspect);
			m_aspect = init.m_depthStencilAttachment.m_aspect;
		}
	}

	initClearValues(init);

	if(m_defaultFb)
	{
		m_dflt.m_swapchain = getGrManagerImpl().getSwapchain();
		m_dflt.m_loadOp = convertLoadOp(init.m_colorAttachments[0].m_loadOperation);
	}
	else
	{
		// Create a renderpass.
		initRpassCreateInfo(init);
		ANKI_VK_CHECK(vkCreateRenderPass(getDevice(), &m_noDflt.m_rpassCi, nullptr, &m_noDflt.m_compatibleRpass));
		getGrManagerImpl().trySetVulkanHandleName(
			init.getName(), VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, m_noDflt.m_compatibleRpass);

		// Create the FB
		ANKI_CHECK(initFbs(init));
	}

	return Error::NONE;
}

void FramebufferImpl::initClearValues(const FramebufferInitInfo& init)
{
	for(U i = 0; i < m_colorAttCount; ++i)
	{
		if(init.m_colorAttachments[i].m_loadOperation == AttachmentLoadOperation::CLEAR)
		{
			F32* col = &m_clearVals[i].color.float32[0];
			col[0] = init.m_colorAttachments[i].m_clearValue.m_colorf[0];
			col[1] = init.m_colorAttachments[i].m_clearValue.m_colorf[1];
			col[2] = init.m_colorAttachments[i].m_clearValue.m_colorf[2];
			col[3] = init.m_colorAttachments[i].m_clearValue.m_colorf[3];
		}
		else
		{
			m_clearVals[i] = {};
		}
	}

	if(hasDepthStencil())
	{
		if(init.m_depthStencilAttachment.m_loadOperation == AttachmentLoadOperation::CLEAR
			|| init.m_depthStencilAttachment.m_stencilLoadOperation == AttachmentLoadOperation::CLEAR)
		{
			m_clearVals[m_colorAttCount].depthStencil.depth =
				init.m_depthStencilAttachment.m_clearValue.m_depthStencil.m_depth;

			m_clearVals[m_colorAttCount].depthStencil.stencil =
				init.m_depthStencilAttachment.m_clearValue.m_depthStencil.m_stencil;
		}
		else
		{
			m_clearVals[m_colorAttCount] = {};
		}
	}
}

Error FramebufferImpl::initFbs(const FramebufferInitInfo& init)
{
	VkFramebufferCreateInfo ci = {};
	ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	ci.renderPass = m_noDflt.m_compatibleRpass;
	ci.attachmentCount = init.m_colorAttachmentCount + ((hasDepthStencil()) ? 1 : 0);
	ci.layers = 1;

	Array<VkImageView, MAX_COLOR_ATTACHMENTS + 1> imgViews;
	U count = 0;

	for(U i = 0; i < init.m_colorAttachmentCount; ++i)
	{
		const FramebufferAttachmentInfo& att = init.m_colorAttachments[i];
		const TextureImpl& tex = static_cast<const TextureImpl&>(*att.m_texture);

		imgViews[count++] = tex.getOrCreateSingleSurfaceView(att.m_surface, att.m_aspect);

		if(m_noDflt.m_width == 0)
		{
			m_noDflt.m_width = tex.getWidth() >> att.m_surface.m_level;
			m_noDflt.m_height = tex.getHeight() >> att.m_surface.m_level;
		}

		m_noDflt.m_refs[i] = att.m_texture;
	}

	if(hasDepthStencil())
	{
		const FramebufferAttachmentInfo& att = init.m_depthStencilAttachment;
		const TextureImpl& tex = static_cast<const TextureImpl&>(*att.m_texture);

		imgViews[count++] = tex.getOrCreateSingleSurfaceView(att.m_surface, m_aspect);

		if(m_noDflt.m_width == 0)
		{
			m_noDflt.m_width = tex.getWidth() >> att.m_surface.m_level;
			m_noDflt.m_height = tex.getHeight() >> att.m_surface.m_level;
		}

		m_noDflt.m_refs[MAX_COLOR_ATTACHMENTS] = att.m_texture;
	}

	ci.width = m_noDflt.m_width;
	ci.height = m_noDflt.m_height;

	ci.pAttachments = &imgViews[0];
	ANKI_ASSERT(count == ci.attachmentCount);

	ANKI_VK_CHECK(vkCreateFramebuffer(getDevice(), &ci, nullptr, &m_noDflt.m_fb));
	getGrManagerImpl().trySetVulkanHandleName(
		init.getName(), VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT, m_noDflt.m_fb);

	return Error::NONE;
}

void FramebufferImpl::setupAttachmentDescriptor(
	const FramebufferAttachmentInfo& att, VkAttachmentDescription& desc, VkImageLayout layout) const
{
	desc = {};
	desc.format = convertFormat(att.m_texture->getPixelFormat());
	desc.samples = VK_SAMPLE_COUNT_1_BIT;
	desc.loadOp = convertLoadOp(att.m_loadOperation);
	desc.storeOp = convertStoreOp(att.m_storeOperation);
	desc.stencilLoadOp = convertLoadOp(att.m_stencilLoadOperation);
	desc.stencilStoreOp = convertStoreOp(att.m_stencilStoreOperation);
	desc.initialLayout = layout;
	desc.finalLayout = layout;
}

void FramebufferImpl::initRpassCreateInfo(const FramebufferInitInfo& init)
{
	// Setup attachments and references
	for(U i = 0; i < init.m_colorAttachmentCount; ++i)
	{
		setupAttachmentDescriptor(
			init.m_colorAttachments[i], m_noDflt.m_attachmentDescriptions[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		m_noDflt.m_references[i].attachment = i;
		m_noDflt.m_references[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	if(hasDepthStencil())
	{
		setupAttachmentDescriptor(init.m_depthStencilAttachment,
			m_noDflt.m_attachmentDescriptions[init.m_colorAttachmentCount],
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

		VkAttachmentReference& dsReference = m_noDflt.m_references[init.m_colorAttachmentCount];
		dsReference.attachment = init.m_colorAttachmentCount;
		dsReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	// Setup the render pass
	m_noDflt.m_rpassCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	m_noDflt.m_rpassCi.pAttachments = &m_noDflt.m_attachmentDescriptions[0];
	m_noDflt.m_rpassCi.attachmentCount = init.m_colorAttachmentCount + ((hasDepthStencil()) ? 1 : 0);

	// Subpass
	m_noDflt.m_subpassDescr.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	m_noDflt.m_subpassDescr.colorAttachmentCount = init.m_colorAttachmentCount;
	m_noDflt.m_subpassDescr.pColorAttachments = (init.m_colorAttachmentCount) ? &m_noDflt.m_references[0] : nullptr;
	m_noDflt.m_subpassDescr.pDepthStencilAttachment =
		(hasDepthStencil()) ? &m_noDflt.m_references[init.m_colorAttachmentCount] : nullptr;

	m_noDflt.m_rpassCi.subpassCount = 1;
	m_noDflt.m_rpassCi.pSubpasses = &m_noDflt.m_subpassDescr;
}

VkRenderPass FramebufferImpl::getRenderPassHandle(
	const Array<VkImageLayout, MAX_COLOR_ATTACHMENTS>& colorLayouts, VkImageLayout dsLayout)
{
	VkRenderPass out = {};

	if(!m_defaultFb)
	{
		// Create hash
		Array<VkImageLayout, MAX_COLOR_ATTACHMENTS + 1> allLayouts;
		U allLayoutCount = 0;
		for(U i = 0; i < m_colorAttCount; ++i)
		{
			ANKI_ASSERT(colorLayouts[i] != VK_IMAGE_LAYOUT_UNDEFINED);
			allLayouts[allLayoutCount++] = colorLayouts[i];
		}

		if(hasDepthStencil())
		{
			ANKI_ASSERT(dsLayout != VK_IMAGE_LAYOUT_UNDEFINED);
			allLayouts[allLayoutCount++] = dsLayout;
		}

		U64 hash = computeHash(&allLayouts[0], allLayoutCount * sizeof(allLayouts[0]));

		// Get or create
		LockGuard<Mutex> lock(m_noDflt.m_rpassesMtx);
		auto it = m_noDflt.m_rpasses.find(hash);
		if(it != m_noDflt.m_rpasses.getEnd())
		{
			out = *it;
		}
		else
		{
			// Create

			VkRenderPassCreateInfo ci = m_noDflt.m_rpassCi;
			Array<VkAttachmentDescription, MAX_COLOR_ATTACHMENTS + 1> attachmentDescriptions =
				m_noDflt.m_attachmentDescriptions;
			Array<VkAttachmentReference, MAX_COLOR_ATTACHMENTS + 1> references = m_noDflt.m_references;
			VkSubpassDescription subpassDescr = m_noDflt.m_subpassDescr;

			// Fix pointers
			subpassDescr.pColorAttachments = &references[0];
			ci.pAttachments = &attachmentDescriptions[0];
			ci.pSubpasses = &subpassDescr;

			for(U i = 0; i < subpassDescr.colorAttachmentCount; ++i)
			{
				const VkImageLayout lay = colorLayouts[i];
				ANKI_ASSERT(lay != VK_IMAGE_LAYOUT_UNDEFINED);

				attachmentDescriptions[i].initialLayout = lay;
				attachmentDescriptions[i].finalLayout = lay;

				references[i].layout = lay;
			}

			if(hasDepthStencil())
			{
				const U i = subpassDescr.colorAttachmentCount;
				const VkImageLayout lay = dsLayout;
				ANKI_ASSERT(lay != VK_IMAGE_LAYOUT_UNDEFINED);

				attachmentDescriptions[i].initialLayout = lay;
				attachmentDescriptions[i].finalLayout = lay;

				references[subpassDescr.colorAttachmentCount].layout = lay;
				subpassDescr.pDepthStencilAttachment = &references[subpassDescr.colorAttachmentCount];
			}

			ANKI_VK_CHECKF(vkCreateRenderPass(getDevice(), &ci, nullptr, &out));
			getGrManagerImpl().trySetVulkanHandleName(&m_name[0], VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, out);

			m_noDflt.m_rpasses.emplace(getAllocator(), hash, out);
		}
	}
	else
	{
		out = m_dflt.m_swapchain->getRenderPass(m_dflt.m_loadOp);
	}

	ANKI_ASSERT(out);
	return out;
}

void FramebufferImpl::sync()
{
	if(m_defaultFb)
	{
		LockGuard<SpinLock> lock(m_dflt.m_swapchainLock);
		m_dflt.m_swapchain = getGrManagerImpl().getSwapchain();
		m_dflt.m_currentBackbufferIndex = m_dflt.m_swapchain->m_currentBackbufferIndex;
	}
}

} // end namespace anki
