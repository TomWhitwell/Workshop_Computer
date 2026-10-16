// set the MUX logic pins - reading channels 0,1,2,3 = 00 01 10 11
void muxUpdate(int num)
{
  digitalWrite(MX_A, ((num >> 0)) & 1);
  digitalWrite(MX_B, ((num >> 1)) & 1);
  // NB this seems to need 1us delay for pins to 'settle' before reading.
}

// Read the Mux IO pins into the value store
void muxRead(int num)
{
  switch (num)
  {

    case 0:
      // read Main Knob
      analog[4]->update(analogRead(A2));
      // Read CV 1
      analog[2]->update(maxRange - analogRead(A3)); // inverted
      break;

    case 1:
      // read Knob X
      analog[5]->update(analogRead(A2));
      // Read CV 2
      analog[3]->update(maxRange - analogRead(A3)); // inverted
      break;

    case 2:
      // read Knob Y
      analog[6]->update(analogRead(A2));
      // Read CV 1
      analog[2]->update(maxRange - analogRead(A3)); // inverted

      break;

    case 3:
      // read Switch
      analog[7]->update(analogRead(A2));
      // Read CV 2
      analog[3]->update(maxRange - analogRead(A3)); // inverted

      break;
  }
}


// Write to the SPI DAC = the two 'CV/Audio' channels
void dacWrite(int channel, int value) {
  uint16_t DAC_data = (channel == 0) ? (DAC_config_chan_A_nogain | (value & 0xFFF)) : (DAC_config_chan_B_nogain | (value & 0xFFF));
  uint8_t highByte = DAC_data >> 8;
  uint8_t lowByte = DAC_data & 0xFF;

  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0)); // Start SPI transaction
  digitalWrite(DAC_CS, LOW); // Select the SPI device

  SPI.transfer(highByte); // Send first byte
  SPI.transfer(lowByte); // Send second byte

  digitalWrite(DAC_CS, HIGH); // Deselect the SPI device
  SPI.endTransaction(); // End SPI transaction
}





void SetupComputerIO() {

  analogReadResolution(adcResolutionBits);


  // Initialize all LEDs
  for (int i = 0; i < NUM_LEDS; i++) {
    pinMode(leds[i], OUTPUT);
  }

  // Setup SPI for DAC output
  SPI.begin();
  pinMode(DAC_CS, OUTPUT);
  digitalWrite(DAC_CS, HIGH); // Set CS high initially


  pinMode(MX_A, OUTPUT);
  pinMode(MX_B, OUTPUT);

  pinMode(PULSE_1_INPUT, INPUT_PULLUP);
  pinMode(PULSE_2_INPUT, INPUT_PULLUP);
  pinMode(PULSE_1_RAW_OUT, OUTPUT);
  pinMode(PULSE_2_RAW_OUT, OUTPUT);


  for (int i = 0; i < channelCount; i++) {
    analog[i] = new ResponsiveAnalogRead(0, true, .1); //  lower is smoother
    analog[i]->setActivityThreshold(16); // sleep threshold
    analog[i]->setAnalogResolution(1 << adcResolutionBits);
  }


  // Set up Eeprom
  Wire.setSDA(16);
  Wire.setSCL(17);
  Wire.begin();
  Wire.setClock(400000);



}


void ledAnimate() {
  static uint8_t ledIndex = 0;

  // Turn off all LEDs
  for (int i = 0; i < NUM_LEDS; i++) {
    digitalWrite(leds[i], LOW);
  }

  // Turn on the current LED
  digitalWrite(leds[ledIndex], HIGH);

  // Move to the next LED
  ledIndex = (ledIndex + 1) % NUM_LEDS;

  delay(50); // Adjust delay for animation speed
}


void ledAnimate2() {
  static uint8_t ledIndex = 0;
  int ledPath[NUM_LEDS] = {0, 1, 3, 5, 4, 2};

  digitalWrite(leds[ledPath[ledIndex]], 1);
  delay(25);
  digitalWrite(leds[ledPath[ledIndex]], 0);

  ledIndex = (ledIndex + 1) % NUM_LEDS;

  delay(25); // Adjust delay for animation speed
}


void ledOff() {
  for (int i = 0; i < NUM_LEDS; i++) {
    digitalWrite(leds[i], LOW);
  }
}
