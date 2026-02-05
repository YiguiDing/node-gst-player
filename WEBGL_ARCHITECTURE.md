# 为什么 WebGL 必须使用纹理和着色器？

## 核心问题

> "buffer 中已经是 RGB 数据了，能直接拷贝到显存就好了，为什么要以纹理方式渲染？"

## 简短答案

**WebGL/OpenGL 的渲染管线就是设计成这样：数据必须先转换为纹理，然后通过着色器渲染。**

这背后的原因包括：
1. **GPU 硬件架构**：GPU 无法直接"显示"任意内存，必须通过纹理→帧缓冲流程
2. **Web 安全限制**：浏览器沙盒不允许直接访问显存
3. **跨平台抽象**：提供统一的 API 适配不同硬件
4. **图形渲染通用性**：支持各种效果、滤镜、3D 变换

---

## GPU 渲染管线（无法绕过）

```
系统内存 (CPU)
    ↓ [gl.texImage2D] CPU→GPU 拷贝
GPU 显存 - 纹理 (Texture)
    ↓ [着色器] GPU 并行计算
GPU 显存 - 帧缓冲 (Framebuffer)
    ↓ [硬件合成]
显示器
```

### 为什么不能直接跳到显示？

#### 硬件层面

现代 GPU 的工作原理：

```cpp
// GPU 硬件伪代码
class GPU {
    // 1. GPU 没有直接的"显示内存"指令
    void display(void* system_memory, int width, int height) {
        // ❌ 这种指令不存在！
    }

    // 2. GPU 只能通过固定的渲染管线工作
    void renderPipeline(Texture* texture, Shader* shader) {
        // ✓ 这是 GPU 唯一的工作方式
        Fragment fragment = shader->sample(texture);
        framebuffer->write(fragment);
    }
};
```

**GPU 的设计哲学**：
- GPU 是**并行计算单元**，不是简单的显存显示器
- GPU 必须通过**着色器程序**告诉它"怎么处理像素"
- GPU 有固定的**渲染流水线**：顶点 → 光栅化 → 片段 → 帧缓冲

#### 对比：Linux Framebuffer vs WebGL

##### Linux Framebuffer（可以"直接显示"）

```c
// /dev/fb0 - Linux 帧缓冲设备
int fb = open("/dev/fb0", O_RDWR);
unsigned char* fb_mem = mmap(fb, size);

// ✓ 可以直接写入显示
memcpy(fb_mem, rgb_data, size);  // 直接显示！
```

**为什么可以？**
- `/dev/fb0` 映射到显存中的帧缓冲区
- 直接写入就等于显示
- 不经过 GPU 渲染管线

**缺点**：
- ❌ 只能全屏显示
- ❌ 不能做任何效果/滤镜
- ❌ 不能缩放/旋转
- ❌ 不支持多层叠加
- ❌ Windows/macOS 不支持这种模式

##### WebGL/OpenGL（必须通过纹理）

```javascript
// WebGL 必须这样
const texture = gl.createTexture();
gl.bindTexture(gl.TEXTURE_2D, texture);
gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0,
              gl.RGBA, gl.UNSIGNED_BYTE, data);

// 必须有着色器
const shaderProgram = gl.createProgram();
// ... 编译着色器 ...

// 渲染
gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
```

---

## WebGL 着色器的作用

### 着色器是什么？

着色器是运行在 GPU 上的小程序，告诉 GPU "怎么处理每个像素"。

```glsl
// 片段着色器（最简单的）
precision highp float;
varying highp vec2 vTextureCoord;
uniform sampler2D uSampler;

void main() {
    vec4 color = texture2D(uSampler, vTextureCoord);
    gl_FragColor = color;  // 直接输出纹理颜色
}
```

**这个着色器做了什么？**
1. 从纹理中采样颜色
2. 直接输出到屏幕

### 为什么必须要有着色器？

#### 1. GPU 不知道"显示"是什么

```glsl
// ❌ 如果没有着色器，GPU 不知道该做什么
// GPU 就像一个只懂指令的机器人，必须告诉它：
// - 从哪里取数据？（纹理）
// - 如何处理？（着色器代码）
// - 输出到哪里？（帧缓冲）
```

#### 2. 坐标转换（纹理坐标 → 屏幕坐标）

```glsl
// 顶点着色器
attribute vec4 aVertexPosition;
attribute vec2 aTextureCoord;
varying vec2 vTextureCoord;

void main() {
    gl_Position = aVertexPosition;  // 裁剪空间坐标 (-1 到 1)
    vTextureCoord = aTextureCoord;  // 纹理坐标 (0 到 1)
}
```

**为什么需要？**
- 纹理坐标是 (0,0) 到 (1,1)
- 屏幕坐标是 (-1,-1) 到 (1,1)
- GPU 需要这个转换

#### 3. 支持各种效果（着色器的真正价值）

```glsl
// 颜色反转滤镜
void main() {
    vec4 color = texture2D(uSampler, vTextureCoord);
    gl_FragColor = vec4(1.0 - color.r, 1.0 - color.g,
                        1.0 - color.b, color.a);
}

// 灰度滤镜
void main() {
    vec4 color = texture2D(uSampler, vTextureCoord);
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    gl_FragColor = vec4(gray, gray, gray, color.a);
}

// 高斯模糊
// ... 复杂着色器代码 ...
```

**如果没有着色器，这些效果都无法实现！**

---

## WebGL 渲染的完整流程

### 完整的调用链

```javascript
// 1. 准备顶点数据（告诉 GPU 画什么形状）
const positions = [
    -1.0,  1.0,  // 左上
     1.0,  1.0,  // 右上
    -1.0, -1.0,  // 左下
     1.0, -1.0,  // 右下
];

const positionBuffer = gl.createBuffer();
gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(positions), gl.STATIC_DRAW);

// 2. 准备纹理坐标（告诉 GPU 怎么映射纹理）
const textureCoords = [
    0.0, 0.0,  // 左上纹理坐标
    1.0, 0.0,  // 右上
    0.0, 1.0,  // 左下
    1.0, 1.0,  // 右下
];

const texCoordBuffer = gl.createBuffer();
gl.bindBuffer(gl.ARRAY_BUFFER, texCoordBuffer);
gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(textureCoords), gl.STATIC_DRAW);

// 3. 上传纹理数据（gl.texImage2D - CPU→GPU 拷贝）
const texture = gl.createTexture();
gl.bindTexture(gl.TEXTURE_2D, texture);
gl.texImage2D(
    gl.TEXTURE_2D,
    0,                // mipmap 层级
    gl.RGBA,          // 内部格式
    width, height,
    0,                // 边界
    gl.RGBA,          // 源格式
    gl.UNSIGNED_BYTE, // 数据类型
    data              // ← 这里发生 CPU→GPU 拷贝
);

// 4. 编译着色器（告诉 GPU 怎么渲染）
const vertexShader = compileShader(gl.VERTEX_SHADER, vertexSource);
const fragmentShader = compileShader(gl.FRAGMENT_SHADER, fragmentSource);
const shaderProgram = gl.createProgram();
gl.attachShader(shaderProgram, vertexShader);
gl.attachShader(shaderProgram, fragmentShader);
gl.linkProgram(shaderProgram);
gl.useProgram(shaderProgram);

// 5. 设置属性（连接 JS 数据和着色器变量）
const vertexPosition = gl.getAttribLocation(shaderProgram, 'aVertexPosition');
gl.enableVertexAttribArray(vertexPosition);
gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
gl.vertexAttribPointer(vertexPosition, 2, gl.FLOAT, false, 0, 0);

const textureCoord = gl.getAttribLocation(shaderProgram, 'aTextureCoord');
gl.enableVertexAttribArray(textureCoord);
gl.bindBuffer(gl.ARRAY_BUFFER, texCoordBuffer);
gl.vertexAttribPointer(textureCoord, 2, gl.FLOAT, false, 0, 0);

// 6. 绘制（GPU 执行着色器）
gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
```

### GPU 内部发生了什么？

```
1. 顶点着色器 (每个顶点执行一次)
   └─ 转换坐标：纹理坐标 (0,1) → 屏幕坐标 (-1,1)

2. 光栅化 (GPU 自动)
   └─ 将三角形转换为像素

3. 片段着色器 (每个像素执行一次)
   └─ 从纹理采样颜色
   └─ 可能应用效果（灰度、模糊等）
   └─ 输出到帧缓冲

4. 输出合并 (GPU 自动)
   └─ 深度测试、混合等
   └─ 最终显示到屏幕
```

---

## 能不能简化？

### 方案对比

#### ❌ 方案 1：直接显存写入（不可能）

```javascript
// ❌ WebGL 没有这样的 API
gl.writeToFramebuffer(data, 0, 0, width, height);
```

**原因**：
- WebGL 规范不允许直接写帧缓冲
- 沙盒安全限制
- 不支持跨平台

#### ❌ 方案 2：跳过着色器（不可能）

```javascript
// ❌ 没有"无着色器"模式
gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4, { shader: null });
```

**原因**：
- GPU 渲染管线必须有着色器
- 这是 OpenGL/WebGL 的核心设计

#### ✓ 方案 3：使用最简单的着色器（可行）

```glsl
// 片段着色器 - 最简版本
precision highp float;
varying vec2 vTextureCoord;
uniform sampler2D uSampler;

void main() {
    gl_FragColor = texture2D(uSampler, vTextureCoord);
}
```

**这个着色器只是简单采样纹理，不做任何处理，这就是最简形式。**

---

## 其他图形框架对比

### OpenGL / Vulkan / Metal

它们都需要：
1. 纹理上传
2. 着色器程序
3. 渲染命令

**区别在于**：
- OpenGL/Vulkan/Metal 可以直接写显存（无沙盒限制）
- 但仍需要纹理和着色器（硬件架构决定）

### Direct2D (Windows)

```cpp
// Direct2D - 也需要位图，不是"直接显示"
ID2D1Bitmap* bitmap;
d2dContext->CreateBitmap(
    D2D1::SizeU(width, height),
    data,
    pitch,
    D2D1::BitmapProperties(),
    &bitmap
);
d2dContext->DrawBitmap(bitmap);  // 仍需要渲染命令
```

### Cairo / Skia (2D 绘图库)

```cpp
// Cairo - 也需要表面，不是"直接显示"
cairo_surface_t* surface = cairo_image_surface_create_for_data(
    data, CAIRO_FORMAT_ARGB32, width, height, stride
);
cairo_t* cr = cairo_create(surface);
cairo_paint(cr);
```

### HTML5 Canvas 2D

```javascript
// Canvas 2D - 更简单，但内部还是用了 GPU
const ctx = canvas.getContext('2d');
const imgData = ctx.createImageData(width, height);
imgData.data.set(data);
ctx.putImageData(imgData, 0, 0);  // 仍需要绘制命令
```

**Canvas 2D vs WebGL**：
- Canvas 2D API 更简单，但内部还是用 GPU 渲染
- WebGL API 更底层，性能更好，但更复杂

---

## 性能分析

### gl.texImage2D 的开销

| 操作 | 开销 | 位置 |
|------|------|------|
| 数据验证 | 小 | CPU |
| 格式转换 | 小 | CPU |
| PCIe 传输 | **大** | CPU→GPU |
| 纹理创建 | 小 | GPU |
| Mipmap 生成 | 中 | GPU (如果启用) |

**主要开销**：PCIe 总线传输（约 8-12 GB/s 带宽）

### 着色器的开销

| 操作 | 开销 | 备注 |
|------|------|------|
| 顶点着色器 | 极小 | 4 个顶点 |
| 光栅化 | 小 | GPU 硬件 |
| 片段着色器 | 小 | 每像素 1 次采样 |
| 总计 | **< 0.1 ms** | 1080p @ 60fps |

**着色器开销极小，几乎可以忽略！**

### 总体性能

```
gl.texImage2D (8.3 MB):  ~0.5-1 ms
着色器渲染 (1080p):      ~0.1 ms
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
总计 (每帧):             ~0.6-1.1 ms
```

**结论**：`gl.texImage2D` 占据了绝大部分时间，着色器开销极小。

---

## 能不能避免 gl.texImage2D？

### 方案 A：GPU 共享内存（零拷贝）

**理论可行**：

```cpp
// 使用 EGLImage (Linux)
EGLImage eglImage = eglCreateImageKHR(
    display,
    EGL_NO_CONTEXT,
    EGL_LINUX_DMA_BUF_EXT,
    NULL,
    attribs
);

// WebGL 端
gl.EGLImageTargetTexture2DOES(gl.TEXTURE_2D, eglImage);
// ← 零拷贝！GPU 直接读取 DMA buffer
```

**实际限制**：
- ❌ WebGL 不支持 EGLImage API
- ❌ 浏览器安全限制
- ❌ 跨平台兼容性差

### 方案 B：硬件解码直接输出纹理

```cpp
// 使用 VideoToolbox (macOS) / VAAPI (Linux)
// 硬件解码直接输出到 GPU 纹理
VTDecompressionSessionDecodeFrame(
    session,
    sample,
    0,
    &outputPixelBuffer,
    NULL
);

// outputPixelBuffer 可以直接映射为纹理
CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sample);
IOSurfaceRef surface = CVPixelBufferGetIOSurface(pixelBuffer);
// WebGL 可以直接使用这个 surface
```

**实际限制**：
- ⚠️ 需要 WebGL 扩展支持
- ⚠️ 只有 macOS/Linux 支持
- ❌ Windows 需要不同的 API

### 方案 C：使用 SharedArrayBuffer + WebGL（当前方案）

```javascript
// SharedArrayBuffer - 跨进程零拷贝
const sharedBuffer = new SharedArrayBuffer(size);
// GStreamer 直接写入这个 buffer

// WebGL 上传（仍有 CPU→GPU 拷贝）
gl.texImage2D(..., new Uint8Array(sharedBuffer));
```

**优点**：
- ✓ 跨进程零拷贝
- ✓ 跨平台支持
- ✓ 相对简单

**缺点**：
- ❌ 仍有 gl.texImage2D 开销

---

## 结论

### 为什么必须用纹理和着色器？

1. **GPU 硬件架构**：GPU 通过渲染管线工作，无法"直接显示"内存
2. **Web 安全限制**：浏览器沙盒不允许直接访问显存
3. **跨平台抽象**：统一 API 适配不同硬件
4. **功能需求**：支持各种效果、变换、3D 渲染

### 着色器真的是必须的吗？

**是的！**
- 着色器是 GPU 渲染的核心
- 即使是最简单的着色器也是必须的
- 着色器开销极小（<0.1 ms）

### gl.texImage2D 可以避免吗？

**理论上可以，但实际很难：**
- GPU 共享内存（EGLImage/CGLIOSurface）- WebGL 不支持
- 硬件解码直接纹理 - 需要特定平台
- WebGL 安全限制 - 浏览器不允许

### 当前方案是最优解吗？

**是的，在浏览器环境中：**
- ✅ 共享内存 → ArrayBuffer（零拷贝）
- ✅ ArrayBuffer → gl.texImage2D（仅 1 次 CPU→GPU 拷贝）
- ✅ 最小化着色器（只做纹理采样）
- ✅ 跨平台兼容

### 对比其他方案

| 方案 | gl.texImage2D | 着色器 | 零拷贝 | 跨平台 |
|------|--------------|-------|--------|--------|
| WebGL 当前方案 | ❌ 需要 | ❌ 需要 | 部分 | ✅ |
| GPU 共享内存 | ❌ 需要 | ❌ 需要 | ✅ | ❌ |
| 硬件解码纹理 | ❌ 不需要 | ❌ 需要 | ✅ | ❌ |
| Linux Framebuffer | ❌ 不需要 | ❌ 不需要 | ✅ | ❌ |
| Canvas 2D | ❌ 内部需要 | ❌ 内部需要 | ❌ | ✅ |

**WebGL 当前方案是在"零拷贝"、"跨平台"、"易实现"之间的最佳平衡。**

---

## 进一步优化方向

### 1. 使用 WebGL2 的优化

```javascript
// WebGL2 支持更高效的上传
gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA8, width, height);
gl.texSubImage2D(...)  // 比 texImage2D 更快
```

### 2. 使用 GPU 加速解码

```javascript
// H.264 直接解码到纹理
const video = document.createElement('video');
const texture = gl.createTexture();
gl.bindTexture(gl.TEXTURE_2D, texture);
video.addEventListener('play', () => {
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, video);
});
```

### 3. 使用 WebCodecs API

```javascript
// WebCodecs - 更底层的控制
const decoder = new VideoDecoder({
    output: frame => {
        // frame 可以直接传递给 WebGL
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, frame);
    },
    error: console.error
});
decoder.configure({ codec: 'avc1.42E01E' });
```

### 4. 使用 WebGPU（未来）

```javascript
// WebGPU - 更底层，更高效
const device = await navigator.gpu.requestDevice();
const texture = device.createTexture({
    size: [width, height],
    format: 'rgba8unorm',
    usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING
});

// 零拷贝上传（理论）
queue.writeTexture(
    { texture },
    data,
    { bytesPerRow: width * 4 },
    { width, height }
);
```

---

## 总结

> "为什么不能直接拷贝到显存显示？"

**答案**：
1. **GPU 架构限制**：GPU 通过渲染管线工作，无法"直接显示"
2. **Web 安全限制**：浏览器沙盒不允许直接操作显存
3. **跨平台抽象**：需要统一 API 适配不同硬件
4. **功能需求**：支持各种效果、变换、3D 渲染

> "为什么要编译着色器？"

**答案**：
1. **GPU 必需**：GPU 渲染管线必须有程序
2. **坐标转换**：纹理坐标 → 屏幕坐标
3. **效果支持**：灰度、模糊、滤镜等
4. **开销极小**：<0.1 ms，几乎可以忽略

> "gl.texImage2D 可以避免吗？"

**答案**：
- **理论上可以**：GPU 共享内存、硬件解码纹理
- **实际很难**：WebGL 限制、跨平台问题
- **当前方案最优**：在浏览器环境下的最佳平衡

**WebGL 的纹理 + 着色器模式不是"额外的复杂度"，而是 GPU 渲染的标准方式，无法避免也不应该避免。**
