#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <M5Core2.h>
#include <Adafruit_Seesaw.h>

/////////////////////////////////////////////////////////////////////////
//Bluetooth variables
/////////////////////////////////////////////////////////////////////////
BLEServer *bleServer;
BLEService *bleService;
BLECharacteristic *bleCharacteristic_S;
BLECharacteristic *bleCharacteristic_C;
bool deviceConnected = false;
bool previouslyConnected = false;
int timer = 0;

// See the following for generating UUIDs: https://www.uuidgenerator.net/
#define SERVICE_UUID "74be116b-2158-4c76-a579-0980aaa415d3"
#define CHARACTERISTIC_UUID_S "7dcf8cb1-a9b1-44e6-bed5-3c2ffe02bb07"
#define CHARACTERISTIC_UUID_C "41755070-8e22-4588-bf9b-80dd3ac3f42f"

// BLE Broadcast Name
static String BLE_BROADCAST_NAME = "M5 Core2 Tanks Game";

///////////////////////////////////////////////////////////////
// BLE Server Callback Methods
///////////////////////////////////////////////////////////////
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer *pServer) {
        deviceConnected = true;
        previouslyConnected = true;
        Serial.println("Device connected...");
    }
    void onDisconnect(BLEServer *pServer) {
        deviceConnected = false;
        Serial.println("Device disconnected...");
    }
};

///////////////////////////////////////////////////////////////
// BLECharacteristic Callback Methods
///////////////////////////////////////////////////////////////
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
    // Callback function to support a read request.
    void onRead(BLECharacteristic* pCharacteristic) {
        String characteristicUUID = pCharacteristic->getUUID().toString().c_str();
        String chraracteristicValue = pCharacteristic->getValue().c_str();
        Serial.printf("Client JUST read from %s: %s", characteristicUUID.c_str(), chraracteristicValue.c_str());
    }

    // Callback function to support a write request.
    void onWrite(BLECharacteristic* pCharacteristic) {
        // Get the characteristic enum and print for logging
        String characteristicUUID = pCharacteristic->getUUID().toString().c_str();
        String chraracteristicValue = pCharacteristic->getValue().c_str();
        Serial.printf("Client JUST wrote to %s: %s", characteristicUUID.c_str(), chraracteristicValue.c_str());

        // TODO: Take action by checking if the characteristicUUID matches a known UUID
        if (characteristicUUID.equalsIgnoreCase(CHARACTERISTIC_UUID_S)) {
          // The characteristicUUID that just got written to by a client matches the known
          // CHARACTERISTIC_UUID (assuming this constant is defined somewhere in our code)
        }
    }

    // Callback function to support a Notify request.
    void onNotify(BLECharacteristic* pCharacteristic) {
        String characteristicUUID = pCharacteristic->getUUID().toString().c_str();
        Serial.printf("Client JUST notified about change to %s: %s", characteristicUUID.c_str(), pCharacteristic->getValue().c_str());
    }

    // Callback function to support when a client subscribes to notifications/indications.
    void onSubscribe(BLECharacteristic* pCharacteristic, uint16_t subValue) {
    }

    // Callback function to support a Notify/Indicate Status report.
    void onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code) {
        // Print appropriate response
		String characteristicUUID = pCharacteristic->getUUID().toString().c_str();
        switch (s) {
            case SUCCESS_INDICATE:
                // Serial.printf("Status for %s: Successful Indication", characteristicUUID.c_str());
                break;
            case SUCCESS_NOTIFY:
                Serial.printf("Status for %s: Successful Notification", characteristicUUID.c_str());
                break;
            case ERROR_INDICATE_DISABLED:
                Serial.printf("Status for %s: Failure; Indication Disabled on Client", characteristicUUID.c_str());
                break;
            case ERROR_NOTIFY_DISABLED:
                Serial.printf("Status for %s: Failure; Notification Disabled on Client", characteristicUUID.c_str());
                break;
            case ERROR_GATT:
                Serial.printf("Status for %s: Failure; GATT Issue", characteristicUUID.c_str());
                break;
            case ERROR_NO_CLIENT:
                Serial.printf("Status for %s: Failure; No BLE Client", characteristicUUID.c_str());
                break;
            case ERROR_INDICATE_TIMEOUT:
                Serial.printf("Status for %s: Failure; Indication Timeout", characteristicUUID.c_str());
                break;
            case ERROR_INDICATE_FAILURE:
                Serial.printf("Status for %s: Failure; Indication Failure", characteristicUUID.c_str());
                break;
        }
    }
};

/////////////////////////////////////////////////////////////////////////
//Joystick/Gamepad Buttons variables
/////////////////////////////////////////////////////////////////////////
Adafruit_seesaw ss = Adafruit_seesaw(&Wire);

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

/////////////////////////////////////////////////////////////////////////
//display variables
/////////////////////////////////////////////////////////////////////////
int sWidth = 0;
int sHeight = 0;

//draw map variables
int floorHeight = 175;
uint16_t skyColor = CYAN;
uint16_t floorColor = 0;

//draw tank variables
const int tankSizeX = 25;
const int tankSizeY = 12;
const int tankSizeR = 7;
const int barrelSizeL = 15;
const int barrelSizeH = 3;

int tankBodyY = 0;
int tankBarrelY = 0;

/////////////////////////////////////////////////////////////////////////
//game tank variables
/////////////////////////////////////////////////////////////////////////
uint16_t tankColor1 = GREEN; //default
uint16_t tankColor2 = RED; //default
int tankPos1 = 50;
int tankPos2 = 250;
int barrelAngle1 = 0; //degrees 0 is on the right like cartesian plane
int barrelAngle2 = 0; //degrees 0 is on the right like cartesian plane
int tankHealth1 = 100;
int tankHealth2 = 100;

/////////////////////////////////////////////////////////////////////////
//game projectile variables
/////////////////////////////////////////////////////////////////////////

//server controls the game state
enum GameState{
  CONNECTING,
  CUSTOMIZE_PLAYER,
  PLAYER1_TURN,
  PLAYER2_TURN,
  GAME_OVER
};
// GameState gameState = CONNECTING; //startup default state
GameState gameState = PLAYER1_TURN; //testing default state


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

const float gravity = 0.5;
const float projOffset = 20;
float projX1 = 0;
float projY1 = 300; //init beyond screen
float projX2 = 0;
float projY2 = 300;
int projSizeR = 0;
float vx = 0;
float vy = 0;
float speed = 0;

bool collision;

/////////////////////////////////////////////////////////////////////////
//functions
/////////////////////////////////////////////////////////////////////////
//bluetooth functions
void broadcastBleServer();
void readData();
void sendData();

//joystick functions
void setupJoystick();

//draw functions
void drawMap();
void drawTank(uint16_t color,int xPos,int angle);
void drawHealth(uint16_t color1, uint16_t color2, int hp1, int hp2);
void drawGameOver(bool whoWon);
// void drawHealth(uint16_t color1, int color2, int hp1, int hp2);

//game functions
void serverGameManager();

//tank control functions
int controlAngle(int angle);
int controlMove(int xPos);

//health functions
void damage(Power power);

//projectile/shoot functions
Power selectPower(Power prevPower);
void shoot(uint16_t color, int xPos, int angle, Power power);
void projectileMotion(uint16_t color, Power power);
void checkCollision(Power power);


/////////////////////////////////////////////////////////////////////////
//Setup
/////////////////////////////////////////////////////////////////////////
void setup() {
  M5.begin();
  sWidth = M5.Lcd.width();
  sHeight = M5.Lcd.height();
  Serial.println(sWidth);
  Serial.println(sHeight);

  //draw map
  floorColor = M5.Lcd.color565(240, 239, 173);
  drawMap();

  //draw tanks
  tankColor1 = M5.Lcd.color565(12, 140, 72);
  tankBodyY = floorHeight - tankSizeY;
  tankBarrelY = floorHeight - tankSizeY - tankSizeR/2 + 2;
  drawTank(tankColor1, tankPos1, barrelAngle1);
  drawTank(tankColor2, tankPos2, barrelAngle2);

  setupJoystick();
}

/////////////////////////////////////////////////////////////////////////
//loop
/////////////////////////////////////////////////////////////////////////
void loop() {
  M5.update();

  serverGameManager();

  delay(50);
}

/////////////////////////////////////////////////////////////////////////
//functions
/////////////////////////////////////////////////////////////////////////

//game manager
void serverGameManager(){
  //player turn
  if(gameState == PLAYER1_TURN){
    //erase previous tank
    drawTank(skyColor, tankPos1, barrelAngle1);
    drawTank(skyColor, tankPos2, barrelAngle2);

    //move
    tankPos1 = controlMove(tankPos1);

    //change barrel angle
    barrelAngle1 = controlAngle(barrelAngle1);

    //draw tank in new position
    drawTank(tankColor1, tankPos1, barrelAngle1);
    drawTank(tankColor2, tankPos2, barrelAngle2);

    //select power
    tankPower1 = selectPower(tankPower1);

    //send tank/power over bluetooth to client

    //spawn projectile and animate, pause game until projectile strikes the ground
    shoot(tankColor1, tankPos1, barrelAngle1, tankPower1);

    //check change turn

  } else if(gameState == PLAYER2_TURN){
    //bluetooth player
    //get bluetooth date from client
    //get player pos
    //get player angle
    //get player power
    //get projectile pos
    //check collisions

  } else if(gameState == GAME_OVER){
    drawGameOver(0); //--------------------------------FIXME --------------------------check who won
  }
  
  //local player 
    //move player
    //change angle
    //select power
    //shoot
    //check collisions
}

// Joystick Setup
void setupJoystick() {
  while(!ss.begin(0x50)){
    Serial.println("ERROR! seesaw not found");
    delay(100);
  }
  Serial.println("seesaw started at 0x50");
  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version != 5743) {
    Serial.print("Wrong firmware loaded? ");
    Serial.println(version);
    while(1) delay(10);
  }
  Serial.println("Found Product 5743");

  ss.pinModeBulk(button_mask, INPUT_PULLUP);
}

/////////////////////////////////////////////////////////////////////////
//tank control functions
/////////////////////////////////////////////////////////////////////////

int controlAngle(int angle){
  //check joystick up and down buttons
  if (!ss.digitalRead(BUTTON_X)) {
    Serial.println("X Button Pressed, ROTATE LEFT"); //Debugging
    if(angle < 180){
      angle += 3; //rotate left
    }
  } else if(!ss.digitalRead(BUTTON_B)){
    Serial.println("B Button Pressed, ROTATE RIGHT");
    if(angle > 0){
      angle -= 3; //rotate right
    }
  }

  return angle;
}

int controlMove(int xPos){
  //check joystick up and down buttons
  if (!ss.digitalRead(BUTTON_Y)) {
    Serial.println("Y Button Pressed, MOVE LEFT"); //Debugging
    if(xPos > tankSizeX/2){
      xPos -= 3; //move left
    }
  } else if(!ss.digitalRead(BUTTON_A)){
    Serial.println("A Button Pressed, MOVE RIGHT");
    if(xPos < sWidth - tankSizeX/2){
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

  if (!ss.digitalRead(BUTTON_SELECT)) {
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
    projX1 = xPos + (projOffset+projSizeR/2) * cos(angleRad); 
    projY1 = tankBarrelY - (projOffset+projSizeR/2) * sin(angleRad);
    
    vx = speed * cos(angleRad);
    vy = -speed * sin(angleRad);

    //calculate projectile motion and check for collisions/hit
    projectileMotion(color, power);
  }
}

void projectileMotion(uint16_t color, Power power){
  //pause game until projectile strikes ground
  collision = false;
  while(!collision){
    //calculate new position and velocity
    projX1 += vx;
    projY1 += vy;

    vy += gravity;  // gravity pulls down

    Serial.printf("projX: %f - projY: %f\n", projX1, projY1); //debug

    // draw projectile in new position
    M5.Lcd.fillCircle(projX1, projY1, projSizeR, color);

    //check for collision
    checkCollision(power);

    //small delay for animation
    delay(50);

    //erase previous projectile
    M5.Lcd.fillCircle(projX1, projY1, projSizeR, skyColor);
  }
}

//check collisions
void checkCollision(Power power){
  //check if out of screen
  if(projX1 > sWidth || projX1 < 0){
    Serial.println("MISS, out of screen");
    collision = true;
    return;
  }

  //check for collision with ground
  if(projY1 >= floorHeight){
    //vibrate
    M5.Axp.SetVibration(true);
    delay(100);
    M5.Axp.SetVibration(false);

    Serial.println("MISS, landed on ground");

    collision = true;
    return;
  }

  float targetX;
  if(gameState == PLAYER1_TURN){
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
  bool hitX = (projX1 >= hitBoxLeft && projX1 <= hitBoxRight);
  bool hitY = (projY1 >= hitBoxTop  && projY1 <= hitBoxBottom);

  Serial.printf("proj size: %d\n");
  Serial.printf("proj(%.1f, %.1f) | tank: (%.1f, %.1f)\n", projX1, projY1, targetX, floorHeight - tankSizeY);
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

/////////////////////////////////////////////////////////////////////////
//health functions
/////////////////////////////////////////////////////////////////////////

void damage(Power power){
  int dmg = 0;
  if(power == SMALL){
    dmg = smallDmg;
  } else if(power == MEDIUM){
    dmg = medDmg;
  } else {
    dmg = largeDmg;
  }

  if(gameState == PLAYER1_TURN){
    //damage player 2
    tankHealth2 -= dmg;
    Serial.printf("Player 2 Health: %d\n", tankHealth2);
  } else if(gameState == PLAYER2_TURN){
    //damage player 1
    tankHealth1 -= dmg;
    Serial.printf("Player 1 Health: %d\n", tankHealth1);
  }

  // drawHealth(tankColor1, tankColor2, tankHealth1, tankHealth2);
}

/////////////////////////////////////////////////////////////////////////
//draw functions
/////////////////////////////////////////////////////////////////////////

void drawMap(){
  M5.Lcd.fillScreen(skyColor);
  M5.Lcd.fillRect(0, floorHeight, sWidth, sHeight - floorHeight, floorColor);
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

void drawHealth(uint16_t color1, int color2, int hp1, int hp2){
  
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

///////////////////////////////////////////////////////////////
// Bluetooth Server
///////////////////////////////////////////////////////////////
void broadcastBleServer() {    
    // Initializing the server, a service and a characteristic 
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new MyServerCallbacks());
    bleService = bleServer->createService(SERVICE_UUID);

    //initialize server characteristic
    bleCharacteristic_S = bleService->createCharacteristic(CHARACTERISTIC_UUID_S,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_INDICATE
    );
    bleCharacteristic_S->setCallbacks(new MyCharacteristicCallbacks());
    bleCharacteristic_S->setValue("Hello from Jacob -> This is the server characteristic");
    bleService->start();

    //initialize client characteristic
    bleCharacteristic_C = bleService->createCharacteristic(CHARACTERISTIC_UUID_C,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_INDICATE
    );
    bleCharacteristic_C->setCallbacks(new MyCharacteristicCallbacks());
    bleCharacteristic_C->setValue("Hello from Jacob -> This is the client characteristic");
    bleService->start();

    // Start broadcasting (advertising) BLE service
    BLEAdvertising *bleAdvertising = BLEDevice::getAdvertising();
    bleAdvertising->addServiceUUID(SERVICE_UUID);
    bleAdvertising->setScanResponse(true);
    bleAdvertising->setMinPreferred(0x12); // Use this value most of the time 
    // bleAdvertising->setMinPreferred(0x06); // Functions that help w/ iPhone connection issues 
    // bleAdvertising->setMinPreferred(0x00); // Set value to 0x00 to not advertise this parameter
    BLEDevice::startAdvertising();
    Serial.println("Characteristic defined...you can connect with your phone!"); 

}

void readData()
{
  //read from ble
  //data from player2 client player

  // Read the characteristic's value as a string (which can be written from a client)
  std::string readValue = bleCharacteristic_C->getValue();
  Serial.printf("Client Characteristic Value (player 2 Position): %s\n", readValue.c_str()); //format xxx-yyy
  String str = readValue.c_str();

  // parse the data
  
  //format: gameState_color-health=power+tankPosX>projPosX,projPosY
  //ranges: (0-1)_(0-3)-(0-100)=(0-2)+(0-320)>(0-320,0-240)

  //gameState controlled, not received by server
  tankColor2 = str.substring(str.indexOf("_"), str.indexOf("-")).toInt();
  tankHealth2 = str.substring(str.indexOf("-"), str.indexOf("=")).toInt();
  int temp = str.substring(str.indexOf("="), str.indexOf("+")).toInt();
  if(temp == 0){
    tankPower2 = SMALL;
  } else if(temp == 1){
    tankPower2 = MEDIUM;
  } else if(temp == 2){
    tankPower2 = LARGE;
  }
  tankPos2 = str.substring(str.indexOf("+"), str.indexOf(">")).toInt();

  projX2 = str.substring(str.indexOf(">"), str.indexOf(",")).toInt();
  projY2 = str.substring(str.indexOf(","), str.indexOf(")")).toInt();
}

void sendData(){
  // write to ble
  //data for player1 Server player

  //format: gameState_color-health=power+tankPosX>projPosX,projPosY)
  //ranges: (0-1)_(0-3)-(0-100)=(0-2)+(0-320)>(0-320,0-240)

  //convert to string
  String str = String(gameState) + "_" + String(tankColor1) + "-" + String(tankHealth1) + "=" + String(tankPower1) + "+" + String(tankPos1) + ">" + String(projX1) + "," + String(projY1);

  // write the charactistic's value as a string (which can be read by a client)
  bleCharacteristic_S->setValue(str.c_str());
  bleCharacteristic_S->notify();
  Serial.printf("%d Server characteristic value (player 1 Position): %s\n", str.c_str());
}