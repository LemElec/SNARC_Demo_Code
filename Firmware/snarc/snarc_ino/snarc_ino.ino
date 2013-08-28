//Wizard^^ last update 25th August 2013 - adding support for SNARC 1.2 pin change for MOSFET from D14 to D6
// Buzz last update 6th april - added support for automatically re-initialising wiznet module every 60 secs until the network comes good again. ( needs bluewire mod to connect wiznet reset pin to arduino D8).  Also added support for MOSFET pin being inverted logic or normal. Also reduced RAM usage by using F() more.
// Buzz last updated this 5 April 2013 - added support for concurrent http  outgoingclient and server, minor tweaks, and commited latest to github link below.
// Hovo last updated this 07 Feb 2013
// Buzz last updated this 21 Jan 2013

// see https://github.com/davidbuzz/snarc for more info
// the door code we use ( as per below) is now *nearly* an exact copy of the above github repo, except for the IP address assignments and secrets and some tweaks.
// this IS the code you are looking for :-)
// NOTE YOU ( YES YOU ) WILL NEED TO
// Update to the latest Ethernet code to support the W5200 (Look on WizNet website)
// http://www.wiznet.co.kr/Sub_Modules/en/product/Product_Detail.asp?pid=1161
// TIP: if everything else looks right, but you can't get your device to PING, even after 30 seconds of uptime, then read the two lines above this one!   SERIOUS!
//
#include <SPI.h> //needed by Ethernet.h
#include <EEPROM.h>
#include <Ethernet.h>
#include <SoftwareSerial.h>

// un-comment if debugging
// #define DEBUG

// un-comment if it applies
// SNARC+ PIN selection
// #define SNARCPLUS
// SNARC 1.2 PIN selection
#define SNARC_1_2

// We connect a RFID reader to a "Software Serial" pin, it needs a pin for RX data to arrive on
// we use one of the 5 generic I/O pins for this, leaving the hardware serial for programming and console.
#define RXSOFT 15

// External I/O pins that can be used for anything , but we connected some extra LED's to: RED/YELLOW/GREEN
#ifdef SNARCPLUS
#define LEDRED    5
#define LEDYELLOW 6
#define LEDGREEN  7
#else
#define LEDRED  17
#define LEDYELLOW 18
#define LEDGREEN 19
#endif

// SNARCs can have issues booting the wiznet module/s reliably, so we connect the reset pin of the wiznet module to
// an arbitrary I/O pin, and if we detect networking issue/s, then we can do a wiznet reset.  
// NOTE:   wiznet module can take 30 secs to init, so we must ensure we don't do it too often,
#define WIZRESET 8 // D8 lower left corner pin on atmel328p is easiest to "blue wire' .  D7 could also be used.

// flashing GREEN LED means operating correctly!
// flashing RED LED means "in programming mode"
// fast flashing RED LED means under E-STOP condition.

//ONBOARD LED related variables:
#ifndef SNARCPLUS
#define LED_PIN1 5  //GREEN HIGH
#define LED_PIN2 6  //RED HIGH
#endif
enum LEDSTATES {
  RED , GREEN , OFF }
;
int ledState = RED;  // toggle to turn the LEDS on/off/etc
long previousMillis = 0;
long interval = 1000;           // interval at which to blink (milliseconds)


// ie "press here in emergengy to stop device"
// connect estop button so it grounds pin 3 when pressed.
// connect mosfet output ( THISSNARC_OPEN_PIN )  so it activates/deactivates tool/equipment
//#define ENABLE_ESTOP_AS_SAFETY_DEVICE  
// OR
// ie "press here to exit"
// connect "pressto exit" button the same as estop.... so it grounds pin 3 when pressed.
// connect mosfet output ( THISSNARC_OPEN_PIN )  so it activates/deactivates electric/magnetic door strike

#define ENABLE_ESTOP_AS_EGRESS_BUTTON 1

//
#ifdef ENABLE_ESTOP_AS_SAFETY_DEVICE
#ifdef ENABLE_ESTOP_AS_EGRESS_BUTTON
#error Can not define both ENABLE_ESTOP_AS_EGRESS_BUTTON and ENABLE_ESTOP_AS_SAFETY_DEVICE
#endif
#define ESTOP_PIN 3
#define ESTOP_INTERRUPT_PIN 1
volatile bool lockdown = false;
#endif

#ifdef ENABLE_ESTOP_AS_EGRESS_BUTTON
#ifdef ENABLE_ESTOP_AS_SAFETY_DEVICE
#error Can not define both ENABLE_ESTOP_AS_EGRESS_BUTTON and ENABLE_ESTOP_AS_SAFETY_DEVICE
#endif
#define ESTOP_PIN 3
#define ESTOP_INTERRUPT_PIN 1
volatile bool egressrequest = false;
#endif

#define CLIENT 1   // comment out this line to completely disable all ETHERNET and networking related code

#define SERIAL_RECIEVE_BUFFER_LENGTH 32 // Length of serial read buffer

#define DEVICE_NAME_MAX_LENGTH 12 // Maximum length of the device name

// EEPROM address offsets for saving data
#define MAC_START_ADDRESS 0 // Where to save the mac address in the eeprom (needs 6 bytes)
#define DEVICE_NAME_ADDRESS 6 // Where to save the device name
#define EEPROM_CARDS_START_ADDRESS 20 // Which address to start saving cards to. Should be the last address


// Pin that is already connected to a MOSFET that'll act as a switch for door strikes, power tools, etc.
#ifdef SNARC_1_2
#define THISSNARC_OPEN_PIN 6
#else
#define THISSNARC_OPEN_PIN 14  //54 on the mega, or 14 on the normal arduino
#endif

// one of these must be HIGH, the other LOW
#define OPEN LOW    // choose 'OPEN HIGH'  or 'OPEN LOW' depending on your MOSFET config.
#define LOCKED HIGH    // choose 'LOCKED HIGH'  or 'LOCKED LOW' depending on your MOSFET config - must be the opposite of OPEN line .  


// pin that we'll allow users to remotely toggle on/off with a web interface:  - by default the "strike" ( to unlock door remotely )
#define IOPIN THISSNARC_OPEN_PIN

// we use one of the 5 generic I/O pins to enable/disable th RFID reader ( so that the LED on it changes green briefly  when users swipe )  
#define THISSNARC_RFID_ENABLE_PIN 16



/* USAGE: be sure to revise this script for each piece of hardware:
 * change the Mac address
 * change the IP address that it should be assigned to ( default is 192.168.7.7 )
 * make sure the "remoteserver" it will authenticate to is correct ( default is 192.168.7.77 )
 * make sure the URL that is being used for your "auth server" is correct.  ( default is /auth.php? )
 * make sure you have it wired-up correctly! Code currently assumes the RFID reader is on Software Serial on RX/TX pins  D15 and D16 ( also sometimes called A1 and A2 )
 */

#ifdef CLIENT
// network settings:
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

char device_name[DEVICE_NAME_MAX_LENGTH + 1]; // +1 for terminating char


#define THISIP_1 10
#define THISIP_2 0
#define THISIP_3 1
#define THISIP_4 240

// remote server IP is put here:
#define SERVERIP_1 10
#define SERVERIP_2 0
#define SERVERIP_3 1
#define SERVERIP_4 253

// fill in an available IP address on your network here,
// for manual configuration:
IPAddress ip(THISIP_1,THISIP_2,THISIP_3,THISIP_4);
IPAddress gateway(10,0,1,254); // <-- network gateway IP is put here.
IPAddress subnet(255, 255, 254, 0);
IPAddress remoteserver( SERVERIP_1, SERVERIP_2, SERVERIP_3, SERVERIP_4 ); // destinaton end-point is "auth server" IP.


// when a client connects to the server, we cache the incoming data here, max 100 bytes
String clientReadString;


byte serial_recieve_data[SERIAL_RECIEVE_BUFFER_LENGTH];
byte serial_recieve_index = 0;

// be a server, on local port 80, not a client:
EthernetServer localserver(80);
// when a client connects to the server, we cache the incoming data here, max 100 bytes:
String serverReadString;

// also, be a http client, not a server, connect to remote port 80
EthernetClient outgoingclient;

// other connection related state variables:
unsigned long lastConnectionTime = 0;          // last time you connected to the server, in milliseconds
boolean lastConnected = false;                 // state of the connection last time through the main loop
const unsigned long postingInterval = 2*1000;  // minimum delay between updates, in milliseconds
const unsigned long pollingInterval = 60;  // maximum time between network checks, in seconds


#endif

SoftwareSerial RFIDSerial(RXSOFT, 21);  //RFID reader is on Soft Serial port.   A1-A5 are D15-D20 , 21 is unused, and just a placeholder as we don't use TX


// where in EEPROM do we write the next EEPROM?
// this field is populated by either write_codes_to_eeprom() or read_eeprom_codes() or find_last_eeprom_code()
// and after each of tehse will be left pointing to the "next slot" in the EEPROM to write to
unsigned int last_address = 0;

// this may be used by find_code_in_eeprom() to point to the last-located code's EEPROM address ( for if we need to update it)
unsigned int last_found_address = 0;


// last detected code, and which door it was that triggered it.
char last_code[12];
int  last_door ;

//when writing an invalid code, write this one:
char invalidCode[] = "          ";

// If you use more than one door in a "set" of readers, you can name there all here. Keep this consistent between all doors, and also the
//script needs it too!
// Always put "INVALID" as the first element, then other elements follow:
// eg multi-door system:
//enum AccessType {  INVALID, INNER , OUTER, BACK, OFFICE1, SHED, LIGHTS };
// eg one door system:
enum AccessType {  
  INVALID, INNER };


//  Which of the above list is this particulare one?  ( this should be different for each door/reader that has different permissions )
//  This tells us which "bit" in the access data we should consider ours!
// eg: this means that if the server sends "access:1, then only the "INNER" door is permitted. if the server sends "access:3" then both the INNER and OUTER door are permitted.  If it sends "access:8" then only "SHED" is permittd  ( it's a bit-mask )
#define THISSNARC INNER


void setup()
{

  // start console ASAP.
  Serial.begin(19200);

  delay(200);   // delay boot by 1sec to allow power rail time to stabilise, etc ( ethernet module draws mucho powero )
 
  wiznet_reset();
 
  randomSeed(analogRead(A1));

  // we setup the generic I/O pins that are used for external LEDS here:
  pinMode(LEDRED, OUTPUT);
  digitalWrite(LEDRED, LOW); //RED
  pinMode(LEDYELLOW, OUTPUT);
  digitalWrite(LEDYELLOW, LOW); //YELLOW
  pinMode(LEDGREEN, OUTPUT);
  digitalWrite(LEDGREEN, LOW); //GREEN


  Serial.println();
  //get_mac_address();
  load_device_name();

  // Display basic config details
  Serial.print(F("Name: "));
  print_device_name();
  Serial.println();


#ifdef CLIENT

  // start the Ethernet connection using a fixed IP address and DNS server:
  //Ethernet.begin(mac, ip, myDns);
  Ethernet.begin(mac, ip, gateway, subnet);
 

  // print the Ethernet board/shield's IP address:
  Serial.print("THIS IP: (hardcoded) ");
  //Serial.println(Ethernet.localIP()) - only works with DHCP.. I think?  seems to return dud info otherwise.
  Serial.print(THISIP_1,DEC);  
  Serial.print(F("."));
  Serial.print(THISIP_2,DEC);  
  Serial.print(F("."));
  Serial.print(THISIP_3,DEC);  
  Serial.print(F("."));
  Serial.println(THISIP_4,DEC);
 
    // start the local server too.
  localserver.begin();


  Serial.print(F("SERVER IP: (hardcoded) "));
  Serial.print(SERVERIP_1,DEC);  
  Serial.print(F("."));
  Serial.print(SERVERIP_2,DEC);  
  Serial.print(F("."));
  Serial.print(SERVERIP_3,DEC);  
  Serial.print(F("."));
  Serial.println(SERVERIP_4,DEC);


  Serial.print(F("MAC:  "));
  print_mac_address();
  Serial.println();

#endif


#ifdef ENABLE_ESTOP_AS_EGRESS_BUTTON
  // Configure E-Stop pin
  pinMode(ESTOP_PIN, INPUT);
  digitalWrite(ESTOP_PIN, HIGH);
  attachInterrupt(ESTOP_INTERRUPT_PIN, estop_interrupt_handler, CHANGE);
#endif
#ifdef ENABLE_ESTOP_AS_SAFETY_DEVICE
  // Configure E-Stop pin
  pinMode(ESTOP_PIN, INPUT);
  digitalWrite(ESTOP_PIN, HIGH);
  attachInterrupt(ESTOP_INTERRUPT_PIN, estop_interrupt_handler, CHANGE);
#endif

  // logging/programming/USB interface   
  Serial.println(F("started - network will initialise in approx 30 secs or less, please ensure it PINGs before doing networking tests "));



  // "request exit" button input
  //pinMode(REX_PIN, INPUT);
  //digitalWrite(REX_PIN, HIGH);

  // status led
#ifndef SNARCPLUS
  pinMode(LED_PIN1, OUTPUT);
  pinMode(LED_PIN2, OUTPUT);

  digitalWrite(LED_PIN1, LOW);
  digitalWrite(LED_PIN2, LOW);
#endif
  ledState = OFF;

  // Inner Door reader
  RFIDSerial.begin(2400); // RFID reader SOUT pin connected to Serial RX pin at 2400bps
  pinMode(THISSNARC_RFID_ENABLE_PIN, OUTPUT); // RFID /ENABLE pin
  digitalWrite(THISSNARC_RFID_ENABLE_PIN, LOW);
  pinMode(THISSNARC_OPEN_PIN, OUTPUT); // Setup internal door opener
  digitalWrite(THISSNARC_OPEN_PIN, LOCKED); // default to locked.


  //
  programming_mode();


  Serial.println(F("Entering Normal Mode! "));


}

void wiznet_reset() {
 
    //WIZRESET - ~30-48 secs after this, it pings - this is probably overkill, but I'm not sure if it's leading or trailing edge trggered, so I do both
  pinMode(WIZRESET, OUTPUT);
  digitalWrite(WIZRESET, HIGH);
    delay(50);
  digitalWrite(WIZRESET, LOW);
    delay(50);
  digitalWrite(WIZRESET, HIGH);
  delay(200);

}

void clear_serial_buffer ()
{
  while (Serial.available())
  {
    Serial.read();
  }
}

void prompt() {
  Serial.println();
  Serial.println(F("PROGRAM MODE:"));
  Serial.println(F("r - read eeprom list"));
  Serial.println(F("n - program new key to EEPROM"));
  Serial.println(F("s - test server interface ( sends fake tag 1234567890 to server ) "));
  Serial.println(F("d - set device name"));
  Serial.println(F("i - wipe and initialise EEPROM (dangerous!) "));
  Serial.println(F("w - write hard-coded tags to EEPROM (dangerous!)"));
  Serial.println(F("z - delete a single card from EEPROM"));
#ifdef CLIENT
  Serial.println(F("m - set/reset MAC address"));
  Serial.println(F("a - set device IP address"));
#endif
  Serial.println();
  Serial.println(F("x - exit programming mode"));
}


void programming_mode() {

  clear_serial_buffer();
  Serial.println(F("Press a few keys, then ENTER *NOW* to start programming mode ( 3...2...1... ) "));

  delay(3000);

  // three ENTER keys is >2, AND WE ARE IN PROGRAMMING MODE !  
  int incomingByte = 0;

#ifdef DEBUG
  if (true) // DEBUG: Always enter programming mode
#else
      if (Serial.available() > 2)
#endif
    {
      clear_serial_buffer();
      Serial.println(F("Entered Programming Mode! "));
      prompt();

      while( incomingByte != -1 ) {

        led_flash(RED);   // RED FLASH FOR PROGRAMMING MODE

        if ( Serial.available() ) {
          incomingByte = Serial.read();
          Serial.println((char)incomingByte);

          switch((char)incomingByte) {
          case 's':
            // test send to server
            Serial.println(F("testing send_to_server2() function"));
            Serial.println(send_to_server2("1234567890", 0));
            prompt();
            break;
#ifdef CLIENT
          case 'm':
            Serial.println(F("disabled in implementation sorry ( buggy ) , use hardcode MACs only."));
            // TODO: Implement IP address change
            // set MAC address
            //listen_for_new_mac_address();
            prompt();
            break;
          case 'a':
            Serial.println(F("not yet implemeted, sorry."));
            // TODO: Implement IP address change
            prompt();
            break;
#endif
          case 'd':
            listen_for_device_name();
            prompt();
            break;
            // w means "write" initial LIST to EEPROM cache - undocumented command for initial population of eeprom only during transition.
          case 'w':
            // generally yuou should do the 'i' before 'w'
            // Serial.println(F("please wait, erase in progress...."));
            // init_eeprom();
            Serial.println(F("please wait, writing codes...."));
            write_codes_to_eeprom();
            Serial.print(F("address:"));
            Serial.println(last_address);
            prompt();
            break;

          case 'i':
            Serial.println(F("please wait, erase in progress...."));
            init_eeprom(); //wipe all codes
            Serial.print(F("address:"));
            Serial.println(last_address);
            prompt();
            break;

            // r mean read current list from EEPROM cache
          case 'r':
            read_eeprom_codes();
            Serial.print(F("address:"));
            Serial.println(last_address);
            prompt();
            break;

            // z means delete a single key from EEPROM without deleting all of them.
          case 'z':
            //read_eeprom_codes();
            delete_eeprom_code();
            Serial.print(F("address:"));
            Serial.println(last_address);
            prompt();
            break;


            // x mean exit programming mode, and resume normal behaviour
          case 'x':
            incomingByte = -1; // exit this mode
            break;

            // ignore whitespace
          case '\r':
          case '\n':
          case ' ':
            break;
            // n means write new code to EEPROM
            // ( the next key scanned
          case 'n':
            {

              //read_eeprom_codes(); // nee to do this so as to get last_address pointing ot the end of the EEPROM list.
              find_last_eeprom_code();  // updates last_address to correct value without any output.

              listen_for_codes();
              // result is in last_door and last_code

              unlockDoor(); // clunk the door as user feedback

              // see if the key already has access
              int access = matchRfid(last_code) & 0xF;

              Serial.print(F("EEPROM access level:"));
              Serial.println(access);

              // IF THIS DOOR/READER is not already in the permitted access list from the EEPROM/SERVER allow it!
              // this converts the bit number, to its binary equivalent, and checks if that bit is set in "access"
              // by using a binary "and" command.
              Serial.print(F("THISSNARC:"));
              Serial.print(THISSNARC);
              Serial.print(F(" bits:"));
              Serial.print((  1 << ( THISSNARC - 1 ) ));
              Serial.print(F(" more bits:"));
              Serial.println(access & (  1 << ( THISSNARC - 1 ) ));

              if ( (access & (  1 << ( THISSNARC - 1 ) )) ==  0 ) { // ie no access yet for this door, yes all these brackets are needed

                // append this ocde to the EEPROM, with current doors permissions+ new one
                write_next_code_to_eeprom(last_code, access | (  1 << ( THISSNARC - 1 ) ));

              }
              else {
                Serial.println(F("Card already has access, no change made"));  
              }
              prompt();
            }   
            break;  

          default:   
            // nothing
            prompt();
            break;
          } //switch/case
          clear_serial_buffer();
        } // if
      } //while

    }  //END PROGRAMMING MODE

  Serial.println(F("Exited Programming Mode! "));
#ifdef ENABLE_ESTOP_AS_SAFETY_DEVICE
  if (lockdown)
  {
    Serial.println(F("E-STOP triggered while in programming mode."));
  }
#endif
}



// now we optionally can do an outgoing client......
int send_to_server2(char *tag, int door ) {

  // side-effect: LOG TO USB SERIAL:
  Serial.print(F("OK: Tag is "));
  Serial.print(tag);
  Serial.println(F(" .")); // needed by space server auth script to represent end-of-communication.


  // if there's incoming data from the net connection.
  // send it out the serial port.  This is for debugging
  // purposes only:

  // if there's no net connection, but there was one last time
  // through the loop, then stop the outgoingclient:
  if (!outgoingclient.connected() && lastConnected) {
    Serial.println();
    Serial.println(F("disconnecting."));
    outgoingclient.stop();
  }

  // if you're not connected, and X seconds have passed since
  // your last connection, then connect again and send data:
  if(!outgoingclient.connected() && (millis() - lastConnectionTime > postingInterval)) {
     Serial.println(F("pre-connecting!......"));

    // if there's a successful connection:
    if (outgoingclient.connect(remoteserver, 80)) {
      //Serial.println("connecting...");
      // send the HTTP PUT request:
      Serial.println(F("http outgoingclient connected"));
      outgoingclient.print(F("GET /logger.php?secret=asecret&q="));
      outgoingclient.print(tag);
      outgoingclient.print(F("&d="));
      outgoingclient.println(door);
      outgoingclient.println();
      Serial.println(F("http outgoingclient finished"));

      // note the time that the connection was made:
      lastConnectionTime = millis();

      Serial.println(F("connected."));
      //   delay(1); // wait for data

    }
    else {
      // if you couldn't make a connection:
      Serial.println(F("http connection failed"));
      // Serial.println("disconnecting unclean.");
      outgoingclient.stop();
      return -1; // error code to say server offline, which is different to "0" , which means deny access.
   }
  }
  else {
    Serial.print(F("too soon for next http request, please slow them down to wait at least  "));
    Serial.print(postingInterval/1000);
    Serial.println(F(" secs between them."));
    return -1; // -1 means "http request failed"
  }


  while(outgoingclient.connected() && !outgoingclient.available()) delay(1); //waits for data


  //  if we were just connectd by the above block we'll have data, check fgor it now!
  while (outgoingclient.connected() || outgoingclient.available()) { //connected or data available
    char c = outgoingclient.read();
    Serial.print(c);

    // good data:
    if (clientReadString.length() < 100) {
      //store characters to string
      clientReadString += c;
    }
    // bad data:
    if (clientReadString.length() >= 100) {
    Serial.println(F("too much HTTP data, error! ( do you have an auth.php on the server? ) "));
    return -1; // -1 means "http request failed"
    }

  }
  clientReadString += 0; // null terminte my string.

  // Serial.println("data done.");
  // Serial.println(outgoingclient.connected());
  // Serial.println(outgoingclient.available());


  // finally  stop the outgoingclient:
  if (! outgoingclient.connected() ) {
    Serial.println();
    Serial.println(F("disconnected ok."));
    outgoingclient.stop();
  }

  // store the state of the connection for next time through
  // the loop:
  lastConnected = outgoingclient.connected();

  // now we are done with the http garbage, we can process the data we got back from the server:

  Serial.print(F("http data:"));
  // recieved data is now in the string: client_recieve_data
  Serial.println(clientReadString);

  // we expect the permissions string to look like 'access:3' ( for permit ), or 'access:0' (for deny )
  int colonPosition = clientReadString.indexOf(':');

  String scs = clientReadString.substring(colonPosition + 1);  // as a "String" "object" starting from after the colon to the end!
  char cs[10]; // client permission, as a string
  scs.toCharArray(cs,10); // same thing as a char array "string" ( lower case)
  Serial.print(F("perms from server:"));
  Serial.println(cs);
  int ci = atoi(cs); // client permission , as an int!  if this fails, it returns zero, which means no-access, so that's OK.


  //clearing string for next read
  clientReadString = "";

  // basic bound check,  return -1 on error .
  if ( ci < 0 || ci > 255 ) {
    return -1;    // -1 means "http request failed"
  }

  // return permission from server.
  return ci;   


}


// scan the EEPROM looking for a matching RFID code, and return it if it's found
//also, as a side-affect, we leave last_found_address pointing to it.
int matchRfid(char * code)
{
  int address = 0;
  int result = 0; // access level from EEPROM
  bool match = false;

  while(!match)
  {
    Serial.print(F("checking against eeprom location:"));
    Serial.println(address);    

    // read the "result" ( the access level ) of the next card, and if INVALID, that's the end-of-list indicator!
    if((result = EEPROM.read(EEPROM_CARDS_START_ADDRESS + address*11)) == INVALID)
    {
      // end of list
      Serial.println(F("Error: No tag matched in eeprom"));
      return INVALID;
    }

    // ok, so it's not the end of the list, so let's compare our tag to the one at this location:
    match = true;
    for(int i=0; i<10; i++)
    {
      int b = EEPROM.read(EEPROM_CARDS_START_ADDRESS + address*11+i+1);

      Serial.print(F("testing:"));
      Serial.print(b,HEX);
      Serial.print(F(":"));
      Serial.println(code[i],HEX);

      if( b != code[i])
      {
        Serial.print(F("code-mis-match found at i value of: "));
        Serial.println(i);

        match = false;
        break;
      }
    }


    address++;
  }
  Serial.print(F("OK:  tag match found in eeprom with access level:"));
  Serial.println(result);

  last_found_address = address-1; //remember it incase we need to "UPDATE" this location later....

  return result;
}

// pull the codes from EEPROM, emit to Serial0
void read_eeprom_codes() {

  int address = 0; // eeprom address pointer
  int result = 0;  
  char codeout[12]; // holder for code strings as we output them

  // read EEPROM in 11 byte chunks till we get an INVALID tag to label the end of them.
  while((result = EEPROM.read(EEPROM_CARDS_START_ADDRESS + address*11)) != INVALID)
  {
    for(int i=0; i<10; i++)
    {
      codeout[i] = EEPROM.read(EEPROM_CARDS_START_ADDRESS + address*11+i+1);
    }
    codeout[10] = '\0'; //end of string

    // write the current code to the USB port for the user!
    Serial.print(F("code: "));
    Serial.print(codeout);
    Serial.print(F(" at posn: "));
    Serial.println(address);

    address++;
  }

  last_address = address;
}

// simpler/quieter variant of the above, used in different circumstance.
void find_last_eeprom_code() {
  int address = 0; // eeprom address pointer
  int result = 0;  
  //  char codeout[12]; // holder for code strings as we output them

  // read EEPROM in 11 byte chunks till we get an INVALID tag to label the end of them.
  while((result = EEPROM.read( EEPROM_CARDS_START_ADDRESS + address*11)) != INVALID)
  {
    address++;
  }

  last_address = address;
}

void unlockDoor()
{
  digitalWrite(LEDGREEN, HIGH);

  digitalWrite(THISSNARC_RFID_ENABLE_PIN, HIGH);
  digitalWrite(THISSNARC_OPEN_PIN, OPEN);
#ifndef SNARCPLUS
  digitalWrite(LED_PIN1, HIGH);
  digitalWrite(LED_PIN2, LOW);
#endif
  Serial.println(F("OK: Opening internal door"));
  delay(4000);
  digitalWrite(THISSNARC_OPEN_PIN, LOCKED);
  digitalWrite(THISSNARC_RFID_ENABLE_PIN, LOW);
#ifndef SNARCPLUS
  digitalWrite(LED_PIN1, LOW);
  digitalWrite(LED_PIN2, LOW);
#endif
  //Serial.println(F("OK: Normalising internal door"));


  digitalWrite(LEDGREEN, LOW);

}


#ifdef ENABLE_ESTOP_AS_SAFETY_DEVICE
void actuator_on()
{
  if (lockdown)
  {
    digitalWrite(ACTUATOR_PIN, LOW); // Force low
    estop_lockdown();
  }
  digitalWrite(ACTUATOR_PIN, HIGH);
}

void actuator_off()
{
  digitalWrite(ACTUATOR_PIN, LOW);
}
#endif


void loop () {

bailout:

#ifdef ENABLE_ESTOP_AS_SAFETY_DEVICE
  if (lockdown)
  {
    estop_lockdown();
  }
#endif
  // char code[12]; // 10 chars of code , 1 byte of null terminator, and one byte of the door number.

  // reset global variables each time thru
  last_code[10]=0; // null terminator
  last_code[0]=0;
  last_door =  INVALID;

  // int door = INVALID; // assume INVALID to start with.


  listen_for_codes();   // blocking, waits for a code/door combo, then writes the result into global last_code and last_door

  //    Serial.println(F("TAG detected!"));
  //    Serial.println(last_code);  //don't tell user the full tag number

  // NOTE, THIS MATCHES AGAINS THE EEPROM FIRST, NOT THE REMOTE SERVER, AS ITS FASTER ETC.  
  // IN ORDER TO UPDATE OUR EEPROM AGAINST THE REMOTE SERVER, WE DO THAT *AFTER* allowing someone through ( for speed)
  // but before denying them fully.
  int access = matchRfid(last_code) & 0xF; // high bits for future 'master' card flag

  Serial.print(F("EEPROM access level:"));
  Serial.println(access);


  // last_code = the just-scanned RFID card string   
  // last_door = the integer representing the door this code was just scanned at  (not necessarily permitted )

  int serveraccess = INVALID; // what the remote server is willing to allow the user, we cache this info to EEPROM

  // THISSNARC DOOR CHECK
  if ( last_door == THISSNARC ) {
    //
    if( access & ( 1 << ( THISSNARC - 1 ) ) )  
      //if ( 1)   // use this line instead of above one to allow *any* key that will scan.
    {
      unlockDoor();
      serveraccess = send_to_server2(last_code,last_door); //log successes/failures/
      //etc, and return the permissions the server has.
      //serveraccess = 3; // uncomment to fake or override server response
      if ( serveraccess == -1 ) {
        Serial.println(F("Server offline , no further options available. "));
        goto bailout; // end this loop itteration.
      }

      // after the fact, if the server access and the local cached access don't match, update the local one
      // this can be used for "revoking" keys, by setting they access to INVALID/0
      if ( serveraccess != access ) {
        writeCode( last_found_address, last_code , serveraccess ) ;
      }  
    }
    else {
      Serial.println(F("Error: Insufficient rights in EEPROM for THISSNARC door - checking server"));  // must end in fullstop!

      serveraccess = send_to_server2(last_code,last_door); //log successes/failures/etc, and return the permissions the server has.

      if ( serveraccess == -1 ) {
        Serial.println(F("Server offline , no further options available. "));
        goto bailout; // end this loop itteration.
      }

      if(serveraccess & ( 1 << ( THISSNARC -1 ) ) ) {
        unlockDoor();
        //NOTE:   to get here, the key was NOT in EEPROM, but WAS on SERVER, SO ALLOW IT, AND CACHE TO EEPROM TOO!
        if ( access != THISSNARC ) {
          // append this ocde to the EEPROM, with BOTH doors permissions:
          write_next_code_to_eeprom(last_code,serveraccess);
        }  
      }
      else {
        Serial.println(F("Error: Insufficient rights ( from server) for THISSNARC door "));
      }
    }
  }


}



// blocking function that waits for a card to be scanned, and returns the code as a string.
// WE ALSO supply THE DOOR/READER NUMBER AS THE 12th BYTE in the string ( the 11th byte is the null string terminator )
// result data is put into global last_code and last_door, not returned.
void listen_for_codes ( void ) {

  // boolean

  // before starting to listen, clear all the Serial buffers:
  while (RFIDSerial.available() > 0) {
    RFIDSerial.read();
  }

  RFIDSerial.flush();


  Serial.println(F("listening to READERS, please swipe a card now"));

  int  found = 0 ;
  while ( found == 0 ) { // blocking loop, only exits when card is swiped.

#ifdef ENABLE_ESTOP_AS_SAFETY_DEVICE
    if (lockdown)
    {
      estop_lockdown();
    }
#endif


#ifdef  ENABLE_ESTOP_AS_EGRESS_BUTTON
    if ( egressrequest == true ) {
      digitalWrite(LEDRED,HIGH);
      Serial.println(F("egress request..."));
      unlockDoor();
      egressrequest=false;
      digitalWrite(LEDRED,LOW);
    }
#endif
    //     char code[12];
    int val;
    last_code[10]=0;
    last_code[0]=0;
    last_door = INVALID;


    led_flash(GREEN);   // GREEN FLASH FOR READY MODE

    // input waiting from internal rfid reader
    if (RFIDSerial.available() > 0)
    {
      if ((val = RFIDSerial.read()) == 10)
      {
        int bytesread = 0;
        while (bytesread < 10)
        { // read 10 digit code
          if (RFIDSerial.available() > 0)
          {
            val = RFIDSerial.read();
            if ((val == 10) || (val == 13))
            {
              break;
            }
            last_code[bytesread++] = val;
          }
        }
        if(bytesread == 10)
        {
          Serial.println(F("TAG detected!"));
          Serial.println(last_code);  //maybe we could not tell user the full tag number?

          // give a little user feedback, even on bad reads!
          digitalWrite(THISSNARC_RFID_ENABLE_PIN, HIGH);       
          delay(200);
          digitalWrite(THISSNARC_RFID_ENABLE_PIN, LOW);   

          // just in case.....
          Serial.flush();
          RFIDSerial.flush();

          // which door was this?
          last_door = THISSNARC;

          found = 1 ;

        }
        Serial.flush();
        bytesread = 0;
      }

    } //end if (RFIDSerial.available() > 0

      // web SERVER/LISTNER code:
    if (1 )  
    {

      // listen for incoming clients
      EthernetClient incomingclient = localserver.available();
      if (incomingclient) {
        // an http request ends with a blank line
        boolean currentLineIsBlank = true;
        while (incomingclient.connected()) {
          if (incomingclient.available()) {
            char c = incomingclient.read();
            //Serial.print(c); // debug to show client data on Serial console.
            // if you've gotten to the end of the line (received a newline
            // character) and the line is blank, the http request has ended,
            // so you can send a reply

            //read char by char HTTP request into 100 byte buffer, toss rest away!
            if (serverReadString.length() < 100) {
              //store characters to string
              serverReadString += c;
            }

            if (c == '\n' && currentLineIsBlank) {
              // send a standard http response header
              incomingclient.println(F("HTTP/1.1 200 OK"));
              incomingclient.println(F("Content-Type: text/html"));
              incomingclient.println();

              incomingclient.println(F("<HTML>"));
              incomingclient.println(F("<HEAD>"));
              incomingclient.println(F("<TITLE>Buzzs simple html page</TITLE>"));
              incomingclient.println(F("</HEAD>"));
              incomingclient.println(F("<BODY>"));

              // and output the links for the button on/off stuff:
              /*    incomingclient.println("<H2>Buzz's Analog data samples:</H2>");
               
               
               // output the value of each analog input pin
               for (int analogChannel = 0; analogChannel < 6; analogChannel++) {
               incomingclient.print("analog input ");
               incomingclient.print(analogChannel);
               incomingclient.print(" is ");
               incomingclient.print(analogRead(analogChannel));
               incomingclient.println("<br />");
               }
               */

              // and output the links for the button on/off stuff:
              incomingclient.println(F("<H2>Buzz's simple Override button:</H2>"));

              ///////////////////// control arduino pin
              if(serverReadString.indexOf("open") >0)//checks for on
              {
                digitalWrite(IOPIN, OPEN);    // set pin 4 high
                Serial.println(F("IOPIN open"));
              }
              if(serverReadString.indexOf("close") >0)//checks for off
              {
                digitalWrite(IOPIN, LOCKED);    // set pin 4 low
                Serial.println(F("IOPIN close"));
              }

              // report to user revised state of pin!
              if ( digitalRead(IOPIN) == LOCKED ) {
                incomingclient.println(F("<a href=\"/?open\">DOOR IS LOCKED. CLICK TO (HOLD) OPEN</a>"));
              }
              else {
                incomingclient.println(F("<a href=\"/?close\">DOOR IS UN-LOCKED. CLICK TO RE-LOCK.</a> <br>( you may also swipe any RFID key/fob/card to re-lock. )"));
              }
              //client.println("<IFRAME name=inlineframe style=\"display:none\" >");          
              //client.println("</IFRAME>");

              incomingclient.println(F("</BODY>"));
              incomingclient.println(F("</HTML>"));

              break;
            }
            if (c == '\n') {
              // you're starting a new line
              currentLineIsBlank = true;
            }
            else if (c != '\r') {
              // you've gotten a character on the current line
              currentLineIsBlank = false;
            }
          }
        }
        // give the web browser time to receive the data
        delay(1);
        // close the connection:
        incomingclient.stop();
        //clearing string for next read
        serverReadString="";
      }


    } // END WEB SERVER/LISTNER CODE:
    
    
    // network timeout checker.      
    // If we've not had a successful http request in the last 60 seconds, then try to make one
    // if we've still not had a sucessful http request in hte last 60 seconds, then reset the wiznet module, wait 60 secs and try again.
    if ( 1 ) {
      
      
    //  Serial.println(millis()/1000);
    //  Serial.println(lastConnectionTime/1000);
    //  Serial.println(pollingInterval);
      
      //XXXX
      if (millis() - lastConnectionTime > (long)(pollingInterval*1000) ) {
         Serial.println(F("network poll checking now.... "));

         // do the poll/check with a dummy key, for now.
        int serveraccess = INVALID; //
        serveraccess = send_to_server2("1234567890", 0); //log successes/failures/
        //etc, and return the permissions the server has.
        if ( serveraccess == -1 ) {
          Serial.println(F("network appears offline, forcing wiznet reset "));
          lastConnectionTime = millis();  // to make this code block not run again for at least 60 seconds, which is enough time ot bring wiznet online.
          wiznet_reset();
          delay(200);
          // re-init wiznet stackand whatnot.
          Ethernet.begin(mac, ip, gateway, subnet);
          localserver.begin();
          
        } else {
          Serial.println(F("network check appears OK. "));
        }
      } else {
       
        
      }
      
      
    } // end network timeout checker.

  } // end while ( found == 0 ) loop



  Serial.flush(); // just in case.

}

void led_flash(int whichled) {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis > interval) {
    // save the last time you blinked the LED
    previousMillis = currentMillis;   

    // if the LED is off turn it on and vice-versa:
    if (ledState == whichled)  {
      ledState = OFF;
      led_off();  //both off!
    }
    else {
      ledState = whichled;
      led_on(whichled);
    }
  }
}

void led_on(int which){
#ifndef SNARCPLUS
  if ( which == RED ) { //onboard RED
    digitalWrite(LED_PIN1, LOW);
    digitalWrite(LED_PIN2, HIGH);
  }
  else { //onboard GREEN
    digitalWrite(LED_PIN1, HIGH);
    digitalWrite(LED_PIN2, LOW);
  }
#endif
  digitalWrite(LEDYELLOW, HIGH);
} // offboard YELLOW
void led_off(){
#ifndef SNARCPLUS
  digitalWrite(LED_PIN1, LOW);
  digitalWrite(LED_PIN2, LOW);
#endif
  digitalWrite(LEDYELLOW, LOW);
}


/////////////////////FROM PROGRAMMER CODE!
// Upload rfid codes into EEPROM

//#include <EEPROM.h>


// currently MAX of approx 50?
// SIZE data; each is 11 bytes, and there are 50, so memory used is 550-600 bytes
// ... which would be better in the 4K EEPROM, or the 128K PROGMEM , but in 8K SRAM on a Mega it's OK too.
// IF YOU WANT TO HARDCODE ALL YOUR RFID CARDS IN THE CODE, STORE THEM HERE, and use the "w" command in programming mode!
// ( not recommended for poduction, there are better and easier ways to do it!  )
//
char * codeListBoth[]  =
{
  "1234567890", // sample/ demo key.
  "\0"  //EOF
};



void writeCode(int address, char * code, int accessLevel)
{
#ifndef SNARCPLUS
  digitalWrite(LED_PIN1, HIGH);
  digitalWrite(LED_PIN2, LOW);
#endif
  //delay(10);

  Serial.print(F("Writing to address "));
  Serial.println(address);

  Serial.print(F("Access "));
  Serial.println(accessLevel);
  EEPROM.write(EEPROM_CARDS_START_ADDRESS + address*11,accessLevel);

  Serial.print(F("Code "));
  for(int i = 1; i<11; i++)
  {
    EEPROM.write(EEPROM_CARDS_START_ADDRESS + address * 11 + i, code[i-1]);
    Serial.print(code[i-1]);
  }
  Serial.println("");
#ifndef SNARCPLUS
  digitalWrite(LED_PIN1, LOW);
  digitalWrite(LED_PIN2, LOW);
#endif
}

// scan thru eeprom looking for code, and delete it if it exists, moving rest of list down one position.
void delete_eeprom_code( ) {

  // get a tag from the user to delete
  listen_for_codes(); // blocking , waits for user input ....  result is in last_code and last_door

  //int address = 0; // eeprom address pointer
  int result = 0;  
  char codeout[12]; // holder for code strings as we output them

  int access = matchRfid(last_code) & 0xF; // searches for and updates last_found_address to point to tag, if it exists. XXXXX
  if ( ( access != INVALID ) && (last_found_address > 0 )) { // found a valid tag at a valid location TODO use >= ?

    Serial.print(F("Found valid code to *DELETE* at EEPROM location:"));
    Serial.println(last_found_address);

  }
  else {
    Serial.print(F("Nothing found to delete, no action done."));  
    return;  
  }

  // erase  the one at this location:  , just to be sure
  //writeCode( last_found_address, invalidCode, INVALID ) ; // termination code after loast code is ESSENTIAL to know where end of list is.

  // start from this address, and rewrite each code at the next earlier position.....

  // temporarily....
  // we point address at the location we are *reading* from AND
  // we point last_found_address at the location we are writing *TO*
  // when finished we
  //address =
  last_found_address+=1;

  // we go till we get an INVALID tag to label the end of them.
  // "result"  = the access level for the user!
  while((result = EEPROM.read(EEPROM_CARDS_START_ADDRESS + last_found_address*11)) != INVALID)
  {
    for(int i=0; i<10; i++)
    {
      codeout[i] = EEPROM.read(EEPROM_CARDS_START_ADDRESS + last_found_address*11+i+1);
    }
    codeout[10] = '\0'; //end of string

    // write the current code to the USB port for the user!
    Serial.print(F("moving code down: "));
    Serial.println(codeout);

    // then finally write it all back:
    writeCode( last_found_address-1, codeout, result ) ;

    last_found_address++;
    //address++;
  }

  //last_address = last_found_address;  // keep this uptodate too!

  // since the address at last_found_address-1 has been copied down to last_found_address-2
  // we put the terminator at last_found_address-1
  Serial.print(F("writing terminator at address:"));
  Serial.println(last_found_address-1);

  writeCode( last_found_address-1, invalidCode, INVALID ) ; // termination code after loast code is ESSENTIAL to know where end of list is.


  // }

}

// write a single code (  and it's permissions bitmask )
void write_next_code_to_eeprom(char * code, int dooraccess ){

  // first locate the end of the EEPROM list. Updates last_address variable to correct value without any output.
  find_last_eeprom_code();  

  writeCode( last_address, code, dooraccess ) ;
  last_address++;
  writeCode( last_address, invalidCode, INVALID ) ; // termination code after loast code is ESSENTIAL to know where end of list is.
}

void write_codes_to_eeprom()
{
  int address = 0;

  int codeIdx = 0;
  while(codeListBoth[codeIdx][0]!=0)
  {
    writeCode(address, codeListBoth[codeIdx], THISSNARC);
    address++;
    codeIdx++;
  }

  // char invalidCode[] = "          ";
  writeCode(address, invalidCode, INVALID);
  Serial.println(F("Finished"));

  last_address = address;
}

// wipes eeprom, and initialised it for codes bbeing programmed later
void init_eeprom()
{
  // wipe everything!  
  for ( int a = 0 ; a < 1024 ; a++ ) {  
    EEPROM.write(a, 255);  // writing a 255 to every EPROM address, not a zero, just cause.
  }

  // write empty name to name field
  for (byte i = 0; i <= DEVICE_NAME_MAX_LENGTH; i++) // the "<=" is to accomodate the extra null character that's needed to terminate the string.
  {
    device_name[i] = 0;
  }
  save_device_name();


  // write a zero address
  int address = 0;

  writeCode(address, invalidCode, INVALID);
  Serial.println(F("Init Finished"));

  last_address = address;
}


// MAC address stuff
void print_mac_address ()
{
  if (mac[0] < 16)
  {
    Serial.print('0');
  }
  Serial.print(mac[0], HEX);
  Serial.print(':');
  if (mac[1] < 16)
  {
    Serial.print('0');
  }
  Serial.print(mac[1], HEX);
  Serial.print(':');
  if (mac[2] < 16)
  {
    Serial.print('0');
  }
  Serial.print(mac[2], HEX);
  Serial.print(':');
  if (mac[3] < 16)
  {
    Serial.print('0');
  }
  Serial.print(mac[3], HEX);
  Serial.print(':');
  if (mac[4] < 16)
  {
    Serial.print('0');
  }
  Serial.print(mac[4], HEX);
  Serial.print(':');
  if (mac[5] < 16)
  {
    Serial.print('0');
  }
  Serial.print(mac[5], HEX);
}

void save_mac_address ()
{
  for (byte i = 0; i < 6; i++)
  {
    EEPROM.write(MAC_START_ADDRESS + i, mac[i]);
  }
}

void generate_random_mac_address ()
{
  set_mac_address(random(0, 255), random(0, 255), random(0, 255), random(0, 255), random(0, 255), random(0, 255));
}

void get_mac_address ()
{
  bool hasNeverBeenSaved = true;
  for (byte i = 0; i < 6; i++)
  {
    mac[i] = EEPROM.read(MAC_START_ADDRESS + i);
    if (mac[i] != 255)  // If a EEPROM cell has never been written to before, it returns 255 (accordoing to Arduino doco)
    {
      hasNeverBeenSaved = false;
    }
  }
  if (hasNeverBeenSaved)
  {
    Serial.print(F("Generating random MAC address: "));
    generate_random_mac_address();
    print_mac_address();
    Serial.println();
  }
}

void set_mac_address (byte octet0, byte octet1, byte octet2, byte octet3, byte octet4, byte octet5)
{
  mac[0] = octet0;
  mac[1] = octet1;
  mac[2] = octet2;
  mac[3] = octet3;
  mac[4] = octet4;
  mac[5] = octet5;
  save_mac_address();
}

/*
Listens for new MAC address over serial or creates random. Warning... here be dragons...
 */
void listen_for_new_mac_address () // blocking operation
{

  Serial.print(F("Current MAC address is "));
  print_mac_address();
  Serial.println();

  Serial.println(F("Enter new MAC address 01:23:45:67:89:AB (enter a newline for random MAC address):"));
  byte b;
  byte bi = 0; // Index for octetB
  byte index = 0;
  byte acc = 0;
  byte octetB[] = {
    0, 0  };
  char macOctets[] = {
    0, 0, 0, 0, 0, 0  };
  clear_serial_buffer();
  while (true)
  {
    if (Serial.available())
    {
      b = Serial.read();
      if (b == 10 || b == 13)
      {
        if (index == 0)
        {
          // Nothing entered, use random MAC
          generate_random_mac_address();
          Serial.print(F("New MAC: "));
          print_mac_address();
          Serial.println();
        }
        else if (index == 6)
        {
          // nothing. Should be valid.
        }
        else
        {
          Serial.println(F("Invalid mac address!"));
        }
        break;
      }
      else if ((b >= 48 && b <= 58) || (b >= 97 && b <= 102) || (b >= 65 && b <= 70)) // 0-9: || a-z || A-Z
      {
        if (b >= 48 && b <= 57) // 0-9
        {
          acc *= 16;
          acc += (b - 48);
          bi++;
        }
        else if (b != 58) // (a-f || A-F)
        {
          if (b >= 97) // convert to uppercase
          {
            b -= 32;
          }
          acc *= 16;
          acc += (b - 55);
          bi++;
        }
        if (bi == 2 && index == 5)
        {
          b = 58; // Force b = 58 so that the final octet is added
        }
        else if (bi > 2)
        {
          // Invalid
          Serial.println(F("Invalid mac address!"));
          break;
        }

        if (b == 58) // :
        {
          if (bi == 2)
          {
            // Set octet & reset counters
            macOctets[index++] = acc;
            acc = 0;
            bi = 0;
            if (index == 6)
            {
              // Done. Save
              set_mac_address(macOctets[0], macOctets[1], macOctets[2], macOctets[3], macOctets[4], macOctets[5]);
              print_mac_address();
              Serial.println();
              break;
            }
          }
          else if (bi > 2)
          {
            Serial.println(F("Invalid mac address!"));
          }
        }
      }
      else
      {
        // Invalid
        Serial.println(F("Invalid mac address!"));
        break;
      }
    }
  }
  clear_serial_buffer();
}

// Device name
void print_device_name()
{
  Serial.print(device_name);
}

void save_device_name()
{
  for (byte i = 0; i <= DEVICE_NAME_MAX_LENGTH; i++) // the "<=" is to accomodate the extra null character that's needed to terminate the string.
  {
    EEPROM.write(DEVICE_NAME_ADDRESS + i, (byte)device_name[i]);
  }
}

void load_device_name()
{
  for (byte i = 0; i <= DEVICE_NAME_MAX_LENGTH; i++) // the "<=" is to accomodate the extra null character that's needed to terminate the string.
  {
    device_name[i] = (char)EEPROM.read(DEVICE_NAME_ADDRESS + i);
  }
}

void listen_for_device_name()
{
  Serial.print(F("Current device name is: "));
  print_device_name();
  Serial.println();

  Serial.print(F("Enter new device name (max "));
  Serial.print(DEVICE_NAME_MAX_LENGTH);
  Serial.println(F(" characters):"));

  serial_recieve_index = 0;
  clear_serial_buffer();

  bool keepReading = true;
  while (keepReading)
  {
    while (Serial.available())
    {
      if (Serial.peek() == 13 || Serial.peek() == 10)
      {
        // new line. End entry
        keepReading = false;
        break;
      }
      serial_recieve_data[serial_recieve_index++] = Serial.read();
      if (serial_recieve_index >= SERIAL_RECIEVE_BUFFER_LENGTH)
      {
        // max length. End entry
        keepReading = false;
        break;
      }
    }
  }
  clear_serial_buffer();

  if (serial_recieve_index == 0)
  {
    // Empty, do not save.
    Serial.println(F("No input detected. No changes made."));
  }
  else
  {
    // Echo & save new name
    Serial.print(F("Device name set to: "));
    byte i;
    for (i = 0; i < serial_recieve_index; i++)
    {
      device_name[i] = (char)serial_recieve_data[i];
      Serial.print(device_name[i]);
    }
    // Fill rest of device name array with null chars
    for (; i <= DEVICE_NAME_MAX_LENGTH; i++)
    {
      device_name[i] = '\0';
    }
    Serial.println();
    save_device_name();
  }
}

// Door egres functionality:
#ifdef ENABLE_ESTOP_AS_EGRESS_BUTTON
void estop_interrupt_handler()
{

  int x1 = 0;
  int x2 = 0;
  // read the IO twice as crude debounce:
  x1 = digitalRead(ESTOP_INTERRUPT_PIN) ;
  delay(100);
  x2 = digitalRead(ESTOP_INTERRUPT_PIN) ;

  if ( x1 && x2 ) {
    egressrequest = true;
  }

}
#endif

// ESTOP functionality
#ifdef ENABLE_ESTOP_AS_SAFETY_DEVICE
void broadcast_estop()
{
  // TODO: Implement
}

void estop_interrupt_handler()
{
  actuator_off();
  lockdown = true;
}


void estop_lockdown()
{
  detachInterrupt(ESTOP_INTERRUPT_PIN); // disable estop interrupt so repeated estop presses won't cause weird/mistimed LED blinks.
  Serial.println(F("LOCKED DOWN!!!"));
  broadcast_estop();
  interval = 250; // Make flash faster.
  led_on(RED);
  while (true)
  {
    // Lockdown loop
    led_flash(RED);
  }
  attachInterrupt(ESTOP_INTERRUPT_PIN, estop_interrupt_handler, CHANGE);
}
#endif

