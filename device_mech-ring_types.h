#ifndef _DEVICE_MECH_RING_TYPES_h
#define _DEVICE_MECH_RING_TYPES_h

// ============================================================
// device_mech-ring_types.h — типы, структуры и define'ы устройства
// «механические часы с боем».
// Реализация: device_mech-ring.cpp (шаблон) и
// device_mech-ring_engine.cpp (автомат и звонок).
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#define RINGMECH_STEP      17
#define RINGMECH_EN        16
#define RINGMECH_SENS_LED  27
#define RINGMECH_SENS      32


#define RINGMECH_MIN_STEPS_GAP 40
#define RINGMECH_RING_FIRST_PAUSE_DEFAULT 750
#define RINGMECH_RING_SECON_PAUSE_DEFAULT 750
#define RINGMECH_SPEED_DEFAULT 2
// Пауза фазы шага, если ядро занято (сеть/веб/чтение FS), мс
#define RINGMECH_STEP_BUSY_MS       500
// Интервал между соседними шагами, с которого считаем ядро занятым, мс
#define RINGMECH_STEP_GAP_BUSY_MS   15
// Интервал больше этого — считаем новым запуском механизма (не пауза), мс
#define RINGMECH_STEP_GAP_RESET_MS  1000
#define RINGMECH_FIRST_POSITION_DEFAULT 85
#define RINGMECH_TIME_BEGIN_DEFAULT 9
#define RINGMECH_TIME_END_DEFAULT   18

#define CONFIG_FILE_RINGMECH    "/config_ring-mech.json"

#define SENS_TRIGGERED  (digitalRead(RINGMECH_SENS) == LOW)

#define     HOURINCIRCLE				12
#define     MININHOUR					60
#define     MINMAX						59
typedef void (*DPDR)(void);
typedef enum {
    RING_STATUS_IDLE    = 0,
    RING_STATUS_HOMING,
    RING_STATUS_ROTATION,
    RING_STATUS_COUNTING,
    RING_STATUS_TURN,
    RING_ERROR_NO_MECH
} ring_status_e;

typedef enum {
    RING_MODE_DEBUG = 0,
    RING_MODE_WORK  = 1
} ring_mode_e;

typedef struct {
    uint8_t  enable_status;
    uint16_t stepsPerRevolution;
    uint16_t pollInterval;
    uint16_t errorLimitSteps;
    uint16_t ringPauseOne;
    uint16_t ringPauseTwo;
    uint8_t firstPosition;
    uint8_t time_begin;
    uint8_t time_end;
} strRingMechConfig;

#endif // _DEVICE_MECH_RING_TYPES_h
