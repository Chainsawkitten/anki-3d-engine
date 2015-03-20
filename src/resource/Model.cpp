// Copyright (C) 2009-2015, Panagiotis Christopoulos Charitos.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include "anki/resource/Model.h"
#include "anki/resource/Material.h"
#include "anki/resource/Mesh.h"
#include "anki/resource/MeshLoader.h"
#include "anki/resource/ProgramResource.h"
#include "anki/misc/Xml.h"
#include "anki/util/Logger.h"
#include "anki/physics/PhysicsWorld.h"

namespace anki {

//==============================================================================
// ModelPatchBase                                                              =
//==============================================================================

//==============================================================================
ModelPatchBase::~ModelPatchBase()
{
	m_vertJobs.destroy(m_alloc);
	m_meshes.destroy(m_alloc);
}

//==============================================================================
Error ModelPatchBase::createVertexDesc(
	const ShaderHandle& prog,
	const Mesh& mesh,
	CommandBufferHandle& vertexJobs)
{
	Error err = ErrorCode::NONE;
	BufferHandle vbo;
	U32 size;
	GLenum type;
	U32 stride;
	U32 offset;
	Bool normalized;

	U count = 0;
	for(VertexAttribute attrib = VertexAttribute::POSITION;
		attrib < VertexAttribute::INDICES; ++attrib)
	{
		mesh.getBufferInfo(attrib, vbo, size, type,
			stride, offset, normalized);

		if(!vbo.isCreated())
		{
			continue;
		}
		
		vbo.bindVertexBuffer(vertexJobs, size, type, normalized, stride,
			offset, static_cast<U>(attrib));

		++count;
	}

	if(count < 1)
	{
		ANKI_LOGE("The mesh doesn't have any attributes");
		return ErrorCode::USER_DATA;
	}

	// The indices VBO
	mesh.getBufferInfo(VertexAttribute::INDICES, vbo, size, type,
			stride, offset, normalized);

	ANKI_ASSERT(vbo.isCreated());
	vbo.bindIndexBuffer(vertexJobs);

	return err;
}

//==============================================================================
Error ModelPatchBase::getRenderingDataSub(
	const RenderingKey& key, 
	CommandBufferHandle& vertJobs, 
	PipelineHandle& ppline,
	const U8* subMeshIndexArray, 
	U32 subMeshIndexCount,
	Array<U32, ANKI_GL_MAX_SUB_DRAWCALLS>& indicesCountArray,
	Array<PtrSize, ANKI_GL_MAX_SUB_DRAWCALLS>& indicesOffsetArray, 
	U32& drawcallCount) const
{
	Error err = ErrorCode::NONE;

	// Vertex descr
	vertJobs = m_vertJobs[getVertexDescIdx(key)];

	// Prog
	RenderingKey mtlKey = key;
	mtlKey.m_lod = 
		std::min(key.m_lod, (U8)(getMaterial().getLevelsOfDetail() - 1));

	ANKI_CHECK(m_mtl->getProgramPipeline(mtlKey, ppline));

	// Mesh and indices
	RenderingKey meshKey = key;
	meshKey.m_lod = std::min(key.m_lod, (U8)(getMeshesCount() - 1));

	const Mesh& mesh = getMesh(meshKey);

	if(subMeshIndexCount == 0 || subMeshIndexArray == nullptr
		|| mesh.getSubMeshesCount() == 0)
	{
		drawcallCount = 1;
		indicesOffsetArray[0] = 0;
		indicesCountArray[0] = mesh.getIndicesCount();
	}
	else
	{
		ANKI_ASSERT(subMeshIndexCount <= mesh.getSubMeshesCount());

		drawcallCount = 0;
		I prevIndex = -1;
		for(U i = 0; i < subMeshIndexCount; i++)
		{
			I index = (subMeshIndexArray == nullptr) 
				? (I)i
				: (I)subMeshIndexArray[i];
		
			// Check if we can merge with the previous submesh
			if(index > 0 && (index - 1) == prevIndex)
			{
				ANKI_ASSERT(drawcallCount > 0);

				// increase the indices count, leave offset alone
				U32 offset;
				indicesCountArray[drawcallCount - 1] +=
					mesh.getIndicesCountSub((U)index, offset);
			}
			else
			{
				U32 offset;
				indicesCountArray[drawcallCount] =
					mesh.getIndicesCountSub((U)index, offset);

				indicesOffsetArray[drawcallCount] = (PtrSize)offset;

				++drawcallCount;
			}

			prevIndex = index;
		}
	}

	return err;
}

//==============================================================================
Error ModelPatchBase::create(GrManager* gl)
{
	Error err = ErrorCode::NONE;

	const Material& mtl = getMaterial();
	U lodsCount = getLodsCount();

	ANKI_CHECK(m_vertJobs.create(m_alloc, lodsCount * mtl.getPassesCount()));

	for(U lod = 0; lod < lodsCount; ++lod)
	{
		for(U pass = 0; pass < mtl.getPassesCount(); ++pass)
		{
			RenderingKey key((Pass)pass, lod, false);
			ShaderHandle prog;
			const Mesh* mesh;

			// Get mesh
			ANKI_ASSERT(getMeshesCount() > 0);
			RenderingKey meshKey = key;
			meshKey.m_lod = std::min(key.m_lod, (U8)(getMeshesCount() - 1));
			mesh = &getMesh(meshKey);

			// Get shader prog
			ANKI_ASSERT(getMaterial().getLevelsOfDetail() > 0);
			RenderingKey shaderKey = key;
			shaderKey.m_lod = std::min(key.m_lod, 
				(U8)(getMaterial().getLevelsOfDetail() - 1));

			PipelineHandle ppline;
			ANKI_CHECK(m_mtl->getProgramPipeline(shaderKey, ppline));
			prog = ppline.getAttachedProgram(GL_VERTEX_SHADER);
			
			// Create vert descriptor
			CommandBufferHandle vertJobs;
			ANKI_CHECK(vertJobs.create(gl));
			ANKI_CHECK(createVertexDesc(prog, *mesh, vertJobs));

			m_vertJobs[getVertexDescIdx(key)] = vertJobs;
		}
	}

	return err;
}

//==============================================================================
U ModelPatchBase::getLodsCount() const
{
	U meshLods = getMeshesCount();
	U mtlLods = getMaterial().getLevelsOfDetail();
	return std::max(meshLods, mtlLods);
}

//==============================================================================
U ModelPatchBase::getVertexDescIdx(const RenderingKey& key) const
{
	U passesCount = getMaterial().getPassesCount();
	ANKI_ASSERT((U)key.m_pass < passesCount);

	// Sanitize LOD
	U lod = std::min((U)key.m_lod, getLodsCount() - 1);

	U idx = lod * passesCount + (U)key.m_pass;
	ANKI_ASSERT(idx < m_vertJobs.getSize());
	return idx;
}

//==============================================================================
// ModelPatch                                                                  =
//==============================================================================

//==============================================================================
template<typename MeshResourcePointerType>
Error ModelPatch<MeshResourcePointerType>::create(
	CString meshFNames[], 
	U32 meshesCount, 
	const CString& mtlFName,
	ResourceManager* resources)
{
	ANKI_ASSERT(meshesCount > 0);

	Error err = ErrorCode::NONE;

	ANKI_CHECK(m_meshes.create(m_alloc, meshesCount));
	ANKI_CHECK(m_meshResources.create(m_alloc, meshesCount));

	// Load meshes
	for(U i = 0; i < meshesCount; i++)
	{
		ANKI_CHECK(m_meshResources[i].load(meshFNames[i], resources));
		m_meshes[i] = m_meshResources[i].get();

		// Sanity check
		if(i > 0 && !m_meshResources[i]->isCompatible(*m_meshResources[i - 1]))
		{
			ANKI_LOGE("Meshes not compatible");
			return ErrorCode::USER_DATA;
		}
	}

	// Load material
	ANKI_CHECK(m_mtlResource.load(mtlFName, resources));
	m_mtl = m_mtlResource.get();

	// Create VAOs
	ANKI_CHECK(Base::create(&resources->getGrManager()));

	return err;
}

//==============================================================================
// Model                                                                       =
//==============================================================================

//==============================================================================
Model::Model(ResourceAllocator<U8>& alloc)
{}

//==============================================================================
Model::~Model()
{
	if(m_resources)
	{
		auto alloc = m_resources->_getAllocator();

		for(ModelPatchBase* patch : m_modelPatches)
		{
			alloc.deleteInstance(patch);
		}

		m_modelPatches.destroy(alloc);
	}
}

//==============================================================================
Error Model::load(const CString& filename, ResourceInitializer& init)
{
	Error err = ErrorCode::NONE;
	m_resources = &init.m_resources;
	auto alloc = init.m_alloc;

	// Load
	//
	XmlElement el;
	XmlDocument doc;
	ANKI_CHECK(doc.loadFile(filename, init.m_tempAlloc));

	XmlElement rootEl;
	ANKI_CHECK(doc.getChildElement("model", rootEl));

	// <collisionShape>
	XmlElement collEl;
	ANKI_CHECK(rootEl.getChildElementOptional("collisionShape", collEl));
	if(collEl)
	{
		ANKI_CHECK(collEl.getChildElement("type", el));
		CString type;
		ANKI_CHECK(el.getText(type));

		XmlElement valEl;
		ANKI_CHECK(collEl.getChildElement("value", valEl));

		PhysicsWorld& physics = m_resources->_getPhysicsWorld();
		PhysicsCollisionShape::Initializer csInit;

		if(type == "sphere")
		{
			F64 tmp;
			ANKI_CHECK(valEl.getF64(tmp));
			m_physicsShape = physics.newCollisionShape<PhysicsSphere>(
				csInit, tmp);
		}
		else if(type == "box")
		{
			Vec3 extend;
			if(err = valEl.getVec3(extend))
			{
				return err;
			}
			m_physicsShape = physics.newCollisionShape<PhysicsBox>(
				csInit, extend);
		}
		else if(type == "staticMesh")
		{
			CString filename;
			if(err = valEl.getText(filename))
			{
				return err;
			}
	
			TempResourceString fixedFilename;
			if(err = init.m_resources.fixResourceFilename(
				filename, fixedFilename))
			{
				return err;
			}

			MeshLoader loader;
			if(err = loader.load(init.m_tempAlloc, fixedFilename.toCString()))
			{
				fixedFilename.destroy(init.m_tempAlloc);
				return err;
			}

			fixedFilename.destroy(init.m_tempAlloc);

			m_physicsShape = physics.newCollisionShape<PhysicsTriangleSoup>(
				csInit, 
				reinterpret_cast<const Vec3*>(loader.getVertexData()), 
				loader.getVertexSize(),
				reinterpret_cast<const U16*>(loader.getIndexData()),
				loader.getHeader().m_totalIndicesCount);
		}
		else
		{
			ANKI_LOGE("Incorrect collision type");
			return ErrorCode::USER_DATA;
		}

		if(m_physicsShape == nullptr)
		{
			return ErrorCode::OUT_OF_MEMORY;
		}
	}

	// <modelPatches>
	XmlElement modelPatchesEl;
	ANKI_CHECK(rootEl.getChildElement("modelPatches", modelPatchesEl));

	XmlElement modelPatchEl;
	ANKI_CHECK(modelPatchesEl.getChildElement("modelPatch", modelPatchEl));

	// Count
	U count = 0;
	do
	{
		++count;
		// Move to next
		ANKI_CHECK(
			modelPatchEl.getNextSiblingElement("modelPatch", modelPatchEl));
	} while(modelPatchEl);

	// Check number of model patches
	if(count < 1)
	{
		ANKI_LOGE("Zero number of model patches");
		return ErrorCode::USER_DATA;
	}

	ANKI_CHECK(m_modelPatches.create(alloc, count));

	count = 0;
	ANKI_CHECK(modelPatchesEl.getChildElement("modelPatch", modelPatchEl));
	do
	{
		XmlElement materialEl;
		ANKI_CHECK(modelPatchEl.getChildElement("material", materialEl));

		Array<CString, 3> meshesFnames;
		U meshesCount = 1;
		ModelPatchBase* patch;

		// Try mesh
		XmlElement meshEl;
		ANKI_CHECK(modelPatchEl.getChildElementOptional("mesh", meshEl));
		if(meshEl)
		{
			XmlElement meshEl1;
			ANKI_CHECK(modelPatchEl.getChildElementOptional("mesh1", meshEl1));
			
			XmlElement meshEl2;
			ANKI_CHECK(modelPatchEl.getChildElementOptional("mesh2", meshEl2));

			ANKI_CHECK(meshEl.getText(meshesFnames[0]));

			if(meshEl1)
			{
				++meshesCount;
				ANKI_CHECK(meshEl1.getText(meshesFnames[1]));
			}

			if(meshEl2)
			{
				++meshesCount;
				ANKI_CHECK(meshEl2.getText(meshesFnames[2]));
			}

			CString cstr;
			ANKI_CHECK(materialEl.getText(cstr));
			ModelPatch<MeshResourcePointer>* mpatch = alloc.newInstance<
				ModelPatch<MeshResourcePointer>>(alloc);

			if(mpatch == nullptr)
			{
				return ErrorCode::OUT_OF_MEMORY;
			}

			ANKI_CHECK(mpatch->create(&meshesFnames[0], meshesCount, cstr,
				&init.m_resources));

			patch = mpatch;
		}
		else
		{
			XmlElement bmeshEl;
			ANKI_CHECK(modelPatchEl.getChildElement("bucketMesh", bmeshEl));

			XmlElement bmeshEl1;
			ANKI_CHECK(
				modelPatchEl.getChildElementOptional("bucketMesh1", bmeshEl1));

			XmlElement bmeshEl2;
			ANKI_CHECK(
				modelPatchEl.getChildElementOptional("bucketMesh2", bmeshEl2));

			ANKI_CHECK(bmeshEl.getText(meshesFnames[0]));

			if(bmeshEl1)
			{
				++meshesCount;
				ANKI_CHECK(bmeshEl1.getText(meshesFnames[1]));
			}

			if(bmeshEl2)
			{
				++meshesCount;
				ANKI_CHECK(bmeshEl2.getText(meshesFnames[2]));
			}

			CString cstr;
			ANKI_CHECK(materialEl.getText(cstr));

			ModelPatch<BucketMeshResourcePointer>* mpatch = 
				alloc.newInstance<
				ModelPatch<BucketMeshResourcePointer>>(alloc);

			if(mpatch == nullptr)
			{
				return ErrorCode::OUT_OF_MEMORY;
			}

			ANKI_CHECK(mpatch->create(&meshesFnames[0], meshesCount, cstr,
				&init.m_resources));

			patch  = mpatch;
		}

		m_modelPatches[count++] = patch;

		// Move to next
		ANKI_CHECK(
			modelPatchEl.getNextSiblingElement("modelPatch", modelPatchEl));
	} while(modelPatchEl);

	// Calculate compound bounding volume
	RenderingKey key;
	key.m_lod = 0;
	m_visibilityShape = m_modelPatches[0]->getMesh(key).getBoundingShape();

	for(auto it = m_modelPatches.begin() + 1;
		it != m_modelPatches.end();
		++it)
	{
		m_visibilityShape = m_visibilityShape.getCompoundShape(
			(*it)->getMesh(key).getBoundingShape());
	}

	return err;
}

} // end namespace anki
