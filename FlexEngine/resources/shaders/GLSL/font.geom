#version 400 core

layout(points, invocations = 1) in;
layout(triangle_strip, max_vertices = 4) out;

in VSO
{
    vec2 position;
    vec4 color;
    vec2 texCoord;
    vec2 charSize;
	flat int channel;
} inputs[];

out GSO
{
	flat int channel;
	vec4 color;
	vec2 texCoord;
} outputs;

uniform mat4 transformMat;
uniform vec2 texSize;

void main()
{
	vec2 charSize = inputs[0].charSize;

	// TODO: Divide by z for neato scaling effect
	//vec2 transformedPos = (vec4(inputs[0].position, 0, 1) * transformMat).xy;
	
	vec2 pos = inputs[0].position;
	vec2 uv = inputs[0].texCoord;
	
	vec2 normUV = vec2(charSize.x, charSize.y) / texSize;
	
	outputs.channel = inputs[0].channel;
	gl_Position = transformMat * vec4(pos.x, pos.y + charSize.y, 0, 1);
	outputs.color = inputs[0].color;
	outputs.texCoord = uv + vec2(0, normUV.y);
	EmitVertex();
	
	outputs.channel = inputs[0].channel;
	gl_Position = transformMat * vec4(pos.x + charSize.x, pos.y + charSize.y, 0, 1);
	outputs.color = inputs[0].color;
	outputs.texCoord = uv + normUV;
	EmitVertex();
	
	outputs.channel = inputs[0].channel;
	gl_Position = transformMat * vec4(pos.x, pos.y, 0, 1);
	outputs.color = inputs[0].color;
	outputs.texCoord = uv;
	EmitVertex();
	
	outputs.channel = inputs[0].channel;
	gl_Position = transformMat * vec4(pos.x + charSize.x, pos.y, 0, 1);
	outputs.color = inputs[0].color;
	outputs.texCoord = uv + vec2(normUV.x, 0);
	EmitVertex();
	
	EndPrimitive();
}