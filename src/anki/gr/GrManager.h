// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#pragma once

#include <anki/gr/Common.h>
#include <anki/gr/GrObject.h>
#include <anki/util/String.h>

namespace anki
{

// Forward
class ConfigSet;
class NativeWindow;

/// @addtogroup graphics
/// @{

/// Manager initializer.
class GrManagerInitInfo
{
public:
	AllocAlignedCallback m_allocCallback = nullptr;
	void* m_allocCallbackUserData = nullptr;

	CString m_cacheDirectory;

	const ConfigSet* m_config = nullptr;
	NativeWindow* m_window = nullptr;
};

/// The graphics manager, owner of all graphics objects.
class GrManager
{
public:
	/// Create.
	static ANKI_USE_RESULT Error newInstance(GrManagerInitInfo& init, GrManager*& gr);

	/// Destroy.
	static void deleteInstance(GrManager* gr);

	/// Begin frame.
	void beginFrame();

	/// Swap buffers
	void swapBuffers();

	/// Wait for all work to finish.
	void finish();

	/// @name Object creation methods. They are thread-safe.
	/// @{
	ANKI_USE_RESULT BufferPtr newBuffer(const BufferInitInfo& init);
	ANKI_USE_RESULT TexturePtr newTexture(const TextureInitInfo& init);
	ANKI_USE_RESULT SamplerPtr newSampler(const SamplerInitInfo& init);
	ANKI_USE_RESULT ShaderPtr newShader(const ShaderInitInfo& init);
	ANKI_USE_RESULT ShaderProgramPtr newShaderProgram(const ShaderProgramInitInfo& init);
	ANKI_USE_RESULT CommandBufferPtr newCommandBuffer(const CommandBufferInitInfo& init);
	ANKI_USE_RESULT FramebufferPtr newFramebuffer(const FramebufferInitInfo& init);
	ANKI_USE_RESULT OcclusionQueryPtr newOcclusionQuery();
	ANKI_USE_RESULT RenderGraphPtr newRenderGraph();
	/// @}

	/// Call this before calling allocateFrameTransientMemory or tryAllocateFrameTransientMemory to get the exact memory
	/// that will be required for the CommandBuffer::uploadTextureSurface.
	///
	/// If the expectedTransientAllocationSize is greater than the expected you are required to allocate that amount and
	/// write your pixels to be uploaded to the first part of the memory as before and leave the rest of the memory for
	/// internal use.
	void getTextureSurfaceUploadInfo(
		TexturePtr tex, const TextureSurfaceInfo& surf, PtrSize& expectedTransientAllocationSize);

	/// Same as getTextureSurfaceUploadInfo but for volumes.
	void getTextureVolumeUploadInfo(
		TexturePtr tex, const TextureVolumeInfo& vol, PtrSize& expectedTransientAllocationSize);

	/// Get some buffer alignment and size info.
	void getUniformBufferInfo(U32& bindOffsetAlignment, PtrSize& maxUniformBlockSize) const;

	/// Get some buffer alignment info.
	void getStorageBufferInfo(U32& bindOffsetAlignment, PtrSize& maxStorageBlockSize) const;

	/// Get some buffer alignment info.
	void getTextureBufferInfo(U32& bindOffsetAlignment, PtrSize& maxRange) const;

anki_internal:
	GrAllocator<U8>& getAllocator()
	{
		return m_alloc;
	}

	GrAllocator<U8> getAllocator() const
	{
		return m_alloc;
	}

	CString getCacheDirectory() const
	{
		return m_cacheDir.toCString();
	}

	U64& getUuidIndex()
	{
		return m_uuidIndex;
	}

protected:
	GrAllocator<U8> m_alloc; ///< Keep it first to get deleted last
	String m_cacheDir;
	U64 m_uuidIndex = 1;

	GrManager();

	virtual ~GrManager();
};
/// @}

} // end namespace anki
