# 安装指南

## 前置要求

### Linux (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-libav \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-good1.0-dev \
    build-essential \
    nodejs \
    npm
```

### macOS
```bash
brew install gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-libav node
```

### Windows (MSYS2)
```bash
pacman -S mingw-w64-x86_64-gstreamer \
    mingw-w64-x86_64-gst-plugins-base \
    mingw-w64-x86_64-gst-plugins-good \
    mingw-w64-x86_64-gst-plugins-bad \
    mingw-w64-x86_64-gst-libav \
    mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-make \
    nodejs
```

## 编译

```bash
# 安装 npm 依赖
npm install

# 编译 native addon
node-gyp rebuild
```

## 测试

### Node.js 测试
```bash
node test.js
```

### Electron 测试
```bash
npm run electron
```

## 使用示例

### Node.js 基础用法
```javascript
const GstPlayer = require('./index');
const player = new GstPlayer();

// 设置视频 URI
player.setUri('file:///path/to/video.mp4');

// 开始播放
player.play();

// 获取当前帧
const frame = player.getFrame();
console.log(frame); 
// 输出: { width, height, size, format, data: ArrayBuffer }

// 获取共享内存信息
const shmInfo = player.getSharedMemoryInfo();
console.log(shmInfo);
// 输出: { name: '/gst_player_xxx', size: 8294400 }

// 暂停/停止
player.pause();
player.stop();
```

### Electron WebGL 渲染
```javascript
const GstPlayer = require('./index');
const player = new GstPlayer();

player.setUri('file:///path/to/video.mp4');
player.play();

// 轮询帧并渲染到 WebGL
setInterval(() => {
    const frame = player.getFrame();
    if (frame && frame.data) {
        // 使用 WebGL 渲染帧数据
        updateTexture(frame.width, frame.height, new Uint8Array(frame.data));
    }
}, 33); // ~30 FPS
```

完整示例请查看 `index.html` 和 `electron-app.js`。

## 共享内存说明

本项目使用 POSIX 共享内存实现零拷贝数据传输：

- 共享内存名称: `/gst_player_<pid>`
- 默认大小: 8294400 字节 (1920x1080 RGBA)
- 数据格式: RGBA (每像素 4 字节)

## 故障排除

### 编译失败
确保已安装所有 GStreamer 开发包:
```bash
# Linux
pkg-config --cflags gstreamer-1.0

# Windows (MSYS2)
echo $PKG_CONFIG_PATH
```

### 运行时错误
检查 GStreamer 插件是否正确安装:
```bash
gst-inspect-1.0 uridecodebin
gst-inspect-1.0 appsink
```

### Electron 渲染问题
确保在 `webPreferences` 中启用了 WebGL:
```javascript
webPreferences: {
    webGL: true,
    experimentalFeatures: true
}
```

## 性能优化建议

1. **使用自定义 GstShmSink**: 替换 `appsink` 以获得更好的性能
2. **调整共享内存大小**: 根据视频分辨率调整 `shm_size_`
3. **减少轮询频率**: 根据视频帧率调整 `setInterval` 间隔
4. **使用双缓冲**: 避免读取和写入同一帧时的竞态条件

## 许可证

MIT License
