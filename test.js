var socket = require('./main');

socket.open( function (data, timestamp, srcName, pgn, priority, dstAddr) {
  console.log("t:" + timestamp + " s:" + srcName + " pgn:" + pgn + " d:" + dstAddr);
  console.warn(data);
}
);

