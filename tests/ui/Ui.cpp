// Copyright (C) 2009-2016, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <tests/framework/Framework.h>
#include <anki/core/Config.h>
#include <anki/util/HighRezTimer.h>
#include <anki/Ui.h>

namespace anki
{

static FramebufferPtr createDefaultFb(GrManager& gr)
{
	FramebufferInitInfo fbinit;
	fbinit.m_colorAttachmentCount = 1;
	fbinit.m_colorAttachments[0].m_clearValue.m_colorf = {{1.0, 0.0, 1.0, 1.0}};

	return gr.newInstance<Framebuffer>(fbinit);
}

class Label : public UiImmediateModeBuilder
{
public:
	void build(CanvasPtr canvas)
	{
		nk_context* ctx = &canvas->getContext();
		// TODO
	}
};

ANKI_TEST(Ui, Ui)
{
	Config cfg;
	initConfig(cfg);
	cfg.set("vsync", 1);

	NativeWindow* win = createWindow(cfg);
	GrManager* gr = createGrManager(cfg, win);
	PhysicsWorld* physics;
	ResourceFilesystem* fs;
	ResourceManager* resource = createResourceManager(cfg, gr, physics, fs);
	UiManager* ui = new UiManager();

	HeapAllocator<U8> alloc(allocAligned, nullptr);
	ANKI_TEST_EXPECT_NO_ERR(ui->init(alloc, resource, gr));

	{
		FontPtr font;
		ANKI_TEST_EXPECT_NO_ERR(ui->newInstance(font, "UbuntuRegular.ttf", 30));

		FramebufferPtr fb = createDefaultFb(*gr);

		U iterations = 200;
		while(iterations--)
		{
			HighRezTimer timer;
			timer.start();

			gr->beginFrame();

			CommandBufferInitInfo cinit;
			cinit.m_flags = CommandBufferFlag::GRAPHICS_WORK | CommandBufferFlag::SMALL_BATCH;
			CommandBufferPtr cmdb = gr->newInstance<CommandBuffer>(cinit);

			cmdb->beginRenderPass(fb);
			cmdb->endRenderPass();
			cmdb->flush();

			gr->swapBuffers();

			timer.stop();
			const F32 TICK = 1.0 / 30.0;
			if(timer.getElapsedTime() < TICK)
			{
				HighRezTimer::sleep(TICK - timer.getElapsedTime());
			}
		}
	}

	delete ui;
	delete resource;
	delete physics;
	delete fs;
	delete gr;
	delete win;
}

} // end namespace anki