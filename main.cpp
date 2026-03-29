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

/////////////////////////////////////////////////////////////////////////
//display variables
/////////////////////////////////////////////////////////////////////////
int screenWidth = 0;
int screenHeight = 0;

//draw map variables
int floorHeight = 175;
uint16_t skyColor = CYAN;
uint16_t floorColor = M5.Lcd.color565(240, 239, 173);

//draw tank variables
const int tankSizeX = 25;
const int tankSizeY = 12;
const int tankSizeR = 7;
const int barrelSizeL = 15;
const int barrelSizeH = 3;

int tankBodyY = floorHeight - tankSizeY;
int tankBarrelY = tankBodyY + barrelSizeH/2;

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
//game state variables
/////////////////////////////////////////////////////////////////////////

//server controls the game state
enum GameState{
  CONNECTING,
  CUSTOMIZE_PLAYER,
  PLAYER1_TURN,
  PLAYER2_TURN,
  GAME_OVER
};
GameState serverGameState = CONNECTING; //default state
GameState clientGameState = CONNECTING; //default state

bool player2Ready = false;
bool player1Ready = true; //debug change to false after

/////////////////////////////////////////////////////////////////////////
//customize player variables
/////////////////////////////////////////////////////////////////////////
int tankColor[4] = {MAROON, NAVY, GREEN, YELLOW};
String tankName[4] = {"PREDATOR", "WRAITH", "SCORPION", "ABRAMS"};
int tank1 = 0;
int tank2 = 0;

/////////////////////////////////////////////////////////////////////////
//power variables
/////////////////////////////////////////////////////////////////////////
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

/////////////////////////////////////////////////////////////////////////
//game projectile variables
/////////////////////////////////////////////////////////////////////////

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
//health UI variables
/////////////////////////////////////////////////////////////////////////
bool updateUI = true;

bool player1Damaged = false;
bool player2Damaged = false;
bool player1Turn = true;
int healthBarWidth = 100;
int healthBarHeight = 10;
int healthBarY = 30;
int healthBarX1 = 20;
int healthBarX2 = 0;

/////////////////////////////////////////////////////////////////////////
//power item UI variables
/////////////////////////////////////////////////////////////////////////
int itemSquareSize = 40;
int itemY = 0;
int itemX1 = itemSquareSize - 20;
int itemX2 = 0;

/////////////////////////////////////////////////////////////////////////
//functions
/////////////////////////////////////////////////////////////////////////
//bluetooth functions
void broadcastBleServer();
void readClientData();
void sendServerData();

//joystick functions
void setupJoystick();

//draw functions
void drawMap();
void drawTank(uint16_t color,int xPos,int angle);
void drawHealth(uint16_t color1, uint16_t color2, int hp1, int hp2);
void drawGameOver(bool whoWon);
void drawScreenTextWithBackground(String text, int backgroundColor);
void drawItems();
void drawPlayerItems();
void drawPlayerTitles();
void updatePlayerUI();

//game functions
void serverGameManager();

//customize player functions
void tankSelectionScreen();

void transitionScreenStart();
void transitionScreenEnd();

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
  //bluetooth connection

  M5.begin();

  //initialize vars
  screenWidth = M5.Lcd.width();
  screenHeight = M5.Lcd.height();
  Serial.println(screenWidth);
  Serial.println(screenHeight);

  itemY = screenHeight - itemSquareSize - 10;
  itemX2 = screenWidth - itemSquareSize - 20;

  healthBarX2 = screenWidth - healthBarWidth - 20;

  // Initialize M5Core2 as a BLE server
  Serial.print("Starting BLE...");
  BLEDevice::init(BLE_BROADCAST_NAME.c_str());

  // Broadcast the BLE server
  drawScreenTextWithBackground("Initializing BLE...", TFT_CYAN);
  broadcastBleServer();
  drawScreenTextWithBackground("Broadcasting as BLE server named:\n\n" + BLE_BROADCAST_NAME, TFT_BLUE);

  //wait until connected
  while(!deviceConnected){
    Serial.println("Waiting for a client to connect...");
    delay(500);
  }
  drawScreenTextWithBackground("Connected to BLE server: ", TFT_GREEN);
  delay(3000);

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
  if(serverGameState == CONNECTING){
    Serial.println("CONNECTING State");
    
    serverGameState = CUSTOMIZE_PLAYER; //debug
  } else if(serverGameState == CUSTOMIZE_PLAYER){
    Serial.println("CUSTOMIZE_PLAYER State");

    //customize player color
    tankSelectionScreen();
    if (player2Ready && player1Ready) {
      serverGameState = PLAYER1_TURN;// dubug line, replace with reading the server's game state
      player2Ready = false;
      transitionScreenStart();
      drawMap();
      transitionScreenEnd();
    }
          // Serial.println(serverGameState);
  } else if(serverGameState == PLAYER1_TURN){
    Serial.println("PLAYER1_TURN State");

    // updatePlayerUI();
    updatePlayerUI();


    //erase previous tank
    drawTank(skyColor, tankPos1, barrelAngle1);
    drawTank(skyColor, tankPos2, barrelAngle2);

    //move
    tankPos1 = controlMove(tankPos1);

    //change barrel angle
    barrelAngle1 = controlAngle(barrelAngle1);

    //draw tank in new position
    drawTank(tankColor[tank1], tankPos1, barrelAngle1);
    drawTank(tankColor[tank2], tankPos2, barrelAngle2);

    //select power
    tankPower1 = selectPower(tankPower1);

    //send tank/power over bluetooth to client

    //spawn projectile and animate, pause game until projectile strikes the ground
    shoot(tankColor[tank1], tankPos1, barrelAngle1, tankPower1);

    //check change turn

  } else if(serverGameState == PLAYER2_TURN){
    Serial.println("PLAYER2_TURN State");

    // updatePlayerUI();

    //bluetooth player
    //get bluetooth date from client
    //get player pos
    //get player angle
    //get player power
    //get projectile pos
    //check collisions

  } else if(serverGameState == GAME_OVER){
    Serial.println("GAME_OVER State");

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
  while(!seeSaw.begin(0x50)){
    Serial.println("ERROR! seesaw not found");
    delay(100);
  }
  Serial.println("seesaw started at 0x50");
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
    if(angle < 180){
      angle += 3; //rotate left
      
      //BLUETOOTH
      sendServerData(); //update client with new angle values
    }
  } else if(!seeSaw.digitalRead(BUTTON_B)){
    Serial.println("B Button Pressed, ROTATE RIGHT");
    if(angle > 0){
      angle -= 3; //rotate right

      //BLUETOOTH
      sendServerData(); //update client with new angle values
    }
  }

  return angle;
}

int controlMove(int xPos){
  //check joystick up and down buttons
  if (!seeSaw.digitalRead(BUTTON_Y)) {
    Serial.println("Y Button Pressed, MOVE LEFT"); //Debugging
    if(xPos > tankSizeX/2){
      xPos -= 3; //move left

      //BLUETOOTH
      sendServerData(); //update client with new move values
    }
  } else if(!seeSaw.digitalRead(BUTTON_A)){
    Serial.println("A Button Pressed, MOVE RIGHT");
    if(xPos < screenWidth - tankSizeX/2){
      xPos += 3; //move right

      //BLUETOOTH
      sendServerData(); //update client with new move values
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

    //BLUETOOTH
    sendServerData(); //update client with new power values
  } else if(M5.BtnB.isPressed()){
    return MEDIUM;

    //BLUETOOTH
    sendServerData(); //update client with new power values
  } else if(M5.BtnC.isPressed()){
    return LARGE;

    //BLUETOOTH
    sendServerData(); //update client with new power values
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
    projX1 = xPos + (projOffset+projSizeR/2) * cos(angleRad); 
    projY1 = tankBarrelY - (projOffset+projSizeR/2) * sin(angleRad);
    
    vx = speed * cos(angleRad);
    vy = -speed * sin(angleRad);

    //calculate projectile motion and check for collisions/hit
    projectileMotion(color, power);

    //BLUETOOTH
    sendServerData(); //update client with new projectile values
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

  //switch turn after collision
  if(serverGameState == PLAYER1_TURN){
    serverGameState = PLAYER2_TURN;
    player1Turn = false;
  } else if(serverGameState == PLAYER2_TURN){
    serverGameState = PLAYER1_TURN;
    player1Turn = true;
  }
}

//check collisions
void checkCollision(Power power){
  //check if out of screen
  if(projX1 > screenWidth || projX1 < 0){
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
  sendServerData(); //update client with new health values
}

void updatePlayerUI() {
  if (updateUI) {
    drawPlayerTitles();

    drawItems();

    // drawBarrelOutline();

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
      M5.Lcd.drawRect(healthBarX1 - 4, healthBarY - 4, healthBarWidth + 8, healthBarHeight + 8, RED);
      M5.Lcd.drawRect(healthBarX2 - 5, healthBarY - 5, healthBarWidth + 10, healthBarHeight + 10, RED);
    } else {
      drawHealth(tankColor[tank1], tankColor[tank2], tankHealth1, tankHealth2);
      M5.Lcd.drawRect(healthBarX2 - 4, healthBarY - 4, healthBarWidth + 8, healthBarHeight + 8, RED);
      M5.Lcd.drawRect(healthBarX2 - 5, healthBarY - 5, healthBarWidth + 10, healthBarHeight + 10, RED);
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

/////////////////////////////////////////////////////////////////////////
//draw functions
/////////////////////////////////////////////////////////////////////////

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

// draw text an a background color
void drawScreenTextWithBackground(String text, int backgroundColor) {
    M5.Lcd.fillScreen(backgroundColor);
    M5.Lcd.setCursor(0,0);
    M5.Lcd.println(text);
}

/////////////////////////////////////////////////////////////////////////
//customize player functions
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

void readClientData()
{
  //read from ble
  //data from player2 client player

  // Read the characteristic's value as a string (which can be written from a client)
  std::string readValue = bleCharacteristic_C->getValue();
  Serial.printf("Client Characteristic Value (player 2 Position): %s\n", readValue.c_str()); //format xxx-yyy
  String str = readValue.c_str();

  // parse the data
  
  //format: serverGameState_color-health=power+tankPosX>projPosX,projPosY
  //ranges: (0-4)_(0-3)-(0-100)=(0-2)+(0-320)>(0-320,0-240)

  //get game state
  int temp = str.substring(0, str.indexOf("_")).toInt();
  if(temp == 0){
    clientGameState = CONNECTING;
  } else if(temp == 1){
    clientGameState = CUSTOMIZE_PLAYER;
  } else if(temp == 2){
    clientGameState = PLAYER1_TURN;
  } else if(temp == 3){
    clientGameState = PLAYER2_TURN;
  } else if(temp == 4){
    clientGameState = GAME_OVER;
  }

  tank2 = str.substring(str.indexOf("_"), str.indexOf("-")).toInt();
  tankHealth2 = str.substring(str.indexOf("-"), str.indexOf("=")).toInt();

  //get power
  temp = str.substring(str.indexOf("="), str.indexOf("+")).toInt();
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

void sendServerData(){
  // write to ble
  // data for player1 Server player

  // format: serverGameState_color-health=power+tankPosX>projPosX,projPosY)
  // ranges: (0-1)_(0-3)-(0-100)=(0-2)+(0-320)>(0-320,0-240)

  // convert to string
  // tank color index
  // FIXME
  String str = String(serverGameState) + "_" + String(tank1) + "-" + 
               String(tankHealth1) + "=" + String(tankPower1) + "+" + 
               String(tankPos1) + ">" + String(projX1) + "," + String(projY1);

  // write the charactistic's value as a string (which can be read by a client)
  bleCharacteristic_S->setValue(str.c_str());
  bleCharacteristic_S->notify();
  Serial.printf("%d Server characteristic value (player 1 Position): %s\n", str.c_str());
}