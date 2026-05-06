/* -----------------------------------------------------------------------------
Software License for the Fraunhofer TS-UNB-Lib

(c) Copyright  2019 - 2021 Fraunhofer-Gesellschaft zur Foerderung der angewandten
Forschung e.V. All rights reserved.

1. INTRODUCTION

The Fraunhofer Telegram Splitting - Ultra Narrowband Library ("TS-UNB-Lib") is software 
that implements the ETSI TS 103 357 TS-UNB standard ("MIOTY") for wireless data 
transmission in the field of IoT. Patent licenses for necessary patent claims for 
the ETSI TS 103 357 TS-UNB standard (including those of Fraunhofer) may be obtained
through Sisvel International S.A. 
(https://www.sisvel.com/licensing-programs/wireless-communications/mioty/license-terms) 
or through the respective patent owners individually. 
Commercially-licensed MIOTY software is also available from Fraunhofer. Users are
encouraged to check the Fraunhofer website for additional applications
information and documentation.

2. COPYRIGHT LICENSE

Redistribution and use in source and binary forms, with or without modification,
are permitted without payment of copyright license fees provided that you
satisfy the following conditions:
You must retain the complete text of this software license in redistributions of
the TS-UNB-Lib software or your modifications thereto in source code form.
You must retain the complete text of this software license in the documentation
and/or other materials provided with redistributions of the TS-UNB-Lib software or
your modifications thereto in binary form. 
You must make available free of charge copies of the complete source code of the 
TS-UNB-Lib software and your modifications thereto to recipients of copies in binary form.
The name of Fraunhofer may not be used to endorse or promote products derived
from this software without prior written permission.
You may not charge copyright license fees for anyone to use, copy or distribute
the TS-UNB-Lib software or your modifications thereto.
Your modified versions of the TS-UNB-Lib software must carry prominent notices stating
that you changed the software and the date of any change. 
For modified versions of the TS-UNB-Lib software, the term "Fraunhofer TS-UNB-Lib"
must be replaced by the term "Third-Party Modified Version of the Fraunhofer TS-UNB-Lib."

3. NO PATENT LICENSE

NO EXPRESS OR IMPLIED LICENSES TO ANY PATENT CLAIMS, including without
limitation the patents of Fraunhofer, ARE GRANTED BY THIS SOFTWARE LICENSE.
Fraunhofer provides no warranty of patent non-infringement with respect to this
software.
You may use this TS-UNB-Lib software or modifications thereto only for
purposes that are authorized by appropriate patent licenses.

4. DISCLAIMER

This TS-UNB-Lib software is provided by Fraunhofer on behalf of the copyright
holders and contributors "AS IS" and WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES,
including but not limited to the implied warranties of merchantability and
fitness for a particular purpose. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
CONTRIBUTORS BE LIABLE for any direct, indirect, incidental, special, exemplary,
or consequential damages, including but not limited to procurement of substitute
goods or services; loss of use, data, or profits, or business interruption,
however caused and on any theory of liability, whether in contract, strict
liability, or tort (including negligence), arising in any way out of the use of
this software, even if advised of the possibility of such damage.

5. CONTACT INFORMATION

Fraunhofer Institute for Integrated Circuits IIS  
Attention: Division Communication Systems  
Am Wolfsmantel 33  
91058 Erlangen, Germany  
ks-contracts@iis.fraunhofer.de  

----------------------------------------------------------------------------- */

/**
 * \brief	Template for transceiver
 *
 * \authors Joerg Robert (joerg.robert@ieee.org)
 * \file	TrxTemplate.h
 *
 */



#ifndef TRX_TEMPLATE_H_
#define TRX_TEMPLATE_H_

#include <stdint.h>

#include "../Utils/BitAccess.h"

namespace TsUnbLib {
namespace Trx {




/**
 * \brief Implementation template for implementing a burst transmission
 *
 * This class is a template for the tranmsission of radio bursts using a transceiver module.
 *
 *
 * The template class RadioBurst_T defines a radio burst data structure. The actual implementation has to
 * offer the methods: uint16_t getBurstLength(void), uint8_t* getBurst(void), uint16_t get_channel(void).
 *
 * At an early stage in the program the \p init() method shall be called. It brings the device into the sleep
 * mode to save energy. It is not part of the constructor to allow the user to start a watchdog before
 * calling the \p init() method.
 * 
 * \tparam		Cpu_T			Plattform depended implementation
 * \tparam		RadioBurst_T	Radio burst class
 *
 */
template <class Cpu_T, class RadioBurst_T = TsUnb::RadioBurst<> >
class TrxTemplate {
public:
	// Instance of Implementation specific parameters
	Cpu_T Cpu;
	
	TrxTemplate() {
		//TODO	Implement init if required
	}

	~TrxTemplate() {
		//TODO	Implement deinit if required
	}


	/**
	 * \brief Init method
	 *
	 * This method initializes the transceiver and brings it into sleep mode.
	 * It must be called before the send function. Furthermore, it shall
	 * be called as early as possible in the code to bring the transceiver
	 * into sleep mode to save energy after power on.
	 *
	 * \return 0 if OK, negative falue in case of errors
	 */
	int16_t init(void) {
		Cpu.spiInit();

		// TODO Configure the TRX registers here
		
		
		Cpu.spiDeinit();	// Deinit SPI to save power

		return 0;
	}


	/**
	 * \brief	Transmit method
	 * 
	 * This method transmits the complete packed contained in the \p numTxBursts with
	 * the frequency offset given in \p frequency.
	 *
	 * \param	Bursts		Pointer to burst data
	 * \param	numTxBursts	Number of transmit bursts
	 * \param	frequency	Frequency f0 of the transmission (module dependent in register values)
	 * 
	 * \return 0 if OK, negative falue in case of errors
	 */
	int16_t transmit(const RadioBurst_T* const Bursts, const uint16_t numTxBursts, const uint32_t frequency) {
		// Init SPI and start Timer
		Cpu.spiInit();
		Cpu.initTimer();
		
		// Set the power here, because setTxPower onlys sets an member varaible
		setTxPwrReg(txPower);

		// Give the system the time of four bits to initialize everything (approx. 10ms)
		Cpu.addTimerDelay(4);
		Cpu.startTimer();

		for (uint16_t burstIdx = 0; burstIdx < numTxBursts;	++burstIdx) {
			Cpu.resetWatchdog();

			// Special handling in case of zero length bursts
			if (Bursts[burstIdx].getBurstLength() == 0) {
				Cpu.waitTimer();
				if (burstIdx + 1 < numTxBursts) {
					Cpu.addTimerDelay((int16_t)Bursts[burstIdx].get_T_RB() - Bursts[burstIdx].getBurstLength());
				}
				continue;
			}

			// Calculate frequency register setting
			const uint32_t cFrequency = (uint32_t) Bursts[burstIdx].getCarrierOffset();
			const uint32_t modFreq = frequency + cFrequency;
			Cpu.waitTimer();
			setFrequencyReg(modFreq);

			const uint8_t* burstData = Bursts[burstIdx].getBurst();
			for (uint8_t byteIdx = 0; byteIdx < Bursts[burstIdx].getBurstLengthBytes(); ++byteIdx) {
				//TODO 	Write data bytes to FIFO
			}

			Cpu.addTimerDelay(2);
			Cpu.waitTimer();
			
			// Start TX, you shall ensure that the TX stops the TX after the data has been transmitted
			// Otherwise the system will transmit for a long time if it crashes somewhere
			

			Cpu.addTimerDelay(Bursts[burstIdx].getBurstLength());
			Cpu.waitTimer();
			
			// Set the device to sleep

			// Wait for the end of the burst, exclude the last burst
			if (burstIdx + 1 < numTxBursts) {
				Cpu.addTimerDelay((int16_t)Bursts[burstIdx].get_T_RB() - 2);
			}
		}
		
		// Set the device to power off
		Cpu.stopTimer();
		Cpu.spiDeinit();

		return 0;
	}

	/**
	 * @brief Sets the transmit power
	 *
	 * This method is used to set the transmit power in dBm.
	 *
	 * @param 	power	Transmit power in dBm
	 */
	void setTxPower(const int8_t power) {
		txPower = power;
	}

private:

	/**
	 * @brief Set frequency register
	 *
	 * This method sets the frequency register to the value frequency.
	 * Caution: This method assumes that SPI is initialized!
	 *
	 */
	void setFrequencyReg(const uint32_t frequency) {
		//TODO Write frequency register value
	}
	
	/**
	 * @brief Set the transmit power register
	 *
	 * This methods sets the transmit power in the register.
	 * Caution: This method assumes that SPI is initialized!
	 *
	 * Details how on setting the transmit power can be found
	 * in the RFM69HW datasheet.
	 *
	 * @param	power	Transmit power in dBm
	 *
	 * @return	The actually set transmit power.
	 */
	int8_t setTxPwrReg(int8_t power) {
		//TODO Write TX power to register
	}

	/**
	 * @brief Set RFM69HW mode
	 *
	 * This methods sets the power mode of the RFM69HW.
	 * Caution: This method assumes that SPI is initialized!
	 *
	 * @param	mode	RFM69HW mode according to datasheet
	 */
	void setMode(const uint8_t mode) {
		
	}

	//! Internal register to store the transmit power
	int8_t txPower;
};

};	// namespace Trx
};	// namespace TsUnbLib

#endif	/*TRX_TEMPLATE_H_*/

