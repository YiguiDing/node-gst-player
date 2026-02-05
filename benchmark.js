/**
 * 性能基准测试脚本
 * 用于比较优化前后的性能差异
 */

const GstPlayer = require('./index');
const fs = require('fs');

class PerformanceBenchmark {
    constructor() {
        this.player = null;
        this.metrics = {
            totalFrames: 0,
            totalBytesCopied: 0,
            startTime: 0,
            endTime: 0,
            frameTimes: [],
            zeroCopyCount: 0,
            memcpyCount: 0
        };
    }

    async run(durationSeconds = 30, videoUri = null) {
        console.log('=== 性能基准测试 ===\n');
        console.log(`测试时长: ${durationSeconds} 秒`);
        console.log(`视频 URI: ${videoUri || '默认测试视频'}\n`);

        // 初始化播放器
        this.player = new GstPlayer();

        if (!videoUri) {
            // 使用默认测试视频
            videoUri = 'file:///path/to/test-video.mp4';
            console.log('警告: 请提供有效的视频文件路径');
            console.log('示例: node benchmark.js 30 file:///path/to/video.mp4\n');
        }

        // 设置共享内存信息
        const shmInfo = this.player.getSharedMemoryInfo();
        console.log('共享内存信息:');
        console.log(`  名称: ${shmInfo.name}`);
        console.log(`  大小: ${(shmInfo.size / 1024 / 1024).toFixed(2)} MB\n`);

        // 设置 URI
        this.player.setUri(videoUri);

        // 开始测试
        this.metrics.startTime = Date.now();
        this.player.play();

        console.log('开始采集帧数据...\n');

        await this.collectFrames(durationSeconds * 1000);

        this.player.stop();
        this.metrics.endTime = Date.now();

        this.printReport();
    }

    async collectFrames(durationMs) {
        const startTime = Date.now();

        return new Promise((resolve) => {
            const collect = () => {
                const elapsed = Date.now() - startTime;

                if (elapsed >= durationMs) {
                    resolve();
                    return;
                }

                const frameStartTime = Date.now();

                // 获取帧数据
                const frame = this.player.getFrame();

                if (frame && frame.data) {
                    this.metrics.totalFrames++;
                    this.metrics.totalBytesCopied += frame.size;
                    this.metrics.frameTimes.push(Date.now() - frameStartTime);

                    // 每 100 帧打印一次进度
                    if (this.metrics.totalFrames % 100 === 0) {
                        const fps = (this.metrics.totalFrames / (elapsed / 1000)).toFixed(1);
                        const throughput = ((this.metrics.totalBytesCopied / 1024 / 1024) / (elapsed / 1000)).toFixed(2);
                        console.log(`进度: ${(elapsed / durationMs * 100).toFixed(0)}% | 帧: ${this.metrics.totalFrames} | FPS: ${fps} | 吞吐: ${throughput} MB/s`);
                    }
                }

                setImmediate(collect);
            };

            collect();
        });
    }

    printReport() {
        const duration = (this.metrics.endTime - this.metrics.startTime) / 1000;
        const avgFps = this.metrics.totalFrames / duration;
        const avgThroughput = (this.metrics.totalBytesCopied / 1024 / 1024) / duration;

        console.log('\n=== 测试结果 ===\n');
        console.log(`总帧数: ${this.metrics.totalFrames}`);
        console.log(`测试时长: ${duration.toFixed(2)} 秒`);
        console.log(`平均帧率: ${avgFps.toFixed(2)} FPS`);
        console.log(`吞吐量: ${avgThroughput.toFixed(2)} MB/s`);
        console.log(`总数据量: ${(this.metrics.totalBytesCopied / 1024 / 1024).toFixed(2)} MB\n`);

        // 帧处理时间统计
        if (this.metrics.frameTimes.length > 0) {
            const avgFrameTime = this.metrics.frameTimes.reduce((a, b) => a + b, 0) / this.metrics.frameTimes.length;
            const maxFrameTime = Math.max(...this.metrics.frameTimes);
            const minFrameTime = Math.min(...this.metrics.frameTimes);

            console.log('帧处理时间 (ms):');
            console.log(`  平均: ${avgFrameTime.toFixed(2)} ms`);
            console.log(`  最小: ${minFrameTime.toFixed(2)} ms`);
            console.log(`  最大: ${maxFrameTime.toFixed(2)} ms`);
            console.log(`  中位数: ${this.getMedian(this.metrics.frameTimes).toFixed(2)} ms\n`);
        }

        // 零拷贝统计
        if (this.metrics.zeroCopyCount > 0 || this.metrics.memcpyCount > 0) {
            const zeroCopyRatio = (this.metrics.zeroCopyCount / this.metrics.totalFrames * 100).toFixed(1);
            console.log('内存拷贝统计:');
            console.log(`  零拷贝帧数: ${this.metrics.zeroCopyCount} (${zeroCopyRatio}%)`);
            console.log(`  memcpy 帧数: ${this.metrics.memcpyCount}\n`);
        }

        // 性能评估
        console.log('=== 性能评估 ===\n');

        if (avgThroughput < 5) {
            console.log('⚠️  警告: 吞吐量较低，可能存在优化空间');
        } else if (avgThroughput < 50) {
            console.log('✅ 良好: 吞吐量正常');
        } else {
            console.log('🚀 优秀: 高吞吐量，零拷贝优化有效');
        }

        if (avgFps < 24) {
            console.log('⚠️  警告: 帧率低于 24 FPS，可能存在性能瓶颈');
        } else if (avgFps >= 30) {
            console.log('✅ 优秀: 帧率达到 30 FPS');
        }
    }

    getMedian(arr) {
        const sorted = [...arr].sort((a, b) => a - b);
        const mid = Math.floor(sorted.length / 2);
        return sorted.length % 2 !== 0 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
    }
}

// 运行测试
const args = process.argv.slice(2);
const duration = parseInt(args[0]) || 30;
const videoUri = args[1];

const benchmark = new PerformanceBenchmark();
benchmark.run(duration, videoUri).catch(console.error);

module.exports = PerformanceBenchmark;
