#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;

layout (location = 0) out vec4 ex_WorldPos;

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
	float time;
	// vec4 screenSize; // (w, h, 1/w, 1/h)
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

void main()
{
    ex_WorldPos = uboDynamic.model * vec4(in_Position, 1.0);
    gl_Position = uboConstant.viewProjection * ex_WorldPos;
}