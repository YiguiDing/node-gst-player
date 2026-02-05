#ifndef GST_PLAYER_H
#define GST_PLAYER_H

#include <node.h>
#include <node_object_wrap.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/app/gstappsink.h>
#include <atomic>
#include <memory>

class GstPlayer : public node::ObjectWrap {
public:
    static void Init(v8::Local<v8::Object> exports);
    
    // Public methods callable from JS
    void Play(const v8::FunctionCallbackInfo<v8::Value>& args);
    void Pause(const v8::FunctionCallbackInfo<v8::Value>& args);
    void Stop(const v8::FunctionCallbackInfo<v8::Value>& args);
    void SetUri(const v8::FunctionCallbackInfo<v8::Value>& args);
    void GetFrame(const v8::FunctionCallbackInfo<v8::Value>& args);
    void GetSharedMemoryInfo(const v8::FunctionCallbackInfo<v8::Value>& args);
    
private:
    explicit GstPlayer();
    ~GstPlayer();
    
    static v8::Persistent<v8::Function> constructor;
    static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
    
    // GStreamer pipeline setup
    bool SetupPipeline();
    bool SetupSharedMemory();
    void CleanupPipeline();
    
    // GStreamer callbacks
    static GstFlowReturn OnNewSample(GstAppSink* appsink, gpointer user_data);
    static GstCaps* OnQueryCaps(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    
    // Helper methods
    void ProcessFrame(GstSample* sample);
    
    // GStreamer elements
    GstElement* pipeline_;
    GstElement* uridecodebin_;
    GstElement* videoconvert_;
    GstElement* videoscale_;
    GstElement* capsfilter_;
    GstElement* appsink_;
    
    // Shared memory
    void* shm_data_;
    size_t shm_size_;
    int shm_fd_;
    std::string shm_name_;
    
    // Frame info (updated by GStreamer thread)
    struct FrameInfo {
        int width;
        int height;
        int format;
        size_t size;
        int stride[4];
        std::atomic<bool> ready;
    } frame_info_;
    
    // State
    bool is_playing_;
    std::atomic<bool> pipeline_running_;
    
    // Lock for shared memory access
    pthread_mutex_t shm_mutex_;
};

#endif // GST_PLAYER_H
