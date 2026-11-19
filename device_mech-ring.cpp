#include "core_web/FSWebServerLib.h"

#include <string.h>

#include "core_json/core_json.h"

#include "device_mech-ring.h"
#include "common/common.h"
#include "device_mech-ring_version.h"
#include "core_sys/eertos.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/ErriezSerialTerminal.h"
#include "core_led/core_led.h"

CLASS_DEVICE_RINGMECH device_mech_ring;
void CLASS_DEVICE_RINGMECH::setFs(fs::LittleFSFS* fs)  {   _fs = fs;   }

// ============================================================
// begin()
// ============================================================
void CLASS_DEVICE_RINGMECH::begin() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    GoToTaskAfterStepRing = Idle_task;
    _mechControlSteps = 0;
    _ringStatus = RING_STATUS_IDLE;
    ledClearState(LED_PRIO_DEV); // сброс моргания ошибки устройства
    _sensorState = digitalRead(RINGMECH_SENS);
    _timeHourPrev = 0xFF;

    defaultConfig();
    if (loadConfig() == false) { saveConfig(); }

    ringMechTerminalRegister();
    MechInitGPIOs();

    if (_config.enable_status == RING_MODE_WORK) { SetTask(MechHomeSetup); }
    SetTask(RingPollTask);
}

void CLASS_DEVICE_RINGMECH::begin(ModContext& ctx) {
#if defined(ESP32)
    _fs = ctx.fs;
#endif
    begin();
}

// ============================================================
// web_Init()
// ============================================================
void CLASS_DEVICE_RINGMECH::web_Init() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);

    ESPHTTPServer.on("/ring-mech/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSave(request);
    });

    ESPHTTPServer.on("/ring-mech/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo_ring(request);
    });

    ESPHTTPServer.on("/ring-mech/turn",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdTurnWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/en",     HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdEnWeb(request);   else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/sens",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdSensWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/home",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdHomeWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/count",  HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdCountWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/status", HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdStatusWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/reset",  HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdResetWeb(request);  else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/ver",    HTTP_GET, [this](AsyncWebServerRequest *request) { this->html_ver_get(request);});
}

// ============================================================
// Веб-обработчики
// ============================================================
void CLASS_DEVICE_RINGMECH::handleInfo_ring(AsyncWebServerRequest *request) {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    doc["enable_status"]        = _config.enable_status;
    doc["stepsPerRevolution"]   = _config.stepsPerRevolution;
    doc["pollInterval"]         = _config.pollInterval;
    doc["errorLimitSteps"]      = _config.errorLimitSteps;
    doc["ringPauseOne"]         = _config.ringPauseOne;
    doc["ringPauseTwo"]         = _config.ringPauseTwo;
    doc["firstPosition"]        = _config.firstPosition;
    doc["time_begin"]           = _config.time_begin;
    doc["time_end"]             = _config.time_end;
    doc["status"]               = _ringStatus;
    doc["mechControlSteps"]     = _mechControlSteps;
    doc["turnCount"]            = _mechTurnTarget;
    doc["sensor"]               = digitalRead(RINGMECH_SENS);
    time_t t = getCurrentTime();
    if (t > 0) {
        char buf[9];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour(t), minute(t), second(t));
        doc["currentTime"] = buf;
    } else {
        doc["currentTime"] = "N/A";
    }
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void CLASS_DEVICE_RINGMECH::handleSave(AsyncWebServerRequest *request) {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);

    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            String name = request->argName(i);
            String val  = request->arg(i);

            if (name == "enable_status")            { _config.enable_status = (uint8_t)val.toInt(); }
            else if (name == "stepsPerRevolution")  { _config.stepsPerRevolution = (uint16_t)val.toInt(); }
            else if (name == "pollInterval")        { _config.pollInterval = (uint16_t)val.toInt(); if (_config.pollInterval < 1) _config.pollInterval = 1; }
            else if (name == "errorLimitSteps")     { _config.errorLimitSteps = (uint16_t)val.toInt(); }
            else if (name == "ringPauseOne")           { _config.ringPauseOne = (uint16_t)val.toInt(); if (_config.ringPauseOne < 1) _config.ringPauseOne = RINGMECH_RING_FIRST_PAUSE_DEFAULT; }
            else if (name == "ringPauseTwo")           { _config.ringPauseTwo = (uint16_t)val.toInt(); if (_config.ringPauseTwo < 1) _config.ringPauseTwo = RINGMECH_RING_SECON_PAUSE_DEFAULT; }
            else if (name == "firstPosition")       { _config.firstPosition = (uint8_t)val.toInt(); }
            else if (name == "time_begin") {
                uint8_t v = (uint8_t)constrain(val.toInt(), 0, 23);
                if (v == 0 && _config.time_end == 0) { _config.time_begin = 0; }
                else if (v <= _config.time_end) _config.time_begin = v;
            }
            else if (name == "time_end") {
                uint8_t v = (uint8_t)constrain(val.toInt(), 0, 23);
                if (_config.time_begin == 0 && v == 0) { _config.time_end = 0; }
                else if (v >= _config.time_begin) _config.time_end = v;
            }
        }
        saveConfig();
        request->send(200, "text/plain", "OK");
    }
}

// ============================================================
// Конфиг
// ============================================================
void CLASS_DEVICE_RINGMECH::defaultConfig() {
    _config.enable_status      = RING_MODE_DEBUG;
    _config.stepsPerRevolution = 0;
    _config.pollInterval       = 5;
    _config.errorLimitSteps    = 500;
    _config.ringPauseOne        = RINGMECH_RING_FIRST_PAUSE_DEFAULT ;
    _config.ringPauseTwo        = RINGMECH_RING_SECON_PAUSE_DEFAULT;
    _config.firstPosition       = RINGMECH_FIRST_POSITION_DEFAULT;
    _config.time_begin          = RINGMECH_TIME_BEGIN_DEFAULT;
    _config.time_end            = RINGMECH_TIME_END_DEFAULT;
}

bool CLASS_DEVICE_RINGMECH::loadConfig() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (core_json.jsonFileLoadDoc(CONFIG_FILE_RINGMECH, doc) == false) { return false; }

    _config.enable_status       = doc["enable_status"].as<uint8_t>();
    _config.stepsPerRevolution  = doc["stepsPerRevolution"].as<uint16_t>();
    _config.pollInterval        = doc["pollInterval"].as<uint16_t>();
    _config.errorLimitSteps     = doc["errorLimitSteps"].as<uint16_t>();
    _config.ringPauseOne           = doc["ringPauseOne"].as<uint16_t>();
    _config.ringPauseTwo           = doc["ringPauseTwo"].as<uint16_t>();
    _config.firstPosition       = doc["firstPosition"].as<uint8_t>();
    _config.time_begin           = doc["time_begin"].as<uint8_t>();
    _config.time_end             = doc["time_end"].as<uint8_t>();

    if (_config.time_begin > 23) _config.time_begin = RINGMECH_TIME_BEGIN_DEFAULT;
    if (_config.time_end   > 23) _config.time_end   = RINGMECH_TIME_END_DEFAULT;
    if (_config.time_begin > _config.time_end && !(_config.time_begin == 0 && _config.time_end == 0)) { _config.time_begin = RINGMECH_TIME_BEGIN_DEFAULT; _config.time_end = RINGMECH_TIME_END_DEFAULT; }

    if (_config.pollInterval < 1)   { _config.pollInterval = 5; }
    if (_config.stepsPerRevolution < 1) { _config.stepsPerRevolution = 400; }
    if (_config.ringPauseOne < 1)      { _config.ringPauseOne = RINGMECH_RING_FIRST_PAUSE_DEFAULT; }
    if (_config.ringPauseTwo < 1)      { _config.ringPauseTwo = RINGMECH_RING_SECON_PAUSE_DEFAULT; }

    return true;
}

bool CLASS_DEVICE_RINGMECH::saveConfig() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    core_json.jsonFileLoadDoc(CONFIG_FILE_RINGMECH, doc);
    doc["enable_status"]        = _config.enable_status;
    doc["stepsPerRevolution"]   = _config.stepsPerRevolution;
    doc["pollInterval"]         = _config.pollInterval;
    doc["errorLimitSteps"]      = _config.errorLimitSteps;
    doc["ringPauseOne"]         = _config.ringPauseOne;
    doc["ringPauseTwo"]         = _config.ringPauseTwo;
    doc["firstPosition"]        = _config.firstPosition;
    doc["time_begin"]           = _config.time_begin;
    doc["time_end"]             = _config.time_end;
    return core_json.jsonFileSaveDoc(CONFIG_FILE_RINGMECH, doc);
}

// ============================================================
// Версионные методы
// ============================================================
String CLASS_DEVICE_RINGMECH::getVersionStr() { return String(DEVICE_MECH_RING_VERSION);  }
String CLASS_DEVICE_RINGMECH::getGeneratedTime() { return String(DEVICE_MECH_RING_GENERATED_TIME);    }
String CLASS_DEVICE_RINGMECH::getCommitDateStr() { return String(DEVICE_MECH_RING_COMMIT_DATE_STR);   }

void CLASS_DEVICE_RINGMECH::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    String values = "";
    values += "ringmechversion|" + getVersionStr()    + "|div\n";
    values += "ringmechgentime|" + getGeneratedTime() + "|div\n";
    values += "ringmechgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

// ============================================================
// Терминальные команды
// ============================================================
static const char* const kRingCommands[] = {
    "show", "en", "sens", "home", "cnt", "mode", "trn", "time", "save", nullptr
};

static const char kRingHelp[] PROGMEM =
    "ring - clock strike mechanism (config is saved only by 'ring save')\r\n"
    "  ring show             status, config, sensor and pins\r\n"
    "  ring en               toggle driver enable\r\n"
    "  ring sens             read sensor\r\n"
    "  ring home             home by sensor\r\n"
    "  ring cnt              calibrate steps per revolution\r\n"
    "  ring mode [dbg|work]  show/toggle/set mode (not saved)\r\n"
    "  ring trn <N>          make N full turns\r\n"
    "  ring time [<b> <e>]   show/set strike window (hours)\r\n"
    "  ring save             persist config\r\n"
    "  ring help             this help\r\n";

static void s_ringCmd() {
    char* sub = term.getNext();
    if (sub == nullptr || strcmp(sub, "help") == 0) { TerminalPrintText(kRingHelp); return; }

    if (strcmp(sub, "show") == 0 || strcmp(sub, "stat") == 0) { CLASS_DEVICE_RINGMECH::cmdStatus(); return; }
    if (strcmp(sub, "en")   == 0) { CLASS_DEVICE_RINGMECH::cmdEn();     return; }
    if (strcmp(sub, "sens") == 0) { CLASS_DEVICE_RINGMECH::cmdSens();   return; }
    if (strcmp(sub, "home") == 0) { CLASS_DEVICE_RINGMECH::cmdHome();   return; }
    if (strcmp(sub, "cnt")  == 0) { CLASS_DEVICE_RINGMECH::cmdCount();  return; }
    if (strcmp(sub, "mode") == 0) { CLASS_DEVICE_RINGMECH::cmdMode();   return; }
    if (strcmp(sub, "trn")  == 0) { CLASS_DEVICE_RINGMECH::cmdTurn();   return; }
    if (strcmp(sub, "time") == 0) { CLASS_DEVICE_RINGMECH::cmdTime();   return; }
    if (strcmp(sub, "save") == 0) { CLASS_DEVICE_RINGMECH::cmdSave();   return; }

    DBG_MOD("[D_CLOCKRING] ", "Unknown ring subcommand '%s'. Type 'ring help'.\r\n", sub);
}

void ringMechTerminalRegister() {
    TerminalRegisterModule("device_mech-ring", "ring", "clock strike mechanism",
                           s_ringCmd, kRingCommands, kRingHelp, true);

    // Старые имена r-* сохранены скрытыми алиасами (слоты не занимают).
    TerminalRegisterAlias("r-enc",  CLASS_DEVICE_RINGMECH::cmdEn);
    TerminalRegisterAlias("r-sens", CLASS_DEVICE_RINGMECH::cmdSens);
    TerminalRegisterAlias("r-home", CLASS_DEVICE_RINGMECH::cmdHome);
    TerminalRegisterAlias("r-cnt",  CLASS_DEVICE_RINGMECH::cmdCount);
    TerminalRegisterAlias("r-mode", CLASS_DEVICE_RINGMECH::cmdMode);
    TerminalRegisterAlias("r-stat", CLASS_DEVICE_RINGMECH::cmdStatus);
    TerminalRegisterAlias("r-save", CLASS_DEVICE_RINGMECH::cmdSave);
    TerminalRegisterAlias("r-trn",  CLASS_DEVICE_RINGMECH::cmdTurn);
    TerminalRegisterAlias("r-time", CLASS_DEVICE_RINGMECH::cmdTime);
}
