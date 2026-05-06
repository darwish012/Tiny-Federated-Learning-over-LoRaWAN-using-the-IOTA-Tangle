



#ifndef LORAWAN_MIOTY_UPLINK_H_
#define LORAWAN_MIOTY_UPLINK_H_

#include <inttypes.h>

namespace LoRaWAN {



template<typename MAC, typename PHY, typename TX>
class MiotyUplink {
public:

	/**
	 * @brief Constructor, currently not used
	 */
	MiotyUplink() {
	}

	/**
	 * @brief Destructor, currently not used
	 */
	~MiotyUplink() {
	}

	/**
	 * @brief Initialization method
	 *
	 * This method initializes the node and the underlying Tx and Mac module.
	 * It should be called very early after the start-up of the program
	 * code to bring the transmitter into a defined state.
	 *
	 * @return 	0 in case of success, negative value in case of an initialization error
	 */
	int16_t init() {
		int16_t ret;
		ret = Tx.init();
		if (ret < 0)
			return ret;

		ret = Mac.init();
		return ret;
	}


	int16_t send(const uint8_t* const payload, const uint16_t payloadLength,
			const bool priority = false) {

		uint16_t MPDU_length = Mac.MPDU_Length(payloadLength, MPF_present);

		if (MPDU_length == 0)
			return -1;

		uint8_t MPDU[MPDU_length];
		Mac.encode(MPDU, payload, payloadLength, MPF_present, MPF_value);

		//! PHY Instance.
		PHY Phy;

		uint16_t numRadioBursts = Phy.numRadioBursts(MPDU_length);
		if (SYNC_BURST == true)
			numRadioBursts++;

		//! Allocate memory for storing radio bursts
		typename PHY::RadioBurst_t Bursts[numRadioBursts];

		// Transmit frequency
		uint32_t freqReg;

		// We have to do a seperate handling if the Sync Burts is used
		if (SYNC_BURST == false) {
			// Normal mode without sync burst
			if (priority)
				freqReg = Phy.encode(Bursts, MPDU, MPDU_length, 6, MAC::MMODE);
			else
				freqReg = Phy.encode(Bursts, MPDU, MPDU_length, Phy.getTsmaPattern(Mac.extPkgCnt), MAC::MMODE);

			if (freqReg > 0)
				return Tx.transmit(Bursts, numRadioBursts, freqReg);
			else
				return -1;
		}
		else {
			// This is special handling in caes of a sync burst
			numRadioBursts += 1;
			
			// The first data Burst is Burst[1] as Burst[0] is the Sync Burst
			if (priority) {
				freqReg = Phy.encode(&Bursts[1], MPDU, MPDU_length, 6, MAC::MMODE);
				Phy.encodeSyncBurst(&Bursts[0], 6, Mac.getLsbShortAddress());
			}
			else {
				const uint8_t tsmaPattern = Phy.getTsmaPattern(Mac.extPkgCnt);
				freqReg = Phy.encode(&Bursts[1], MPDU, MPDU_length, tsmaPattern, MAC::MMODE);
				Phy.encodeSyncBurst(&Bursts[0], tsmaPattern, Mac.getLsbShortAddress());
			}

		}
		if (freqReg > 0)
			return Tx.transmit(Bursts, numRadioBursts, freqReg);
		else
			return -1;

	}

	//! Instance of TX that is active during the complete lifetime of this class
	TX Tx;

	//! Instance of the MAC that is active during the complete lifetime of this class
	MAC Mac;


};

};	// namespace LoRaWAN


#endif // LORAWAN_MIOTY_UPLINK_H_

