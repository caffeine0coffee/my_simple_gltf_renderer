#version 450

// 頂点シェーダーからラスタライザー経由で補間されたカラー入力
layout(location = 0) in vec3 fragColor;

// フレームバッファ（カラーアタッチメント 0）への出力カラー
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
