SAE J1939 CAN socket node interface
-----------------------------------

Nodejs wrapper around kurt-vd's CAN-J1939 socket code.
See https://github.com/kurt-vd/test-can-j1939

This module requires a patched linux kernel, for example https://github.com/kurt-vd/linux/tree/j1939-v3.10

#Run
node-gyp configure
node-gyp build
node main.js
