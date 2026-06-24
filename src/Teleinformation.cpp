#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "driver/temp_sensor.h"
#include "Teleinformation.h"
#include "config.h"

// ── Parser state (mutated only inside bTranscodeCharTIC) ─────────────────────
static unsigned int s_pos      = 0;   // field index: 0=label 1=horodate 2=value 3=checksum
static unsigned int s_idx      = 0;   // byte index within current field
static uint8_t      s_crc_rx   = 0;   // last received byte when pos≥2 (becomes checksum)
static uint8_t      s_crc_acc  = 0;   // running sum: label+TAB+horodate+TAB+value+TAB
static uint8_t      s_crc_snap = 0;   // snapshot of s_crc_acc at the separator after value

extern uint16_t             sdm630InputRegisters[622];
extern uint32_t             u32Timeout;
extern uint8_t              u8ErrorDecode;
extern ConfigSettingsStruct ConfigSettings;

// ── Eastron SDM630 input register addresses (FC=04, IEEE 754 float32 big-endian)
#define SDM630_L1_VOLTAGE          0
#define SDM630_L2_VOLTAGE          2
#define SDM630_L3_VOLTAGE          4
#define SDM630_L1_CURRENT          6
#define SDM630_L2_CURRENT          8
#define SDM630_L3_CURRENT         10
#define SDM630_L1_ACTIVE_POWER    12
#define SDM630_L2_ACTIVE_POWER    14
#define SDM630_L3_ACTIVE_POWER    16
#define SDM630_L1_APPARENT_POWER  18
#define SDM630_L2_APPARENT_POWER  20
#define SDM630_L3_APPARENT_POWER  22
#define SDM630_L1_POWER_FACTOR    30
#define SDM630_L2_POWER_FACTOR    32
#define SDM630_L3_POWER_FACTOR    34
#define SDM630_TOTAL_ACTIVE_POWER 52
#define SDM630_TOTAL_APPARENT_VA  56
#define SDM630_TOTAL_POWER_FACTOR 62

#define SDM630_TOTAL_ACTIVE_ENERGY  342
#define SDM630_TEMPO_BLUE_KWH       500
#define SDM630_TEMPO_WHITE_KWH      502
#define SDM630_TEMPO_RED_KWH        504
#define SDM630_TOTAL_HP_KWH         506
#define SDM630_TOTAL_HC_KWH         508
#define SDM630_BLUE_HC_KWH          510
#define SDM630_BLUE_HP_KWH          512
#define SDM630_WHITE_HC_KWH         514
#define SDM630_WHITE_HP_KWH         516
#define SDM630_RED_HC_KWH           518
#define SDM630_RED_HP_KWH           520

#define SDM630_IS_TEMPO_BLUE        600
#define SDM630_IS_TEMPO_WHITE       601
#define SDM630_IS_TEMPO_RED         602
#define SDM630_IS_HP                603
#define SDM630_IS_HC                604
#define SDM630_IS_BASE_TARIFF       605
#define SDM630_IS_HPHC_TARIFF       606
#define SDM630_IS_TEMPO_TARIFF      607
#define SDM630_IS_POWER_OVERFLOW    608

#define SDM630_CONTRACTED_POWER     609
#define SDM630_TEMPERATURE          612

#define SDM630_ACTIVE_POWER_W_MVAVG      614
#define SDM630_APPARENT_POWER_VA_MVAVG   616
#define SDM630_CURRENT_L1_A_MVAVG        618
#define SDM630_VOLTAGE_L1_V_MVAVG        620

static void sdm630WriteFloat(uint16_t reg, float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    sdm630InputRegisters[reg]     = (uint16_t)(bits >> 16);
    sdm630InputRegisters[reg + 1] = (uint16_t)(bits & 0xFFFF);
}



// High-precision current registers — total and per-phase
#define SE_PRECISE_I_FLOAT_REG   20
#define SE_PRECISE_I_U16_REG     22
#define SE_PRECISE_I1_FLOAT_REG  23
#define SE_PRECISE_I1_U16_REG    25
#define SE_PRECISE_I2_FLOAT_REG  26
#define SE_PRECISE_I2_U16_REG    28
#define SE_PRECISE_I3_FLOAT_REG  29
#define SE_PRECISE_I3_U16_REG    31

// cos phi = CCASN / SINSTS
#define SE_COSPHI_FLOAT_REG      32
#define SE_COSPHI_U16_REG        34

// STGE decoded flags (one register each, 0/1)
#define SE_STGE_INJECT_REG       35   // bit 4: 0=consumption, 1=injection
#define SE_STGE_OVERLOAD_REG     36   // bit 6: 0=OK, 1=over contracted power
#define SE_STGE_PRODUCER_REG     37   // bit 7: 0=consumer, 1=producer mode

static uint32_t s_sinsts_va  = 0;
static uint32_t s_sinsts1_va = 0;
static uint32_t s_sinsts2_va = 0;
static uint32_t s_sinsts3_va = 0;
static uint16_t s_urms1_v    = 230;
static uint16_t s_urms2_v    = 230;
static uint16_t s_urms3_v    = 230;
static bool     s_urms2_set  = false;
static bool     s_urms3_set  = false;
static bool     s_sinsts1_set = false;  // Track if SINSTS1 is received from Linky
static uint32_t s_ccasn_w    = 0;

static float s_easf01 = 0;
static float s_easf02 = 0;
static float s_easf03 = 0;
static float s_easf04 = 0;
static float s_easf05 = 0;
static float s_easf06 = 0;
static float s_easd01 = 0;
static float s_easd02 = 0;
static float s_easd03 = 0;
static float s_easd04 = 0;

// Compute current from Active Power / Voltage (assuming PF=1 for higher precision)
static void updateComputedCurrentL1() {
    if (s_urms1_v > 0 && s_ccasn_w > 0) {
        float current = (float)s_ccasn_w / (float)s_urms1_v;
        sdm630WriteFloat(SDM630_L1_CURRENT, current);
    }
}

// ── Moving Average & Latch Logic (10s window, updated every 2s) ──
static float mv_active_power[5] = {0};
static float mv_apparent_power[5] = {0};
static float mv_current_l1[5] = {0};
static float mv_voltage_l1[5] = {0};
static bool mv_power_overflow[5] = {false};
static uint8_t mv_idx = 0;
static uint32_t last_mv_update = 0;

void updatePowerOverflow(bool overflow) {
    mv_power_overflow[mv_idx] = overflow; // Latch it into the current bucket
}

void processMovingAverages() {
    uint32_t now = millis();
    if (now - last_mv_update >= 2000) {
        last_mv_update = now;

        mv_idx = (mv_idx + 1) % 5;

        float f_active_power, f_apparent_power, f_current_l1, f_voltage_l1;
        uint32_t bits;

        bits = ((uint32_t)sdm630InputRegisters[SDM630_TOTAL_ACTIVE_POWER] << 16) | sdm630InputRegisters[SDM630_TOTAL_ACTIVE_POWER + 1];
        memcpy(&f_active_power, &bits, 4);

        bits = ((uint32_t)sdm630InputRegisters[SDM630_TOTAL_APPARENT_VA] << 16) | sdm630InputRegisters[SDM630_TOTAL_APPARENT_VA + 1];
        memcpy(&f_apparent_power, &bits, 4);

        bits = ((uint32_t)sdm630InputRegisters[SDM630_L1_CURRENT] << 16) | sdm630InputRegisters[SDM630_L1_CURRENT + 1];
        memcpy(&f_current_l1, &bits, 4);

        bits = ((uint32_t)sdm630InputRegisters[SDM630_L1_VOLTAGE] << 16) | sdm630InputRegisters[SDM630_L1_VOLTAGE + 1];
        memcpy(&f_voltage_l1, &bits, 4);

        mv_active_power[mv_idx] = f_active_power;
        mv_apparent_power[mv_idx] = f_apparent_power;
        mv_current_l1[mv_idx] = f_current_l1;
        mv_voltage_l1[mv_idx] = f_voltage_l1;

        mv_power_overflow[mv_idx] = false;

        float avg_active = 0, avg_apparent = 0, avg_curr = 0, avg_volt = 0;
        bool any_overflow = false;
        for (int i = 0; i < 5; i++) {
            avg_active += mv_active_power[i];
            avg_apparent += mv_apparent_power[i];
            avg_curr += mv_current_l1[i];
            avg_volt += mv_voltage_l1[i];
            if (mv_power_overflow[i]) any_overflow = true;
        }
        avg_active /= 5.0f;
        avg_apparent /= 5.0f;
        avg_curr /= 5.0f;
        avg_volt /= 5.0f;

        sdm630WriteFloat(SDM630_ACTIVE_POWER_W_MVAVG, avg_active);
        sdm630WriteFloat(SDM630_APPARENT_POWER_VA_MVAVG, avg_apparent);
        sdm630WriteFloat(SDM630_CURRENT_L1_A_MVAVG, avg_curr);
        sdm630WriteFloat(SDM630_VOLTAGE_L1_V_MVAVG, avg_volt);

        sdm630InputRegisters[SDM630_IS_POWER_OVERFLOW] = any_overflow ? 1 : 0;

        // Fallback: if SINSTS1 not received from Linky, use SINSTS (single-phase)
        if (!s_sinsts1_set && s_sinsts_va > 0) {
            sdm630WriteFloat(SDM630_L1_APPARENT_POWER, (float)s_sinsts_va);
        }

        // Update temperature register
        float temp;
        temp_sensor_read_celsius(&temp);
        sdm630WriteFloat(SDM630_TEMPERATURE, temp);
    }
}


// ── Energy-delta active power estimation ─────────────────────────────────────
static uint64_t s_east_wh     = 0;
static uint32_t s_energy_ms   = 0;
static float    s_power_ema_w = 0.0f;
static bool     s_east_init   = false;












// dt guard (500 ms) rejects anomalously fast back-to-back frames.
// Stale gap (>60 s): reset EMA so next valid delta seeds fresh.
// Large load change (P_raw > 2× or < 0.5× EMA): re-seed for fast response.
static void applyEnergyDelta(uint64_t delta_wh, uint32_t now_ms) {
    uint32_t dt = now_ms - s_energy_ms;
    s_energy_ms = now_ms;
    if (dt < 500 || delta_wh == 0) return;
    if (dt > 60000u) { s_power_ema_w = 0.0f; return; }
    float P_raw = (float)delta_wh * 3600000.0f / (float)dt;
    const float alpha = 0.25f;
    if (s_power_ema_w == 0.0f || P_raw > s_power_ema_w * 2.0f || P_raw < s_power_ema_w * 0.5f)
        s_power_ema_w = P_raw;
    else
        s_power_ema_w = alpha * P_raw + (1.0f - alpha) * s_power_ema_w;
}

static void standardEnergyUpdate(uint64_t wh) {
    uint32_t now = (uint32_t)millis();
    if (!s_east_init) {
        s_east_wh = wh; s_energy_ms = now; s_east_init = true; return;
    }
    if (wh > s_east_wh) {
        applyEnergyDelta(wh - s_east_wh, now);
        s_east_wh = wh;
    } else if (s_east_wh > wh + 10000ULL) {
        // Counter rollback (meter replacement or overflow) — re-seed from new baseline
        s_east_wh = wh; s_east_init = false; s_power_ema_w = 0.0f;
    }
}

// ── Register write helpers ────────────────────────────────────────────────────





// ── Label dispatch table ──────────────────────────────────────────────────────

enum TicType : uint8_t { TY_U16, TY_U64, TY_STR, TY_NONE };
enum TicSfx  : uint8_t {
    SFX_NONE,
    SFX_EAST,
    SFX_EASF01, SFX_EASF02, SFX_EASF03, SFX_EASF04, SFX_EASF05, SFX_EASF06, SFX_EASF07, SFX_EASF08, SFX_EASF09, SFX_EASF10,
    SFX_EASD01, SFX_EASD02, SFX_EASD03, SFX_EASD04,
    SFX_PREF,
    SFX_NTARF,
    SFX_IRMS1, SFX_IRMS2, SFX_IRMS3,
    SFX_URMS1, SFX_URMS2, SFX_URMS3,
    SFX_SINSTS, SFX_SINSTS1, SFX_SINSTS2, SFX_SINSTS3,
    SFX_CCASN,
    SFX_STGE,
};

struct TicEntry {
    const char *label;
    uint16_t    reg;           // 0xFFFF = no register
    TicType     type;
    TicSfx      sfx;
    bool        need_horodate; // set u8ErrorDecode=3 if au8Pos != 3
};

// Labels ordered arbitrarily — strcmp gives exact match so prefix order doesn't matter.
static const TicEntry s_table[] = {
    // Identifiers
    { "ADSC",      300,   TY_U64, SFX_NONE,    false },
    // Tariff info
    { "NGTF",     2000,   TY_STR, SFX_NONE,    false },
    { "LTARF",    2100,   TY_STR, SFX_NTARF,    false },
    { "NTARF",  0xFFFF,   TY_STR, SFX_NTARF,   false },
    { "VTIC",   0xFFFF,   TY_NONE, SFX_NONE,   false },
    // Date
    { "DATE",     3000,   TY_STR, SFX_NONE,    false },
    // Active energy — total and per-tariff index
    { "EAST",     1000,   TY_U64, SFX_EAST,    false },
    { "EASF01",   1004,   TY_U64, SFX_EASF01,    false },
    { "EASF02",   1008,   TY_U64, SFX_EASF02,    false },
    { "EASF03",   1012,   TY_U64, SFX_EASF03,    false },
    { "EASF04",   1016,   TY_U64, SFX_EASF04,    false },
    { "EASF05",   1020,   TY_U64, SFX_EASF05,    false },
    { "EASF06",   1024,   TY_U64, SFX_EASF06,    false },
    { "EASF07",   1028,   TY_U64, SFX_EASF07,    false },
    { "EASF08",   1032,   TY_U64, SFX_EASF08,    false },
    { "EASF09",   1036,   TY_U64, SFX_EASF09,    false },
    { "EASF10",   1040,   TY_U64, SFX_EASF10,    false },
    { "EASD01",   1100,   TY_U64, SFX_EASD01,    false },
    { "EASD02",   1104,   TY_U64, SFX_EASD02,    false },
    { "EASD03",   1108,   TY_U64, SFX_EASD03,    false },
    { "EASD04",   1112,   TY_U64, SFX_EASD04,    false },
    // Reactive / injected energy
    { "EAIT",     1200,   TY_U64, SFX_NONE,    false },
    { "ERQ1",     1300,   TY_U64, SFX_NONE,    false },
    { "ERQ2",     1304,   TY_U64, SFX_NONE,    false },
    { "ERQ3",     1308,   TY_U64, SFX_NONE,    false },
    { "ERQ4",     1312,   TY_U64, SFX_NONE,    false },
    // Instantaneous measurements
    { "IRMS1",    1320,   TY_U16, SFX_IRMS1,   false },
    { "IRMS2",    1321,   TY_U16, SFX_IRMS2,   false },
    { "IRMS3",    1322,   TY_U16, SFX_IRMS3,   false },
    { "URMS1",    1323,   TY_U16, SFX_URMS1,   false },
    { "URMS2",    1324,   TY_U16, SFX_URMS2,   false },
    { "URMS3",    1325,   TY_U16, SFX_URMS3,   false },
    { "UMOY1",    1326,   TY_U16, SFX_NONE,    false },
    { "UMOY2",    1327,   TY_U16, SFX_NONE,    false },
    { "UMOY3",    1328,   TY_U16, SFX_NONE,    false },
    // Apparent power — per-phase first so SINSTS prefix-matches correctly
    { "SINSTS1",  1329,   TY_U64, SFX_SINSTS1, false },
    { "SINSTS2",  1333,   TY_U64, SFX_SINSTS2, false },
    { "SINSTS3",  1337,   TY_U64, SFX_SINSTS3, false },
    { "SINSTS",   1341,   TY_U64, SFX_SINSTS,  false },
    // Contract / power limits
    { "PREF",     1399,   TY_U16, SFX_PREF,    false },
    { "PCOUP",    1398,   TY_U16, SFX_NONE,    false },
    // Status register
    { "STGE",     4000,   TY_STR, SFX_STGE,    false },
    // Injection apparent power
    { "SINSTI",   1400,   TY_U64, SFX_NONE,    false },
    // Timestamped maximums/averages (require horodate)
    { "SMAXIN-1", 1404,   TY_U64, SFX_NONE,    true  },
    { "SMAXIN",   1408,   TY_U64, SFX_NONE,    true  },
    { "SMAXSN-1", 1345,   TY_U64, SFX_NONE,    true  },
    { "SMAXSN1-1",1345,   TY_U64, SFX_NONE,    true  },
    { "SMAXSN2-1",1353,   TY_U64, SFX_NONE,    true  },
    { "SMAXSN3-1",1357,   TY_U64, SFX_NONE,    true  },
    { "SMAXSN1",  1361,   TY_U64, SFX_NONE,    true  },
    { "SMAXSN2",  1365,   TY_U64, SFX_NONE,    true  },
    { "SMAXSN3",  1369,   TY_U64, SFX_NONE,    true  },
    { "SMAXSN",   1361,   TY_U64, SFX_NONE,    true  },
    { "CCASN-1",  1500,   TY_U64, SFX_NONE,    true  },
    { "CCASN",    1504,   TY_U64, SFX_CCASN,   true  },
    { "CCAIN-1",  1412,   TY_U64, SFX_NONE,    true  },
    { "CCAIN",    1416,   TY_U64, SFX_NONE,    true  },
    // Messages / identifiers
    { "MSG1",     6000,   TY_STR, SFX_NONE,    false },
    { "MSG2",     6100,   TY_STR, SFX_NONE,    false },
    { "PRM",      7000,   TY_STR, SFX_NONE,    false },
    // Load-shedding programme points (timestamped, no register)
    { "DPM1",   0xFFFF,   TY_NONE, SFX_NONE,   true  },
    { "DPM2",   0xFFFF,   TY_NONE, SFX_NONE,   true  },
    { "DPM3",   0xFFFF,   TY_NONE, SFX_NONE,   true  },
    { "FPM1",   0xFFFF,   TY_NONE, SFX_NONE,   true  },
    { "FPM2",   0xFFFF,   TY_NONE, SFX_NONE,   true  },
    { "FPM3",   0xFFFF,   TY_NONE, SFX_NONE,   true  },
    // Miscellaneous (log only)
    { "RELAIS",  0xFFFF,  TY_NONE, SFX_NONE,   false },
    { "NJOURF+1",0xFFFF,  TY_NONE, SFX_NONE,   false },
    { "NJOURF",  0xFFFF,  TY_NONE, SFX_NONE,   false },
    { "PJOURF+1",0xFFFF,  TY_NONE, SFX_NONE,   false },
    { "PPOINTE", 0xFFFF,  TY_NONE, SFX_NONE,   false },
};

static void dispatchSfx(TicSfx sfx, uint64_t v) {

    static bool pf_initialized = false;
    if (!pf_initialized) {
        sdm630WriteFloat(SDM630_L1_POWER_FACTOR, 1.0f);
        sdm630WriteFloat(SDM630_L2_POWER_FACTOR, 1.0f);
        sdm630WriteFloat(SDM630_L3_POWER_FACTOR, 1.0f);
        sdm630WriteFloat(SDM630_TOTAL_POWER_FACTOR, 1.0f);
        pf_initialized = true;
    }

    int32_t  v32  = (v > 0x7FFFFFFFull) ? 0x7FFFFFFF : (int32_t)v;
    uint32_t v32u = (v > 0xFFFFFFFFull) ? 0xFFFFFFFF : (uint32_t)v;
    uint16_t v16  = (uint16_t)v;
    switch (sfx) {

        case SFX_EAST:
            sdm630WriteFloat(SDM630_TOTAL_ACTIVE_ENERGY, (float)v / 1000.0f);
            standardEnergyUpdate(v);
            break;
        case SFX_PREF:
            sdm630WriteFloat(SDM630_CONTRACTED_POWER, (float)v16);
            break;
        case SFX_EASF01:
            s_easf01 = (float)v / 1000.0f;
            sdm630WriteFloat(SDM630_BLUE_HC_KWH, s_easf01);
            sdm630WriteFloat(SDM630_TEMPO_BLUE_KWH, s_easf01 + s_easf02);
            break;
        case SFX_EASF02:
            s_easf02 = (float)v / 1000.0f;
            sdm630WriteFloat(SDM630_BLUE_HP_KWH, s_easf02);
            sdm630WriteFloat(SDM630_TEMPO_BLUE_KWH, s_easf01 + s_easf02);
            break;
        case SFX_EASF03:
            s_easf03 = (float)v / 1000.0f;
            sdm630WriteFloat(SDM630_WHITE_HC_KWH, s_easf03);
            sdm630WriteFloat(SDM630_TEMPO_WHITE_KWH, s_easf03 + s_easf04);
            break;
        case SFX_EASF04:
            s_easf04 = (float)v / 1000.0f;
            sdm630WriteFloat(SDM630_WHITE_HP_KWH, s_easf04);
            sdm630WriteFloat(SDM630_TEMPO_WHITE_KWH, s_easf03 + s_easf04);
            break;
        case SFX_EASF05:
            s_easf05 = (float)v / 1000.0f;
            sdm630WriteFloat(SDM630_RED_HC_KWH, s_easf05);
            sdm630WriteFloat(SDM630_TEMPO_RED_KWH, s_easf05 + s_easf06);
            break;
        case SFX_EASF06:
            s_easf06 = (float)v / 1000.0f;
            sdm630WriteFloat(SDM630_RED_HP_KWH, s_easf06);
            sdm630WriteFloat(SDM630_TEMPO_RED_KWH, s_easf05 + s_easf06);
            break;
        case SFX_EASD01: s_easd01 = (float)v / 1000.0f; sdm630WriteFloat(SDM630_TOTAL_HC_KWH, s_easd01); break;
        case SFX_EASD02: s_easd02 = (float)v / 1000.0f; sdm630WriteFloat(SDM630_TOTAL_HP_KWH, s_easd02); break;
        case SFX_EASD03: s_easd03 = (float)v / 1000.0f; break;
        case SFX_EASD04: s_easd04 = (float)v / 1000.0f; break;
        case SFX_IRMS1: break;
        case SFX_IRMS2: break;
        case SFX_IRMS3: break;
        case SFX_URMS1:
            s_urms1_v = v16;
            sdm630WriteFloat(SDM630_L1_VOLTAGE, (float)v16);
            updateComputedCurrentL1();
            break;
        case SFX_URMS2:
            s_urms2_v = v16;
            s_urms2_set = true;
            sdm630WriteFloat(SDM630_L2_VOLTAGE, (float)v16);
            break;
        case SFX_URMS3:
            s_urms3_v = v16;
            s_urms3_set = true;
            sdm630WriteFloat(SDM630_L3_VOLTAGE, (float)v16);
            break;
        case SFX_SINSTS:
            s_sinsts_va = v32u;
            sdm630WriteFloat(SDM630_TOTAL_APPARENT_VA, (float)v32u);
            break;
        case SFX_SINSTS1:
            s_sinsts1_va = v32u;
            s_sinsts1_set = true;
            sdm630WriteFloat(SDM630_L1_APPARENT_POWER, (float)v32u);
            break;
        case SFX_SINSTS2:
            s_sinsts2_va = v32u;
            sdm630WriteFloat(SDM630_L2_APPARENT_POWER, (float)v32u);
            break;
        case SFX_SINSTS3:
            s_sinsts3_va = v32u;
            sdm630WriteFloat(SDM630_L3_APPARENT_POWER, (float)v32u);
            break;
        case SFX_CCASN:
            s_ccasn_w = v32u;
            sdm630WriteFloat(SDM630_L1_ACTIVE_POWER, (float)v32u);
            updateComputedCurrentL1();
            break;
        case SFX_STGE: {
            extern void updatePowerOverflow(bool overflow);
            updatePowerOverflow((v32u >> 7) & 1u);
        }
        break;
    }
}

// ── Data processor ────────────────────────────────────────────────────────────

bool bDataProcessingStandard(char *au8Command, char *au8Value, uint8_t au8Pos) {
    for (size_t n = 0; n < sizeof(s_table) / sizeof(s_table[0]); ++n) {
        const TicEntry &e = s_table[n];
        if (strcmp(au8Command, e.label) != 0) continue;

        uint64_t v64 = 0;

        switch (e.type) {
            case TY_U64:
                v64 = (uint64_t)strtoull(au8Value, NULL, 10);

                dispatchSfx(e.sfx, v64);
                break;
            case TY_U16: {
                uint16_t v16 = (uint16_t)atoi(au8Value);

                dispatchSfx(e.sfx, (uint64_t)v16);
                break;
            }
                        case TY_STR:
                if (e.sfx == SFX_NTARF) {
                    sdm630WriteFloat(SDM630_IS_TEMPO_BLUE, 0.0f);
                    sdm630WriteFloat(SDM630_IS_TEMPO_WHITE, 0.0f);
                    sdm630WriteFloat(SDM630_IS_TEMPO_RED, 0.0f);
                    sdm630WriteFloat(SDM630_IS_HP, 0.0f);
                    sdm630WriteFloat(SDM630_IS_HC, 0.0f);
                    sdm630WriteFloat(SDM630_IS_BASE_TARIFF, 0.0f);
                    sdm630WriteFloat(SDM630_IS_HPHC_TARIFF, 0.0f);
                    sdm630WriteFloat(SDM630_IS_TEMPO_TARIFF, 0.0f);

                    String t = String(au8Value);
                    t.toUpperCase();

                    if (t.indexOf("BLEU") >= 0) { sdm630WriteFloat(SDM630_IS_TEMPO_BLUE, 1.0f); sdm630WriteFloat(SDM630_IS_TEMPO_TARIFF, 1.0f); }
                    if (t.indexOf("BLANC") >= 0) { sdm630WriteFloat(SDM630_IS_TEMPO_WHITE, 1.0f); sdm630WriteFloat(SDM630_IS_TEMPO_TARIFF, 1.0f); }
                    if (t.indexOf("ROUGE") >= 0) { sdm630WriteFloat(SDM630_IS_TEMPO_RED, 1.0f); sdm630WriteFloat(SDM630_IS_TEMPO_TARIFF, 1.0f); }
                    if (t.indexOf("HP") >= 0) { sdm630WriteFloat(SDM630_IS_HP, 1.0f); sdm630WriteFloat(SDM630_IS_HPHC_TARIFF, 1.0f); }
                    if (t.indexOf("HC") >= 0) { sdm630WriteFloat(SDM630_IS_HC, 1.0f); sdm630WriteFloat(SDM630_IS_HPHC_TARIFF, 1.0f); }
                    if (t.indexOf("BASE") >= 0) { sdm630WriteFloat(SDM630_IS_BASE_TARIFF, 1.0f); }
                } else if (e.sfx == SFX_STGE) {
                    uint32_t stge = strtoul(au8Value, NULL, 16);
                    dispatchSfx(e.sfx, stge);
                } else if (e.sfx != SFX_NONE) {
                    uint64_t hv = (uint64_t)strtoull(au8Value, NULL, 16);
                    dispatchSfx(e.sfx, hv);
                }
                break;
            case TY_NONE:
                break;
        }

        if (ConfigSettings.enableDebug)
            Serial.printf("%s : %s\r\n", au8Command, au8Value);

        if (e.need_horodate && au8Pos != 3) {
            u8ErrorDecode = 3;
            return false;
        }
        return true;
    }

    return true;
}

// ── TIC standard-mode frame parser ───────────────────────────────────────────
//
// Frame wire format (standard mode, 9600 baud 7E1):
//   STX
//   LF <label> TAB [<horodate> TAB] <value> TAB <checksum> CR LF  (repeated)
//   ETX CR
//
// Returns true (one complete valid line ready) only when CR arrives with a
// matching checksum.  The caller checks *error==1 to distinguish a CRC/framing
// error from the normal "still accumulating" false return.
//
// For DATE the horodate field IS the value; the parser copies date→value before
// returning so bDataProcessingStandard always receives the payload in au8Value.

bool bTranscodeCharTIC(unsigned int /*u16MaxLength*/, char *command, char *date,
                       char *value, uint8_t *error, uint8_t *posFinal,
                       byte u8Data, bool *lf)
{
    switch (u8Data) {
        case 0x0A:
            s_crc_acc  = 0;
            s_crc_snap = 0;
            command[0] = '\0';
            date[0]    = '\0';
            value[0]   = '\0';
            *error     = 0;
            *posFinal  = 0;
            s_pos      = 0;
            s_idx      = 0;
            break;

        case 0x09:  // TAB field separator
            if (*lf) {
                s_pos++;
                *posFinal  = s_pos;
                s_idx      = 0;
                s_crc_acc += 0x09;
                s_crc_snap = s_crc_acc;
            } else {
                *error = 1;
                Serial.print("\r\nError separator");
                return false;
            }
            break;

        case 0x0D:  // CR — end of data line, validate checksum
            if (!*lf) {
                *error = 1;
                Serial.print("\r\nLF Missing");
                return false;
            }
            *lf = false;

            if (s_pos == 2) {
                // No horodate: label TAB value TAB checksum
                // s_crc_snap holds sum(label+TAB+value+TAB) at this point.
                // Reject labels that are specified always to carry a horodate.
                if ((memcmp(command, "SMAX", 4) != 0) &&
                    (memcmp(command, "CCA",  3) != 0) &&
                    (memcmp(command, "UMOY", 4) != 0) &&
                    (memcmp(command, "DMP",  3) != 0) &&
                    (memcmp(command, "FPM",  3) != 0)) {
                    uint8_t computed = (s_crc_snap & 0x3F) + 0x20;
                    if (computed == s_crc_rx) {
                        memcpy(value, date, 256);  // date holds the value field for pos==2
                        return true;
                    }
                    Serial.printf("command : %s Error CRC : tmp=%d - reçu=%d\r\n",
                                  command, computed, s_crc_rx);
                    *error = 1;
                    return false;
                }
                *error = 1;
                Serial.print("\r\nDatas miss");
                return false;

            } else if (s_pos == 3) {
                // With horodate: label TAB horodate TAB value TAB checksum
                uint8_t computed = (s_crc_snap & 0x3F) + 0x20;
                if (computed == s_crc_rx) {
                    if (memcmp(command, "DATE", 4) == 0)
                        memcpy(value, date, 256);  // for DATE, horodate is the payload
                    return true;
                }
                Serial.printf("HORODATE - command : %s Error CRC : tmp=%d - reçu=%d\r\n",
                              command, computed, s_crc_rx);
                *error = 1;
                return false;

            } else if (s_pos == 0) {
                *error = 1;
                Serial.printf("\r\nincorrect position : %d", s_pos);
                return false;
            }
            break;

        case 0x02:
        case 0x03:
        case 0x04:
            break;

        default:
            if (!*lf) {
                *error = 1;
                Serial.print("\r\nLF Missing : No valid data");
                return false;
            }
            if (s_pos == 0) {
                command[s_idx++]  = (char)u8Data;
                command[s_idx]    = '\0';
                s_crc_acc        += u8Data;
                s_crc_snap        = s_crc_acc;
            } else if (s_pos == 1) {
                date[s_idx++]     = (char)u8Data;
                date[s_idx]       = '\0';
                s_crc_acc        += u8Data;
                s_crc_snap        = s_crc_acc;
            } else if (s_pos == 2) {
                value[s_idx++]    = (char)u8Data;
                value[s_idx]      = '\0';
                s_crc_acc        += u8Data;
                s_crc_rx          = u8Data;  // last byte at pos==2 will be the checksum
            } else {
                s_crc_rx = u8Data;  // checksum byte at pos==3
            }
            break;
    }

    *error = 0;
    return false;
}
