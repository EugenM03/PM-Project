#include <Adafruit_GFX.h> // display SPI
#include <Adafruit_ILI9341.h>
#include <Wire.h>  // biblioteca I2C
#include <Rtc_Pcf8563.h> // RTC
#include <string.h>

// configurare pini
#define MOSI       11
#define TFT_CS      10
#define TFT_RST     -1
#define TFT_DC       9
#define JOY_VERT    A0
#define JOY_HORZ    A1
#define JOY_BUTTON   6
uint8_t buzzerPin = 8; // D8 (PB0), PWM Out

// obiecte si timp
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST); // biblioteca desenare init
Rtc_Pcf8563 rtc;
int lastMinute;
unsigned long lastMoveTime = 0;

// constante joc si dimensiuni
#define SNAKE_WIN_LENGTH 16
#define GRID_SIZE 8 // marime default de desenare a obiectelor
#define TUTORIAL 0 // nivel 0 (fara obstacole)
#define LEVEL 1   // nivel 1 (cu obstacole)
#define UNDEFINED -1  // folosit in afara jocului 
const int screenWidth = 320;
const int screenHeight = 240;

// sunete si melodii, buzzer
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
int win_melody[6] = {NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4};
int lose_melody[6] = {NOTE_F4, NOTE_E4, NOTE_D4, NOTE_C4};
int noteDuration = 500;
bool melodyPlayed = false;

// automat de stari
enum STATE { MAIN_MENU_STATE, LEVEL_CHOOSE_STATE, GAME_STATE, GAME_INFO_STATE, FINISHED_GAME_STATE };
STATE global_state = MAIN_MENU_STATE;

struct booster {
  char name[10];
  int positionX, positionY;
};
struct menu_item {
  char name[20];
  int position;
};

// obstacole
// (x,y) - desenarea obstacolelor; w - width; h - height
struct Obstacle {
  int x, y, w, h;
};

// nivel obstacol 1
Obstacle patternLevel[4] = {
  {64, 64, 48, 48},
  {208, 64, 48, 48},
  {64, 144, 48, 48},
  {208, 144, 48, 48}
};
int countSimple = 4;

// nivel obstacol 2
Obstacle patternLevelHard[6] = {
  {0, 40, 120, 30}, 
  {200, 80, 120, 30},
  {60, 130, 40, 40},
  {220, 130, 40, 40},
  {0, 180, 150, 20},
  {190, 200, 100, 20}
};
int countComplex = 6;

Obstacle *currentMap = patternLevel;
int currentCount = 4;

// variabile sarpe si miscare
int snakeX[SNAKE_WIN_LENGTH + 2], snakeY[SNAKE_WIN_LENGTH + 2];
int dirX = 1, dirY = 0;
int currentSnakeLength = 4; // startLength
// int winningLength = SNAKE_WIN_LENGTH;

// variabile joc (scor, fructe, nivel)
int totalScore = 0;
int startLength = 4;
int gameSpeed = 150;
int randomNumber;
int maximumFruits = 3;
int fruitX[3], fruitY[3];
int totalFruits = 0;
int currentLevel;
bool win = false;

// logica boostere
unsigned long boosterExpireTime = 0; // efect booster temporar
bool isSlowed = false;
const int NORMAL_SPEED = 150;
const int SLOW_SPEED = 250;
const long BOOSTER_DURATION = 10000; // 10 sec slowdown
int maximumBoosters = 1;
int currentBoosters = 0;
bool destroyBoosterTaken = false;
booster booster;

// logica meniu
struct menu_item menu_items[3] = { {"New Game", 0}, {"Game Info", 1}};
int currentButton = 0;
int menuItemsLength = 2;

void setup() {
  Serial.begin(9600);
  Wire.begin();
  
  tft.begin();
  tft.setRotation(3);

  pinMode(JOY_BUTTON, INPUT_PULLUP);

  rtc.initClock();
  rtc.getTime();
  lastMinute = rtc.getMinute();

  drawMenuBackground();
}

void loop() {
  chooseMapBasedOnTime();
  int posX = analogRead(JOY_HORZ);
  int posY = analogRead(JOY_VERT);
  int click = digitalRead(JOY_BUTTON);

  // Serial.print("Valoare: ");
  // Serial.println(posY);
  // Serial.println();

  // choose menu option
  // if (posY > 600) {
  //   currentButton = (currentButton + 1) % menuItemsLength;
  //   delay(200);
  // } else if (posY < 400) {
  //   currentButton = (currentButton - 1);
  //   if (currentButton < 0) currentButton = menuItemsLength - 1;
  //   delay(200);
  // }

  switch (global_state) {
    case MAIN_MENU_STATE:
      // logica de selectare butoane
      if (posY > 600) { currentButton = (currentButton + 1) % menuItemsLength; delay(500); }
      else if (posY < 400) { currentButton = (currentButton - 1 + menuItemsLength) % menuItemsLength; delay(500); }
      drawMainMenu(click);
      break;

    case LEVEL_CHOOSE_STATE:
      if (posY > 600) { currentButton = (currentButton + 1) % 3; delay(500); }
      else if (posY < 400) { currentButton = (currentButton - 1 + 3) % 3; delay(500); }
      drawLevelMenu(click);
      break;
    case GAME_INFO_STATE:
      drawGameInfoMenu(click);
      break;
    case GAME_STATE:
      if (isSlowed && millis() > boosterExpireTime) {
          isSlowed = false;
          gameSpeed = NORMAL_SPEED; // timeout slow down
      }
      if (millis() - lastMoveTime >= gameSpeed) {
        updateGame();
        lastMoveTime = millis();
      }

      break;

    case FINISHED_GAME_STATE:
      drawGameEndScene(click); 
  }
}

void chooseMapBasedOnTime() {
  rtc.getTime();

  int current_minute = rtc.getMinute();
  int elapsed = (current_minute - lastMinute + 60) % 60;

  if (elapsed >= 2 && global_state != GAME_STATE) {
    lastMinute = current_minute;
    Serial.println("mapa schimbata");
    Serial.print("Timp RTC: ");
    Serial.print(rtc.getHour());
    Serial.print(":");
    Serial.print(rtc.getMinute());
    Serial.print(":");
    Serial.println(rtc.getSecond());

    if (currentCount == countSimple) {
      currentCount = countComplex;
      currentMap = patternLevelHard;
    } else {
      currentCount = countSimple;
      currentMap = patternLevel;
    }
  }
}


void startGame() {
  win = false;
  melodyPlayed = false;
  currentSnakeLength = startLength;
  totalFruits = 0;
  destroyBoosterTaken = false;
  totalScore = 0;
  currentBoosters = 0;
  booster.positionX = -100;
  booster.positionY = -100;
  dirX = 1;
  dirY = 0;
  gameSpeed = 150;

  drawObstacles();

  int startX = (screenWidth / 2) / GRID_SIZE * GRID_SIZE;
  int startY = (screenHeight / 2) / GRID_SIZE * GRID_SIZE;

  snakeX[3] = startX; snakeY[3] = startY;

  for (int i = 0; i < startLength; i++) {
    snakeX[i] = startX - i * GRID_SIZE;
    snakeY[i] = startY;
  }

  for (int i = 0; i < maximumFruits; i++) {
    fruitX[i] = -100;
    fruitY[i] = -100;
  }

  drawSnakeInit();
}

void updateGame() {
  handleSnakeMoving();

  tft.fillRect(snakeX[currentSnakeLength - 1], snakeY[currentSnakeLength - 1], GRID_SIZE, GRID_SIZE, ILI9341_BLACK);
  
  for (int i = 1; i < currentSnakeLength; i++) {
    if (snakeX[0] == snakeX[i] && snakeY[0] == snakeY[i]) {
      delay(250);

      global_state = FINISHED_GAME_STATE;
      drawMenuBackground();
      currentButton = 0;
      currentLevel = UNDEFINED;

      return;
    }
  }

  for (int i = currentSnakeLength - 1; i > 0; i--) {
    snakeX[i] = snakeX[i - 1];
    snakeY[i] = snakeY[i - 1];
  }

  snakeX[0] += dirX * GRID_SIZE;
  snakeY[0] += dirY * GRID_SIZE;

  if (snakeX[0] >= screenWidth - GRID_SIZE) snakeX[0] = GRID_SIZE;
  if (snakeX[0] < GRID_SIZE) snakeX[0] = screenWidth - GRID_SIZE - (screenWidth % GRID_SIZE);
  if (snakeY[0] >= screenHeight - GRID_SIZE) snakeY[0] = GRID_SIZE;
  if (snakeY[0] < GRID_SIZE) snakeY[0] = screenHeight - GRID_SIZE - (screenHeight % GRID_SIZE);

  for (int i = 0; i < totalFruits; i++) {
    if (snakeX[0] == fruitX[i] && snakeY[0] == fruitY[i]) {
      totalScore++;
      
      if (currentSnakeLength < SNAKE_WIN_LENGTH) {
        tone(buzzerPin, NOTE_D4, noteDuration / 3);

        currentSnakeLength++;
        // totalScore += random(50, 80);
        totalScore++;
      }
      
      spawnFruit(i);
    }
  }

  if (checkObstacleCollision(snakeX[0], snakeY[0]) == true) {
    delay(250);

    global_state = FINISHED_GAME_STATE;
    drawMenuBackground();
    currentButton = 0;
    currentLevel = UNDEFINED;

    return;
  }

  if (snakeX[0] == booster.positionX && snakeY[0] == booster.positionY) {
    currentBoosters = 0;
    tone(buzzerPin, NOTE_C4, 100);

    if (strcmp(booster.name, "destroy") == 0) {
        destroyBoosterTaken = true;
        destroyObstacles();
    } else if (strcmp(booster.name, "slow") == 0) {
        isSlowed = true;
        boosterExpireTime = millis() + BOOSTER_DURATION; // timp expirare
        gameSpeed = SLOW_SPEED;
    }
  }

  long booster_randomNumber = random(0, 100);
  if (booster_randomNumber <= 1) {
    spawnBooster();
  }
  
  if (totalFruits < maximumFruits) {
    randomNumber = random(0, 100);
    if (randomNumber <= 5) {
      spawnFruit(totalFruits);
      totalFruits++;
    }
  }

  if (currentSnakeLength == SNAKE_WIN_LENGTH) {
    delay(250);

    global_state = FINISHED_GAME_STATE;
    win = true;
    drawMenuBackground();
    currentButton = 0;
    currentLevel = UNDEFINED;

    return;
  }

  tft.fillRect(snakeX[0], snakeY[0], GRID_SIZE, GRID_SIZE, ILI9341_GREEN);
}

void handleSnakeMoving() {
  int horz = analogRead(JOY_HORZ);
  int vert = analogRead(JOY_VERT);

  int center = 512;
  int threshold = 120;

  int dx = horz - center;
  int dy = vert - center;

  if (abs(dx) > abs(dy)) {
    if (dx > threshold && dirX != -1) {
      dirX = 1; dirY = 0;
    } else if (dx < -threshold && dirX != 1) {
      dirX = -1; dirY = 0;
    }
  } else {
    if (dy > threshold && dirY != -1) {
      dirX = 0; dirY = 1;
    } else if (dy < -threshold && dirY != 1) {
      dirX = 0; dirY = -1;
    }
  }
}


bool checkPositionAvailability(int x, int y) {
  if (currentLevel == LEVEL && !destroyBoosterTaken) {
    for (int i = 0; i < currentCount; i++) {
      if (x < currentMap[i].x + currentMap[i].w &&
          x + GRID_SIZE > currentMap[i].x &&
          y < currentMap[i].y + currentMap[i].h &&
          y + GRID_SIZE > currentMap[i].y) {
        return false;
      }
    }
  }

  for (int i = 0; i < currentSnakeLength; i++) {
    if (x == snakeX[i] && y == snakeY[i]) {
      return false; 
    }
  }

  return true; 
}

bool checkObstacleCollision(int x, int y) {
  if (currentLevel != LEVEL || destroyBoosterTaken) return false;
  for (int i = 0; i < currentCount; i++) {
    if (x >= currentMap[i].x && x < currentMap[i].x + currentMap[i].w &&
        y >= currentMap[i].y && y < currentMap[i].y + currentMap[i].h) {
      return true;
    }
  }
  
  return false;
}


void spawnFruit(int index) {
  if (index < 0 || index >= maximumFruits) return;

  int minGridX = 2;
  int maxGridX = (screenWidth / GRID_SIZE) - 2;
  int minGridY = 2;
  int maxGridY = (screenHeight / GRID_SIZE) - 2;

  int newX, newY;

  do {
    newX = random(minGridX, maxGridX) * GRID_SIZE;
    newY = random(minGridY, maxGridY) * GRID_SIZE;
  } while (!checkPositionAvailability(newX, newY));

  fruitX[index] = newX;
  fruitY[index] = newY;

  tft.fillRect(fruitX[index], fruitY[index], GRID_SIZE, GRID_SIZE, ILI9341_RED);
}

void spawnBooster() {
  if (maximumBoosters == currentBoosters) return;
  
  currentBoosters++;

  int minGridX = 2;
  int maxGridX = (screenWidth / GRID_SIZE) - 2;
  int minGridY = 2;
  int maxGridY = (screenHeight / GRID_SIZE) - 2;

  int newX, newY;

  do {
    newX = random(minGridX, maxGridX) * GRID_SIZE;
    newY = random(minGridY, maxGridY) * GRID_SIZE;
  } while (!checkPositionAvailability(newX, newY));

  booster.positionX = newX;
  booster.positionY = newY;
  booster.name[0] = 0;

  long randomNameNum = random(0, 2);
  if (randomNameNum == 0) {
    strcpy(booster.name, "destroy");
    tft.fillRect(booster.positionX, booster.positionY, GRID_SIZE, GRID_SIZE, ILI9341_YELLOW);
  } else {
    strcpy(booster.name, "slow");
    tft.fillRect(booster.positionX, booster.positionY, GRID_SIZE, GRID_SIZE, ILI9341_BLUE);
  }
}


void drawMenuBackground() {
  tft.fillScreen(ILI9341_BLACK);
  tft.drawRect(10, 10, screenWidth - 20, screenHeight - 20, ILI9341_GREEN);
  tft.drawRect(12, 12, screenWidth - 24, screenHeight - 24, ILI9341_BLACK);
}

void drawMainMenu(int clicked) {
  if (global_state != MAIN_MENU_STATE) return;

  tft.setCursor(100, 50);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(3);
  tft.print("SNAKE C");

  tft.setCursor(40, 100);
  tft.setTextColor(currentButton == 0 ? ILI9341_WHITE : ILI9341_GREEN);
  tft.setTextSize(2);
  tft.print("New Game ");

  tft.setCursor(40, 130);
  tft.setTextColor(currentButton == 1 ? ILI9341_WHITE : ILI9341_GREEN);
  tft.print("Game Info");

  // tft.setCursor(40, 160);
  // tft.setTextColor(currentButton == 2 ? ILI9341_WHITE : ILI9341_GREEN);
  // tft.print("Exit     ");

  if (clicked == LOW) {
    delay(250);
    
    switch (currentButton) {
      case 0:
        global_state = LEVEL_CHOOSE_STATE;
        break;
      
      case 1:
        global_state = GAME_INFO_STATE;
        break;

      case 2:
        return;
        
      default:
        break;
    }

    currentButton = 0;
    drawMenuBackground();
  }
}

void drawLevelMenu(int clicked) {
  if (global_state != LEVEL_CHOOSE_STATE) return;

  tft.setCursor(100, 50);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(3);
  tft.print("SNAKE C");

  tft.setCursor(40, 100);
  tft.setTextColor(currentButton == 0 ? ILI9341_WHITE : ILI9341_GREEN);
  tft.setTextSize(2);
  tft.print("Tutorial");

  tft.setCursor(40, 130);
  tft.setTextColor(currentButton == 1 ? ILI9341_WHITE : ILI9341_GREEN);
  tft.print("Level");

  tft.setCursor(40, 160);
  tft.setTextColor(currentButton == 2 ? ILI9341_WHITE : ILI9341_GREEN);
  tft.print("Back");

  if (clicked == LOW) {
    delay(250);
    drawMenuBackground();
    
    if (currentButton == 2) {
      global_state = MAIN_MENU_STATE;
      currentButton = 0;
    } else {
      global_state = GAME_STATE;

      if (currentButton == 0) {
        currentLevel = TUTORIAL;
      } else if (currentButton == 1) {
        currentLevel = LEVEL;
      }

      currentButton = 0;
      startGame();
    }
  }
}

void drawGameInfoMenu(int clicked) {
  if (global_state != GAME_INFO_STATE) return;

  // Serial.println("drawGameInfoMenu");

  tft.setCursor(20, 10);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.print("Go Back");
  
  tft.setCursor(100, 50);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(3);
  tft.print("SNAKE C");

  tft.setCursor(60, 100);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);
  tft.fillRect(40, 100, GRID_SIZE, GRID_SIZE, ILI9341_RED);
  tft.print("Eat Fruits");

  tft.setCursor(60, 150);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);
  tft.fillRect(40, 150, GRID_SIZE, GRID_SIZE, ILI9341_YELLOW);
  tft.print("Destroy obstacles");

  tft.setCursor(60, 200);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);
  tft.fillRect(40, 200, GRID_SIZE, GRID_SIZE, ILI9341_BLUE);
  tft.print("Slow down");

  if (clicked == LOW) {
    delay(250);

    currentButton = 0;
    global_state = MAIN_MENU_STATE;
    drawMenuBackground();
  }
}

void drawGameEndScene(int clicked) {
  if (global_state != FINISHED_GAME_STATE) return;

  char message[10];
  if (win == true) {
    strcpy(message, "You Win!");
  } else {
    strcpy(message, "You Lost!");
  }

  tft.setCursor(40, 100);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.print(message);

  tft.setCursor(40, 140);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.print("Your score: ");
  tft.print(totalScore);

  if (win == true) {
    if (melodyPlayed == false) {
      for (int i = 0; i < 4; i++) {
        tone(buzzerPin, win_melody[i], noteDuration);
        delay(noteDuration / 5);
      }
    }

    melodyPlayed = true;
  } else {
    if (melodyPlayed == false) {
      for (int i = 0; i < 4; i++) {
        tone(buzzerPin, lose_melody[i], noteDuration);
        delay(noteDuration / 5);
      }
    }

    melodyPlayed = true;
  }

  if (clicked == LOW) {
    delay(250);

    currentButton = 0;
    global_state = MAIN_MENU_STATE;
    drawMenuBackground();
  } 
}


void drawSnakeInit() {
  for (int i = 0; i < currentSnakeLength; i++) {
    tft.fillRect(snakeX[i], snakeY[i], GRID_SIZE, GRID_SIZE, ILI9341_GREEN);
  }
}

void drawObstacles() {
  if (destroyBoosterTaken || currentLevel != LEVEL) return;

  for (int i = 0; i < currentCount; i++) {
    tft.fillRect(currentMap[i].x, currentMap[i].y, currentMap[i].w, currentMap[i].h, ILI9341_MAROON);
  }
}

void destroyObstacles() {
  if (!destroyBoosterTaken || currentLevel == TUTORIAL) return;

  for (int i = 0; i < currentCount; i++) {
    tft.fillRect(currentMap[i].x, currentMap[i].y, currentMap[i].w, currentMap[i].h, ILI9341_BLACK);
  }
}
