const { WebGLGstPlayer } = require("node-gst-player");

// 获取当前窗口对象
var win = nw.Window.get();
win.showDevTools();

// 初始化播放器
const canvas = document.getElementById("gl-canvas");
const gstPlayer = new WebGLGstPlayer(canvas);

// 启动播放 - I420 格式 (YUV420)
gstPlayer.start(
  "videotestsrc ! queue ! video/x-raw,width=1920,height=1080,framerate=24/1,format=I420 ! appsink name=sink",
);

// 显示帧率
let lastTime = Date.now();
let lastFrames = gstPlayer.getFrameCount();
setInterval(() => {
  const now = Date.now();
  const nowFrames = gstPlayer.getFrameCount();
  const elapsed = (now - lastTime) / 1000;
  const fps = (nowFrames - lastFrames) / elapsed;
  console.log(`FPS: ${fps.toFixed(1)}`);
  lastTime = now;
  lastFrames = nowFrames;
}, 5000);

// 10 秒后停止
// setTimeout(() => {
//   gstPlayer.stop();
//   console.log('Playback stopped');
// }, 10000);

console.log("WebGL Player initialized with YUV rendering");

// 捕获未处理的异步异常
process.on("uncaughtException", (err) => {
  console.error("Uncaught Exception:", err);
});

process.on("unhandledRejection", (reason, promise) => {
  console.error("Unhandled Rejection at:", promise, "reason:", reason);
});
