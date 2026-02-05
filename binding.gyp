{
  "targets": [
    {
      "target_name": "gst_player",
      "sources": [
        "src/gst_player.cpp",
        "src/shm_allocator.cpp",
        "src/gst_shm_sink.cpp"
      ],
      "include_dirs": [
        "<!(node -e \"require('nan')\")",
        "<!@(pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)"
      ],
      "libraries": [
        "<!@(pkg-config --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)"
      ],
      "defines": [
        "GST_USE_UNSTABLE_API"
      ],
      "conditions": [
        ["OS=='win'", {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": 1
            }
          }
        }]
      ]
    }
  ]
}
