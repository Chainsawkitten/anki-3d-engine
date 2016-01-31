// Copyright (C) 2009-2016, Panagiotis Christopoulos Charitos.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <anki/core/Config.h>

namespace anki
{

//==============================================================================
Config::Config()
{
	//
	// Renderer
	//

	newOption("is.sm.enabled", true);
	newOption("is.sm.poissonEnabled", true);
	newOption("is.sm.bilinearEnabled", true);
	newOption("is.sm.resolution", 512);
	newOption("is.sm.maxLights", 4);

	newOption("is.groundLightEnabled", true);
	newOption("is.maxPointLights", 384);
	newOption("is.maxSpotLights", 16);
	newOption("is.maxSpotTexLights", 8);
	newOption("is.maxLightsPerCluster", 8);

	newOption("lf.maxSpritesPerFlare", 8);
	newOption("lf.maxFlares", 16);

	newOption("bloom.enabled", true);
	newOption("bloom.renderingQuality", 0.5);
	newOption("bloom.blurringDist", 1.0);
	newOption("bloom.samples", 5);
	newOption("bloom.blurringIterationsCount", 1);
	newOption("bloom.threshold", 1.0);
	newOption("bloom.scale", 2.0);

	newOption("ssao.enabled", true);
	newOption("ssao.renderingQuality", 0.3);
	newOption("ssao.blurringIterationsCount", 1);

	newOption("sslf.enabled", true);

	newOption("pps.enabled", true);
	newOption("pps.sharpen", true);
	newOption("pps.gammaCorrection", true);

	newOption("ir.enabled", true);
	newOption("ir.rendererSize", 128);
	newOption("ir.cubemapTextureArraySize", 16);
	newOption("sslr.enabled", true);
	newOption("sslr.startRoughnes", 0.2);

	newOption("dbg.enabled", false);
	newOption("tm.enabled", true);

	// Globals
	newOption("width", 0);
	newOption("height", 0);
	newOption("renderingQuality", 1.0); // Applies only to MainRenderer
	newOption("lodDistance", 10.0); // Distance that used to calculate the LOD
	newOption("samples", 1);
	newOption("tessellation", true);
	newOption("sceneFrameAllocatorSize", 1024 * 1024);
	newOption("clusterSizeZ", 32);
	newOption("imageReflectionMaxDistance", 30.0);

	//
	// GR
	//
	newOption("gr.frameUniformsSize", 1024 * 1024 * 16);
	newOption("gr.frameStorageSize", 1024 * 1024 * 16);
	newOption("gr.frameVertexSize", 1024 * 1024 * 2);
	newOption("gr.frameTransferSize", 1024 * 1024 * 32);

	//
	// Resource
	//
	newOption("maxTextureSize", 1024 * 1024);
	newOption("textureAnisotropy", 8);
	newOption("dataPaths", ".");

	//
	// Window
	//
	newOption("glminor", 5);
	newOption("glmajor", 4);
	newOption("fullscreenDesktopResolution", false);
	newOption("debugContext",
#if ANKI_DEBUG == 1
		true
#else
		false
#endif
		);
}

//==============================================================================
Config::~Config()
{
}

} // end namespace anki
