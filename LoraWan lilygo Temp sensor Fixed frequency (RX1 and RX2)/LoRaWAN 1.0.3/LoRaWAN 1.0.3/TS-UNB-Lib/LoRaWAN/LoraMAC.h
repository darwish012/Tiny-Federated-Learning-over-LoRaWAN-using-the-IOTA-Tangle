/**
    @brief Header file for Lora MAC
*/
#ifndef LORAMAC_H
#define LORAMAC_H
#include <Arduino.h>

/**
   @brief Define MAC Header FType possibilities
*/
#define FType_JoinRequest 0x00;
#define FType_JoinAccept 0x20;
#define FType_unconDataUp 0x40;
#define FType_unconDataDown 0x60;
#define FType_conDataUp 0x80;
#define FType_conDataDown 0xA0;

/**
   @brief Class offers function to build and encrypt the complete data frame and triggers transmission
*/
class LoraMAC {
  public:
    /**
       @brief Builds the entire data Frame and starts transmission at the end

       @param frm_payload Data to be send
       @param frm_payload_length Length of data that is to be send
       @param d0 First part of device address
       @param d1 Second part of device address
       @param d2 Thrid part of device address
       @param d3 Fourth part of device address
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
       @param a15 Sixteenth part of application session key@param n0 First part of network session key
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
    */
    void buildDataFrame(const uint8_t* frm_payload, uint8_t frm_payload_length, const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3, const uint8_t a0, const uint8_t a1, const uint8_t a2, const uint8_t a3, const uint8_t a4, const uint8_t a5, const uint8_t a6, const uint8_t a7, const uint8_t a8, const uint8_t a9, const uint8_t a10, const uint8_t a11, const uint8_t a12, const uint8_t a13, const uint8_t a14, const uint8_t a15, const uint8_t n0, const uint8_t n1, const uint8_t n2, const uint8_t n3, const uint8_t n4, const uint8_t n5, const uint8_t n6, const uint8_t n7, const uint8_t n8, const uint8_t n9, const uint8_t n10, const uint8_t n11, const uint8_t n12, const uint8_t n13, const uint8_t n14, const uint8_t n15);
};

#endif
