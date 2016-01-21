var j1939Socket = require('bindings')('main.node');

var socket = new j1939Socket.J1939Socket("can0");


exports.open = function(handlePacket) {
  socket.open(handlePacket);
  setInterval(function() { socket.fetch(); }, 50);
};

