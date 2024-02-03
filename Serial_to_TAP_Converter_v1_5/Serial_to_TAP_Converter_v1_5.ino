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
int STX = 0x02;
int ETX = 0x03;
int EOT = 0x04;
int ENQ = 0x05;
int ACK = 0x06;
int ESC = 0x1b;
int newLineN = 0x4e; // N
int newLineA = 0x41; // A
int newLine = 0x0A;  // <LF>
int CR = 0x0D;       // <CR>

// VARIABLES

#define MAX_QUEUE_SIZE 10   // Define the maximum size of the queue
#define MAX_FAILURE_COUNT 2 // Define the maximum size of the queue
#define BUFFER_SIZE 200     // Define the maximum size of the queue

bool DEBUG = false;

////////////////////////////////////////////////////
// NETWORK INFORMATION
////////////////////////////////////////////////////
// Modify as required

// Arduino Settings
EthernetClient client;
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED}; // MAC Address used by this Arduino
IPAddress ip(192, 168, 0, 222);                    // IP of this Arduino
IPAddress gateway(192, 168, 0, 1);                 // Gateway for this Arduino (if needed)
IPAddress subnet(255, 255, 255, 0);                // Subnet of this Arduino

// Server Settings
IPAddress server(192, 168, 0, 103); // TAP Server IP (TAP PARSER ACS110)
int port = 7000;                    // TAP Server Port
String CapCode = "919";             // Capcode appended to incoming strings.

///////////////////////////////////////////////////
// END NETWORK CONFIG
// No end user config should be performed below
///////////////////////////////////////////////////

///////////////////////////////////////////////////
// Messaging Functions
///////////////////////////////////////////////////

struct Message
{
  String data;
  String checkSum;
  int failureCount;
};

Message messageQueue[MAX_QUEUE_SIZE];
int front = 0;
int rear = -1;
int itemCount = 0;

bool isQueueEmpty()
{
  return itemCount == 0;
}

bool isQueueFull()
{
  return itemCount == MAX_QUEUE_SIZE;
}

void enqueue(Message message)
{
  if (!isQueueFull())
  {
    if (rear == MAX_QUEUE_SIZE - 1)
    {
      rear = -1;
    }

    messageQueue[++rear] = message;
    itemCount++;
  }
}

Message dequeue()
{
  Message message = messageQueue[front++];

  if (front == MAX_QUEUE_SIZE)
  {
    front = 0;
  }

  itemCount--;
  return message;
}

///////////////////////////////////////////////////
// Business Logic
///////////////////////////////////////////////////

void setup()
{
  Serial.begin(9600);
  // Ethernet.begin(mac, ip, gateway, DNSserver, subnet);
  Ethernet.begin(mac, ip, gateway, subnet);
}

void checkAndQueueSerialData()
{

  if (Serial.available())
  {
    char incomingData[BUFFER_SIZE]; // Buffer to store incoming data

    if (Serial.peek() == 13)
    {
      Serial.read();
      return;
    };
    // Read until <CR>
    Serial.readBytesUntil(CR, incomingData, BUFFER_SIZE);

    if (incomingData[0] == 13)
    {
      String(serialIn) = "Blank Line do Nothing";
    }

    String Output = String(incomingData);
    strcpy(incomingData, Output.c_str());

    Message message;
    message.data = Output;

    //////////////////
    // CheckSum
    //////////////////

    // Part 1:-
    // Convert to Decimal, and message.checkSum them all together
    // Create a 12 digit long binary string

    int sum = 0;

    // Data Segment
    for (int b = 0; b < BUFFER_SIZE; b++)
    {
      if (incomingData[b] == '\0')
      {
        break;
      }
      sum += incomingData[b];
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

    int MaskA = 3840; // B111100000000
    int A = (sum & MaskA) >> 8;
    int MaskB = 240; // B000011110000
    int B = (sum & MaskB) >> 4;
    int MaskC = 15; // B000000001111
    int C = (sum & MaskC);

    // Add Decimal 48 as per checksum specification
    A += 48;
    B += 48;
    C += 48;

    // make a string with these chars
    message.checkSum = String(A) + String(B) + String(C);

    if (DEBUG)
    {
      Serial.print("Checksum: ");
      Serial.println(message.checkSum);

      Serial.print("Data: ");
      Serial.println(message.data);
    }

    // Enqueue the message
    if (!isQueueFull())
    {
      enqueue(message);
    }

    // clear the buffer
    memset(incomingData, 0, sizeof(incomingData));
  }
}

void processAndTransmitQueuedMessages()
{
  while (!isQueueEmpty())
  {
    Message msg = dequeue();
    bool success = transmitTCP(msg);
    if (!success)
    {
      msg.failureCount++;
      if (msg.failureCount < MAX_FAILURE_COUNT)
      {
        enqueue(msg); // Re-queue the message for retry
      }
      else
      {
        // Handle max failure count reached (e.g., log error, alert, etc.)
      }
    }
  }
}

bool transmitTCP(Message msg)
{
  // IF connection avaliable, send
  if (client.connect(server, port))
  {

    //////////////////////////////////
    // Wakeup the Paging Terminal (SERVER)
    //////////////////////////////////
    bool response = false;
    int attemptCount = 0;
    while (!response)
    {
      client.write(CR);
      if (client.findUntil("ID", "="))
      {
        response = true;
      }
      attemptCount++;
      if (attemptCount == 3)
      {

        client.stop();
        return false;
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
    // String Line= String(CR);
    if (!client.find("110 1.8"))
    {
      client.stop();
      if (DEBUG)
      {
        Serial.println("Server Validation Failed - 110 1.8 not found");
      }
      return false;
    }

    ////////////////////
    // Server OK to Send
    ////////////////////

    // If we dont get ACK
    if (!client.find(ACK))
    {
      // set flag for retransmit
      client.stop();
      return false;
    }

    ////////////////////
    // Send Message
    ////////////////////

    // v1.5 Move this OUT of Loop!

    // Datagram
    client.write(STX);
    client.print(CapCode); // Destination Capcode
    client.write(CR);
    client.print(msg.data);
    client.write(CR);

    // Checksum
    client.write(ETX);
    client.print(msg.checkSum);
    client.write(CR);
    if (DEBUG)
    {
      Serial.println("Message Sent");
      Serial.println(msg.checkSum);
    }
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
      }
      else
      {
        attemptCount++;
      }
      if (attemptCount >= 3)
      {
        client.stop();
        return false;
      }
    }

    ////////////////////
    // Client Disconnect
    ////////////////////
    // v1.5 pull this out of the loop
    client.write(EOT);
    client.write(CR);

    while (!response)
    {

      // v1.5 Change the char im testing for
      // 115 Exited normally<CR>
      if (client.find(CR))
      {
        // touch lazy here, assuming the disconnect is fine
        response = true;
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
  return true;
}

void loop()
{
  checkAndQueueSerialData();
  processAndTransmitQueuedMessages();
}
