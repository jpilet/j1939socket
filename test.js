var socket = require('./main');

socket.open(function (data, timestamp, srcName,
                      pgn, priority, dstAddr, srcAddr) {
  console.log("t:" + timestamp + " s:" + srcName + " pgn:" + pgn
              + "src: " + srcAddr + " d:" + dstAddr);
  console.warn(data);
}
);

