var j1939Socket = require('bindings')('main.node');

var socket = new j1939Socket.J1939Socket("can0");

socket.open(
    function(data, timestamp, srcAddr, priority, dstAddr) {
      console.log(timestamp + ":" + srcAddr +":" + dstAddr);
      console.warn(data);
    }
);
