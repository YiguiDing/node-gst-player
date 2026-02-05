#include "gst_shm_sink.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC(gst_shm_sink_debug);
#define GST_CAT_DEFAULT gst_shm_sink_debug

#define DEFAULT_SHM_NAME "/gst_shm_sink"
#define DEFAULT_SHM_SIZE (1920 * 1080 * 4) // RGBA 1080p

enum {
    PROP_0,
    PROP_SHM_NAME,
    PROP_SHM_SIZE
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "video/x-raw, "
        "format = (string) { RGBA, RGBx, BGRA, BGRx }, "
        "width = (int) [ 1, 2147483647 ], "
        "height = (int) [ 1, 2147483647 ], "
        "framerate = (fraction) [ 0/1, 2147483647/1 ]"
    )
);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT(gst_shm_sink_debug, "shmsink", 0, "Shared Memory Sink");

G_DEFINE_TYPE_WITH_CODE(GstShmSink, gst_shm_sink, GST_TYPE_BASE_SINK, _do_init);

static void gst_shm_sink_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec);
static void gst_shm_sink_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec);
static void gst_shm_sink_finalize(GObject* object);

static gboolean gst_shm_sink_start(GstBaseSink* basesink);
static gboolean gst_shm_sink_stop(GstBaseSink* basesink);
static gboolean gst_shm_sink_set_caps(GstBaseSink* basesink, GstCaps* caps);
static GstFlowReturn gst_shm_sink_render(GstBaseSink* basesink, GstBuffer* buffer);

static void gst_shm_sink_class_init(GstShmSinkClass* klass) {
    GObjectClass* gobject_class;
    GstElementClass* element_class;
    GstBaseSinkClass* basesink_class;
    
    gobject_class = G_OBJECT_CLASS(klass);
    element_class = GST_ELEMENT_CLASS(klass);
    basesink_class = GST_BASE_SINK_CLASS(klass);
    
    gobject_class->set_property = gst_shm_sink_set_property;
    gobject_class->get_property = gst_shm_sink_get_property;
    gobject_class->finalize = gst_shm_sink_finalize;
    
    basesink_class->start = GST_DEBUG_FUNCPTR(gst_shm_sink_start);
    basesink_class->stop = GST_DEBUG_FUNCPTR(gst_shm_sink_stop);
    basesink_class->set_caps = GST_DEBUG_FUNCPTR(gst_shm_sink_set_caps);
    basesink_class->render = GST_DEBUG_FUNCPTR(gst_shm_sink_render);
    
    g_object_class_install_property(
        gobject_class, PROP_SHM_NAME,
        g_param_spec_string("shm-name", "Shared Memory Name",
                            "Name of the shared memory segment",
                            DEFAULT_SHM_NAME,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS))
    );
    
    g_object_class_install_property(
        gobject_class, PROP_SHM_SIZE,
        g_param_spec_int("shm-size", "Shared Memory Size",
                         "Size of the shared memory segment in bytes",
                         0, G_MAXINT, DEFAULT_SHM_SIZE,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS))
    );
    
    gst_element_class_set_metadata(
        element_class,
        "Shared Memory Sink",
        "Sink/Video",
        "Writes video frames to shared memory",
        "Node-GST-Player"
    );
    
    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&sink_template)
    );
}

static void gst_shm_sink_init(GstShmSink* sink) {
    sink->shm_name = g_strdup(DEFAULT_SHM_NAME);
    sink->shm_size = DEFAULT_SHM_SIZE;
    sink->shm_fd = -1;
    sink->shm_data = nullptr;
    sink->allocator = nullptr;
    sink->new_frame = FALSE;
    
    g_mutex_init(&sink->mutex);
    g_cond_init(&sink->cond);
}

static void gst_shm_sink_finalize(GObject* object) {
    GstShmSink* sink = GST_SHM_SINK(object);
    
    g_free(sink->shm_name);
    
    if (sink->shm_data && sink->shm_data != MAP_FAILED) {
        munmap(sink->shm_data, sink->shm_size);
    }
    
    if (sink->shm_fd != -1) {
        close(sink->shm_fd);
        shm_unlink(sink->shm_name);
    }
    
    if (sink->allocator) {
        gst_object_unref(sink->allocator);
    }
    
    g_mutex_clear(&sink->mutex);
    g_cond_clear(&sink->cond);
    
    G_OBJECT_CLASS(gst_shm_sink_parent_class)->finalize(object);
}

static void gst_shm_sink_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec) {
    GstShmSink* sink = GST_SHM_SINK(object);
    
    switch (prop_id) {
        case PROP_SHM_NAME:
            g_free(sink->shm_name);
            sink->shm_name = g_value_dup_string(value);
            break;
        case PROP_SHM_SIZE:
            sink->shm_size = g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void gst_shm_sink_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec) {
    GstShmSink* sink = GST_SHM_SINK(object);
    
    switch (prop_id) {
        case PROP_SHM_NAME:
            g_value_set_string(value, sink->shm_name);
            break;
        case PROP_SHM_SIZE:
            g_value_set_int(value, sink->shm_size);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static gboolean gst_shm_sink_start(GstBaseSink* basesink) {
    GstShmSink* sink = GST_SHM_SINK(basesink);
    
    // Create shared memory
#ifdef __linux__
    sink->shm_fd = shm_open(sink->shm_name, O_CREAT | O_RDWR, 0666);
#elif defined(__APPLE__)
    sink->shm_fd = shm_open(sink->shm_name, O_CREAT | O_RDWR, 0666);
#elif defined(_WIN32)
    HANDLE hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        sink->shm_size,
        sink->shm_name
    );
    if (hMapFile == NULL) return FALSE;
    sink->shm_fd = _open_osfhandle((intptr_t)hMapFile, _O_RDWR);
#else
    return FALSE;
#endif
    
    if (sink->shm_fd == -1) {
        GST_ERROR_OBJECT(sink, "Failed to create shared memory: %s", strerror(errno));
        return FALSE;
    }
    
    // Set size
    if (ftruncate(sink->shm_fd, sink->shm_size) == -1) {
        GST_ERROR_OBJECT(sink, "Failed to set shared memory size: %s", strerror(errno));
        close(sink->shm_fd);
        sink->shm_fd = -1;
        return FALSE;
    }
    
    // Map shared memory
    sink->shm_data = mmap(nullptr, sink->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, sink->shm_fd, 0);
    if (sink->shm_data == MAP_FAILED) {
        GST_ERROR_OBJECT(sink, "Failed to map shared memory: %s", strerror(errno));
        close(sink->shm_fd);
        sink->shm_fd = -1;
        sink->shm_data = nullptr;
        return FALSE;
    }
    
    // Clear memory
    memset(sink->shm_data, 0, sink->shm_size);
    
    GST_INFO_OBJECT(sink, "Shared memory created: %s, size: %d", sink->shm_name, sink->shm_size);
    
    return TRUE;
}

static gboolean gst_shm_sink_stop(GstBaseSink* basesink) {
    GstShmSink* sink = GST_SHM_SINK(basesink);
    
    if (sink->shm_data && sink->shm_data != MAP_FAILED) {
        munmap(sink->shm_data, sink->shm_size);
        sink->shm_data = nullptr;
    }
    
    if (sink->shm_fd != -1) {
        close(sink->shm_fd);
        shm_unlink(sink->shm_name);
        sink->shm_fd = -1;
    }
    
    return TRUE;
}

static gboolean gst_shm_sink_set_caps(GstBaseSink* basesink, GstCaps* caps) {
    GstShmSink* sink = GST_SHM_SINK(basesink);
    
    if (!gst_video_info_from_caps(&sink->video_info, caps)) {
        GST_ERROR_OBJECT(sink, "Failed to parse video caps");
        return FALSE;
    }
    
    GST_INFO_OBJECT(sink, "Video caps set: %dx%d",
                   sink->video_info.width, sink->video_info.height);
    
    return TRUE;
}

static GstFlowReturn gst_shm_sink_render(GstBaseSink* basesink, GstBuffer* buffer) {
    GstShmSink* sink = GST_SHM_SINK(basesink);
    GstMapInfo map;
    
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT(sink, "Failed to map buffer");
        return GST_FLOW_ERROR;
    }
    
    // Lock mutex and copy data to shared memory
    g_mutex_lock(&sink->mutex);
    
    // Copy frame data to shared memory
    size_t copy_size = GST_MIN(map.size, sink->shm_size);
    memcpy(sink->shm_data, map.data, copy_size);
    
    // Signal new frame
    sink->new_frame = TRUE;
    g_cond_broadcast(&sink->cond);
    
    g_mutex_unlock(&sink->mutex);
    
    gst_buffer_unmap(buffer, &map);
    
    return GST_FLOW_OK;
}
