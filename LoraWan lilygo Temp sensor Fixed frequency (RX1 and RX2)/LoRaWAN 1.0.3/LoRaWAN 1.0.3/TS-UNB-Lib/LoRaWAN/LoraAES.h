/**
   @brief Header file for Lora AES
*/
#ifndef LORAAES_H
#define LORAAES_H
#include <Arduino.h>

/**
   @brief Implementation of MAC Frame Payload Encryption and Message Integrity Code (MIC) according to LoRaWAN™ 1.1 Specification
   (https://lora-alliance.org/wp-content/uploads/2020/11/lorawantm_specification_-v1.1.pdf)


*/
class LoraAES {
  public:
    /**
       @brief Generates the initialization vector B for CMAC generation

       @param dataLen = len(msg)
       @param devaddr Pointer to array containing MAC_DEVADDR
       @param B Pointer to initialization vector B array
       @param fcnt Carries current frame count
    */
    void generateB(uint8_t dataLen,uint8_t *devaddr, uint8_t *B, uint8_t fcnt);
    /**
      @brief Generates the initialization vector A for MAC Frame Payload Encryption

      @param d0 First part of device address
      @param devaddr Pointer to array containing MAC_DEVADDR
      @param B Pointer to initialization vector A array
      @param fcnt Carries current frame count
    */
    void generateA(uint8_t *devaddr, uint8_t *A, uint8_t fcnt);
    /**
       @brief Write device address into device address array

       @param d0 First part of device address
       @param d1 Second part of device address
       @param d2 Thrid part of device address
       @param d3 Fourth part of device address
       @param devaddr Pointer to devaddr array
    */
    void setDevAddr(const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3, uint8_t *devaddr);
    /**
       @brief Write network session key into network session key array

       @param n0 First part of network session key
       @param n1 Second part of network session key
       @param n2 Third part of network session key
       @param n3 Fourth part of network session key
       @param n4 FIfth part of network session key
       @param n5 Sixth part of network session key
       @param n6 Seventh part of network session key
       @param n7 Eighth part of network session key
       @param n8 Ninth part of network session key
       @param n9 Tenth part of network session key
       @param n10 Eleventh part of network session key
       @param n11 Twelfth part of network session key
       @param n12 Thirteenth part of network session key
       @param n13 Fourteenth part of network session key
       @param n14 Fifteenth part of network session key
       @param n15 Sixteenth part of network session key
       @param networkSKey Pointer to networkSKey array
    */
    void setNetSKey(const uint8_t n0, const uint8_t n1, const uint8_t n2, const uint8_t n3, const uint8_t n4, const uint8_t n5, const uint8_t n6, const uint8_t n7, const uint8_t n8, const uint8_t n9, const uint8_t n10, const uint8_t n11, const uint8_t n12, const uint8_t n13, const uint8_t n14, const uint8_t n15, uint8_t *networkSKey);
    /**
      @brief Write application session key into application session key array

      @param a0 First part of application session key
      @param a1 Second part of application session key
      @param a2 Third part of application session key
      @param a3 Fourth part of application session key
      @param a4 FIfth part of application session key
      @param a5 Sixth part of application session key
      @param a6 Seventh part of application session key
      @param a7 Eighth part of application session key
      @param a8 Ninth part of application session key
      @param a9 Tenth part of application session key
      @param a10 Eleventh part of application session key
      @param a11 Twelfth part of application session key
      @param a12 Thirteenth part of application session key
      @param a13 Fourteenth part of application session key
      @param a14 Fifteenth part of application session key
      @param a15 Sixteenth part of application session key
      @param appSKey Pointer to appSKey array
    */
    void setAppSKey(const uint8_t a0, const uint8_t a1, const uint8_t a2, const uint8_t a3, const uint8_t a4, const uint8_t a5, const uint8_t a6, const uint8_t a7, const uint8_t a8, const uint8_t a9, const uint8_t a10, const uint8_t a11, const uint8_t a12, const uint8_t a13, const uint8_t a14, const uint8_t a15, uint8_t *appSKey);
    /**
       @brief Generates the CMAC from the given phypayload

       @param dataLen Length of data frame from which the MIC is created (dataFramLength - 4)
       @param phypayload Pointer to phypayload array which contains the data frame up to this point (excluding MIC)
       @param networkSKey Pointer to array containing MAC_NETWORK_SESSION_KEY 
       @param devaddr Pointer to array containing MAC_DEVADDR
       @param output Pointer to CMAC output vector (the first 4 bits represent the MIC, MIC = cmac[0..3])
       @param fcnt Carries current frame count
    */
    void generateCMAC(uint8_t dataLen, uint8_t *phypayload,uint8_t *networkSKey , uint8_t *devaddr, uint8_t *output, uint8_t fcnt);
    /**
       @brief Encode the data which is to be sent

       @param frm_payload Contains the data which is to be encoded
       @param frm_payload_length Length of frm_payload
       @param appSKey Pointer to array containing MAC_APP_SESSION_KEY 
       @param devaddr Pointer to array containing MAC_DEVADDR
       @param phypayloadtest Pointer to array which contains the encoded data
       @param fcnt Carries current frame count
    */
    void encodePayload(const uint8_t *frm_payload, uint8_t frm_payload_length, uint8_t *appSKey, uint8_t *devaddr, uint8_t *phypayloadtest,  uint8_t fcnt);
};

#endif
