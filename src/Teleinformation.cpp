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
    Serial.print("BASE : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"HHPHC",5)==0)
  {
    Serial.print("HHPHC : ");
    Serial.println(au8Value);
    for (size_t i = 0; i < strlen(au8Value); ++i)
    {
      holdingRegisters[5000+i] = static_cast<uint16_t>(au8Value[i]);
    }
  }else if (memcmp(au8Command,"OPTARIF",7)==0)
  {
    Serial.print("OPTARIF : ");
    Serial.println(au8Value);
    for (size_t i = 0; i < strlen(au8Value); ++i) 
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
    Serial.print("HCHC : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"HCHP",4)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1011] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1010] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1009] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1008] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("HCHP : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"EJPHN",5)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1007] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1006] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1005] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1004] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("EJPHN : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"EJPHPM",6)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1011] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1010] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1009] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1008] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("EJPHPM : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHCJB",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1007] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1006] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1005] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1004] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("BBRHCJB : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHPJB",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1011] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1010] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1009] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1008] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    Serial.print("BBRHPJB : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHCJW",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1015] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1014] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1013] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1012] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("BBRHCJW : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHPJW",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1019] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1018] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1017] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1016] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

    Serial.print("BBRHPJW : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHCJR",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1023] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1022] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1021] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1020] = (uint16_t)(tmp >> 48 ) & 0xFFFF;
    
    Serial.print("BBRHCJR : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"BBRHPJR",7)==0)
  {
    long long tmp = strtoull(au8Value,NULL,10);  

    holdingRegisters[1027] = (uint16_t)tmp & 0xFFFF;
    holdingRegisters[1026] = (uint16_t)(tmp >> 16 ) & 0xFFFF;
    holdingRegisters[1025] = (uint16_t)(tmp >> 32 ) & 0xFFFF;
    holdingRegisters[1024] = (uint16_t)(tmp >> 48 ) & 0xFFFF;

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
    Serial.print("PAPP : ");
    Serial.println(tmp);
  }else if (memcmp(au8Command,"PTEC",4)==0)
  {
    long tmp = atol(au8Value);
    Serial.print("PTEC : ");
    Serial.println(tmp);
    for (size_t i = 0; i < strlen(au8Value); ++i) 
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
    for (size_t i = 0; i < strlen(au8Value); ++i) 
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
            if (memcmp(au8Command,temp["historique"][i]["command"].as<String>().c_str(),strlen(temp["historique"][i]["command"].as<String>().c_str()))==0)
            {
              if (memcmp(temp["historique"][i]["type"].as<String>().c_str(),"numeric",7)==0)
              {
                long long tmp = strtoull(au8Value,NULL,10);  
                int k = temp["historique"][i]["size"].as<int>();
                for (int j=0;j<temp["historique"][i]["size"].as<int>();j++)
                {              
                  holdingRegisters[temp["historique"][i]["reg"].as<int>()+j] = (uint16_t)(tmp >> ((k-1)*16) ) & 0xFFFF;
                  k--;
                }
              }else if (memcmp(temp["historique"][i]["type"].as<String>().c_str(),"string",6)==0)
              {
                for (int j=0;j<temp["historique"][i]["size"].as<int>();j++)
                {
                  holdingRegisters[temp["historique"][i]["reg"].as<int>()+j]=static_cast<uint16_t>(au8Value[j]);
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
    for (size_t i = 0; i < strlen(au8Value); ++i) 
    {    
      holdingRegisters[2000+i] = static_cast<uint16_t>(au8Value[i]);
    }

  }else if (memcmp(au8Command,"LTARF",5)==0)
  {   
    Serial.print("LTARF : ");
    Serial.println(au8Value);
    for (size_t i = 0; i < strlen(au8Value); ++i) 
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
    for (size_t i = 0; i < strlen(au8Value); ++i) 
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

    Serial.print("URMS1 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"URMS2",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1324] = (uint16_t)tmp & 0xFFFF;
    seWriteInt32(SE_URMS2_REG, (int32_t)tmp * 10);

    Serial.print("URMS2 : ");
    Serial.println(tmp);

  }else if (memcmp(au8Command,"URMS3",5)==0)
  {
    uint16_t tmp = atoi(au8Value);

    holdingRegisters[1325] = (uint16_t)tmp & 0xFFFF;
    seWriteInt32(SE_URMS3_REG, (int32_t)tmp * 10);

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
    for (size_t i = 0; i < strlen(au8Value); ++i) 
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
    for (size_t i = 0; i < strlen(au8Value); ++i) 
    {
      holdingRegisters[6000+i] = static_cast<uint16_t>(au8Value[i]);
    }
  }else if (memcmp(au8Command,"MSG2",4)==0)
  {
    Serial.print("MSG2 : ");
    Serial.println(au8Value);
    for (size_t i = 0; i < strlen(au8Value); ++i) 
    {
      holdingRegisters[6100+i] = static_cast<uint16_t>(au8Value[i]);
    }
  }else if (memcmp(au8Command,"PRM",3)==0)
  {
    Serial.print("PRM : ");
    Serial.println(au8Value);
    for (size_t i = 0; i < strlen(au8Value); ++i) 
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
            if (memcmp(au8Command,temp["standard"][i]["command"].as<String>().c_str(),strlen(temp["standard"][i]["command"].as<String>().c_str()))==0)
            {
              if (memcmp(temp["standard"][i]["type"].as<String>().c_str(),"numeric",7)==0)
              {
                long long tmp = strtoull(au8Value,NULL,10);  
                int k = temp["standard"][i]["size"].as<int>();
                for (int j=0;j<temp["standard"][i]["size"].as<int>();j++)
                {              
                  holdingRegisters[temp["standard"][i]["reg"].as<int>()+j] = (uint16_t)(tmp >> ((k-1)*16) ) & 0xFFFF;
                  k--;
                }
              }else if (memcmp(temp["standard"][i]["type"].as<String>().c_str(),"string",6)==0)
              {
                for (int j=0;j<temp["standard"][i]["size"].as<int>();j++)
                {
                  holdingRegisters[temp["standard"][i]["reg"].as<int>()+j]=static_cast<uint16_t>(au8Value[j]);
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

