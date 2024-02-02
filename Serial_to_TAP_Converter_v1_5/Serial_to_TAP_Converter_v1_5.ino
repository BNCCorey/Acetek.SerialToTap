/* Serial to TAP converter Universal Base
   --------------------------------------
   Last Modified: 31-01-2024
   Modified by: Michael Wilmshurst

   Build Version: 1.4

   Change log
   ---------------------------------------------
   v1.0
   - Built on Questek to Austco via TAP Converter 1.8
   - Set Client and Server IP
   - Removed Gateway and DNS
   - Removed Debugging for production purposes
   - removed DHCP ability (for space)
   - line(s) 224, 258 replaced break; with return;

   v1.1
   - Adding "Normal" and "Alarm" words to the arduino output
   - this is to overcome issue with ACS interpretation.

   v1.2
   - trying to remove Zone Messages

   v1.3
   - remove all filtering of incoming strings
   from version 1.1 and 1.2, and leave this responsibility to the acs script
   - minor code commenting and tidy.
   - Project renamed from THC Dural, to be generic program

   v1.4
   - Stop Tx of <CR> only incoming serial lines of text as it locks up acs port
   v1.5 
   - pulled wakeup <CR> out of loop it should NOT have been in

*/

#include <SPI.h>
#include <Ethernet.h>


// CONTROL CHARACTERS
int CR = 0xD;
int STX = 0x02;
int ETX = 0x03;
int EOT = 0x04;
int ENQ = 0x05;
int ACK = 0x06;
int ESC = 0x1b;
int newLineN = 0x4e;  //N
int newLineA = 0x41;  //A

// VARIABLES
char serialIn[50];
char filteredData[44];
String State;
int attemptCount;
int sum;
String CheckSum;
int failureCount = 0;
int MAX_failureCount = 1;
bool response;
bool proceed;
bool Failure = false;
bool Allow_Retransmit = false;
bool DEBUG = false;
bool DEBUGSUM = false;


////////////////////////////////////////////////////
// NETWORK INFORMATION
////////////////////////////////////////////////////
// Modify as required

// Arduino Settings
EthernetClient client;
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };  // MAC Address used by this Arduino
IPAddress ip(10, 1, 1, 222);                          // IP of this Arduino
IPAddress gateway(10, 1, 1, 1);                       // Gateway for this Arduino (if needed)
IPAddress subnet(255, 255, 255, 0);                   // Subnet of this Arduino



// Server Settings
IPAddress server(10, 1, 1, 250);  // TAP Server IP (TAP PARSER ACS110)
int port = 7000;                  // TAP Server Port
String CapCode = "919";           // Capcode appended to incoming strings.

///////////////////////////////////////////////////
// END NETWORK CONFIG
// No end user config should be performed below
///////////////////////////////////////////////////

void setup() 
{
  Serial.begin(9600);
  //Ethernet.begin(mac, ip, gateway, DNSserver, subnet);
  Ethernet.begin(mac, ip, gateway, subnet);
}

void receiveSerial()
 {
  for (int j = 0; j < 50; j++) 
  {
    serialIn[j] = '\0';
  }
  for (int p = 0; p < 3; p++) 
  {
    CheckSum[p] = '\0';
  }
  if (Serial.available() > 0) 
  {
    //Read until <CR>
    Serial.readBytesUntil(CR, serialIn, 50);

  if (serialIn[0]==13)
  {
    String(serialIn) ="Blank Line do Nothing";
  }
    ////////////////////////////
    // Message/String Filtering has been removed
    ////////////////////////////

    String Output = String(serialIn);
    strcpy(filteredData, Output.c_str());

    //////////////////
    // CHECKSUM
    //////////////////

    // Part 1:-
    // Convert to Decimal, and Sum them all together
    // Create a 12 digit long binary string

    sum = 0;

    // Data Segment
    for (int b = 0; b < 50; b++) 
    {
      if (filteredData[b] == '\0') 
      {
        break;
      }
      sum += filteredData[b];
    }

    // Capcode Segment
    for (int b = 0; b <= (sizeof(CapCode) / 2); b++) 
    {
      sum += CapCode[b];
    }

    // Control Characters Segment
    // Add Constant Value of 31;
    // <STX> = 2
    // <CR>  = 13
    // <CR>  = 13
    // <ETX> = 3
    //       +
    // ============
    // Total = 31

    sum += 31;


    // Part 2:-
    // Split 12 bit string into 3 sections,
    // string where; AAAA-BBBB-CCCC
    // use bitmasks and shifting to split to values

    int MaskA = 3840;  // B111100000000
    int A = (sum & MaskA) >> 8;

    int MaskB = 240;  // B000011110000
    int B = (sum & MaskB) >> 4;

    int MaskC = 15;  // B000000001111
    int C = (sum & MaskC);

    // Add Decimal 48 as per checksum specification
    A += 48;
    B += 48;
    C += 48;

    char sumA = A;
    char sumB = B;
    char sumC = C;

    // make a string with these chars
    CheckSum = String(sumA) + String(sumB) + String(sumC);


    proceed = true;
  }

}

void transmitTCP() 
{
  //IF connection avaliable, send
  if (client.connect(server, port)) 
  {

    // reset Failure flag
    Failure = false;

    //////////////////////////////////
    // Wakeup the Paging Terminal (SERVER)
    //////////////////////////////////
    response = false;
    attemptCount = 0;
    
    //1.5 fix-> move this our of the loop below
    // Send Wakeup
    client.write(CR);
    

  // Basically give it 3times the default timeout for correct response
    while (!response) 
    {
    // if i get correct response proceed
      if (client.findUntil("ID", "=")) 
      {
        response = true;
      }

      // otherwise count and retry
      attemptCount++;
      if (attemptCount == 3) 
      {
        // set flag for retransmit
        Failure = true;
        failureCount++;
        client.stop();
        return;
      }
    }

    ////////////////////
    // Login Response
    ////////////////////
    client.write(ESC);
    client.write("PG1");
    client.write(CR);

    //////////////////////
    // Server Validation
    //////////////////////
    //String Line= String(CR);
    if (!client.find("110 1.8"))
    {
      // set flag for retransmit
      Failure = true;
      failureCount++;
      client.stop();

      return;
    }

    ////////////////////
    //Server OK to Send
    ////////////////////

    // If we dont get ACK
    if (!client.find(ACK)) 
    {
      // set flag for retransmit
      Failure = true;
      failureCount++;
      client.stop();
      return;
    }

    ////////////////////
    //Send Message
    ////////////////////

    // v1.5 Move this OUT of Loop!

    // Datagram
    client.write(STX);
    client.print(CapCode);  // Destination Capcode
    client.write(CR);
    client.write(filteredData);  // Processed Message from Serial
    client.write(CR);

    // Checksum
    client.write(ETX);
    client.print(CheckSum);
    client.write(CR);


    // Confirm MSG Receipt give 3x's std length to confirm
    response = false;
    attemptCount = 0;
    while (!response) 
    {
      // v1.5 - Change test condition to <CR> rather than 211
      // 211 Message received correctly<CR>
      if (client.find(CR)) 
      {
        // Assuming that the full thing is there
        // touch lazy here
        response = true;
        // Clear all failure Flags
        Failure = false;
        failureCount = 0;
      }
      else
      {
        attemptCount++;
      }
      if (attemptCount >= 3) 
      {
        client.stop();

        // set flag for retransmit
        Failure = true;
        failureCount++;

        delay(4000);
        return;
      }
    }

    ////////////////////
    // Client Disconnect
    ////////////////////
    // v1.5 pull this out of the loop
    client.write(EOT);
    client.write(CR);

    // reset failure flags
    response = false;
    attemptCount = 0;
    while (!response) 
    {

      // v1.5 Change the char im testing for
      // 115 Exited normally<CR>
      if (client.find(CR)) 
      {
        // touch lazy here, assuming the disconnect is fine
        response = true;
        Failure = false;
        failureCount = 0;
      }

      attemptCount++;
      if (attemptCount == 3) 
      {
        client.stop();
        break;
      }
    }
    client.stop();
  }
}

void loop() 
{
  if (failureCount >= MAX_failureCount || Allow_Retransmit == false)
  {
    Failure = false;
    failureCount = 0;
    proceed = false;
  }

  // no failure- get new Packet
  if (!Failure) 
  {
    receiveSerial();
  }

  // if new packet is obtained, or retrying current packet
  if (proceed == true || Failure >= 1) 
  {
    transmitTCP();
    proceed = false;
  }
}
