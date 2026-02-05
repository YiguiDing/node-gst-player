#ifndef GST_SHM_SINK_H
#define GST_SHM_SINK_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#define GST_TYPE_SHM_SINK (gst_shm_sink_get_type())
#define GST_SHM_SINK(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SHM_SINK, GstShmSink))
#define GST_SHM_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SHM_SINK, GstShmSinkClass))
#define GST_IS_SHM_SINK(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SHM_SINK))
#define GST_IS_SHM_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SHM_SINK))

typedef struct _GstShmSink GstShmSink;
typedef struct _GstShmSinkClass GstShmSinkClass;

struct _GstShmSink {
    GstBaseSink parent;
    
    gchar* shm_name;
    gint shm_size;
    gint shm_fd;
    gpointer shm_data;
    
    GstVideoInfo video_info;
    GstAllocator* allocator;
    
    GMutex mutex;
    GCond cond;
    gboolean new_frame;
};

struct _GstShmSinkClass {
    GstBaseSinkClass parent_class;
};

GType gst_shm_sink_get_type(void);

G_END_DECLS

#endif // GST_SHM_SINK_H
