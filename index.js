const { GstPlayer } = require("bindings")("node-gst-player.node");

class WebGLGstPlayer {
    constructor(canvas) {
        this.canvas = canvas;
        this.gl = canvas.getContext('webgl2') || canvas.getContext('webgl');
        if (!this.gl) {
            throw new Error('WebGL not supported');
        }

        this.player = new GstPlayer();
        this.textures = null;
        this.shaderProgram = null;
        this.frameCount = 0;
        this.videoInfo = null;

        this.initWebGL();
        this.setupWindowEvents();
    }
    setupWindowEvents() {
        const { getCurrentWindow } = require('@electron/remote');
        try {
            const win = getCurrentWindow();

            win.on('minimize', () => {
                console.log('Window minimized, pausing playback');
                this.player.setState(this.player.GST_STATE_PAUSED);
            });

            win.on('restore', () => {
                console.log('Window restored, resuming playback');
                this.player.setState(this.player.GST_STATE_PLAYING);
            });

            win.on('maximize', () => {
                console.log('Window maximized, resuming playback');
                this.player.setState(this.player.GST_STATE_PLAYING);
            });
        } catch (e) {
            console.log('Electron remote not available:', e.message);
        }
    }

    initWebGL() {
        const gl = this.gl;

        // 顶点着色器
        const vsSource = `
      attribute vec2 a_position;
      attribute vec2 a_texCoord;
      varying vec2 v_texCoord;
      void main() {
        gl_Position = vec4(a_position, 0.0, 1.0);
        v_texCoord = a_texCoord;
      }
    `;

        // 片段着色器 (I420 YUV to RGB)
        const fsSource = `
      precision mediump float;
      uniform sampler2D u_yTexture;
      uniform sampler2D u_uTexture;
      uniform sampler2D u_vTexture;
      varying vec2 v_texCoord;

      const mat3 yuv2rgb = mat3(
        1.0,  1.0,     1.0,
        0.0, -0.39465, 2.03211,
        1.13983, -0.58060, 0.0
      );

      vec3 yuv2rgb_convert(float y, float u, float v) {
        vec3 yuv = vec3(y, u - 0.5, v - 0.5);
        return yuv2rgb * yuv;
      }

      void main() {
        float y = texture2D(u_yTexture, v_texCoord).r;
        float u = texture2D(u_uTexture, v_texCoord).r;
        float v = texture2D(u_vTexture, v_texCoord).r;

        vec3 rgb = yuv2rgb_convert(y, u, v);
        gl_FragColor = vec4(rgb, 1.0);
      }
    `;

        // 编译着色器
        const vs = this.compileShader(gl.VERTEX_SHADER, vsSource);
        const fs = this.compileShader(gl.FRAGMENT_SHADER, fsSource);

        // 创建程序
        this.shaderProgram = gl.createProgram();
        gl.attachShader(this.shaderProgram, vs);
        gl.attachShader(this.shaderProgram, fs);
        gl.linkProgram(this.shaderProgram);

        if (!gl.getProgramParameter(this.shaderProgram, gl.LINK_STATUS)) {
            throw new Error('Shader program link failed: ' + gl.getProgramInfoLog(this.shaderProgram));
        }

        // 创建缓冲区
        const positions = new Float32Array([
            -1.0, -1.0,
            1.0, -1.0,
            -1.0, 1.0,
            1.0, 1.0,
        ]);

        const texCoords = new Float32Array([
            0.0, 1.0,
            1.0, 1.0,
            0.0, 0.0,
            1.0, 0.0,
        ]);

        this.positionBuffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.positionBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, positions, gl.STATIC_DRAW);

        this.texCoordBuffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.texCoordBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, texCoords, gl.STATIC_DRAW);

        // 获取属性和 uniform 位置
        this.positionLoc = gl.getAttribLocation(this.shaderProgram, 'a_position');
        this.texCoordLoc = gl.getAttribLocation(this.shaderProgram, 'a_texCoord');
        this.yTextureLoc = gl.getUniformLocation(this.shaderProgram, 'u_yTexture');
        this.uTextureLoc = gl.getUniformLocation(this.shaderProgram, 'u_uTexture');
        this.vTextureLoc = gl.getUniformLocation(this.shaderProgram, 'u_vTexture');
    }

    compileShader(type, source) {
        const gl = this.gl;
        const shader = gl.createShader(type);
        gl.shaderSource(shader, source);
        gl.compileShader(shader);

        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
            const error = gl.getShaderInfoLog(shader);
            gl.deleteShader(shader);
            throw new Error('Shader compile error: ' + error);
        }

        return shader;
    }

    createTexture() {
        const gl = this.gl;
        const texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        return texture;
    }

    initVideo(width, height, format) {
        const gl = this.gl;

        // 调整 canvas 大小
        this.canvas.width = width;
        this.canvas.height = height;

        // 释放旧的纹理
        if (this.textures) {
            gl.deleteTexture(this.textures.y);
            gl.deleteTexture(this.textures.u);
            gl.deleteTexture(this.textures.v);
        }

        // 创建 YUV 三个纹理
        this.textures = {
            y: this.createTexture(),
            u: this.createTexture(),
            v: this.createTexture()
        };

        this.videoInfo = { width, height, format };
        console.log(`Video initialized: ${width}x${height}, format: ${format}`);
    }

    updateYUVTextures(yuvData) {
        const gl = this.gl;
        const { width, height } = this.videoInfo;

        // I420 格式: Y plane (full size), U plane (1/4 size), V plane (1/4 size)
        const ySize = width * height;
        const uvSize = ySize / 4;
        const uvWidth = width / 2;
        const uvHeight = height / 2;

        // Y plane
        gl.bindTexture(gl.TEXTURE_2D, this.textures.y);
        gl.texImage2D(
            gl.TEXTURE_2D,
            0,
            gl.LUMINANCE,
            width,
            height,
            0,
            gl.LUMINANCE,
            gl.UNSIGNED_BYTE,
            yuvData.subarray(0, ySize)
        );

        // U plane
        gl.bindTexture(gl.TEXTURE_2D, this.textures.u);
        gl.texImage2D(
            gl.TEXTURE_2D,
            0,
            gl.LUMINANCE,
            uvWidth,
            uvHeight,
            0,
            gl.LUMINANCE,
            gl.UNSIGNED_BYTE,
            yuvData.subarray(ySize, ySize + uvSize)
        );

        // V plane
        gl.bindTexture(gl.TEXTURE_2D, this.textures.v);
        gl.texImage2D(
            gl.TEXTURE_2D,
            0,
            gl.LUMINANCE,
            uvWidth,
            uvHeight,
            0,
            gl.LUMINANCE,
            gl.UNSIGNED_BYTE,
            yuvData.subarray(ySize + uvSize, ySize + uvSize * 2)
        );
    }

    render() {
        const gl = this.gl;

        // 渲染
        gl.viewport(0, 0, this.canvas.width, this.canvas.height);
        gl.clearColor(0.0, 0.0, 0.0, 1.0);
        gl.clear(gl.COLOR_BUFFER_BIT);

        gl.useProgram(this.shaderProgram);

        // 绑定位置
        gl.bindBuffer(gl.ARRAY_BUFFER, this.positionBuffer);
        gl.enableVertexAttribArray(this.positionLoc);
        gl.vertexAttribPointer(this.positionLoc, 2, gl.FLOAT, false, 0, 0);

        // 绑定纹理坐标
        gl.bindBuffer(gl.ARRAY_BUFFER, this.texCoordBuffer);
        gl.enableVertexAttribArray(this.texCoordLoc);
        gl.vertexAttribPointer(this.texCoordLoc, 2, gl.FLOAT, false, 0, 0);

        // 绑定 YUV 纹理
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.textures.y);
        gl.uniform1i(this.yTextureLoc, 0);

        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, this.textures.u);
        gl.uniform1i(this.uTextureLoc, 1);

        gl.activeTexture(gl.TEXTURE2);
        gl.bindTexture(gl.TEXTURE_2D, this.textures.v);
        gl.uniform1i(this.vTextureLoc, 2);

        // 绘制
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);

        this.frameCount++;
    }

    renderFrame(arrayBuffer) {
        try {
            const gl = this.gl;

            if (!this.textures) {
                console.warn('Textures not initialized, skipping frame');
                return;
            }

            // 转换为 Uint8Array 并更新纹理
            const yuvData = new Uint8Array(arrayBuffer);
            // console.log(`Rendering frame: ${yuvData.length} bytes, expected ${this.videoInfo.width * this.videoInfo.height * 1.5} bytes`);

            this.updateYUVTextures(yuvData);
            this.render();
        } catch (error) {
            console.error('Error rendering frame:', error);
        }
    }

    start(pipelineDesc) {
        this.player.parseLaunch(pipelineDesc);

        this.player.addAppSinkCallback('sink', (event, ...args) => {
            try {
                switch (event) {
                    case this.player.AppSinkSetup:
                        const [info] = args;
                        console.log('Setup:', JSON.stringify(info, null, 2));
                        this.initVideo(info.width, info.height, info.pixelFormat);
                        break;

                    case this.player.AppSinkNewPreroll:
                        console.log('Preroll received');
                        break;

                    case this.player.AppSinkNewSample:
                        this.renderFrame(args[0]);
                        break;

                    case this.player.AppSinkEos:
                        console.log('EOS received');
                        break;
                }
            } catch (error) {
                console.error('Callback error:', error);
            }
        });

        this.player.setState(this.player.GST_STATE_PLAYING);
        console.log('Playback started');
    }

    stop() {
        this.player.sendEos();
        console.log('Playback stopped');
    }

    getFrameCount() {
        return this.frameCount;
    }
}

module.exports = {
    GstPlayer,
    WebGLGstPlayer
};
