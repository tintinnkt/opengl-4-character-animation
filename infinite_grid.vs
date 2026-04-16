#version 330 core

// Full-screen quad in clip space — just pass NDC xy to fragment shader.
// z = 0.9999 so the quad renders behind everything but still depth-tests.

layout (location = 0) in vec2 aPos;

out vec2 ndcPos;

void main()
{
    ndcPos      = aPos;
    gl_Position = vec4(aPos, 0.9999, 1.0);
}
