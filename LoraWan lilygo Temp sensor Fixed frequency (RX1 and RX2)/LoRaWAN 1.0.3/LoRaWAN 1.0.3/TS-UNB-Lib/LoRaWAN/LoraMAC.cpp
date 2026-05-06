/**
   @brief Source file for LoraMAC

   @file LoraMAC.cpp
   @author Jonas Brief
   @date 2022-01-16
*/

#include "LoraMAC.h"
#include "LoraAES.h"
//#include "LoraRadio.h"
#include <Arduino.h>
#include <EEPROM.h>

void LoraMAC::buildDataFrame(const uint8_t* frm_payload, uint8_t frm_payload_length, 
                             const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3,
                             const uint8_t a0, const uint8_t a1, const uint8_t a2, const uint8_t a3, const uint8_t a4, const uint8_t a5, const uint8_t a6, const uint8_t a7, const uint8_t a8, const uint8_t a9, const uint8_t a10, const uint8_t a11, const uint8_t a12, const uint8_t a13, const uint8_t a14, const uint8_t a15,
                             const uint8_t n0, const uint8_t n1, const uint8_t n2, const uint8_t n3, const uint8_t n4, const uint8_t n5, const uint8_t n6, const uint8_t n7, const uint8_t n8, const uint8_t n9, const uint8_t n10, const uint8_t n11, const uint8_t n12, const uint8_t n13, const uint8_t n14, const uint8_t n15) {
  //Initialize the necesarry variables
  uint8_t dataFramLength = 9 + frm_payload_length + 4; // Calculate dataFrame length, 9 = Mac Header and Frame Header, 4 = MIC, frm_payload_length = length of acutal payload
  uint8_t dataFrame[dataFramLength]; //array for storing all the necessary bytes
  uint8_t devaddr[4]; //array for storing the device address
  uint8_t appSKey[16]; //array for storing app session key
  uint8_t networkSKey[16]; //array for storing app session key
  uint8_t l = 16 * ceil((float)frm_payload_length / 16.0); //calculate necessary array length for storing the encoded payload
  uint8_t phypayloadtest[l]; //array for storing the encoded payload
  uint8_t output[16]; //array for storing the calculated MIC
  uint8_t phypayload[dataFramLength - 4]; //array for storing data needed for CMAC generation

  //LoraRadio Radio; //Create LoraRadio on stack
  LoraAES loraAes; //create LoraAES on stack

  loraAes.setDevAddr(d0,d1,d2,d3,devaddr);
  loraAes.setAppSKey(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, appSKey);
  loraAes.setNetSKey(n0, n1, n2, n3, n4, n5, n6, n7, n8, n9, n10, n11, n12, n13, n14, n15, networkSKey);

  //set MHDR
  dataFrame[0] = FType_unconDataUp;
  
  for (uint8_t i = 0; i < 4; i++) {
    dataFrame[i + 1] = devaddr[i]; //Add device address to data frame
  }

  //set Fctrl
  dataFrame[5] = 0x80;
  //set FCnt
  dataFrame[6] = EEPROM.read(0);
  dataFrame[7] = 0x00;
  //Set Fopts
  dataFrame[8] = 0x01;

  loraAes.encodePayload(frm_payload, frm_payload_length, appSKey, devaddr, phypayloadtest, dataFrame[6]); //Encode data

  for (uint8_t i = 0; i < frm_payload_length; i++) {
    dataFrame[i + 9] = phypayloadtest[i]; //add encoded data to data frame
  }


  for (uint8_t i = 0; i < dataFramLength - 4; i++) {
    phypayload[i] = dataFrame[i];
  }

  loraAes.generateCMAC(dataFramLength - 4, phypayload, networkSKey, devaddr, output, dataFrame[6]); //Generate CMAC

  for (uint8_t i = 0; i < 4; i++) {
    dataFrame[i + 9 + frm_payload_length] = output[i]; //Add MIC to data frame
  }

  
    Serial.println("Gesamter Frame");
    for(uint8_t i = 0; i < dataFramLength; i++){
    Serial.println(dataFrame[i]);
    }
  

  //Start Transmission to The Things Network
  //Radio.txLora(LORA_BW_125, LORA_CR_4_5, LORA_SF9, LORA_TXCONTIMODE_OFF, LORA_RXPAYLOADCRC_ON, LORA_SYMBOLTIMEOUT, LORA_LOWDATARATEOPTIMIZED_ON, LORA_AGCAUTOON_ON, 868300000, 14, dataFramLength, dataFrame);
  Serial.println("Das ist FCnt");
  EEPROM.write(0, dataFrame[6] + 1);
  Serial.println(dataFrame[6]);
}
