// node application that will connect to serial port and send a set of strings to the serial port in rapid succession
// to test the serial port communication with the arduino

const { SerialPort } = require("serialport");

// port = new SerialPort talking to arduino on COM port 7
const port = new SerialPort({
  path: "COM7",
  baudRate: 9600,
  autoOpen: false,
});

port.open(function (err) {
  if (err) {
    return console.log("Error opening port: ", err.message);
  }
  setTimeout(() => {
    // for loop with each loop delayed by a variable to send another variable amoutn of strings to the serial port
    for (let i = 1; i <= 20; i++) {
      port.write(`#Duress #Cancelled #Fob ${i} #Main Hub\r`, function (err) {
        if (err) {
          return console.log("Error on write: ", err.message);
        }
        console.log(`#Duress #Cancelled #Fob ${i} #Main Hub\r`);
      });

      // ensure this write is done
      port.drain();
    }
  }, 2000);
});

// Adjust the delay as needed
port.on("error", function (err) {
  console.log("Error: ", err.message);
});
