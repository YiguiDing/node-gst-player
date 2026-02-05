#include "gst_player.h"
#include <nan.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <gst/gstallocator.h>

using namespace v8;
using namespace node;

Persistent<Function> GstPlayer::constructor;

// GStreamer 共享内存分配器回调
static gpointer shm_alloc(GstAllocator* allocator, gsize size, GstAllocationParams* params) {
    GstPlayer* player = static_cast<GstPlayer*>(allocator->user_data);
    if (player && player->shm_data_) {
        // 直接返回共享内存指针，无需分配新内存
        return player->shm_data_;
    }
    return nullptr;
}

static void shm_free(GstAllocator* allocator, gpointer memory) {
    // 共享内存由系统管理，无需释放
}

static void* shm_mem_map(GstMemory* mem, gsize maxsize, GstMapFlags flags) {
    GstPlayer* player = static_cast<GstPlayer*>(mem->allocator->user_data);
    return player ? player->shm_data_ : nullptr;
}

static gboolean shm_mem_unmap(GstMemory* mem) {
    return TRUE;
}

static GstMemory* shm_mem_copy(GstMemory* mem, gssize offset, gssize size) {
    // 不支持拷贝，返回空引用
    return gst_memory_ref(mem);
}

static GstMemory* shm_mem_share(GstMemory* mem, gssize offset, gssize size) {
    return gst_memory_ref(mem);
}

static gboolean shm_mem_is_span(GstMemory* mem1, GstMemory* mem2, gsize* offset) {
    return FALSE;
}

// Initialize the addon
void GstPlayer::Init(Local<Object> exports) {
    Isolate* isolate = exports->GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    
    // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
    tpl->SetClassName(String::NewFromUtf8(isolate, "GstPlayer").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    
    // Prototype methods
    NODE_SET_PROTOTYPE_METHOD(tpl, "play", Play);
    NODE_SET_PROTOTYPE_METHOD(tpl, "pause", Pause);
    NODE_SET_PROTOTYPE_METHOD(tpl, "stop", Stop);
    NODE_SET_PROTOTYPE_METHOD(tpl, "setUri", SetUri);
    NODE_SET_PROTOTYPE_METHOD(tpl, "getFrame", GetFrame);
    NODE_SET_PROTOTYPE_METHOD(tpl, "getSharedMemoryInfo", GetSharedMemoryInfo);
    
    Local<Function> constructor_func = tpl->GetFunction(context).ToLocalChecked();
    constructor.Reset(isolate, constructor_func);
    exports->Set(context,
                 String::NewFromUtf8(isolate, "GstPlayer").ToLocalChecked(),
                 constructor_func).FromJust();
}

// Constructor called from JavaScript
void GstPlayer::New(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    
    if (args.IsConstructCall()) {
        GstPlayer* obj = new GstPlayer();
        obj->Wrap(args.This());
        args.GetReturnValue().Set(args.This());
    } else {
        Local<Context> context = isolate->GetCurrentContext();
        Local<Function> cons = Local<Function>::New(isolate, constructor);
        Local<Object> result = cons->NewInstance(context).ToLocalChecked();
        args.GetReturnValue().Set(result);
    }
}

// GstPlayer constructor implementation
GstPlayer::GstPlayer()
    : pipeline_(nullptr),
      uridecodebin_(nullptr),
      videoconvert_(nullptr),
      videoscale_(nullptr),
      capsfilter_(nullptr),
      appsink_(nullptr),
      shm_data_(nullptr),
      shm_size_(0),
      shm_fd_(-1),
      shm_allocator_(nullptr),
      is_playing_(false),
      pipeline_running_(false) {
    
    // Initialize mutex
    pthread_mutex_init(&shm_mutex_, nullptr);
    
    // Initialize GStreamer
    gst_init(nullptr, nullptr);
    
    // Initialize frame info
    frame_info_.width = 0;
    frame_info_.height = 0;
    frame_info_.format = 0;
    frame_info_.size = 0;
    frame_info_.stride[0] = 0;
    frame_info_.stride[1] = 0;
    frame_info_.stride[2] = 0;
    frame_info_.stride[3] = 0;
    frame_info_.ready = false;
}

GstPlayer::~GstPlayer() {
    CleanupPipeline();
    
    if (shm_allocator_) {
        gst_object_unref(shm_allocator_);
        shm_allocator_ = nullptr;
    }
    
    if (shm_data_ && shm_data_ != MAP_FAILED) {
        munmap(shm_data_, shm_size_);
    }
    
    if (shm_fd_ != -1) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
    }
    
    pthread_mutex_destroy(&shm_mutex_);
}

void GstPlayer::Play(const FunctionCallbackInfo<Value>& args) {
    GstPlayer* obj = ObjectWrap::Unwrap<GstPlayer>(args.Holder());
    
    if (obj->pipeline_ && !obj->is_playing_) {
        gst_element_set_state(obj->pipeline_, GST_STATE_PLAYING);
        obj->is_playing_ = true;
    }
}

void GstPlayer::Pause(const FunctionCallbackInfo<Value>& args) {
    GstPlayer* obj = ObjectWrap::Unwrap<GstPlayer>(args.Holder());
    
    if (obj->pipeline_ && obj->is_playing_) {
        gst_element_set_state(obj->pipeline_, GST_STATE_PAUSED);
        obj->is_playing_ = false;
    }
}

void GstPlayer::Stop(const FunctionCallbackInfo<Value>& args) {
    GstPlayer* obj = ObjectWrap::Unwrap<GstPlayer>(args.Holder());
    
    if (obj->pipeline_) {
        gst_element_set_state(obj->pipeline_, GST_STATE_NULL);
        obj->is_playing_ = false;
    }
}

void GstPlayer::SetUri(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(Exception::TypeError(
            String::NewFromUtf8(isolate, "URI required").ToLocalChecked()));
        return;
    }
    
    GstPlayer* obj = ObjectWrap::Unwrap<GstPlayer>(args.Holder());
    String::Utf8Value uri(isolate, args[0]);
    
    // Cleanup existing pipeline
    obj->CleanupPipeline();
    
    // Set up new pipeline with the URI
    obj->uridecodebin_ = gst_element_factory_make("uridecodebin", "source");
    if (!obj->uridecodebin_) {
        isolate->ThrowException(Exception::Error(
            String::NewFromUtf8(isolate, "Failed to create uridecodebin").ToLocalChecked()));
        return;
    }
    
    g_object_set(obj->uridecodebin_, "uri", *uri, nullptr);
    
    // Connect pad-added signal
    g_signal_connect(obj->uridecodebin_, "pad-added",
                     G_CALLBACK([](GstElement* element, GstPad* pad, gpointer user_data) {
        GstPlayer* player = static_cast<GstPlayer*>(user_data);
        
        // Try to link video pad
        if (gst_pad_has_current_caps(pad)) {
            GstCaps* caps = gst_pad_get_current_caps(pad);
            const GstStructure* structure = gst_caps_get_structure(caps, 0);
            const gchar* name = gst_structure_get_name(structure);
            
            if (g_str_has_prefix(name, "video/")) {
                GstPad* sinkpad = gst_element_get_static_pad(player->videoconvert_, "sink");
                if (gst_pad_link(pad, sinkpad) == GST_PAD_LINK_OK) {
                    // Linked successfully
                }
                gst_object_unref(sinkpad);
            }
            gst_caps_unref(caps);
        }
    }), obj);
    
    // Setup rest of pipeline
    if (!obj->SetupPipeline()) {
        isolate->ThrowException(Exception::Error(
            String::NewFromUtf8(isolate, "Failed to setup pipeline").ToLocalChecked()));
        return;
    }
    
    // Link elements
    gst_bin_add_many(GST_BIN(obj->pipeline_), obj->uridecodebin_,
                     obj->videoconvert_, obj->videoscale_,
                     obj->capsfilter_, obj->appsink_, nullptr);
    
    if (!gst_element_link_many(obj->videoconvert_, obj->videoscale_,
                               obj->capsfilter_, obj->appsink_, nullptr)) {
        isolate->ThrowException(Exception::Error(
            String::NewFromUtf8(isolate, "Failed to link elements").ToLocalChecked()));
        return;
    }
    
    // Set to READY state
    gst_element_set_state(obj->pipeline_, GST_STATE_READY);
}

void GstPlayer::GetFrame(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    GstPlayer* obj = ObjectWrap::Unwrap<GstPlayer>(args.Holder());
    
    pthread_mutex_lock(&obj->shm_mutex_);
    
    Local<Object> result = Object::New(isolate);
    
    if (obj->frame_info_.ready) {
        result->Set(isolate->GetCurrentContext(),
                    String::NewFromUtf8(isolate, "width").ToLocalChecked(),
                    Number::New(isolate, obj->frame_info_.width));
        result->Set(isolate->GetCurrentContext(),
                    String::NewFromUtf8(isolate, "height").ToLocalChecked(),
                    Number::New(isolate, obj->frame_info_.height));
        result->Set(isolate->GetCurrentContext(),
                    String::NewFromUtf8(isolate, "size").ToLocalChecked(),
                    Number::New(isolate, obj->frame_info_.size));
        result->Set(isolate->GetCurrentContext(),
                    String::NewFromUtf8(isolate, "format").ToLocalChecked(),
                    Number::New(isolate, obj->frame_info_.format));
        
        // Create ArrayBuffer from shared memory pointer
        // Note: This doesn't copy the data, just creates a view
        if (obj->shm_data_ && obj->frame_info_.size > 0) {
            Local<ArrayBuffer> buffer = ArrayBuffer::New(isolate, obj->shm_data_, obj->frame_info_.size);
            result->Set(isolate->GetCurrentContext(),
                        String::NewFromUtf8(isolate, "data").ToLocalChecked(),
                        buffer);
        }
        
        // Mark frame as consumed
        obj->frame_info_.ready = false;
    }
    
    pthread_mutex_unlock(&obj->shm_mutex_);
    
    args.GetReturnValue().Set(result);
}

void GstPlayer::GetSharedMemoryInfo(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    GstPlayer* obj = ObjectWrap::Unwrap<GstPlayer>(args.Holder());
    
    Local<Object> result = Object::New(isolate);
    result->Set(isolate->GetCurrentContext(),
                String::NewFromUtf8(isolate, "name").ToLocalChecked(),
                String::NewFromUtf8(isolate, obj->shm_name_.c_str()).ToLocalChecked());
    result->Set(isolate->GetCurrentContext(),
                String::NewFromUtf8(isolate, "size").ToLocalChecked(),
                Number::New(isolate, obj->shm_size_));
    
    args.GetReturnValue().Set(result);
}

bool GstPlayer::SetupPipeline() {
    // Create elements
    pipeline_ = gst_pipeline_new("player-pipeline");
    videoconvert_ = gst_element_factory_make("videoconvert", "convert");
    videoscale_ = gst_element_factory_make("videoscale", "scale");
    capsfilter_ = gst_element_factory_make("capsfilter", "filter");
    appsink_ = gst_element_factory_make("appsink", "sink");
    
    if (!pipeline_ || !videoconvert_ || !videoscale_ || !capsfilter_ || !appsink_) {
        return false;
    }
    
    // Set caps (RGBA format for WebGL compatibility)
    GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "RGBA",
                                        "width", G_TYPE_INT, 1920,
                                        "height", G_TYPE_INT, 1080,
                                        NULL);
    g_object_set(capsfilter_, "caps", caps, nullptr);
    gst_caps_unref(caps);
    
    // Setup shared memory first
    if (!SetupSharedMemory()) {
        return false;
    }
    
    // Create custom allocator for zero-copy
    SetupShmAllocator();
    
    // Configure appsink to use our allocator
    g_object_set(appsink_,
                 "emit-signals", FALSE,
                 "enable-last-sample", FALSE,
                 "max-buffers", 1,
                 "drop", TRUE,
                 "sync", FALSE,
                 nullptr);
    
    // Set callbacks
    GstAppSinkCallbacks callbacks = {nullptr, nullptr, OnNewSample, nullptr};
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink_), &callbacks, this, nullptr);
    
    return true;
}

void GstPlayer::SetupShmAllocator() {
    // 创建自定义共享内存分配器
    shm_allocator_ = static_cast<GstAllocator*>(g_object_new(GST_TYPE_ALLOCATOR, NULL));
    g_strlcpy(shm_allocator_->mem_type, "GstShmMemory", GST_MEM_TYPE_NAME_LENGTH);
    
    // 设置分配器回调
    shm_allocator_->alloc = shm_alloc;
    shm_allocator_->free = shm_free;
    shm_allocator_->mem_map = shm_mem_map;
    shm_allocator_->mem_unmap = shm_mem_unmap;
    shm_allocator_->mem_copy = shm_mem_copy;
    shm_allocator_->mem_share = shm_mem_share;
    shm_allocator_->mem_is_span = shm_mem_is_span;
    
    // 保存用户数据
    shm_allocator_->user_data = this;
}

bool GstPlayer::SetupSharedMemory() {
    // Allocate 1920x1080x4 bytes (RGBA)
    shm_size_ = 1920 * 1080 * 4;
    
    // Generate unique shared memory name
    shm_name_ = "/gst_player_" + std::to_string(getpid());
    
#ifdef __linux__
    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
#elif defined(__APPLE__)
    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
#elif defined(_WIN32)
    // Windows shared memory
    HANDLE hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        shm_size_,
        shm_name_.c_str()
    );
    if (hMapFile == NULL) return false;
    shm_fd_ = _open_osfhandle((intptr_t)hMapFile, _O_RDWR);
#else
    return false;
#endif
    
    if (shm_fd_ == -1) {
        return false;
    }
    
    // Set size
    if (ftruncate(shm_fd_, shm_size_) == -1) {
        close(shm_fd_);
        return false;
    }
    
    // Map shared memory
    shm_data_ = mmap(nullptr, shm_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (shm_data_ == MAP_FAILED) {
        close(shm_fd_);
        shm_data_ = nullptr;
        return false;
    }
    
    // Clear shared memory
    memset(shm_data_, 0, shm_size_);
    
    return true;
}

void GstPlayer::CleanupPipeline() {
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    
    uridecodebin_ = nullptr;
    videoconvert_ = nullptr;
    videoscale_ = nullptr;
    capsfilter_ = nullptr;
    appsink_ = nullptr;
    
    is_playing_ = false;
    pipeline_running_ = false;
}

// Static callback for new samples from appsink
GstFlowReturn GstPlayer::OnNewSample(GstAppSink* appsink, gpointer user_data) {
    GstPlayer* player = static_cast<GstPlayer*>(user_data);
    
    GstSample* sample = gst_app_sink_pull_sample(appsink);
    if (sample) {
        player->ProcessFrame(sample);
        gst_sample_unref(sample);
    }
    
    return GST_FLOW_OK;
}

void GstPlayer::ProcessFrame(GstSample* sample) {
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (!buffer) return;
    
    // 检查 buffer 是否已经在共享内存中
    GstMemory* mem = gst_buffer_peek_memory(buffer, 0);
    bool is_zero_copy = (mem && mem->allocator == shm_allocator_);
    
    if (is_zero_copy) {
        // 零拷贝路径：数据已经在共享内存中
        pthread_mutex_lock(&shm_mutex_);
        
        GstCaps* caps = gst_sample_get_caps(sample);
        if (caps) {
            GstVideoInfo video_info;
            if (gst_video_info_from_caps(&video_info, caps)) {
                frame_info_.width = GST_VIDEO_INFO_WIDTH(&video_info);
                frame_info_.height = GST_VIDEO_INFO_HEIGHT(&video_info);
                frame_info_.format = GST_VIDEO_INFO_FORMAT(&video_info);
                frame_info_.size = gst_buffer_get_size(buffer);
                
                for (int i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
                    frame_info_.stride[i] = GST_VIDEO_INFO_PLANE_STRIDE(&video_info, i);
                }
            }
        }
        
        frame_info_.ready = true;
        pthread_mutex_unlock(&shm_mutex_);
    } else {
        // 回退路径：需要拷贝数据（兼容性处理）
        GstMapInfo map;
        if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            return;
        }
        
        pthread_mutex_lock(&shm_mutex_);
        
        GstCaps* caps = gst_sample_get_caps(sample);
        if (caps) {
            GstVideoInfo video_info;
            if (gst_video_info_from_caps(&video_info, caps)) {
                frame_info_.width = GST_VIDEO_INFO_WIDTH(&video_info);
                frame_info_.height = GST_VIDEO_INFO_HEIGHT(&video_info);
                frame_info_.format = GST_VIDEO_INFO_FORMAT(&video_info);
                frame_info_.size = map.size;
                
                for (int i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
                    frame_info_.stride[i] = GST_VIDEO_INFO_PLANE_STRIDE(&video_info, i);
                }
            }
        }
        
        // 拷贝数据到共享内存
        size_t copy_size = std::min(map.size, shm_size_);
        memcpy(shm_data_, map.data, copy_size);
        
        frame_info_.ready = true;
        pthread_mutex_unlock(&shm_mutex_);
        
        gst_buffer_unmap(buffer, &map);
    }
}

// Module registration
void InitAll(Local<Object> exports) {
    GstPlayer::Init(exports);
}

NODE_MODULE(NODE_GYP_MODULE_NAME, InitAll)
