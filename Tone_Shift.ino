#include <Audio.h>
#include "mod-delay-gain.h"
#include "NewEncoder.h"

void configureShift(void), setOutputMix(void);

const int8_t minToneShift = -50;
const int8_t maxToneShift = 50;
const uint8_t shiftEncoderPinA = 2;
const uint8_t shiftEncoderPinB = 3;

const int8_t minOutputMix = 0;
const int8_t maxOutputMix = 25;
const uint8_t mixEncoderPinA = 4;
const uint8_t mixEncoderPinB = 5;

const uint16_t overlap = 16;  // Overlap between complementry delay ramps
const uint16_t rampLength = 128 + overlap;
const uint16_t waveformSize = 256;
const int32_t delayBufferLength = 4500;

int16_t rampDown[waveformSize], rampUp[waveformSize], window[waveformSize];
int16_t delayBuf1[delayBufferLength];
int16_t delayBuf2[delayBufferLength];
int16_t *currentRamp = rampDown;
float modFreq = 0.0, outputMix = 0.0;

NewEncoder toneShiftEncoder(shiftEncoderPinA, shiftEncoderPinB, minToneShift, maxToneShift, 0);
NewEncoder mixingEncoder(mixEncoderPinA, mixEncoderPinB, minOutputMix, maxOutputMix, 0);

AudioInputI2S i2sIn;           //xy=82,429
AudioMixer4 inputGain;         //xy=282,475

AudioSynthWaveform delaySweep1;    //xy=261,323
AudioSynthWaveform envelope1;      //xy=268,375
AudioSynthWaveform delaySweep2;    //xy=289,539
AudioSynthWaveform envelope2;      //xy=301,590

AudioEffectModDelayGain modulatedDelay1; //xy=511,329
AudioEffectModDelayGain modulatedDelay2; //xy=515,547

AudioMixer4 outputMixer;    //xy=752,420
AudioOutputI2S i2sOut;           //xy=973,422
AudioAnalyzeRMS outputLevel;       //xy=989,475

AudioConnection patchCord1(i2sIn, 0, inputGain, 0);
AudioConnection patchCord2(inputGain, 0, outputMixer, 2);

AudioConnection patchCord5(inputGain, 0, modulatedDelay1, 0);
AudioConnection patchCord3(delaySweep1, 0, modulatedDelay1, 1);
AudioConnection patchCord4(envelope1, 0, modulatedDelay1, 2);

AudioConnection patchCord6(inputGain, 0, modulatedDelay2, 0);
AudioConnection patchCord7(delaySweep2, 0, modulatedDelay2, 1);
AudioConnection patchCord8(envelope2, 0, modulatedDelay2, 2);

AudioConnection patchCord9(modulatedDelay1, 0, outputMixer, 0);
AudioConnection patchCord10(modulatedDelay2, 0, outputMixer, 1);

AudioConnection patchCord11(outputMixer, 0, i2sOut, 0);
AudioConnection patchCord12(outputMixer, 0, i2sOut, 1);
AudioConnection patchCord13(outputMixer, outputLevel);

void setup() {
	double decrement, increment, delayValue, windowValue;
	uint16_t index1, temp1;

	Serial.begin(115200);
	delay(2000);
	Serial.println("Starting");

	if (!toneShiftEncoder.begin()) {
		Serial.println(
				F("Tone Shift Encoder failed to initialize. Check pin assignments and available interrupts. Aborting"));
		while (1) {
		}
	}

	if (!mixingEncoder.begin()) {
		Serial.println(
				F("Mixing Encoder failed to initialize. Check pin assignments and available interrupts. Aborting"));
		while (1) {
		}
	}

	AudioNoInterrupts();
	AudioMemory(20);

	// create the arrays to ramp delay value
	decrement = (double) 32767.0 / (double) (rampLength - 1);
	delayValue = 32767.0;
	for (index1 = 0; index1 < rampLength; index1++) {
		rampDown[index1] = (delayValue + 0.5 > 0) ? delayValue + 0.5 : 0;
		rampUp[index1] = 32767 - rampDown[index1];
		delayValue -= decrement;
	}
	//for (; index1 < 256; index1++) {
	for (; index1 < waveformSize; index1++) {
		rampDown[index1] = 0;
		rampUp[index1] = 32767;
	}

	// create the amplitude window array
	increment = 1.0 / (double) (overlap - 1);
	windowValue = 0.0;
	//for (index1 = 127; index1 < 256; index1++) {
	for (index1 = 0; index1 < waveformSize; index1++) {
		window[index1] = 0;
	}
	for (index1 = 0; index1 < overlap; index1++) {
		temp1 = sqrt(windowValue) * 32767 + 0.5;
		temp1 = (temp1 < 32768) ? temp1 : 32767;
		window[index1] = temp1;
		//window[127 + overlap - index1] = temp1;
		window[waveformSize / 2 - 1 + overlap - index1] = temp1;
		windowValue += increment;
	}
	//for (; index1 <= 127; index1++) {
	for (; index1 < waveformSize / 2; index1++) {
		window[index1] = 32767;
	}

	modulatedDelay1.setbuf(delayBufferLength, delayBuf1);
	modulatedDelay2.setbuf(delayBufferLength, delayBuf2);

	//outputMixer.gain(0, 1.0);
	//outputMixer.gain(1, 1.0);
	//outputMixer.gain(2, 0.0);
	outputMixer.gain(3, 0.0);
	setOutputMix();

	inputGain.gain(0, 5.0);
	inputGain.gain(1, 0.0);
	inputGain.gain(2, 0.0);
	inputGain.gain(3, 0.0);

	configureShift();
}

void loop() {
	static elapsedMillis fps;
	static int16_t lastToneShiftDecoderPosition = -5000;
	static int16_t lastMixDecoderPosition = -5000;
	int16_t currentDecoderPosition;

	if (fps > 24) {
		if (outputLevel.available()) {
			fps = 0;
			int monoPeak = outputLevel.read() * 30.0;
			Serial.print("|");
			for (int cnt = 0; cnt < monoPeak; cnt++) {
				Serial.print(">");
			}
			Serial.println();
		}
	}

	currentDecoderPosition = toneShiftEncoder;
	if (currentDecoderPosition != lastToneShiftDecoderPosition) {
		lastToneShiftDecoderPosition = currentDecoderPosition;
		Serial.print("New Tone Shift Position = ");
		Serial.println(currentDecoderPosition);
		if (currentDecoderPosition >= 0) {
			modFreq = 5.0 * currentDecoderPosition / maxToneShift;
			currentRamp = rampDown;
		} else {
			modFreq = 2.5 * currentDecoderPosition / minToneShift;
			currentRamp = rampUp;
		}
		configureShift();
	}

	currentDecoderPosition = mixingEncoder;
	if (currentDecoderPosition != lastMixDecoderPosition) {
		lastMixDecoderPosition = currentDecoderPosition;
		Serial.print("New Mix Position = ");
		Serial.print(currentDecoderPosition);
		outputMix = (float) currentDecoderPosition / (maxOutputMix - minOutputMix) + minOutputMix;
		Serial.print("  ----> ");
		Serial.println(outputMix);
		setOutputMix();
	}
}

void configureShift() {
	AudioNoInterrupts();

	delaySweep1.begin(1, modFreq, WAVEFORM_ARBITRARY);
	delaySweep1.arbitraryWaveform(currentRamp, 100);
	delaySweep2.begin(1, modFreq, WAVEFORM_ARBITRARY);
	delaySweep2.arbitraryWaveform(currentRamp, 100);
	delaySweep2.phase(180.0);

	envelope1.begin(1, modFreq, WAVEFORM_ARBITRARY);
	envelope1.arbitraryWaveform(window, 100);
	envelope2.begin(1, modFreq, WAVEFORM_ARBITRARY);
	envelope2.arbitraryWaveform(window, 100);
	envelope2.phase(180.0);

	AudioInterrupts();
}

void setOutputMix() {
	outputMixer.gain(0, (1.0 - outputMix) / 2.0);
	outputMixer.gain(1, (1.0 - outputMix) / 2.0);
	outputMixer.gain(2, outputMix);
}
