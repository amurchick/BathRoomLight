#include <math.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>

/*
	WARNING - нужно добавить в IRremoteTools.h строчку:
		#define TKD2 2
	где 2 - номер пина, куда воткнут IR приемник
*/

#include <IRremote.h>
#include <IRremoteInt.h>
#include <IRremoteTools.h>

#include "remoteCodes.h"

// Выход на светодиодную ленту
#define	LED_LIGHT	5
// Вход ИК приемника
#define	IR	2
// Вход ПИР-сенсора
#define PIR 3
// Выход на светодиод индикации состояния ПИР-сенсора
#define	LED_PIR		4
// Вход сенсора света
#define LIGHT_SENSOR A0

volatile uint16_t timerCounter = 0;
volatile uint16_t timerCounterOneForSecond = 0;
volatile uint16_t timerCounterOneForHalfSecond = 0;
volatile uint32_t uptime = 0;
volatile uint32_t prevIr = 0;
volatile uint8_t ledOn = false;
volatile uint16_t ledLevel = 0;
volatile uint8_t pir_state = 0;
// Сколько секунд осталось до отключения
volatile uint16_t secsToOff = 0;

uint16_t ligth_low = 0;
uint16_t ligth_high = 0;
uint16_t ligth_threshold = 0;
uint16_t idle_time = 0;


#define STEPS 511

// Возможные состояния
typedef enum  {S_OFF, S_ON, S_GO_TO_OFF} states_t;
states_t curr_state = S_OFF;

// Таблица функций, которые вызываются в каждом состоянии
void	f_off();
void	f_on();
void	f_go_to_off();
void (*state_funcs[])() = {f_off, f_on, f_go_to_off};

// считалка для функций задержки
volatile  uint16_t	count_down = 0;

// Функция задержки, не менее 1 мс и не более 214748 с
void	delay_ms(uint16_t ms)
{
	if (ms < 1)			ms = 1;
	if (ms > 214748364)	ms = 214748364;

	cli();
	count_down = ms * 20;
	sei();
	while (count_down)
		sleep_mode();
}


// Вызывается 1 раз в 0.00005 сек
ISR(TIMER2_COMPB_vect) {

	timerCounter++;

	// Для функции delay
	if (count_down > 0)
		count_down--;

	// Если прошло 0.01 секунды
	if (timerCounter == 200) {
		timerCounter = 0;
		timerCounterOneForSecond++;
		if (timerCounterOneForHalfSecond)
			timerCounterOneForHalfSecond++;

		// Если прошло пол секунды
		if (timerCounterOneForHalfSecond == 50) {
			timerCounterOneForHalfSecond = 0;
			prevIr = 0;
		}

		// Если прошла секунда
		if (timerCounterOneForSecond == 100) {
			timerCounterOneForSecond = 0;
			uptime++;

			if (secsToOff) {
				secsToOff--;
				// Если нужно отключиться
				if (secsToOff == 0)
					curr_state = S_GO_TO_OFF;
			}
		}

		// Если прошло 0.02 секунды
		if (timerCounterOneForSecond % 2 == 0) {
			if (ledOn && ledLevel != STEPS) {
				analogWrite(LED_LIGHT, ledLevel >> 1);
				ledLevel += ledLevel < 41 ? 1 : 2;
				if (ledLevel == STEPS)
					digitalWrite(LED_LIGHT, HIGH);
			}
			if (!ledOn && ledLevel != 0) {
				ledLevel -= ledLevel < 41 ? 1 : 2;
				analogWrite(LED_LIGHT, ledLevel >> 1);
				if (!ledLevel)
					digitalWrite(LED_LIGHT, LOW);
			}
		}
	} 
}

ISR (INT1_vect)
{
	pir_state = digitalRead(PIR);
	digitalWrite(LED_PIR, pir_state);
}

void setup()
{
	digitalWrite(LED_LIGHT, LOW);
	pinMode(LED_LIGHT, OUTPUT);

	digitalWrite(LED_PIR, LOW);
	pinMode(LED_PIR, OUTPUT);

	digitalWrite(PIR, LOW);
	pinMode(PIR, INPUT);

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

	// Настроим прерывания INT1 по изменению ножки D3 (как с 0 до 1, так и с 1 до 0)
	EICRA |= _BV(ISC10);
	// Разрешим такие прерывания
	EIMSK |= _BV(INT1);

	sei();

	// Прочитаем значения датчика света для включенного и выключенного света в ванной
	ligth_low = eeprom_read_word((uint16_t *)0);
	ligth_high = eeprom_read_word((uint16_t *)sizeof(ligth_low));
	ligth_threshold = ligth_high - (ligth_high - ligth_low)/2;
	idle_time = eeprom_read_word((uint16_t *)(sizeof(ligth_low)*2));
}

uint32_t irIn;

uint32_t isIrReceived() {

	uint32_t ir;

	if (IRrecived()) {
		ir = getIRresult();
		resumeIRremote();
		if (ir != prevIr) {
			irIn = prevIr = ir;
			timerCounterOneForHalfSecond = 1;
			return ir;
		}
	}

	return 0;
}

void loop()
{
	state_funcs[curr_state]();
	delay_ms(150);
}

void saveLightLevel() {
	ligth_threshold = ligth_high - (ligth_high - ligth_low)/2;
	eeprom_write_word((uint16_t *)0, ligth_low);
	eeprom_write_word((uint16_t *)sizeof(ligth_low), ligth_high);
}

void saveIdleTime() {
	eeprom_write_word((uint16_t *)(sizeof(ligth_low)*2), idle_time);
	secsToOff = idle_time;
}

void processIrInput() {
	if (!isIrReceived())
		return;

	switch (irIn) {
	case POWER:
		// Если выключено - включить
		if (curr_state == S_OFF)
			curr_state = S_ON;
		
		// Если включено - выключить через 1 секунду
		if (curr_state == S_ON)
			secsToOff = 1;

		break;

	case LIGTH_LOW:
		ligth_low = analogRead(LIGHT_SENSOR);
		saveLightLevel();
		break;

	case LIGTH_HIGH:
		ligth_high = analogRead(LIGHT_SENSOR);
		saveLightLevel();
		break;

	case DIG0:
		idle_time = 10*60; saveIdleTime(); break;

	case DIG1:
		idle_time = 1*60; saveIdleTime(); break;

	case DIG2:
		idle_time = 2*60; saveIdleTime(); break;

	case DIG3:
		idle_time = 3*60; saveIdleTime(); break;

	case DIG4:
		idle_time = 4*60; saveIdleTime(); break;

	case DIG5:
		idle_time = 5*60; saveIdleTime(); break;

	case DIG6:
		idle_time = 6*60; saveIdleTime(); break;

	case DIG7:
		idle_time = 7*60; saveIdleTime(); break;

	case DIG8:
		idle_time = 8*60; saveIdleTime(); break;

	case DIG9:
		idle_time = 9*60; saveIdleTime(); break;

	default:
		Serial.println(irIn, HEX);
		break;
	}
}

void f_off() {

	// Проверим на нажатия кнопок пульта
	processIrInput();

	// Проверим - включился ли свет
	if (analogRead(LIGHT_SENSOR) > ligth_threshold)
		curr_state = S_ON;

	// Проверим состояние ПИР-сенсора
	if (pir_state)
		curr_state = S_ON;

	// Если нужно включиться - запускаем счетчик
	if (curr_state == S_ON)
		secsToOff = idle_time;

}

void f_on() {
	// Проверим на нажатия кнопок пульта
	processIrInput();

	// Включим освещение
	ledOn = true;

	// Проверим состояние ПИР-сенсора - если есть движение - запусчим отсчет заново
	if (pir_state)
		secsToOff = idle_time;

}

void f_go_to_off() {

	// Выключим освещение
	ledOn = false;

	// Подождем 10 секунд
	delay_ms(10*1000);

	// Установим начальный статус
	curr_state = S_OFF;

}
