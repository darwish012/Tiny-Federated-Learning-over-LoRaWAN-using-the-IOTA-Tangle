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
The name of Fraunhofer may not be used to endorse or promote products derived+
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
 * @brief LoRaWAN uplink with DHT22 temperature sensor
 */

#include <DHT.h>
#include <ArduinoTsUnb.h>
#include "Rfm95w_LoRa.h"
#include "LoRaWANUplinkMAC.h"

// ── Device credentials ── fill in your own values ──────────────────────────
#define JOIN_EUI  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
#define DEV_EUI   0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x06, 0x9B, 0x6C
#define APP_KEY   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
// ────────────────────────────────────────────────────────────────────────────

#define BOOST_PIN   true
#define RFM95_RST   23
#define DHTPIN      14
#define DHTTYPE     DHT22
#define LED_PIN     25
#define SEND_INTERVAL_MS 10000

using namespace TsUnbLib::Arduino;

DHT dht(DHTPIN, DHTTYPE);
LoRaWAN::LoRaWANUplinkMac LoRaMAC;

void setup() {
  delay(100);
  Serial.begin(115200);
  Serial.println("LoRaWAN Device Starting...");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Reset RFM95
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, LOW);
  delay(500);
  digitalWrite(RFM95_RST, HIGH);
  delay(500);

  // Initialise MAC and set credentials
  LoRaMAC.init();
  LoRaMAC.setJoinEUI(JOIN_EUI);
  LoRaMAC.setDevEUI(DEV_EUI);
  LoRaMAC.setappKey(APP_KEY);

  delay(1000);

  // Join network
  bool accepted = LoRaMAC.join();
  while (accepted != true) {
    Serial.println("Join not accepted, trying again...");
    delay(10000);
    accepted = LoRaMAC.join();
  }

  Serial.println("LoRaWAN join successful");

  dht.begin();
  delay(2000);
}

void loop() {
  float temp_f = dht.readTemperature();
  int temp = (int)temp_f;

  Serial.print("Temperature: ");
  Serial.print(temp);
  Serial.println(" C");

  char str[32];
  int len = snprintf(str, sizeof(str), "%d", temp);

  Serial.println("Sending data via LoRaWAN...");

  // Blink LED on send
  digitalWrite(LED_PIN, HIGH);
  LoRaMAC.send_message((uint8_t *)str, len, false, false);
  delay(100);
  digitalWrite(LED_PIN, LOW);

  delay(SEND_INTERVAL_MS);

  while (LoRaMAC.getOptionAnsLength() > 15) {
    delay(2000);
    LoRaMAC.send_message(NULL, 0, false, true, 0);
  }

  while (LoRaMAC.getFPending() == true) {
    delay(2000);
    LoRaMAC.send_message(NULL, 0, false, true);
  }
}
