#version 450

// フラグメントシェーダーへ渡す補間カラー
layout(location = 0) out vec3 fragColor;

// 画面中央に配置する三角形の頂点座標（NDC座標系: X[-1, 1], Y[-1, 1], Z[0, 1]）
// VulkanのNDCではY軸が下向き正であるため、時計回り(Clockwise)の順序で指定します。
const vec2 kPositions[3] = vec2[](
    vec2(0.0, -0.5),  // 上
    vec2(0.5, 0.5),   // 右下
    vec2(-0.5, 0.5)   // 左下
);

// 各頂点に対応するRGBカラー（赤、緑、青）
const vec3 kColors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),  // 赤
    vec3(0.0, 1.0, 0.0),  // 緑
    vec3(0.0, 0.0, 1.0)   // 青
);

void main() {
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
    fragColor = kColors[gl_VertexIndex];
}
