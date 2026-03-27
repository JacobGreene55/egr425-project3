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

bool begin = true;
//server controls the game state
enum GameState{
  CONNECTING,
  CUSTOMIZE_PLAYER,
  PLAYER1_TURN,
  PLAYER2_TURN,
  GAME_OVER
};
// GameState gameState = CONNECTING; //startup default state
GameState serverGameState = CUSTOMIZE_PLAYER; //testing default state
bool player2Ready = false;
bool player1Ready = true; //debug change to false after

String clientPlayerMove = "";
bool playerMoved = false;

int tankColor[4] = {MAROON, NAVY, GREEN, YELLOW};
String tankName[4] = {"PREDATOR", "WRAITH", "SCORPION", "ABRAMS"};
int tank1 = 0;
int tank2 = 0;

int floorHeight = 175;
uint16_t skyColor = CYAN;
uint16_t floorColor = 0;

bool updateUI = true;
bool player1Damaged = false;
bool player2Damaged = false;
bool player1Turn = true;
int healthBarWidth = 100;
int healthBarHeight = 10;
int healthBarY = 30;
int healthBarX1 = 20;
int healthBarX2 = screenWidth - healthBarWidth - 20;
int itemSquareSize = 40;
int itemY = screenHeight - itemSquareSize - 10;
int itemX1 = itemSquareSize - 20;
int itemX2 = screenWidth - itemSquareSize - 20;

enum shotType{NORMAL, SPLIT, STRAIFE_RUN};
shotType tank1ShotType = NORMAL;
shotType tank2ShotType = NORMAL;
int tankPos1 = 50;
int tankPos2 = 250;
int barrelAngle1 = 0; //degrees 0 is on the right like cartesian plane
int barrelAngle2 = 0; //degrees 0 is on the right like cartesian plane
int tankHealth1 = 100;
int tankHealth2 = 100;
enum Power{
  SMALL,
  MEDIUM,
  LARGE
};
Power tankPower1 = SMALL;
Power tankPower2 = SMALL;

const int smallSizeR = 4;
const int medSizeR = 8;
const int largeSizeR = 12;

const float gravity = 0.5;
const float projOffset = 20;
float projX = 0;
float projY = 300; //init beyond screen
int projSizeR = 0;
float vx = 0;
float vy = 0;
float speed = 0;

const int tankSizeX = 25;
const int tankSizeY = 12;
const int tankSizeR = 7;
const int barrelSizeL = 15;
const int barrelSizeH = 3;

int tankBodyY = floorHeight - tankSizeY;
int tankBarrelY = tankBodyY + barrelSizeH/2;

bool whoWon = false;

///////////////////////////////////////////////////////////////
// Forward Declarations
///////////////////////////////////////////////////////////////
void drawScreenTextWithBackground(String text, int backgroundColor);
bool connectToServer();

void setupController();

void tankSelectionScreen();

void transitionScreenStart();
void transitionScreenEnd();

void drawMap();
void drawTank(uint16_t color,int xPos,int angle);
void drawHealth(uint16_t color1, uint16_t color2, int hp1, int hp2);
void updatePlayerUI();

// Tank Control Functions //
//controller input
void checkController();

//tank control functions
int controlAngle(int angle);
int controlMove(int xPos);

//projectile/shoot functions
Power selectPower(Power prevPower);
void shoot(uint16_t color, int xPos, int angle, Power power);
void projectileMotion(uint16_t color);
void checkCollision(Power power);

//tank location
void checkBoarderLimit();
void checkCollision();

// UI Draw Functions //
//health function
void drawHealth();
void refreshDrawTank();
void drawPlayerTitles();
void drawItems();

// Server Communication Functions //
void sendClientPosition();
void updateServerPosition();


void drawGameOver(bool whoWon);

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
        tankPos2 = str.substring(0, str.indexOf("-")).toInt();
        // player2YPos = str.substring(str.indexOf("-") + 1).toInt();
        Serial.printf("Updated player2 position: X=%d", tankPos2);
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
    // BLEScan *pBLEScan = BLEDevice::getScan();
    // pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    // pBLEScan->setInterval(1349);
    // pBLEScan->setWindow(449);
    // pBLEScan->setActiveScan(true);
    // pBLEScan->start(0, false);

    setupController();
}

///////////////////////////////////////////////////////////////
// Put your main code here, to run repeatedly
///////////////////////////////////////////////////////////////
void loop()
{
    // If the flag "doConnect" is true then we have scanned for and found the desired
    // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
    // connected we set the connected flag to be false.
    // if (doConnect == true)
    // {
    //     if (connectToServer()) {
    //         Serial.println("We are now connected to the BLE Server.");
    //         if (begin) {
    //           drawScreenTextWithBackground("Connected to BLE server: " + String(bleRemoteServer->getName().c_str()), TFT_GREEN);
    //           delay(3000);
    //           begin = false;
    //         }
    //         doConnect = false;
    //         //drawMap(begin);
    //     }
    //     else {
    //         Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    //         drawScreenTextWithBackground("FAILED to connect to BLE server: " + String(bleRemoteServer->getName().c_str()), TFT_GREEN);
    //         delay(3000);
    //     }
    // }

    deviceConnected = true; // Debug line

    // If we are connected to a peer BLE Server, update the characteristic each time we are reached
    // with the current time since boot.
    while (deviceConnected)
    {
      switch (serverGameState) {
        case CONNECTING:
          drawScreenTextWithBackground("Connected to BLE server: " + String(bleRemoteServer->getName().c_str()), TFT_GREEN);
          delay(3000); // Debug lines <  ^
          serverGameState = CUSTOMIZE_PLAYER;// dubug line, replace with reading the server's game state
          break;
        case CUSTOMIZE_PLAYER:
          tankSelectionScreen();
          if (player2Ready && player1Ready) {
            serverGameState = PLAYER1_TURN;// dubug line, replace with reading the server's game state
            player2Ready = false;
            transitionScreenStart();
            drawMap();
            transitionScreenEnd();
          }
          // Serial.println(serverGameState);
          break;
        case PLAYER1_TURN:
          player1Turn = true;
          //recieve tank/power over bluetooth from server, update player 1's stuff
          //TODO: Finish function
          //spawn projectile and animate, pause game until projectile strikes the ground
          //shoot(tankColor[tank1], tankPos1, barrelAngle1, tankPower1);
          serverGameState = PLAYER2_TURN;
          player1Turn = false;
          // Handle player 1's turn
          break;
        case PLAYER2_TURN:
          updatePlayerUI();
          drawTank(tankColor[tank1], tankPos1, barrelAngle1);
          drawTank(tankColor[tank2], tankPos2, barrelAngle2);

          tankPos2 = controlMove(tankPos2);

          barrelAngle2 = controlAngle(barrelAngle2);

          drawTank(tankColor[tank1], tankPos1, barrelAngle1);
          drawTank(tankColor[tank2], tankPos2, barrelAngle2);

          tankPower2 = selectPower(tankPower2);

          shoot(tankColor[tank2], tankPos2, barrelAngle2, tankPower2);
          // Handle player 2's turn
          break;
        case GAME_OVER:
          drawGameOver(whoWon);
          break;
      } 
    }
    if (doScan) {
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

/////////////////////////////////////////////////////////////////////////
//tank control functions
/////////////////////////////////////////////////////////////////////////

int controlAngle(int angle){
  //check joystick up and down buttons
  if (!seeSaw.digitalRead(BUTTON_X)) {
    Serial.println("X Button Pressed, ROTATE LEFT"); //Debugging
    playerMoved = true;
    if(angle < 180){
      angle += 3; //rotate left
    }
  } else if(!seeSaw.digitalRead(BUTTON_B)){
    Serial.println("B Button Pressed, ROTATE RIGHT");
    playerMoved = true;
    if(angle > 0){
      angle -= 3; //rotate right
    }
  }

  return angle;
}

int controlMove(int xPos){
  //check joystick up and down buttons
  if (!seeSaw.digitalRead(BUTTON_Y)) {
    Serial.println("Y Button Pressed, MOVE LEFT"); //Debugging
    playerMoved = true;
    if(xPos > tankSizeX/2){
      xPos -= 3; //move left
    }
  } else if(!seeSaw.digitalRead(BUTTON_A)){
    Serial.println("A Button Pressed, MOVE RIGHT");
    playerMoved = true;
    if(xPos < screenWidth - tankSizeX/2){
      xPos += 3; //move right
    }
  }
  
  return xPos;
}

//get player 2 from bluetooth
void getPlayerPos(){
  //position
  //angle
  //color
}

//get projectile from bluetooth
void getProjectilePos(){
  //position
  //color
  //power type / size
}

/////////////////////////////////////////////////////////////////////////
//projectile/shoot functions
/////////////////////////////////////////////////////////////////////////

Power selectPower(Power prevPower){
  if(M5.BtnA.isPressed()){
    return SMALL;
  } else if(M5.BtnB.isPressed()){
    return MEDIUM;
  } else if(M5.BtnC.isPressed()){
    return LARGE;
  } else {
    return prevPower;
  }
}

void shoot(uint16_t color, int xPos, int angle, Power power){

  if (!seeSaw.digitalRead(BUTTON_SELECT)) {
    Serial.println("SELECT Button Pressed, SHOOT"); //Debugging

    //spawn projectile based on power type, give initial speed
    if(power == SMALL){
      //spawn a small projectile
      projSizeR = smallSizeR;
      Serial.println("Small projectile");

      //give large initial speed
      speed = 12;
    } else if(power == MEDIUM){
      //spawn a medium projectile
      projSizeR = medSizeR;
      Serial.println("Small projectile");

      //give medium initial speed
      speed = 10;
    } else {
      //spawn a large projectile
      projSizeR = largeSizeR;
      Serial.println("Small projectile");

      //give small initial speed
      speed = 8;
    }

    //calculate velocity
    float angleRad = angle * DEG_TO_RAD;

    //init projectile at tip of barrel X and Y pos
    projX = xPos + (projOffset+projSizeR/2) * cos(angleRad); 
    projY = tankBarrelY - (projOffset+projSizeR/2) * sin(angleRad);
    
    vx = speed * cos(angleRad);
    vy = -speed * sin(angleRad);
  
    //pause game until projectile strikes ground
    while((projY <= floorHeight) & (projX <= screenWidth) & (projX >= 0)){
      //draw projectile in new position
      projectileMotion(color);
    }

    //check collision
    checkCollision(power);
  }
}

void checkCollision(Power power){
  //check projX position and distance from player

  //calculate damage

  //update health bar

  //vibration
  if(power == SMALL){
    M5.Axp.SetVibration(true);
    delay(100);
  } else if(power == MEDIUM){
    M5.Axp.SetVibration(true);
    delay(250);
  } else {
    M5.Axp.SetVibration(true);
    delay(400);
  }

  M5.Axp.SetVibration(false);
}

void projectileMotion(uint16_t color){
  //calculate new position and velocity
  projX += vx;
  projY += vy;

  vy += gravity;  // gravity pulls down

  Serial.printf("projX: %f - projY: %f\n", projX, projY); //debug

  // draw projectile in new position
  M5.Lcd.fillCircle(projX, projY, projSizeR, color);

  //small delay for animation
  delay(50);

  //erase previous projectile
  M5.Lcd.fillCircle(projX, projY, projSizeR, skyColor);
}

void checkBoarderLimit() {
  // if (tankPos1 < 0) tankPos1 = 0;
  // if (tankPos1 > screenWidth - playerSize) tankPos1 = screenWidth - playerSize;
  // if (player1YPos < 0) player1YPos = 0;
  // if (player1YPos > screenHeight - playerSize) player1YPos = screenHeight - playerSize;
}

void checkCollision() {
  // if (tankPos1 < tankPos2 + 4 &&
  //     tankPos1 + 4*player1Speed > tankPos2 &&
  //     player1YPos < player2YPos + 4 &&
  //     player1YPos + 4*player1Speed > player2YPos) {
  //   //Serial.println("Collision!"); //Debugging
  //   // End Game
  //   drawGameOver();
  // }
}

/////////////////////////////////////////////////////////////////////////
//draw functions
/////////////////////////////////////////////////////////////////////////

void tankSelectionScreen() {
    String text = "TANK SELECTION";
    int i = 0;

    M5.Lcd.fillScreen(0x4B5320); // Olive drab military color
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(screenWidth/2 - text.length()*6*3/2, 45);
    M5.Lcd.print(text);

    while (!player2Ready) {
      //check for joystick left and right input
      if (seeSaw.analogRead(JOY1_X) < 480 || seeSaw.analogRead(JOY1_X) > 544) {
        M5.Lcd.fillScreen(0x4B5320); // Olive military color
        M5.Lcd.setTextColor(WHITE);
        M5.Lcd.setTextSize(3);
        M5.Lcd.setCursor(screenWidth/2 - text.length()*6*3/2, 45);
        M5.Lcd.print("TANK SELECTION");
        
        if (seeSaw.analogRead(JOY1_X) < 480)  i++;
        else if (seeSaw.analogRead(JOY1_X) > 544) i--;
        if (i < 0) i = 3;
        else if (i > 3) i = 0;

        M5.Lcd.fillCircle(screenWidth/2, screenHeight * 1/3 + 45, 40, tankColor[i]);
        M5.Lcd.drawCircle(screenWidth/2, screenHeight * 1/3 + 45, 40, BLACK);
        M5.Lcd.drawCircle(screenWidth/2, screenHeight * 1/3 + 45, 39, BLACK);
        M5.Lcd.drawCircle(screenWidth/2, screenHeight * 1/3 + 45, 38, BLACK);

        //M5.Lcd.setTextSize(2);
        M5.Lcd.setCursor(screenWidth/2 - tankName[i].length()*6*3/2 + 5, screenHeight/2 + 65);
        M5.Lcd.setTextColor(tankColor[i]);
        M5.Lcd.print(tankName[i]);

        switch (i) {
          case 0:
              M5.Lcd.fillEllipse(screenWidth/2, screenHeight * 1/3 + 45, 8, 35, BLACK);
              break;
          case 1:
              //TODO: Add unique barrel design for WRAITH
              M5.Lcd.fillRect(screenWidth/2 - 4, screenHeight * 1/3 + 10, 8, 30, BLACK);
              break;
          case 2:
              //TODO: Add unique barrel design for SCORPION
              M5.Lcd.fillRect(screenWidth/2 - 4, screenHeight * 1/3 + 10, 8, 30, BLACK);
              break;
          case 3:
              //TODO: Add unique barrel design for ABRAMS
              M5.Lcd.fillRect(screenWidth/2 - 4, screenHeight * 1/3 + 10, 8, 30, BLACK);
              break;
        }
      }
      if (seeSaw.digitalRead(BUTTON_A) == LOW) {
        Serial.println("Player 1 ready with tank " + tankName[i]);
        tank1 = i;
        player2Ready = true;
      }
      delay(250);
  }
}

void transitionScreenStart() {
  for (int i = 0; i < 16; i++) {
    M5.Axp.SetLcdVoltage(2800-i*50);
    delay(50);
  }
}
void transitionScreenEnd() {
  for (int i = 0; i < 16; i++) {
    M5.Axp.SetLcdVoltage(2000+i*50);
    delay(50);
  }
}

void updatePlayerUI() {
  if (updateUI) {
    drawPlayerTitles();

    //drawItems();

    if (player1Damaged) {
      drawHealth(RED, GREEN, tankHealth1, tankHealth2);
      delay(300);
      drawHealth(GREEN, GREEN, tankHealth1, tankHealth2);
      delay(300);
      drawHealth(RED, GREEN, tankHealth1, tankHealth2);
      delay(300);
      drawHealth(GREEN, GREEN, tankHealth1, tankHealth2);
    } 
    else if (player2Damaged) {
      drawHealth(GREEN, RED, tankHealth1, tankHealth2);
      delay(300);
      drawHealth(GREEN, GREEN, tankHealth1, tankHealth2);
      delay(300);
      drawHealth(GREEN, RED, tankHealth1, tankHealth2);
      delay(300);
      drawHealth(GREEN, GREEN, tankHealth1, tankHealth2);
    }

    if (player1Turn) {
      drawHealth(tankColor[tank1], tankColor[tank2], tankHealth1, tankHealth2);
      M5.Lcd.drawRect(healthBarX1 - 4, healthBarY - 4, healthBarWidth + 8, healthBarHeight + 8, GREENYELLOW);
    } else {
      drawHealth(tankColor[tank1], tankColor[tank2], tankHealth1, tankHealth2);
      M5.Lcd.drawRect(healthBarX2 - 4, healthBarY - 4, healthBarWidth + 8, healthBarHeight + 8, GREENYELLOW);
    }
  }
}

void drawHealth(uint16_t color1, uint16_t color2, int hp1, int hp2){
  M5.Lcd.fillRect(healthBarX1, healthBarY, healthBarWidth * hp1 / 100, healthBarHeight, color1);
  M5.Lcd.drawRect(healthBarX1, healthBarY, healthBarWidth, healthBarHeight, BLACK);
  M5.Lcd.fillRect(healthBarX2, healthBarY, healthBarWidth * hp2 / 100, healthBarHeight, color2);
  M5.Lcd.drawRect(healthBarX2, healthBarY, healthBarWidth, healthBarHeight, BLACK);
}

void drawPlayerTitles() {
  M5.Lcd.setTextSize(2);

  M5.Lcd.setCursor(healthBarX1,healthBarHeight - 2);
  M5.Lcd.setTextColor(tankColor[tank1]);
  M5.Lcd.print("Player 1");

  M5.Lcd.setCursor(healthBarX2,healthBarHeight - 2);
  M5.Lcd.setTextColor(tankColor[tank2]);
  M5.Lcd.print("Player 2");
}

void drawItems() {
  M5.Lcd.fillRect(itemX1, itemY, itemSquareSize, itemSquareSize, WHITE);
  M5.Lcd.drawRect(itemX1, itemY, itemSquareSize, itemSquareSize, BLACK);
  switch (tank1ShotType) {
    case NORMAL:
      M5.Lcd.fillCircle(itemX1 + itemSquareSize/2, itemY + itemSquareSize/2, 12, BLACK);
      break;
    case SPLIT:
      M5.Lcd.fillCircle(itemX1 + itemSquareSize/2 - 5, itemY + itemSquareSize/2, 10, BLACK);
      M5.Lcd.fillCircle(itemX1 + itemSquareSize/2 + 5, itemY + itemSquareSize/2, 10, BLACK);
      break;
    case STRAIFE_RUN:
      M5.Lcd.fillRect(itemX1 + itemSquareSize/2 - 7, itemY + itemSquareSize/2 - 3, 14, 6, BLACK);
      break;
  }

  M5.Lcd.fillRect(itemX2, itemY, itemSquareSize, itemSquareSize, WHITE);
  M5.Lcd.drawRect(itemX2, itemY, itemSquareSize, itemSquareSize, BLACK);
  switch (tank2ShotType) {
    case NORMAL:
      M5.Lcd.fillCircle(itemX2 + itemSquareSize/2, itemY + itemSquareSize/2, 12, BLACK);
      break;
    case SPLIT:
      M5.Lcd.fillCircle(itemX2 + itemSquareSize/2 - 5, itemY + itemSquareSize/2, 10, BLACK);
      M5.Lcd.fillCircle(itemX2 + itemSquareSize/2 + 5, itemY + itemSquareSize/2, 10, BLACK);
      break;
    case STRAIFE_RUN:
      M5.Lcd.fillRect(itemX2 + itemSquareSize/2 - 7, itemY + itemSquareSize/2 - 3, 14, 6, BLACK);
      break;
  }
}

void drawMap(){
  M5.Lcd.fillScreen(skyColor);
  M5.Lcd.fillRect(0, floorHeight, screenWidth, screenHeight - floorHeight, floorColor);
}

void drawTank(uint16_t color,int xPos,int angle){
  refreshDrawTank();

  //draw Tank body
  M5.Lcd.fillRect(xPos - tankSizeX/2, tankBodyY, tankSizeX, tankSizeY, color);
  M5.Lcd.fillCircle(xPos, tankBarrelY, tankSizeR, color);

  //calculate barrel angle
  int xEnd = barrelSizeL*cos(angle*DEG_TO_RAD);
  int yEnd = barrelSizeL*sin(angle*DEG_TO_RAD);
  for(int i = -2; i < 3; i++){
    M5.Lcd.drawLine(xPos + i, floorHeight - tankSizeY - tankSizeR/2 + i, xPos + xEnd + i, floorHeight - tankSizeY - tankSizeR/2 - yEnd + i, color);
  }
}

void refreshDrawTank() {
  if (playerMoved && player1Turn) {
    M5.Lcd.fillRect(tankPos1 - tankSizeX - 20, tankBodyY - 20, tankSizeX + 40, tankSizeY + 20, skyColor);
    playerMoved = false;
  } else if (playerMoved && !player1Turn) {
    M5.Lcd.fillRect(tankPos2 - tankSizeX - 20, tankBodyY - 20, tankSizeX + 40, tankSizeY + 20, skyColor);
    playerMoved = false;
  }
}

/////////////////////////////////////////////////////////////////////////
//Bluetooth Server functions
/////////////////////////////////////////////////////////////////////////
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
  String position = String(tankPos1);
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

void drawGameOver(bool whoWon){
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(50, 50);
  M5.Lcd.setTextSize(4);
  M5.Lcd.println("Game Over\n");

  //check who won
  if(whoWon == 0){
    M5.Lcd.println("Player 1 Wins!");
  } else {
    M5.Lcd.println("Player 2 Wins!");
  }
}