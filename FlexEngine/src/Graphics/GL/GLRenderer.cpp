#include "stdafx.hpp"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLRenderer.hpp"

#include <array>
#include <algorithm>
#include <string>
#include <utility>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "FreeCamera.hpp"
#include "Graphics/GL/GLHelpers.hpp"
#include "Logger.hpp"
#include "Window/Window.hpp"
#include "Window/GLFWWindowWrapper.hpp"
#include "VertexAttribute.hpp"
#include "GameContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Helpers.hpp"

namespace flex
{
	namespace gl
	{
		GLRenderer::GLRenderer(GameContext& gameContext) :
			m_HDREquirectangularCubemapCaptureSize(512, 512)
		{
			UNREFERENCED_PARAMETER(gameContext);

			CheckGLErrorMessages();

			LoadShaders();

			CheckGLErrorMessages();

			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			CheckGLErrorMessages();

			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glFrontFace(GL_CCW);
			CheckGLErrorMessages();

			// HDR Equirectangular Cube
			glGenFramebuffers(1, &m_CaptureFBO);
			glGenRenderbuffers(1, &m_CaptureRBO);
			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			// TODO: Remove?
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_HDREquirectangularCubemapCaptureSize.x, m_HDREquirectangularCubemapCaptureSize.y);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_CaptureRBO);
			CheckGLErrorMessages();

			// Prevent seams from appearing on lower mip map levels of cubemaps
			glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);


			m_CaptureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
			m_CaptureViews =
			{
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
			};
		}

		GLRenderer::~GLRenderer()
		{
			CheckGLErrorMessages();

			if (m_1x1_NDC_QuadVertexBufferData.pDataStart)
			{
				m_1x1_NDC_QuadVertexBufferData.Destroy();
			}

			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				Destroy(i);
				CheckGLErrorMessages();
			}
			m_RenderObjects.clear();
			CheckGLErrorMessages();

			m_gBufferQuadVertexBufferData.Destroy();

			glfwTerminate();
		}

		MaterialID GLRenderer::InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo)
		{
			UNREFERENCED_PARAMETER(gameContext);

			CheckGLErrorMessages();

			m_Materials.push_back({});
			GLMaterial& mat = m_Materials.back();
			mat.material = {};
			mat.material.name = createInfo->name;

			const MaterialID materialID = m_Materials.size() - 1;

			if (!GetShaderID(createInfo->shaderName, mat.material.shaderID))
			{
				if (createInfo->shaderName.empty())
				{
					Logger::LogError("Material's shader name not set! MaterialCreateInfo::shaderName must be filled in");
				}
				else
				{
					Logger::LogError("Material's shader not set! (material: " + createInfo->name + ", shader: " + createInfo->shaderName + ")");
				}
			}

			glUseProgram(m_Shaders[mat.material.shaderID].program);
			CheckGLErrorMessages();

			// TODO: Is this really needed? (do things dynamically instead?)
			UniformInfo uniformInfo[] = {
				{ "model", 							&mat.uniformIDs.model },
				{ "modelInvTranspose", 				&mat.uniformIDs.modelInvTranspose },
				{ "modelViewProjection",			&mat.uniformIDs.modelViewProjection },
				{ "view", 							&mat.uniformIDs.view },
				{ "viewInv", 						&mat.uniformIDs.viewInv },
				{ "viewProjection", 				&mat.uniformIDs.viewProjection },
				{ "projection", 					&mat.uniformIDs.projection },
				{ "camPos", 						&mat.uniformIDs.camPos },
				{ "enableDiffuseSampler", 			&mat.uniformIDs.enableDiffuseTexture },
				{ "enableNormalSampler", 			&mat.uniformIDs.enableNormalTexture },
				{ "enableSpecularSampler", 			&mat.uniformIDs.enableSpecularTexture },
				{ "enableCubemapSampler", 			&mat.uniformIDs.enableCubemapTexture },
				{ "enableAlbedoSampler", 			&mat.uniformIDs.enableAlbedoSampler },
				{ "constAlbedo", 					&mat.uniformIDs.constAlbedo },
				{ "enableMetallicSampler", 			&mat.uniformIDs.enableMetallicSampler },
				{ "constMetallic", 					&mat.uniformIDs.constMetallic },
				{ "enableRoughnessSampler", 		&mat.uniformIDs.enableRoughnessSampler },
				{ "constRoughness", 				&mat.uniformIDs.constRoughness },
				{ "enableAOSampler",				&mat.uniformIDs.enableAOSampler },
				{ "constAO",						&mat.uniformIDs.constAO },
				{ "hdrEquirectangularSampler",		&mat.uniformIDs.hdrEquirectangularSampler },
				{ "enableIrradianceSampler",		&mat.uniformIDs.enableIrradianceSampler },
			};

			const glm::uint uniformCount = sizeof(uniformInfo) / sizeof(uniformInfo[0]);

			for (size_t i = 0; i < uniformCount; ++i)
			{
				if (m_Shaders[mat.material.shaderID].shader.dynamicBufferUniforms.HasUniform(uniformInfo[i].name) ||
					m_Shaders[mat.material.shaderID].shader.constantBufferUniforms.HasUniform(uniformInfo[i].name))
				{
					*uniformInfo[i].id = glGetUniformLocation(m_Shaders[mat.material.shaderID].program, uniformInfo[i].name);
					if (*uniformInfo[i].id == -1) Logger::LogWarning(std::string(uniformInfo[i].name) + " was not found for material " + createInfo->name + " (shader " + createInfo->shaderName + ")");
				}
			}

			CheckGLErrorMessages();

			mat.material.diffuseTexturePath = createInfo->diffuseTexturePath;
			mat.material.generateDiffuseSampler = createInfo->generateDiffuseSampler;
			mat.material.enableDiffuseSampler = createInfo->enableDiffuseSampler;

			mat.material.normalTexturePath = createInfo->normalTexturePath;
			mat.material.generateNormalSampler = createInfo->generateNormalSampler;
			mat.material.enableNormalSampler = createInfo->enableNormalSampler;

			mat.material.specularTexturePath = createInfo->specularTexturePath;
			mat.material.generateSpecularSampler = createInfo->generateSpecularSampler;
			mat.material.enableSpecularSampler = createInfo->enableSpecularSampler;

			mat.material.enablePositionFrameBufferSampler = createInfo->enablePositionFrameBufferSampler;
			mat.material.enableNormalFrameBufferSampler = createInfo->enableNormalFrameBufferSampler;
			mat.material.enableDiffuseSpecularFrameBufferSampler = createInfo->enableDiffuseSpecularFrameBufferSampler;

			mat.material.enableCubemapSampler = createInfo->enableCubemapSampler;
			mat.material.generateCubemapSampler = createInfo->generateCubemapSampler;
			mat.material.cubemapSamplerSize = createInfo->generatedCubemapSize;
			mat.material.cubeMapFilePaths = createInfo->cubeMapFilePaths;

			mat.material.constAlbedo = glm::vec4(createInfo->constAlbedo, 0);
			mat.material.generateAlbedoSampler = createInfo->generateAlbedoSampler;
			mat.material.albedoTexturePath = createInfo->albedoTexturePath;
			mat.material.enableAlbedoSampler = createInfo->enableAlbedoSampler;

			mat.material.constMetallic = createInfo->constMetallic;
			mat.material.generateMetallicSampler = createInfo->generateMetallicSampler;
			mat.material.metallicTexturePath = createInfo->metallicTexturePath;
			mat.material.enableMetallicSampler = createInfo->enableMetallicSampler;

			mat.material.constRoughness = createInfo->constRoughness;
			mat.material.generateRoughnessSampler = createInfo->generateRoughnessSampler;
			mat.material.roughnessTexturePath = createInfo->roughnessTexturePath;
			mat.material.enableRoughnessSampler = createInfo->enableRoughnessSampler;

			mat.material.constAO = createInfo->constAO;
			mat.material.generateAOSampler = createInfo->generateAOSampler;
			mat.material.aoTexturePath = createInfo->aoTexturePath;
			mat.material.enableAOSampler = createInfo->enableAOSampler;

			mat.material.enableHDREquirectangularSampler = createInfo->enableHDREquirectangularSampler;
			mat.material.generateHDREquirectangularSampler = createInfo->generateHDREquirectangularSampler;
			mat.material.hdrEquirectangularTexturePath = createInfo->hdrEquirectangularTexturePath;

			mat.material.enableHDRCubemapSampler = createInfo->enableHDRCubemapSampler;
			mat.material.generateHDRCubemapSampler = createInfo->generateHDRCubemapSampler;

			mat.material.enableIrradianceSampler = createInfo->enableIrradianceSampler;
			mat.material.generateIrradianceSampler = createInfo->generateIrradianceSampler;
			mat.material.irradianceSamplerSize = createInfo->generatedIrradianceCubemapSize;

			if (m_Shaders[mat.material.shaderID].shader.needIrradianceSampler)
			{
				mat.irradianceSamplerID = (createInfo->irradianceSamplerMatID < m_Materials.size() ?
					m_Materials[createInfo->irradianceSamplerMatID].irradianceSamplerID : 0);
			}
			if (m_Shaders[mat.material.shaderID].shader.needBRDFLUT)
			{
				mat.brdfLUTSamplerID = (createInfo->brdfLUTSamplerMatID < m_Materials.size() ?
					m_Materials[createInfo->brdfLUTSamplerMatID].brdfLUTSamplerID : 0);
			}
			if (m_Shaders[mat.material.shaderID].shader.needPrefilteredMap)
			{
				mat.prefilteredMapSamplerID = (createInfo->prefilterMapSamplerMatID < m_Materials.size() ?
					m_Materials[createInfo->prefilterMapSamplerMatID].prefilteredMapSamplerID : 0);
			}

			mat.material.enablePrefilteredMap = createInfo->enablePrefilteredMap;
			mat.material.generatePrefilteredMap = createInfo->generatePrefilteredMap;
			mat.material.prefilteredMapSize = createInfo->generatedPrefilteredCubemapSize;

			mat.material.enableBRDFLUT = createInfo->enableBRDFLUT;
			mat.material.generateBRDFLUT = createInfo->generateBRDFLUT;
			mat.material.generatedBRDFLUTSize = createInfo->generatedBRDFLUTSize;

			struct SamplerCreateInfo
			{
				bool needed;
				bool create;
				glm::uint* id;
				std::string filepath;
				std::string textureName;
				std::function<bool(glm::uint&, const std::string&, bool)> createFunction;
			};

			// Samplers that need to be loaded from file, and their GL counter parts generated (glGenTextures)
			SamplerCreateInfo samplerCreateInfos[] =
			{
				{ m_Shaders[mat.material.shaderID].shader.needAlbedoSampler,
				mat.material.generateAlbedoSampler, &mat.albedoSamplerID, 
				createInfo->albedoTexturePath, "albedoSampler", GenerateGLTexture },
				{ m_Shaders[mat.material.shaderID].shader.needMetallicSampler,  
				mat.material.generateMetallicSampler, &mat.metallicSamplerID, 
				createInfo->metallicTexturePath, "metallicSampler", GenerateGLTexture },
				{ m_Shaders[mat.material.shaderID].shader.needRoughnessSampler, 
				mat.material.generateRoughnessSampler, &mat.roughnessSamplerID, 
				createInfo->roughnessTexturePath, "roughnessSampler" , GenerateGLTexture },
				{ m_Shaders[mat.material.shaderID].shader.needAOSampler, 
				mat.material.generateAOSampler, &mat.aoSamplerID, 
				createInfo->aoTexturePath, "aoSampler", GenerateGLTexture },
				{ m_Shaders[mat.material.shaderID].shader.needDiffuseSampler, 
				mat.material.generateDiffuseSampler, &mat.diffuseSamplerID, 
				createInfo->diffuseTexturePath, "diffuseSampler", GenerateGLTexture },
				{ m_Shaders[mat.material.shaderID].shader.needNormalSampler, 
				mat.material.generateNormalSampler, &mat.normalSamplerID, 
				createInfo->normalTexturePath, "normalSampler", GenerateGLTexture },
				{ m_Shaders[mat.material.shaderID].shader.needSpecularSampler, 
				mat.material.generateSpecularSampler, &mat.specularSamplerID, 
				createInfo->specularTexturePath, "specularSampler", GenerateGLTexture },
				{ m_Shaders[mat.material.shaderID].shader.needHDREquirectangularSampler, 
				mat.material.generateHDREquirectangularSampler, &mat.hdrTextureID, 
				createInfo->hdrEquirectangularTexturePath, "hdrEquirectangularSampler", GenerateHDRGLTexture },
			};

			int binding = 0;

			for (SamplerCreateInfo& samplerCreateInfo : samplerCreateInfos)
			{
				if (samplerCreateInfo.needed)
				{
					if (samplerCreateInfo.create)
					{
						// TODO: Generate mip maps? (add member to SamplerCreateInfo if needed)
						samplerCreateInfo.createFunction(*samplerCreateInfo.id, samplerCreateInfo.filepath, false);
						int uniformLocation = glGetUniformLocation(m_Shaders[mat.material.shaderID].program, samplerCreateInfo.textureName.c_str());
						CheckGLErrorMessages();
						if (uniformLocation == -1)
						{
							Logger::LogWarning(samplerCreateInfo.textureName + " was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].shader.name + ")");
						}
						else
						{
							glUniform1i(uniformLocation, binding);
							CheckGLErrorMessages();
						}
					}
					// Always increment the binding, even when not binding anything
					++binding;
				}
			}

			// TODO: Condense following uniform checks (pos, norm, diffSpec, irradiance)
			// Samplers that don't need to be loaded from file, but need to be located in shaders
			if (m_Shaders[mat.material.shaderID].shader.needPositionFrameBufferSampler)
			{
				mat.positionFrameBufferSamplerID = createInfo->positionFrameBufferSamplerID;
				int positionLocation = glGetUniformLocation(m_Shaders[mat.material.shaderID].program, "positionFrameBufferSampler");
				CheckGLErrorMessages();
				if (positionLocation == -1)
				{
					Logger::LogWarning("positionFrameBufferSampler was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].shader.name + ")");
				}
				else
				{
					glUniform1i(positionLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (m_Shaders[mat.material.shaderID].shader.needNormalFrameBufferSampler)
			{
				mat.normalFrameBufferSamplerID = createInfo->normalFrameBufferSamplerID;
				int normalLocation = glGetUniformLocation(m_Shaders[mat.material.shaderID].program, "normalFrameBufferSampler");
				CheckGLErrorMessages();
				if (normalLocation == -1)
				{
					Logger::LogWarning("normalFrameBufferSampler was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].shader.name + ")");
				}
				else
				{
					glUniform1i(normalLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (m_Shaders[mat.material.shaderID].shader.needDiffuseSpecularFrameBufferSampler)
			{
				mat.diffuseSpecularFrameBufferSamplerID = createInfo->diffuseSpecularFrameBufferSamplerID;
				int diffuseSpecularLocation = glGetUniformLocation(m_Shaders[mat.material.shaderID].program, "diffuseSpecularFrameBufferSampler");
				CheckGLErrorMessages();
				if (diffuseSpecularLocation == -1)
				{
					Logger::LogWarning("diffuseSpecularFrameBufferSampler was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].shader.name + ")");
				}
				else
				{
					glUniform1i(diffuseSpecularLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}


			// Skybox
			if (createInfo->generateCubemapSampler)
			{
				if (createInfo->cubeMapFilePaths[0].empty())
				{
					// Cubemap is needed, but doesn't need to loaded from file
					GenerateGLCubemap_Empty(mat.cubemapSamplerID, createInfo->generatedCubemapSize.x, createInfo->generatedCubemapSize.y, false, createInfo->enableCubemapTrilinearFiltering);
				}
				else
				{
					// Load from file
					GenerateGLCubemapTextures(mat.cubemapSamplerID, mat.material.cubeMapFilePaths);

					int uniformLocation = glGetUniformLocation(m_Shaders[mat.material.shaderID].program, "cubemapSampler");
					CheckGLErrorMessages();
					if (uniformLocation == -1)
					{
						Logger::LogWarning("cubemapSampler was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].shader.name + ")");
					}
					else
					{
						glUniform1i(uniformLocation, binding);
					}
					CheckGLErrorMessages();
					++binding;
				}
			}

			if (createInfo->generateHDRCubemapSampler)
			{
				GenerateGLCubemap_Empty(mat.cubemapSamplerID, createInfo->generatedHDRCubemapSize.x, createInfo->generatedHDRCubemapSize.y, false, createInfo->enableCubemapTrilinearFiltering);
			}

			if (m_Shaders[mat.material.shaderID].shader.needCubemapSampler)
			{
				// TODO: Save location for binding later?
				int uniformLocation = glGetUniformLocation(m_Shaders[mat.material.shaderID].program, "cubemapSampler");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("cubemapSampler was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (mat.material.generateBRDFLUT)
			{
				GenerateGLTexture_Empty(mat.brdfLUTSamplerID, createInfo->generatedBRDFLUTSize, false, GL_RG16F, GL_RG, GL_FLOAT);
			}

			if (m_Shaders[mat.material.shaderID].shader.needBRDFLUT)
			{
				int uniformLocation = glGetUniformLocation(m_Shaders[mat.material.shaderID].program, "brdfLUT");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("brdfLUT was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (mat.material.generateIrradianceSampler)
			{
				GenerateGLCubemap_Empty(mat.irradianceSamplerID, createInfo->generatedIrradianceCubemapSize.x, createInfo->generatedIrradianceCubemapSize.y);
			}

			if (m_Shaders[mat.material.shaderID].shader.needIrradianceSampler)
			{
				int uniformLocation = glGetUniformLocation(m_Shaders[mat.material.shaderID].program, "irradianceSampler");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("irradianceSampler was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
 				CheckGLErrorMessages();
				++binding;
			}

			if (mat.material.generatePrefilteredMap)
			{
				GenerateGLCubemap_Empty(mat.prefilteredMapSamplerID, createInfo->generatedPrefilteredCubemapSize.x, createInfo->generatedPrefilteredCubemapSize.y, true);
			}

			if (m_Shaders[mat.material.shaderID].shader.needPrefilteredMap)
			{
				int uniformLocation = glGetUniformLocation(m_Shaders[mat.material.shaderID].program, "prefilterMap");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("prefilterMap was not found in material " + mat.material.name + " (shader " + m_Shaders[mat.material.shaderID].shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			glUseProgram(0);

			return materialID;
		}

		glm::uint GLRenderer::InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo)
		{
			UNREFERENCED_PARAMETER(gameContext);

			const RenderID renderID = GetFirstAvailableRenderID();

			GLRenderObject* renderObject = new GLRenderObject(renderID, createInfo->name);
			InsertNewRenderObject(renderObject);
			renderObject->materialID = createInfo->materialID;
			renderObject->cullFace = CullFaceToGLMode(createInfo->cullFace);

			renderObject->info = {};
			renderObject->info.materialName = m_Materials[renderObject->materialID].material.name;
			renderObject->info.name = createInfo->name;
			renderObject->info.transform = createInfo->transform;

			if (m_Materials.empty()) Logger::LogError("Render object is being created before any materials have been created!");
			if (renderObject->materialID >= m_Materials.size())
			{
				Logger::LogError("Uninitialized material with MaterialID " + std::to_string(renderObject->materialID));
				return renderID;
			}

			GLMaterial& material = m_Materials[renderObject->materialID];
			GLShader& shader = m_Shaders[material.material.shaderID];

			glUseProgram(shader.program);
			CheckGLErrorMessages();

			glGenVertexArrays(1, &renderObject->VAO);
			glBindVertexArray(renderObject->VAO);
			CheckGLErrorMessages();

			glGenBuffers(1, &renderObject->VBO);
			glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
			glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)createInfo->vertexBufferData->BufferSize, createInfo->vertexBufferData->pDataStart, GL_STATIC_DRAW);
			CheckGLErrorMessages();

			renderObject->vertexBufferData = createInfo->vertexBufferData;

			if (createInfo->indices != nullptr)
			{
				renderObject->indices = createInfo->indices;
				renderObject->indexed = true;

				glGenBuffers(1, &renderObject->IBO);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderObject->IBO);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(createInfo->indices->at(0)) * createInfo->indices->size()), createInfo->indices->data(), GL_STATIC_DRAW);
			}

			glBindVertexArray(0);
			glUseProgram(0);

			return renderID;
		}

		void GLRenderer::PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);

			if (m_Materials[renderObject->materialID].material.generateIrradianceSampler)
			{
				GenerateCubemapFromHDREquirectangular(gameContext, renderObject);
				GenerateIrradianceSamplerFromCubemap(gameContext, renderObject);
				GeneratePrefilteredMapFromCubemap(gameContext, renderObject);
				GenerateBRDFLUT(gameContext, renderObject);
			}
		}

		void GLRenderer::GenerateCubemapFromHDREquirectangular(const GameContext& gameContext, GLRenderObject* renderObject)
		{
			MaterialCreateInfo equirectangularToCubeMatCreateInfo = {};
			equirectangularToCubeMatCreateInfo.name = "Equirectangular to Cube";
			equirectangularToCubeMatCreateInfo.shaderName = "equirectangular_to_cube";
			equirectangularToCubeMatCreateInfo.enableHDREquirectangularSampler = true;
			equirectangularToCubeMatCreateInfo.generateHDREquirectangularSampler = true;
			// TODO: Make cyclable at runtime
			equirectangularToCubeMatCreateInfo.hdrEquirectangularTexturePath =
				//RESOURCE_LOCATION + "textures/hdri/Arches_E_PineTree/Arches_E_PineTree_3k.hdr";
				//RESOURCE_LOCATION + "textures/hdri/Factory_Catwalk/Factory_Catwalk_2k.hdr";
				//RESOURCE_LOCATION + "textures/hdri/Ice_Lake/Ice_Lake_Ref.hdr";
				RESOURCE_LOCATION + "textures/hdri/Protospace_B/Protospace_B_Ref.hdr";
			MaterialID equirectangularToCubeMatID = InitializeMaterial(gameContext, &equirectangularToCubeMatCreateInfo);

			// Convert HDR equirectangular texture to cubemap using six snapshots
			glUseProgram(m_Shaders[m_Materials[equirectangularToCubeMatID].material.shaderID].program);
			CheckGLErrorMessages();

			// Update object's uniforms under this shader's program
			renderObject->model = glm::mat4(1.0f);
			glUniformMatrix4fv(m_Materials[equirectangularToCubeMatID].uniformIDs.model, 1, false, &renderObject->model[0][0]);
			CheckGLErrorMessages();

			glUniformMatrix4fv(m_Materials[equirectangularToCubeMatID].uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
			CheckGLErrorMessages();

			// TODO: Store what location this texture is at (might not be 0)
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_Materials[equirectangularToCubeMatID].hdrTextureID);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			//glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_IrradianceCubemapCaptureSize.x, m_IrradianceCubemapCaptureSize.y);

			CheckGLErrorMessages();

			glViewport(0, 0, m_HDREquirectangularCubemapCaptureSize.x, m_HDREquirectangularCubemapCaptureSize.y);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);

			for (unsigned int i = 0; i < 6; ++i)
			{
				glBindVertexArray(renderObject->VAO);
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
				CheckGLErrorMessages();

				glUniformMatrix4fv(m_Materials[equirectangularToCubeMatID].uniformIDs.view, 1, false, &m_CaptureViews[i][0][0]);
				CheckGLErrorMessages();

				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[renderObject->materialID].cubemapSamplerID, 0);
				CheckGLErrorMessages();

				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				CheckGLErrorMessages();

				glCullFace(renderObject->cullFace);
				CheckGLErrorMessages();

				glDrawArrays(renderObject->topology, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
				CheckGLErrorMessages();
			}

			// Generate mip maps for generated cubemap
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[renderObject->materialID].cubemapSamplerID);
			glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

			glUseProgram(0);
			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			glViewport(0, 0, frameBufferSize.x, frameBufferSize.y);
		}

		void GLRenderer::GeneratePrefilteredMapFromCubemap(const GameContext& gameContext, GLRenderObject* renderObject)
		{
			MaterialCreateInfo prefilterMaterialCreateInfo = {};
			prefilterMaterialCreateInfo.name = "Prefilter";
			prefilterMaterialCreateInfo.shaderName = "prefilter";
			MaterialID prefilterMatID = InitializeMaterial(gameContext, &prefilterMaterialCreateInfo);

			glUseProgram(m_Shaders[m_Materials[prefilterMatID].material.shaderID].program);
			CheckGLErrorMessages();

			glUniformMatrix4fv(m_Materials[prefilterMatID].uniformIDs.model, 1, false, &renderObject->model[0][0]);
			CheckGLErrorMessages();

			glUniformMatrix4fv(m_Materials[prefilterMatID].uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
			CheckGLErrorMessages();

			glActiveTexture(GL_TEXTURE0); // TODO: Remove constant
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[renderObject->materialID].cubemapSamplerID);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);

			unsigned int maxMipLevels = 5;
			for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
			{
				glBindVertexArray(renderObject->VAO);
				CheckGLErrorMessages();
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
				CheckGLErrorMessages();

				unsigned int mipWidth = (unsigned int)(m_Materials[renderObject->materialID].material.prefilteredMapSize.x * pow(0.5f, mip));
				unsigned int mipHeight = (unsigned int)(m_Materials[renderObject->materialID].material.prefilteredMapSize.y * pow(0.5f, mip));

				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				//glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
				CheckGLErrorMessages();

				glViewport(0, 0, mipWidth, mipHeight);
				CheckGLErrorMessages();

				float roughness = (float)mip / (float(maxMipLevels - 1));
				int roughnessUniformLocation = glGetUniformLocation(m_Shaders[m_Materials[prefilterMatID].material.shaderID].program, "roughness");
				glUniform1f(roughnessUniformLocation, roughness);
				CheckGLErrorMessages();

				for (unsigned int i = 0; i < 6; ++i)
				{
					glUniformMatrix4fv(m_Materials[prefilterMatID].uniformIDs.view, 1, false, &m_CaptureViews[i][0][0]);
					CheckGLErrorMessages();

					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[renderObject->materialID].prefilteredMapSamplerID, mip);
					CheckGLErrorMessages();

					glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
					CheckGLErrorMessages();

					glCullFace(renderObject->cullFace);

					glDrawArrays(renderObject->topology, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
					CheckGLErrorMessages();
				}
			}

			// TODO: Make this a togglable bool param for the shader (or roughness param)
			// Visualize prefiltered map as skybox:
			//m_Materials[renderObject->materialID].cubemapSamplerID = m_Materials[renderObject->materialID].prefilteredMapSamplerID;

			glUseProgram(0);
			glBindVertexArray(0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			glViewport(0, 0, frameBufferSize.x, frameBufferSize.y);
		}

		void GLRenderer::GenerateBRDFLUT(const GameContext& gameContext, GLRenderObject* renderObject)
		{
			MaterialCreateInfo brdfMaterialCreateInfo = {};
			brdfMaterialCreateInfo.name = "BRDF";
			brdfMaterialCreateInfo.shaderName = "brdf";
			MaterialID brdfMatID = InitializeMaterial(gameContext, &brdfMaterialCreateInfo);

			if (m_1x1_NDC_Quad == nullptr)
			{
				VertexBufferData::CreateInfo quadVertexBufferDataCreateInfo = {};
				quadVertexBufferDataCreateInfo.positions_3D = {
					{ -1.0f,  1.0f, 0.0f },
					{ -1.0f, -1.0f, 0.0f },
					{ 1.0f,  1.0f, 0.0f },
					{ 1.0f, -1.0f, 0.0f },
				};
				quadVertexBufferDataCreateInfo.texCoords_UV = {
					{ 0.0f, 1.0f },
					{ 0.0f, 0.0f },
					{ 1.0f, 1.0f },
					{ 1.0f, 0.0f },
				};
				quadVertexBufferDataCreateInfo.attributes = (glm::uint)VertexAttribute::POSITION | (glm::uint)VertexAttribute::UV;

				m_1x1_NDC_QuadVertexBufferData = {};
				m_1x1_NDC_QuadVertexBufferData.Initialize(&quadVertexBufferDataCreateInfo);

				RenderObjectCreateInfo quadCreateInfo = {};
				quadCreateInfo.name = "1x1 Quad";
				quadCreateInfo.materialID = brdfMatID;
				quadCreateInfo.vertexBufferData = &m_1x1_NDC_QuadVertexBufferData;
				m_1x1_NDC_QuadTransform = Transform::Identity(); // Should we even have this member?
				quadCreateInfo.transform = &m_1x1_NDC_QuadTransform;

				RenderID quadRenderID = InitializeRenderObject(gameContext, &quadCreateInfo);
				m_1x1_NDC_Quad = GetRenderObject(quadRenderID);

				if (!m_1x1_NDC_Quad)
				{
					Logger::LogError("Failed to create 1x1 NDC quad!");
				}
				else
				{
					SetTopologyMode(quadRenderID, TopologyMode::TRIANGLE_STRIP);
					m_1x1_NDC_Quad->visible = false; // Don't render this normally, we'll draw it manually
					m_1x1_NDC_QuadVertexBufferData.DescribeShaderVariables(gameContext.renderer, quadRenderID);
				}
			}

			glUseProgram(m_Shaders[m_Materials[brdfMatID].material.shaderID].program);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			CheckGLErrorMessages();
			//glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
			CheckGLErrorMessages();
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_Materials[renderObject->materialID].brdfLUTSamplerID, 0);
			CheckGLErrorMessages();

			glBindVertexArray(m_1x1_NDC_Quad->VAO);
			CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, m_1x1_NDC_Quad->VBO);
			CheckGLErrorMessages();

			glViewport(0, 0, m_Materials[renderObject->materialID].material.generatedBRDFLUTSize.x, m_Materials[renderObject->materialID].material.generatedBRDFLUTSize.y);
			CheckGLErrorMessages();

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			CheckGLErrorMessages();

			glCullFace(m_1x1_NDC_Quad->cullFace);

			// Render quad
			glDrawArrays(m_1x1_NDC_Quad->topology, 0, (GLsizei)m_1x1_NDC_Quad->vertexBufferData->VertexCount);
			CheckGLErrorMessages();

			glUseProgram(0);
			glBindVertexArray(0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			glViewport(0, 0, frameBufferSize.x, frameBufferSize.y);
		}

		bool GLRenderer::GetShaderID(const std::string& shaderName, ShaderID& shaderID)
		{
			// TODO: Store shaders using sorted data structure?
			for (size_t i = 0; i < m_Shaders.size(); ++i)
			{
				if (m_Shaders[i].shader.name.compare(shaderName) == 0)
				{
					shaderID = i;
					return true;
				}
			}

			return false;
		}

		void GLRenderer::GenerateIrradianceSamplerFromCubemap(const GameContext& gameContext, GLRenderObject* renderObject)
		{

			// Irradiance sampler generation
			MaterialCreateInfo irrandianceMatCreateInfo = {};
			irrandianceMatCreateInfo.name = "Irradiance";
			irrandianceMatCreateInfo.shaderName = "irradiance";
			irrandianceMatCreateInfo.enableCubemapSampler = true;
			MaterialID irrandianceMatID = InitializeMaterial(gameContext, &irrandianceMatCreateInfo);

			glUseProgram(m_Shaders[m_Materials[irrandianceMatID].material.shaderID].program);
			CheckGLErrorMessages();

			glUniformMatrix4fv(m_Materials[irrandianceMatID].uniformIDs.model, 1, false, &renderObject->model[0][0]);
			CheckGLErrorMessages();

			glUniformMatrix4fv(m_Materials[irrandianceMatID].uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
			CheckGLErrorMessages();

			glActiveTexture(GL_TEXTURE0); // TODO: Remove constant
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[renderObject->materialID].cubemapSamplerID);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			// TODO: Why is this swapped with the other call?
			//glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_HDREquirectangularCubemapCaptureSize.x, m_HDREquirectangularCubemapCaptureSize.y);
			CheckGLErrorMessages();

			glViewport(0, 0, m_Materials[renderObject->materialID].material.irradianceSamplerSize.x, m_Materials[renderObject->materialID].material.irradianceSamplerSize.y);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);

			for (unsigned int i = 0; i < 6; ++i)
			{
				glBindVertexArray(renderObject->VAO);
				CheckGLErrorMessages();
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
				CheckGLErrorMessages();

				glUniformMatrix4fv(m_Materials[irrandianceMatID].uniformIDs.view, 1, false, &m_CaptureViews[i][0][0]);
				CheckGLErrorMessages();

				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[renderObject->materialID].irradianceSamplerID, 0);
				CheckGLErrorMessages();

				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				CheckGLErrorMessages();

				glCullFace(renderObject->cullFace);

				glDrawArrays(renderObject->topology, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
				CheckGLErrorMessages();
			}

			glUseProgram(0);
			glBindVertexArray(0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			glViewport(0, 0, frameBufferSize.x, frameBufferSize.y);
		}

		DirectionalLightID GLRenderer::InitializeDirectionalLight(const DirectionalLight& dirLight)
		{
			m_DirectionalLight = dirLight;
			return 0;
		}

		PointLightID GLRenderer::InitializePointLight(const PointLight& pointLight)
		{
			m_PointLights.push_back(pointLight);
			return m_PointLights.size() - 1;
		}

		Renderer::DirectionalLight& GLRenderer::GetDirectionalLight(DirectionalLightID dirLightID)
		{
			// TODO: Add support for multiple directional lights
			UNREFERENCED_PARAMETER(dirLightID);
			return m_DirectionalLight;
		}

		Renderer::PointLight& GLRenderer::GetPointLight(PointLightID pointLightID)
		{
			return m_PointLights[pointLightID];
		}

		std::vector<Renderer::PointLight>& GLRenderer::GetAllPointLights()
		{
			return m_PointLights;
		}

		void GLRenderer::SetTopologyMode(RenderID renderID, TopologyMode topology)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			GLenum glMode = TopologyModeToGLMode(topology);

			if (glMode == GL_INVALID_ENUM)
			{
				Logger::LogError("Unhandled TopologyMode passed to GLRenderer::SetTopologyMode: " + std::to_string((int)topology));
				renderObject->topology = GL_TRIANGLES;
			}
			else
			{
				renderObject->topology = glMode;
			}
		}

		void GLRenderer::SetClearColor(float r, float g, float b)
		{
			glClearColor(r, g, b, 1.0f);
			CheckGLErrorMessages();
		}

		void GLRenderer::PostInitialize(const GameContext& gameContext)
		{
			// G-buffer objects
			glGenFramebuffers(1, &m_gBufferHandle);
			glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);

			const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

			GenerateFrameBufferTexture(&m_gBuffer_PositionHandle, 0, GL_RGB16F, GL_RGB, frameBufferSize);
			GenerateFrameBufferTexture(&m_gBuffer_NormalHandle, 1, GL_RGB16F, GL_RGB, frameBufferSize);
			GenerateFrameBufferTexture(&m_gBuffer_DiffuseSpecularHandle, 2, GL_RGBA, GL_RGBA, frameBufferSize);

			constexpr int numBuffers = 3;
			unsigned int attachments[numBuffers] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
			glDrawBuffers(numBuffers, attachments);

			// Create and attach depth buffer
			glGenRenderbuffers(1, &m_gBufferDepthHandle);
			glBindRenderbuffer(GL_RENDERBUFFER, m_gBufferDepthHandle);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, frameBufferSize.x, frameBufferSize.y);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_gBufferDepthHandle);

			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			{
				Logger::LogError("Framebuffer not complete!");
			}
			glBindFramebuffer(GL_FRAMEBUFFER, 0);


			MaterialCreateInfo gBufferMaterialCreateInfo = {};
			gBufferMaterialCreateInfo.name = "GBuffer material";
			gBufferMaterialCreateInfo.shaderName = "deferred_combine";

			gBufferMaterialCreateInfo.enablePositionFrameBufferSampler = true;
			gBufferMaterialCreateInfo.positionFrameBufferSamplerID = m_gBuffer_PositionHandle;

			gBufferMaterialCreateInfo.enableNormalFrameBufferSampler = true;
			gBufferMaterialCreateInfo.normalFrameBufferSamplerID = m_gBuffer_NormalHandle;

			gBufferMaterialCreateInfo.enableDiffuseSpecularFrameBufferSampler = true;
			gBufferMaterialCreateInfo.diffuseSpecularFrameBufferSamplerID = m_gBuffer_DiffuseSpecularHandle;

			MaterialID gBufferMatID = InitializeMaterial(gameContext, &gBufferMaterialCreateInfo);

			VertexBufferData::CreateInfo gBufferQuadVertexBufferDataCreateInfo = {};

			gBufferQuadVertexBufferDataCreateInfo.positions_3D = {
				glm::vec3(-1.0f,  1.0f, 0.0f),
				glm::vec3(-1.0f, -1.0f, 0.0f),
				glm::vec3(1.0f,  1.0f, 0.0f),

				glm::vec3(1.0f, -1.0f, 0.0f),
				glm::vec3(1.0f,  1.0f, 0.0f),
				glm::vec3(-1.0f, -1.0f, 0.0f),
			};

			gBufferQuadVertexBufferDataCreateInfo.texCoords_UV = {
				glm::vec2(0.0f, 1.0f),
				glm::vec2(0.0f, 0.0f),
				glm::vec2(1.0f, 1.0f),

				glm::vec2(1.0f, 0.0f),
				glm::vec2(1.0f, 1.0f),
				glm::vec2(0.0f, 0.0f),
			};

			gBufferQuadVertexBufferDataCreateInfo.attributes = (glm::uint)VertexAttribute::POSITION | (glm::uint)VertexAttribute::UV;

			m_gBufferQuadVertexBufferData.Initialize(&gBufferQuadVertexBufferDataCreateInfo);

			RenderObjectCreateInfo gBufferQuadCreateInfo = {};
			gBufferQuadCreateInfo.name = "G Buffer Quad";
			gBufferQuadCreateInfo.materialID = gBufferMatID;
			m_gBufferQuadTransform = {}; // Identity transform
			gBufferQuadCreateInfo.transform = &m_gBufferQuadTransform;
			gBufferQuadCreateInfo.vertexBufferData = &m_gBufferQuadVertexBufferData;

			m_GBufferQuadRenderID = InitializeRenderObject(gameContext, &gBufferQuadCreateInfo);

			m_gBufferQuadVertexBufferData.DescribeShaderVariables(this, m_GBufferQuadRenderID);

			GLRenderObject* gBufferRenderObject = GetRenderObject(m_GBufferQuadRenderID);
			gBufferRenderObject->visible = false; // Don't render the g buffer normally, we'll handle it separately

			CheckGLErrorMessages();

			Logger::LogInfo("Ready!\n");
		}

		void GLRenderer::GenerateFrameBufferTexture(glm::uint* handle, int index, GLint internalFormat, GLenum format, const glm::vec2i& size)
		{
			glGenTextures(1, handle);
			glBindTexture(GL_TEXTURE_2D, *handle);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size.x, size.y, 0, format, GL_FLOAT, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D, *handle, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			CheckGLErrorMessages();
		}

		void GLRenderer::ResizeFrameBufferTexture(glm::uint handle, int index, GLint internalFormat, GLenum format, const glm::vec2i& size)
		{
			UNREFERENCED_PARAMETER(index);

			glBindTexture(GL_TEXTURE_2D, handle);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size.x, size.y, 0, format, GL_FLOAT, NULL);
			CheckGLErrorMessages();
		}

		void GLRenderer::ResizeRenderBuffer(glm::uint handle, const glm::vec2i& size)
		{
			glBindRenderbuffer(GL_RENDERBUFFER, handle);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, size.x, size.y);
		}

		void GLRenderer::Update(const GameContext& gameContext)
		{
			UNREFERENCED_PARAMETER(gameContext);
			CheckGLErrorMessages();
		}

		void GLRenderer::Draw(const GameContext& gameContext)
		{
			CheckGLErrorMessages();

			// TODO: Don't create two nested vectors every frame, just sort things by deferred/forward, then by material ID
			/*
			  Eg. deferred | matID
				  yes		 0
				  yes		 2
				  no		 1
				  no		 3
				  no		 5
			*/
			// Sort render objects into deferred + forward buckets
			std::vector<std::vector<GLRenderObject*>> deferredRenderObjectBatches;
			std::vector<std::vector<GLRenderObject*>> forwardRenderObjectBatches;
			for (size_t i = 0; i < m_Materials.size(); ++i)
			{
				GLShader* shader = &m_Shaders[m_Materials[i].material.shaderID];

				UpdateMaterialUniforms(gameContext, i);

				if (shader->shader.deferred)
				{
					deferredRenderObjectBatches.push_back({});
					for (size_t j = 0; j < m_RenderObjects.size(); ++j)
					{
						GLRenderObject* renderObject = GetRenderObject(j);
						if (renderObject && renderObject->visible && renderObject->materialID == i)
						{
							deferredRenderObjectBatches.back().push_back(renderObject);
						}
					}
				}
				else
				{
					forwardRenderObjectBatches.push_back({});
					for (size_t j = 0; j < m_RenderObjects.size(); ++j)
					{
						GLRenderObject* renderObject = GetRenderObject(j);
						if (renderObject && renderObject->visible && renderObject->materialID == i)
						{
							forwardRenderObjectBatches.back().push_back(renderObject);
						}
					}
				}
			}

			// Geometry pass - Render scene's geometry into gbuffer render targets
			glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);
			CheckGLErrorMessages();

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			CheckGLErrorMessages();

			for (size_t i = 0; i < deferredRenderObjectBatches.size(); ++i)
			{
				if (!deferredRenderObjectBatches[i].empty())
				{
					DrawRenderObjectBatch(deferredRenderObjectBatches[i], gameContext);
				}
			}

			glUseProgram(0);
			glBindVertexArray(0);
			CheckGLErrorMessages();

			const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

			// Copy depth from gbuffer to default render target
			glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferHandle);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			glBlitFramebuffer(0, 0, frameBufferSize.x, frameBufferSize.y, 0, 0, frameBufferSize.x, frameBufferSize.y, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			CheckGLErrorMessages();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			// Don't write gbuffer quad into depth buffer
			glDepthMask(GL_FALSE);

			// Lighting pass - Calculate lighting based on the gbuffer's contents
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			GLRenderObject* gBufferQuad = GetRenderObject(m_GBufferQuadRenderID);


			// TODO: Draw offscreen quad once for each deferred material type (store deferred matID in shaders, remove gBufferQuad->materialID) 


			glUseProgram(m_Shaders[m_Materials[gBufferQuad->materialID].material.shaderID].program);

			glBindVertexArray(gBufferQuad->VAO);
			glBindBuffer(GL_ARRAY_BUFFER, gBufferQuad->VBO);
			CheckGLErrorMessages();

			UpdateMaterialUniforms(gameContext, gBufferQuad->materialID);
			UpdatePerObjectUniforms(gBufferQuad->renderID, gameContext);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_gBuffer_PositionHandle);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, m_gBuffer_NormalHandle);
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, m_gBuffer_DiffuseSpecularHandle);
			CheckGLErrorMessages();

			glCullFace(gBufferQuad->cullFace);
			CheckGLErrorMessages();
			glDrawArrays(gBufferQuad->topology, 0, (GLsizei)gBufferQuad->vertexBufferData->VertexCount);
			CheckGLErrorMessages();

			glDepthMask(GL_TRUE);
			CheckGLErrorMessages();

			// Forward pass - draw all objects which don't use deferred shading
			for (size_t i = 0; i < forwardRenderObjectBatches.size(); ++i)
			{
				if (!forwardRenderObjectBatches[i].empty())
				{
					DrawRenderObjectBatch(forwardRenderObjectBatches[i], gameContext);
				}
			}

			// Draw UI
			ImDrawData* drawData = ImGui::GetDrawData();

			// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
			ImGuiIO& io = ImGui::GetIO();
			int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
			int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
			if (fb_width == 0 || fb_height == 0)
				return;
			drawData->ScaleClipRects(io.DisplayFramebufferScale);

			// Backup GL state
			GLenum last_active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&last_active_texture);
			glActiveTexture(GL_TEXTURE0);
			GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
			GLint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
			GLint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
			GLint last_element_array_buffer; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &last_element_array_buffer);
			GLint last_vertex_array; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
			GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
			GLenum last_blend_src_rgb; glGetIntegerv(GL_BLEND_SRC_RGB, (GLint*)&last_blend_src_rgb);
			GLenum last_blend_dst_rgb; glGetIntegerv(GL_BLEND_DST_RGB, (GLint*)&last_blend_dst_rgb);
			GLenum last_blend_src_alpha; glGetIntegerv(GL_BLEND_SRC_ALPHA, (GLint*)&last_blend_src_alpha);
			GLenum last_blend_dst_alpha; glGetIntegerv(GL_BLEND_DST_ALPHA, (GLint*)&last_blend_dst_alpha);
			GLenum last_blend_equation_rgb; glGetIntegerv(GL_BLEND_EQUATION_RGB, (GLint*)&last_blend_equation_rgb);
			GLenum last_blend_equation_alpha; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, (GLint*)&last_blend_equation_alpha);
			GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
			GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
			GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
			GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);

			// Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled
			glEnable(GL_BLEND);
			glBlendEquation(GL_FUNC_ADD);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_CULL_FACE);
			glDisable(GL_DEPTH_TEST);
			glEnable(GL_SCISSOR_TEST);

			// Setup viewport, orthographic projection matrix
			glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
			const float ortho_projection[4][4] =
			{
				{ 2.0f / io.DisplaySize.x, 0.0f,                   0.0f, 0.0f },
				{ 0.0f,                  2.0f / -io.DisplaySize.y, 0.0f, 0.0f },
				{ 0.0f,                  0.0f,                  -1.0f, 0.0f },
				{ -1.0f,                  1.0f,                   0.0f, 1.0f },
			};
			glUseProgram(m_ImGuiShaderHandle);
			//glUniform1i(m_ImGuiAttribLocationTex, 0);
			glUniformMatrix4fv(m_ImGuiAttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);
			glBindVertexArray(m_ImGuiVaoHandle);

			for (int n = 0; n < drawData->CmdListsCount; ++n)
			{
				const ImDrawList* cmd_list = drawData->CmdLists[n];
				const ImDrawIdx* idx_buffer_offset = 0;

				glBindBuffer(GL_ARRAY_BUFFER, m_ImGuiVboHandle);
				glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);

				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ElementsHandle);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), (const GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);

				for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i)
				{
					const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
					if (pcmd->UserCallback)
					{
						pcmd->UserCallback(cmd_list, pcmd);
					}
					else
					{
						glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
						glScissor((int)pcmd->ClipRect.x, (int)(fb_height - pcmd->ClipRect.w), (int)(pcmd->ClipRect.z - pcmd->ClipRect.x), (int)(pcmd->ClipRect.w - pcmd->ClipRect.y));
						glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer_offset);
					}
					idx_buffer_offset += pcmd->ElemCount;
				}
			}

			// Restore modified GL state
			glUseProgram(last_program);
			glBindTexture(GL_TEXTURE_2D, last_texture);
			glActiveTexture(last_active_texture);
			glBindVertexArray(last_vertex_array);
			glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, last_element_array_buffer);
			glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
			glBlendFuncSeparate(last_blend_src_rgb, last_blend_dst_rgb, last_blend_src_alpha, last_blend_dst_alpha);
			if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
			if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
			if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
			if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
			glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
			glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);


			glfwSwapBuffers(((GLWindowWrapper*)gameContext.window)->GetWindow());
		}

		void GLRenderer::DrawRenderObjectBatch(const std::vector<GLRenderObject*>& batchedRenderObjects, const GameContext& gameContext)
		{
			assert(!batchedRenderObjects.empty());

			GLMaterial* material = &m_Materials[batchedRenderObjects[0]->materialID];
			GLShader* shader = &m_Shaders[material->material.shaderID];
			glUseProgram(shader->program);
			CheckGLErrorMessages();

			for (size_t i = 0; i < batchedRenderObjects.size(); ++i)
			{
				GLRenderObject* renderObject = batchedRenderObjects[i];

				glBindVertexArray(renderObject->VAO);
				CheckGLErrorMessages();
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
				CheckGLErrorMessages();

				glCullFace(renderObject->cullFace);
				CheckGLErrorMessages();

				UpdatePerObjectUniforms(renderObject->renderID, gameContext);

				struct Tex
				{
					bool needed;
					bool enabled;
					glm::uint textureID;
				};

				std::vector<Tex> textures;

				textures.push_back({ m_Shaders[material->material.shaderID].shader.needAlbedoSampler, material->material.enableAlbedoSampler, material->albedoSamplerID });
				textures.push_back({ m_Shaders[material->material.shaderID].shader.needMetallicSampler, material->material.enableMetallicSampler, material->metallicSamplerID });
				textures.push_back({ m_Shaders[material->material.shaderID].shader.needRoughnessSampler, material->material.enableRoughnessSampler, material->roughnessSamplerID });
				textures.push_back({ m_Shaders[material->material.shaderID].shader.needAOSampler, material->material.enableAOSampler, material->aoSamplerID });
				textures.push_back({ m_Shaders[material->material.shaderID].shader.needDiffuseSampler, material->material.enableDiffuseSampler, material->diffuseSamplerID });
				textures.push_back({ m_Shaders[material->material.shaderID].shader.needNormalSampler, material->material.enableNormalSampler, material->normalSamplerID });
				textures.push_back({ m_Shaders[material->material.shaderID].shader.needSpecularSampler, material->material.enableSpecularSampler, material->specularSamplerID });
				textures.push_back({ m_Shaders[material->material.shaderID].shader.needBRDFLUT, material->material.enableBRDFLUT, material->brdfLUTSamplerID });

				glm::uint location = 0;
				for (Tex& tex : textures)
				{
					if (tex.needed)
					{
						if (tex.enabled)
						{
							GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)location);
							glActiveTexture(activeTexture);
							glBindTexture(GL_TEXTURE_2D, (GLuint)tex.textureID);
							CheckGLErrorMessages();
						}
						++location;
					}
				}

				// TODO: Batch together cubemaps like textures
				if (m_Shaders[material->material.shaderID].shader.needCubemapSampler)
				{
					if (material->material.enableCubemapSampler)
					{
						GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)location);
						glActiveTexture(activeTexture);
						glBindTexture(GL_TEXTURE_CUBE_MAP, material->cubemapSamplerID);
						CheckGLErrorMessages();
					}
					++location;
				}

				if (m_Shaders[material->material.shaderID].shader.needIrradianceSampler)
				{
					if (material->material.enableIrradianceSampler)
					{
						GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)location);
						glActiveTexture(activeTexture);
						glBindTexture(GL_TEXTURE_CUBE_MAP, material->irradianceSamplerID);
						CheckGLErrorMessages();
					}
					++location;
				}

				if (m_Shaders[material->material.shaderID].shader.needPrefilteredMap)
				{
					if (material->material.enablePrefilteredMap)
					{
						GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)location);
						glActiveTexture(activeTexture);
						glBindTexture(GL_TEXTURE_CUBE_MAP, material->prefilteredMapSamplerID);
						CheckGLErrorMessages();
					}
					++location;
				}

				if (renderObject->indexed)
				{
					glDrawElements(renderObject->topology, (GLsizei)renderObject->indices->size(), GL_UNSIGNED_INT, (void*)renderObject->indices->data());
					CheckGLErrorMessages();
				}
				else
				{
					glDrawArrays(renderObject->topology, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
					CheckGLErrorMessages();
				}
			}
		}

		void GLRenderer::ReloadShaders(GameContext& gameContext)
		{
			UNREFERENCED_PARAMETER(gameContext);

			UnloadShaders();
			LoadShaders();

			CheckGLErrorMessages();
		}

		void GLRenderer::UnloadShaders()
		{
			const size_t shaderCount = m_Shaders.size();
			for (size_t i = 0; i < shaderCount; ++i)
			{
				glDeleteProgram(m_Shaders[i].program);
				CheckGLErrorMessages();
			}
			m_Shaders.clear();
		}

		void GLRenderer::LoadShaders()
		{
			m_Shaders = {
				{ "deferred_simple", RESOURCE_LOCATION + "shaders/GLSL/deferred_simple.vert", RESOURCE_LOCATION + "shaders/GLSL/deferred_simple.frag" },
				{ "color", RESOURCE_LOCATION + "shaders/GLSL/color.vert", RESOURCE_LOCATION + "shaders/GLSL/color.frag" },
				{ "imgui", RESOURCE_LOCATION + "shaders/GLSL/imgui.vert", RESOURCE_LOCATION + "shaders/GLSL/imgui.frag" },
				{ "pbr", RESOURCE_LOCATION + "shaders/GLSL/pbr.vert", RESOURCE_LOCATION + "shaders/GLSL/pbr.frag" },
				{ "skybox", RESOURCE_LOCATION + "shaders/GLSL/skybox.vert", RESOURCE_LOCATION + "shaders/GLSL/skybox.frag" },
				{ "equirectangular_to_cube", RESOURCE_LOCATION + "shaders/GLSL/skybox.vert", RESOURCE_LOCATION + "shaders/GLSL/equirectangular_to_cube.frag" },
				{ "irradiance", RESOURCE_LOCATION + "shaders/GLSL/skybox.vert", RESOURCE_LOCATION + "shaders/GLSL/irradiance.frag" },
				{ "prefilter", RESOURCE_LOCATION + "shaders/GLSL/skybox.vert", RESOURCE_LOCATION + "shaders/GLSL/prefilter.frag" },
				{ "brdf", RESOURCE_LOCATION + "shaders/GLSL/brdf.vert", RESOURCE_LOCATION + "shaders/GLSL/brdf.frag" },
				{ "background", RESOURCE_LOCATION + "shaders/GLSL/background.vert", RESOURCE_LOCATION + "shaders/GLSL/background.frag" },
				{ "deferred_combine", RESOURCE_LOCATION + "shaders/GLSL/deferred_combine.vert", RESOURCE_LOCATION + "shaders/GLSL/deferred_combine.frag" },
			};

			ShaderID shaderID = 0;

			// TOOD: Determine this info automatically when parsing shader code

			// Deferred Simple
			m_Shaders[shaderID].shader.deferred = true;
			m_Shaders[shaderID].shader.needDiffuseSampler = true;
			m_Shaders[shaderID].shader.needNormalSampler = true;
			m_Shaders[shaderID].shader.needSpecularSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("viewProjection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("modelInvTranspose");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableDiffuseSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableNormalSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableSpecularSampler");
			++shaderID;

			// Color
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("viewProjection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// ImGui
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// PBR
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needAlbedoSampler = true;
			m_Shaders[shaderID].shader.needMetallicSampler = true;
			m_Shaders[shaderID].shader.needRoughnessSampler = true;
			m_Shaders[shaderID].shader.needAOSampler = true;
			m_Shaders[shaderID].shader.needNormalSampler = true;
			m_Shaders[shaderID].shader.needBRDFLUT = true;
			m_Shaders[shaderID].shader.needIrradianceSampler = true;
			m_Shaders[shaderID].shader.needPrefilteredMap = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("viewProjection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("camPos");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("pointLights");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("irradianceSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("prefilterMap");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("brdfLUT");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constAlbedo");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableAlbedoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("albedoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constMetallic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableMetallicSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("metallicSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constRoughness");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableRoughnessSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("roughnessSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableAOSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constAO");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("aoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableNormalSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("normalSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableIrradianceSampler");
			++shaderID;

			// Skybox
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableCubemapSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("cubemapSampler");
			++shaderID;

			// Equirectangular to Cube
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needHDREquirectangularSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("hdrEquirectangularSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Irradiance
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Prefilter
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// BRDF
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Background
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Deferred combine (sample gbuffer)
			m_Shaders[shaderID].shader.deferred = false; // Sounds strange but this isn't deferred
			m_Shaders[shaderID].shader.needPositionFrameBufferSampler = true;
			m_Shaders[shaderID].shader.needNormalFrameBufferSampler = true;
			m_Shaders[shaderID].shader.needDiffuseSpecularFrameBufferSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("positionFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("normalFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("diffuseSpecularFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("pointLights");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("dirLight");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("camPos");

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			for (size_t i = 0; i < m_Shaders.size(); ++i)
			{
				m_Shaders[i].program = glCreateProgram();
				CheckGLErrorMessages();

				if (!LoadGLShaders(m_Shaders[i].program, m_Shaders[i]))
				{
					Logger::LogError("Couldn't load shaders " + m_Shaders[i].shader.vertexShaderFilePath + " and " + m_Shaders[i].shader.fragmentShaderFilePath + "!");
				}

				LinkProgram(m_Shaders[i].program);
			}

			glm::uint imGuiShaderID;
			if (!GetShaderID("imgui", imGuiShaderID))
			{
				Logger::LogError("Could not get imgui shader ID!");
			}
			else
			{
				m_ImGuiShaderHandle = m_Shaders[imGuiShaderID].program;
			}

			CheckGLErrorMessages();
		}

		void GLRenderer::UpdateMaterialUniforms(const GameContext& gameContext, MaterialID materialID)
		{
			GLMaterial* material = &m_Materials[materialID];
			GLShader* shader = &m_Shaders[material->material.shaderID];
			
			glUseProgram(shader->program);

			glm::mat4 proj = gameContext.camera->GetProjection();
			glm::mat4 view = gameContext.camera->GetView();
			glm::mat4 viewInv = glm::inverse(view);
			glm::mat4 viewProj = proj * view;
			glm::vec4 camPos = glm::vec4(gameContext.camera->GetPosition(), 0.0f);


			if (shader->shader.constantBufferUniforms.HasUniform("view"))
			{
				glUniformMatrix4fv(material->uniformIDs.view, 1, false, &view[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("viewInv"))
			{
				glUniformMatrix4fv(material->uniformIDs.viewInv, 1, false, &viewInv[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("projection"))
			{
				glUniformMatrix4fv(material->uniformIDs.projection, 1, false, &proj[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("viewProjection"))
			{
				glUniformMatrix4fv(material->uniformIDs.viewProjection, 1, false, &viewProj[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("camPos"))
			{
				glUniform4f(material->uniformIDs.camPos,
					camPos.x,
					camPos.y,
					camPos.z,
					camPos.w);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("dirLight"))
			{
				if (m_DirectionalLight.enabled)
				{
					SetUInt(material->material.shaderID, "dirLight.enabled", 1);
					CheckGLErrorMessages();
					SetVec4f(material->material.shaderID, "dirLight.direction", m_DirectionalLight.direction);
					CheckGLErrorMessages();
					SetVec4f(material->material.shaderID, "dirLight.color", m_DirectionalLight.color);
					CheckGLErrorMessages();
				}
				else
				{
					SetUInt(material->material.shaderID, "dirLight.enabled", 0);
					CheckGLErrorMessages();
				}
			}

			if (shader->shader.constantBufferUniforms.HasUniform("pointLights"))
			{
				for (size_t i = 0; i < m_PointLights.size(); ++i)
				{
					const std::string numberStr = std::to_string(i);

					if (m_PointLights[i].enabled)
					{
						SetUInt(material->material.shaderID, "pointLights[" + numberStr + "].enabled", 1);
						CheckGLErrorMessages();

						SetVec4f(material->material.shaderID, "pointLights[" + numberStr + "].position", m_PointLights[i].position);
						CheckGLErrorMessages();

						SetVec4f(material->material.shaderID, "pointLights[" + numberStr + "].color", m_PointLights[i].color);
						CheckGLErrorMessages();
					}
					else
					{
						SetUInt(material->material.shaderID, "pointLights[" + numberStr + "].enabled", 0);
						CheckGLErrorMessages();
					}
				}
			}
		}


		void GLRenderer::UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			GLMaterial* material = &m_Materials[renderObject->materialID];
			GLShader* shader = &m_Shaders[material->material.shaderID];

			glm::mat4 model = renderObject->model;
			glm::mat4 modelInv = glm::inverse(renderObject->model);
			glm::mat4 proj = gameContext.camera->GetProjection();
			glm::mat4 view = gameContext.camera->GetView();
			glm::mat4 MVP = proj * view * model;

			// TODO: Wrap into array
			if (shader->shader.dynamicBufferUniforms.HasUniform("model"))
			{
				glUniformMatrix4fv(material->uniformIDs.model, 1, false, &model[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("modelInvTranspose"))
			{
				// OpenGL will transpose for us if we set the third param to true
				glUniformMatrix4fv(material->uniformIDs.modelInvTranspose, 1, true, &modelInv[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableDiffuseSampler"))
			{
				glUniform1i(material->uniformIDs.enableDiffuseTexture, material->material.enableDiffuseSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableNormalSampler"))
			{
				glUniform1i(material->uniformIDs.enableNormalTexture, material->material.enableNormalSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableSpecularSampler"))
			{
				glUniform1i(material->uniformIDs.enableSpecularTexture, material->material.enableSpecularSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableCubemapSampler"))
			{
				glUniform1i(material->uniformIDs.enableCubemapTexture, material->material.enableCubemapSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableAlbedoSampler"))
			{
				glUniform1ui(material->uniformIDs.enableAlbedoSampler, material->material.enableAlbedoSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constAlbedo"))
			{
				glUniform4f(material->uniformIDs.constAlbedo, material->material.constAlbedo.x, material->material.constAlbedo.y, material->material.constAlbedo.z, 0);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableMetallicSampler"))
			{
				glUniform1ui(material->uniformIDs.enableMetallicSampler, material->material.enableMetallicSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constMetallic"))
			{
				glUniform1f(material->uniformIDs.constMetallic, material->material.constMetallic);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableRoughnessSampler"))
			{
				glUniform1ui(material->uniformIDs.enableRoughnessSampler, material->material.enableRoughnessSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constRoughness"))
			{
				glUniform1f(material->uniformIDs.constRoughness, material->material.constRoughness);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableAOSampler"))
			{
				glUniform1ui(material->uniformIDs.enableAOSampler, material->material.enableAOSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constAO"))
			{
				glUniform1f(material->uniformIDs.constAO, material->material.constAO);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableIrradianceSampler"))
			{
				glUniform1i(material->uniformIDs.enableIrradianceSampler, material->material.enableIrradianceSampler);
				CheckGLErrorMessages();
			}
		}

		void GLRenderer::OnWindowSize(int width, int height)
		{
			glViewport(0, 0, width, height);
			CheckGLErrorMessages();

			const glm::vec2i newFrameBufferSize(width, height);

			// TODO: Store formats so they aren't duplicated here
			ResizeFrameBufferTexture(m_gBuffer_PositionHandle, 0, GL_RGB16F, GL_RGB, newFrameBufferSize);
			ResizeFrameBufferTexture(m_gBuffer_NormalHandle, 1, GL_RGB16F, GL_RGB, newFrameBufferSize);
			ResizeFrameBufferTexture(m_gBuffer_DiffuseSpecularHandle, 2, GL_RGBA, GL_RGBA, newFrameBufferSize);

			ResizeRenderBuffer(m_gBufferDepthHandle, newFrameBufferSize);
		}

		void GLRenderer::SetVSyncEnabled(bool enableVSync)
		{
			m_VSyncEnabled = enableVSync;
			glfwSwapInterval(enableVSync ? 1 : 0);
			CheckGLErrorMessages();
		}

		// TODO: Remove function
		void GLRenderer::UpdateTransformMatrix(const GameContext& gameContext, RenderID renderID, const glm::mat4& model)
		{
			UNREFERENCED_PARAMETER(gameContext);

			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			renderObject->model = model;
		}

		void GLRenderer::SetFloat(ShaderID shaderID, const std::string& valName, float val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName.c_str());
			if (location == -1) Logger::LogWarning("Float " + valName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform1f(location, val);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetUInt(ShaderID shaderID, const std::string& valName, glm::uint val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName.c_str());
			if (location == -1) Logger::LogWarning("Unsigned int " + valName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform1ui(location, val);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec2f(ShaderID shaderID, const std::string& vecName, const glm::vec2& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1) Logger::LogWarning("Vec2f " + vecName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform2f(location, vec[0], vec[1]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec3f(ShaderID shaderID, const std::string& vecName, const glm::vec3& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1) Logger::LogWarning("Vec3f " + vecName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform3f(location, vec[0], vec[1], vec[2]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec4f(ShaderID shaderID, const std::string& vecName, const glm::vec4& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1) Logger::LogWarning("Vec4f " + vecName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform4f(location, vec[0], vec[1], vec[2], vec[3]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetMat4f(ShaderID shaderID, const std::string& matName, const glm::mat4& mat)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, matName.c_str());
			if (location == -1) Logger::LogWarning("Mat4f " + matName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniformMatrix4fv(location, 1, false, &mat[0][0]);
			CheckGLErrorMessages();
		}

		glm::uint GLRenderer::GetRenderObjectCount() const
		{
			glm::uint count = 0;

			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				if (m_RenderObjects[i]) ++count;
			}

			return count;
		}

		glm::uint GLRenderer::GetRenderObjectCapacity() const
		{
			return m_RenderObjects.size();
		}

		void GLRenderer::DescribeShaderVariable(RenderID renderID, const std::string& variableName, int size,
			Renderer::Type renderType, bool normalized, int stride, void* pointer)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			GLMaterial* material = &m_Materials[renderObject->materialID];
			glm::uint program = m_Shaders[material->material.shaderID].program;

			glUseProgram(program);

			glBindVertexArray(renderObject->VAO);
			CheckGLErrorMessages();

			GLint location = glGetAttribLocation(program, variableName.c_str());
			if (location == -1)
			{
				//Logger::LogWarning("Invalid shader variable name: " + variableName);
				glBindVertexArray(0);
				return;
			}
			glEnableVertexAttribArray((GLuint)location);

			GLenum glRenderType = TypeToGLType(renderType);
			glVertexAttribPointer((GLuint)location, size, glRenderType, (GLboolean)normalized, stride, pointer);
			CheckGLErrorMessages();

			glBindVertexArray(0);

			glUseProgram(0);
		}

		void GLRenderer::Destroy(RenderID renderID)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			m_RenderObjects[renderObject->renderID] = nullptr;

			glDeleteBuffers(1, &renderObject->VBO);
			if (renderObject->indexed)
			{
				glDeleteBuffers(1, &renderObject->IBO);
			}

			SafeDelete(renderObject);
		}

		void GLRenderer::GetRenderObjectInfos(std::vector<RenderObjectInfo>& vec)
		{
			vec.clear();
			vec.resize(GetRenderObjectCount());

			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				vec[i] = m_RenderObjects[i]->info;
			}
		}

		bool GLRenderer::ImGui_CreateFontsTexture()
		{
			// Build texture atlas
			ImGuiIO& io = ImGui::GetIO();
			unsigned char* pixels;
			int width, height;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

			// Upload texture to graphics system
			GLint last_texture;
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
			glGenTextures(1, &m_ImGuiFontTexture); CheckGLErrorMessages();
			glBindTexture(GL_TEXTURE_2D, m_ImGuiFontTexture); CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

			// Store our identifier
			io.Fonts->TexID = (void *)(intptr_t)m_ImGuiFontTexture;

			// Restore state
			glBindTexture(GL_TEXTURE_2D, last_texture); CheckGLErrorMessages();

			return true;
		}

		void GLRenderer::ImGui_Init(const GameContext& gameContext)
		{
			ImGuiIO& io = ImGui::GetIO();

			io.RenderDrawListsFn = NULL;

			io.SetClipboardTextFn = SetClipboardText;
			io.GetClipboardTextFn = GetClipboardText;
			io.ClipboardUserData = gameContext.window;

			glm::vec2i windowSize = gameContext.window->GetSize();
			glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			io.DisplaySize = ImVec2((float)windowSize.x, (float)windowSize.y);
			io.DisplayFramebufferScale = ImVec2(
				windowSize.x > 0 ? ((float)frameBufferSize.x / windowSize.x) : 0,
				windowSize.y > 0 ? ((float)frameBufferSize.y / windowSize.y) : 0);

			io.DeltaTime = gameContext.deltaTime;

			ImGui_CreateDeviceObjects();
		}

		void GLRenderer::ImGui_NewFrame(const GameContext& gameContext)
		{
			ImGui::NewFrame();

			ImGuiIO& io = ImGui::GetIO();

			glm::vec2i windowSize = gameContext.window->GetSize();
			glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			io.DisplaySize = ImVec2((float)windowSize.x, (float)windowSize.y);
			io.DisplayFramebufferScale = ImVec2(
				windowSize.x > 0 ? ((float)frameBufferSize.x / windowSize.x) : 0,
				windowSize.y > 0 ? ((float)frameBufferSize.y / windowSize.y) : 0);

			io.DeltaTime = gameContext.deltaTime;
		}

		void GLRenderer::ImGui_Render()
		{
			ImGui::Render();
		}

		void GLRenderer::ImGui_ReleaseRenderObjects()
		{
			ImGui_InvalidateDeviceObjects();
		}

		void GLRenderer::DrawImGuiItems(const GameContext& gameContext)
		{
			if (ImGui::CollapsingHeader("Scene info"))
			{
				const std::string sceneCountStr("Scene count: " + std::to_string(gameContext.sceneManager->GetSceneCount()));
				ImGui::Text(sceneCountStr.c_str());
				const std::string currentSceneStr("Current scene: " + gameContext.sceneManager->CurrentScene()->GetName());
				ImGui::Text(currentSceneStr.c_str());
				const glm::uint objectCount = GetRenderObjectCount();
				const glm::uint objectCapacity = GetRenderObjectCapacity();
				const std::string objectCountStr("Object count/capacity: " + std::to_string(objectCount) + "/" + std::to_string(objectCapacity));
				ImGui::Text(objectCountStr.c_str());

				if (ImGui::TreeNode("Render Objects"))
				{
					std::vector<Renderer::RenderObjectInfo> renderObjectInfos;
					GetRenderObjectInfos(renderObjectInfos);
					assert(renderObjectInfos.size() == objectCount);
					for (size_t i = 0; i < objectCount; ++i)
					{
						const std::string objectName(renderObjectInfos[i].name + "##" + std::to_string(i));
						if (ImGui::TreeNode(objectName.c_str()))
						{
							if (renderObjectInfos[i].transform)
							{
								ImGui::Text("Transform");
								
								ImGui::DragFloat3("Translation", &renderObjectInfos[i].transform->position.x, 0.1f);
								glm::vec3 rot = glm::eulerAngles(renderObjectInfos[i].transform->rotation);
								ImGui::DragFloat3("Rotation", &rot.x, 0.01f);
								renderObjectInfos[i].transform->rotation = glm::quat(rot);
								ImGui::DragFloat3("Scale", &renderObjectInfos[i].transform->scale.x, 0.01f);
							}
							else
							{
								ImGui::Text("Transform not set");
							}

							GLMaterial* material = &m_Materials[m_RenderObjects[i]->materialID];
							if (material->uniformIDs.enableIrradianceSampler)
							{
								ImGui::Checkbox("Enable Irradiance Sampler", &material->material.enableIrradianceSampler);
							}

							ImGui::TreePop();
						}
					}

					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Lights"))
				{
					ImGui::AlignFirstTextHeightToWidgets();

					ImGuiColorEditFlags colorEditFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_RGB | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_HDR;

					bool dirLightEnabled = m_DirectionalLight.enabled;
					ImGui::Checkbox("##dir-light-enabled", &dirLightEnabled);
					m_DirectionalLight.enabled = dirLightEnabled ? 1 : 0;
					ImGui::SameLine();
					if (ImGui::TreeNode("Directional Light"))
					{
						ImGui::DragFloat3("Rotation", &m_DirectionalLight.direction.x, 0.01f);

						CopyableColorEdit4("Color ", m_DirectionalLight.color, "c##diffuse", "p##color", colorEditFlags);

						ImGui::TreePop();
					}

					for (size_t i = 0; i < m_PointLights.size(); ++i)
					{
						const std::string iStr = std::to_string(i);
						const std::string objectName("Point Light##" + iStr);

						bool pointLightEnabled = m_PointLights[i].enabled;
						ImGui::Checkbox(std::string("##enabled" + iStr).c_str(), &pointLightEnabled);
						m_PointLights[i].enabled = pointLightEnabled ? 1 : 0;
						ImGui::SameLine();
						if (ImGui::TreeNode(objectName.c_str()))
						{
							ImGui::DragFloat3("Translation", &m_PointLights[i].position.x, 0.1f);

							CopyableColorEdit4("Color ", m_PointLights[i].color, "c##diffuse", "p##color", colorEditFlags);

							ImGui::TreePop();
						}
					}

					ImGui::TreePop();
				}
			}
		}

		bool GLRenderer::ImGui_CreateDeviceObjects()
		{
			// Backup GL state
			GLint last_texture, last_array_buffer, last_vertex_array;
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
			glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
			glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);

			//m_ImGuiShaderHandle = glCreateProgram(); CheckGLErrorMessages();
			//m_ImGuiVertHandle = glCreateShader(GL_VERTEX_SHADER); CheckGLErrorMessages();
			//m_ImGuiFragHandle = glCreateShader(GL_FRAGMENT_SHADER); CheckGLErrorMessages();
			//glShaderSource(m_ImGuiVertHandle, 1, &vertex_shader, 0); CheckGLErrorMessages();
			//glShaderSource(m_ImGuiFragHandle, 1, &fragment_shader, 0); CheckGLErrorMessages();
			//glCompileShader(m_ImGuiVertHandle); CheckGLErrorMessages();
			//glCompileShader(m_ImGuiFragHandle); CheckGLErrorMessages();
			//glAttachShader(m_ImGuiShaderHandle, m_ImGuiVertHandle); CheckGLErrorMessages();
			//glAttachShader(m_ImGuiShaderHandle, m_ImGuiFragHandle); CheckGLErrorMessages();
			//glLinkProgram(m_ImGuiShaderHandle); CheckGLErrorMessages();

			glUseProgram(m_ImGuiShaderHandle);

			m_ImGuiAttribLocationTex = glGetUniformLocation(m_ImGuiShaderHandle, "in_Texture"); CheckGLErrorMessages();
			if (m_ImGuiAttribLocationTex == -1) Logger::LogWarning("in_Texture not found in ImGui shader!");

			m_ImGuiAttribLocationProjMtx = glGetUniformLocation(m_ImGuiShaderHandle, "in_ProjMatrix"); CheckGLErrorMessages();
			if (m_ImGuiAttribLocationProjMtx == -1) Logger::LogWarning("in_ProjMatrix not found in ImGui shader!");

			m_ImGuiAttribLocationPosition = glGetAttribLocation(m_ImGuiShaderHandle, "in_Position2D"); CheckGLErrorMessages();
			if (m_ImGuiAttribLocationPosition == -1) Logger::LogWarning("in_Position2D not found in ImGui shader!");

			m_ImGuiAttribLocationUV = glGetAttribLocation(m_ImGuiShaderHandle, "in_TexCoord"); CheckGLErrorMessages();
			if (m_ImGuiAttribLocationUV == -1) Logger::LogWarning("in_TexCoord not found in ImGui shader!");

			m_ImGuiAttribLocationColor = glGetAttribLocation(m_ImGuiShaderHandle, "in_Color_32"); CheckGLErrorMessages();
			if (m_ImGuiAttribLocationColor == -1) Logger::LogWarning("in_Color_32 not found in ImGui shader!");


			glGenBuffers(1, &m_ImGuiVboHandle);
			glGenBuffers(1, &g_ElementsHandle);

			glGenVertexArrays(1, &m_ImGuiVaoHandle); CheckGLErrorMessages();
			glBindVertexArray(m_ImGuiVaoHandle); CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, m_ImGuiVboHandle); CheckGLErrorMessages();
			glEnableVertexAttribArray((GLuint)m_ImGuiAttribLocationPosition); CheckGLErrorMessages();
			glEnableVertexAttribArray((GLuint)m_ImGuiAttribLocationUV); CheckGLErrorMessages();
			glEnableVertexAttribArray((GLuint)m_ImGuiAttribLocationColor); CheckGLErrorMessages();

#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
			glVertexAttribPointer(m_ImGuiAttribLocationPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, pos));
			glVertexAttribPointer(m_ImGuiAttribLocationUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, uv));
			glVertexAttribPointer(m_ImGuiAttribLocationColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, col));
#undef OFFSETOF

			ImGui_CreateFontsTexture();

			// Restore modified GL state
			glBindTexture(GL_TEXTURE_2D, last_texture); CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer); CheckGLErrorMessages();
			glBindVertexArray(last_vertex_array); CheckGLErrorMessages();

			return true;
		}

		void GLRenderer::ImGui_InvalidateDeviceObjects()
		{
			if (m_ImGuiVaoHandle) glDeleteVertexArrays(1, &m_ImGuiVaoHandle);
			if (m_ImGuiVboHandle) glDeleteBuffers(1, &m_ImGuiVboHandle);
			if (g_ElementsHandle) glDeleteBuffers(1, &g_ElementsHandle);
			m_ImGuiVaoHandle = m_ImGuiVboHandle = g_ElementsHandle = 0;

			if (m_ImGuiShaderHandle) glDeleteProgram(m_ImGuiShaderHandle);
			m_ImGuiShaderHandle = 0;

			if (m_ImGuiFontTexture)
			{
				glDeleteTextures(1, &m_ImGuiFontTexture);
				ImGui::GetIO().Fonts->TexID = 0;
				m_ImGuiFontTexture = 0;
			}
		}

		GLRenderObject* GLRenderer::GetRenderObject(RenderID renderID)
		{
			return m_RenderObjects[renderID];
		}

		void GLRenderer::InsertNewRenderObject(GLRenderObject* renderObject)
		{
			if (renderObject->renderID < m_RenderObjects.size())
			{
				assert(m_RenderObjects[renderObject->renderID] == nullptr);
				m_RenderObjects[renderObject->renderID] = renderObject;
			}
			else
			{
				m_RenderObjects.push_back(renderObject);
			}
		}

		RenderID GLRenderer::GetFirstAvailableRenderID() const
		{
			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				if (!m_RenderObjects[i]) return i;
			}

			return m_RenderObjects.size();
		}


		void SetClipboardText(void* userData, const char* text)
		{
			GLFWWindowWrapper* glfwWindow = static_cast<GLFWWindowWrapper*>(userData);
			glfwWindow->SetClipboardText(text);
		}

		const char* GetClipboardText(void* userData)
		{
			GLFWWindowWrapper* glfwWindow = static_cast<GLFWWindowWrapper*>(userData);
			return glfwWindow->GetClipboardText();
		}

	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL
