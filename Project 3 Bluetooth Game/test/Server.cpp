#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <M5Core2.h>
#include <Adafruit_Seesaw.h>

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

const float gravity = 0.5;
const float projOffset = 20;
float projX = 0;
float projY = 300; //init beyond screen
int projSizeR = 0;
float vx = 0;
float vy = 0;
float speed = 0;

/////////////////////////////////////////////////////////////////////////
//functions
/////////////////////////////////////////////////////////////////////////
//joystick functions
void setupJoystick();

//draw functions
void drawMap();
void drawTank(uint16_t color,int xPos,int angle);
void drawHealth(uint16_t color1, uint16_t color2, int hp1, int hp2);
void drawGameOver(bool whoWon);

//game functions
void serverGameManager();

//tank control functions
int controlAngle(int angle);
int controlMove(int xPos);

//projectile/shoot functions
Power selectPower(Power prevPower);
void shoot(uint16_t color, int xPos, int angle, Power power);
void projectileMotion(uint16_t color);
void checkCollision(Power power);

//health functions

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
    while((projY <= floorHeight) & (projX <= sWidth) & (projX >= 0)){
      //draw projectile in new position
      projectileMotion(color);
    }

    //check collision
    checkCollision(power);
  }
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

//check collisions
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

/////////////////////////////////////////////////////////////////////////
//health functions
/////////////////////////////////////////////////////////////////////////

void checkHealth(){

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