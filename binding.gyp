{
  "targets": [
    {
        "target_name": "node-gst-player",
        "sources": ["src/module.cpp", "src/GstPlayer.cpp"],
        "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
        "dependencies": [
            "<!(node -p \"require('node-addon-api').targets\"):node_addon_api"
        ],
        "conditions": [
            ["OS=='win'", {
                "msvs_settings": {
                    "VCCLCompilerTool": {
                        "AdditionalOptions": ["/std:c++20"]
                    }
                },
                "include_dirs": [
                        "<!(node -p \"require('node-addon-api').include\")",
                        "<!(echo %GSTREAMER_1_0_ROOT_X86_64%)\\lib\\glib-2.0\\include",
                        "<!(echo %GSTREAMER_1_0_ROOT_X86_64%)\\include\\glib-2.0",
                        "<!(echo %GSTREAMER_1_0_ROOT_X86_64%)\\include\\gstreamer-1.0",
                        "<!(echo %GSTREAMER_1_0_ROOT_X86_64%)\\include\\gstapp-1.0",
                ],
                "libraries": [
                        "<!(echo %GSTREAMER_1_0_ROOT_X86_64%)\\lib\\glib-2.0.lib",
                        "<!(echo %GSTREAMER_1_0_ROOT_X86_64%)\\lib\\gstreamer-1.0.lib",
                        "<!(echo %GSTREAMER_1_0_ROOT_X86_64%)\\lib\\gstapp-1.0.lib",
                        "<!(echo %GSTREAMER_1_0_ROOT_X86_64%)\\lib\\gstvideo-1.0.lib",
                        "<!(echo %GSTREAMER_1_0_ROOT_X86_64%)\\lib\\gstaudio-1.0.lib",
                        "<!(echo %GSTREAMER_1_0_ROOT_X86_64%)\\lib\\gobject-2.0.lib",
                ]
            }]
        ],

    }
  ]
}