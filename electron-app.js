const { app, BrowserWindow } = require('electron');
const path = require('path');
const GstPlayer = require('./index');

let mainWindow;
let player;
let frameCount = 0;

function createWindow() {
    mainWindow = new BrowserWindow({
        width: 1280,
        height: 720,
        webPreferences: {
            nodeIntegration: true,
            contextIsolation: false,
            webGL: true,
            experimentalFeatures: true
        }
    });

    mainWindow.loadFile('index.html');
    
    // Open DevTools for debugging
    mainWindow.webContents.openDevTools();
    
    mainWindow.on('closed', () => {
        mainWindow = null;
    });
    
    // Initialize GstPlayer after window is ready
    mainWindow.webContents.on('did-finish-load', () => {
        initializePlayer();
    });
}

function initializePlayer() {
    player = new GstPlayer();
    
    const shmInfo = player.getSharedMemoryInfo();
    console.log('Shared Memory:', shmInfo.name, shmInfo.size);
    
    // Send shared memory info to renderer
    mainWindow.webContents.send('shm-info', shmInfo);
    
    // Set video URI (modify as needed)
    const videoUri = 'file:///path/to/video.mp4';
    player.setUri(videoUri);
    
    // Start frame polling loop
    startFrameLoop();
    
    player.play();
}

function startFrameLoop() {
    const pollFrames = () => {
        if (!mainWindow) return;
        
        const frame = player.getFrame();
        if (frame && frame.data && frame.width > 0) {
            frameCount++;
            
            // Send frame info to renderer
            mainWindow.webContents.send('new-frame', {
                width: frame.width,
                height: frame.height,
                format: frame.format,
                size: frame.size,
                frameNumber: frameCount
            });
        }
        
        setImmediate(pollFrames);
    };
    
    pollFrames();
}

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
    if (player) {
        player.stop();
    }
    if (process.platform !== 'darwin') {
        app.quit();
    }
});

app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
        createWindow();
    }
});
