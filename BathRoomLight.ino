/*
	WARNING - нужно добавить в IRremoteTools.h строчку:
		#define TKD2 2
	где 2 - номер пина, куда воткнут IR приемник
*/

#include <IRremote.h>
#include <IRremoteInt.h>
#include <IRremoteTools.h>

#include "remoteCodes.h"

#define	LED	5
#define	IR	2

void setup()
{
	digitalWrite(LED, 0);
	pinMode(LED, OUTPUT);

	Serial.begin(115200);
	beginIRremote();
	digitalWrite(IR, HIGH);
	irparams.blinkflag = true;
	cli();

	// Библиотека IRremote на ардуино нано 328 юзает таймер 2, канал OC2A - мы будем юзать канал OC2B
	// Библиотека IRremote настроило прерывание вызываться 1 раз в 0.5 микросекунд - каунтер установим в 100 - будем вызываться раз в 50 мксек
	OCR2B = 100;
	// Разрешить прерывания таймера 2 по каналу OC2B
	TIMSK2 |= _BV(OCIE2B);

	sei();
	Serial.println(TCCR2B, HEX);
}

volatile unsigned int timerCounter = 0;
volatile unsigned int timerCounterOneForSecond = 0;
volatile unsigned int timerCounterOneForHalfSecond = 0;
volatile unsigned long uptime = 0;
volatile long prevIr = 0;
volatile unsigned int ledOn = false;
volatile unsigned int ledLevel = 0;

// Вызывается 1 раз в 0.00005 сек
ISR(TIMER2_COMPB_vect) {

	timerCounter++;

	// Если прошло 0.01 секунды
	if (timerCounter == 200) {
		timerCounter = 0;
		timerCounterOneForSecond++;
		if (timerCounterOneForHalfSecond)
			timerCounterOneForHalfSecond++;

		if (ledOn && ledLevel != 255) {
			ledLevel++;
			analogWrite(LED, ledLevel);
		}
		if (!ledOn && ledLevel != 0) {
			ledLevel--;
			analogWrite(LED, ledLevel);
		}
	}

	// Если прошло пол секунды
	if (timerCounterOneForHalfSecond == 50) {
		timerCounterOneForHalfSecond = 0;
		prevIr = 0;
	}

	// Если прошла секунда
	if (timerCounterOneForSecond == 100) {
		timerCounterOneForSecond = 0;
		uptime++;
	}
}

unsigned long isIrReceived() {

	unsigned long ir;

	if (IRrecived()) {
		ir = getIRresult();
		resumeIRremote();
		if (ir != prevIr) {
			prevIr = ir;
			timerCounterOneForHalfSecond = 1;
			return ir;
		}
	}

	return 0;
}

unsigned long tmp = 0;
unsigned long irIn;

void loop()
{
	if (irIn = isIrReceived()) {
		Serial.println(irIn, HEX);
		if (irIn == LIGTH_HIGH || irIn == LIGTH_LOW)
			Serial.println(analogRead(A0));

		if (irIn == POWER) {
			ledOn = !ledOn;
		}
	}

	if (tmp != uptime) {
		//Serial.println(uptime);
		tmp = uptime;
	}
}
