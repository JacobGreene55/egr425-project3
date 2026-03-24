#include <BLEDevice.h>
#include <BLE2902.h>
#include <M5Core2.h>
#include "Adafruit_seesaw.h"

///////////////////////////////////////////////////////////////
// Variables
///////////////////////////////////////////////////////////////
static BLERemoteCharacteristic *bleRemoteCharacteristic_S;
static BLERemoteCharacteristic *bleRemoteCharacteristic_C;
static BLEAdvertisedDevice *bleRemoteServer;
static boolean doConnect = false;
static boolean doScan = false;
bool deviceConnected = false;

// See the following for generating UUIDs: https://www.uuidgenerator.net/
static BLEUUID SERVICE_UUID("74be116b-2158-4c76-a579-0980aaa415d3");
static BLEUUID CHARACTERISTIC_UUID_S("7dcf8cb1-a9b1-44e6-bed5-3c2ffe02bb07"); // Server Characteristic
static BLEUUID CHARACTERISTIC_UUID_C("41755070-8e22-4588-bf9b-80dd3ac3f42f"); 

// BLE Broadcast Name
static String BLE_BROADCAST_NAME = "The bruhs M5Core2";

// controller declarations
Adafruit_seesaw seeSaw = Adafruit_seesaw(&Wire);
#define BUTTON_X      6
#define BUTTON_Y      2
#define BUTTON_A      5 
#define BUTTON_B      1  

#define JOY1_X        14
#define JOY1_Y        15
#define BUTTON_SELECT 0
#define BUTTON_START  16

uint32_t button_mask = (1UL << BUTTON_X) | (1UL << BUTTON_Y) | (1UL << BUTTON_START)
                      | (1UL << BUTTON_A) | (1UL << BUTTON_B) | (1UL << BUTTON_SELECT);
uint32_t buttons;
// cosnt vars
const int screenWidth = 320;
const int screenHeight = 240;

// global vars
unsigned long lastBLEUpdate = 0;
const unsigned long BLE_UPDATE_INTERVAL = 100; // milliseconds

int tankColor = {RED, BLUE, GREEN, YELLOW};
String tankName = {PREDATOR, WRAITH, SCORPION, ABRAMS};

String clientPlayerMove = "None";
bool begin = true;
bool playerMoved = false;

int playerSize = 4;
short int player1Speed = 1;
int player1XPos = screenWidth/2 - 10; // Starting position
int player1YPos = screenHeight/2;
int player2XPos = screenWidth/2 + 10; // Starting position
int player2YPos = screenHeight/2;


///////////////////////////////////////////////////////////////
// Forward Declarations
///////////////////////////////////////////////////////////////
void drawScreenTextWithBackground(String text, int backgroundColor);
void setupController();
bool connectToServer();
void tankSelectionScreen();
void drawGameScreen(bool begin);
void updatePlayerPosition();
void sendClientPosition();
void updateServerPosition();
void checkController();
void checkSpeed();
void checkWarp();
void checkBoarderLimit();
void checkCollision();
void endScreen();

///////////////////////////////////////////////////////////////
// BLE Client Callback Methods
// This method is called when the server that this client is
// connected to NOTIFIES this client (or any client listening)
// that it has changed the remote characteristic
///////////////////////////////////////////////////////////////
static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
    Serial.printf("Notify callback for characteristic %s of data length %d\n", pBLERemoteCharacteristic->getUUID().toString().c_str(), length);
    Serial.printf("\tData: %s\n", (char *)pData);
    
    // Parse server position from the notification
    String str = String((char*)pData);
    if (str.indexOf("-") != -1) {
        player2XPos = str.substring(0, str.indexOf("-")).toInt();
        player2YPos = str.substring(str.indexOf("-") + 1).toInt();
        Serial.printf("Updated player2 position: X=%d, Y=%d\n", player2XPos, player2YPos);
    }
}

///////////////////////////////////////////////////////////////
// BLE Server Callback Method
// These methods are called upon connection and disconnection
// to BLE service.
///////////////////////////////////////////////////////////////
class MyClientCallback : public BLEClientCallbacks
{
    void onConnect(BLEClient *pclient)
    {
        deviceConnected = true;
        Serial.println("Device connected...");
    }

    void onDisconnect(BLEClient *pclient)
    {
        deviceConnected = false;
        Serial.println("Device disconnected...");
        //drawScreenTextWithBackground("LOST connection to device.\n\nAttempting re-connection...", TFT_RED);
    }
};

///////////////////////////////////////////////////////////////
// Method is called to connect to server
///////////////////////////////////////////////////////////////
bool connectToServer()
{
    // Create the client
    Serial.printf("Forming a connection to %s\n", bleRemoteServer->getName().c_str());
    BLEClient *bleClient = BLEDevice::createClient();
    bleClient->setClientCallbacks(new MyClientCallback());
    Serial.println("\tClient connected");

    // Connect to the remote BLE Server.
    if (!bleClient->connect(bleRemoteServer))
        Serial.printf("FAILED to connect to server (%s)\n", bleRemoteServer->getName().c_str());
    Serial.printf("\tConnected to server (%s)\n", bleRemoteServer->getName().c_str());

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService *bleRemoteService = bleClient->getService(SERVICE_UUID);
    if (bleRemoteService == nullptr) {
        Serial.printf("Failed to find our service UUID: %s\n", SERVICE_UUID.toString().c_str());
        bleClient->disconnect();
        return false;
    }
    Serial.printf("\tFound our service UUID: %s\n", SERVICE_UUID.toString().c_str());

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    bleRemoteCharacteristic_S = bleRemoteService->getCharacteristic(CHARACTERISTIC_UUID_S);
    bleRemoteCharacteristic_C = bleRemoteService->getCharacteristic(CHARACTERISTIC_UUID_C);
    if (bleRemoteCharacteristic_S == nullptr) {
        Serial.printf("Failed to find our characteristic Server UUID: %s\n", CHARACTERISTIC_UUID_S.toString().c_str());
        bleClient->disconnect();
        return false;
    }
    if (bleRemoteCharacteristic_C == nullptr) {
        Serial.printf("Failed to find our characteristic Client UUID: %s\n", CHARACTERISTIC_UUID_C.toString().c_str());
        bleClient->disconnect();
        return false;
    }
    Serial.printf("\tFound our characteristic UUID: %s\n", CHARACTERISTIC_UUID_S.toString().c_str());

    // Read the value of the characteristic
    if (bleRemoteCharacteristic_S->canRead()) {
        std::string value = bleRemoteCharacteristic_S->readValue();
        Serial.printf("The characteristic value was: %s", value.c_str());
        drawScreenTextWithBackground("Initial characteristic value read from server:\n\n" + String(value.c_str()), TFT_GREEN);
    }
    
    // Check if server's characteristic can notify client of changes and register to listen if so
    if (bleRemoteCharacteristic_S->canNotify())
        bleRemoteCharacteristic_S->registerForNotify(notifyCallback);

    //deviceConnected = true;
    return true;
}

///////////////////////////////////////////////////////////////
// Scan for BLE servers and find the first one that advertises
// the service we are looking for.
///////////////////////////////////////////////////////////////
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
    /**
     * Called for each advertising BLE server.
     */
    void onResult(BLEAdvertisedDevice advertisedDevice)
    {
        // Print device found
        Serial.print("BLE Advertised Device found:");
        Serial.printf("\tName: %s\n", advertisedDevice.getName().c_str());


        // More debugging print
        // Serial.printf("\tAddress: %s\n", advertisedDevice.getAddress().toString().c_str());
        // Serial.printf("\tHas a ServiceUUID: %s\n", advertisedDevice.haveServiceUUID() ? "True" : "False");
        // for (int i = 0; i < advertisedDevice.getServiceUUIDCount(); i++) {
        //    Serial.printf("\t\t%s\n", advertisedDevice.getServiceUUID(i).toString().c_str());
        // }
        // Serial.printf("\tHas our service: %s\n\n", advertisedDevice.isAdvertisingService(SERVICE_UUID) ? "True" : "False");
        
        // We have found a device, let us now see if it contains the service we are looking for.
        if (advertisedDevice.haveServiceUUID() && 
                advertisedDevice.isAdvertisingService(SERVICE_UUID) && 
                advertisedDevice.getName() == BLE_BROADCAST_NAME.c_str()) {
            BLEDevice::getScan()->stop();
            bleRemoteServer = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
            doScan = true;
        }

    }     
};        

///////////////////////////////////////////////////////////////
// Put your setup code here, to run once
///////////////////////////////////////////////////////////////
void setup()
{
    // Init device
    M5.begin();
    M5.Lcd.setTextSize(3);
    drawScreenTextWithBackground("Scanning for BLE server...", TFT_BLUE);

    BLEDevice::init("");

    // Retrieve a Scanner and set the callback we want to use to be informed when we
    // have detected a new device.  Specify that we want active scanning and start the
    // scan to run indefinitely (by passing in 0 for the "duration")
    BLEScan *pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(0, false);

    setupController();
}

///////////////////////////////////////////////////////////////
// Put your main code here, to run repeatedly
///////////////////////////////////////////////////////////////
void loop()
{
    doConnect = true; //Debug line
    
    // If the flag "doConnect" is true then we have scanned for and found the desired
    // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
    // connected we set the connected flag to be false.
    if (doConnect == true)
    {
        if (connectToServer()) {
            Serial.println("We are now connected to the BLE Server.");
            if (begin) {
              drawScreenTextWithBackground("Connected to BLE server: " + String(bleRemoteServer->getName().c_str()), TFT_GREEN);
              delay(3000);
              begin = false;
            }
            doConnect = false;
            //drawGameScreen(begin);
        }
        else {
            Serial.println("We have failed to connect to the server; there is nothin more we will do.");
            drawScreenTextWithBackground("FAILED to connect to BLE server: " + String(bleRemoteServer->getName().c_str()), TFT_GREEN);
            delay(3000);
        }
    }

    // If we are connected to a peer BLE Server, update the characteristic each time we are reached
    // with the current time since boot.
    if (deviceConnected)
    {
        if (begin) {
            tankSelectionScreen();
        }
        checkController();
        updatePlayerPosition();
        drawGameScreen(begin);
        checkCollision();
    }
    else if (doScan) {
        drawScreenTextWithBackground("Disconnected....re-scanning for BLE server...", TFT_ORANGE);
        BLEDevice::getScan()->start(0); // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
    }
    delay(100); // Delay a second between loops.
}

///////////////////////////////////////////////////////////////
// Colors the background and then writes the text on top
///////////////////////////////////////////////////////////////
void drawScreenTextWithBackground(String text, int backgroundColor) {
    M5.Lcd.fillScreen(backgroundColor);
    M5.Lcd.setCursor(0,0);
    M5.Lcd.println(text);
}

// Controller functions
void setupController() {
  if(!seeSaw.begin(0x50)){
    Serial.println("ERROR! seesaw not found");
    delay(1000);
    setupController();
  }
  Serial.println("seesaw started");
  uint32_t version = ((seeSaw.getVersion() >> 16) & 0xFFFF);
  if (version != 5743) {
    Serial.print("Wrong firmware loaded? ");
    Serial.println(version);
    while(1) delay(10);
  }
  Serial.println("Found Product 5743");
  seeSaw.pinModeBulk(button_mask, INPUT_PULLUP);
}

void checkController() {
  buttons = seeSaw.digitalReadBulk(button_mask);
  bool a = seeSaw.digitalRead(BUTTON_A);
  bool b = seeSaw.digitalRead(BUTTON_B);
  bool x = seeSaw.digitalRead(BUTTON_X);
  bool y = seeSaw.digitalRead(BUTTON_Y);
  int xAxis = seeSaw.analogRead(JOY1_X);
  int yAxis = seeSaw.analogRead(JOY1_Y);
  Serial.println("X:" + String(xAxis) + "  Y:" + String(yAxis));  

  if (x) {
    clientPlayerMove = "A";
  } else if (b) {
    clientPlayerMove = "B";
  } else if (y) {
    clientPlayerMove = "Y";
  } else if (x) {
    clientPlayerMove = "X";
  }

  if (yAxis <= 250) {
    clientPlayerMove = "Up";
    playerMoved = true;
  } else if (yAxis >= 770) {
    clientPlayerMove = "Down";
    playerMoved = true;
  } else if (xAxis <= 250) {
    clientPlayerMove = "Right";
    playerMoved = true;
  } else if (xAxis >= 770) {
    clientPlayerMove = "Left";
    playerMoved = true;
  } else {
    clientPlayerMove = "None";
  }

  checkSpeed();
  checkWarp();
}

void checkSpeed() {
  if (! (buttons & (1UL << BUTTON_START))) {
    //Serial.println("Start Button Pressed"); //Debugging
    player1Speed++;
    if (player1Speed > 6) player1Speed = 2; // Cap speed at 6
  }
}

void checkWarp() {
  if (! (buttons & (1UL << BUTTON_SELECT))) {
    //Serial.println("Select Button Pressed"); //Debugging
    player1XPos = random(0, screenWidth - playerSize);
    player1YPos = random(0, screenHeight - playerSize);
  }
}

// Game functions
void tankSelectionScreen() {
    while (clientPlayerMove != "A") {
        checkController();
        M5.Lcd.fillScreen(WHITE);
        M5.Lcd.setTextColor(TFT_BROWN);
        M5.Lcd.setTextSize(3);
        M5.Lcd.setCursor(screenWidth/2 - 100, screenHeight/2 - 30);
        M5.Lcd.print("TANK SELECTION");

        M5.Lcd.fillCircle(screenWidth/2, screenHeight/3, 40, tankColor);
        M5.Lcd.drawCircle(screenWidth/2, screenHeight/3, 40, BLACK);
        
        M5.Lcd.setTextSize(2);
        M5.Lcd.print(tankName);
        delay(100);
    }
}

void transitionScreen() {
    M5.Aux
}

void drawGameScreen(bool begin) {
  if (begin) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.fillRect(player1XPos, player1YPos, playerSize, playerSize, BLUE);
    M5.Lcd.fillRect(player2XPos, player2YPos, playerSize, playerSize, RED);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(screenWidth/2 - 50, screenHeight/2 - 30);
    M5.Lcd.println("BEGIN!");
    begin = false;
  } else {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.fillRect(player1XPos, player1YPos, playerSize, playerSize, BLUE);
    M5.Lcd.fillRect(player2XPos, player2YPos, playerSize, playerSize, RED);
  }
}

void updatePlayerPosition() {
  if (clientPlayerMove == "Up") {
    player1YPos -= 4*player1Speed;
  } else if (clientPlayerMove == "Down") {
    player1YPos += 4*player1Speed;
  } else if (clientPlayerMove == "Left") {
    player1XPos -= 4*player1Speed;
  } else if (clientPlayerMove == "Right") {
    player1XPos += 4*player1Speed;
  } else if (clientPlayerMove == "None") {
    playerMoved = false; // No movement, reset flag
  }
  Serial.println("Player move: " + clientPlayerMove);
  checkBoarderLimit();

  if (millis() - lastBLEUpdate >= BLE_UPDATE_INTERVAL) {
    sendClientPosition();
    updateServerPosition();
    lastBLEUpdate = millis();
  }
}

void checkBoarderLimit() {
  if (player1XPos < 0) player1XPos = 0;
  if (player1XPos > screenWidth - playerSize) player1XPos = screenWidth - playerSize;
  if (player1YPos < 0) player1YPos = 0;
  if (player1YPos > screenHeight - playerSize) player1YPos = screenHeight - playerSize;
}

void checkCollision() {
  if (player1XPos < player2XPos + 4 &&
      player1XPos + 4*player1Speed > player2XPos &&
      player1YPos < player2YPos + 4 &&
      player1YPos + 4*player1Speed > player2YPos) {
    //Serial.println("Collision!"); //Debugging
    // End Game
    endScreen();
  }
}

void endScreen() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(screenWidth/2 - 60, screenHeight/2 - 10);
  M5.Lcd.print("Game Over!");
  M5.Lcd.setCursor(screenWidth/2 - 125, screenHeight/2 + 10);
  M5.Lcd.printf("Time Played: %.2f \nseconds", (millis() - 1000) / 1000.0);
  while(1) delay(5000);
}

// Bluetooth Server functions
void sendClientPosition() {
  if (bleRemoteCharacteristic_C == nullptr) {
    Serial.println("ERROR: bleRemoteCharacteristic_C is null");
    return;
  }
  
  if (!deviceConnected) {
    Serial.println("Device not connected, cannot send position");
    return;
  }
  
  if (!bleRemoteCharacteristic_C->canWrite()) {
    Serial.println("ERROR: Cannot write to characteristic");
    return;
  }
  
  Serial.println("Updating server with new position...");
  String position = String(player1XPos) + "-" + String(player1YPos);
  Serial.println("Sending position to server: " + position);
  
  // Use writeValue with response=false to avoid blocking
  bleRemoteCharacteristic_C->writeValue((uint8_t*)position.c_str(), position.length(), false);
  Serial.println("Position sent successfully");
}

void updateServerPosition() {
  // Don't actively read - this causes blocking
  // Instead, rely on the notification callback to update player2 position
  // The server will push updates via the notify callback
  return;
}