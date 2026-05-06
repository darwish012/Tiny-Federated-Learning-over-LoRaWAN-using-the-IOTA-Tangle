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
 * \brief	Transmitter that writes data to file
 *
 * \authors Joerg Robert (joerg.robert@ieee.org)
 * \file	TileTx.h
 *
 */



#ifndef FILE_TX_H_
#define FILE_TX_H_

#include <fstream>
#include <complex>
#include <cmath>
#include <vector>
#include <algorithm>
#include <stdint.h>

#include <iostream>

#include "../Utils/BitAccess.h"

#ifndef M_PI
#define M_PI 3.141592653589793238462643383279502884197
#endif

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
 * \tparam		RadioBurst_T	Radio burst class
 *
 */
template <class RadioBurst_T = TsUnb::RadioBurst<> >


class FileTx {
public:
	FileTx() {
		samplingRate = 2380.371;
		
		OVERSAMPLING = 256;

		signalPhase = 0;
		
		txDelay = 16;
		txTail = 16;
	
		randomPhase = true;
		powerShaping = true;
		gaussianShaping = true;
		signalAmplitude = 1;
		
		centerFrequency = 868.13e6;
	}

	~FileTx() {
		//TODO	Implement deinit if required
	}


	const double BASE_MULTIPLIER = 15.2587890625;

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
		OutputFile.open("abc.bin");
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
		createIQVector(Bursts, numTxBursts);

		for (uint16_t burstIdx = 0; burstIdx < numTxBursts;	++burstIdx) {
			// Calculate frequency register setting
			const uint32_t cFrequency = (uint32_t) Bursts[burstIdx].getCarrierOffset();
			const uint32_t burstFreqReg = frequency + cFrequency;

			const double burstFreq = BASE_MULTIPLIER * burstFreqReg;
			
			initSigPhase();
			const uint8_t* burstData = Bursts[burstIdx].getBurst();
			
			std::vector<float> RadioBurstMod(Bursts[burstIdx].getBurstLength() * OVERSAMPLING, 0.0f);
			for (size_t symbIdx = 0; symbIdx < Bursts[burstIdx].getBurstLength(); ++symbIdx) {
				uint8_t symb = readBit(symbIdx, burstData);
				float modulation;
				if (readBit(symbIdx, burstData)) {
					modulation = BASE_MULTIPLIER * 39;
				}
				else {
					modulation = -BASE_MULTIPLIER * 39;
				}
				
				for (size_t i = 0; i < OVERSAMPLING; ++i) {
					RadioBurstMod[symbIdx * OVERSAMPLING + i] = modulation;
				}

			}	
			
			
			//TODO do the Gaussian filtering
			if (gaussianShaping) {
				const float BT = 1.0f;
				const size_t N = (size_t)(4 * OVERSAMPLING * BT);
				
				std::vector<float> GaussFilter(N * 2 + 1, 0.0f);
				
				float B_g = BT / OVERSAMPLING;
				float T_a = 1.0f / OVERSAMPLING;
				
				for (size_t i = 0; i < GaussFilter.size(); ++i) {
					
					float t = ((float)i - N) / OVERSAMPLING;
					//std::cout << t << " ";
					
					GaussFilter[i] = std::sqrt( 2 * M_PI / std::log(2) ) * B_g * T_a * std::exp( -2 * ( M_PI * B_g * t ) * ( M_PI * B_g * t ) / std::log(2.0) );
					//std::cout << GaussFilter[i] << " ";
				}
				
				// Normalize
				{
				float sum = 0;
					for (size_t i = 0; i < GaussFilter.size(); ++i) {
						sum += GaussFilter[i];
					}					
					for (size_t i = 0; i < GaussFilter.size(); ++i) {
						GaussFilter[i] /= sum;	
					}
				}				

				std::vector<float> BtFiltered(RadioBurstMod.size(), 0.0f);

				// Apply the filtering
				for (size_t i = 0; i < RadioBurstMod.size(); ++i) {
					float sum = 0;
					for (size_t j = 0; j < GaussFilter.size(); ++j) {
						
						
						ssize_t pos = (ssize_t) i - j + (GaussFilter.size() / 2);
						
						if (pos < 0) {
							sum += GaussFilter[j] * RadioBurstMod[0];
						}
						else if (pos >= RadioBurstMod.size()) {
							sum += GaussFilter[j] * RadioBurstMod[RadioBurstMod.size() - 1];
						}
						else {
							sum += GaussFilter[j] * RadioBurstMod[pos];
						}
						//sum += 
						//std::cout << pos << " " ;
					}
					BtFiltered[i] = sum;
					
					//std::cout << sum << " ";
				}
				
				
				RadioBurstMod = BtFiltered;
				
				
				
				
				
				
			}
						
			
			// Do the FSK modulation
			const size_t burstStart = getBurstStart(Bursts, burstIdx) * OVERSAMPLING;
			for (size_t sampleIdx = 0; sampleIdx < RadioBurstMod.size(); ++sampleIdx) {
				double currentFrequency = (RadioBurstMod[sampleIdx] + burstFreq - centerFrequency);
				//std::cout << " " << currentFrequency;
				double phaseInc = currentFrequency * (2 * M_PI) / (OVERSAMPLING * samplingRate);
				
				signalPhase += phaseInc;
				IQData[burstStart + sampleIdx] = signalAmplitude * std::polar(1.0f, (float)signalPhase);
				signalPhase = std::fmod(signalPhase, 2 * M_PI);
			}


			/*
			for (uint8_t symbIdx = 0; symbIdx < Bursts[burstIdx].getBurstLength(); ++symbIdx) {
				
				size_t samplePos = (getBurstStart(Bursts, burstIdx) + symbIdx) * OVERSAMPLING;
				
				// Get bits and calculate frequency for each bits
				
				uint8_t symb = readBit(symbIdx, burstData);
				double modFreq = burstFreq;
				if (symb)
					modFreq += BASE_MULTIPLIER * 39;
				else
					modFreq -= BASE_MULTIPLIER * 39;
					
				modulateSamples(modFreq, samplePos);
				//TODO add random start phase
			}*/

		}
		
		if (powerShaping)
			applyPowerRamp(Bursts, numTxBursts);
		
	
		
		if (OutputFile.is_open()) {
			OutputFile.write((char*)IQData.data(), sizeof(std::complex<float>) * IQData.size());
		}
		
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
		signalAmplitude = std::pow(10.0f, (float)power / 20.0f);
	}


	const std::vector<std::complex<float> >& getIQSamples() {
		return IQData;
	}
	
private:

	const size_t getBurstStart(const RadioBurst_T* const Bursts, const size_t numBurst) {
		size_t burstStartPos = txDelay;
		for (size_t i = 0; i < numBurst; ++i) {
			burstStartPos += Bursts[i].get_T_RB();
		}
		return burstStartPos;
	}

	void createIQVector(const RadioBurst_T* const Bursts, const uint16_t numTxBursts) {
		size_t iqVectorElements = 0;
		// Go through all bursts and get maximum length of each burst
		// Theoretically, not the last burst defines the total length (though, this should be the normal case)
		for (size_t txBurstIdx = 0; txBurstIdx < numTxBursts; ++txBurstIdx) {
			// Calculate total length of burst including delay and tail
			size_t currBurstLength = getBurstStart(Bursts, txBurstIdx) + Bursts[txBurstIdx].getBurstLength() + txTail;
			
			iqVectorElements = std::max(iqVectorElements, currBurstLength);
			
		}
		
		size_t numIQSamples = iqVectorElements * OVERSAMPLING;
		
		IQData.assign(numIQSamples, std::complex<float>(0.0f, 0.0f));
		
		
		return;
	}
	
	void initSigPhase() {
		if (randomPhase) {
			signalPhase = (double)rand() * (2.0 * M_PI / RAND_MAX);
		}
		else {
			signalPhase = 0;
		}
		
	}

	int16_t modulateSamples(const double modFrequency, size_t samplePos) {
		double phaseInc = (modFrequency - centerFrequency) * (2 * M_PI) / (samplingRate * OVERSAMPLING);
		
		for (size_t i = 0; i < OVERSAMPLING; ++i) {
			signalPhase += phaseInc;
			std::complex<float> sig = signalAmplitude * std::polar(1.0f, (float)signalPhase);
			IQData[samplePos] = sig;
			samplePos++;
		}
		signalPhase = std::fmod(signalPhase, 2 * M_PI);

		return 0;
	}
	
	
	void applyPowerRamp(const RadioBurst_T* const Bursts, const uint16_t numTxBursts) {
				
		// Use a blackman window and define all necessary constants
		const float alpha = 0.16f;
		const size_t TRANSITION_LENGTH = OVERSAMPLING;
		std::vector<float> BlackmanWindow(TRANSITION_LENGTH, 0);
		const float a_0 = (1.0f-alpha) / 2.0f;
		const float a_1 = 0.5f;
		const float a_2 = alpha / 2.0f;
		const float N = TRANSITION_LENGTH;
		for (size_t n = 0; n < TRANSITION_LENGTH; ++n) {
			BlackmanWindow[n] = a_0 - a_1 * std::cos(2.0f * (float)M_PI * n / N) + a_2 * std::cos(4.0f * (float)M_PI * n / N);
		}
		
		
		// Get integral of transission
		std::vector<float> IntWindow(TRANSITION_LENGTH, 0);
		for (size_t n = 0; n < TRANSITION_LENGTH; ++n) {
			for (size_t i = 0; i < n; ++i) {
				IntWindow[n] += BlackmanWindow[i];
			}
		}
		
		// Normalize
		for (size_t n = 0; n < TRANSITION_LENGTH; ++n) {
			IntWindow[n] /= IntWindow[TRANSITION_LENGTH - 1];
			//std::cout <<  IntWindow[n] << " ";
		}
		
		// Power Ramp vector
		std::vector<float> PowerRamp = std::vector<float>(IQData.size(), 0.0f);
		
		// Go trough all vectors and add delay and 
		for (size_t burstIdx = 0; burstIdx < numTxBursts; ++burstIdx) {
			const size_t startPos = getBurstStart(Bursts, burstIdx) * OVERSAMPLING;
			const size_t endPos = (getBurstStart(Bursts, burstIdx) + Bursts[burstIdx].getBurstLength()) * OVERSAMPLING;

			// Lead in
			for (size_t i = 0; i < TRANSITION_LENGTH; ++i) {
				PowerRamp[startPos + i] = IntWindow[i];
			}
			
			// Normally this if statement shall be always true, just to be on the safe side
			if (startPos + TRANSITION_LENGTH < endPos - TRANSITION_LENGTH) {
				for (size_t i = startPos + TRANSITION_LENGTH; i < endPos - TRANSITION_LENGTH; ++i) {
					PowerRamp[i] = 1;
				}
			}
			
			// Lead out
			for (size_t i = 0; i < TRANSITION_LENGTH; ++i) {
				PowerRamp[endPos - i - 1] = IntWindow[i];
			}

		}
		
		// Apply window
		for (size_t i = 0 ; i < IQData.size(); ++i)
			IQData[i] *= PowerRamp[i];
	}
	
	double signalPhase;

	float signalAmplitude;
	//! Internal register to store the transmit power
	int8_t txPower;
	
	
	
	double samplingRate;
	size_t OVERSAMPLING;
	
	uint32_t txDelay;
	uint32_t txTail;
	
	
	double centerFrequency;
	
	bool randomPhase;
	bool powerShaping;
	bool gaussianShaping;
	
	std::vector<std::complex<float> >IQData;	//!< Oversampled IQ vector
	
	std::ofstream OutputFile;
	
	
	
};

};	// namespace Trx
};	// namespace TsUnbLib

#endif	/*FILE_TX_H_*/

