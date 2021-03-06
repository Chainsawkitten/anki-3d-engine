// Copyright (C) 2009-2016, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <tests/framework/Framework.h>
#include <anki/core/Config.h>
#include <anki/util/HighRezTimer.h>
#include <anki/Ui.h>
#include <anki/Input.h>
#include <anki/core/StagingGpuMemoryManager.h>

namespace anki
{

static FramebufferPtr createDefaultFb(GrManager& gr)
{
	FramebufferInitInfo fbinit;
	fbinit.m_colorAttachmentCount = 1;
	fbinit.m_colorAttachments[0].m_clearValue.m_colorf = {{1.0, 0.0, 1.0, 1.0}};

	return gr.newFramebuffer(fbinit);
}

class Label : public UiImmediateModeBuilder
{
public:
	using UiImmediateModeBuilder::UiImmediateModeBuilder;

	void build(CanvasPtr canvas) final
	{
		nk_context* ctx = &canvas->getNkContext();

		if(nk_begin(ctx,
			   "Window name",
			   nk_rect(10, 10, 200, 800),
			   NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE))
		{
			nk_layout_row_dynamic(ctx, 30, 1);

			canvas->pushFont(canvas->getDefaultFont(), 10);
			nk_label(ctx, "Label0", NK_TEXT_ALIGN_LEFT);
			canvas->popFont();

			canvas->pushFont(canvas->getDefaultFont(), 60);
			nk_label(ctx, "Label1", NK_TEXT_ALIGN_LEFT);
			canvas->popFont();
		}

		nk_end(ctx);
	}
};

ANKI_TEST(Ui, Ui)
{
	Config cfg;
	initConfig(cfg);
	cfg.set("window.vsync", 1);
	cfg.set("window.debugContext", 0);

	NativeWindow* win = createWindow(cfg);
	Input* in = new Input();
	GrManager* gr = createGrManager(cfg, win);
	PhysicsWorld* physics;
	ResourceFilesystem* fs;
	ResourceManager* resource = createResourceManager(cfg, gr, physics, fs);
	UiManager* ui = new UiManager();

	ANKI_TEST_EXPECT_NO_ERR(in->init(win));

	StagingGpuMemoryManager* stagingMem = new StagingGpuMemoryManager();
	ANKI_TEST_EXPECT_NO_ERR(stagingMem->init(gr, cfg));

	HeapAllocator<U8> alloc(allocAligned, nullptr);
	ANKI_TEST_EXPECT_NO_ERR(ui->init(alloc, resource, gr, stagingMem, in));

	{
		FontPtr font;
		ANKI_TEST_EXPECT_NO_ERR(
			ui->newInstance(font, "engine_data/UbuntuRegular.ttf", std::initializer_list<U32>{10, 20, 30, 60}));

		CanvasPtr canvas;
		ANKI_TEST_EXPECT_NO_ERR(ui->newInstance(canvas, font, 30));

		IntrusivePtr<Label> label;
		ANKI_TEST_EXPECT_NO_ERR(ui->newInstance(label));

		FramebufferPtr fb = createDefaultFb(*gr);

		U iterations = 300;
		while(iterations--)
		{
			ANKI_TEST_EXPECT_NO_ERR(in->handleEvents());
			HighRezTimer timer;
			timer.start();

			canvas->handleInput();

			canvas->beginBuilding();
			label->build(canvas);
			canvas->endBuilding();

			gr->beginFrame();

			CommandBufferInitInfo cinit;
			cinit.m_flags = CommandBufferFlag::GRAPHICS_WORK | CommandBufferFlag::SMALL_BATCH;
			CommandBufferPtr cmdb = gr->newCommandBuffer(cinit);

			cmdb->beginRenderPass(fb, {{}}, {});
			canvas->appendToCommandBuffer(cmdb);
			cmdb->endRenderPass();
			cmdb->flush();

			gr->swapBuffers();
			stagingMem->endFrame();

			timer.stop();
			const F32 TICK = 1.0 / 30.0;
			if(timer.getElapsedTime() < TICK)
			{
				HighRezTimer::sleep(TICK - timer.getElapsedTime());
			}
		}
	}

	delete ui;
	delete stagingMem;
	delete resource;
	delete physics;
	delete fs;
	GrManager::deleteInstance(gr);
	delete in;
	delete win;
}

} // end namespace anki
