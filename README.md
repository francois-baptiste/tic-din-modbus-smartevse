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

This fork adds native compatibility with [SmartEVSE v3.1](https://github.com/SmartEVSE/SmartEVSE-3) dynamic load balancing. The device exposes a **SmartEVSE mirror register block** at addresses **0–19** that SmartEVSE can read directly as a Custom meter.

### Mirror register block (registers 0–19)

All values are **signed INT32, big-endian (high word first)**, FC=3 (holding registers).

| Registers | Content | Unit | Source TIC field |
|-----------|---------|------|-----------------|
| 0–1 | Current L1 | mA | IRMS1 / IINST1 |
| 2–3 | Current L2 | mA | IRMS2 / IINST2 |
| 4–5 | Current L3 | mA | IRMS3 / IINST3 |
| 6–7 | Voltage L1 | V×10 | URMS1 |
| 8–9 | Voltage L2 | V×10 | URMS2 |
| 10–11 | Voltage L3 | V×10 | URMS3 |
| 12–13 | Total apparent power | VA | SINSTS / PAPP |
| 14–15 | Apparent power L1 | VA | SINSTS1 / PAPP |
| 16–17 | Apparent power L2 | VA | SINSTS2 |
| 18–19 | Apparent power L3 | VA | SINSTS3 |

> **Historic single-phase meters**: L1 carries IINST and PAPP; L2/L3 are set to 0.

### Wiring

Connect the TIC-DIN-MODBUS RS485 A/B terminals to the same RS485 bus as the SmartEVSE. Both devices share 9600 baud 8N1.

### SmartEVSE v3.1 Custom Meter configuration

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

> **Note on apparent vs. active power**: Linky reports apparent power (VA), not active power (W). For purely resistive loads the difference is negligible. For installations with significant reactive loads, SINSTS will slightly overestimate the load seen by SmartEVSE, which is the safe (conservative) direction for load balancing.

### Verify communication

Use a Modbus RTU master tool (e.g. `modpoll`, Modbus Poll app) to confirm the TIC-DIN-MODBUS answers on address 11 before configuring SmartEVSE:

```
modpoll -m rtu -a 11 -r 1 -c 20 -t 4 /dev/ttyUSB0
```

Registers 0–19 should show non-zero values once the Linky meter is transmitting.

---

## Changelog

### Version 1.1
* Fix Serial speed change between historic and standard mode
  * fix auto-detection speed
    
### Version v1.0
* Initial version
