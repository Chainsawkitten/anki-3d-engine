// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <anki/gr/vulkan/CommandBufferImpl.h>
#include <anki/gr/GrManager.h>
#include <anki/gr/vulkan/GrManagerImpl.h>

#include <anki/gr/Framebuffer.h>
#include <anki/gr/vulkan/FramebufferImpl.h>

#include <algorithm>

namespace anki
{

CommandBufferImpl::~CommandBufferImpl()
{
	if(m_empty)
	{
		ANKI_VK_LOGW("Command buffer was empty");
	}

	if(!m_finalized)
	{
		ANKI_VK_LOGW("Command buffer was not flushed");
	}

	m_imgBarriers.destroy(m_alloc);
	m_buffBarriers.destroy(m_alloc);
	m_queryResetAtoms.destroy(m_alloc);
	m_writeQueryAtoms.destroy(m_alloc);
	m_secondLevelAtoms.destroy(m_alloc);
}

Error CommandBufferImpl::init(const CommandBufferInitInfo& init)
{
	m_tid = Thread::getCurrentThreadId();
	m_flags = init.m_flags;

	ANKI_CHECK(getGrManagerImpl().getCommandBufferFactory().newCommandBuffer(m_tid, m_flags, m_microCmdb));
	m_handle = m_microCmdb->getHandle();

	m_alloc = m_microCmdb->getFastAllocator();

	// Store some of the init info for later
	if(!!(m_flags & CommandBufferFlag::SECOND_LEVEL))
	{
		m_activeFb = init.m_framebuffer;
		m_colorAttachmentUsages = init.m_colorAttachmentUsages;
		m_depthStencilAttachmentUsage = init.m_depthStencilAttachmentUsage;
		m_state.beginRenderPass(m_activeFb);
	}

	return Error::NONE;
}

void CommandBufferImpl::beginRecording()
{
	// Do the begin
	VkCommandBufferInheritanceInfo inheritance = {};
	inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;

	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin.pInheritanceInfo = &inheritance;

	if(!!(m_flags & CommandBufferFlag::SECOND_LEVEL))
	{
		FramebufferImpl& impl = static_cast<FramebufferImpl&>(*m_activeFb);
		impl.sync();

		// Calc the layouts
		Array<VkImageLayout, MAX_COLOR_ATTACHMENTS> colAttLayouts;
		for(U i = 0; i < impl.getColorAttachmentCount(); ++i)
		{
			colAttLayouts[i] = static_cast<const TextureImpl&>(
				*impl.getColorAttachment(
					i)).computeLayout(m_colorAttachmentUsages[i], 0);
		}

		VkImageLayout dsAttLayout = VK_IMAGE_LAYOUT_MAX_ENUM;
		if(impl.hasDepthStencil())
		{
			dsAttLayout = static_cast<const TextureImpl&>(*impl.getDepthStencilAttachment())
							  .computeLayout(m_depthStencilAttachmentUsage, 0);
		}

		inheritance.renderPass = impl.getRenderPassHandle(colAttLayouts, dsAttLayout);
		inheritance.subpass = 0;

		if(!impl.isDefaultFramebuffer())
		{
			inheritance.framebuffer = impl.getFramebufferHandle(0);
		}
		else
		{
			MicroSwapchainPtr swapchain;
			U32 backbufferIdx;
			impl.getDefaultFramebufferInfo(swapchain, backbufferIdx);

			inheritance.framebuffer = impl.getFramebufferHandle(backbufferIdx);
		}

		begin.flags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
	}

	vkBeginCommandBuffer(m_handle, &begin);
}

void CommandBufferImpl::beginRenderPass(FramebufferPtr fb,
	const Array<TextureUsageBit, MAX_COLOR_ATTACHMENTS>& colorAttachmentUsages,
	TextureUsageBit depthStencilAttachmentUsage,
	U32 minx,
	U32 miny,
	U32 width,
	U32 height)
{
	commandCommon();
	ANKI_ASSERT(!insideRenderPass());

	m_rpCommandCount = 0;
	m_activeFb = fb;

	FramebufferImpl& fbimpl = static_cast<FramebufferImpl&>(*fb);
	fbimpl.sync();

	U32 fbWidth, fbHeight;
	fbimpl.getAttachmentsSize(fbWidth, fbHeight);
	m_fbSize[0] = fbWidth;
	m_fbSize[1] = fbHeight;

	ANKI_ASSERT(minx < fbWidth && miny < fbHeight);

	const U32 maxx = min<U32>(minx + width, fbWidth);
	const U32 maxy = min<U32>(miny + height, fbHeight);
	width = maxx - minx;
	height = maxy - miny;
	ANKI_ASSERT(minx + width <= fbWidth && miny + height <= fbHeight);

	m_renderArea[0] = minx;
	m_renderArea[1] = miny;
	m_renderArea[2] = width;
	m_renderArea[3] = height;

	m_colorAttachmentUsages = colorAttachmentUsages;
	m_depthStencilAttachmentUsage = depthStencilAttachmentUsage;

	m_microCmdb->pushObjectRef(fb);

	m_subpassContents = VK_SUBPASS_CONTENTS_MAX_ENUM;

	// Re-set the viewport and scissor because sometimes they are set clamped
	m_viewportDirty = true;
	m_scissorDirty = true;
}

void CommandBufferImpl::beginRenderPassInternal()
{
	m_state.beginRenderPass(m_activeFb);

	FramebufferImpl& impl = static_cast<FramebufferImpl&>(*m_activeFb);

	VkRenderPassBeginInfo bi = {};
	bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	bi.clearValueCount = impl.getAttachmentCount();
	bi.pClearValues = impl.getClearValues();

	if(!impl.isDefaultFramebuffer())
	{
		// Bind a non-default FB

		bi.framebuffer = impl.getFramebufferHandle(0);

		// Calc the layouts
		Array<VkImageLayout, MAX_COLOR_ATTACHMENTS> colAttLayouts;
		for(U i = 0; i < impl.getColorAttachmentCount(); ++i)
		{
			colAttLayouts[i] = static_cast<const TextureImpl&>(
				*impl.getColorAttachment(
					i)).computeLayout(m_colorAttachmentUsages[i], 0);
		}

		VkImageLayout dsAttLayout = VK_IMAGE_LAYOUT_MAX_ENUM;
		if(impl.hasDepthStencil())
		{
			dsAttLayout = static_cast<const TextureImpl&>(*impl.getDepthStencilAttachment())
							  .computeLayout(m_depthStencilAttachmentUsage, 0);
		}

		bi.renderPass = impl.getRenderPassHandle(colAttLayouts, dsAttLayout);
	}
	else
	{
		// Bind the default FB
		m_renderedToDefaultFb = true;

		MicroSwapchainPtr swapchain;
		U32 backbufferIdx;
		impl.getDefaultFramebufferInfo(swapchain, backbufferIdx);

		bi.framebuffer = impl.getFramebufferHandle(backbufferIdx);
		bi.renderPass = impl.getRenderPassHandle({}, VK_IMAGE_LAYOUT_MAX_ENUM);

		// Perform the transition
		setImageBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			0,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			swapchain->m_images[backbufferIdx],
			VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
	}

	const Bool flipvp = flipViewport();
	bi.renderArea.offset.x = m_renderArea[0];
	if(flipvp)
	{
		ANKI_ASSERT(m_renderArea[3] <= m_fbSize[1]);
	}
	bi.renderArea.offset.y = (flipvp) ? m_fbSize[1] - (m_renderArea[1] + m_renderArea[3]) : m_renderArea[1];
	bi.renderArea.extent.width = m_renderArea[2];
	bi.renderArea.extent.height = m_renderArea[3];

	ANKI_CMD(vkCmdBeginRenderPass(m_handle, &bi, m_subpassContents), ANY_OTHER_COMMAND);
}

void CommandBufferImpl::endRenderPass()
{
	commandCommon();
	ANKI_ASSERT(insideRenderPass());
	if(m_rpCommandCount == 0)
	{
		m_subpassContents = VK_SUBPASS_CONTENTS_INLINE;
		beginRenderPassInternal();
	}

	ANKI_CMD(vkCmdEndRenderPass(m_handle), ANY_OTHER_COMMAND);

	// Default FB barrier/transition
	if(static_cast<const FramebufferImpl&>(*m_activeFb).isDefaultFramebuffer())
	{
		MicroSwapchainPtr swapchain;
		U32 backbufferIdx;
		static_cast<const FramebufferImpl&>(*m_activeFb).getDefaultFramebufferInfo(swapchain, backbufferIdx);

		setImageBarrier(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			swapchain->m_images[backbufferIdx],
			VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
	}

	m_activeFb.reset(nullptr);
	m_state.endRenderPass();

	// After pushing second level command buffers the state is undefined. Reset the tracker
	if(m_subpassContents == VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS)
	{
		m_state.reset();
	}
}

void CommandBufferImpl::endRecording()
{
	commandCommon();

	ANKI_ASSERT(!m_finalized);
	ANKI_ASSERT(!m_empty);

	ANKI_CMD(ANKI_VK_CHECKF(vkEndCommandBuffer(m_handle)), ANY_OTHER_COMMAND);
	m_finalized = true;

#if ANKI_EXTRA_CHECKS
	if(!!(m_flags & CommandBufferFlag::SMALL_BATCH))
	{
		if(m_commandCount > COMMAND_BUFFER_SMALL_BATCH_MAX_COMMANDS * 4)
		{
			ANKI_VK_LOGW("Command buffer has too many commands: %u", U(m_commandCount));
		}
	}
	else
	{
		if(m_commandCount <= COMMAND_BUFFER_SMALL_BATCH_MAX_COMMANDS / 4)
		{
			ANKI_VK_LOGW("Command buffer has too few commands: %u", U(m_commandCount));
		}
	}
#endif
}

void CommandBufferImpl::generateMipmaps2d(TexturePtr tex, U face, U layer)
{
	commandCommon();

	const TextureImpl& impl = static_cast<const TextureImpl&>(*tex);
	ANKI_ASSERT(impl.getTextureType() != TextureType::_3D && "Not for 3D");

	for(U i = 0; i < impl.getMipmapCount() - 1u; ++i)
	{
		// Transition source
		if(i > 0)
		{
			VkImageSubresourceRange range;
			impl.computeSubResourceRange(TextureSurfaceInfo(i, 0, face, layer), impl.m_akAspect, range);

			setImageBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				impl.m_imageHandle,
				range);
		}

		// Transition destination
		{
			VkImageSubresourceRange range;
			impl.computeSubResourceRange(TextureSurfaceInfo(i + 1, 0, face, layer), impl.m_akAspect, range);

			setImageBarrier(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				impl.m_imageHandle,
				range);
		}

		// Setup the blit struct
		I32 srcWidth = impl.getWidth() >> i;
		I32 srcHeight = impl.getHeight() >> i;

		I32 dstWidth = impl.getWidth() >> (i + 1);
		I32 dstHeight = impl.getHeight() >> (i + 1);

		ANKI_ASSERT(srcWidth > 0 && srcHeight > 0 && dstWidth > 0 && dstHeight > 0);

		U vkLayer = 0;
		switch(impl.getTextureType())
		{
		case TextureType::_2D:
		case TextureType::_2D_ARRAY:
			break;
		case TextureType::CUBE:
			vkLayer = face;
			break;
		case TextureType::CUBE_ARRAY:
			vkLayer = layer * 6 + face;
			break;
		default:
			ANKI_ASSERT(0);
			break;
		}

		VkImageBlit blit;
		blit.srcSubresource.aspectMask = impl.m_aspect;
		blit.srcSubresource.baseArrayLayer = vkLayer;
		blit.srcSubresource.layerCount = 1;
		blit.srcSubresource.mipLevel = i;
		blit.srcOffsets[0] = {0, 0, 0};
		blit.srcOffsets[1] = {srcWidth, srcHeight, 1};

		blit.dstSubresource.aspectMask = impl.m_aspect;
		blit.dstSubresource.baseArrayLayer = vkLayer;
		blit.dstSubresource.layerCount = 1;
		blit.dstSubresource.mipLevel = i + 1;
		blit.dstOffsets[0] = {0, 0, 0};
		blit.dstOffsets[1] = {dstWidth, dstHeight, 1};

		ANKI_CMD(vkCmdBlitImage(m_handle,
					 impl.m_imageHandle,
					 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					 impl.m_imageHandle,
					 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					 1,
					 &blit,
					 (impl.m_depthStencil) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR),
			ANY_OTHER_COMMAND);
	}

	// Hold the reference
	m_microCmdb->pushObjectRef(tex);
}

void CommandBufferImpl::flushBarriers()
{
	if(m_imgBarrierCount == 0 && m_buffBarrierCount == 0)
	{
		return;
	}

	// Sort
	//

	if(m_imgBarrierCount > 0)
	{
		std::sort(&m_imgBarriers[0],
			&m_imgBarriers[0] + m_imgBarrierCount,
			[](const VkImageMemoryBarrier& a, const VkImageMemoryBarrier& b) -> Bool {
				if(a.image != b.image)
				{
					return a.image < b.image;
				}

				if(a.subresourceRange.aspectMask != b.subresourceRange.aspectMask)
				{
					return a.subresourceRange.aspectMask < b.subresourceRange.aspectMask;
				}

				if(a.oldLayout != b.oldLayout)
				{
					return a.oldLayout < b.oldLayout;
				}

				if(a.newLayout != b.newLayout)
				{
					return a.newLayout < b.newLayout;
				}

				if(a.subresourceRange.baseArrayLayer != b.subresourceRange.baseArrayLayer)
				{
					return a.subresourceRange.baseArrayLayer < b.subresourceRange.baseArrayLayer;
				}

				if(a.subresourceRange.baseMipLevel != b.subresourceRange.baseMipLevel)
				{
					return a.subresourceRange.baseMipLevel < b.subresourceRange.baseMipLevel;
				}

				return false;
			});
	}

	// Batch
	//

	DynamicArrayAuto<VkImageMemoryBarrier> finalImgBarriers(m_alloc);
	U finalImgBarrierCount = 0;
	if(m_imgBarrierCount > 0)
	{
		DynamicArrayAuto<VkImageMemoryBarrier> squashedBarriers(m_alloc);
		U squashedBarrierCount = 0;

		squashedBarriers.create(m_imgBarrierCount);

		// Squash the mips by reducing the barriers
		for(U i = 0; i < m_imgBarrierCount; ++i)
		{
			const VkImageMemoryBarrier* prev = (i > 0) ? &m_imgBarriers[i - 1] : nullptr;
			const VkImageMemoryBarrier& crnt = m_imgBarriers[i];

			if(prev && prev->image == crnt.image
				&& prev->subresourceRange.aspectMask == crnt.subresourceRange.aspectMask
				&& prev->oldLayout == crnt.oldLayout
				&& prev->newLayout == crnt.newLayout
				&& prev->srcAccessMask == crnt.srcAccessMask
				&& prev->dstAccessMask == crnt.dstAccessMask
				&& prev->subresourceRange.baseMipLevel + prev->subresourceRange.levelCount
					== crnt.subresourceRange.baseMipLevel
				&& prev->subresourceRange.baseArrayLayer == crnt.subresourceRange.baseArrayLayer
				&& prev->subresourceRange.layerCount == crnt.subresourceRange.layerCount)
			{
				// Can batch
				squashedBarriers[squashedBarrierCount - 1].subresourceRange.levelCount +=
					crnt.subresourceRange.levelCount;
			}
			else
			{
				// Can't batch, create new barrier
				squashedBarriers[squashedBarrierCount++] = crnt;
			}
		}

		ANKI_ASSERT(squashedBarrierCount);

		// Squash the layers
		finalImgBarriers.create(squashedBarrierCount);

		for(U i = 0; i < squashedBarrierCount; ++i)
		{
			const VkImageMemoryBarrier* prev = (i > 0) ? &squashedBarriers[i - 1] : nullptr;
			const VkImageMemoryBarrier& crnt = squashedBarriers[i];

			if(prev && prev->image == crnt.image
				&& prev->subresourceRange.aspectMask == crnt.subresourceRange.aspectMask
				&& prev->oldLayout == crnt.oldLayout
				&& prev->newLayout == crnt.newLayout
				&& prev->srcAccessMask == crnt.srcAccessMask
				&& prev->dstAccessMask == crnt.dstAccessMask
				&& prev->subresourceRange.baseMipLevel == crnt.subresourceRange.baseMipLevel
				&& prev->subresourceRange.levelCount == crnt.subresourceRange.levelCount
				&& prev->subresourceRange.baseArrayLayer + prev->subresourceRange.layerCount
					== crnt.subresourceRange.baseArrayLayer)
			{
				// Can batch
				finalImgBarriers[finalImgBarrierCount - 1].subresourceRange.layerCount +=
					crnt.subresourceRange.layerCount;
			}
			else
			{
				// Can't batch, create new barrier
				finalImgBarriers[finalImgBarrierCount++] = crnt;
			}
		}

		ANKI_ASSERT(finalImgBarrierCount);
	}

	// Finish the job
	//
	vkCmdPipelineBarrier(m_handle,
		m_srcStageMask,
		m_dstStageMask,
		0,
		0,
		nullptr,
		m_buffBarrierCount,
		(m_buffBarrierCount) ? &m_buffBarriers[0] : nullptr,
		finalImgBarrierCount,
		(finalImgBarrierCount) ? &finalImgBarriers[0] : nullptr);

	ANKI_TRACE_INC_COUNTER(VK_PIPELINE_BARRIERS, 1);

	m_imgBarrierCount = 0;
	m_buffBarrierCount = 0;
	m_srcStageMask = 0;
	m_dstStageMask = 0;
}

void CommandBufferImpl::flushQueryResets()
{
	if(m_queryResetAtomCount == 0)
	{
		return;
	}

	std::sort(&m_queryResetAtoms[0],
		&m_queryResetAtoms[0] + m_queryResetAtomCount,
		[](const QueryResetAtom& a, const QueryResetAtom& b) -> Bool {
			if(a.m_pool != b.m_pool)
			{
				return a.m_pool < b.m_pool;
			}

			ANKI_ASSERT(a.m_queryIdx != b.m_queryIdx && "Tried to reset the same query more than once");
			return a.m_queryIdx < b.m_queryIdx;
		});

	U firstQuery = m_queryResetAtoms[0].m_queryIdx;
	U queryCount = 1;
	VkQueryPool pool = m_queryResetAtoms[0].m_pool;
	for(U i = 1; i < m_queryResetAtomCount; ++i)
	{
		const QueryResetAtom& crnt = m_queryResetAtoms[i];
		const QueryResetAtom& prev = m_queryResetAtoms[i - 1];

		if(crnt.m_pool == prev.m_pool && crnt.m_queryIdx == prev.m_queryIdx + 1)
		{
			// Can batch
			++queryCount;
		}
		else
		{
			// Flush batch
			vkCmdResetQueryPool(m_handle, pool, firstQuery, queryCount);

			// New batch
			firstQuery = crnt.m_queryIdx;
			queryCount = 1;
			pool = crnt.m_pool;
		}
	}

	vkCmdResetQueryPool(m_handle, pool, firstQuery, queryCount);

	m_queryResetAtomCount = 0;
}

void CommandBufferImpl::flushWriteQueryResults()
{
	if(m_writeQueryAtomCount == 0)
	{
		return;
	}

	std::sort(&m_writeQueryAtoms[0],
		&m_writeQueryAtoms[0] + m_writeQueryAtomCount,
		[](const WriteQueryAtom& a, const WriteQueryAtom& b) -> Bool {
			if(a.m_pool != b.m_pool)
			{
				return a.m_pool < b.m_pool;
			}

			if(a.m_buffer != b.m_buffer)
			{
				return a.m_buffer < b.m_buffer;
			}

			if(a.m_offset != b.m_offset)
			{
				return a.m_offset < b.m_offset;
			}

			ANKI_ASSERT(a.m_queryIdx != b.m_queryIdx && "Tried to write the same query more than once");
			return a.m_queryIdx < b.m_queryIdx;
		});

	U firstQuery = m_writeQueryAtoms[0].m_queryIdx;
	U queryCount = 1;
	VkQueryPool pool = m_writeQueryAtoms[0].m_pool;
	PtrSize offset = m_writeQueryAtoms[0].m_offset;
	VkBuffer buff = m_writeQueryAtoms[0].m_buffer;
	for(U i = 1; i < m_writeQueryAtomCount; ++i)
	{
		const WriteQueryAtom& crnt = m_writeQueryAtoms[i];
		const WriteQueryAtom& prev = m_writeQueryAtoms[i - 1];

		if(crnt.m_pool == prev.m_pool && crnt.m_buffer == prev.m_buffer && prev.m_queryIdx + 1 == crnt.m_queryIdx
			&& prev.m_offset + sizeof(U32) == crnt.m_offset)
		{
			// Can batch
			++queryCount;
		}
		else
		{
			// Flush batch
			vkCmdCopyQueryPoolResults(
				m_handle, pool, firstQuery, queryCount, buff, offset, sizeof(U32), VK_QUERY_RESULT_PARTIAL_BIT);

			// New batch
			firstQuery = crnt.m_queryIdx;
			queryCount = 1;
			pool = crnt.m_pool;
			buff = crnt.m_buffer;
		}
	}

	vkCmdCopyQueryPoolResults(
		m_handle, pool, firstQuery, queryCount, buff, offset, sizeof(U32), VK_QUERY_RESULT_PARTIAL_BIT);

	m_writeQueryAtomCount = 0;
}

void CommandBufferImpl::copyBufferToTextureSurface(
	BufferPtr buff, PtrSize offset, PtrSize range, TexturePtr tex, const TextureSurfaceInfo& surf)
{
	commandCommon();

	const TextureImpl& impl = static_cast<const TextureImpl&>(*tex);
	impl.checkSurfaceOrVolume(surf);
	ANKI_ASSERT(impl.usageValid(TextureUsageBit::TRANSFER_DESTINATION));
	const VkImageLayout layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

	if(!impl.m_workarounds)
	{
		U width = impl.getWidth() >> surf.m_level;
		U height = impl.getHeight() >> surf.m_level;
		ANKI_ASSERT(range == computeSurfaceSize(width, height, impl.getPixelFormat()));

		// Copy
		VkBufferImageCopy region;
		region.imageSubresource.aspectMask = impl.m_aspect;
		region.imageSubresource.baseArrayLayer = impl.computeVkArrayLayer(surf);
		region.imageSubresource.layerCount = 1;
		region.imageSubresource.mipLevel = surf.m_level;
		region.imageOffset = {0, 0, I32(surf.m_depth)};
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = 1;
		region.bufferOffset = offset;
		region.bufferImageHeight = 0;
		region.bufferRowLength = 0;

		ANKI_CMD(
			vkCmdCopyBufferToImage(
				m_handle, static_cast<const BufferImpl&>(*buff).getHandle(), impl.m_imageHandle, layout, 1, &region),
			ANY_OTHER_COMMAND);
	}
	else if(!!(impl.m_workarounds & TextureImplWorkaround::R8G8B8_TO_R8G8B8A8))
	{
		U width = impl.getWidth() >> surf.m_level;
		U height = impl.getHeight() >> surf.m_level;

		// Create a new shadow buffer
		const PtrSize shadowSize =
			computeSurfaceSize(width, height, PixelFormat(ComponentFormat::R8G8B8A8, TransformFormat::UNORM));
		BufferPtr shadow = getManager().newBuffer(
			BufferInitInfo(shadowSize, BufferUsageBit::TRANSFER_ALL, BufferMapAccessBit::NONE, "Workaround"));
		const VkBuffer shadowHandle = static_cast<const BufferImpl&>(*shadow).getHandle();
		m_microCmdb->pushObjectRef(shadow);

		// Create the copy regions
		DynamicArrayAuto<VkBufferCopy> copies(m_alloc);
		copies.create(width * height);
		U count = 0;
		for(U x = 0; x < width; ++x)
		{
			for(U y = 0; y < height; ++y)
			{
				VkBufferCopy& c = copies[count++];
				c.srcOffset = (y * width + x) * 3 + offset;
				c.dstOffset = (y * width + x) * 4 + 0;
				c.size = 3;
			}
		}

		// Copy buffer to buffer
		ANKI_CMD(vkCmdCopyBuffer(m_handle,
					 static_cast<const BufferImpl&>(*buff).getHandle(),
					 shadowHandle,
					 copies.getSize(),
					 &copies[0]),
			ANY_OTHER_COMMAND);

		// Set barrier
		setBufferBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			0,
			shadowSize,
			static_cast<const BufferImpl&>(*shadow).getHandle());

		// Do the copy to the image
		VkBufferImageCopy region;
		region.imageSubresource.aspectMask = impl.m_aspect;
		region.imageSubresource.baseArrayLayer = impl.computeVkArrayLayer(surf);
		region.imageSubresource.layerCount = 1;
		region.imageSubresource.mipLevel = surf.m_level;
		region.imageOffset = {0, 0, I32(surf.m_depth)};
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = 1;
		region.bufferOffset = 0;
		region.bufferImageHeight = 0;
		region.bufferRowLength = 0;

		ANKI_CMD(
			vkCmdCopyBufferToImage(m_handle, shadowHandle, impl.m_imageHandle, layout, 1, &region), ANY_OTHER_COMMAND);
	}
	else
	{
		ANKI_ASSERT(0);
	}

	m_microCmdb->pushObjectRef(tex);
	m_microCmdb->pushObjectRef(buff);
}

void CommandBufferImpl::copyBufferToTextureVolume(
	BufferPtr buff, PtrSize offset, PtrSize range, TexturePtr tex, const TextureVolumeInfo& vol)
{
	commandCommon();

	const TextureImpl& impl = static_cast<const TextureImpl&>(*tex);
	impl.checkSurfaceOrVolume(vol);
	ANKI_ASSERT(impl.usageValid(TextureUsageBit::TRANSFER_DESTINATION));
	const VkImageLayout layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

	if(!impl.m_workarounds)
	{
		U width = impl.getWidth() >> vol.m_level;
		U height = impl.getHeight() >> vol.m_level;
		U depth = impl.getDepth() >> vol.m_level;
		ANKI_ASSERT(range == computeVolumeSize(width, height, depth, impl.getPixelFormat()));

		// Copy
		VkBufferImageCopy region;
		region.imageSubresource.aspectMask = impl.m_aspect;
		region.imageSubresource.baseArrayLayer = impl.computeVkArrayLayer(vol);
		region.imageSubresource.layerCount = 1;
		region.imageSubresource.mipLevel = vol.m_level;
		region.imageOffset = {0, 0, 0};
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = depth;
		region.bufferOffset = offset;
		region.bufferImageHeight = 0;
		region.bufferRowLength = 0;

		ANKI_CMD(
			vkCmdCopyBufferToImage(
				m_handle, static_cast<const BufferImpl&>(*buff).getHandle(), impl.m_imageHandle, layout, 1, &region),
			ANY_OTHER_COMMAND);
	}
	else if(!!(impl.m_workarounds & TextureImplWorkaround::R8G8B8_TO_R8G8B8A8))
	{
		// Find the offset to the RGBA staging buff
		U width = impl.getWidth() >> vol.m_level;
		U height = impl.getHeight() >> vol.m_level;
		U depth = impl.getDepth() >> vol.m_level;

		// Create a new shadow buffer
		const PtrSize shadowSize =
			computeVolumeSize(width, height, depth, PixelFormat(ComponentFormat::R8G8B8A8, TransformFormat::UNORM));
		BufferPtr shadow = getManager().newBuffer(
			BufferInitInfo(shadowSize, BufferUsageBit::TRANSFER_ALL, BufferMapAccessBit::NONE, "Workaround"));
		const VkBuffer shadowHandle = static_cast<const BufferImpl&>(*shadow).getHandle();
		m_microCmdb->pushObjectRef(shadow);

		// Create the copy regions
		DynamicArrayAuto<VkBufferCopy> copies(m_alloc);
		copies.create(width * height * depth);
		U count = 0;
		for(U x = 0; x < width; ++x)
		{
			for(U y = 0; y < height; ++y)
			{
				for(U d = 0; d < depth; ++d)
				{
					VkBufferCopy& c = copies[count++];
					c.srcOffset = (d * height * width + y * width + x) * 3 + offset;
					c.dstOffset = (d * height * width + y * width + x) * 4 + 0;
					c.size = 3;
				}
			}
		}

		// Copy buffer to buffer
		ANKI_CMD(vkCmdCopyBuffer(m_handle,
					 static_cast<const BufferImpl&>(*buff).getHandle(),
					 shadowHandle,
					 copies.getSize(),
					 &copies[0]),
			ANY_OTHER_COMMAND);

		// Set barrier
		setBufferBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			0,
			shadowSize,
			shadowHandle);

		// Do the copy to the image
		VkBufferImageCopy region;
		region.imageSubresource.aspectMask = impl.m_aspect;
		region.imageSubresource.baseArrayLayer = impl.computeVkArrayLayer(vol);
		region.imageSubresource.layerCount = 1;
		region.imageSubresource.mipLevel = vol.m_level;
		region.imageOffset = {0, 0, 0};
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = depth;
		region.bufferOffset = 0;
		region.bufferImageHeight = 0;
		region.bufferRowLength = 0;

		ANKI_CMD(vkCmdCopyBufferToImage(
					 m_handle, shadowHandle, impl.m_imageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region),
			ANY_OTHER_COMMAND);
	}
	else
	{
		ANKI_ASSERT(0);
	}

	m_microCmdb->pushObjectRef(tex);
	m_microCmdb->pushObjectRef(buff);
}

} // end namespace anki
