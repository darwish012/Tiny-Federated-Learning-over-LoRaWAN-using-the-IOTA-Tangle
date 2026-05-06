#ifndef RFM95W_LORA_H_
#define RFM95W_LORA_H_

#include <stdint.h>

#include "../Utils/BitAccess.h"

#include "Phy.h"
namespace LoRaWAN {
namespace Trx {


#define RegOpMode 0x01 //LoraMode 
#define OPMODE_SLEEP 0x80
#define OPMODE_STANDBY 0x81
#define OPMODE_LORA 0x80
#define OPMODE_TX 0x83
#define WRITE_MODE 0x81
#define RXMODE_RSSI
#define LORA_REG_MODEM_CONFIG_1 0x1D //Modem PHY config 1
#define LORA_REG_MODEM_CONFIG_2 0x1E //Modem PHY config 2
#define LORA_REG_MODEM_CONFIG_3 0x26 //Modem PHY config 3
#define RXLORA_RXMODE_RSSI_REG_MODEM_CONFIG_1 0x0A
#define RXLORA_RXMODE_RSSI_REG_MODEM_CONFIG_2 0x70
#define LORA_PAYLOAD_LENGTH 0x22
#define LORA_FRF_MSB 0x06
#define LORA_FRF_MID 0x07
#define LORA_FRF_LSB 0x08



//! Array used for initialization of the transceiver, format: length, address, one or multiple data
#define RFM95_LORA_CONFIG_ARRAY  {\
    2, 0x80 + 0x01, 0x80,\
    2, 0x80 + 0x0A, 0x0D,\
    2, 0x80 + 0x0B, 0x2B,\
    2, 0x80 + 0x4D, 0x4B,\
    2, 0x80 + 0x39, 0x34,\
    2, 0x80 + 0x40, 0x40 | 0x30 | 0x0C,\
    2, 0x80 + 0x12, 0xFF,\
    2, 0x80 + 0x11, 0xF7,\
    2, 0x80 + 0x0E, 0x00,\
    2, 0x80 + 0x21, 0x08,\
    2, 0x80 + 0x26, 0x08,\
    2, 0x80 + 0x0D, 0x00,\
    2, 0x80 + 0x1F, 0x08,\
    0}


template <class Cpu_T, bool BOOST_PIN = false>
class Rfm95w_LoRa {
  public:
    Cpu_T Cpu;
    Rfm95w_LoRa() {
      txPower = 13;
      frequency = 868100000;
      CodeRate = CR_4_5;
      DataRate=DR0;

    }

    ~Rfm95w_LoRa() {
    }


    /**
       @brief Init method

       This module initializes the RFM69HW and brings it into sleep mode.
       It must be called before the send function. Furthermore, it shall
       be called as early as possible in the code to bring the RFM69HW
       into sleep mode to save energy after power on.

       @return 0 if OK, negative falue in case of errors
    */
    int16_t init(void) {
      // TODO check if chips is actually present and return negative value in case of error
      Cpu.spiInit();

      /*
         Initialize with presets
      */
      const uint8_t presets[] = RFM95_LORA_CONFIG_ARRAY;
      uint8_t idx = 0;
      while (presets[idx] != 0) {
        Cpu.spiSend(&presets[idx + 1], presets[idx]);
        idx += presets[idx] + 1;
      }

      Cpu.spiDeinit();

      return 0;
    }

    int16_t transmit(const uint8_t* const PHYPayload, const uint16_t PHYPayloadLength) {
      Cpu.spiInit();
      setPhyParameter();
      setTxPwrReg();
      setFrequencyReg();
      disableInvertedPolarization();
      setRegDIOMapping1(0x40|0x30|0x0C);
      /*
      		// SET IRQ
      		{
      			const uint8_t data_fifotxbaseaddr[2] = {0x80 + 0x11, 0x08};
      			Cpu.spiSend(data_fifotxbaseaddr, 2);
      		}


      		{
      			const uint8_t data_fifotxbaseaddr[2] = {0x80 + 0x12, 0xFF};
      			Cpu.spiSend(data_fifotxbaseaddr, 2);
      		}
      */
      // Freq. sync on
      {
        const uint8_t data_fifotxbaseaddr[2] = {0x80 + 0x01, 0x82};
        Cpu.spiSend(data_fifotxbaseaddr, 2);
      }
      delay(10);

      //init the payload size and address pointers
      {
        const uint8_t data_fifotxbaseaddr[2] = {0x0E + 0x80, 0x00};
        Cpu.spiSend(data_fifotxbaseaddr, 2);
      }

      {
        const uint8_t data_fifoaddrptr[2] = {0x0D + 0x80, 0};
        Cpu.spiSend(data_fifoaddrptr, 2);
      }

      {
        const uint8_t data_payloadlen[2] = {LORA_PAYLOAD_LENGTH + 0x80, (uint8_t)((PHYPayloadLength) & 0xFF)};
        Cpu.spiSend(data_payloadlen, 2);
      }

      //put data into radio FIFO
      {
        uint8_t data_fifo[PHYPayloadLength + 1]; data_fifo[0] = 0x00 + 0x80;
        for (uint8_t i = 0; i < PHYPayloadLength; ++i) {
          data_fifo[1 + i] = PHYPayload[i];
        }
        Cpu.spiSend(data_fifo, PHYPayloadLength + 1);
      }

      // OPMODE TX
      {
        const uint8_t data_fifotxbaseaddr[2] = {0x80 + 0x01, 0x83};
        Cpu.spiSend(data_fifotxbaseaddr, 2);
      }
      delay(10);
      // Wait for the end of the TX
      while (true) {
        // Check for TX done flag, exit if set
        uint8_t dataInOut[2];

        dataInOut[0] = 0x01;
        Cpu.spiSendReceive(dataInOut, 2);

        if ((dataInOut[1] & 0x07) != 0x03)
          break;
        /*

        			dataInOut[0] = 0x12;
        			Cpu.spiSendReceive(dataInOut, 2);

        			if (dataInOut[1] & 0x08)
        				break;
        			delay(10);
        */
      }


      // OPMODE Sleep
      {
        const uint8_t data_fifotxbaseaddr[2] = {0x80 + 0x01, 0x80};
        Cpu.spiSend(data_fifotxbaseaddr, 2);
      }

      Cpu.spiDeinit();

      return 0;
    }
    uint8_t* receive() {
      const uint8_t clearINT[2] = {0x80 + 0x12, 0xFF};
      Cpu.spiInit();
      Cpu.spiSend(clearINT, 2);
      setPhyParameter();
      enableInvertedPolarization();
      disablePayloadCRC();
      setFrequencyReg();
      setRegDIOMapping1(0x00|0x00|0x0C);
      uint8_t* zeros=new uint8_t[1];
      zeros[0]=0;
      //mode FS for reception
      {
        const uint8_t FSRX_mode[2] = {0x80 + 0x01, 0x84};
        Cpu.spiSend(FSRX_mode, 2);
      }

      {
        uint8_t clearMask[2] = {0x11 + 0x80, 0};
        Cpu.spiSend(clearMask, 2);
      }
      //initialize base address of fifo for reception
      {
        const uint8_t data_fiforxbaseaddr[2] = {0x0F + 0x80, 0x00};
        Cpu.spiSend(data_fiforxbaseaddr, 2);
      }
      //mode single Lora RX on
      {
        const uint8_t RX_mode[2] = {0x80 + 0x01, 0x86};
        Cpu.spiSend(RX_mode, 2);
//        uint8_t dataInOut[2];
//        dataInOut[0] = 0x42 + 0x00;
//        Cpu.spiSendReceive(dataInOut, 2);
//        Serial.println(dataInOut[1],HEX);
      }
      //clear IRQ flags Mask
      

      while (true) {
        uint8_t dataInOut[2];
        dataInOut[0] = 0x12 + 0x00;
        Cpu.spiSendReceive(dataInOut, 2);
//        Serial.println(dataInOut[1],HEX);

        

        //check for timeout
        if ((dataInOut[1] & 0x80) == 0x80) {
          break;
        }

        //check if receiving is not done
        if ((dataInOut[1] & 0x64) == 0x00) {
          continue;
        }

        //receiving is done with no errors
        if ((dataInOut[1] & 0x70) == 0x50) {
          //get the number of received bytes
          uint8_t  FifoNbRxBytes [2];
          FifoNbRxBytes[0] = 0x13 + 0x00;
          Cpu.spiSendReceive(FifoNbRxBytes, 2);
          //get the start address of the last received packet
          uint8_t  RxCrrntAddr [2];
          RxCrrntAddr[0] = 0x10 + 0x00;
          Cpu.spiSendReceive(RxCrrntAddr, 2);
          //set the fifo address pointer as the RXCurrent Address
          const uint8_t data_fifoaddrptr[2] = {0x0D + 0x80, RxCrrntAddr[1]};
          Cpu.spiSend(data_fifoaddrptr, 2);
          uint8_t* data_fifo =  new uint8_t[FifoNbRxBytes[1] + 1];
          data_fifo[0] = 0x00 + 0x00;
          uint8_t numBytes = FifoNbRxBytes[1];
          Cpu.spiSendReceive(data_fifo, numBytes +1);
          Cpu.spiSend(clearINT, 2);
          data_fifo[0]=numBytes;
          return &data_fifo[0];
        }
        else break;

      }
      
      Cpu.spiSend(clearINT, 2);
      return zeros;

    }
    uint8_t* receiveCont() {
      const uint8_t clearINT[2] = {0x80 + 0x12, 0xFF};
      Cpu.spiInit();
      Cpu.spiSend(clearINT, 2);
      setPhyParameter();
      enableInvertedPolarization();
      disablePayloadCRC();
      setFrequencyReg();
      setRegDIOMapping1(0x00|0x00|0x0C);
      //mode FS for reception
      {
        const uint8_t FSRX_mode[2] = {0x80 + 0x01, 0x84};
        Cpu.spiSend(FSRX_mode, 2);
      }

      delay(10);
      //initialize base address of fifo for reception
      {
        const uint8_t data_fiforxbaseaddr[2] = {0x0F + 0x80, 0x00};
        Cpu.spiSend(data_fiforxbaseaddr, 2);
      }

      // mode continuos Lora RX
      
         uint8_t RX_mode[2] = {0x80 + 0x01, 0x85};
        Cpu.spiSend(RX_mode, 2);
        RX_mode[1]=0x80;
      

      //clear IRQ Flags Mask
      {
        uint8_t clearMask[2] = {0x11 + 0x80, 0};
        Cpu.spiSend(clearMask, 2);
      }
      
      //uint8_t previous=0;
      //uint8_t modemStat[2];
      //modemStat[0] = 0x18 + 0x00;
      //Cpu.spiSendReceive(modemStat, 2);
      //uint8_t modemStat_prev=modemStat[1];
      //Serial.print("status ");
      //Serial.println(modemStat[1]);
      while (true) {
        uint8_t dataInOut[2];
        dataInOut[0] = 0x12 + 0x00;
        Cpu.spiSendReceive(dataInOut, 2);
        //Cpu.spiSendReceive(modemStat, 2);     
        //Serial.print(dataInOut[1]);
        
        //check for timeout
        /*
        if ((dataInOut[1] & 0x80) == 0x80) {
          Serial.println("Timeout");
          Serial.println(dataInOut[1]);
          Cpu.spiSend(clearINT, 2);
          break;
        }
        
        if (previous != dataInOut[1]){
          Serial.print("Change ");
          Serial.println(dataInOut[1]);
          previous=dataInOut[1];
        }

        if (modemStat_prev != modemStat[1]){
          Serial.print("status ");
          Serial.println(modemStat[1]);
          modemStat_prev=modemStat[1];
        }
        */
        
        //check if receiving is not done
        if ((dataInOut[1] & 0x40) == 0x00) {
          continue;
        }

        //receiving is done with no errors
        if ((dataInOut[1] & 0x40) == 0x40) {
          //get the number of received bytes
          uint8_t  FifoNbRxBytes [2];
          FifoNbRxBytes[0] = 0x13 + 0x00;
          Cpu.spiSendReceive(FifoNbRxBytes, 2);
          //get the start address of the last received packet
          uint8_t  RxCrrntAddr [2];
          RxCrrntAddr[0] = 0x10 + 0x00;
          Cpu.spiSendReceive(RxCrrntAddr, 2);
          //Serial.println("The Rx Crrnt Addr " + RxCrrntAddr[1]);
          //set the fifo address pointer as the RXCurrent Address
          const uint8_t data_fifoaddrptr[2] = {0x0D + 0x80, RxCrrntAddr[1]};
          Cpu.spiSend(data_fifoaddrptr, 2);
          uint8_t* data_fifo =  new uint8_t[FifoNbRxBytes[1] + 1];
          data_fifo[0] = 0x00 + 0x00;
          uint8_t numBytes = FifoNbRxBytes[1];
          Cpu.spiSendReceive(data_fifo, numBytes +1);
          //Serial.print("the length is");
          //Serial.println(FifoNbRxBytes[1]);
          data_fifo[0]=numBytes;
          Cpu.spiSend(clearINT, 2); 
          Cpu.spiSend(RX_mode,2);
          return &data_fifo[0];
        }
        else
        {
          Cpu.spiSend(clearINT, 2);
          continue;
        }

      }

      Cpu.spiSend(clearINT, 2);
      Cpu.spiSend(RX_mode,2);
      return 0;

    }



    /**
       @brief Sets the transmit power

       This method is used to set the transmit power in dBm.
       The default transmit power is 13dBm.

       @param 	power	Transmit power in dBm
    */
    void setTxPower(const int8_t power) {
      txPower = power;
    }

    void setFrequency(const uint32_t frequency) {
      this->frequency = frequency;
    }

    void setDR(const DataRate_e DataRate) {
      this->DataRate = DataRate;
    }

    void setCodeRate(const CodeRate_e CodeRate) {
      this->CodeRate = CodeRate;
    }
    void enableInvertedPolarization(){
      uint8_t dataInOut3[2];
      dataInOut3[0] = 0x33 + 0x00;
      Cpu.spiSendReceive(dataInOut3, 2);
      dataInOut3[0]=0x33 +0x80;
      //Serial.println("before");
      //Serial.println(dataInOut3[1],HEX);
      uint8_t tmp=dataInOut3[1];
      dataInOut3[1]= tmp | (1 << 6);
      //Serial.println("Inverted");
      //Serial.println(dataInOut3[1],HEX);
      Cpu.spiSend(dataInOut3,2);

      dataInOut3[0] = 0x3B + 0x00;
      Cpu.spiSendReceive(dataInOut3, 2);
      dataInOut3[0]=0x3B +0x80;
      //Serial.println("before");
      //Serial.println(dataInOut3[1],HEX);
      tmp=dataInOut3[1];
      dataInOut3[1]= 0x19;
      //Serial.println("Inverted");
      //Serial.println(dataInOut3[1],HEX);
      Cpu.spiSend(dataInOut3,2);
      
    }

    void disableInvertedPolarization(){
      uint8_t dataInOut2[2];
      dataInOut2[0] = 0x33 + 0x00;
      Cpu.spiSendReceive(dataInOut2, 2);
      dataInOut2[0]=0x33 +0x80;
      uint8_t tmp=dataInOut2[1];
      dataInOut2[1]= tmp & ~(1 << 6);
      Cpu.spiSend(dataInOut2,2);

      dataInOut2[0] = 0x3B + 0x00;
      Cpu.spiSendReceive(dataInOut2, 2);
      dataInOut2[0]=0x3B +0x80;
      tmp=dataInOut2[1];
      dataInOut2[1]= 0x1D;
      Cpu.spiSend(dataInOut2,2);
    }
    void disablePayloadCRC(){
      uint8_t payloadcrc[2];
      payloadcrc[0] = 0x1E + 0x00;
      Cpu.spiSendReceive(payloadcrc, 2);
      payloadcrc[0]=0x1E +0x80;
      uint8_t tmp=payloadcrc[1] & ~(0x04);
      payloadcrc[1]= tmp ;
      Cpu.spiSend(payloadcrc,2);
    }
    void setRegDIOMapping1(uint8_t dio){
      uint8_t mapping [2];
      mapping[0]=0x80+0x40;
      mapping[1]=dio;
      Cpu.spiSend(mapping,2);
    }

    uint8_t getSNR(){
      uint8_t packetSNR [2];
      packetSNR[0]=0x00+0x19;
      Cpu.spiSendReceive(packetSNR,2);
      return (int8_t) packetSNR[1];
    }

  


  private:
    /**
       @brief Set frequency register

       This method sets the frequency register to the value frequency.
       Caution: This method assumes that SPI is initialized!

    */
    void setFrequencyReg() {

      uint64_t frf = ((uint64_t)frequency << 19) / 32000000;
      
      {
        const uint8_t data_msb[2] = {LORA_FRF_MSB + 0x80, (uint8_t)(frf >> 16)};
        Cpu.spiSend(data_msb, 2);
      }
      {
        const uint8_t data_mid[2] = {LORA_FRF_MID + 0x80, (uint8_t)(frf >> 8)};
        Cpu.spiSend(data_mid, 2);
      }

      { const uint8_t data_lsb[2] = {LORA_FRF_LSB + 0x80, (uint8_t)(frf >> 0)};
        Cpu.spiSend(data_lsb, 2);
      }

    }

    /**
       @brief Set the transmit power register

       This methods sets the transmit power in the register.
       Caution: This method assumes that SPI is initialized!

       Details how on setting the transmit power can be found
       in the RFM95HW datasheet.

       @param	power	Transmit power in dBm

       @return	The actually set transmit power.
    */
    int8_t setTxPwrReg() {
      int8_t power = txPower;
      // Special handling if boost pin is used
      if (BOOST_PIN) {
        if (power > 20)
          power = 20;
        if (power < 2)
          power = 2;

        uint8_t regPower = power - 2;
        // Enable PA Dac for more than 17, this will boost by 3dB
        if (power > 17) {
          const uint8_t data[2] = {0x80 + 0x4d, 0x87};
          Cpu.spiSend(data, 2);

          // Correct register by 3dB
          regPower -= 3;
        }
        else {
          // Disable DAC otherwise
          const uint8_t data[2] = {0x80 + 0x4d, 0x84};
          Cpu.spiSend(data, 2);
        }

        uint8_t data[2] = {0x80 + 0x09, 0xF0 | regPower};
        Cpu.spiSend(data, 2);

        return power;
      }
      else {
        // PA Boost Pin is not used

        // Limit to valid power range
        if (power > 15)
          power = 15;
        if (power < 0)
          power = 0;

        uint8_t regPower = power;

        uint8_t data[2] = {0x80 + 0x09, 0x70 | regPower};
        Cpu.spiSend(data, 2);

        return power;
      }
    }

    void setPhyParameter () {
      uint8_t modemConfig1 = 0, modemConfig2 = 0, modemConfig3 = 0;
/*
      switch (Bandwidth) {
        case BW_62_5: {
            modemConfig1 |= 0x60;
          } break;
        case BW_125: {
            modemConfig1 |= 0x70;
          } break;
        case BW_250: {
            modemConfig1 |= 0x80;
          } break;
        case BW_500: {
            modemConfig1 |= 0x90;
          } break;
      }
      */

      switch (CodeRate) {
        case CR_4_5: {
            modemConfig1 |= 0x02;
          } break;
        case CR_4_6: {
            modemConfig1 |= 0x04;
          } break;
        case CR_4_7: {
            modemConfig1 |= 0x06;
          } break;
        case CR_4_8: {
            modemConfig1 |= 0x08;
          } break;
      }

      //TODO set moding confing to 0x08 if symbol exceeds 16ms
      switch (DataRate) {
        case DR6: {
            modemConfig2 |= 0x70;
            modemConfig1 |= 0x80;
          } break;
        case DR5: {
            modemConfig2 |= 0x70;
            modemConfig1 |= 0x70;
          } break;
        case DR4: {
            modemConfig2 |= 0x80;
            modemConfig1 |= 0x70;
          } break;
        case DR3: {
            modemConfig2 |= 0x90;
            modemConfig1 |= 0x70;
          } break;
        case DR2: {
            modemConfig2 |= 0xA0;
            modemConfig1 |= 0x70;
          } break;
        case DR1: {
            modemConfig2 |= 0xB0;
            modemConfig3 = 0x08;
            modemConfig1 |= 0x70;
          } break;
        case DR0: {
            modemConfig2 |= 0xC0;
            modemConfig3 = 0x08;
            modemConfig1 |= 0x70;
          } break;
      }

      // Switch on Header CRC
      modemConfig2 |= 0x04;




      // Send registers
      {
        const uint8_t spiData[2] = {LORA_REG_MODEM_CONFIG_1 + 0x80, modemConfig1};
        Cpu.spiSend(spiData, 2);
      }
      {
        const uint8_t spiData[2] = {LORA_REG_MODEM_CONFIG_2 + 0x80, modemConfig2};
        Cpu.spiSend(spiData, 2);
      }
      {
        const uint8_t spiData[2] = {LORA_REG_MODEM_CONFIG_3 + 0x80, modemConfig3};
        Cpu.spiSend(spiData, 3);
      }


      return;

    }
    



  public:
    //! Internal register to store the transmit power
    int8_t txPower;



    CodeRate_e CodeRate;
    DataRate_e DataRate;
    uint32_t frequency;
};


};	// namespace Trx
};	// namespace LoRaWAN

#endif	/*RFM95HW_H_*/
