#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "Teleinformation.h"
#include "config.h"

// ── Parser state (mutated only inside bTranscodeCharTIC) ─────────────────────
static unsigned int s_pos      = 0;   // field index: 0=label 1=horodate 2=value 3=checksum
static unsigned int s_idx      = 0;   // byte index within current field
static uint8_t      s_crc_rx   = 0;   // last received byte when pos≥2 (becomes checksum)
static uint8_t      s_crc_acc  = 0;   // running sum: label+TAB+horodate+TAB+value+TAB
static uint8_t      s_crc_snap = 0;   // snapshot of s_crc_acc at the separator after value

extern uint16_t             holdingRegisters[24600];
extern uint32_t             u32Timeout;
extern uint8_t              u8ErrorDecode;
extern ConfigSettingsStruct ConfigSettings;

// ── SmartEVSE v3.1 mirror registers (0–19) — INT32 big-endian (high word first)
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

static uint32_t s_sinsts_va  = 0;
static uint32_t s_sinsts1_va = 0;
static uint32_t s_sinsts2_va = 0;
static uint32_t s_sinsts3_va = 0;
static uint16_t s_urms1_v    = 230;
static uint16_t s_urms2_v    = 230;
static uint16_t s_urms3_v    = 230;
static uint32_t s_ccasn_w    = 0;

// ── Energy-delta active power estimation ─────────────────────────────────────
static uint64_t s_east_wh     = 0;
static uint32_t s_energy_ms   = 0;
static float    s_power_ema_w = 0.0f;
static bool     s_east_init   = false;

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

static void seUpdateCosPhi() {
    if (s_sinsts_va == 0) return;
    bool stale = s_east_init && ((uint32_t)millis() - s_energy_ms) > 60000u;
    float P = (!s_east_init || stale) ? (float)s_ccasn_w : s_power_ema_w;
    float cos_phi = P / (float)s_sinsts_va;
    if (cos_phi > 1.0f) cos_phi = 1.0f;
    seWriteFloat32(SE_COSPHI_FLOAT_REG, cos_phi);
    holdingRegisters[SE_COSPHI_U16_REG] = (uint16_t)(cos_phi * 1000.0f + 0.5f);
}

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
        seUpdateCosPhi();
    }
}

// ── Register write helpers ────────────────────────────────────────────────────

static void writeU64BE(uint16_t reg, uint64_t v) {
    holdingRegisters[reg]     = (uint16_t)(v >> 48);
    holdingRegisters[reg + 1] = (uint16_t)(v >> 32);
    holdingRegisters[reg + 2] = (uint16_t)(v >> 16);
    holdingRegisters[reg + 3] = (uint16_t)(v);
}

static void writeStr(uint16_t reg, const char *s) {
    for (size_t k = 0; s[k]; ++k)
        holdingRegisters[reg + k] = (uint16_t)(uint8_t)s[k];
}

// ── Label dispatch table ──────────────────────────────────────────────────────

enum TicType : uint8_t { TY_U16, TY_U64, TY_STR, TY_NONE };
enum TicSfx  : uint8_t {
    SFX_NONE,
    SFX_EAST,
    SFX_IRMS1, SFX_IRMS2, SFX_IRMS3,
    SFX_URMS1, SFX_URMS2, SFX_URMS3,
    SFX_SINSTS, SFX_SINSTS1, SFX_SINSTS2, SFX_SINSTS3,
    SFX_CCASN,
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
    { "LTARF",    2100,   TY_STR, SFX_NONE,    false },
    { "NTARF",  0xFFFF,   TY_NONE, SFX_NONE,   false },
    { "VTIC",   0xFFFF,   TY_NONE, SFX_NONE,   false },
    // Date
    { "DATE",     3000,   TY_STR, SFX_NONE,    false },
    // Active energy — total and per-tariff index
    { "EAST",     1000,   TY_U64, SFX_EAST,    false },
    { "EASF01",   1004,   TY_U64, SFX_NONE,    false },
    { "EASF02",   1008,   TY_U64, SFX_NONE,    false },
    { "EASF03",   1012,   TY_U64, SFX_NONE,    false },
    { "EASF04",   1016,   TY_U64, SFX_NONE,    false },
    { "EASF05",   1020,   TY_U64, SFX_NONE,    false },
    { "EASF06",   1024,   TY_U64, SFX_NONE,    false },
    { "EASF07",   1028,   TY_U64, SFX_NONE,    false },
    { "EASF08",   1032,   TY_U64, SFX_NONE,    false },
    { "EASF09",   1036,   TY_U64, SFX_NONE,    false },
    { "EASF10",   1040,   TY_U64, SFX_NONE,    false },
    { "EASD01",   1100,   TY_U64, SFX_NONE,    false },
    { "EASD02",   1104,   TY_U64, SFX_NONE,    false },
    { "EASD03",   1108,   TY_U64, SFX_NONE,    false },
    { "EASD04",   1112,   TY_U64, SFX_NONE,    false },
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
    { "PREF",     1399,   TY_U16, SFX_NONE,    false },
    { "PCOUP",    1398,   TY_U16, SFX_NONE,    false },
    // Status register
    { "STGE",     4000,   TY_STR, SFX_NONE,    false },
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
    int32_t  v32  = (v > 0x7FFFFFFFull) ? 0x7FFFFFFF : (int32_t)v;
    uint32_t v32u = (v > 0xFFFFFFFFull) ? 0xFFFFFFFF : (uint32_t)v;
    uint16_t v16  = (uint16_t)v;
    switch (sfx) {
        case SFX_EAST:
            standardEnergyUpdate(v);
            break;
        case SFX_IRMS1:
            seWriteInt32(SE_IRMS1_REG, (int32_t)v16 * 1000);
            break;
        case SFX_IRMS2:
            seWriteInt32(SE_IRMS2_REG, (int32_t)v16 * 1000);
            break;
        case SFX_IRMS3:
            seWriteInt32(SE_IRMS3_REG, (int32_t)v16 * 1000);
            break;
        case SFX_URMS1:
            seWriteInt32(SE_URMS1_REG, (int32_t)v16 * 10);
            s_urms1_v = v16;
            seUpdatePreciseCurrent();
            seUpdatePreciseCurrentL1();
            break;
        case SFX_URMS2:
            seWriteInt32(SE_URMS2_REG, (int32_t)v16 * 10);
            s_urms2_v = v16;
            seUpdatePreciseCurrentL2();
            break;
        case SFX_URMS3:
            seWriteInt32(SE_URMS3_REG, (int32_t)v16 * 10);
            s_urms3_v = v16;
            seUpdatePreciseCurrentL3();
            break;
        case SFX_SINSTS:
            seWriteInt32(SE_PAPP_REG, v32);
            s_sinsts_va = v32u;
            seUpdatePreciseCurrent();
            seUpdateCosPhi();
            break;
        case SFX_SINSTS1:
            seWriteInt32(SE_PAPP1_REG, v32);
            s_sinsts1_va = v32u;
            seUpdatePreciseCurrentL1();
            break;
        case SFX_SINSTS2:
            seWriteInt32(SE_PAPP2_REG, v32);
            s_sinsts2_va = v32u;
            seUpdatePreciseCurrentL2();
            break;
        case SFX_SINSTS3:
            seWriteInt32(SE_PAPP3_REG, v32);
            s_sinsts3_va = v32u;
            seUpdatePreciseCurrentL3();
            break;
        case SFX_CCASN:
            s_ccasn_w = v32u;
            seUpdateCosPhi();
            break;
        default:
            break;
    }
}

// ── Spec-file cache ───────────────────────────────────────────────────────────
// /modbus/registres_spec.json is loaded once on first unknown label and kept in
// heap. The web UI always reboots after saving the file, so the cache is valid
// for the lifetime of the process.

static DynamicJsonDocument *s_spec_doc   = nullptr;
static bool                 s_spec_tried = false;

static void ensureSpecDoc() {
    if (s_spec_tried) return;
    s_spec_tried = true;
    File f = LittleFS.open("/modbus/registres_spec.json", FILE_READ);
    if (!f || f.isDirectory()) { f.close(); return; }
    s_spec_doc = new DynamicJsonDocument(100000);
    if (deserializeJson(*s_spec_doc, f) != DeserializationError::Ok) {
        delete s_spec_doc;
        s_spec_doc = nullptr;
    }
    f.close();
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
                if (e.reg != 0xFFFF) writeU64BE(e.reg, v64);
                dispatchSfx(e.sfx, v64);
                break;
            case TY_U16: {
                uint16_t v16 = (uint16_t)atoi(au8Value);
                holdingRegisters[e.reg] = v16;
                dispatchSfx(e.sfx, (uint64_t)v16);
                break;
            }
            case TY_STR:
                if (e.reg != 0xFFFF) writeStr(e.reg, au8Value);
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

    // Fallback: user-defined register spec (loaded once from /modbus/registres_spec.json)
    ensureSpecDoc();
    if (!s_spec_doc) return true;

    if (s_spec_doc->containsKey("standard")) {
        for (JsonVariant entry : (*s_spec_doc)["standard"].as<JsonArray>()) {
            const char *cmd = entry["command"].as<const char *>();
            if (!cmd || strcmp(au8Command, cmd) != 0) continue;
            int         size = entry["size"].as<int>();
            int         reg  = entry["reg"].as<int>();
            const char *type = entry["type"].as<const char *>();
            if (type && memcmp(type, "numeric", 7) == 0) {
                long long tmp = strtoull(au8Value, NULL, 10);
                for (int j = 0; j < size; j++)
                    holdingRegisters[reg + j] = (uint16_t)(tmp >> ((size - 1 - j) * 16)) & 0xFFFF;
            } else if (type && memcmp(type, "string", 6) == 0) {
                for (int j = 0; j < size; j++)
                    holdingRegisters[reg + j] = static_cast<uint16_t>(au8Value[j]);
            }
            break;
        }
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
