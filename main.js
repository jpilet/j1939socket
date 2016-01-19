var j1939Socket = require('bindings')('main.node');

var socket = new j1939Socket.J1939Socket("can0");

function handlePacket(data, timestamp, srcAddr, priority, dstAddr) {
  console.log("t:" + timestamp + " s:" + srcAddr + " d:" + dstAddr);
  console.warn(data);
}

socket.open(handlePacket);
setInterval(function() { socket.fetch(); }, 50);
