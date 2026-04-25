#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;

uniform vec3 facenormal;

vec3 lightDir = vec3(0.5, 1.0, 0.3);


out vec4 finalColor;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec3 N = facenormal;
    vec3 L = normalize(lightDir);
    float NdotL = max(dot(N, L), 0.0);
    finalColor = vec4(texel.rgb * NdotL, texel.a);
}
