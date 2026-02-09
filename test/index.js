const { GstPlayer } = require('../index.js');

const player = new GstPlayer();

// 解析 pipeline 描述
player.parseLaunch('videotestsrc ! queue ! video/x-raw,width=640,height=480,format=I420 ! appsink name=sink');

// 添加 appsink 回调
console.log('Adding app sink callback...');
player.addAppSinkCallback('sink', (event, ...args) => {
  switch (event) {
    case GstPlayer.prototype.AppSinkSetup:
      const [info] = args;
      console.log('Setup:', JSON.stringify(info, null, 2));
      break;
    case GstPlayer.prototype.AppSinkNewPreroll:
      console.log('New Preroll: ArrayBuffer byteLength =', args[0].byteLength);
      break;
    case GstPlayer.prototype.AppSinkNewSample:
      console.log('New Sample: ArrayBuffer byteLength =', args[0].byteLength);
      break;
    case GstPlayer.prototype.AppSinkEos:
      console.log('AppSink EOS');
      break;
  }
});

// 添加 caps probe 回调
console.log('Adding caps probe callback...');
player.addCapsProbe('sink', 'sink', (capsInfo) => {
  console.log('Caps changed:', JSON.stringify(capsInfo, null, 2));
});

// 设置为 PLAYING 状态
console.log('Setting state to PLAYING...');
player.setState(GstPlayer.prototype.GST_STATE_PLAYING);

console.log('Test running... press Ctrl+C to stop');

// 10 秒后停止
setTimeout(() => {
  console.log('Sending EOS...');
  player.sendEos();
}, 10000);
