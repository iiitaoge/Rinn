#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;

uniform sampler2D sprite_normal;


uniform vec3 tangent;
uniform vec3 bitangent;
uniform vec3 facenormal;

uniform vec3 lightDir;  // 太阳在哪
uniform vec3 light_color;   // 太阳是什么颜色 白色：RGB 全满
uniform vec3 ambient_color; // 阴影颜色



out vec4 finalColor;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec4 norel = texture(sprite_normal, fragTexCoord);

    mat3 TBN = mat3(tangent, bitangent, facenormal);
    vec3 normal = norel.rgb * 2.0 - 1.0;

    vec3 N = TBN * normal;
    N = normalize(N);
    vec3 L = normalize(lightDir);
    float NdotL = max(dot(N, L), 0.0);
    vec3 lighting = mix(ambient_color, light_color, NdotL);
    finalColor = vec4(texel.rgb * lighting, texel.a);
    //finalColor = vec4(N * 0.5 + 0.5, 1.0);
}
