const GstPlayer = require('./index');

console.log('Testing GstPlayer Node Addon...\n');

// Create player instance
const player = new GstPlayer();

// Get shared memory info
const shmInfo = player.getSharedMemoryInfo();
console.log('Shared Memory Info:');
console.log('  Name:', shmInfo.name);
console.log('  Size:', shmInfo.size, 'bytes\n');

// Set a test URI (replace with actual video path)
const testUri = 'file:///path/to/test/video.mp4';
console.log('Setting URI:', testUri);
player.setUri(testUri);

console.log('\nStarting playback...');
player.play();

// Poll for frames periodically
let frameCount = 0;
const interval = setInterval(() => {
    const frame = player.getFrame();
    
    if (frame && frame.data) {
        frameCount++;
        if (frameCount % 30 === 0) { // Log every 30 frames
            console.log(`Frame ${frameCount}: ${frame.width}x${frame.height}, format: ${frame.format}`);
        }
    }
}, 33); // ~30 FPS

// Stop after 5 seconds
setTimeout(() => {
    console.log('\nStopping playback...');
    player.pause();
    clearInterval(interval);
    console.log(`Total frames captured: ${frameCount}`);
    console.log('\nTest completed!');
}, 5000);
