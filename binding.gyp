{
  "targets": [
    {
      "target_name": "main",
      "sources": [
        "src/libj1939.c",
        "src/j1939socket.h",
        "src/j1939socket.cc",
      ],
      "include_dirs": [
        "src/include",
        "<!(node -e \"require('nan')\")"
      ]
    }
  ]
}
