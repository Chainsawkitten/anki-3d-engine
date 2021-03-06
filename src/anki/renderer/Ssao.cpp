// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <anki/renderer/Ssao.h>
#include <anki/renderer/Renderer.h>
#include <anki/renderer/GBuffer.h>
#include <anki/renderer/RenderQueue.h>
#include <anki/renderer/DepthDownscale.h>
#include <anki/util/Functions.h>
#include <anki/misc/ConfigSet.h>

namespace anki
{

const PixelFormat Ssao::RT_PIXEL_FORMAT(ComponentFormat::R8, TransformFormat::UNORM);

Ssao::~Ssao()
{
}

Error Ssao::initMain(const ConfigSet& config)
{
	// Noise
	ANKI_CHECK(getResourceManager().loadResource("engine_data/BlueNoiseLdrRgb64x64.ankitex", m_main.m_noiseTex));

	// Shader
	ANKI_CHECK(getResourceManager().loadResource("programs/Ssao.ankiprog", m_main.m_prog));

	ShaderProgramResourceConstantValueInitList<6> consts(m_main.m_prog);
	consts.add("NOISE_MAP_SIZE", U32(m_main.m_noiseTex->getWidth()))
		.add("FB_SIZE", UVec2(m_width, m_height))
		.add("RADIUS", 3.0f)
		.add("BIAS", 0.0f)
		.add("STRENGTH", 2.0f)
		.add("HISTORY_FEEDBACK", 1.0f / 4.0f);
	const ShaderProgramResourceVariant* variant;
	m_main.m_prog->getOrCreateVariant(consts.get(), variant);
	m_main.m_grProg = variant->getProgram();

	return Error::NONE;
}

Error Ssao::initHBlur(const ConfigSet& config)
{
	// shader
	ANKI_CHECK(m_r->getResourceManager().loadResource("programs/DepthAwareBlur.ankiprog", m_hblur.m_prog));

	ShaderProgramResourceMutationInitList<3> mutators(m_hblur.m_prog);
	mutators.add("HORIZONTAL", 1).add("KERNEL_SIZE", 9).add("COLOR_COMPONENTS", 1);
	ShaderProgramResourceConstantValueInitList<1> consts(m_hblur.m_prog);
	consts.add("TEXTURE_SIZE", UVec2(m_width, m_height));

	const ShaderProgramResourceVariant* variant;
	m_hblur.m_prog->getOrCreateVariant(mutators.get(), consts.get(), variant);

	m_hblur.m_grProg = variant->getProgram();

	return Error::NONE;
}

Error Ssao::initVBlur(const ConfigSet& config)
{
	// shader
	ANKI_CHECK(m_r->getResourceManager().loadResource("programs/DepthAwareBlur.ankiprog", m_vblur.m_prog));

	ShaderProgramResourceMutationInitList<3> mutators(m_vblur.m_prog);
	mutators.add("HORIZONTAL", 0).add("KERNEL_SIZE", 9).add("COLOR_COMPONENTS", 1);
	ShaderProgramResourceConstantValueInitList<1> consts(m_vblur.m_prog);
	consts.add("TEXTURE_SIZE", UVec2(m_width, m_height));

	const ShaderProgramResourceVariant* variant;
	m_vblur.m_prog->getOrCreateVariant(mutators.get(), consts.get(), variant);

	m_vblur.m_grProg = variant->getProgram();

	return Error::NONE;
}

Error Ssao::init(const ConfigSet& config)
{
	m_width = m_r->getWidth() / SSAO_FRACTION;
	m_height = m_r->getHeight() / SSAO_FRACTION;

	ANKI_R_LOGI("Initializing SSAO. Size %ux%u", m_width, m_height);

	static const Array<const char*, 2> RT_NAMES = {{"SsaoMain #1", "SsaoMain #2"}};
	for(U i = 0; i < 2; ++i)
	{
		// RT
		TextureInitInfo texinit = m_r->create2DRenderTargetInitInfo(m_width,
			m_height,
			Ssao::RT_PIXEL_FORMAT,
			TextureUsageBit::SAMPLED_FRAGMENT | TextureUsageBit::FRAMEBUFFER_ATTACHMENT_WRITE | TextureUsageBit::CLEAR,
			&RT_NAMES[i][0]);
		texinit.m_initialUsage = TextureUsageBit::SAMPLED_FRAGMENT;

		m_rtTextures[i] = m_r->createAndClearRenderTarget(texinit);
	}

	// FB descr
	m_fbDescr.m_colorAttachmentCount = 1;
	m_fbDescr.m_colorAttachments[0].m_loadOperation = AttachmentLoadOperation::DONT_CARE;
	m_fbDescr.bake();

	Error err = initMain(config);

	if(!err)
	{
		err = initHBlur(config);
	}

	if(!err)
	{
		err = initVBlur(config);
	}

	if(err)
	{
		ANKI_R_LOGE("Failed to init PPS SSAO");
	}

	return err;
}

void Ssao::runMain(const RenderingContext& ctx, RenderPassWorkContext& rgraphCtx)
{
	CommandBufferPtr& cmdb = rgraphCtx.m_commandBuffer;

	cmdb->setViewport(0, 0, m_width, m_height);
	cmdb->bindShaderProgram(m_main.m_grProg);

	rgraphCtx.bindTextureAndSampler(0, 0, m_r->getDepthDownscale().getQuarterColorRt(), m_r->getLinearSampler());
	rgraphCtx.bindTextureAndSampler(0, 1, m_r->getGBuffer().getColorRt(2), m_r->getLinearSampler());
	cmdb->bindTextureAndSampler(
		0, 2, m_main.m_noiseTex->getGrTexture(), m_r->getTrilinearRepeatSampler(), TextureUsageBit::SAMPLED_FRAGMENT);
	rgraphCtx.bindTextureAndSampler(0, 3, m_runCtx.m_rts[(m_r->getFrameCount() + 1) & 1], m_r->getLinearSampler());

	struct Unis
	{
		Vec4 m_unprojectionParams;
		Vec4 m_projectionMat;
		Vec4 m_noiseLayerPad3;
		Mat4 m_prevViewProjMatMulInvViewProjMat;
	};

	Unis* unis = allocateAndBindUniforms<Unis*>(sizeof(Unis), cmdb, 0, 0);
	const Mat4& pmat = ctx.m_renderQueue->m_projectionMatrix;
	unis->m_unprojectionParams = ctx.m_unprojParams;
	unis->m_projectionMat = Vec4(pmat(0, 0), pmat(1, 1), pmat(2, 2), pmat(2, 3));
	unis->m_noiseLayerPad3 = Vec4(m_r->getFrameCount() % m_main.m_noiseTex->getLayerCount(), 0.0, 0.0, 0.0);
	unis->m_prevViewProjMatMulInvViewProjMat =
		ctx.m_prevViewProjMat * ctx.m_renderQueue->m_viewProjectionMatrix.getInverse();

	drawQuad(cmdb);
}

void Ssao::runHBlur(RenderPassWorkContext& rgraphCtx)
{
	CommandBufferPtr& cmdb = rgraphCtx.m_commandBuffer;

	cmdb->setViewport(0, 0, m_width, m_height);
	cmdb->bindShaderProgram(m_hblur.m_grProg);
	rgraphCtx.bindTextureAndSampler(0, 0, m_runCtx.m_rts[m_r->getFrameCount() & 1], m_r->getLinearSampler());
	rgraphCtx.bindTextureAndSampler(0, 1, m_r->getDepthDownscale().getQuarterColorRt(), m_r->getLinearSampler());
	drawQuad(cmdb);
}

void Ssao::runVBlur(RenderPassWorkContext& rgraphCtx)
{
	CommandBufferPtr& cmdb = rgraphCtx.m_commandBuffer;

	cmdb->setViewport(0, 0, m_width, m_height);
	cmdb->bindShaderProgram(m_vblur.m_grProg);
	rgraphCtx.bindTextureAndSampler(0, 0, m_runCtx.m_rts[(m_r->getFrameCount() + 1) & 1], m_r->getLinearSampler());
	rgraphCtx.bindTextureAndSampler(0, 1, m_r->getDepthDownscale().getQuarterColorRt(), m_r->getLinearSampler());
	drawQuad(cmdb);
}

void Ssao::populateRenderGraph(RenderingContext& ctx)
{
	m_runCtx.m_ctx = &ctx;
	RenderGraphDescription& rgraph = ctx.m_renderGraphDescr;

	// Create RTs
	const U rtToRenderIdx = m_r->getFrameCount() & 1;
	m_runCtx.m_rts[rtToRenderIdx] =
		rgraph.importRenderTarget("SSAO #1", m_rtTextures[rtToRenderIdx], TextureUsageBit::NONE);
	const U rtToReadIdx = !rtToRenderIdx;
	m_runCtx.m_rts[rtToReadIdx] =
		rgraph.importRenderTarget("SSAO #2", m_rtTextures[rtToReadIdx], TextureUsageBit::SAMPLED_FRAGMENT);

	// Create main render pass
	{
		GraphicsRenderPassDescription& pass = rgraph.newGraphicsRenderPass("SSAO main");

		pass.setWork(runMainCallback, this, 0);
		pass.setFramebufferInfo(m_fbDescr, {{m_runCtx.m_rts[rtToRenderIdx]}}, {});

		pass.newConsumer({m_r->getGBuffer().getColorRt(2), TextureUsageBit::SAMPLED_FRAGMENT});
		pass.newConsumer({m_runCtx.m_rts[rtToRenderIdx], TextureUsageBit::FRAMEBUFFER_ATTACHMENT_WRITE});
		pass.newConsumer({m_runCtx.m_rts[rtToReadIdx], TextureUsageBit::SAMPLED_FRAGMENT});
		pass.newConsumer({m_r->getDepthDownscale().getQuarterColorRt(), TextureUsageBit::SAMPLED_FRAGMENT});
		pass.newProducer({m_runCtx.m_rts[rtToRenderIdx], TextureUsageBit::FRAMEBUFFER_ATTACHMENT_WRITE});
	}

	// Create HBlur pass
	{
		GraphicsRenderPassDescription& pass = rgraph.newGraphicsRenderPass("SSAO hblur");

		pass.setWork(runHBlurCallback, this, 0);
		pass.setFramebufferInfo(m_fbDescr, {{m_runCtx.m_rts[rtToReadIdx]}}, {});

		pass.newConsumer({m_runCtx.m_rts[rtToReadIdx], TextureUsageBit::FRAMEBUFFER_ATTACHMENT_WRITE});
		pass.newConsumer({m_runCtx.m_rts[rtToRenderIdx], TextureUsageBit::SAMPLED_FRAGMENT});
		pass.newConsumer({m_r->getDepthDownscale().getQuarterColorRt(), TextureUsageBit::SAMPLED_FRAGMENT});
		pass.newProducer({m_runCtx.m_rts[rtToReadIdx], TextureUsageBit::FRAMEBUFFER_ATTACHMENT_WRITE});
	}

	// Create VBlur pass
	{
		GraphicsRenderPassDescription& pass = rgraph.newGraphicsRenderPass("SSAO vblur");

		pass.setWork(runVBlurCallback, this, 0);
		pass.setFramebufferInfo(m_fbDescr, {{m_runCtx.m_rts[rtToRenderIdx]}}, {});

		pass.newConsumer({m_runCtx.m_rts[rtToRenderIdx], TextureUsageBit::FRAMEBUFFER_ATTACHMENT_WRITE});
		pass.newConsumer({m_runCtx.m_rts[rtToReadIdx], TextureUsageBit::SAMPLED_FRAGMENT});
		pass.newConsumer({m_r->getDepthDownscale().getQuarterColorRt(), TextureUsageBit::SAMPLED_FRAGMENT});
		pass.newProducer({m_runCtx.m_rts[rtToRenderIdx], TextureUsageBit::FRAMEBUFFER_ATTACHMENT_WRITE});
	}
}

RenderTargetHandle Ssao::getRt() const
{
	return m_runCtx.m_rts[m_r->getFrameCount() & 1];
}

} // end namespace anki
