/**
   @brief Source file for LoraAES

   @file LoraAES.cpp
   @author Jonas Brief
   @date 2022-01-17
*/

#include "LoraAES.h"
#include "../Encryption/Aes128.h"

void LoraAES::setDevAddr(const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3, uint8_t *devaddr) {
  devaddr[0] = d0;
  devaddr[1] = d1;
  devaddr[2] = d2;
  devaddr[3] = d3;
}

void LoraAES::setNetSKey(const uint8_t n0, const uint8_t n1, const uint8_t n2, const uint8_t n3,
                         const uint8_t n4, const uint8_t n5, const uint8_t n6, const uint8_t n7,
                         const uint8_t n8, const uint8_t n9, const uint8_t n10, const uint8_t n11,
                         const uint8_t n12, const uint8_t n13, const uint8_t n14, const uint8_t n15, uint8_t *networkSKey) {
  networkSKey[0] = n0;
  networkSKey[1] = n1;
  networkSKey[2] = n2;
  networkSKey[3] = n3;
  networkSKey[4] = n4;
  networkSKey[5] = n5;
  networkSKey[6] = n6;
  networkSKey[7] = n7;
  networkSKey[8] = n8;
  networkSKey[9] = n9;
  networkSKey[10] = n10;
  networkSKey[11] = n11;
  networkSKey[12] = n12;
  networkSKey[13] = n13;
  networkSKey[14] = n14;
  networkSKey[15] = n15;
}

void LoraAES::setAppSKey(const uint8_t a0, const uint8_t a1, const uint8_t a2, const uint8_t a3,
                         const uint8_t a4, const uint8_t a5, const uint8_t a6, const uint8_t a7,
                         const uint8_t a8, const uint8_t a9, const uint8_t a10, const uint8_t a11,
                         const uint8_t a12, const uint8_t a13, const uint8_t a14, const uint8_t a15, uint8_t *appSKey) {
  appSKey[0] = a0;
  appSKey[1] = a1;
  appSKey[2] = a2;
  appSKey[3] = a3;
  appSKey[4] = a4;
  appSKey[5] = a5;
  appSKey[6] = a6;
  appSKey[7] = a7;
  appSKey[8] = a8;
  appSKey[9] = a9;
  appSKey[10] = a10;
  appSKey[11] = a11;
  appSKey[12] = a12;
  appSKey[13] = a13;
  appSKey[14] = a14;
  appSKey[15] = a15;
}

void LoraAES::generateB(uint8_t dataLen, uint8_t *devaddr, uint8_t *B, uint8_t fcnt) {
  //Initialize B vector with all zeros
  for (uint8_t i = 0; i < 16; i++) {
    B[i] = 0;
  }

  //From Lora Spec
  B[0] = 0x49;

  //set Dir
  B[5] = 0x00; //Only uplink is considered, so Dir 0x00 (Lora Spec)


  //set devaddr of initialization vector
  for (uint8_t i = 0; i < 4; i++) {
    B[i + 6] = devaddr[i];
  }


  //set Fcntup (erstmal zu 0 zum testen, soll um 1 erhöht werden je Sendevorgang)
  for (uint8_t i = 0; i < 4; i++) {
    B[i + 10] = 0;
  }
  B[10] =  fcnt;
  //set len(msg)
  B[15] = dataLen;

    Serial.println("B");
  for(uint8_t i = 0; i < 16; i++){
    Serial.println(B[i]);
  }
}

void LoraAES::generateA(uint8_t *devaddr, uint8_t *A, uint8_t fcnt) {
  //Initialize A vector with all zeros
  for (uint8_t i = 0; i < 16; i++) {
    A[i] = 0;
  }

  //From Lora Spec
  A[0] = 0x01;

  //set devaddr of initialization vector
  for (uint8_t i = 0; i < 4; i++) {
    A[i + 6] = devaddr[i];
  }

  //set fcntup
  A[10] = fcnt;
 
  A[15] = 1; //must be incremented with every usage, 1 is initial value (Lora Spec)

  Serial.println("A");
  for(uint8_t i = 0; i < 16; i++){
    Serial.println(A[i]);
  }
}

void LoraAES::encodePayload(const uint8_t* frm_payload, uint8_t frm_payload_length,uint8_t *appSKey, uint8_t *devaddr, uint8_t *phypayloadtest, uint8_t fcnt) {
  //create AES on stack
  TsUnbLib::Aes128 aes;

  uint8_t frm_payload_pad[16]; //according to lora spec the frm_payload needs to be split into chunks of 16bits, if payload < 16 bit padding is needed
  //Initialize application session key for AES payload encryption
  aes.init(appSKey);

  //calculate the amount of iterations/amount of different blocks S are nedded for the given frm_payload_length
  uint8_t k = ceil((float)frm_payload_length / 16.0);
  //calculate length of array to store encoded payload
  uint8_t l = k * 16;
  uint8_t outputS[16];

  //initialize frm_payload_pad to all zeros
  for (uint8_t i = 0; i < 16; i++) {
    frm_payload_pad[i] = 0;
  }

  //initialize phypayloadtest to all zeros
  for (uint8_t i = 0; i < l; i++) {
    phypayloadtest[i] = 0;
  }

  //Genrate initialization vector A
  uint8_t A[16];
  generateA(devaddr, A,fcnt);

  //sequences are generated and phypayload is encrypted in chunks of 16 and added to the overall encrypted payload phypayloadtest
  for (uint8_t j = 0; j < k; j++) {
    A[15] = 1 + j; //increase counter i by 1 with every 16bit chunck of data (lora spec)
    aes.chipher(A, outputS); //encrypt Ai with appSKey

    for (uint8_t i = 0; i < 16; i++) {
      if ((i + (((j + 1 ) - 1) * 16)) > frm_payload_length) //checks if frm_payload_length is shorter than multiples of 16
      {
        frm_payload_pad[i] = 0; //if true, add 0 as padding (this case only applies when all the frm_payload data is already added but is shorter then multiples of 16, e.g. frm_payload_length = 31 -> but 2x16 = 32 -> index 32 is 0)
      } else {
        frm_payload_pad[i] = frm_payload[i + (((j + 1 ) - 1) * 16)]; //else add frm_payload data
      }
    }

    //actual payload encryption --> xor payload with sequence block Si
    for (uint8_t i = 0; i < 16; i++) {
      frm_payload_pad[i] ^= outputS[i];
    }

    //add encrypted payload to the phypayload collection array
    for (uint8_t i = 0; i < 16; i++) {
      phypayloadtest[i + (((j + 1 ) - 1) * 16)] = frm_payload_pad[i];
    }

    //clear frm_payload_pad so it can be used again for the next 16 bits
    for (uint8_t i = 0; i < 16; i++) {
      frm_payload_pad[i] = 0;
    }
  }
}
void LoraAES::generateCMAC(uint8_t dataLen, uint8_t *phypayload, uint8_t *networkSKey, uint8_t *devaddr, uint8_t *output, uint8_t fcnt) {
  //Create AES on stack
  TsUnbLib::Aes128 aes;

  //Genrate initialization vector B
  uint8_t B[16];
  generateB(dataLen, devaddr, B, fcnt);

  //Initialize network session key for AES CMAC generation
  aes.init(networkSKey);
  //Actual generation of CMAC --> MIC = CMAC[0..3]
  aes.generateCmac(B, phypayload, dataLen, output);
}
