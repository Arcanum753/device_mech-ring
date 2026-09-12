#ifndef _DEVICE_RINGMECH_h
#define _DEVICE_RINGMECH_h

#include "main.h"

#include "mod_context.h"

#ifdef DEBUG_RINGMECH
#define DEBUGRINGMECH(...) DBG_MOD("[D_CLOCKRING] ", __VA_ARGS__)
#else
#define DEBUGRINGMECH(...)
#endif

#include <LittleFS.h>

#include "common/TimeLib.h"

#if defined(MODULE_DS3231)
#include "module_ds3231/module_ds3231.h"
#endif

#include "device_mech-ring_types.h"

void ringMechTerminalRegister();

// Планируемая после шага задача (определена в _engine.cpp)
extern DPDR GoToTaskAfterStepRing;

class CLASS_DEVICE_RINGMECH {
public:
    void setFs(fs::LittleFSFS* fs);
    void begin();
    void begin(ModContext& ctx);
    void web_Init();
    time_t getCurrentTime();

    // Кассета светодиодной индикации ошибки устройства
    static void ledMacrosRingMechError();
    
    static void cmdEn();
    static void cmdSens();
    static void cmdHome();
    static void cmdCount();
    static void cmdTurn();
    static void cmdSave();
    static void cmdMode();
    static void cmdStatus();
    static void cmdTime();
    static void cmdSource();
    static void cmdEnWeb(AsyncWebServerRequest *request);
    static void cmdSensWeb(AsyncWebServerRequest *request);
    static void cmdNWeb(AsyncWebServerRequest *request);
    static void cmdHomeWeb(AsyncWebServerRequest *request);
    static void cmdCountWeb(AsyncWebServerRequest *request);
    static void cmdTurnWeb(AsyncWebServerRequest *request);
    static void cmdStatusWeb(AsyncWebServerRequest *request);
    static void cmdResetWeb(AsyncWebServerRequest *request);
private:
    // Версионные методы
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    // Веб-обработчики
    void handleInfo_ring(AsyncWebServerRequest *request);
    void handleSave(AsyncWebServerRequest *request);

    // Конфиг
    void defaultConfig();
    bool loadConfig();
    bool saveConfig();

    // Логика устройства
    void CheckTime (uint8_t _inH);

    void MechInitGPIOs();
    static void MechMoveStepDown();
    static void MechMoveStepUp();
    static void MechNCmdStep();

    static void MechHomeSetup();
    static void MechHomeTask();
    static void MechHomeEndOk();
    static void MechHomeEndFail();

    static void MechCountStepsSetup();
    static void MechCountStepsTask();
    static void MechCountStepsOk();
    static void MechCountStepsFail();

    static void MechTurnNCount();
    static void MechTurnNSetup();
    static void MechTurnNTask();
    static void MechTurnNEndOk();
    static void MechTurnNEndFail();

    static void RingPollTask();

protected:
    fs::LittleFSFS*     _fs;
    strRingMechConfig   _config;
    uint16_t            _mechControlSteps;
    uint16_t            _mechTurnTarget;
    uint8_t             _ringStatus;
    int                 _sensorState;
    uint8_t             _timeHourReal;
    uint8_t             _timeHourPrev;
    uint8_t             _timeMinReal;
};

extern CLASS_DEVICE_RINGMECH device_mech_ring;

#endif
