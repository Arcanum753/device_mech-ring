#include "core_web/FSWebServerLib.h"

#include "core_json/core_json.h"

#include "device_mech-ring.h"
#include "core_sys/eertos.h"
#include "core_sys/core_sys.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/ErriezSerialTerminal.h"
#include "core_led/core_led.h"

#if defined(ESP8266)
#include <avr/pgmspace.h>
#endif

// ============================================================
// Глобальные объекты и переменные
// ============================================================

DPDR GoToTaskAfterStepRing = Idle_task;

volatile uint16_t step_time = RINGMECH_SPEED_DEFAULT;

// Время последнего шага (для детектора занятости ядра в MechMoveStepDown)
static uint32_t _lastStepDownUs = 0;

// Паттерн светодиодной индикации ошибки (кассета модуля)
static const char patDevError[] PROGMEM = LED_PATTERN_DEV_ERROR;

// ============================================================
// Светодиодная индикация ошибки устройства
// ============================================================
void CLASS_DEVICE_RINGMECH::ledMacrosRingMechError() {
    ledSetState(LED_PRIO_DEV, patDevError, -1);
}

// ============================================================
// Время из источника
// ============================================================
time_t CLASS_DEVICE_RINGMECH::getCurrentTime() {
    // Единый источник времени — подсистема виртуального времени core_sys.
    return core_sys.timeNow();
}

// ============================================================
// Инициализация GPIO
// ============================================================
void CLASS_DEVICE_RINGMECH::MechInitGPIOs() {
    pinMode(RINGMECH_STEP, OUTPUT);
    pinMode(RINGMECH_EN,   OUTPUT);

    pinMode(RINGMECH_SENS, INPUT_PULLUP);
    digitalWrite(RINGMECH_STEP, LOW);
    digitalWrite(RINGMECH_EN, HIGH);
}

// ============================================================
// Генератор шага A4988
// ============================================================
void CLASS_DEVICE_RINGMECH::MechMoveStepDown() {
    digitalWrite(RINGMECH_STEP, HIGH);
    uint32_t stepDelayMs = step_time;

    // Проверка занятости ядра: если предыдущий цикл шага (HIGH->LOW->задача)
    // занял заметно больше штатного времени — планировщик/таймеры задерживались
    // (сеть, веб, чтение FS). Тогда следующую фазу откладываем, механизм
    // «встаёт» на паузу; логика при этом не меняется.
    // bool coreBusy = false;
    // if (_lastStepDownUs != 0) {
    //     uint32_t gapMs = (micros() - _lastStepDownUs) / 1000UL;
    //     if ((gapMs > RINGMECH_STEP_GAP_BUSY_MS) && (gapMs < RINGMECH_STEP_GAP_RESET_MS)) {
    //         coreBusy = true;
    //     }
    // }
    // if (coreBusy) {
    //     // Пауза из-за занятости ядра. Сбрасываем базу отсчёта: собственная
    //     // пауза не должна восприниматься как новая занятость на следующем шаге.
    //     stepDelayMs = RINGMECH_STEP_BUSY_MS;
    //     _lastStepDownUs = 0;
    // } 
    // else {
    //     _lastStepDownUs = micros();
    // }
    SetTimerTask(MechMoveStepUp, stepDelayMs);
}
void CLASS_DEVICE_RINGMECH::MechMoveStepUp() {
    digitalWrite(RINGMECH_STEP, LOW);
    SetTimerTask(GoToTaskAfterStepRing, step_time);
}

// ============================================================
// Хоминг — поиск метки сенсора
// ============================================================

void CLASS_DEVICE_RINGMECH::MechHomeSetup() {
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    if (SENS_TRIGGERED) { MechHomeEndOk(); return; }
    device_mech_ring._ringStatus = RING_STATUS_HOMING;
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, LOW);
    device_mech_ring._mechControlSteps = 0;
    GoToTaskAfterStepRing = MechHomeTask;
    SetTask(MechMoveStepDown);
    step_time = RINGMECH_SPEED_DEFAULT;
}

void CLASS_DEVICE_RINGMECH::MechHomeTask() {
    if (device_mech_ring._ringStatus != RING_STATUS_HOMING) { return; }

    // Светодиод сенсоров общий с часовым механизмом (GPIO 27).
    // Включаем на каждой итерации, чтобы он не был сброшен логикой часов.
    digitalWrite(RINGMECH_SENS_LED, HIGH);

    if (device_mech_ring._mechControlSteps > device_mech_ring._config.errorLimitSteps) { MechHomeEndFail(); return; }
    if (SENS_TRIGGERED) { MechHomeEndOk(); return; }
    device_mech_ring._mechControlSteps++;
    GoToTaskAfterStepRing = MechHomeTask;
    SetTask(MechMoveStepDown);
}

void CLASS_DEVICE_RINGMECH::MechHomeEndOk() {
    device_mech_ring._ringStatus = RING_STATUS_IDLE;
    ledClearState(LED_PRIO_DEV);
    GoToTaskAfterStepRing = Idle_task;
    device_mech_ring._mechControlSteps = 0;
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);

    if (device_mech_ring._config.enable_status == RING_MODE_WORK) {
        if (device_mech_ring._config.stepsPerRevolution == 0) { SetTask(MechCountStepsSetup); }
    }
    DEBUGRINGMECH("MechHome: done, sensor found\r\n");
}

void CLASS_DEVICE_RINGMECH::MechHomeEndFail() {
    device_mech_ring._ringStatus = RING_ERROR_NO_MECH;
    ledMacrosRingMechError();
    GoToTaskAfterStepRing = Idle_task;
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    DEBUGRINGMECH("MechHome: RING_ERROR_NO_MECH (%d steps)\r\n", device_mech_ring._mechControlSteps);
    device_mech_ring._mechControlSteps = 0;
}

// ============================================================
// Подсчёт шагов на оборот (калибровка)
// ============================================================

void CLASS_DEVICE_RINGMECH::MechCountStepsSetup() {
    device_mech_ring._ringStatus = RING_STATUS_COUNTING;
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, LOW);
    device_mech_ring._mechControlSteps = 0;
    GoToTaskAfterStepRing = MechCountStepsTask;
    SetTask(MechMoveStepDown);
    step_time = RINGMECH_SPEED_DEFAULT;
}

void CLASS_DEVICE_RINGMECH::MechCountStepsTask() {
    if (device_mech_ring._ringStatus != RING_STATUS_COUNTING) { return; }

    // Светодиод сенсоров общий с часовым механизмом (GPIO 27).
    // Включаем на каждой итерации, чтобы он не был сброшен логикой часов.
    digitalWrite(RINGMECH_SENS_LED, HIGH);

    if (device_mech_ring._mechControlSteps >= device_mech_ring._config.errorLimitSteps) { MechCountStepsFail(); return; }
    if (SENS_TRIGGERED && device_mech_ring._mechControlSteps > RINGMECH_MIN_STEPS_GAP) { MechCountStepsOk(); return; }
    device_mech_ring._mechControlSteps++; 
    GoToTaskAfterStepRing = MechCountStepsTask;
    SetTask(MechMoveStepDown);
}

void CLASS_DEVICE_RINGMECH::MechCountStepsOk() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    ledClearState(LED_PRIO_DEV);
    digitalWrite(RINGMECH_EN, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    device_mech_ring._config.stepsPerRevolution = device_mech_ring._mechControlSteps;
    if (device_mech_ring._mechControlSteps != 0) { device_mech_ring.saveConfig(); }
    GoToTaskAfterStepRing = Idle_task;
    device_mech_ring._mechControlSteps = 0;
    DEBUGRINGMECH("MechCountSteps: %d steps\r\n", device_mech_ring._config.stepsPerRevolution);
    device_mech_ring._ringStatus = RING_STATUS_IDLE;
    
}

void CLASS_DEVICE_RINGMECH::MechCountStepsFail() {
    DEBUGRINGMECH("MechCountSteps: RING_ERROR_NO_MECH\r\n");
    device_mech_ring._ringStatus = RING_ERROR_NO_MECH;
    ledMacrosRingMechError();
    GoToTaskAfterStepRing = Idle_task;
    device_mech_ring._mechControlSteps = 0;
    digitalWrite(RINGMECH_EN, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
}

// ============================================================
// Вращение на N оборотов (r-turn)
// ============================================================

void CLASS_DEVICE_RINGMECH::MechTurnNCount() {
    if (device_mech_ring._ringStatus != RING_STATUS_IDLE ) { return; }
    if (device_mech_ring._mechTurnTarget == 0) { return; }
    DEBUGRINGMECH("MechTurn: %d \r\n", device_mech_ring._mechTurnTarget);
    SetTask(MechTurnNSetup);
    device_mech_ring._mechTurnTarget --;
}

void CLASS_DEVICE_RINGMECH::MechTurnNSetup() {
    device_mech_ring._ringStatus = RING_STATUS_TURN;
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, LOW);
    device_mech_ring._mechControlSteps = 0;
    GoToTaskAfterStepRing = MechTurnNTask;
    SetTask(MechMoveStepDown);
    step_time = RINGMECH_SPEED_DEFAULT;
}


void CLASS_DEVICE_RINGMECH::MechTurnNTask() {
    if (device_mech_ring._ringStatus != RING_STATUS_TURN) { return; }

    // Светодиод сенсоров общий с часовым механизмом (GPIO 27).
    // Включаем на каждой итерации, чтобы он не был сброшен логикой часов.
    digitalWrite(RINGMECH_SENS_LED, HIGH);

    if (SENS_TRIGGERED && device_mech_ring._mechControlSteps > RINGMECH_MIN_STEPS_GAP) { MechTurnNEndOk(); return; }
    if (device_mech_ring._mechControlSteps > device_mech_ring._config.errorLimitSteps) { MechTurnNEndFail(); return; }
    
    device_mech_ring._mechControlSteps++;
    GoToTaskAfterStepRing = MechTurnNTask;

    if ( device_mech_ring._mechControlSteps == device_mech_ring._config.firstPosition  ) { 
        SetTimerTask(MechMoveStepDown, device_mech_ring._config.ringPauseOne); 
    }
    else { SetTask(MechMoveStepDown); }
}

void CLASS_DEVICE_RINGMECH::MechTurnNEndOk() {
    digitalWrite(RINGMECH_EN, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    ledClearState(LED_PRIO_DEV);
    GoToTaskAfterStepRing = Idle_task;
    device_mech_ring._mechControlSteps = 0;
    device_mech_ring._ringStatus = RING_STATUS_IDLE;
    SetTimerTask(MechTurnNCount, device_mech_ring._config.ringPauseTwo);
}

void CLASS_DEVICE_RINGMECH::MechTurnNEndFail() {
    DEBUGRINGMECH("MechTurn: fail at step %d, turn %d\r\n", device_mech_ring._mechControlSteps, device_mech_ring._mechTurnTarget);
    digitalWrite(RINGMECH_EN, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    GoToTaskAfterStepRing = Idle_task;
    device_mech_ring._mechControlSteps = 0;
    device_mech_ring._ringStatus = RING_ERROR_NO_MECH;
    ledMacrosRingMechError();
}

// ============================================================
// Периодический опрос (Wheel rotation in WORK mode)
// ============================================================
void CLASS_DEVICE_RINGMECH::RingPollTask() {
    SetTimerTask(RingPollTask, device_mech_ring._config.pollInterval * 1000UL);
    if (device_mech_ring._config.enable_status != RING_MODE_WORK) { return; }
    if (device_mech_ring._ringStatus != RING_STATUS_IDLE) { return; }

    time_t t = device_mech_ring.getCurrentTime();
    
    if (t == 0) { return; }
    uint8_t hourBegin = device_mech_ring._config.time_begin;
    uint8_t hourEnd = device_mech_ring._config.time_end;
    uint8_t timeNowHour = (uint8_t)hour(t) ;
    uint8_t timeNowMin = (uint8_t)minute(t) ;

    if (timeNowHour < hourBegin || timeNowHour > hourEnd) { return; } // мы НЕ в рабочем диапазоне
    if (device_mech_ring._timeHourPrev == timeNowHour){ return; } // час новый.
    if ( timeNowMin == 0  ){ // в начале часа, ноль минут.
       
            device_mech_ring._timeHourPrev = timeNowHour;
            device_mech_ring.CheckTime(timeNowHour);
            SetTask(MechTurnNCount);
        
    }
}

void CLASS_DEVICE_RINGMECH::CheckTime (uint8_t _inH)	{
	if (_inH > HOURINCIRCLE)   { _inH -= HOURINCIRCLE; }
	_mechTurnTarget = _inH;
}

// ============================================================
// Терминальные команды
// ============================================================

void CLASS_DEVICE_RINGMECH::cmdEn() {
    bool en = digitalRead(RINGMECH_EN);
    digitalWrite(RINGMECH_EN, en == LOW ? HIGH : LOW);
    DBG_MOD("[D_CLOCKRING] ", "Driver: %s\r\n", en == LOW ? "OFF" : "ON");
}

void CLASS_DEVICE_RINGMECH::cmdSens() { DBG_MOD("[D_CLOCKRING] ", "SENS=%d\r\n", digitalRead(RINGMECH_SENS)); }
void CLASS_DEVICE_RINGMECH::cmdHome() { SetTask(MechHomeSetup); }
void CLASS_DEVICE_RINGMECH::cmdCount() { SetTask(MechCountStepsSetup); }
void CLASS_DEVICE_RINGMECH::cmdSave() { device_mech_ring.saveConfig(); }

void CLASS_DEVICE_RINGMECH::cmdTurn() {
    if (device_mech_ring._mechTurnTarget != 0) { DBG_MOD("[D_CLOCKRING] ", "Busy: previous r-turn still running\r\n"); return; }
    if (device_mech_ring._config.enable_status == RING_MODE_WORK) { DBG_MOD("[D_CLOCKRING] ", "Blocked: WORK mode\r\n"); return; }
    char *arg = term.getNext();
    if (arg == NULL) { DBG_MOD("[D_CLOCKRING] ", "Usage: r-turn <N>\r\n"); return; }
    device_mech_ring._mechTurnTarget = (uint16_t)atoi(arg);
    if (device_mech_ring._mechTurnTarget == 0) return;
    SetTask(MechTurnNCount);
}

void CLASS_DEVICE_RINGMECH::cmdMode() {
    char *arg = term.getNext();
    if (arg == NULL) { device_mech_ring._config.enable_status = !device_mech_ring._config.enable_status; } 
    else {
        String s(arg);
        if (s == "dbg" || s == "debug")       { device_mech_ring._config.enable_status = RING_MODE_DEBUG; }
        else if (s == "work")                 { device_mech_ring._config.enable_status = RING_MODE_WORK; }
        else { DBG_MOD("[D_CLOCKRING] ", "Usage: c-mode [dbg|work]\r\n"); return; }
    }
    DBG_MOD("[D_CLOCKRING] ", "Mode: %s\r\n", device_mech_ring._config.enable_status == RING_MODE_WORK ? "WORK" : "DEBUG");
}

void CLASS_DEVICE_RINGMECH::cmdStatus() {
    DBG_MOD("[D_CLOCKRING] ", "===== RingMech Status =====\r\n");
    DBG_MOD("[D_CLOCKRING] ", "_ringStatus:           %d\r\n",      device_mech_ring._ringStatus);
    DBG_MOD("[D_CLOCKRING] ", "_config.enable_status: %d\r\n",      device_mech_ring._config.enable_status);
    DBG_MOD("[D_CLOCKRING] ", "_config.stepsPerRevolution: %d\r\n", device_mech_ring._config.stepsPerRevolution);
    DBG_MOD("[D_CLOCKRING] ", "_config.pollInterval:  %d\r\n",      device_mech_ring._config.pollInterval);
    DBG_MOD("[D_CLOCKRING] ", "_config.errorLimitSteps: %d\r\n",    device_mech_ring._config.errorLimitSteps);
    DBG_MOD("[D_CLOCKRING] ", "_config.ringPauseOne:     %d\r\n",      device_mech_ring._config.ringPauseOne);
    DBG_MOD("[D_CLOCKRING] ", "_config.ringPauseTwo:     %d\r\n",      device_mech_ring._config.ringPauseTwo);
    DBG_MOD("[D_CLOCKRING] ", "_config.firstPosition: %d\r\n",      device_mech_ring._config.firstPosition);
    DBG_MOD("[D_CLOCKRING] ", "_config.time_begin:    %d\r\n",      device_mech_ring._config.time_begin);
    DBG_MOD("[D_CLOCKRING] ", "_config.time_end:      %d\r\n",      device_mech_ring._config.time_end);
    DBG_MOD("[D_CLOCKRING] ", "_mechControlSteps:     %d\r\n",      device_mech_ring._mechControlSteps);
    DBG_MOD("[D_CLOCKRING] ", "_mechTurnTarget:       %d\r\n",      device_mech_ring._mechTurnTarget);
    DBG_MOD("[D_CLOCKRING] ", "_sensorState:          %d\r\n",      device_mech_ring._sensorState);
    time_t t = device_mech_ring.getCurrentTime();
    if (t > 0) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour(t), minute(t));
        DBG_MOD("[D_CLOCKRING] ", "time: %s\r\n", buf);
    }
    DBG_MOD("[D_CLOCKRING] ", "SENS=%d STEP=%d EN=%d\r\n",
        digitalRead(RINGMECH_SENS),
        digitalRead(RINGMECH_STEP),
        digitalRead(RINGMECH_EN));
    DBG_MOD("[D_CLOCKRING] ", "=============================\r\n");
}

void CLASS_DEVICE_RINGMECH::cmdTime() {
    char *arg1 = term.getNext();
    if (arg1 == NULL) {
        DBG_MOD("[D_CLOCKRING] ", "time_begin=%d time_end=%d\r\n", device_mech_ring._config.time_begin, device_mech_ring._config.time_end);
        return;
    }
    uint8_t b = (uint8_t)atoi(arg1);
    if (b > 23) { DBG_MOD("[D_CLOCKRING] ", "Error: value must be 0-23\r\n"); return; }
    char *arg2 = term.getNext();
    if (arg2 == NULL) { DBG_MOD("[D_CLOCKRING] ", "Usage: r-time <begin> <end>\r\n"); return; }
    uint8_t e = (uint8_t)atoi(arg2);
    if (e > 23) { DBG_MOD("[D_CLOCKRING] ", "Error: value must be 0-23\r\n"); return; }
    if (b > e) { DBG_MOD("[D_CLOCKRING] ", "Error: begin must be <= end\r\n"); return; }
    device_mech_ring._config.time_begin = b;
    device_mech_ring._config.time_end   = e;
    DBG_MOD("[D_CLOCKRING] ", "OK\r\n");
}

// ============================================================
// Веб-эндпоинты ручного управления
// ============================================================

void CLASS_DEVICE_RINGMECH::cmdEnWeb(AsyncWebServerRequest *request) {
    cmdEn();
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_RINGMECH::cmdSensWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["SENS"]  = digitalRead(RINGMECH_SENS);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void CLASS_DEVICE_RINGMECH::cmdHomeWeb(AsyncWebServerRequest *request) {
    if (device_mech_ring._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechHomeSetup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_RINGMECH::cmdCountWeb(AsyncWebServerRequest *request) {
    if (device_mech_ring._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechCountStepsSetup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_RINGMECH::cmdTurnWeb(AsyncWebServerRequest *request) {
    if (device_mech_ring._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    if (!request->hasArg("count")) { request->send(400, "application/json", "{\"error\":\"Missing count\"}"); return; }
    if (device_mech_ring._mechTurnTarget != 0) { request->send(429, "application/json", "{\"error\":\"Busy\"}"); return; }
    device_mech_ring._mechTurnTarget = (uint16_t)request->arg("count").toInt();
    if (device_mech_ring._mechTurnTarget == 0) { request->send(200, "application/json", "{\"ok\":true}"); return; }
    SetTask(MechTurnNCount);
    request->send(200, "application/json", "{\"ok\":true}");
}

void CLASS_DEVICE_RINGMECH::cmdStatusWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["_ringStatus"]            = device_mech_ring._ringStatus;
    doc["enable_status"]          = device_mech_ring._config.enable_status;
    doc["stepsPerRevolution"]     = device_mech_ring._config.stepsPerRevolution;
    doc["pollInterval"]           = device_mech_ring._config.pollInterval;
    doc["errorLimitSteps"]        = device_mech_ring._config.errorLimitSteps;
    doc["_mechControlSteps"]      = device_mech_ring._mechControlSteps;
    doc["_sensorState"]           = device_mech_ring._sensorState;
    doc["gpio_SENS"]              = digitalRead(RINGMECH_SENS);
    doc["gpio_STEP"]              = digitalRead(RINGMECH_STEP);
    doc["gpio_EN"]                = digitalRead(RINGMECH_EN);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void CLASS_DEVICE_RINGMECH::cmdResetWeb(AsyncWebServerRequest *request) {
    device_mech_ring._mechControlSteps = 0;
    device_mech_ring._mechTurnTarget = 0;
    device_mech_ring._ringStatus = RING_STATUS_IDLE;
    ledClearState(LED_PRIO_DEV);
    request->send(200, "application/json", "{\"ok\":true}");
}
