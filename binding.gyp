{
  "targets": [
    {
      "target_name": "PuglifyWorker",
      "sources": ["src/puglify-worker.cpp", "src/generic-streaming-worker.h"],
      "cflags": ["-Wall", "-std=c++14", "-fexceptions"],
      "cflags_cc": ["-fexceptions"],
      "cflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "include_dirs": [
        "<!(node -e \"require('nan')\")",
        "vendor"
      ],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-std=c++14"]
      },
      "conditions": [
        [
          "OS==\"mac\"",
          {
            "xcode_settings": {
              "OTHER_CPLUSPLUSFLAGS": [
                "-std=c++14",
                "-stdlib=libc++",
                "-fexceptions"
              ],
              "OTHER_LDFLAGS": ["-stdlib=libc++"],
              "MACOSX_DEPLOYMENT_TARGET": "10.7",
              "GCC_ENABLE_CPP_EXCEPTIONS": "YES"
            }
          }
        ]
      ]
    }
  ]
}
