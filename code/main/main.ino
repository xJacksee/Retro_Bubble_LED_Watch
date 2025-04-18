/*
***************************
* Retro Bubble LED Watch  *
* Revision: 1.1           *
* Version: 1.2            *
* Date: 15.09.2024        *
* Szymon Bartosik         *
***************************

Description:

Arduino Core: https://github.com/MCUdude/MiniCore
Bootloader: None
BOD: Disabled
Clock: Internal RC clock at 1MHz (factory default)
All other fuse/lock bits are set to factory defalult as stated in datasheet

Wiring:

Segments:
PD0 - a
PD1 - b
PD2 - c
PD3 - d
PD4 - e
PD5 - f
PD6 - g
PD7 - dp

Cathodes:
PB0 - CA1 - position 0
PB1 - CA2 - position 1
PB2 - CA3 - position 2
PB6 - CA4 - position 3
PB7 - CA5 - position 4

RTC I2C communication:
PC5 - SCL
PC4 - SDA

Buttons:
PC0 - MODE switch
PC1 - UP switch

RTC interrupts:
PC2 - RTC alarm interrupt

External components:
PE0 - 20mm 4kHz piezo disc


*/
#include <Arduino.h>
#include <Wire.h> //Library needed for I2C communication with RTC module

//Number of display's digits
#define DIGITS_COUNT    5

//DS1337 RTC register addresses
#define RTC_ADDR        0x68
#define SEC_ADDR        0x00
#define MIN_ADDR        0x01
#define HOUR_ADDR       0x02
#define DAY_ADDR        0x03
#define DATE_ADDR       0x04
#define MONTH_ADDR      0x05
#define YEAR_ADDR       0x06
#define AL1_SEC_ADDR    0x07
#define AL1_MIN_ADDR    0x08
#define AL1_HOUR_ADDR   0x09
#define AL1_DATE_ADDR   0x0A
#define AL2_MIN_ADDR    0x0B
#define AL2_HOUR_ADDR   0x0C
#define AL2_DATE_ADDR   0x0D
#define CTRL_ADDR       0x0E
#define STATUS_ADDR     0x0F

//Temperature sensor calibrated offset at room temperature (has to be calibrated for each chip indivudually; use this value as a reference)
#define T_OFFSET        280

//Function prototypes
int temp_read();
uint8_t rtc_read(uint8_t);
uint8_t rtc_read_dec(uint8_t);
uint8_t read_digit(uint8_t, bool);
bool bit_read(uint8_t, uint8_t);
void rtc_write(uint8_t, uint8_t);
void display_write();

//Menu list
enum menu_list {
  READ_TIME,
  READ_DATE,
  READ_YEAR,
  READ_TEMP,
  READ_ALARM1,
  READ_ALARM1_STATUS,
  READ_ALARM2,
  READ_ALARM2_STATUS,
  WRITE_HOUR,
  WRITE_MINUTE,
  WRITE_SECOND,
  WRITE_DAY,
  WRITE_DATE,
  WRITE_MONTH,
  WRITE_YEAR,
  WRITE_ALARM1_HOUR,
  WRITE_ALARM1_MINUTE,
  WRITE_ALARM1_SECOND,
  WRITE_ALARM1_STATUS,
  WRITE_ALARM2_HOUR,
  WRITE_ALARM2_MINUTE,
  WRITE_ALARM2_STATUS,
  ALARM_TRIGGER
};

//Menu display structure definition
typedef struct Display {
  bool dots[DIGITS_COUNT];
  uint8_t digits[DIGITS_COUNT];
  } display;

//Menu display structures with default (blank) values
display read_time = {{0,1,0,1,0}, {10, 10, 10, 10, 10}};
display read_date = {{1,0,1,0,1}, {10, 10, 10, 10, 10}};
display read_year = {{0,0,0,0,0}, {2, 0, 10, 10, 10}};
display read_alarm1 = {{0,1,0,1,0}, {10, 10, 10, 10, 10}};
display read_alarm1_status = {{0,0,0,0,0}, {10,10,11,13,13}};
display read_alarm2 = {{0,1,0,1,0}, {10, 10, 10, 10, 0}};
display read_alarm2_status = {{0,0,0,0,0}, {10,10,11,13,13}};
display read_temp = {{0,0,0,0,0}, {10,10,10,14,16}};

//Current position for display
uint8_t position = 0;
//Curret menu selected by user
volatile enum menu_list menu = READ_TIME;
//Write/read mode flag
volatile bool write_mode = 0;
//Digit up write value
volatile uint8_t up = 0;
//Blink period (ms)
uint8_t t_blink = 450;
//Sleep mode flag
volatile bool sleep_flag = 0;
//Temperature reading
int T = 0;
//Refresh time value for timer 1
const uint16_t t_ref[] = {1000, 31250};

//Menu structure pointer array
const display* display_ptr[] = {&read_time, &read_date, &read_year, &read_temp, &read_alarm1, &read_alarm1_status, &read_alarm2, &read_alarm2_status, &read_time, &read_time, &read_time, &read_date, &read_date, &read_date, &read_year, &read_alarm1, &read_alarm1, &read_alarm1, &read_alarm1_status, &read_alarm2, &read_alarm2, &read_alarm2_status, &read_time};
//Position lookup table for PORT manipulation
const uint8_t position_array[] = {0b11111110, 0b11111101, 0b11111011, 0b10111111, 0b01111111};  //CA1,CA2,CA3,CA4,CA5
//Digit segment configuration for PORT manipulation
const uint8_t digits_array[] = {0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110, 0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01101111, 0b00000000, 0b01011100, 0b00110111, 0b01110001, 0b01100011, 0b01000000, 0b00111001};  //0,1,2,3,4,5,6,7,8,9,null,o,n,f,deg,-,C

//Interrupt service routine for Timer 1 to regularly update display
ISR(TIMER1_COMPA_vect) {
  if (!sleep_flag) {
    display_write();
  }
}

//Interrupt service routine for toggling write mode if MODE button was pressed for set time
ISR(TIMER3_COMPA_vect) {
  TCCR3B = 0b00000000;  //Stop Timer 3
  TCNT3 = 0;            //Set Timer 3 value to 0
  write_mode = 1;       //Enable write mode flag
  menu = WRITE_HOUR;    //Go to first write menu
}

//Interrupt service routine for MODE and UP switches + alarm
ISR(PCINT1_vect) {
  TCNT4 = 0;                    //Reset sleep timer
  if (sleep_flag) {
    OCR1A = t_ref[0];
    TCCR1B  = 0b00001001; //Enable display refreshing timer
    menu = READ_TIME;     //Let the first menu to display after wake-up to be READ_TIME
  }
  if (!(PINC & 0b00000100)) { //If alarm was triggered
    menu = ALARM_TRIGGER;
  }
  else if (!(PINC & 0b00000001)) { //If MODE button was pressed (detect LOW state)
    TCCR3B = 0b00000011;      //Start Timer 3 with 64 prescaler
    if (!sleep_flag) {
      menu = menu + 1;          //Cycle through menus
    }
    if (menu > READ_ALARM2_STATUS & !write_mode) {
      menu = READ_TIME;
    }
    else if (menu > WRITE_ALARM2_STATUS & write_mode){
      menu = READ_TIME;
      write_mode = 0;
    }
  }
  else if (!(PINC & 0b00000010) & write_mode & (menu != ALARM_TRIGGER)) { //If UP button was pressed (detect LOW state)
    up++; //Increment digit
  }
  else if (!(PINC & 0b00000010) & !write_mode & (menu != ALARM_TRIGGER)) { //If UP button was pressed (detect LOW state)
    TCCR1B  = 0b00001010; //Set prescaler to 8
    OCR1A = t_ref[1];     //Set refresh period to 250ms
  }
  else if (PINC & 0b00000001){ //If MODE button was depressed (detect HIGH state)
    TCCR3B = 0b00000000;  //Stop Timer 3
    TCNT3 = 0;            //Set Timer 3 value to 0
  }
  PORTE = 0b00000000;
  sleep_flag = 0;               //Disable sleep
}

//Interrupt service routine for entering sleep mode
ISR(TIMER4_COMPA_vect) {
  sleep_flag = 1;
}

void setup() {
  //Set sleep mode register to Power-down mode and enable it
  SMCR = 0b00000101;
  
  //Set Timer 1 to interrupt and refresh screen periodacly
  TCCR1A  = 0b00000000; //No PWM functions
  TCNT1   = 0;          //Reset initial value
  TCCR1B  = 0b00001001; //No prescaler, CTC for OCR1A
  TIMSK1  = 0b00000010; //Set interrupt on compare A
  OCR1A   = t_ref[0];   //Display new digit every 1ms
  
  //Set Timer 3 to count how long MODE button was pressed
  TCCR3A = 0b00000000;  //No PWM functions
  TCCR3B = 0b00000000;  //Keep Timer 3 initially disabled
  TCNT3 = 0;            //Reset initial value
  TIMSK3 = 0b00000010;  //Set interrupt on compare A
  OCR3A = 31250;        //Set 2s write mode trigger time (64 prescaler set in ISR)

  //Set Timer 4 to countdown time to enter sleep mode
  TCCR4A = 0b00000000;  //No PWM functions
  TCNT4 = 0;            //Reset initial value
  TCCR4B = 0b00001011;  //Enable Timer 4 with 64 prescaler and CTC for OCR4A
  TIMSK4 = 0b00000010;  //Set interrupt on compare A
  OCR4A = 62500;        //Set 4s time to display before enabling power-down sleep
  
  //Set D and B pins as outputs for driving the display
  DDRD = 0b11111111;
  DDRB = 0b11000111;
  
  //Set C pins as inputs for switches and enable pullup resistors
  DDRC &= 0b11110000;
  PORTC |= 0b00000111;

  //Set pin in E port as an output for buzzer alarm
  DDRE  = 0b00000001;
  PORTE = 0b11111110;

  //Setup ADC to measure internal temperature
  ADMUX   = 0b11001000;  //Internal 1.1V reference and temperature sensor selected
  
  //Initialize Pin Chnage Interrupts
  PCICR |= 0b00000010;  //Enable PCI for bank C
  PCIFR |= 0b00000111;  //Clear interrupt flags
  PCMSK1 |= 0b00000111; //Enable PCI for PC0, PC1 and PC2
  
  //Initialize I2C communication library
  Wire.begin();
  
  //Set alrms to trigger when only time matches
  rtc_write(AL1_DATE_ADDR, 0b10000001); 
  rtc_write(AL2_DATE_ADDR, 0b10000001);

  //Disable alarms by default and set ~INTA as only alarm pin (these are default values of RTC's register but have to be setup this way due to possible random changes after programming and startup)
  rtc_write(CTRL_ADDR, 0b00000000);
  
  //Clear alaram flags
  rtc_write(STATUS_ADDR, 0b00000000);
  
  //Enable external interrupts
  sei();
}

void loop() {
  if (sleep_flag) {
    PORTE = 0b11111110;   //Pull buzzer pin low
    ADCSRA  = 0b00000011; //Disable ADC
    PORTD =  0b00000000;  //Disable all LED segments
    PORTB =  0b00111000;  //Set cathodes to ground
    sleep_mode();         //Enter sleep
  }
  
  switch(menu) {
    
    case READ_TIME:
      read_time.digits[0] = read_digit(HOUR_ADDR,1);
      read_time.digits[1] = read_digit(HOUR_ADDR,0);
      read_time.digits[2] = read_digit(MIN_ADDR,1);
      read_time.digits[3] = read_digit(MIN_ADDR,0);
      read_time.digits[4] = read_digit(SEC_ADDR,1);
    break;
      
    case READ_DATE:
      read_date.digits[0] = read_digit(DAY_ADDR,0);
      read_date.digits[1] = read_digit(DATE_ADDR,1);
      read_date.digits[2] = read_digit(DATE_ADDR,0);
      read_date.digits[3] = read_digit(MONTH_ADDR,1);
      read_date.digits[4] = read_digit(MONTH_ADDR,0);
    break;
      
    case READ_YEAR:
      read_year.digits[2] = read_digit(YEAR_ADDR,1);
      read_year.digits[3] = read_digit(YEAR_ADDR,0);
    break;

    case READ_TEMP:
      T = temp_read();
      if (T < 0)  {
        read_temp.digits[0] = 15; //Write -
      }
      else {
        read_temp.digits[0] = 10;
      }

      read_temp.digits[1] = (abs(T) - abs(T) % 10) / 10;
      read_temp.digits[2] = abs(T) % 10;
    break;
      
    case READ_ALARM1:
      read_alarm1.digits[0] = read_digit(AL1_HOUR_ADDR,1);
      read_alarm1.digits[1] = read_digit(AL1_HOUR_ADDR,0);
      read_alarm1.digits[2] = read_digit(AL1_MIN_ADDR,1);
      read_alarm1.digits[3] = read_digit(AL1_MIN_ADDR,0);
      read_alarm1.digits[4] = read_digit(AL1_SEC_ADDR,1);
    break;

    case READ_ALARM1_STATUS:
      if (read_bit(CTRL_ADDR, 0)) {
        read_alarm1_status.digits[2] = 10;
        read_alarm1_status.digits[3] = 11;
        read_alarm1_status.digits[4] = 12;
      }
      else {
        read_alarm1_status.digits[2] = 11;
        read_alarm1_status.digits[3] = 13;
        read_alarm1_status.digits[4] = 13;
      }
    break;
    
    case READ_ALARM2:
      read_alarm2.digits[0] = read_digit(AL2_HOUR_ADDR,1);
      read_alarm2.digits[1] = read_digit(AL2_HOUR_ADDR,0);
      read_alarm2.digits[2] = read_digit(AL2_MIN_ADDR,1);
      read_alarm2.digits[3] = read_digit(AL2_MIN_ADDR,0);
    break;
      
    case READ_ALARM2_STATUS:
      if (read_bit(CTRL_ADDR, 1)) {
        read_alarm2_status.digits[2] = 10;
        read_alarm2_status.digits[3] = 11;
        read_alarm2_status.digits[4] = 12;
      }
      else {
        read_alarm2_status.digits[2] = 11;
        read_alarm2_status.digits[3] = 13;
        read_alarm2_status.digits[4] = 13;
      }
    break;

    case WRITE_HOUR:
      read_time.digits[2] = read_digit(MIN_ADDR,1);
      read_time.digits[3] = read_digit(MIN_ADDR,0);
      read_time.digits[4] = read_digit(SEC_ADDR,1);
      if (rtc_read_dec(HOUR_ADDR) + up <= 23) {
        rtc_write(HOUR_ADDR,rtc_read_dec(HOUR_ADDR) + up);
        up = 0;
      }
      else {
        rtc_write(HOUR_ADDR, 0);
        up = 0;
      }
      read_time.digits[0] = read_digit(HOUR_ADDR,1);
      read_time.digits[1] = read_digit(HOUR_ADDR,0);
      delay(t_blink);
      read_time.digits[0] = 10;
      read_time.digits[1] = 10;
      delay(t_blink);
    break;

    case WRITE_MINUTE:
      read_time.digits[0] = read_digit(HOUR_ADDR,1);
      read_time.digits[1] = read_digit(HOUR_ADDR,0);
      read_time.digits[4] = read_digit(SEC_ADDR,1);
      if (rtc_read_dec(MIN_ADDR) + up <= 59) {
        rtc_write(MIN_ADDR,rtc_read_dec(MIN_ADDR) + up);
        up = 0;
      }
      else {
        rtc_write(MIN_ADDR, 0);
        up = 0;
      }
      read_time.digits[2] = read_digit(MIN_ADDR,1);
      read_time.digits[3] = read_digit(MIN_ADDR,0);
      delay(t_blink);
      read_time.digits[2] = 10;
      read_time.digits[3] = 10;
      delay(t_blink);
    break;

    case WRITE_SECOND:
      read_time.digits[0] = read_digit(HOUR_ADDR,1);
      read_time.digits[1] = read_digit(HOUR_ADDR,0);
      read_time.digits[2] = read_digit(MIN_ADDR,1);
      read_time.digits[3] = read_digit(MIN_ADDR,0);
      if (rtc_read_dec(SEC_ADDR) + up*10 <= 59) {
        rtc_write(SEC_ADDR,rtc_read_dec(SEC_ADDR) + up*10);
        up = 0;
      }
      else {
        rtc_write(SEC_ADDR, 0);
        up = 0;
      }
      read_time.digits[4] = read_digit(SEC_ADDR,1);
      delay(t_blink);
      read_time.digits[4] = 10;
      delay(t_blink);
    break;

    case WRITE_DAY:
      read_date.digits[1] = read_digit(DATE_ADDR,1);
      read_date.digits[2] = read_digit(DATE_ADDR,0);
      read_date.digits[3] = read_digit(MONTH_ADDR,1);
      read_date.digits[4] = read_digit(MONTH_ADDR,0);
      if (rtc_read_dec(DAY_ADDR) + up <= 7) {
        rtc_write(DAY_ADDR,rtc_read_dec(DAY_ADDR) + up);
        up = 0;
      }
      else {
        rtc_write(DAY_ADDR, 1);
        up = 0;
      }
      read_date.digits[0] = read_digit(DAY_ADDR,0);
      delay(t_blink);
      read_date.digits[0] = 10;
      delay(t_blink);
    break;

    case WRITE_DATE:
      read_date.digits[0] = read_digit(DAY_ADDR,0);
      read_date.digits[3] = read_digit(MONTH_ADDR,1);
      read_date.digits[4] = read_digit(MONTH_ADDR,0);
      if (rtc_read_dec(DATE_ADDR) + up <= 31) {
        rtc_write(DATE_ADDR,rtc_read_dec(DATE_ADDR) + up);
        up = 0;
      }
      else {
        rtc_write(DATE_ADDR, 1);
        up = 0;
      }
      read_date.digits[1] = read_digit(DATE_ADDR,1);
      read_date.digits[2] = read_digit(DATE_ADDR,0);
      delay(t_blink);
      read_date.digits[1] = 10;
      read_date.digits[2] = 10;
      delay(t_blink);
    break;

    case WRITE_MONTH:
      read_date.digits[0] = read_digit(DAY_ADDR,0);
      read_date.digits[1] = read_digit(DATE_ADDR,1);
      read_date.digits[2] = read_digit(DATE_ADDR,0);
      if (rtc_read_dec(MONTH_ADDR) + up <= 12) {
        rtc_write(MONTH_ADDR,rtc_read_dec(MONTH_ADDR) + up);
        up = 0;
      }
      else {
        rtc_write(MONTH_ADDR, 1);
        up = 0;
      }
      read_date.digits[3] = read_digit(MONTH_ADDR,1);
      read_date.digits[4] = read_digit(MONTH_ADDR,0);
      delay(t_blink);
      read_date.digits[3] = 10;
      read_date.digits[4] = 10;
      delay(t_blink);
    break;

    case WRITE_YEAR:
      if (rtc_read_dec(YEAR_ADDR) + up <= 99) {
        rtc_write(YEAR_ADDR,rtc_read_dec(YEAR_ADDR) + up);
        up = 0;
      }
      else {
        rtc_write(YEAR_ADDR, 0);
        up = 0;
      }
      read_year.digits[2] = read_digit(YEAR_ADDR,1);
      read_year.digits[3] = read_digit(YEAR_ADDR,0);
      delay(t_blink);
      read_year.digits[2] = 10;
      read_year.digits[3] = 10;
      delay(t_blink);
    break;

    case WRITE_ALARM1_HOUR:
      read_alarm1.digits[2] = read_digit(AL1_MIN_ADDR,1);
      read_alarm1.digits[3] = read_digit(AL1_MIN_ADDR,0);
      read_alarm1.digits[4] = read_digit(AL1_SEC_ADDR,1);
      if (rtc_read_dec(AL1_HOUR_ADDR) + up <= 23) {
        rtc_write(AL1_HOUR_ADDR,rtc_read_dec(AL1_HOUR_ADDR) + up);
        up = 0;
      }
      else {
        rtc_write(AL1_HOUR_ADDR, 0);
        up = 0;
      }
      read_alarm1.digits[0] = read_digit(AL1_HOUR_ADDR,1);
      read_alarm1.digits[1] = read_digit(AL1_HOUR_ADDR,0);
      delay(t_blink);
      read_alarm1.digits[0] = 10;
      read_alarm1.digits[1] = 10;
      delay(t_blink);
    break;

    case WRITE_ALARM1_MINUTE:
      read_alarm1.digits[0] = read_digit(AL1_HOUR_ADDR,1);
      read_alarm1.digits[1] = read_digit(AL1_HOUR_ADDR,0);
      read_alarm1.digits[4] = read_digit(AL1_SEC_ADDR,1);
      if (rtc_read_dec(AL1_MIN_ADDR) + up <= 59) {
        rtc_write(AL1_MIN_ADDR,rtc_read_dec(AL1_MIN_ADDR) + up);
        up = 0;
      }
      else {
        rtc_write(AL1_MIN_ADDR, 0);
        up = 0;
      }
      read_alarm1.digits[2] = read_digit(AL1_MIN_ADDR,1);
      read_alarm1.digits[3] = read_digit(AL1_MIN_ADDR,0);
      delay(t_blink);
      read_alarm1.digits[2] = 10;
      read_alarm1.digits[3] = 10;
      delay(t_blink);
    break;

    case WRITE_ALARM1_SECOND:
      read_alarm1.digits[0] = read_digit(AL1_HOUR_ADDR,1);
      read_alarm1.digits[1] = read_digit(AL1_HOUR_ADDR,0);
      read_alarm1.digits[2] = read_digit(AL1_MIN_ADDR,1);
      read_alarm1.digits[3] = read_digit(AL1_MIN_ADDR,0);
      if (rtc_read_dec(AL1_SEC_ADDR) + up*10 <= 59) {
        rtc_write(AL1_SEC_ADDR,rtc_read_dec(AL1_SEC_ADDR) + up*10);
        up = 0;
      }
      else {
        rtc_write(AL1_SEC_ADDR, 0);
        up = 0;
      }
      read_alarm1.digits[4] = read_digit(AL1_SEC_ADDR,1);
      delay(t_blink);
      read_alarm1.digits[4] = 10;
      delay(t_blink);
    break;

    case WRITE_ALARM1_STATUS:
      rtc_write(STATUS_ADDR, 0b00000000);
      if (up > 0) {
        rtc_write(CTRL_ADDR, rtc_read(CTRL_ADDR) ^ 0b00000001);
        up = 0;
      }
      if (read_bit(CTRL_ADDR, 0)) {
        read_alarm1_status.digits[2] = 10;
        read_alarm1_status.digits[3] = 11;
        read_alarm1_status.digits[4] = 12;
      }
      else {
        read_alarm1_status.digits[2] = 11;
        read_alarm1_status.digits[3] = 13;
        read_alarm1_status.digits[4] = 13;
      }
      delay(t_blink);
      read_alarm1_status.digits[2] = 10;
      read_alarm1_status.digits[3] = 10;
      read_alarm1_status.digits[4] = 10;
      delay(t_blink);
    break;

    case WRITE_ALARM2_HOUR:
      read_alarm2.digits[2] = read_digit(AL2_MIN_ADDR,1);
      read_alarm2.digits[3] = read_digit(AL2_MIN_ADDR,0);
      if (rtc_read_dec(AL2_HOUR_ADDR) + up <= 23) {
        rtc_write(AL2_HOUR_ADDR,rtc_read_dec(AL2_HOUR_ADDR) + up);
        up = 0;
      }
      else {
        rtc_write(AL2_HOUR_ADDR, 0);
        up = 0;
      }
      read_alarm2.digits[0] = read_digit(AL2_HOUR_ADDR,1);
      read_alarm2.digits[1] = read_digit(AL2_HOUR_ADDR,0);
      delay(t_blink);
      read_alarm2.digits[0] = 10;
      read_alarm2.digits[1] = 10;
      delay(t_blink);
    break;

    case WRITE_ALARM2_MINUTE:
      read_alarm2.digits[0] = read_digit(AL2_HOUR_ADDR,1);
      read_alarm2.digits[1] = read_digit(AL2_HOUR_ADDR,0);
      if (rtc_read_dec(AL2_MIN_ADDR) + up <= 59) {
        rtc_write(AL2_MIN_ADDR,rtc_read_dec(AL2_MIN_ADDR) + up);
        up = 0;
      }
      else {
        rtc_write(AL2_MIN_ADDR, 0);
        up = 0;
      }
      read_alarm2.digits[2] = read_digit(AL2_MIN_ADDR,1);
      read_alarm2.digits[3] = read_digit(AL2_MIN_ADDR,0);
      delay(t_blink);
      read_alarm2.digits[2] = 10;
      read_alarm2.digits[3] = 10;
      delay(t_blink);
    break;

    case WRITE_ALARM2_STATUS:
      rtc_write(STATUS_ADDR, 0b00000000);
      if (up > 0) {
        rtc_write(CTRL_ADDR, rtc_read(CTRL_ADDR) ^ 0b00000010);
        up = 0;
      }
      if (read_bit(CTRL_ADDR, 1)) {
        read_alarm2_status.digits[2] = 10;
        read_alarm2_status.digits[3] = 11;
        read_alarm2_status.digits[4] = 12;
      }
      else {
        read_alarm2_status.digits[2] = 11;
        read_alarm2_status.digits[3] = 13;
        read_alarm2_status.digits[4] = 13;
      }
      delay(t_blink);
      read_alarm2_status.digits[2] = 10;
      read_alarm2_status.digits[3] = 10;
      read_alarm2_status.digits[4] = 10;
      delay(t_blink);
    break;

    case ALARM_TRIGGER:
      rtc_write(STATUS_ADDR, 0b00000000); //Reset alarm flag in RTC
      unsigned int i = 0; //Alarm trigger blink repetitions
      while (i < 30) {
        read_time.digits[0] = read_digit(HOUR_ADDR,1);
        read_time.digits[1] = read_digit(HOUR_ADDR,0);
        read_time.digits[2] = read_digit(MIN_ADDR,1);
        read_time.digits[3] = read_digit(MIN_ADDR,0);
        read_time.digits[4] = read_digit(SEC_ADDR,1);
        PORTE = 0b11111110;
        delay(t_blink); 
        TCNT4 = 0;
        read_time.digits[0] = 10;
        read_time.digits[1] = 10;
        read_time.digits[2] = 10;
        read_time.digits[3] = 10;
        read_time.digits[4] = 10; 
        PORTE = 0b11111111;  
        delay(t_blink); 
        i++;  
      }
      PORTE = 0b11111110;
      sleep_flag = 1;
    break;
  }
}

//Read chip temperature
int temp_read() {
  ADCSRA = 0b11000011;          //Enable ADC, start conversion, disable auto trigger and interrupts disabled, set prescaler to 2
  while (ADCSRA & 0b01000000);  //Wait for conversion to complete
  unsigned int temp_raw = ADCL;
  temp_raw |= (ADCH<<8);
  int temp_real = temp_raw - T_OFFSET;
  if ((temp_real > -100) && (temp_real < 100)) {
    return temp_real;
  }
  else {
    return 0;
  }
}

//Read data from RTC - BCD format
uint8_t rtc_read(uint8_t address) {
  uint8_t data_bcd;
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(address);
  Wire.endTransmission();
  Wire.requestFrom(RTC_ADDR,1);
  if (Wire.available()) {
    data_bcd = Wire.read();
  }
  return data_bcd;
}

//Read data from RTC - DEC format
uint8_t rtc_read_dec(uint8_t address) {
  uint8_t data_bcd = rtc_read(address);
  uint8_t data_dec = (data_bcd / 16 * 10) + (data_bcd % 16);
  return data_dec;
}

//Return single digit from byte recieved from RTC
uint8_t read_digit(uint8_t address, bool byte_half) {
  uint8_t digit = 0;
  if (!byte_half) {
    digit = rtc_read(address) & 0b00001111;
  }
  else {
    digit = (rtc_read(address) >> 4) & 0b00000111;
  }
  if (digit < 10) {
    return digit;
  }
  else return 0;
}

//Read single bit from RTC's register
bool read_bit(uint8_t address, uint8_t bit_position) {
  bool flag_bit = (rtc_read(address) >> bit_position) & 0x01;
  return flag_bit;
}

//Write data to RTC
void rtc_write(uint8_t address, uint8_t value) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(address);
  Wire.write((value/10*16) + (value%10)); //Convert decimal to BCD
  Wire.endTransmission();
}

//Display array function
void display_write() {
    PORTB =  0b11111111;                                        //Reset B pins (reverse bias)
    PORTD = digits_array[display_ptr[menu]->digits[position]];  //Set segment configuration from digits_array look-up table
    PORTD |= (display_ptr[menu]->dots[position] << PD7);        //Add dot point bit status to current digit
    PORTB = position_array[position];                           //Add bit corresponding to position argument from position_array look-up table
    position++;                                                 //Move to next digit position
    if (position > DIGITS_COUNT - 1) {                          //Reset digit counter when last position is reached
      position = 0;                                 
    }
}
