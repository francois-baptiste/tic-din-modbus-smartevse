# TIC-DIN-MODBUS — SmartEVSE fork

> **This is a custom fork** of [fairecasoimeme/TIC-DIN-MODBUS](https://github.com/fairecasoimeme/TIC-DIN-MODBUS), adding native support for **SmartEVSE v3.1 dynamic load balancing** using the Linky meter as the mains meter.
>
> The fork source and releases are at: **https://github.com/francois-baptiste/tic-din-modbus-smartevse**

## Purpose of this fork

The original TIC-DIN-MODBUS firmware exposes Linky TIC data over Modbus RTU, but its register layout does not match the format expected by SmartEVSE v3.1.

This fork adds native SmartEVSE v3.1 compatibility via **two parallel Modbus register spaces** served simultaneously on the same slave address:

| Mode | FC | Addresses | Format | SmartEVSE meter type |
|------|----|-----------|--------|----------------------|
| **Eastron SDM630 emulation** *(recommended)* | FC=04 | 0–58 | IEEE 754 float32 | **Eastron SDM630** |
| Custom Meter block | FC=03 | 0–34 | INT32 + FLOAT32 | Custom |

**SDM630 emulation** (FC=04 input registers) is the recommended approach — set SmartEVSE to meter type **Eastron SDM630** and point it at the device address. No custom register mapping required.

| SDM630 register | Content | Source |
|-----------------|---------|--------|
| 0 / 2 / 4 | Voltage L1/L2/L3 (V) | URMS1/2/3 |
| 6 / 8 / 10 | Current L1/L2/L3 (A) | SINSTS_n ÷ URMS_n *(monophasé: SINSTS ÷ URMS1 on L1, 0 on L2/L3)* |
| 12 / 14 / 16 | Active power L1/L2/L3 (W) | SINSTS_n × cos φ |
| 18 / 20 / 22 | Apparent power L1/L2/L3 (VA) | SINSTS1/2/3 |
| 30 / 32 / 34 | Power factor L1/L2/L3 | global cos φ |
| 52 | Total active power (W) | energy-delta EMA |
| 56 | Total apparent power (VA) | SINSTS |
| 58 | Total power factor | global cos φ |

**Custom Meter block** (FC=03, registers 0–34): kept for compatibility and for use cases requiring INT32 or the full FLOAT32/UINT16 precise-current registers. See the [Custom Meter section](#custom-meter-mode-alternative) below for the full register map.

**Precision current**: derived from `SINSTS ÷ URMS`, giving sub-ampere resolution vs. the 1 A step of raw IRMS.

**Power factor / cos φ**: derived from the rate of change of the active energy index (EAST / BASE / HC+HP) divided by SINSTS. Only Wh ticks that actually increment are used (no quantisation noise). An EMA (α = 0.25) smooths the result. Falls back to CCASN when no energy increment has arrived in > 60 s.

See the [SmartEVSE v3.1 section](#smartevse-v31--dynamic-load-balancing-linky-as-mains-meter) below for wiring and configuration details.

---

# TIC-DIN-MODBUS

**TIC-DIN-MODBUS** is a device which can demodulate Linky informations TIC (or old french counter meter). It provides informations with RS845 Modbus RTU interface.

You can with Webpage:
* Add specific managed TIC command
* Configure Modbus parameter 
* Configure WiFi SSID/Password
* Add User/Password for HTTP webpage
* See ModBus with mapping table and Network status
* Update the device

**TIC-DIN-MODBUS** is compatible with historic and standard mode. The mode is detected automaticaly.

**TIC-DIN-MODBUS** est disponible en boutique : https://lixee.fr/teleinformation/39-tic-din-modbus-rtu-rs485-3770014375186.html

## Hardware

**TIC-DIN-MODBUS** use a DIN rail module 1U.
It is based on **ESP32-C3** module and a **MAX485** chip for the RS485 interface

### Device v2
<img src="https://github.com/fairecasoimeme/TIC-DIN-MODBUS/blob/master/Doc/images/tic-din-modbus-rtu-rs485_2.jpg" width="600">
<img src="https://github.com/fairecasoimeme/TIC-DIN-MODBUS/blob/master/Doc/images/tic-din-modbus-rtu-rs485.jpg" width="600">

### RS485 pins
- VIN (5VDC)
- A (+)
- B (-)
- GND

### TIC pins
- I1
- I2
  
### Assembly diagram
<img src="https://github.com/fairecasoimeme/TIC-DIN-MODBUS/blob/master/Doc/images/schema_TIC_MODBUS.jpg" width="600">

### Firmware flash 
<img src="https://github.com/fairecasoimeme/TIC-DIN-MODBUS/blob/master/Doc/images/update_tic-din-modbus.png" width="600">
To flash the firmware, you have to download "firmware.bin" in the https://github.com/fairecasoimeme/TIC-DIN-MODBUS/releases.   

With the webpage config, go to **"Tools"** then **"Update"** :   
Then, click on "Choose File...", then select the binary file then click on **"Update"** and that's all

## LED Flashing

### 1 flash every second
In this case, **TIC-DIN-MODBUS** can't demodulate the counter meter or there are too errors. It is possible that this case can appear 10-15 seconds after resetting due to the mode detection.

### 1 flash every 3 second
In this case, All it's OK. The device demodulate correctly the counter meter.

## WiFi parameter

### By default  
* SSID : LIXEETIC-XXXX (editable) 
* password : adminXXXX (editable - must be 8 characters) 
PS : XXXX is the end of the @MAC

Please open a browser with this IP address : http://192.168.4.1 to connect to the device

⚠️**You can desactive WiFi when all it's OK**  

⚠️**To activate WiFi, you have to send ModBus packet with command 0x06 (Write single register) to address 666 with the value 1.**

## Modbus slave parameter

### By default
* id master : **11** (editable — chosen to avoid conflict with SmartEVSE internal addresses 1-10)
* speed (bauds) : 9600 (editable)
* Data bits : 8
* Stop bits : 1
* Parity : none

## ModBus mapping table
|Mode|command|@registry|
|------|----------|------|	
|Historique|ADCO		|300-303	|
|Historique|OPTARIF		|2000-2099  |
|Historique|BASE		|1004-1007  |
|Historique|ISOUSC	|1399           |
|Historique|HCHC		|1004-1007  |
|Historique|HCHP		|1008-1011  |
|Historique|EJPHN	|1004-1007      |
|Historique|EJPHPM	|1008-1011      |
|Historique|BBRHCJB	|1004-1007      |
|Historique|BBRHPJB	|1008-1011      |
|Historique|BBRHCJW	|1012-1015      |
|Historique|BBRHPJW	|1016-1019      |
|Historique|BBRHCJR	|1020-1023      |
|Historique|BBRHPJR	|1024-1027      |
|Historique|IINST1	|1320           |
|Historique|IINST2	|1321           |
|Historique|IINST3	|1322           |
|Historique|IINST	|1320           |
|Historique|IMAX1	|1600           |
|Historique|IMAX2	|1601           |
|Historique|IMAX3	|1602           |
|Historique|IMAX		|1600       |
|Historique|PMAX		|1361-1364  |
|Historique|PAPP		|1329-1332  |
|Historique|ADPS		|1700       |
|Historique|ADIR1	|1603           |
|Historique|ADIR2	|1604           |
|Historique|ADIR3	|1605           |
|Historique|PEJP		|1606		|
|Historique|PTEC	|2200-2299          |
|Historique|HHPHC		|5000-5099	|
|Standard|ADSC	|	300-303         |
|Standard|NGTF	|	2000-2099         |
|Standard|LTARF|	2100-2199       |
|Standard|DATE	|	3000-3099   |
|Standard|STGE|4000-4099|
|Standard|EAST	|	1000-1003       |
|Standard|EASF01	|	1004-1007   |
|Standard|EASF02	|	1008-1011   |
|Standard|EASF03	|	1012-1015   |
|Standard|EASF04	|	1016-1019   |
|Standard|EASF05	|	1020-1023   |
|Standard|EASF06	|	1024-1027   |
|Standard|EASF07	|	1028-1031   |
|Standard|EASF08	|	1032-1035   |
|Standard|EASF09	|	1036-1039   |
|Standard|EASF10	|	1040-1043   |
|Standard|EASD01	|	1100-1103   |
|Standard|EASD02	|	1104-1107   |
|Standard|EASD03	|	1108-1111   |
|Standard|EASD04	|	1112-1115   |
|Standard|EAIT	|	1200-1203       |
|Standard|ERQ1	|	1300-1303       |
|Standard|ERQ2	|	1304-1307       |
|Standard|ERQ3	|	1308-1311       |
|Standard|ERQ4	|	1312-1315       |
|Standard|IRMS1	|	1320            |
|Standard|IRMS2	|	1321            |
|Standard|IRMS3	|	1322            |
|Standard|URMS1	|	1323            |
|Standard|URMS2	|	1324            |
|Standard|URMS3	|	1325            |
|Standard|PREF	|	1399            |
|Standard|STGE	|	                |
|Standard|PCOUP	|	1398            |
|Standard|SINSTI	|	1400-1403   |
|Standard|SMAXIN-1	|1404-1407      |
|Standard|SMAXIN		|1408-1411  |
|Standard|CCASN-1	|1500-1503      |
|Standard|CCASN		|1504-1507      |
|Standard|CCAIN-1	|1412-1415      |
|Standard|CCAIN		|1416-1419       |
|Standard|UMOY1		|1326           |
|Standard|UMOY2		|1327           |
|Standard|UMOY3		|1328           |
|Standard|SINSTS1	|1329-1332      |
|Standard|SINSTS2	|1333-1336      |
|Standard|SINSTS3	|1337-1340      |
|Standard|SINSTS		|1341-1344  From (v1.3) |
|Standard|SMAXSN-1	|1345-1348      |
|Standard|SMAXSN1-1	|1345-1348      |
|Standard|SMAXSN2-1	|1353-1356      |
|Standard|SMAXSN3-1	|1357-1360      |
|Standard|SMAXSN1	|1361-1364      |
|Standard|SMAXSN2	|1365-1368      |
|Standard|SMAXSN3	|1369-1372      |
|Standard|SMAXSN		|1361-1364  |
|Standard|MSG1	|6000-6099      |
|Standard|MSG2	|6100-6199      |
|Standard|PRM		|7000-7099  |

### Add ModBus specific datas

With the webpage, you can add new TIC command with ModBus registry address.  
Go to **"Tools"** and **"ModBus"** button then click on **"registres_spec.json"**  

Then you get a Json file like :

```json
{
"historique":[{
		"reg":1,
		"command":"xxxx",
		"size":4,
		"type":"numeric"
		}
	      ],
		
"standard":[{
		"reg":1,
		"command":"xxxx",
		"size":4,
    "type":"string"
		}
	      ]
}
```

With this file, you can add new TIC command with historic or standard mode.

* **reg** : Registry address
* **command** : TIC command (for ex : DATECOUR)
* **size** : max size value of the command
* **type** : value type (only "string" or "numeric"

Click to **"save"** to validate

## Screenshots

### Network Status
<img src="https://github.com/fairecasoimeme/TIC-DIN-MODBUS/blob/master/Doc/images/network_status.png" width="600">

### WiFi Config
<img src="https://github.com/fairecasoimeme/TIC-DIN-MODBUS/blob/master/Doc/images/WiFi_config.png" width="600">

### ModBus Status
<img src="https://github.com/fairecasoimeme/TIC-DIN-MODBUS/blob/master/Doc/images/modbus_status.png" width="600">

### ModBus Config
<img src="https://github.com/fairecasoimeme/TIC-DIN-MODBUS/blob/master/Doc/images/modbus_config.png" width="600">

### Modbus Specific mapping
<img src="https://github.com/fairecasoimeme/TIC-DIN-MODBUS/blob/master/Doc/images/specific_mapping.png" width="600">

---

## SmartEVSE v3.1 — Dynamic Load Balancing (Linky as mains meter)

This fork adds native compatibility with [SmartEVSE v3.1](https://github.com/SmartEVSE/SmartEVSE-3) dynamic load balancing. Two register spaces are served simultaneously on the same Modbus slave address — choose the one that matches your SmartEVSE meter type setting.

### Wiring

Connect the TIC-DIN-MODBUS RS485 A/B terminals to the same RS485 bus as the SmartEVSE. Both devices share 9600 baud 8N1.

---

### Recommended: Eastron SDM630 mode (FC=04)

The simplest configuration. The device emulates an Eastron SDM630 on FC=04 (Read Input Registers). All values are IEEE 754 float32, big-endian — no divisor setup required.

In the SmartEVSE menu (`CONFIG → Meter → Mains`), select **Eastron SDM630**:

| SmartEVSE parameter | Value |
|---------------------|-------|
| Meter type | **Eastron SDM630** |
| Modbus address | **11** *(matches TIC-DIN-MODBUS default)* |
| Baud rate / parity | match device settings (default 9600, None) |

**SDM630 input register map** (FC=04, all values float32):

| Address | Content | Unit | Source |
|---------|---------|------|--------|
| 0 / 2 / 4 | Voltage L1/L2/L3 | V | URMS1/2/3 |
| 6 / 8 / 10 | Current L1/L2/L3 | A | SINSTS_n ÷ URMS_n *(monophasé: SINSTS ÷ URMS1 on L1)* |
| 12 / 14 / 16 | Active power L1/L2/L3 | W | SINSTS_n × cos φ |
| 18 / 20 / 22 | Apparent power L1/L2/L3 | VA | SINSTS1/2/3 |
| 30 / 32 / 34 | Power factor L1/L2/L3 | — | global cos φ |
| 52 | Total active power | W | energy-delta EMA |
| 56 | Total apparent power | VA | SINSTS |
| 58 | Total power factor | — | global cos φ |

Verify with modpoll (FC=04 = `-t 3`):

```
modpoll -m rtu -a 11 -r 1 -c 12 -t 3 /dev/ttyUSB0
```

Registers 6–10 (currents) and 0–4 (voltages) should show non-zero values as soon as the Linky is transmitting.

---

### Alternative: Custom Meter mode (FC=03) {#custom-meter-mode-alternative}

Use this if you prefer the SmartEVSE **Custom** meter type, or need INT32 format registers.

In the SmartEVSE menu (`CONFIG → Meter → Mains`), select **Custom** and apply these settings:

| SmartEVSE parameter | Value | Notes |
|---------------------|-------|-------|
| Modbus address | **11** | matches TIC-DIN-MODBUS default |
| Function | **3** | holding registers (FC=03) |
| Data type | **INT32** | 32-bit signed integer |
| Endianness | **HBF & HWF** (value 3) | big-endian, high word first |
| Current register (I) | **0** | L1/L2/L3 at 0–1, 2–3, 4–5 |
| Current divisor (I) | **3** | mA ÷ 10³ = A |
| Voltage register (U) | **6** | L1/L2/L3 at 6–7, 8–9, 10–11 |
| Voltage divisor (U) | **1** | V×10 ÷ 10 = V |
| Power register (P) | **14** | L1/L2/L3 at 14–15, 16–17, 18–19 |
| Power divisor (P) | **0** | VA (used as W approximation) |

**FC=03 holding register map (registers 0–34):**

Registers 0–19: signed INT32, big-endian (high word first).  
Registers 20–31: high-precision current (FLOAT32 + UINT16×100).  
Registers 32–34: power factor / cos φ (FLOAT32 + UINT16×1000).

| Registers | Content | Format | Unit | Source |
|-----------|---------|--------|------|--------|
| 0–1 | Current L1 | INT32 | mA | IRMS1 / IINST1 |
| 2–3 | Current L2 | INT32 | mA | IRMS2 / IINST2 |
| 4–5 | Current L3 | INT32 | mA | IRMS3 / IINST3 |
| 6–7 | Voltage L1 | INT32 | V×10 | URMS1 |
| 8–9 | Voltage L2 | INT32 | V×10 | URMS2 |
| 10–11 | Voltage L3 | INT32 | V×10 | URMS3 |
| 12–13 | Total apparent power | INT32 | VA | SINSTS / PAPP |
| 14–15 | Apparent power L1 | INT32 | VA | SINSTS1 / PAPP |
| 16–17 | Apparent power L2 | INT32 | VA | SINSTS2 |
| 18–19 | Apparent power L3 | INT32 | VA | SINSTS3 |
| 20–21 | Total precise current *(Option A)* | FLOAT32 | A | SINSTS ÷ URMS1 |
| 22 | Total precise current *(Option B)* | UINT16 | cA (×100) | SINSTS ÷ URMS1 |
| 23–24 | L1 precise current *(Option A)* | FLOAT32 | A | SINSTS1 ÷ URMS1 *(monophasé: SINSTS ÷ URMS1)* |
| 25 | L1 precise current *(Option B)* | UINT16 | cA (×100) | SINSTS1 ÷ URMS1 *(monophasé: SINSTS ÷ URMS1)* |
| 26–27 | L2 precise current *(Option A)* | FLOAT32 | A | SINSTS2 ÷ URMS2 |
| 28 | L2 precise current *(Option B)* | UINT16 | cA (×100) | SINSTS2 ÷ URMS2 |
| 29–30 | L3 precise current *(Option A)* | FLOAT32 | A | SINSTS3 ÷ URMS3 |
| 31 | L3 precise current *(Option B)* | UINT16 | cA (×100) | SINSTS3 ÷ URMS3 |
| 32–33 | Power factor cos φ *(Option A)* | FLOAT32 | 0.0–1.0 | ΔWh ÷ Δt ÷ SINSTS |
| 34 | Power factor cos φ *(Option B)* | UINT16 | ×1000 (e.g. 956 = 0.956) | ΔWh ÷ Δt ÷ SINSTS |

> **Historic single-phase meters**: L1 carries IINST and PAPP; L2/L3 are set to 0. URMS1/2/3 default to 230 V since historic frames do not transmit voltage.

**High-precision current** (registers 20–31): derived from `SINSTS ÷ URMS`, sub-ampere resolution vs. the 1 A step of IRMS.

**Power factor / cos φ** (registers 32–34):

```
P_raw  = ΔWh × 3,600,000 / Δt_ms        (W, only when the Wh index increments)
P_ema  = 0.25 × P_raw + 0.75 × P_ema    (EMA, α = 0.25)
cos φ  = P_ema / SINSTS                  (clamped to [0, 1])
```

Falls back to CCASN when no energy increment has arrived in > 60 s. Per-phase cos φ is not available (TIC does not provide per-phase active power).

Verify with modpoll (FC=03 = `-t 4`):

```
modpoll -m rtu -a 11 -r 1 -c 35 -t 4 /dev/ttyUSB0
```

---

## Changelog

### Version v1.17-smartevse
* Fix SDM630 Current L1/L2/L3 = 0 on single-phase (monophasé) Linky meters
  * On monophasé meters the TIC frame emits only `SINSTS` (total apparent power) — `SINSTS1` is never transmitted. The SDM630 L1 current register (FC=04, regs 6-7) was derived from `SINSTS1 ÷ URMS1` and was always 0, causing SmartEVSE (Eastron3P mode) to read 0 A on all phases
  * Fix: `seUpdatePreciseCurrentL1()` now falls back to `SINSTS ÷ URMS1` when the meter is single-phase (no URMS2/URMS3 received and no SINSTS1). Three-phase meters are unaffected
  * Fix: `seUpdatePreciseCurrentL1()` is now also called on every `SINSTS` update so the SDM630 current register refreshes immediately when load changes
  * Same fallback applied to the FC=03 L1 precise-current registers (23–25)

### Version v1.16-smartevse
* Merge upstream v1.4: updated esp32-c3 partition table

### Version v1.15-smartevse
* Add Eastron SDM630 emulation via FC=04 input registers (60 words)
  * SmartEVSE can now be configured as "Eastron SDM630" meter type — no custom register mapping needed
  * SDM630 registers populated from TIC data: voltages (0/2/4), currents (6/8/10), active power (12/14/16), apparent power (18/20/22), power factors (30/32/34), total active power (52), total apparent power (56), total power factor (58)
  * Both FC=03 (custom meter) and FC=04 (SDM630) served simultaneously on the same slave address

### Version v1.14-smartevse
* STGE bit decoding: injection flag (bit 4), overload flag (bit 6), producer mode flag (bit 7) → registers 35/36/37
* EAST rollback guard: re-seeds EMA baseline on counter reset instead of using stale negative delta
* 3-phase voltage averaging for total precise current when all three URMS values are available

### Version v1.13-smartevse
* Performance fixes and bug fixes

### Version v1.12-smartevse
* Standard-mode only parser (historic mode removed)
* Label dispatch table replaces linear string search
* Merged parsers: single unified TIC frame parser

### Version v1.11-smartevse
* Minor stability improvements

### Version v1.10-smartevse
* Refactor cos φ computation to energy-delta method (replaces CCASN÷SINSTS)
  * Tracks EAST (standard) or BASE/HCHC+HCHP/Tempo indices (historic) with `millis()` timestamps
  * P computed only on actual Wh increments — eliminates quantisation noise between ticks
  * EMA (α = 0.25) smooths instantaneous P; seeded directly on first sample
  * Timeout fallback to CCASN after 60 s of no energy increment (very low load)
  * Full historic mode support: BASE, HC/HP, EJP, and all 6 Tempo sub-indices

### Version v1.9-smartevse
* Add power factor (cos φ) register block (regs 32–34): FLOAT32 and UINT16×1000
  * Initial implementation: CCASN (10-min avg active power) ÷ SINSTS — replaced in v1.10
* Update Help page to describe fork features and link to forked repository

### Version v1.8-smartevse
* Fix web UI OOM crash: rewrite `handleStatusNetwork` with `AsyncResponseStream` to eliminate large heap allocations

### Version v1.7-smartevse
* Add all missing TIC registers to the status page (EAIT, ERQ1–4, SMAXSN, CCASN, CCAIN, SINSTS1/2/3, etc.)

### Version v1.6-smartevse
* Add SmartEVSE Custom Meter register block (regs 0–31): INT32 currents/voltages/powers + FLOAT32 precise current

### Version v1.5-smartevse
* Merge feature/precise-current-sinsts-urms into master

### Version 1.1
* Fix Serial speed change between historic and standard mode
  * fix auto-detection speed
    
### Version v1.0
* Initial version
