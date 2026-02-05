#include "shm_allocator.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

ShmAllocator::ShmAllocator(const std::string& name, size_t size)
    : shm_name_(name),
      shm_size_(size),
      shm_fd_(-1),
      shm_data_(nullptr),
      gst_allocator_(nullptr) {
}

ShmAllocator::~ShmAllocator() {
    if (gst_allocator_) {
        gst_object_unref(gst_allocator_);
    }
    
    if (shm_data_ && shm_data_ != MAP_FAILED) {
        munmap(shm_data_, shm_size_);
    }
    
    if (shm_fd_ != -1) {
        close(shm_fd_);
        shm_unlink(shm_name_.c_str());
    }
}

bool ShmAllocator::Initialize() {
    if (!CreateSharedMemory()) {
        return false;
    }
    
    if (!CreateGstAllocator()) {
        return false;
    }
    
    return true;
}

bool ShmAllocator::CreateSharedMemory() {
#ifdef __linux__
    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
#elif defined(__APPLE__)
    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
#elif defined(_WIN32)
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
        shm_fd_ = -1;
        return false;
    }
    
    // Map shared memory
    shm_data_ = mmap(nullptr, shm_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (shm_data_ == MAP_FAILED) {
        close(shm_fd_);
        shm_fd_ = -1;
        shm_data_ = nullptr;
        return false;
    }
    
    // Clear memory
    memset(shm_data_, 0, shm_size_);
    
    return true;
}

// GStreamer memory allocation wrapper
typedef struct {
    GstMemory parent;
    void* shm_data;
    size_t size;
    ShmAllocator* allocator;
} ShmMemory;

typedef struct {
    GstAllocatorClass parent_class;
} ShmAllocatorClass;

G_DEFINE_TYPE(ShmAllocator, shm_allocator, GST_TYPE_ALLOCATOR)

static GstMemory* shm_allocator_alloc(GstAllocator* allocator, gsize size, GstAllocationParams* params) {
    ShmAllocator* shm_alloc = static_cast<ShmAllocator*>(
        g_object_get_data(G_OBJECT(allocator), "shm-allocator")
    );
    
    if (!shm_alloc || size > shm_alloc->GetSize()) {
        return nullptr;
    }
    
    ShmMemory* memory = g_new(ShmMemory, 1);
    gst_memory_init(GST_MEMORY_CAST(memory),
                    (GstMemoryFlags)0,
                    allocator,
                    nullptr,
                    shm_alloc->GetSize(),
                    0,
                    size);
    
    memory->shm_data = shm_alloc->GetData();
    memory->size = size;
    memory->allocator = shm_alloc;
    
    return GST_MEMORY_CAST(memory);
}

static gpointer shm_allocator_map(GstMemory* mem, GstMapInfo* info, GstMapFlags flags) {
    ShmMemory* shm_mem = (ShmMemory*)mem;
    info->data = shm_mem->shm_data;
    info->size = shm_mem->size;
    return info->data;
}

static gboolean shm_allocator_unmap(GstMapInfo* info) {
    return TRUE;
}

static void shm_allocator_free(GstAllocator* allocator, GstMemory* mem) {
    g_free(mem);
}

static void shm_allocator_class_init(ShmAllocatorClass* klass) {
    GstAllocatorClass* allocator_class = GST_ALLOCATOR_CLASS(klass);
    
    allocator_class->alloc = shm_allocator_alloc;
    allocator_class->free = shm_allocator_free;
}

static void shm_allocator_init(ShmAllocator* allocator) {
    GstAllocator* alloc = GST_ALLOCATOR_CAST(allocator);
    
    alloc->map = shm_allocator_map;
    alloc->unmap = shm_allocator_unmap;
    alloc->mem_type = g_strdup("ShmMemory");
    alloc->mem_map_full = FALSE;
}

bool ShmAllocator::CreateGstAllocator() {
    gst_allocator_register("shm_allocator", g_object_new(shm_allocator_get_type(), NULL));
    
    gst_allocator_ = gst_allocator_find("shm_allocator");
    if (!gst_allocator_) {
        return false;
    }
    
    // Store reference to this allocator in the GstAllocator object
    g_object_set_data(G_OBJECT(gst_allocator_), "shm-allocator", this);
    
    return true;
}

void* ShmAllocator::GetData() {
    return shm_data_;
}

size_t ShmAllocator::GetSize() {
    return shm_size_;
}

GstAllocator* ShmAllocator::GetGstAllocator() {
    return gst_allocator_;
}
