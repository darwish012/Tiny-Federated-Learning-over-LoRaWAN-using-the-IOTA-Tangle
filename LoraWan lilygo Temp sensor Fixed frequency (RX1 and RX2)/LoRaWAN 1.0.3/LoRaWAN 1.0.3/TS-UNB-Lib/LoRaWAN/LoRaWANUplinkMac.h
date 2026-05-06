/**
   @brief Implementation of the TS-UNB variable uplink MAC

   @authors  Joerg Robert
   @file  VariableMac.h

*/
#ifndef XTAL_PPM_OFFSET
#define XTAL_PPM_OFFSET 0
#endif

#ifndef LORAWAN_UPLINK_MAC_H_
#define LORAWAN_UPLINK_MAC_H_

#include <inttypes.h>
#include <stdint.h>
//#include <cmath>

#ifdef __AVR_ARCH__
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#endif

#include <ArduinoTsUnb.h>
#include "../Utils/BitAccess.h"

#include <EEPROM.h>
#include <Preferences.h>
#define LORA_FREQUENCY  (868100000)
//#define LORA_FREQUENCY2  (869525000)
//#define LORAWAN_UPLINK_MAC_TYPE 0x03


#define LORAWAN_UPLINK_F_TYPE_JOIN_REQUEST    0x00
#define LORAWAN_UPLINK_F_TYPE_JOIN_ACCEPT   0x20
#define LORAWAN_UPLINK_F_TYPE_UNCON_DATA_UP   0x40
#define LORAWAN_UPLINK_F_TYPE_UNCON_DATA_DOWN   0x60
#define LORAWAN_UPLINK_F_TYPE_CON_DATAUP    0x80
#define LORAWAN_UPLINK_F_TYPE_CON_DATA_DOWN   0xA0

#define BOOST_PIN true
using namespace TsUnbLib::Arduino;
static void dumpHex(const char* label, const uint8_t* b, size_t n) {
  Serial.print(label);
  Serial.print(" (");
  Serial.print(n);
  Serial.println(" bytes):");
  for (size_t i = 0; i < n; i++) {
    if (i % 16 == 0) { Serial.print("  "); }
    if (b[i] < 0x10) Serial.print("0");
    Serial.print(b[i], HEX);
    Serial.print(" ");
    if (i % 16 == 15) Serial.println();
  }
  if (n % 16 != 0) Serial.println();
}

namespace LoRaWAN {



/**
   @brief Implementation of TS-UNB Variable Uplink MAC

   This class implements the TS-UNB Fixed Uplink MAC as defined in 6.3.3

*/

class LoRaWANUplinkMac {
  public:
  LoRaWANUplinkMac () {
    FCntUp = 0;
    prefs.begin("lora", false);
    nonce = prefs.getUShort("devNonce", 0);
    prefs.end();
  }

    /**
       @brief Init method, must be called before usingthe other methods

       @return  Error code, 0 on success
    */
    int16_t init() {
      LoRaPhy.init();
      LoRaPhy.setFrequency(LORA_FREQUENCY);
      LoRaPhy.setTxPower(TRANSMIT_PWR);
      LoRaPhy.setDR(LoRaWAN::DR0);
      LoRaPhy.setCodeRate(LoRaWAN::CR_4_5);
      return 0;
    }

    /**
       @brief Create MPDU payload out of MAC payload

       @param mpduPayload Pointer to existing array for storing the output data (length at least MPDU_Length)
       @param macPayload  Pointer to input MAC payload
       @param len     Length of MAC payload data

       @return  Lenth of MPDU payload
    */
    uint16_t encode(uint8_t* const mpduPayload, uint8_t*  macPayload, uint16_t len,
                    const bool con, const bool ADRbit, const uint8_t FPort = 0x01) {
      uint8_t* dataFrame = &mpduPayload[0];

      //set MHDR
      if (con == 1) {
        dataFrame[0] = LORAWAN_UPLINK_F_TYPE_CON_DATAUP;
      }
      else {
        dataFrame[0] = LORAWAN_UPLINK_F_TYPE_UNCON_DATA_UP;
      }
      acknowledged = (acknowledged & 0x0F) | con << 4;
      for (uint8_t i = 0; i < 4; i++) {
        dataFrame[i + 1] = devAddr[3 - i]; //Add device address to data frame
      }

      //set Fctrl
      uint8_t PortIsZero = 0;
      if (FPort == 0) {
        dataFrame[8 + PortIsZero] = 0;
        PortIsZero = 1;
        dataFrame[5] = 0x00 | (acknowledged & 0x0F) << 5 | 0 | (ADRbit << 7);
      }
      else {
        PortIsZero = 0;
        dataFrame[5] = 0x00 | (acknowledged & 0x0F) << 5 | options_len | (ADRbit << 7);
      }
      //



      if (ADRbit) { //& (!ActivatedBefore) ){
        if ((ADR_ACK_CNT >= 64) & (upDataRate != LoRaWAN::DR0) ) { // set ADRACKReq bit, 64 is ADR_ACK_LIMIT
          dataFrame[5] |= (1 << 6);
          DelayCount += 1;
          //      Serial.print("ADR Delay CNT: ");
          //      Serial.println(DelayCount);
        }
        else if (ADR_ACK_CNT < 5) {
          ADR_ACK_CNT += 1;
          //        Serial.print("ADR ACK CNT: ");
          //        Serial.println(ADR_ACK_CNT);
        }
      }

      if (!ADRbit) {
        DelayCount = 0;
        ADR_ACK_CNT = 0;
      }


      if (DelayCount >= 32) { // 32 is Delay limit
        if ((upDataRate != LoRaWAN::DR0)) { // if not min data rate
          upDataRate = static_cast<DataRate_e>(upDataRate - 1);
          //          Serial.print("New Data Rate: ");
          //          Serial.println(upDataRate);
        }
        DelayCount = 0;
      }

      acknowledged = acknowledged & 0xF0;
      //set FCnt
      dataFrame[6] = (FCntUp >> 0) & 0xFF;
      dataFrame[7] = (FCntUp >> 8) & 0xFF;
      //Set Fopts

      for (uint8_t i = 0; i < options_len; i++) {
        dataFrame[8 + PortIsZero + i] = options_flags[i];
      }
      //set Fport
      uint8_t port_present = 0;
      if (len != 0) {
        dataFrame[8 + options_len] = FPort;
        port_present = 1;
      }
      for (uint16_t i = 0; i < len; i++) {
        dataFrame[8 + options_len + 1 + i] = macPayload[i];
      }
      uint8_t optlen = options_len;
      if (PortIsZero == 1) {
        len = optlen;
        optlen = 0;
        port_present = 1;
        macPayload = &options_flags[0];
      }
      encryptPayload(&dataFrame[8 + optlen + port_present], macPayload, len, FCntUp, 0, FPort);

      const uint8_t payloadLen = 8 + optlen + port_present + len;
      generateCMAC(dataFrame, payloadLen, &dataFrame[payloadLen], FCntUp, 0x00);



      options_len = 0;
      return len + (8 + optlen + port_present + 4);

    }
    bool decode_message(uint8_t* const message, uint8_t len) {
      //    Serial.println("Received Something");
      DelayCount = 0;
      ADR_ACK_CNT = 0;

      uint8_t MHDR = message[0];
      if (MHDR == LORAWAN_UPLINK_F_TYPE_UNCON_DATA_DOWN) {
        acknowledged = acknowledged & 0xF0;
      }
      else if (MHDR == LORAWAN_UPLINK_F_TYPE_CON_DATA_DOWN) {
        acknowledged = (acknowledged & 0xF0) | 1;
      }
      for (uint8_t i = 0; i < 4; i++) {
        if (devAddr[3 - i] != message[i + 1])
          return false;
      }
      if (uint32_t(message [6] | (uint16_t) message[7] << 8) - FCntDown >= MaxFCntGap) {
        //Serial.println("Frame Counter Error");
        return false;
      }
      FCntDown = uint32_t(message [6] | (uint16_t) message[7] << 8);
      generateCMAC(message, len - 4, &message[len], FCntDown, 0x01);
      bool Integrity = (message[len - 1] == message[len + 3]) & (message[len - 2] == message[len + 2]) & (message[len - 3] == message[len + 1]) & (message[len - 4] == message[len]);
      if (!Integrity) {
        Serial.println("Integrity Error, Message might have been Tampered");
        return false;
      }
      uint8_t FCtrl = message[5];
      uint8_t FPort = NULL;
      bool PortIsZero = 0;
      FPending = (FCtrl & 0x10);
      if (len - 8 - (FCtrl & 0x0F) - 4 > 0) {
        FPort = message[8 + (FCtrl & 0x0F)];
        encryptPayload(&message[8 + (FCtrl & 0x0F) + 1], &message[8 + (FCtrl & 0x0F) + 1], len - 8 - (FCtrl & 0x0F) - 5, FCntDown, 1, FPort);
      }
      if ((FCtrl & 0x0F) > 0) {
        decodeOptions(&message[8], FCtrl & 0x0F);
      } else {
        if ((len - 8 - 4) > 0) {
          if (FPort == 0) {
            PortIsZero = 1;
            decodeOptions(&message[9], len - 9 - 4);
          }
        }
        //you can return payload, port etc
      }
      acknowledged = acknowledged ^ ((FCtrl & 0x20 ) >> 1);


      FCntDown++;
      contParamLen = 0;
      return true;

    }

    uint8_t join_request(uint8_t* const mpduPayload) {
      //mpduPayload[0] = LORAWAN_UPLINK_MAC_TYPE;

      uint8_t* dataFrame = &mpduPayload[0];

      //set MHDR

      dataFrame[0] = LORAWAN_UPLINK_F_TYPE_JOIN_REQUEST;
      for (uint8_t i = 0; i < 8; i++) {
        dataFrame[i + 1] = joinEUI[7 - i]; //Add join EUI to data frame
      }
      for (uint8_t i = 0; i < 8; i++) {
        dataFrame[i + 9] = devEUI[7 - i]; //Add dev EUI to data frame
      }

      prefs.begin("lora", false);
      nonce = prefs.getUShort("devNonce", 0);

      if (nonce == 0) {
        nonce = random(0x0001, 0xFFFF);
        Serial.print("[DEBUG] First join - Random DevNonce: 0x");
        Serial.println(nonce, HEX);
      } else {
        nonce++;
        Serial.print("[DEBUG] DevNonce incremented to: 0x");
        Serial.println(nonce, HEX);
      }

      prefs.putUShort("devNonce", nonce);
      prefs.end();

      dataFrame[17] = (nonce >> 0) & 0xFF;
      dataFrame[18] = (nonce >> 8) & 0xFF;
      Serial.println("==== [DEBUG] JoinRequest fields BEFORE MIC ====");

      // Show stored JoinEUI / DevEUI arrays (the ones you set via setJoinEUI / setDevEUI)
      dumpHex("joinEUI[] stored", joinEUI, 8);
      dumpHex("devEUI[] stored",  devEUI,  8);

      // Show what actually got written into the outgoing packet (these are reversed by code)
      dumpHex("JoinEUI on-air bytes (frame[1..8])",  &dataFrame[1], 8);
      dumpHex("DevEUI on-air bytes  (frame[9..16])", &dataFrame[9], 8);

      // Nonce details
      Serial.print("EEPROM nonce value (uint16): ");
      Serial.println(nonce);
      Serial.print("DevNonce bytes on-air (frame[17], frame[18]) = ");
      Serial.print("0x"); if (dataFrame[17] < 0x10) Serial.print("0"); Serial.print(dataFrame[17], HEX);
      Serial.print(" 0x"); if (dataFrame[18] < 0x10) Serial.print("0"); Serial.println(dataFrame[18], HEX);

      // The exact payload that will be signed (MUST be 19 bytes: MHDR..DevNonce)
      dumpHex("JoinReq payload used for CMAC (frame[0..18])", dataFrame, 19);

      // Key sanity (AppKey as bytes)
      dumpHex("AppKey bytes used", appKey, 16);

      Serial.println("===============================================");

      
      const uint8_t payloadLen = 19;

      generateCMACjoin(dataFrame, payloadLen, &dataFrame[payloadLen]);
      dumpHex("MIC appended into frame[19..22]", &dataFrame[19], 4);
      dumpHex("Full JoinRequest frame[0..22]", dataFrame, 23);

      FCntUp = 0;
      FCntDown = 0;
      return 23;

    }


    bool decode_accept(uint8_t* const accept_message, uint8_t len) {


      uint8_t MHDR = accept_message[0];
      //Decrypt the Join Accept Message
      TsUnbLib::Aes128 Aes;
      Aes.init(appKey);
      Aes.chipher(&accept_message[1], &accept_message[1]);
      if (len > 17) {
        Aes.chipher(&accept_message[17], &accept_message[17]);
      }
      //Prove Integrity by re-calculating MIC
      generateCMACjoin(accept_message, len - 4, &accept_message[len]);
      bool Integrity = (accept_message[len - 1] == accept_message[len + 3]) & (accept_message[len - 2] == accept_message[len + 2]) & (accept_message[len - 3] == accept_message[len + 1]) & (accept_message[len - 4] == accept_message[len]);
      if (!Integrity) {
        //Serial.println("Integrity Error, Message might have been Tampered");
        return false;
      }
      if (len > 17) {
        //extract the CFList to later use it if it exists
        setCFList(accept_message[28], accept_message[27], accept_message[26], accept_message[25], accept_message[24], accept_message[23], accept_message[22], accept_message[21], accept_message[20]
                  , accept_message[19], accept_message[18], accept_message[17], accept_message[16], accept_message[15], accept_message[14], accept_message[13]);
      }
      //Calculate Network Session Key
      uint8_t vec[16] = {0x01, accept_message[1], accept_message[2], accept_message[3],
                         accept_message[4], accept_message[5], accept_message[6],
                         lowByte(nonce), highByte(nonce),
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                        };
      Aes.chipher(vec, netSKey);
      //Calculate App Session Key
      vec[0] = 0x02;
      Aes.chipher(vec, appSKey);
      //Extract NetID, DevAddr,DLSettings,RXDelay
      setNetID(accept_message[6], accept_message[5], accept_message[4]);
      setDevAddr(accept_message[10], accept_message[9], accept_message[8], accept_message[7]);
      DLSettings = accept_message[11];
      RXDelay = accept_message[12];

      return true;

    }



    bool join() {
    defaultSettings();
    uint8_t mpduPayload[23];
    uint8_t len = join_request(mpduPayload);
    
    Serial.println("Join Request Bytes (HEX):");
    for(int i=0; i<len; i++) {
        if(i<10) Serial.print("0");
        Serial.print(i);
        Serial.print(": 0x");
        if(mpduPayload[i] < 0x10) Serial.print("0");
        Serial.print(mpduPayload[i], HEX);
        Serial.print("  ");
        if((i+1) % 8 == 0) Serial.println();
    }
    Serial.println("\n---");
    
    //uint8_t selected_channel = rand() % 16;
    //while (((activatedChannelsInt >> selected_channel) & 1) == 0) {
        //selected_channel = rand() % 16;
    //}
    uint8_t selected_channel = 0;
    
    // DEBUG: Print channel info
    Serial.print("[DEBUG] Selected channel: ");
    Serial.println(selected_channel);
    Serial.print("[DEBUG] Frequency: ");
    Serial.println(Channels[selected_channel]);
    Serial.print("[DEBUG] Data rate: ");
    Serial.println(upDataRate);
    
    LoRaPhy.setFrequency(Channels[selected_channel]);
    LoRaPhy.setDR(upDataRate);
    LoRaPhy.setTxPower(TRANSMIT_PWR);
    Serial.println(">>> Sending JoinRequest...");
    LoRaPhy.transmit(mpduPayload, len);
    
    uint32_t now = micros();
    
    // while (micros() - now < 5000000);
    delay(5000);
    Serial.println(">>> Opening RX1 window...");
    uint8_t accepted = false;
    setDownStreamDataRate1();
    LoRaPhy.setFrequency(Channels_DL[selected_channel]);
    
    // DEBUG: Print RX1 frequency
    Serial.print("[DEBUG] RX1 frequency: ");
    Serial.println(Channels_DL[selected_channel]);
    
    uint8_t* received = LoRaPhy.receive();
    
    // DEBUG: Print RX1 received data
    Serial.print("[DEBUG] RX1 received length: ");
    Serial.println(received[0]);
    if (received[0] > 0) {
        Serial.print("[DEBUG] RX1 data: ");
        for (int j = 1; j <= received[0]; j++) {
            Serial.print(received[j], HEX);
            Serial.print(" ");
        }
        Serial.println();
    }
    
    // RX1: try to decode if something was received
    if (received[0] != 0 && received[1] == 0x20) {
        accepted = decode_accept(&received[1], received[0]);
    }

    // RX2: opens exactly once, only if RX1 did not yield an accepted join
    if (!accepted) {
        // while (micros() - now < 6000000);
        delay(6000);
        LoRaPhy.setFrequency(LORA_FREQUENCY2);
        setDownStreamDataRate2();

        Serial.println(">>> Opening RX2 window...");
        Serial.print("[DEBUG] RX2 frequency: ");
        Serial.println(LORA_FREQUENCY2);

        received = LoRaPhy.receive();

        Serial.print("[DEBUG] RX2 received length: ");
        Serial.println(received[0]);
        if (received[0] > 0) {
            Serial.print("[DEBUG] RX2 data: ");
            for (int j = 1; j <= received[0]; j++) {
                Serial.print(received[j], HEX);
                Serial.print(" ");
            }
            Serial.println();
        }

        if (received[0] != 0 && received[1] == 0x20) {
            accepted = decode_accept(&received[1], received[0]);
        }
    }
    
    // DEBUG: Print final result
    Serial.print("[DEBUG] Join result: ");
    Serial.println(accepted ? "ACCEPTED" : "REJECTED");
    
    return accepted;
}

    uint8_t send_message(uint8_t* const message, uint8_t len, const bool con, const bool ADRbit, const uint8_t FPort = 0x01) {
      uint8_t mpduPayload[32];
      uint16_t ln = encode(mpduPayload, message, len, con, ADRbit, FPort);
      uint8_t selected_channel = rand() % 16;
      while ((((activatedChannelsInt >> selected_channel) & 1) == 0) | (upDataRate > ((Channels_DR[selected_channel] & 0xf0) >> 4)) | (upDataRate < (Channels_DR[selected_channel] & 0x0f))) {
        selected_channel = rand() % 16;
      }
      //upDataRate=static_cast<DataRate_e>((Channels_DR[selected_channel] & 0x0f) + rand() % (((Channels_DR[selected_channel] & 0xf0)>>4) - (Channels_DR[selected_channel] & 0x0f) + 1));
      LoRaPhy.setFrequency(Channels[selected_channel]);
      LoRaPhy.setDR(upDataRate);
      LoRaPhy.setTxPower(TRANSMIT_PWR);
      //    Serial.print("RXDelay: ");
      //    Serial.println(RXDelay);
      //    Serial.print("UpDataRate: ");
      //    Serial.println(upDataRate);
      //    Serial.println("Sent");
      //    for (int i =0;i<len;i++){
      //      Serial.print(message[i],HEX);
      //      Serial.print(", ");
      //    }
      LoRaPhy.transmit(mpduPayload, ln);
      uint32_t now = micros();
      while (micros() - now < RXDelay * 1000000);
      setDownStreamDataRate1();
      LoRaPhy.setFrequency(Channels_DL[selected_channel]);
      uint8_t* received = LoRaPhy.receive();
      uint8_t decoded_good = false;



      // RX1: try to decode if something was received
      if (received[0] != 0) {
        decoded_good = decode_message(&received[1], received[0]);
      }

      // RX2: opens exactly once, only if RX1 did not yield a good decode
      if (!decoded_good) {
        while (micros() - now < RXDelay * 1000000 + 1000000);
        LoRaPhy.setFrequency(LORA_FREQUENCY2);
        setDownStreamDataRate2();
        received = LoRaPhy.receive();
        if (received[0] != 0) {
          decoded_good = decode_message(&received[1], received[0]);
        }
      }
      if ((acknowledged & 0xF0) == 0x10) {
        if (retransmitted < max_retransmissions) {
          delay((rand() % retransmitted) * 1000);
          //Serial.println("retransmission");
          retransmitted++;
          return send_message(message, len, con, ADRbit, FPort);
        }
        else {
          retransmitted = 0;
        }

      }
      FCntUp++;

      return decoded_good;
    }

    void setDownStreamDataRate1() {
      uint8_t dr = upDataRate - (DLSettings & 0x70);
      if (dr < 0) {
        LoRaPhy.setDR(LoRaWAN::DR0);
      }
      else {
        LoRaPhy.setDR(static_cast<DataRate_e>(dr));
      }
      return;
    }

    void setDownStreamDataRate2() {
      LoRaPhy.setDR(static_cast<DataRate_e>(DLSettings & 0x0F));
      return;
    }

    void decodeOptions(uint8_t* const options, uint8_t len) {
outerloop:
      for (uint8_t i = 0; i < len; i++) {
        switch (options[i]) {
          case 0x03: {
              options_flags[options_len] = 0x03;
              options_flags[options_len + 1] = 0;
              if ((options[i + 1] & 0x0f) <= 20) {
                TRANSMIT_PWR = options[i + 1] & 0x0f;
                TRANSMIT_PWR = 16 - TRANSMIT_PWR * 2;
                options_flags[options_len + 1] |= 4;
                //            Serial.println(" ");
                //            Serial.print("New Transmit Power: ");
                //            Serial.println(TRANSMIT_PWR);
              }
              bool allChannelsAccepted = true;
              uint16_t channelsToActivateInt = 0;
              nbTrans = options[i + 4] & 0x0F; //global variable
              if (nbTrans == 0) {
                nbTrans = 1;
              }
              //          Serial.println(" ");
              //          Serial.print("New nbTrans: ");
              //          Serial.println(nbTrans);
              switch ((options[i + 4] & 0x70) >> 4) { //chMaskCntl
                case (0): {
                    for (int k = 0; k < 8; k++) {

                      if (( ((options[i + 2] >> k) & 1) && (Channels[k] == 0)) || (  ((options[i + 3] >> k) & 1) && (Channels[k + 8] == 0))) {
                        allChannelsAccepted = false;
                        break;
                      }
                      channelsToActivateInt |= ( ((options[i + 2] >> k) & 1) << k);
                      channelsToActivateInt |= (((options[i + 3] >> k) & 1) << (k + 8) );
                    }
                    if (allChannelsAccepted) {
                      activatedChannelsInt = channelsToActivateInt;
                      options_flags[options_len + 1] |= 1;
                    }
                  } break;
                case (6): {
                    for (int u = 0; u < 16; u++) {
                      if (Channels[u] != 0) {
                        channelsToActivateInt |= (1 << u);
                      }
                    }
                    activatedChannelsInt = channelsToActivateInt; // All currently defined channels on
                  } break;

                default: {

                  } break;
              }

              //          Serial.print("Activated Channels:");
              //          Serial.println(channelsToActivateInt,HEX);
              for (int j = 0; j < 16; j++) {
                if ( (activatedChannelsInt >> j) & 1 ) { // if the channel is activated
                  //if the corrseponding data rate to this channel is in range
                  if (( (Channels_DR[j] & 0x0f) <= ((options[i + 1] & 0xf0 ) >> 4) ) &
                      (Channels_DR[j] & 0xf0) >=  ((options[i + 1] & 0xf0 ) >> 4)) {
                    upDataRate = static_cast<DataRate_e>((options[i + 1] & 0xf0 ) >> 4);
                    //                  Serial.print("Data Rate Changed to: ");
                    //                  Serial.println(upDataRate);
                    options_flags[options_len + 1] = options_flags[options_len + 1] | 2;
                    break;

                  }
                }
              }
              i += 4;
              options_len += 2;
            } break;
          case 0x05: {
              contParam[contParamLen] = 0x05;
              contParam[contParamLen + 1] = 0;
              if (((options [i + 1] & 0x70) <= 5) & ((options [i + 1] & 0x70) >= 0)) {
                DLSettings = (DLSettings & 0x8F) | (options [i + 1] & 0x70);
                contParam[contParamLen + 1] = contParam[contParamLen + 1] | 0x04;
                //            Serial.print("New DL Settings: ");
                //            Serial.println(DLSettings);
              }
              if (((options [i + 1] & 0x0F) <= 6) & ((options [i + 1] & 0x0F) >= 0)) {
                DLSettings = (DLSettings & 0xF0) | (options [i + 1] & 0x0F);
                contParam[contParamLen + 1] = contParam[contParamLen + 1] | 0x02;
                //            Serial.print("New DL Settings: ");
                //            Serial.println(DLSettings);
              }
              uint32_t freq = uint32_t(options[i + 2] | (uint16_t) options[i + 3] << 8 | (uint32_t) options[i + 4] << 16) * 100;
              if ((freq <= 870000000) & (freq >= 863000000)) {
                LORA_FREQUENCY2 = freq;
                contParam[contParamLen + 1] = contParam[contParamLen + 1] | 0x01;
                //            Serial.print("New Freq for R2: ");
                //            Serial.println(LORA_FREQUENCY2);
              }

              contParamLen = contParamLen + 2;
              i = i + 4;
            } break;
          case 0x06: {
              options_flags[options_len] = 0x06;
              options_flags[options_len + 1] = 255;
              options_flags[options_len + 2] = LoRaPhy.getSNR() & 0x3f;
              options_len = options_len + 3;
              //          Serial.println("dev status Requested");
            } break;

          case 0x07: {
              options_flags[options_len] = 0x07;
              options_flags[options_len + 1] = 0;
              uint32_t freq = uint32_t(options[i + 2] | (uint16_t) options[i + 3] << 8 | (uint32_t) options[i + 4] << 16) * 100;
              if (((freq <= 870000000) & (freq >= 863000000)) | (freq == 0)) {
                options_flags[options_len + 1] = options_flags[options_len + 1] | 0x01;
                if ((((options[i + 5] & 0xF0) >> 4) <= 6) & ((options[i + 5] & 0x0F) <= 6)) {
                  Channels[options[i + 1]] = freq;
                  Channels_DL[options[i + 1]] = freq;
                  Channels_DR[options[i + 1]] = options[i + 5];
                  options_flags[options_len + 1] = options_flags[options_len + 1] | 0x02;
                  //              Serial.print("New Channel Added: ");
                  //              Serial.println(freq);
                  //              Serial.print("New Channel DR: ");
                  //              Serial.println(options[i+5],HEX);
                  if (freq != 0) {
                    activatedChannelsInt |= (1 << options[i + 1]);
                  }
                  else {
                    activatedChannelsInt &= !(1 << options[i + 1]);
                  }
                  //              Serial.print("Activated Channels Now: ");
                  //              Serial.println(activatedChannelsInt,HEX);
                }
              }
              i = i + 5;
              options_len = options_len + 2;
            } break;

          case 0x08: {
              if (options[i + 1] & 0x0F != 0) {
                RXDelay = options[i + 1] & 0x0F;
              }
              else {
                RXDelay = 1;
              }
              //          Serial.print("New RXDelay: ");
              //          Serial.println(RXDelay);
              i = i + 1;
              contParam[contParamLen] = 0x08;
              contParamLen = contParamLen + 1;
            } break;


          case 0x0A: {
              contParam[contParamLen] = 0x0A;
              contParam[contParamLen + 1] = 0;
              uint32_t freq = uint32_t(options[i + 2] | (uint16_t) options[i + 3] << 8 | (uint32_t) options[i + 4] << 16) * 100;
              if (Channels[options[i + 1]] != 0) {
                contParam[contParamLen + 1] = contParam[contParamLen + 1] | 0x02;
                if ((freq <= 870000000) & (freq >= 863000000)) {
                  contParam[contParamLen + 1] = contParam[contParamLen + 1] | 0x01;
                  Channels_DL[options[i + 1]] = freq;
                }
              }
              i = i + 4;
              contParamLen = contParamLen + 2;
            } break;
          default: goto outerloopEnd;
        }
      }
outerloopEnd:
      if ((contParamLen != 0)) {
        for ( int j = 0; j < contParamLen; j++) {
          options_flags[options_len] = contParam[j];
          options_len = options_len + 1;
        }
      }
      return;

    }






    /**
       @brief Get the MPDU length for MAC_PayloadLength

       @param MAC_PayloadLength Payload length of the MAC

       @return  Lenght of the MPDU

    */
    uint16_t MPDU_Length(const uint16_t MAC_PayloadLength, const bool) const {
      return MAC_PayloadLength + (9 + 4);
    }

    /**
       @brief Get LSB of Short Address for Sync Burst

       @return  0, as not used here

    */
    uint8_t getLsbShortAddress() const {
      return devAddr[0];
    }


    void setDevAddr(const uint8_t d0, const uint8_t d1, const uint8_t d2, const uint8_t d3) {
      devAddr[0] = d0;
      devAddr[1] = d1;
      devAddr[2] = d2;
      devAddr[3] = d3;
    }

    void setNetID(const uint8_t d0, const uint8_t d1, const uint8_t d2) {
      netID[0] = d0;
      netID[1] = d1;
      netID[2] = d2;
    }

    void setNetSKey(const uint8_t n0, const uint8_t n1, const uint8_t n2, const uint8_t n3,
                    const uint8_t n4, const uint8_t n5, const uint8_t n6, const uint8_t n7,
                    const uint8_t n8, const uint8_t n9, const uint8_t n10, const uint8_t n11,
                    const uint8_t n12, const uint8_t n13, const uint8_t n14, const uint8_t n15) {
      netSKey[0] = n0;
      netSKey[1] = n1;
      netSKey[2] = n2;
      netSKey[3] = n3;
      netSKey[4] = n4;
      netSKey[5] = n5;
      netSKey[6] = n6;
      netSKey[7] = n7;
      netSKey[8] = n8;
      netSKey[9] = n9;
      netSKey[10] = n10;
      netSKey[11] = n11;
      netSKey[12] = n12;
      netSKey[13] = n13;
      netSKey[14] = n14;
      netSKey[15] = n15;
    }
    void setappKey(const uint8_t n0, const uint8_t n1, const uint8_t n2, const uint8_t n3,
                   const uint8_t n4, const uint8_t n5, const uint8_t n6, const uint8_t n7,
                   const uint8_t n8, const uint8_t n9, const uint8_t n10, const uint8_t n11,
                   const uint8_t n12, const uint8_t n13, const uint8_t n14, const uint8_t n15) {
      appKey[0] = n0;
      appKey[1] = n1;
      appKey[2] = n2;
      appKey[3] = n3;
      appKey[4] = n4;
      appKey[5] = n5;
      appKey[6] = n6;
      appKey[7] = n7;
      appKey[8] = n8;
      appKey[9] = n9;
      appKey[10] = n10;
      appKey[11] = n11;
      appKey[12] = n12;
      appKey[13] = n13;
      appKey[14] = n14;
      appKey[15] = n15;
    }

    void setAppSKey(const uint8_t a0, const uint8_t a1, const uint8_t a2, const uint8_t a3,
                    const uint8_t a4, const uint8_t a5, const uint8_t a6, const uint8_t a7,
                    const uint8_t a8, const uint8_t a9, const uint8_t a10, const uint8_t a11,
                    const uint8_t a12, const uint8_t a13, const uint8_t a14, const uint8_t a15) {
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
    void setCFList(const uint8_t a0, const uint8_t a1, const uint8_t a2, const uint8_t a3,
                   const uint8_t a4, const uint8_t a5, const uint8_t a6, const uint8_t a7,
                   const uint8_t a8, const uint8_t a9, const uint8_t a10, const uint8_t a11,
                   const uint8_t a12, const uint8_t a13, const uint8_t a14, const uint8_t a15) {
      Channels[7] = uint32_t(a3 | (uint32_t) a2 << 8 | (uint32_t) a1 << 16) * 100;
      Channels_DL[7] = Channels[7];
      Channels[6] = uint32_t(a6 | (uint16_t) a5 << 8 | (uint32_t) a4 << 16) * 100;
      Channels_DL[6] = Channels[6];
      Channels[5] = uint32_t(a9 | (uint16_t) a8 << 8 | (uint32_t) a7 << 16) * 100;
      Channels_DL[5] = Channels[5];
      Channels[4] = uint32_t(a12 | (uint16_t) a11 << 8 | (uint32_t) a10 << 16) * 100;
      Channels_DL[4] = Channels[4];
      Channels[3] = uint32_t(a15 | (uint16_t) a14 << 8 | (uint32_t) a13 << 16) * 100;
      Channels_DL[3] = Channels[3];
      activatedChannelsInt |= 0xF8;
      Channels_DR[3] = 0x50;
      Channels_DR[4] = 0x50;
      Channels_DR[5] = 0x50;
      Channels_DR[6] = 0x50;
      Channels_DR[7] = 0x50;
    }

    void setJoinEUI(const uint8_t a0, const uint8_t a1, const uint8_t a2, const uint8_t a3,
                    const uint8_t a4, const uint8_t a5, const uint8_t a6, const uint8_t a7) {
      joinEUI[0] = a0;
      joinEUI[1] = a1;
      joinEUI[2] = a2;
      joinEUI[3] = a3;
      joinEUI[4] = a4;
      joinEUI[5] = a5;
      joinEUI[6] = a6;
      joinEUI[7] = a7;
    }

    void setDevEUI(const uint8_t a0, const uint8_t a1, const uint8_t a2, const uint8_t a3,
                   const uint8_t a4, const uint8_t a5, const uint8_t a6, const uint8_t a7) {
      devEUI[0] = a0;
      devEUI[1] = a1;
      devEUI[2] = a2;
      devEUI[3] = a3;
      devEUI[4] = a4;
      devEUI[5] = a5;
      devEUI[6] = a6;
      devEUI[7] = a7;
    }

    void defaultSettings() {
      contParamLen = 0;
      LORA_FREQUENCY2 = 868100000;
      options_len = 0;
      for (uint8_t i = 0; i < 16; i++) {
        if (i > 2) {
          Channels[i] = 0;
          Channels_DL[i] = 0;
          Channels_DR[i] = 0;
        }
        options_flags[i] = 0;
      }
      retransmitted = 0;
      DLSettings = 0;
    }
    uint8_t getOptionAnsLength() {
      return options_len;
    }
    bool getFPending() {
      return FPending;
    }





    /**
       @brief Get internal Counter (i.e. extPkgCnt)

       @return  FCntUp

    */
    uint32_t getCounter() const {
      return FCntUp;
    }
    uint8_t retransmitted = 0;
    uint8_t max_retransmissions = 10;
    uint8_t TRANSMIT_PWR = 5;
    uint32_t FCntUp;
    uint32_t FCntDown;/**< @brief Extended packet counter */
    static const uint8_t MMODE = 1; /**< @brief MMODE for variable MAC */
    uint16_t MaxFCntGap = 16384;
    uint8_t appKey[16];
    uint8_t netSKey[16]; /**< @brief Network key */
    uint8_t appSKey[16]; /**< @brief Network key */
    uint8_t devAddr[4];
    uint8_t joinEUI[8];
    uint8_t devEUI[8];
    uint8_t netID[3];
    uint32_t Channels[16] = {868100000, 868300000, 868500000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bool FPending = 0;
    uint8_t ADR_ACK_CNT = 0;
    uint8_t DelayCount = 0;

    uint16_t activatedChannelsInt = 7;
    uint8_t nbTrans;

    uint32_t Channels_DL[16] = {868100000, 868300000, 868500000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t Channels_DR[16] = {0x60, 0x60, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t address = 2;
    uint8_t DLSettings = 0;
    uint8_t RXDelay = 1;
    uint16_t nonce;
    uint32_t LORA_FREQUENCY2 = 869525000;
    uint8_t options_flags[64] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t options_len;
    uint8_t acknowledged;
    uint8_t contParamLen = 0;
    uint8_t contParam[5] = {0, 0, 0, 0, 0};
    DataRate_e upDataRate = LoRaWAN::DR4;
    LoRaWAN::Trx::Rfm95w_LoRa<ArduinoTsUnb<18, 48, XTAL_PPM_OFFSET>, BOOST_PIN> LoRaPhy;
  private:
    Preferences prefs;

    void encryptPayload(uint8_t *FRMPayload, const uint8_t *inputData, const uint8_t inputLength, const uint32_t Fcnt, const uint8_t Dir = 0, const uint8_t FPort = 1) {
      TsUnbLib::Aes128 Aes;
      if (FPort == 0) {
        Aes.init(netSKey);
      }
      else {
        Aes.init(appSKey);
      }

      uint8_t A_vec[16] = {
        0x01,
        0x00, 0x00, 0x00, 0x00,
        Dir,
        devAddr[3], devAddr[2], devAddr[1], devAddr[0],
        (uint8_t)((Fcnt >> 0) & 0xFF), (uint8_t)((Fcnt >> 8) & 0xFF), (uint8_t)((Fcnt >> 16) & 0xFF), (uint8_t)((Fcnt >> 24) & 0xFF),
        0x00,
        0x01
      };

      const uint8_t k = (inputLength + 15) / 16;
      for (uint8_t i = 0; i < k; ++i) {
        uint8_t S_vec[16];
        A_vec[15] = i + 1;
        Aes.chipher(A_vec, S_vec);

        for (uint8_t j = 0; j < 16; j++) {
          uint16_t pos = (uint16_t)i * 16 + j;
          if (pos < inputLength) {
            FRMPayload[pos] = inputData[pos] ^ S_vec[j];
          }
        }
      }
    }

    void generateCMAC(const uint8_t *playload, const uint8_t payloadLen, uint8_t *output, const uint32_t Fcnt, uint8_t dir) {
      //Create AES on stack
      TsUnbLib::Aes128 Aes;
      Aes.init(netSKey);

      //Genrate initialization vector B
      uint8_t B0_vec[16] = {
        0x49,
        0x00, 0x00, 0x00, 0x00,
        dir,
        devAddr[3], devAddr[2], devAddr[1], devAddr[0],
        (uint8_t)((Fcnt >> 0) & 0xFF), (uint8_t)((Fcnt >> 8) & 0xFF), (uint8_t)((Fcnt >> 16) & 0xFF), (uint8_t)((Fcnt >> 24) & 0xFF),
        0x00,
        payloadLen
      };

      //Actual generation of CMAC --> MIC = CMAC[0..3]
      Aes.generateCmac(B0_vec, playload, payloadLen, B0_vec);

      for (uint8_t i = 0; i < 4; i++) {
        output[i] = B0_vec[i];
      }


    }

    void generateCMACjoin(const uint8_t *payload, const uint8_t payloadLen, uint8_t *output) {
      TsUnbLib::Aes128 Aes;
      Aes.init(appKey);

      uint8_t vec[16] = {0};   // initial

      Serial.println("==== [DEBUG] generateCMACjoin() ====");
      dumpHex("payload passed into generateCMACjoin()", payload, payloadLen);
      dumpHex("AppKey inside generateCMACjoin()", appKey, 16);
      dumpHex("vec BEFORE CMAC", vec, 16);

      // Compute CMAC ONCE
      Aes.generateCmac(payload, payloadLen, vec);

      dumpHex("vec AFTER CMAC (CMAC result)", vec, 16);
      dumpHex("MIC bytes (vec[0..3])", vec, 4);

      // Copy MIC to output (same logic)
      for (uint8_t i = 0; i < 4; i++) output[i] = vec[i];

      dumpHex("output MIC written to frame", output, 4);
      Serial.println("====================================");
    }

};


};  // namespace LoRaWAN

#endif // LORAWAN_UPLINK_MAC_H_