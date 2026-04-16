#version 330 core
out vec4 FragColor;

uniform float alpha;
uniform vec3  dustColor;

void main()
{
    FragColor = vec4(dustColor, alpha);
}
