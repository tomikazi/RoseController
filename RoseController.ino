#include <ESP32GizmoDefault.h>

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ESP32Servo.h>

#define ROSE_CONTROLLER "RoseController"
#define SW_UPDATE_URL   "http://iot.vachuska.com/RoseController.ino.bin"
#define SW_VERSION      "2023.10.16.001"

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#define START_POS     0
#define PULL_POS     60
#define MOVE_MS     500

typedef struct Petal {
    int pin;
    Servo servo;
    int targetPos;
    uint32_t moveStart;
} Petal;

Petal petals[] = {
        Petal{.pin = 19, .servo = Servo{}, .targetPos = START_POS, .moveStart = 0},
        Petal{.pin = 18, .servo = Servo{}, .targetPos = START_POS, .moveStart = 0},
        Petal{.pin =  5, .servo = Servo{}, .targetPos = START_POS, .moveStart = 0},
        Petal{.pin = 17, .servo = Servo{}, .targetPos = START_POS, .moveStart = 0},
        Petal{.pin = 16, .servo = Servo{}, .targetPos = START_POS, .moveStart = 0}
};
uint8_t remainingPetals = ARRAY_SIZE(petals);
Petal *currentPetal = NULL;

typedef struct Light {
    int pin;
    bool on;
} Light;

Light spotLight = {.pin = 25, .on = false};
Light pixieLight = {.pin = 26, .on = false};
Light domeLight = {.pin = 27, .on = false};

WebSocketsServer wsServer(81);

void setup() {
    gizmo.beginSetup(ROSE_CONTROLLER, SW_VERSION, PASSKEY);
    gizmo.setUpdateURL(SW_UPDATE_URL, onUpdate);
    gizmo.httpServer()->on("/spot", handleSpot);
    gizmo.httpServer()->on("/dome", handleDome);
    gizmo.httpServer()->on("/pixie", handlePixie);
    gizmo.httpServer()->on("/petal", handlePetal);
    gizmo.setupWebRoot();
    setupWebSocket();

    setupServos();
    setupSwitches();

    gizmo.endSetup();
}

void onUpdate() {
    for (int i = 0; i < ARRAY_SIZE(petals); i++) {
        petals[i].servo.detach();
    }
}

void setupSwitches() {
    pinMode(spotLight.pin, OUTPUT);
    pinMode(pixieLight.pin, OUTPUT);
    pinMode(domeLight.pin, OUTPUT);
}

void setupServos() {
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    for (int i = 0; i < ARRAY_SIZE(petals); i++) {
        petals[i].servo.setPeriodHertz(50);
        petals[i].servo.attach(petals[i].pin, 1000, 2000);
        petals[i].servo.write(START_POS);
    }
}

void setupWebSocket() {
    wsServer.begin();
    wsServer.onEvent(webSocketEvent);
    Serial.println("WebSocket server setup.");
}

void finishConnection() {
    Serial.printf("%s is ready\n", ROSE_CONTROLLER);
    gizmo.led(false);
}

void loop() {
    if (gizmo.isNetworkAvailable(finishConnection)) {
    }
    wsServer.loop();
    handleServos();
}

void handleSpot() {
    WebServer *server = gizmo.httpServer();
    controlLight(&spotLight, !spotLight.on);
    server->send(200, "text/plain", spotLight.on  ? "on" : "off");
    broadcastState();
}

void handleDome() {
    WebServer *server = gizmo.httpServer();
    controlLight(&domeLight, !domeLight.on);
    server->send(200, "text/plain", domeLight.on  ? "on" : "off");
    broadcastState();
}

void handlePixie() {
    WebServer *server = gizmo.httpServer();
    controlLight(&pixieLight, !pixieLight.on);
    server->send(200, "text/plain", pixieLight.on  ? "on" : "off");
    broadcastState();
}

void handlePetal() {
    WebServer *server = gizmo.httpServer();
    if (remainingPetals == 0 && !spotLight.on && !domeLight.on && !pixieLight.on) {
        resetState();
    } else {
        dropNextPetal();
    }
    char rem[8];
    snprintf(rem, 7, "%d", remainingPetals);
    server->send(200, "text/plain", rem);
    broadcastState();
}

void handleServos() {
    if (!currentPetal) {
        return;
    }

    if (!currentPetal->moveStart && currentPetal->targetPos == PULL_POS) {
        Serial.printf("Retracting to %d\n", currentPetal->targetPos);
        currentPetal->servo.write(currentPetal->targetPos);
        currentPetal->moveStart = millis();
    } else if (currentPetal->moveStart && currentPetal->moveStart < millis() - MOVE_MS &&
               currentPetal->targetPos == PULL_POS) {
        currentPetal->targetPos = START_POS;
        Serial.printf("Returning to %d\n", currentPetal->targetPos);
        currentPetal->servo.write(currentPetal->targetPos);
        currentPetal->moveStart = millis();
    } else if (currentPetal->moveStart && currentPetal->moveStart < millis() - MOVE_MS) {
        Serial.printf("Petal dropped\n");
        currentPetal->moveStart = 0;
        currentPetal = NULL;
    }
}

void dropNextPetal() {
    if (currentPetal || remainingPetals == 0) {
        return;
    }
    remainingPetals--;
    currentPetal = &petals[remainingPetals];
    Serial.printf("Drop petal #%d; %d to go\n", petals[remainingPetals].pin, remainingPetals);
    currentPetal->targetPos = PULL_POS;
}

void controlLight(Light *light, bool on) {
    light->on = on;
    digitalWrite(light->pin, on);
    Serial.printf("Turn %s %d light\n", light->on ? "on" : "off", light->pin);
}

void resetState() {
    Serial.printf("Reset state\n");
    controlLight(&spotLight, false);
    controlLight(&pixieLight, false);
    controlLight(&domeLight, false);
    remainingPetals = ARRAY_SIZE(petals);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected.\n", num);
            break;
        case WStype_CONNECTED:
            Serial.printf("[%u] Connected.\n", num);
            break;
        case WStype_TEXT:
            char cmd[128];
            cmd[0] = '\0';
            strncat(cmd, (char *) payload, length);
            handleWsCommand(cmd);
            break;
        default:
            Serial.printf("Unhandled message type\n");
            break;
    }
}

void processCommand(char *cmd) {
    if (!strcmp(cmd, "get")) { ;
    } else if (!strncmp(cmd, "dropNextPetal ", 14)) {
        dropNextPetal();

    } else if (!strncmp(cmd, "spotLight ", 9)) {
        controlLight(&spotLight, !strcmp(cmd + 10, "on"));
    } else if (!strncmp(cmd, "pixieLight ", 10)) {
        controlLight(&pixieLight, !strcmp(cmd + 11, "on"));
    } else if (!strncmp(cmd, "domeLight ", 9)) {
        controlLight(&domeLight, !strcmp(cmd + 10, "on"));

    } else if (!strncmp(cmd, "allLights ", 9)) {
        controlLight(&spotLight, !strcmp(cmd + 10, "on"));
        controlLight(&pixieLight, !strcmp(cmd + 10, "on"));
        controlLight(&domeLight, !strcmp(cmd + 10, "on"));

    } else if (!strncmp(cmd, "resetState ", 10)) {
        resetState();
    }
}

void handleWsCommand(char *cmd) {
    processCommand(cmd);
    broadcastState();
}

#define STATUS "{\"petals\":%d,\"spotlight\":%s,\"pixielight\":%s,\"domelight\":%s," \
    "\"name\":\"RoseController\",\"version\":\"" SW_VERSION "\"}"

#define T(v) (v.on ? "true" : "false")

void broadcastState() {
    char status[1024];
    status[0] = '\0';

    snprintf(status, 1023, STATUS, remainingPetals, T(spotLight), T(pixieLight), T(domeLight));
    wsServer.broadcastTXT(status);
}
