#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "Teleinformation.h"

bool started=false;
unsigned int pos=0;
byte u8CRC=0;
byte u8TmpCRCHorodate=0;
byte u8TmpCRC=0;
unsigned int i=0;



extern uint16_t holdingRegisters[24600];
extern uint32_t u32Timeout;
extern uint8_t u8ErrorDecode;

// SmartEVSE v3.1 mirror registers (0-19) — INT32 big-endian (high word first)
// Configure SmartEVSE Custom Meter: FC=3, INT32, endian HBF_HWF(3), IDivisor=3, UDivisor=1, PDivisor=0
#define SE_IRMS1_REG  0   // Current L1 (mA)
#define SE_IRMS2_REG  2   // Current L2 (mA)
#define SE_IRMS3_REG  4   // Current L3 (mA)
#define SE_URMS1_REG  6   // Voltage L1 (V*10)
#define SE_URMS2_REG  8   // Voltage L2 (V*10)
#define SE_URMS3_REG  10  // Voltage L3 (V*10)
#define SE_PAPP_REG   12  // Total apparent power (VA)
#define SE_PAPP1_REG  14  // L1 apparent power (VA)
#define SE_PAPP2_REG  16  // L2 apparent power (VA)
#define SE_PAPP3_REG  18  // L3 apparent power (VA)

static void seWriteInt32(uint16_t reg, int32_t value) {
    holdingRegisters[reg]     = (uint16_t)((value >> 16) & 0xFFFF);
    holdingRegisters[reg + 1] = (uint16_t)(value & 0xFFFF);
}

// High-precision current registers — total and per-phase
// Each slot: [float_reg, float_reg+1] = Float32 big-endian (A)  Option A
//            [u16_reg]                = UInt16 × 100 (cA)       Option B
#define SE_PRECISE_I_FLOAT_REG   20  // total: regs 20-21
#define SE_PRECISE_I_U16_REG     22
#define SE_PRECISE_I1_FLOAT_REG  23  // L1:    regs 23-24
#define SE_PRECISE_I1_U16_REG    25
#define SE_PRECISE_I2_FLOAT_REG  26  // L2:    regs 26-27
#define SE_PRECISE_I2_U16_REG    28
#define SE_PRECISE_I3_FLOAT_REG  29  // L3:    regs 29-30
#define SE_PRECISE_I3_U16_REG    31

// Power factor (cos phi) registers — total only (per-phase active power not in TIC)
// cos phi = CCASN (10-min avg active power, W) / SINSTS (instantaneous apparent power, VA)
#define SE_COSPHI_FLOAT_REG      32  // Total cos phi FLOAT32, regs 32-33
#define SE_COSPHI_U16_REG        34  // Total cos phi × 1000, UINT16, reg 34

static uint32_t s_sinsts_va  = 0;    // total apparent power (SINSTS / PAPP)
static uint32_t s_sinsts1_va = 0;    // L1 apparent power (SINSTS1)
static uint32_t s_sinsts2_va = 0;    // L2 apparent power (SINSTS2)
static uint32_t s_sinsts3_va = 0;    // L3 apparent power (SINSTS3)
static uint16_t s_urms1_v    = 230;  // L1 voltage in V (default 230 V)
static uint16_t s_urms2_v    = 230;  // L2 voltage in V (default 230 V)
static uint16_t s_urms3_v    = 230;  // L3 voltage in V (default 230 V)
static uint32_t s_ccasn_w     = 0;    // CCASN 10-min avg active power (W) — fallback only

// ── Energy-delta active power estimation (primary cos phi source) ─────────────
// Records millis() only when the Wh index actually increments, eliminating
// quantisation noise from frames where the counter hasn't changed.
// EMA (alpha=0.25) smooths the instantaneous P_raw computed on each increment.
// Falls back to CCASN when no increment has arrived in > 60 s (very low load).
static uint64_t s_east_wh      = 0;   // last EAST (standard mode)
static uint64_t s_hchc_wh      = 0;   // last BASE / HCHC / EJPHN / BBRHCJB
static uint64_t s_hchp_wh      = 0;   // last HCHP / EJPHPM / BBRHPJB
static uint64_t s_hchcjw_wh    = 0;   // last BBRHCJW (Tempo white-day HC)
static uint64_t s_hchpjw_wh    = 0;   // last BBRHPJW (Tempo white-day HP)
static uint64_t s_hchcjr_wh    = 0;   // last BBRHCJR (Tempo red-day HC)
static uint64_t s_hchpjr_wh    = 0;   // last BBRHPJR (Tempo red-day HP)
static uint64_t s_last_histo_wh = 0;  // historic total at last P computation
static uint32_t s_energy_ms    = 0;   // millis() at last Wh increment
static float    s_power_ema_w  = 0.0f; // EMA-smoothed active power (W)
static bool     s_east_init    = false; // EAST baseline recorded
static bool     s_histo_init   = false; // historic baseline recorded

static void seWriteFloat32(uint16_t reg, float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    holdingRegisters[reg]     = (uint16_t)(bits >> 16);
    holdingRegisters[reg + 1] = (uint16_t)(bits & 0xFFFF);
}

static void seWritePreciseCurrent(uint16_t float_reg, uint16_t u16_reg, uint32_t sinsts_va, uint16_t urms_v) {
    float denom = (urms_v > 0) ? (float)urms_v : 230.0f;
    float a = (float)sinsts_va / denom;
    seWriteFloat32(float_reg, a);
    holdingRegisters[u16_reg] = (uint16_t)(a * 100.0f + 0.5f);
}

static void seUpdatePreciseCurrent()   { seWritePreciseCurrent(SE_PRECISE_I_FLOAT_REG,  SE_PRECISE_I_U16_REG,  s_sinsts_va,  s_urms1_v); }
static void seUpdatePreciseCurrentL1() { seWritePreciseCurrent(SE_PRECISE_I1_FLOAT_REG, SE_PRECISE_I1_U16_REG, s_sinsts1_va, s_urms1_v); }
static void seUpdatePreciseCurrentL2() { seWritePreciseCurrent(SE_PRECISE_I2_FLOAT_REG, SE_PRECISE_I2_U16_REG, s_sinsts2_va, s_urms2_v); }
static void seUpdatePreciseCurrentL3() { seWritePreciseCurrent(SE_PRECISE_I3_FLOAT_REG, SE_PRECISE_I3_U16_REG, s_sinsts3_va, s_urms3_v); }

// seUpdateCosPhi: write cos phi registers from current EMA or CCASN fallback.
// Called after every energy increment and on SINSTS/PAPP/CCASN frames.
static void seUpdateCosPhi() {
    if (s_sinsts_va == 0) return;
    bool energy_seen = s_east_init || s_histo_init;
    bool stale       = energy_seen && ((uint32_t)millis() - s_energy_ms) > 60000u;
    float P = (!energy_seen || stale) ? (float)s_ccasn_w : s_power_ema_w;
    float cos_phi = P / (float)s_sinsts_va;
    if (cos_phi > 1.0f) cos_phi = 1.0f;
    seWriteFloat32(SE_COSPHI_FLOAT_REG, cos_phi);
    holdingRegisters[SE_COSPHI_U16_REG] = (uint16_t)(cos_phi * 1000.0f + 0.5f);
}

// applyEnergyDelta: compute P from a confirmed Wh increment and feed the EMA.
// dt guard (500 ms) rejects anomalously fast back-to-back frames.
// Stale gap (>60 s): reset EMA so next valid delta seeds fresh (avoids near-zero
// first-sample from a large idle gap poisoning the average).
// Large load change (P_raw > 2× or < 0.5× EMA): re-seed directly for fast response.
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

// standardEnergyUpdate: called on every incoming EAST frame.
static void standardEnergyUpdate(uint64_t wh) {
    uint32_t now = (uint32_t)millis();
    if (!s_east_init) {
        s_east_wh = wh; s_energy_ms = now; s_east_init = true; return;
    }
    if (wh > s_east_wh) {
        applyEnergyDelta(wh - s_east_wh, now);
        s_east_wh = wh;
        seUpdateCosPhi();
    }
}

// histoEnergyUpdate: called after any historic tariff index changes.
// Computes the total across all active sub-indices (BASE/HC/HP + Tempo groups).
static void histoEnergyUpdate() {
    uint64_t total = s_hchc_wh + s_hchp_wh
                   + s_hchcjw_wh + s_hchpjw_wh
                   + s_hchcjr_wh + s_hchpjr_wh;
    uint32_t now = (uint32_t)millis();
    if (!s_histo_init) {
        s_last_histo_wh = total; s_energy_ms = now; s_histo_init = true; return;
    }
    if (total > s_last_histo_wh) {
        applyEnergyDelta(total - s_last_histo_wh, now);
        s_last_histo_wh = total;
        seUpdateCosPhi();
    }
}

bool bDataProcessingHisto(char *au8Command,char *au8Value, uint8_t au8Pos)
{
  if (memcmp(au8Command,"ADCO",4)==0)
  {

    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[303] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[302] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[301] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[300] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("ADCO : ");
    Serial.println(au8Value);

  }else if (memcmp(au8Command,"MOTDETAT",8)==0)
  {
    Serial.print("MOTDETAT : ");
    Serial.println(au8Value);
  }else if (memcmp(au8Command,"BASE",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1007] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1006] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1005] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1004] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_hchc_wh = (uint64_t)tmp; histoEnergyUpdate();
    Serial.print("BASE : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"HHPHC",5)==0)
  {
    Serial.print("HHPHC : ");
    Serial.println(au8Value);
    size_t len = strlen(au8Value);
    for (size_t i = 0; i < len; ++i)
    {
      holdingRegisters[5000+i] = static_cast<uint16_t>(au8Value[i]);
    }
  }else if (memcmp(au8Command,"OPTARIF",7)==0)
  {
    Serial.print("OPTARIF : ");
    Serial.println(au8Value);
    size_t len = strlen(au8Value);
    for (size_t i = 0; i < len; ++i)
    {
      holdingRegisters[2000+i] = static_cast<uint16_t>(au8Value[i]);
    }
  }else if (memcmp(au8Command,"ISOUSC",6)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1399] = (uint16_t)tmp & 0xFFFF;
    Serial.print("ISOUSC : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"HCHC",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1007] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1006] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1005] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1004] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_hchc_wh = (uint64_t)tmp; histoEnergyUpdate();
    Serial.print("HCHC : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"HCHP",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1011] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1010] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1009] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1008] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_hchp_wh = (uint64_t)tmp; histoEnergyUpdate();
    Serial.print("HCHP : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"EJPHN",5)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1007] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1006] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1005] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1004] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_hchc_wh = (uint64_t)tmp; histoEnergyUpdate();
    Serial.print("EJPHN : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"EJPHPM",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1011] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1010] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1009] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1008] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_hchp_wh = (uint64_t)tmp; histoEnergyUpdate();
    Serial.print("EJPHPM : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHCJB",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1007] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1006] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1005] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1004] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_hchc_wh = (uint64_t)tmp; histoEnergyUpdate();
    Serial.print("BBRHCJB : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHPJB",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1011] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1010] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1009] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1008] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_hchp_wh = (uint64_t)tmp; histoEnergyUpdate();
    Serial.print("BBRHPJB : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHCJW",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1015] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1014] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1013] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1012] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_hchcjw_wh = (uint64_t)tmp; histoEnergyUpdate();
    Serial.print("BBRHCJW : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHPJW",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1019] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1018] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1017] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1016] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_hchpjw_wh = (uint64_t)tmp; histoEnergyUpdate();
    Serial.print("BBRHPJW : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHCJR",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1023] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1022] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1021] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1020] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_hchcjr_wh = (uint64_t)tmp; histoEnergyUpdate();
    Serial.print("BBRHCJR : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHPJR",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1027] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1026] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1025] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1024] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_hchpjr_wh = (uint64_t)tmp; histoEnergyUpdate();
    Serial.print("BBRHPJR : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"IINST1",6)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1320] = (uint16_t)tmp & 0xFFFF;
    seWriteInt32(SE_IRMS1_REG, (int32_t)tmp * 1000);
    Serial.print("IINST1 : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"IINST2",6)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1321] = (uint16_t)tmp & 0xFFFF;
    seWriteInt32(SE_IRMS2_REG, (int32_t)tmp * 1000);
    Serial.print("IINST2 : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"IINST3",6)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1322] = (uint16_t)tmp & 0xFFFF;
    seWriteInt32(SE_IRMS3_REG, (int32_t)tmp * 1000);
    Serial.print("IINST3 : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"IINST",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1320] = (uint16_t)tmp & 0xFFFF;
    // Single-phase: mirror on L1, zero out L2/L3
    seWriteInt32(SE_IRMS1_REG, (int32_t)tmp * 1000);
    seWriteInt32(SE_IRMS2_REG, 0);
    seWriteInt32(SE_IRMS3_REG, 0);
    Serial.print("IINST : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"IMAX1",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1600] = (uint16_t)tmp & 0xFFFF;
    Serial.print("IMAX1 : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"IMAX2",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1601] = (uint16_t)tmp & 0xFFFF;
    Serial.print("IMAX2 : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"IMAX3",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1602] = (uint16_t)tmp & 0xFFFF;
    Serial.print("IMAX3 : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"IMAX",4)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1600] = (uint16_t)tmp & 0xFFFF;

    Serial.print("IMAX : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"PMAX",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1364] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1363] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1362] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1361] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("PMAX : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"PAPP",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);

    holdingRegisters[1332] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1331] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1330] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1329] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    // Single-phase: all power on L1, zero L2/L3
    int32_t se_papp = (int32_t)(tmp > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)tmp);
    seWriteInt32(SE_PAPP_REG,  se_papp);
    seWriteInt32(SE_PAPP1_REG, se_papp);
    seWriteInt32(SE_PAPP2_REG, 0);
    seWriteInt32(SE_PAPP3_REG, 0);
    // Historic meters have no URMS1; s_urms1_v stays at its default (230 V)
    s_sinsts_va = (uint32_t)(tmp > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32_t)tmp);
    seUpdatePreciseCurrent();
    seUpdateCosPhi();
    Serial.print("PAPP : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"PTEC",4)==0)
  {
    long tmp = atol(au8Value);
    Serial.print("PTEC : ");
    Serial.println(tmp);
    size_t len = strlen(au8Value);
    for (size_t i = 0; i < len; ++i)
    {
      holdingRegisters[2200+i] = static_cast<uint16_t>(au8Value[i]);
    }
  }else if (memcmp(au8Command,"ADPS",4)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1700] = (uint16_t)tmp & 0xFFFF;
    Serial.print("ADPS : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"ADIR1",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1603] = (uint16_t)tmp & 0xFFFF;
    Serial.print("ADIR1 : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"ADIR2",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1604] = (uint16_t)tmp & 0xFFFF;
    Serial.print("ADIR2 : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"ADIR3",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1605] = (uint16_t)tmp & 0xFFFF;
    Serial.print("ADIR3 : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"DEMAIN",6)==0)
  {
    Serial.print("DEMAIN : ");
    Serial.println(au8Value);
    size_t len = strlen(au8Value);
    for (size_t i = 0; i < len; ++i)
    {
      holdingRegisters[2300+i] = static_cast<uint16_t>(au8Value[i]);
    }
  }else if (memcmp(au8Command,"PPOT",4)==0)
  {
    uint16_t tmp = atoi(au8Value);
    Serial.print("PPOT : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"PEJP",4)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1606] = (uint16_t)tmp & 0xFFFF;
    Serial.print("PEJP : ");
    Serial.println(tmp);
  }else{
     //vérifier fichier spécifique
    const char* path ="/modbus/registre_spec.json";
    
    File RegFile = LittleFS.open(path, FILE_READ);
    if (!RegFile || RegFile.isDirectory()) 
    {
      log_e("failed open");
      
    }else
    {
      DynamicJsonDocument temp(100000);
      deserializeJson(temp,RegFile);

      RegFile.close();
      
      int i=0;     
      if (temp.containsKey("historique"))
      {
          JsonArray StatusArray = temp["historique"].as<JsonArray>();
          for(JsonVariant v : StatusArray) 
          {
            size_t cmd_len = strlen(temp["historique"][i]["command"].as<String>().c_str());
            if (memcmp(au8Command,temp["historique"][i]["command"].as<String>().c_str(),cmd_len)==0)
            {
              int size = temp["historique"][i]["size"].as<int>();
              int reg = temp["historique"][i]["reg"].as<int>();
              if (memcmp(temp["historique"][i]["type"].as<String>().c_str(),"numeric",7)==0)
              {
                long long tmp = strtoull(au8Value,NULL,10);
                int k = size;
                for (int j=0;j<size;j++)
                {              
                  holdingRegisters[reg+j] = (uint16_t)(tmp >> ((k-1)*16) ) & 0xFFFF;
                  k--;
                }
              }else if (memcmp(temp["historique"][i]["type"].as<String>().c_str(),"string",6)==0)
              {
                for (int j=0;j<size;j++)
                {
                  holdingRegisters[reg+j]=static_cast<uint16_t>(au8Value[j]);
                }
              }
            }
            i++;
          }
      }
          
    }  
    Serial.printf("%s : %s\r\n",au8Command,au8Value);
  }
  return true;
}

bool bDataProcessingStandard(char *au8Command,char *au8Value, uint8_t au8Pos)
{
  if (memcmp(au8Command,"ADSC",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    /*holdingRegisters[303] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[302] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[301] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[300] = (uint16_t)(tmp >> 48 ) & 0xFFFF;*/

    for (i = 0; i < 4; ++i) {
        holdingRegisters[303-i] = tmp & 0xFFFF; // Prendre les 16 bits de poids faibles
        tmp >>= 16; // Décalage à droite de 16 bits
    }


    Serial.print("ADSC : ");
    Serial.println(au8Value);

  }else if (memcmp(au8Command,"NGTF",4)==0)
  {
    Serial.print("NGTF : ");
    Serial.println(au8Value);
    size_t len = strlen(au8Value);
    for (size_t i = 0; i < len; ++i)
    {    
      holdingRegisters[2000+i] = static_cast<uint16_t>(au8Value[i]);
    }

  }else if (memcmp(au8Command,"LTARF",5)==0)
  {   
    Serial.print("LTARF : ");
    Serial.println(au8Value);
    size_t len = strlen(au8Value);
    for (size_t i = 0; i < len; ++i)
    {
      holdingRegisters[2100+i] = static_cast<uint16_t>(au8Value[i]);
    }
  }else if (memcmp(au8Command,"NTARF",5)==0)
  {   
    uint16_t tmp = atoi(au8Value);
    Serial.print("NTARF : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"VTIC",4)==0)
  {
    uint16_t tmp = atoi(au8Value);
    Serial.print("VTIC : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"DATE",4)==0)
  {
    Serial.print("DATE : ");
    Serial.println(au8Value);
    size_t len = strlen(au8Value);
    for (size_t i = 0; i < len; ++i)
    {
      holdingRegisters[3000+i] = static_cast<uint16_t>(au8Value[i]);
    }

  }else if (memcmp(au8Command,"EAST",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);

    holdingRegisters[1003] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1002] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1001] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1000] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    standardEnergyUpdate((uint64_t)tmp);

    Serial.print("EAST : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASF01",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1007] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1006] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1005] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1004] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASF01 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASF02",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1011] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1010] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1009] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1008] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASF02 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASF03",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1015] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1014] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1013] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1012] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASF03 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASF04",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1019] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1018] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1017] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1016] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASF04 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASF05",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1023] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1022] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1021] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1020] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    
    Serial.print("EASF05 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASF06",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1027] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1026] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1025] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1024] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASF06 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASF07",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1031] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1030] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1029] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1028] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASF07 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASF08",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1035] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1034] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1033] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1032] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASF08 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASF09",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1039] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1038] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1037] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1036] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASF09 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASF10",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1043] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1042] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1041] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1040] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASF10 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASD01",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1103] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1102] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1101] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1100] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASD01 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASD02",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1107] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1106] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1105] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1104] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASD02 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASD03",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1111] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1110] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1109] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1108] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASD03 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EASD04",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1115] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1114] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1113] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1112] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EASD04 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"EAIT",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1203] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1202] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1201] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1200] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("EAIT : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"ERQ1",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1303] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1302] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1301] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1300] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("ERQ1 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"ERQ2",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1307] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1306] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1305] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1304] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("ERQ2 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"ERQ3",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1311] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1310] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1309] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1308] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("ERQ3 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"ERQ4",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1315] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1314] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1313] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1312] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("ERQ4 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"IRMS1",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1320] = (uint16_t)tmp & 0xFFFF;
    seWriteInt32(SE_IRMS1_REG, (int32_t)tmp * 1000);

    Serial.print("IRMS1 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"IRMS2",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1321] = (uint16_t)tmp & 0xFFFF;
    seWriteInt32(SE_IRMS2_REG, (int32_t)tmp * 1000);

    Serial.print("IRMS2 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"IRMS3",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1322] = (uint16_t)tmp & 0xFFFF;
    seWriteInt32(SE_IRMS3_REG, (int32_t)tmp * 1000);

    Serial.print("IRMS3 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"URMS1",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1323] = (uint16_t)tmp & 0xFFFF;
    seWriteInt32(SE_URMS1_REG, (int32_t)tmp * 10);
    s_urms1_v = tmp;
    seUpdatePreciseCurrent();
    seUpdatePreciseCurrentL1();

    Serial.print("URMS1 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"URMS2",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1324] = (uint16_t)tmp & 0xFFFF;
    seWriteInt32(SE_URMS2_REG, (int32_t)tmp * 10);
    s_urms2_v = tmp;
    seUpdatePreciseCurrentL2();

    Serial.print("URMS2 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"URMS3",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1325] = (uint16_t)tmp & 0xFFFF;
    seWriteInt32(SE_URMS3_REG, (int32_t)tmp * 10);
    s_urms3_v = tmp;
    seUpdatePreciseCurrentL3();

    Serial.print("URMS3 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"PREF",4)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1399] = (uint16_t)tmp & 0xFFFF;

    Serial.print("PREF : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"STGE",4)==0)
  {
    long tmp = atol(au8Value);
    Serial.print("STGE : ");
    Serial.println(au8Value);
    size_t len = strlen(au8Value);
    for (size_t i = 0; i < len; ++i)
    {
      holdingRegisters[4000+i] = static_cast<uint16_t>(au8Value[i]);
    }

  }else if (memcmp(au8Command,"PCOUP",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1398] = (uint16_t)tmp & 0xFFFF;
    Serial.print("PCOUP : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"SINSTI",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1403] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1402] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1401] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1400] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("SINSTI : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"SMAXIN-1",8)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1407] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1406] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1405] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1404] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    
    Serial.print("SMAXIN-1 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"SMAXIN",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1411] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1410] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1409] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1408] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("SMAXIN: ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"CCASN-1",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1503] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1502] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1501] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1500] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("CCASN-1: ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"CCASN",5)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);

    holdingRegisters[1507] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1506] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1505] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1504] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    s_ccasn_w = (uint32_t)(tmp > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32_t)tmp);
    seUpdateCosPhi();
    Serial.print("CCASN: ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"CCAIN-1",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1415] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1414] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1413] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1412] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("CCAIN-1: ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"CCAIN",5)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1419] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1418] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1417] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1416] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("CCAIN: ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"UMOY1",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1326] = (uint16_t)tmp & 0xFFFF;
    Serial.print("UMOY1 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"UMOY2",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1327] = (uint16_t)tmp & 0xFFFF;
    Serial.print("UMOY2 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"UMOY3",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1328] = (uint16_t)tmp & 0xFFFF;
    Serial.print("UMOY3 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"SINSTS1",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);

    holdingRegisters[1332] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1331] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1330] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1329] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    seWriteInt32(SE_PAPP1_REG, (int32_t)(tmp > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)tmp));
    s_sinsts1_va = (uint32_t)(tmp > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32_t)tmp);
    seUpdatePreciseCurrentL1();

    Serial.print("SINSTS1 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"SINSTS2",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);

    holdingRegisters[1336] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1335] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1334] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1333] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    seWriteInt32(SE_PAPP2_REG, (int32_t)(tmp > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)tmp));
    s_sinsts2_va = (uint32_t)(tmp > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32_t)tmp);
    seUpdatePreciseCurrentL2();

    Serial.print("SINSTS2 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"SINSTS3",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);

    holdingRegisters[1340] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1339] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1338] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1337] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    seWriteInt32(SE_PAPP3_REG, (int32_t)(tmp > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)tmp));
    s_sinsts3_va = (uint32_t)(tmp > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32_t)tmp);
    seUpdatePreciseCurrentL3();

    Serial.print("SINSTS3 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"SINSTS",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);

    holdingRegisters[1344] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1343] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1342] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1341] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    seWriteInt32(SE_PAPP_REG, (int32_t)(tmp > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)tmp));
    s_sinsts_va = (uint32_t)(tmp > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32_t)tmp);
    seUpdatePreciseCurrent();
    seUpdateCosPhi();

    Serial.print("SINSTS : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"SMAXSN-1",8)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1348] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1347] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1346] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1345] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("SMAXSN-1 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }
  }else if (memcmp(au8Command,"SMAXSN1-1",9)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1348] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1347] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1346] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1345] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("SMAXSN1-1 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }
  }else if (memcmp(au8Command,"SMAXSN2-1",9)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1356] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1355] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1354] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1353] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("SMAXSN2-1 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }
  }else if (memcmp(au8Command,"SMAXSN3-1",9)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1360] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1359] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1358] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1357] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("SMAXSN3-1 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }
  }else if (memcmp(au8Command,"SMAXSN1",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1364] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1363] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1362] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1361] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("SMAXSN1 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }
  }else if (memcmp(au8Command,"SMAXSN2",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1368] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1367] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1366] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1365] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("SMAXSN2 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }
  }else if (memcmp(au8Command,"SMAXSN3",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1372] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1371] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1370] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1369] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("SMAXSN3 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }
  }else if (memcmp(au8Command,"SMAXSN",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1364] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1363] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1362] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1361] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("SMAXSN : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }
  }else if (memcmp(au8Command,"MSG1",4)==0)
  {
    Serial.print("MSG1 : ");
    Serial.println(au8Value);
    size_t len = strlen(au8Value);
    for (size_t i = 0; i < len; ++i)
    {
      holdingRegisters[6000+i] = static_cast<uint16_t>(au8Value[i]);
    }
  }else if (memcmp(au8Command,"MSG2",4)==0)
  {
    Serial.print("MSG2 : ");
    Serial.println(au8Value);
    size_t len = strlen(au8Value);
    for (size_t i = 0; i < len; ++i)
    {
      holdingRegisters[6100+i] = static_cast<uint16_t>(au8Value[i]);
    }
  }else if (memcmp(au8Command,"PRM",3)==0)
  {
    Serial.print("PRM : ");
    Serial.println(au8Value);
    size_t len = strlen(au8Value);
    for (size_t i = 0; i < len; ++i)
    {
      holdingRegisters[7000+i] = static_cast<uint16_t>(au8Value[i]);
    }
  }else if (memcmp(au8Command,"DPM1",4)==0)
  {
    uint16_t tmp = atoi(au8Value);
    Serial.print("DPM1 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"FPM1",4)==0)
  {
    uint16_t tmp = atoi(au8Value);
    Serial.print("FPM1 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"DPM2",4)==0)
  {
    uint16_t tmp = atoi(au8Value);
    Serial.print("DPM2 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"FPM2",4)==0)
  {
    uint16_t tmp = atoi(au8Value);
    Serial.print("FPM2 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"DPM3",4)==0)
  {
    uint16_t tmp = atoi(au8Value);
    Serial.print("DPM3 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"FPM3",4)==0)
  {
    uint16_t tmp = atoi(au8Value);
    Serial.print("FPM3 : ");
    Serial.println(tmp);
    if (au8Pos != 3)
    {
      u8ErrorDecode = 3;
      return false;
    }

  }else if (memcmp(au8Command,"RELAIS",6)==0)
  {
    long tmp = atol(au8Value);
    Serial.print("RELAIS : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"NJOURF+1",8)==0)
  {
    uint16_t tmp = atoi(au8Value);
    Serial.print("NJOURF+1 : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"NJOURF",6)==0)
  {
    uint16_t tmp = atoi(au8Value);
    Serial.print("NJOURF : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"PJOURF+1",8)==0)
  {
    Serial.print("PJOURF+1 : ");
    Serial.println(au8Value);
  }else if (memcmp(au8Command,"PPOINTE",7)==0)
  {
    Serial.print("PPOINTE : ");
    Serial.println(au8Value);
  }else{
    //vérifier fichier spécifique

    const char* path ="/modbus/registres_spec.json";
    
    File RegFile = LittleFS.open(path, FILE_READ);
    if (!RegFile || RegFile.isDirectory()) 
    {
      log_e("failed open");
      
    }else
    {
      log_e("open OK");
      DynamicJsonDocument temp(100000);
      deserializeJson(temp,RegFile);

      RegFile.close();
      
      int i=0;     
      if (temp.containsKey("standard"))
      {
          JsonArray StatusArray = temp["standard"].as<JsonArray>();
          for(JsonVariant v : StatusArray) 
          {
            size_t cmd_len = strlen(temp["standard"][i]["command"].as<String>().c_str());
            if (memcmp(au8Command,temp["standard"][i]["command"].as<String>().c_str(),cmd_len)==0)
            {
              if (memcmp(temp["standard"][i]["type"].as<String>().c_str(),"numeric",7)==0)
              {
                long long tmp = strtoull(au8Value,NULL,10);  
                int size = temp["standard"][i]["size"].as<int>();
                int reg = temp["standard"][i]["reg"].as<int>();
                int k = size;
                for (int j=0;j<size;j++)
                {              
                  holdingRegisters[reg+j] = (uint16_t)(tmp >> ((k-1)*16) ) & 0xFFFF;
                  k--;
                }
              }else if (memcmp(temp["standard"][i]["type"].as<String>().c_str(),"string",6)==0)
              {
                int size = temp["standard"][i]["size"].as<int>();
                int reg = temp["standard"][i]["reg"].as<int>();
                for (int j=0;j<size;j++)
                {
                  holdingRegisters[reg+j]=static_cast<uint16_t>(au8Value[j]);
                }
              }
            }
            i++;
          }
      }
          
    }  
    //Serial.printf("%s : %s\r\n",au8Command,au8Value);
  }
  return true;
}


bool bTranscodeCharTICStandard(unsigned int u16MaxLength, char *command, char *date, char *value,uint8_t *error, uint8_t *posFinal,byte u8Data, bool *lf)
{

  
  switch(u8Data)
  {
    case 0x0A:
      u8TmpCRC=0;
      u8TmpCRCHorodate=0;
      command=0;
      value=0;
      date=0;
      *error=0;
      *posFinal=0;
      pos=0;
      i=0;
      break;
    case 0x09:
      if (*lf)
      {
        pos++;
        *posFinal = pos;
        i=0;
        u8TmpCRCHorodate+=0x09;
        u8TmpCRC=u8TmpCRCHorodate;
      }else{
        *error=1;
        Serial.print("\r\nError sperator");
        return false;
      }
      break;
    case 0x0D:
      if (*lf)
      {
        *lf=false;
        if (pos==2)
        {
          //Calcul CRC
          u8TmpCRC=(u8TmpCRC & 0x3F)+0x20;

          if ((memcmp(command,"SMAX",4)!=0) &&
                    (memcmp(command,"CCA",3)!=0) &&
                    (memcmp(command,"UMOY",4)!=0) &&
                    (memcmp(command,"DMP",3)!=0) &&
                    (memcmp(command,"FPM",3)!=0)
                )
          {
            if (u8TmpCRC==u8CRC)
            {
                memcpy(value,date,256);
                return true;
            }else{

                Serial.print("command : ");
                Serial.print(command);
                Serial.print("Error CRC : tmp=");
                Serial.print(u8TmpCRC);
                Serial.print(" - reçu=");
                Serial.println(u8CRC);
                *error=1;
				return false;
            }
          }else{
            *error=1; // Datas miss
            Serial.print("\r\nDatas miss");
            return false;
          }     
        }else if(pos==3)
        {
          //Calcul CRC
          u8TmpCRCHorodate=(u8TmpCRCHorodate & 0x3F)+0x20;
          if (u8TmpCRCHorodate==u8CRC)
          {
            //Specif for DATE
            if (memcmp(command,"DATE",4)==0)
            {
              memcpy(value,date,256);
            }
            return true;
          }else{
            *error=1;
            Serial.print("HORODATE  - command : ");
            Serial.print(command);
            Serial.print("Error CRC : tmp=");
            Serial.print(u8TmpCRC);
            Serial.print(" - reçu=");
            Serial.println(u8CRC);
            return false;
          }
          
        }else if(pos==0){
            *error=1;
            Serial.printf("\r\nincorrect position : %d",pos);
            return false;
        }
      }else{
        *error=1;
        Serial.print("\r\nLF Missing ");
        return false;
      }
    
      break;
    case 0x02:
    case 0x03:
    case 0x04:
      break;
    default:
      if (*lf)
      {
        if (pos==0)
        {
          command[i++] = (char)u8Data;
          u8TmpCRCHorodate+=u8Data;
          u8TmpCRC=u8TmpCRCHorodate;
          command[i]='\0';
        }else if (pos==1)
        {
          date[i++] = (char)u8Data;
          u8TmpCRCHorodate+=u8Data;
          u8TmpCRC=u8TmpCRCHorodate;
          date[i]='\0';
        }else if (pos==2)
        {
          value[i++] = (char)u8Data;
          u8TmpCRCHorodate+=u8Data;
          value[i]='\0';
          u8CRC = u8Data;
        }else{
          u8CRC = u8Data;
        }
      }else{
        *error=1;
        Serial.print("\r\nLF Missing : No valid data ");
        return false;
      }
      break;
  }
  *error=0;
  return false;
}

bool bTranscodeCharTICHisto(unsigned int u16MaxLength, char *command, char *value, uint8_t *error,byte u8Data, bool *lf)
{
    /*uint8_t u8CRC;
    uint8_t u8TmpCRC;
    uint16_t i;
    uint8_t pos;*/

    switch(u8Data)
    {
      case 0x0A:
        u8TmpCRC=0;
        memset(command,0,0);
        memset(value,0,0);
        command=0;
        value=0;
        pos=0;
        i=0;
        *error=0;
        break;  
      case 0x0D:
        if (*lf)
        {
          *lf=false;
          if (pos>=2)
          {
              //Calcul CRC
            u8TmpCRC=(u8TmpCRC & 0x3F)+0x20;
            if (u8TmpCRC==u8CRC)
            {
              return true;
            }else{
              *error=1;
              Serial.printf("\r\nError CRC : tmp=%d - reçu=%d", u8TmpCRC,u8CRC);
              return false;
            }
          }else if(pos==0){
            *error=1;
            Serial.printf("\r\nEtiquettes / données / séparateur non attendu nb != 2 --> %d", pos);
            return false;
          }
        }else{
          *error=1;
          Serial.print("\r\nLF Missing ");
          return false;
        }
        break;
      case 0x20:
        if (*lf)
        {
          pos++;
          i=0;
          if (pos<2)
            u8TmpCRC+=0x20;
          else{
            u8CRC = u8Data;
          }

        }
        break;
      case 0x02:
      case 0x03:
      case 0x04:
        break;    
      default:
        if (*lf)
        {
          if (pos==0)
          {
            command[i++] = u8Data;
            u8TmpCRC+=u8Data;
            command[i]='\0';

          }else if (pos==1)
          {
            value[i++] = u8Data;
            u8TmpCRC+=u8Data;
            value[i]='\0';

          }else{
            u8CRC = u8Data;

          }
        }else{
          *error=1;
          Serial.print("\r\nLF Missing : No valid data ");
          return false;
        }
        break;
     }
    *error = 0;
    return false;
}

