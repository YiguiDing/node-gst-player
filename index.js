const GstPlayer = require('bindings')('gst_player');

// Example usage
const player = new GstPlayer();

// Set up event listeners
player.on('frame', (frameData) => {
    console.log('New frame:', frameData.width, 'x', frameData.height);
    // Handle frame data
});

// Play a video
player.setUri('file:///path/to/video.mp4');
player.play();

// Get current frame info
const frame = player.getFrame();
if (frame.data) {
    console.log('Frame buffer size:', frame.size);
    // frame.data is a Buffer/ArrayBuffer pointing to shared memory
}

// Stop playback
player.stop();

module.exports = GstPlayer;
