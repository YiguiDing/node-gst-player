#ifndef SHM_ALLOCATOR_H
#define SHM_ALLOCATOR_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <memory>
#include <string>

// Shared memory allocator for GStreamer
// This allows GStreamer to allocate buffers directly in shared memory
// enabling true zero-copy from GStreamer to Node.js/Electron

class ShmAllocator {
public:
    ShmAllocator(const std::string& name, size_t size);
    ~ShmAllocator();
    
    bool Initialize();
    GstAllocator* GetGstAllocator();
    void* GetData();
    size_t GetSize();
    
private:
    std::string shm_name_;
    size_t shm_size_;
    int shm_fd_;
    void* shm_data_;
    GstAllocator* gst_allocator_;
    
    bool CreateSharedMemory();
    bool CreateGstAllocator();
};

#endif // SHM_ALLOCATOR_H
