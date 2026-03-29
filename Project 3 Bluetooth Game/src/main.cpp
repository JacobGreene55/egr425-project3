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
static String BLE_BROADCAST_NAME = "M5 Core2 Tanks Game";

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
GameState serverGameState = CONNECTING; //testing default state
GameState clientGameState = CONNECTING;
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
uint16_t floorColor = M5.Lcd.color565(240, 239, 173);

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

const int smallSpd = 12;
const int medSpd = 10;
const int largeSpd = 8;

const int smallDmg = 5;
const int medDmg = 20;
const int largeDmg = 35;

bool collision = false;
const float gravity = 0.5;
const float projOffset = 20;
float projX1 = 0;
float projY1 = 300; //init beyond screen
float oldProjX1 = 0;
float oldProjY1 = 300;
float projX2 = 0;
float projY2 = 300; //init beyond screen
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

// Old positions for refresh
int oldTankPos1 = 50;
int oldTankPos2 = 250;
int oldBarrelAngle1 = 0;
int oldBarrelAngle2 = 0;

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
void projectileMotion(uint16_t color, Power power);
void checkCollision(Power power);
void damage(Power power);

//tank location
void checkBoarderLimit();
void checkCollision();

// UI Draw Functions //
void drawHealth();
void refreshDrawTank();
void drawPlayerTitles();
void drawItems();
void drawBarrelOutline();
void drawProjectile1(uint16_t color, Power power);

// Server Communication Functions //
void sendClientTankInfo();
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
    String str = String((char*)pData, length);
    str.trim();
    Serial.printf("Notify callback for characteristic %s of data length %d\n", pBLERemoteCharacteristic->getUUID().toString().c_str(), length);
    Serial.printf("\tData: %s\n", str.c_str());

    if (str.length() == 0) {
        return;
    }

    int uIdx = str.indexOf('_');
    if (uIdx > 0) {
        int gs = str.substring(0, uIdx).toInt();
        if (gs >= CONNECTING && gs <= GAME_OVER) {
            serverGameState = (GameState)gs;
            player1Turn = (serverGameState == PLAYER1_TURN);
        }
    }

    int dashIdx = str.indexOf('-', uIdx + 1);
    int eqIdx = str.indexOf('=', dashIdx + 1);
    int plusIdx = str.indexOf('+', eqIdx + 1);
    int gtIdx = str.indexOf('>', plusIdx + 1);
    int commaIdx = str.indexOf(',', gtIdx + 1);
    int atIdx = str.indexOf('@', plusIdx + 1);
    int curlyIdx = str.indexOf('}');

    if (uIdx > 0 && dashIdx > uIdx && eqIdx > dashIdx && plusIdx > eqIdx && gtIdx > plusIdx) {
        tank1 = str.substring(uIdx + 1, dashIdx).toInt();
        tankHealth1 = str.substring(dashIdx + 1, eqIdx).toInt();

        int power = str.substring(eqIdx + 1, plusIdx).toInt();
        if (power == 0) tankPower1 = SMALL;
        else if (power == 1) tankPower1 = MEDIUM;
        else if (power == 2) tankPower1 = LARGE;

        // Parse position and angle
        if (atIdx > gtIdx) {
            oldTankPos1 = tankPos1;
            tankPos1 = str.substring(plusIdx + 1, atIdx).toInt();
            oldBarrelAngle1 = barrelAngle1;
            barrelAngle1 = str.substring(atIdx + 1, gtIdx).toInt();
        } else {
            oldTankPos1 = tankPos1;
            tankPos1 = str.substring(plusIdx + 1, gtIdx).toInt();
        }

        if (commaIdx > gtIdx) {
            oldProjX1 = projX1;
            oldProjY1 = projY1;
            projX1 = str.substring(gtIdx + 1, commaIdx).toFloat();
            projY1 = str.substring(commaIdx + 1).toFloat();
        } else {
            projX1 = str.substring(gtIdx + 1).toFloat();
        }

        if (serverGameState == PLAYER1_TURN) {
            clientGameState = PLAYER1_TURN;
        } else if (serverGameState == PLAYER2_TURN) {
            clientGameState = PLAYER2_TURN;
        } else if (serverGameState == GAME_OVER) {
            clientGameState = GAME_OVER;
        }

        updateUI = true;
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
    bleClient->setMTU(512);

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
    BLEDevice::setMTU(512);

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
            //drawMap(begin);
        }
        else {
            Serial.println("We have failed to connect to the server; there is nothin more we will do.");
            drawScreenTextWithBackground("FAILED to connect to BLE server: " + String(bleRemoteServer->getName().c_str()), TFT_GREEN);
            delay(3000);
        }
    }

    //deviceConnected = true; // Debug line

    // If we are connected to a peer BLE Server, update the characteristic each time we are reached
    // with the current time since boot.
    while (deviceConnected)
    {
      switch (clientGameState) {
        case CONNECTING:
          drawScreenTextWithBackground("Connected to BLE server: " + String(bleRemoteServer->getName().c_str()), TFT_GREEN);
          delay(1000); // Debug lines <  ^
          clientGameState = CUSTOMIZE_PLAYER;// dubug line, replace with reading the server's game state
          break;
        case CUSTOMIZE_PLAYER:
          tankSelectionScreen();
          if (player1Ready && player2Ready) {
            player2Ready = false;
            sendClientTankInfo();
            transitionScreenStart();
            drawMap();
            transitionScreenEnd();
          }
          // Serial.println(serverGameState);
          break;
        case PLAYER1_TURN:
          player1Turn = true;

          //erase projectile
          drawProjectile1(skyColor, tankPower1);

          sendClientTankInfo();
          updatePlayerUI();
          refreshDrawTank();
          drawTank(tankColor[tank1], tankPos1, barrelAngle1);
          drawTank(tankColor[tank2], tankPos2, barrelAngle2);
          drawProjectile1(tankColor[tank1], tankPower1);
          //recieve tank/power over bluetooth from server, update player 1's stuff
          //TODO: Finish function
          //spawn projectile and animate, pause game until projectile strikes the ground
          //shoot(tankColor[tank1], tankPos1, barrelAngle1, tankPower1);

          if (serverGameState == PLAYER2_TURN) {
            clientGameState = PLAYER2_TURN;
            player1Turn = false;
            sendClientTankInfo();
          } else if (serverGameState == GAME_OVER) {
            clientGameState = GAME_OVER;
            whoWon = true; //Server's perspective
            sendClientTankInfo();
          }
          break;
        case PLAYER2_TURN:
          refreshDrawTank();
          drawTank(tankColor[tank1], tankPos1, barrelAngle1);
          drawTank(tankColor[tank2], tankPos2, barrelAngle2);

          sendClientTankInfo();

          tankPos2 = controlMove(tankPos2);

          barrelAngle2 = controlAngle(barrelAngle2);

          refreshDrawTank();
          drawTank(tankColor[tank1], tankPos1, barrelAngle1);
          drawTank(tankColor[tank2], tankPos2, barrelAngle2);

          //Serial.println("Entering SelectPower function"); //Dubug line
          tankPower2 = selectPower(tankPower2);

          shoot(tankColor[tank2], tankPos2, barrelAngle2, tankPower2);

          if (tankHealth1 <= 0) {
            whoWon = false; //Server's perspective
            clientGameState = GAME_OVER;
          }
          // Handle player 2's turn
          break;
        case GAME_OVER:
          drawGameOver(whoWon);
          break;
      }
      delay(100);
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
  int oldAngle = angle;
  //check joystick up and down buttons
  if (!seeSaw.digitalRead(BUTTON_X)) {
    Serial.println("X Button Pressed, ROTATE LEFT"); //Debugging
    if(angle < 180){
      angle += 3; //rotate left
    }
  } else if(!seeSaw.digitalRead(BUTTON_B)){
    Serial.println("B Button Pressed, ROTATE RIGHT");
    if(angle > 0){
      angle -= 3; //rotate right
    }
  }

  if (angle != oldAngle) {
    playerMoved = true;
    oldTankPos2 = tankPos2;
  }

  return angle;
}

int controlMove(int xPos){
  int oldPos = xPos;
  //check joystick up and down buttons
  if (!seeSaw.digitalRead(BUTTON_Y)) {
    Serial.println("Y Button Pressed, MOVE LEFT"); //Debugging
    if(xPos > tankSizeX/2){
      xPos -= 3; //move left
    }
  } else if(!seeSaw.digitalRead(BUTTON_A)){
    Serial.println("A Button Pressed, MOVE RIGHT");
    if(xPos < screenWidth - tankSizeX/2){
      xPos += 3; //move right
    }
  }

  if (xPos != oldPos) {
    playerMoved = true;
    oldTankPos2 = oldPos;
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
  M5.update();  // ensure button states are refreshed every loop
  //Serial.println("Selecting power..."); //Debugging

  if (M5.BtnA.wasPressed()) {
    Serial.println("Small power selected");
    return SMALL;
  } else if (M5.BtnB.wasPressed()) {
    Serial.println("Medium power selected");
    return MEDIUM;
  } else if (M5.BtnC.wasPressed()) {
    Serial.println("Large power selected");
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
            speed = smallSpd;
        } else if(power == MEDIUM){
            //spawn a medium projectile
            projSizeR = medSizeR;
            Serial.println("Small projectile");

            //give medium initial speed
            speed = medSpd;
        } else {
            //spawn a large projectile
            projSizeR = largeSizeR;
            Serial.println("Small projectile");

            //give small initial speed
            speed = largeSpd;
        }

        //calculate velocity
        float angleRad = angle * DEG_TO_RAD;

        //init projectile at tip of barrel X and Y pos
        if (serverGameState == PLAYER1_TURN) {
            projX1 = xPos + (projOffset+projSizeR/2) * cos(angleRad); 
            projY1 = tankBarrelY - (projOffset+projSizeR/2) * sin(angleRad);
        } else if (serverGameState == PLAYER2_TURN) {
            projX2 = xPos + (projOffset+projSizeR/2) * cos(angleRad); 
            projY2 = tankBarrelY - (projOffset+projSizeR/2) * sin(angleRad);
        }

        vx = speed * cos(angleRad);
        vy = -speed * sin(angleRad);

        //calculate projectile motion and check for collisions/hit
        projectileMotion(color, power);
    }
}

void checkCollision(Power power){
  //check if out of screen
  if(projX2 > screenWidth || projX2 < 0){
    Serial.println("MISS, out of screen");
    collision = true;
    return;
  }

  //check for collision with ground
  if(projY2 >= floorHeight){
    //vibrate
    M5.Axp.SetVibration(true);
    delay(100);
    M5.Axp.SetVibration(false);

    Serial.println("MISS, landed on ground");

    collision = true;
    return;
  }

  float targetX;
  if(serverGameState == PLAYER1_TURN){
    targetX = tankPos2;
  } else {
    targetX = tankPos1;
  }

  // get hit box
  int hitBoxLeft   = targetX - tankSizeX / 2.0 - projSizeR;
  int hitBoxRight  = targetX + tankSizeX / 2.0 + projSizeR;
  int hitBoxTop    = floorHeight - tankSizeY - tankSizeR / 2.0;
  int hitBoxBottom = floorHeight;

  // --- Check collision (box) ---
  bool hitX = (projX2 >= hitBoxLeft && projX2 <= hitBoxRight);
  bool hitY = (projY2 >= hitBoxTop  && projY2 <= hitBoxBottom);

  Serial.printf("proj size: %d\n");
  Serial.printf("proj(%.1f, %.1f) | tank: (%.1f, %.1f)\n", projX2, projY2, targetX, floorHeight - tankSizeY);
  Serial.printf("inX: %d inY: %d\n", hitX, hitY);

  if(hitX && hitY){
    //check which power
    if(power == SMALL){
      //vibrate
      M5.Axp.SetVibration(true);
      delay(200);
    } else if(power == MEDIUM){
      //vibrate
      M5.Axp.SetVibration(true);
      delay(400);
    } else {
      //vibrate
      M5.Axp.SetVibration(true);
      delay(600);
    }

    Serial.println("HIT!");
    M5.Axp.SetVibration(false);
    damage(power);

    collision = true;
    return;
  } else {
    Serial.println("MISS, not in hitbox");

    collision = false;
  }
}

void projectileMotion(uint16_t color, Power power){
  //pause game until projectile strikes ground
  collision = false;
  while(!collision){
    //calculate new position and velocity
    projX2 += vx;
    projY2 += vy;

    vy += gravity;  // gravity pulls down

    Serial.printf("projX: %f - projY: %f\n", projX2, projY2); //debug

    // draw projectile in new position
    M5.Lcd.fillCircle(projX2, projY2, projSizeR, color);

    //check for collision
    checkCollision(power);
    sendClientTankInfo(); //update server with new projectile values

    //small delay for animation
    delay(50);

    //erase previous projectile
    M5.Lcd.fillCircle(projX2, projY2, projSizeR, skyColor);
  }

  //switch turn after collision
  if(clientGameState == PLAYER1_TURN){
    clientGameState = PLAYER2_TURN;
    player1Turn = false;
  } else if(clientGameState == PLAYER2_TURN){
    serverGameState = PLAYER1_TURN;
    clientGameState = PLAYER1_TURN;
    player1Turn = true;
  }
  sendClientTankInfo(); //update server with new game state
}

void damage(Power power){
  int dmg = 0;
  if(power == SMALL){
    dmg = smallDmg;
  } else if(power == MEDIUM){
    dmg = medDmg;
  } else {
    dmg = largeDmg;
  }

  if(serverGameState == PLAYER1_TURN){
    //damage player 2
    tankHealth2 -= dmg;
    Serial.printf("Player 2 Health: %d\n", tankHealth2);
  } else if(serverGameState == PLAYER2_TURN){
    //damage player 1
    tankHealth1 -= dmg;
    Serial.printf("Player 1 Health: %d\n", tankHealth1);
  }

  //BLUETOOTH
  sendClientTankInfo(); //update client with new health values
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
        Serial.println("Player 2 ready with tank " + tankName[i]);
        tank2 = i;
        player2Ready = true;
        clientGameState = PLAYER1_TURN;
      }
      delay(200);
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

    drawItems();

    drawBarrelOutline();

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

    if (serverGameState == PLAYER1_TURN) {
      drawHealth(tankColor[tank1], tankColor[tank2], tankHealth1, tankHealth2);
      M5.Lcd.drawRect(healthBarX1 - 4, healthBarY - 4, healthBarWidth + 8, healthBarHeight + 8, RED);
      M5.Lcd.drawRect(healthBarX1 - 5, healthBarY - 5, healthBarWidth + 10, healthBarHeight + 10, RED);
    } else if (serverGameState == PLAYER2_TURN) {
      drawHealth(tankColor[tank1], tankColor[tank2], tankHealth1, tankHealth2);
      M5.Lcd.drawRect(healthBarX2 - 4, healthBarY - 4, healthBarWidth + 8, healthBarHeight + 8, RED);
      M5.Lcd.drawRect(healthBarX2 - 5, healthBarY - 5, healthBarWidth + 10, healthBarHeight + 10, RED);
    } else {
      drawHealth(tankColor[tank1], tankColor[tank2], tankHealth1, tankHealth2);
      M5.Lcd.drawRect(healthBarX1 - 4, healthBarY - 4, healthBarWidth + 8, healthBarHeight + 8, BLACK);
      M5.Lcd.drawRect(healthBarX2 - 5, healthBarY - 5, healthBarWidth + 10, healthBarHeight + 10, BLACK);
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
  switch (tankPower1) {
    case SMALL:
      M5.Lcd.fillCircle(itemX1 + itemSquareSize/2, itemY + itemSquareSize/2, 5, BLACK);
      break;
    case MEDIUM:
      M5.Lcd.fillCircle(itemX1 + itemSquareSize/2, itemY + itemSquareSize/2, 8, BLACK);
      break;
    case LARGE:
      M5.Lcd.fillCircle(itemX1 + itemSquareSize/2, itemY + itemSquareSize/2, 12, BLACK);
      break;
  }

  M5.Lcd.fillRect(itemX2, itemY, itemSquareSize, itemSquareSize, WHITE);
  M5.Lcd.drawRect(itemX2, itemY, itemSquareSize, itemSquareSize, BLACK);
  switch (tankPower2) {
    case SMALL:
      M5.Lcd.fillCircle(itemX2 + itemSquareSize/2, itemY + itemSquareSize/2, 5, BLACK);
      break;
    case MEDIUM:
      M5.Lcd.fillCircle(itemX2 + itemSquareSize/2, itemY + itemSquareSize/2, 8, BLACK);
      break;
    case LARGE:
      M5.Lcd.fillCircle(itemX2 + itemSquareSize/2, itemY + itemSquareSize/2, 12, BLACK);
      break;
  }
}

void drawMap(){
  M5.Lcd.fillScreen(skyColor);
  M5.Lcd.fillRect(0, floorHeight, screenWidth, screenHeight - floorHeight, floorColor);
}

void drawTank(uint16_t color,int xPos,int angle){
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

void drawBarrelOutline() {
  if (serverGameState == PLAYER1_TURN) {
    int power1Ratio = 3;
    if (tankPower1 == SMALL) power1Ratio = 3;
    else if (tankPower1 == MEDIUM) power1Ratio = 2;
    else power1Ratio = 1;
    int xStart1 = tankPos1 + 20*cos(barrelAngle1*DEG_TO_RAD);
    int yStart1 = tankBodyY - 5 - 20*sin(barrelAngle1*DEG_TO_RAD);
    int xEnd1 = tankPos1 + power1Ratio * 20*cos(barrelAngle1*DEG_TO_RAD); //tankPos1 + tankPower1+2*cos(barrelAngle1*DEG_TO_RAD);
    int yEnd1 = tankBodyY - 5 - power1Ratio * 20*sin(barrelAngle1*DEG_TO_RAD); //tankPos1 + tankPower1+2*sin(barrelAngle1*DEG_TO_RAD);
    M5.Lcd.drawLine(xStart1, yStart1, xEnd1, yEnd1, BLACK);
  }

  if (serverGameState == PLAYER2_TURN) {
    int power2Ratio = 3;
    if (tankPower2 == SMALL) power2Ratio = 3;
    else if (tankPower2 == MEDIUM) power2Ratio = 2;
    else power2Ratio = 1;
    int xStart2 = tankPos2 + 20*cos(barrelAngle2*DEG_TO_RAD);
    int yStart2 = tankBodyY - 5 - 20*sin(barrelAngle2*DEG_TO_RAD);
    int xEnd2 = tankPos2 + power2Ratio * 20*cos(barrelAngle2*DEG_TO_RAD); //tankPos2 + tankPower2+2*cos(barrelAngle2*DEG_TO_RAD);
    int yEnd2 = tankBodyY - 5 - power2Ratio * 20*sin(barrelAngle2*DEG_TO_RAD); //tankPos2 + tankPower2+2*sin(barrelAngle2*DEG_TO_RAD);
    M5.Lcd.drawLine(xStart2, yStart2, xEnd2, yEnd2, BLACK);
  }
}

void refreshDrawTank() {
  if (clientGameState == PLAYER1_TURN &&
     (oldTankPos1 != tankPos1 || oldBarrelAngle1 != barrelAngle1)) {
    M5.Lcd.fillRect(oldTankPos1 - tankSizeX - 60, tankBodyY - 70, tankSizeX + 120, tankSizeY + 70, skyColor);
  }

  if (clientGameState == PLAYER2_TURN && (oldTankPos2 != tankPos2 || oldBarrelAngle2 != barrelAngle2)) {
    M5.Lcd.fillRect(oldTankPos2 - tankSizeX - 60, tankBodyY - 70, tankSizeX + 120, tankSizeY + 70, skyColor);
  }
}

void drawProjectile1(uint16_t color, Power power){
  //power type / size
  if(power == SMALL){
    projSizeR = smallSizeR;
  } else if(power == MEDIUM){
    projSizeR = medSizeR;
  } else {
    projSizeR = largeSizeR;
  }

  //draw projectile
  M5.Lcd.fillCircle(projX1, projY1, projSizeR, color);
}

/////////////////////////////////////////////////////////////////////////
//Bluetooth Server functions
/////////////////////////////////////////////////////////////////////////
void sendClientTankInfo() {
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
  String message = String(clientGameState) + "_" + String(tank2) + "-" + 
                   String(tankHealth2) + "=" + String(tankPower2) + "+" + 
                   String(tankPos2) + ">" + String(projX2) + 
                   "," + String(projY2) + "@" + String(barrelAngle2) + "}";
  Serial.println("Sending position to server: " + message);
  
  // Use writeValue with response=false to avoid blocking
  bleRemoteCharacteristic_C->writeValue((uint8_t*)message.c_str(), message.length(), false);
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
  if(whoWon){
    M5.Lcd.println("Player 1 Wins!");
  } else {
    M5.Lcd.println("Player 2 Wins!");
  }
}