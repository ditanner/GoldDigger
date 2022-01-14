#include "Serial.h"

enum signalStates { SETUP, GAME, INERT, RESOLVE };
byte signalState = INERT;
byte gameMode = SETUP;

#define SIGNALOFFSET 3

enum blinkModes { BM_NOTSET, CONTROLLER, PLAYER, BANK };
byte blinkMode = BM_NOTSET;

byte numPlayers = 0;

#define NO_SCORE 999
#define DANGER_SCORE 99
#define DIRT_SCORE 0
#define SILVER_SCORE 1
#define GOLD_SCORE 3
#define DIAMOND_SCORE 10

enum dirtTypes { DANGER, DIRT, SILVER, GOLD, DIAMOND };
byte dirtTypeValues[5] = {DANGER_SCORE, DIRT_SCORE, SILVER_SCORE, GOLD_SCORE,
                          DIAMOND_SCORE};
Color dirtTypeColors[5] = {RED, GREEN, MAGENTA, YELLOW, WHITE};

byte faceValues[6] = {GOLD, SILVER, DIRT, DANGER, DIRT, SILVER};

#define PULSE_LENGTH 750

Timer gameTimer;
Timer rotationTimer;
Timer nextRoundTimer;
byte currentSelectedFace = 0;
bool isValueSelected = true;
bool resultWaitingToBroadcast = false;

byte incomingDirt = DIRT;
byte score = 0;
byte newValue = GOLD;

ServicePortSerial sp;

void setup() {
  randomize();
  setColor(OFF);
  setValueSentOnAllFaces(INERT << SIGNALOFFSET);
  gameTimer.never();
  rotationTimer.never();
        nextRoundTimer.set(0);

  sp.begin();
}

void loop() {
  sp.print("s:");
  sp.print(signalState);
  sp.print(" g:");
  sp.print(gameMode);
  sp.print(" i:");
  sp.println(incomingDirt);
  if (signalState < INERT) {
    sendLoop();
  } else if (INERT == signalState) {
    inertLoop();
  } else if (RESOLVE == signalState) {
    resolveLoop();
  }

  if (SETUP == gameMode) {
    setupLoop();
    drawSetup();
  } else if (GAME == gameMode && INERT <= signalState) {
    gameLoop();
    drawGame();
  }

  // dump button data
  buttonSingleClicked();
  buttonDoubleClicked();
  buttonPressed();
  buttonMultiClicked();

  byte sendData = 0;

  sendData = (signalState << SIGNALOFFSET);
  if (GAME == gameMode && resultWaitingToBroadcast && CONTROLLER == blinkMode) {
    sendData = sendData + faceValues[currentSelectedFace];
  }
  // sendData = sendData + gameMode
  setValueSentOnAllFaces(sendData);
}

#pragma region message loops

void inertLoop() {
  bool sendReceived = false;
  byte val = 0;
  FOREACH_FACE(f) {
    if (!sendReceived) {
      if (!isValueReceivedOnFaceExpired(f)) { // a neighbor!
        val = getSignalState(getLastValueReceivedOnFace(f));
        if (val < INERT) { // a neighbor saying SEND!
          sendReceived = true;
          //          sp.print("SEND Received - moving to mode ");
          //          sp.println(val);
          if (GAME == val & GAME == gameMode) {
            incomingDirt = getPayload(getLastValueReceivedOnFace(f));
            sp.print("p:");
            sp.print(getPayload(getLastValueReceivedOnFace(f)));
            sp.print(" i:");
            sp.println(incomingDirt);
            signalState = RESOLVE;
            sendReceived = false;
          } else {
            signalState = val;
            gameMode = val;
            if (GAME == signalState) {
              byte neighbours = countNeighbours();
              if (1 < neighbours) {
                blinkMode = PLAYER;
              } else {
                blinkMode = BANK;
              }
            }
          }
        }
      }
    }
  }
}

void sendLoop() {
  bool allSend = true;
  byte countReceivers = 0;
  // look for neighbors who have not heard the GO news
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) { // a neighbor!
      countReceivers++;
      byte val = getSignalState(getLastValueReceivedOnFace(f));
      if (val == INERT) { // This neighbor doesn't know it's SEND time. Stay in
                          // SEND, uless we're ignoring it it
        allSend = false;
      }
    }
  }
  if (allSend && countReceivers > 0) {
    signalState = RESOLVE;
  }
}

void resolveLoop() {
  bool allResolve = true;

  // look for neighbors who have not moved to RESOLVE
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) { // a neighbor!
      byte val = getSignalState(getLastValueReceivedOnFace(f));
      if (val < INERT) { // This neighbor isn't in RESOLVE. Stay in RESOLVE
        allResolve = false;
      }
    }
  }
  if (allResolve) {
    signalState = INERT;
    if (resultWaitingToBroadcast) {
      resultWaitingToBroadcast = false;
    }
  }
}

byte getSignalState(byte data) { return ((data >> SIGNALOFFSET)); }

byte getPayload(byte data) { return (data & ((1 << SIGNALOFFSET) - 1)); }

#pragma endregion

#pragma region Game Loops
void setupLoop() {

  if (buttonDoubleClicked()) {
    signalState = GAME;
    gameMode = GAME;
    blinkMode = CONTROLLER;
    //    numPlayers = neighbours;
  }
}

void gameLoop() {
  if (CONTROLLER == blinkMode) {
    if (gameTimer.isExpired()) {
      isValueSelected = true;
      resultWaitingToBroadcast = true;
      signalState = GAME;
      rotationTimer.never();
      gameTimer.never();
      nextRoundTimer.set(1000);
      newValue = incrementDirt(faceValues[currentSelectedFace]);
    } else if (rotationTimer.isExpired()) {
      currentSelectedFace = (currentSelectedFace + 1) % 6;
      rotationTimer.set(500);
    } else {
      if (buttonSingleClicked() && !resultWaitingToBroadcast &&
          nextRoundTimer.isExpired()) {
        faceValues[currentSelectedFace] = newValue;
        // check for diamond....

        currentSelectedFace = 0;
        gameTimer.set(500 * (random(5) + 4));
        rotationTimer.set(500);
        isValueSelected = false;
        nextRoundTimer.never();
      }
    }
  }
}
#pragma endregion

#pragma region helper functions
byte incrementDirt(byte currentDirt) {
  switch (currentDirt) {
  case DANGER:
    return DANGER;
    break;
  case DIRT:
    return SILVER;
    break;
  case SILVER:
    return GOLD;
    break;
  case GOLD:
    return DANGER;
    break;
  case DIAMOND:
    return DANGER;
    break;
  }
  //  return DANGER //shouldnt hit this.
}

byte countNeighbours() {
  byte neighbours = 0;
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      neighbours++;
    }
  }
  return neighbours;
}
#pragma endregion

#pragma region Draw Methods
void drawSetup() { setColor(YELLOW); }

void drawGame() {
  switch (blinkMode) {
  case CONTROLLER:
    if (isValueSelected) {
      FOREACH_FACE(f) {
        if (currentSelectedFace != f) {
          setColorOnFace(dim(dirtTypeColors[faceValues[f]], 50), f);
        } else {
          setColorOnFace(dirtTypeColors[faceValues[f]], f);
        }
      }
    } else {
      drawRotation(faceValues);
    }
    break;
  case PLAYER:
    setColor(dirtTypeColors[incomingDirt]);
  }
}

void drawRotation(byte values[]) {
  // setColor(MAGENTA);

  // get progress from 0 - MAX
  int pulseProgress = millis() % PULSE_LENGTH;

  // transform that progress to a byte (0-255)
  byte pulseMapped = map(pulseProgress, 0, PULSE_LENGTH, 0, 255);

  // transform that byte with sin
  byte dimness = 0;

  // set color
  FOREACH_FACE(f) {
    dimness = sin8_C(pulseMapped - (42 * f));
    setColorOnFace(dim(dirtTypeColors[faceValues[f]], dimness), f);
  }
}
#pragma endregion
