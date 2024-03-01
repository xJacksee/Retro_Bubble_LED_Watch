//Retro Bubble LED Watch
/*
Wiring:
PD0 - a
PD1 - b
PD2 - c
PD3 - d
PD4 - e
PD5 - f
PD6 - g
PD7 - dp

PB0 - CA1 - position 0
PB1 - CA2 - position 1
PB2 - CA3 - position 2
PB6 - CA4 - position 3
PB7 - CA5 - position 4

PC5 - SCL
PC4 - SDA

PC0 - MODE switch
PC1 - UP switch
*/
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
#define AL1_MIN_ADDR    0x08
#define AL1_HOUR_ADDR   0x09
#define AL1_STATE_ADDR  0x0A
#define AL2_MIN_ADDR    0x0B
#define AL2_HOUR_ADDR   0x0C
#define AL2_STATE_ADDR  0x0D
#define CTRL_ADDR       0x0E
#define STATUS_ADDR     0x0F

//Menu list
enum menu_list {
  READ_TIME,
  READ_DATE,
  READ_YEAR,
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
  WRITE_ALARM1,
  WRITE_ALARM1_STATUS,
  WRITE_ALARM2,
  WRITE_ALARM2_STATUS
};

//Current position for display
uint8_t position = 0;
//Curret menu selected by user
volatile enum menu_list menu = READ_TIME;
//Write/read mode flag
volatile bool write_mode = 0;
//Digit up write value
volatile uint8_t up = 0;
//Current digits array with digit number and dot point status
uint8_t display_array[DIGITS_COUNT][2];
//Position lookup table for PORT manipulation
const uint8_t position_array[DIGITS_COUNT] = {0b00000001, 0b00000010, 0b00000100, 0b01000000, 0b10000000};  //CA1,CA2,CA3,CA4,CA5
//Digit segment configuration for PORT manipulation
const uint8_t digits_array[11] = {0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110, 0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01101111, 0b00000000};  //0,1,2,3,4,5,6,7,8,9,null

//Interrupt service routine for Timer 1 to regularly update display
ISR(TIMER1_COMPA_vect) {
  display_write();
}

//Interrupt service routine for toggling write mode if MODE button was pressed for set time
ISR(TIMER3_COMPA_vect) {
  TCCR3B = 0b00000000;  //Stop Timer 3
  TCNT3 = 0;            //Set Timer 3 value to 0
  write_mode = 1;       //Enable write mode flag
  menu = WRITE_HOUR;    //Go to first write menu
}

//Interrupt service routine for MODE and UP switches
ISR(PCINT1_vect) {
  if (!(PINC & 0b00000001)) { //If MODE button was pressed (detect LOW state)
    TCCR3B = 0b00000011;      //Start Timer 3 with 64 prescaler
    menu = menu + 1;          //Cycle through menus
    if (menu > READ_ALARM2_STATUS & !write_mode) {
      menu = READ_HOUR;
    }
    else if (menu > WRITE_ALARM2_STATUS & write_mode){
      menu = READ_HOUR;
      write_mode = 0;
    }
  }
  else if (!(PINC & 0b00000010) & write_mode) { //If UP button was pressed (detect LOW state)
    up++; //Increment digit
  }
  else if (PINC & 0b00000001){ //If MODE button was depressed (detect HIGH state)
    TCCR3B = 0b00000000;  //Stop Timer 3
    TCNT3 = 0;            //Set Timer 3 value to 0
  }
}

void setup() {
  //Set Timer 1 to interrupt and refresh screen periodacly
  TCCR1A  = 0b00000000; //No PWM functions
  TCCR1B  = 0b00001001; //No prescaler, CTC for OCR1A
  TCNT1   = 0;          //Reset initial value
  TIMSK1 |= 0b00000010; //Set interrupt on compare A
  OCR1A   = 5000;       //Display new digit every 5ms
  
  //Set Timer 3 to count how long MODE button was pressed
  TCCR3A = 0b00000000;  //No PWM functions
  TCCR3B = 0b00000000;  //Keep Timer 3 initially disabled
  TCNT3 = 0;            //Reset initial value
  TIMSK3 |= 0b00000010; //Set interrupt on compare A
  OCR3A = 31250;        //Set 2s write mode trigger time (64 prescaler)
  
  //Set D and B pins as outputs for driving the display
  DDRD =  0b11111111;
  DDRB |= 0b11000111;
  
  //Set C pins as inputs for switches and enable pullup resistors
  DDRC &= 0b11111100;
  PORTC |= 0b00000011;
  
  //Initialize Pin Chnage Interrupts
  PCICR |= 0b00000010; //Enable PCI for bank C
  PCIFR |= 0b00000111; //Clear interrupt flags
  PCMSK1 |= 0b00000011; //Enable PCI for PC0 and PC1
  
  //Initial display values
  display_array[0][0] = 0;
  display_array[1][0] = 0;
  display_array[2][0] = 0;
  display_array[3][0] = 0;
  display_array[4][0] = 0;
  display_array[0][1] = 0;
  display_array[1][1] = 0;
  display_array[2][1] = 0;
  display_array[3][1] = 0;
  display_array[4][1] = 0;
  
  //Initialize I2C communication library
  Wire.begin();
  
  //Enable RTS's 2nd alarm interrupt
  rtc_write(CTRL_ADDR, 0b00000100); 
  
  //Enable external interrupts
  sei();
}

void loop() {
  switch(menu) {
    case READ_TIME:
    //Set dot positions
    display_array[0][1] = 0;
    display_array[1][1] = 1;
    display_array[2][1] = 0;
    display_array[3][1] = 1;
    display_array[4][1] = 0;
    //Read data from RTC
    rtc_read(HOUR_ADDR, 0, 1);
    rtc_read(MIN_ADDR, 2, 3);
    rtc_read(SEC_ADDR, 4, 5);
    break;

    case READ_DATE: //Date
    display_array[0][1] = 1;
    display_array[1][1] = 0;
    display_array[2][1] = 1;
    display_array[3][1] = 0;
    display_array[4][1] = 1;
    rtc_read(DAY_ADDR, 0, 5);
    rtc_read(DATE_ADDR, 1, 2);
    rtc_read(MONTH_ADDR, 3, 4);
    break;

    case READ_YEAR: //Year
    display_array[0][1] = 0;
    display_array[1][1] = 0;
    display_array[2][1] = 0;
    display_array[3][1] = 0;
    display_array[4][1] = 0;
    display_array[0][0] = 10;
    display_array[1][0] = 2;
    display_array[2][0] = 0;
    rtc_read(YEAR_ADDR, 3, 4);
    break;

    case WRITE_HOUR: //Set hour
    display_array[0][1] = 0;
    display_array[1][1] = 1;
    display_array[2][1] = 0;
    display_array[3][1] = 1;
    display_array[4][1] = 0;
    rtc_read(MIN_ADDR, 2, 3);
    rtc_read(SEC_ADDR, 4, 5);
    delay(200);
    display_array[0][0] = 10;
    display_array[1][0] = 10;
    delay(200);
    if (rtc_read(HOUR_ADDR, 0, 1) + up < 24) {
    rtc_write(HOUR_ADDR, up + rtc_read(HOUR_ADDR, 0, 1));
    up = 0;
    }
    else {
      up = 0;
      rtc_write(HOUR_ADDR, up);
    }
    break;

    case WRITE_MINUTE: //Set minute
    display_array[0][1] = 0;
    display_array[1][1] = 1;
    display_array[2][1] = 0;
    display_array[3][1] = 1;
    display_array[4][1] = 0;
    rtc_read(HOUR_ADDR, 0, 1);
    rtc_read(SEC_ADDR, 4, 5);
    delay(200);
    display_array[2][0] = 10;
    display_array[3][0] = 10;
    delay(200);
    if (rtc_read(MIN_ADDR, 2, 3) + up < 60) {
    rtc_write(MIN_ADDR, up + rtc_read(MIN_ADDR, 2, 3));
    }
    else {
      up = 0;
      rtc_write(MIN_ADDR, up);
    }
    break;

    case WRITE_SECOND: //Set second
    display_array[0][1] = 0;
    display_array[1][1] = 1;
    display_array[2][1] = 0;
    display_array[3][1] = 1;
    display_array[4][1] = 0;
    rtc_read(HOUR_ADDR, 0, 1);
    rtc_read(MIN_ADDR, 2, 3);
    delay(200);
    display_array[4][0] = 10;
    delay(200);
    if (up + rtc_read(SEC_ADDR, 4, 5) < 60) {
    rtc_write(SEC_ADDR, up + rtc_read(SEC_ADDR, 4, 5));
    }
    else {
      up = 0;
      rtc_write(SEC_ADDR, up);
    }
    break;

    case WRITE_DAY: //Set day
    display_array[0][1] = 1;
    display_array[1][1] = 0;
    display_array[2][1] = 1;
    display_array[3][1] = 0;
    display_array[4][1] = 1;
    rtc_read(DATE_ADDR, 1, 2);
    rtc_read(MONTH_ADDR, 3, 4);
    delay(200);
    display_array[0][0] = 10;
    delay(200);
    if (up + rtc_read(DAY_ADDR, 0, 5) < 8 & rtc_read(DAY_ADDR, 0, 5) + up > 0) {
    rtc_write(DAY_ADDR, up + rtc_read(DAY_ADDR, 0, 5));
    }
    else {
      up = 1;
      rtc_write(DAY_ADDR, up);
    }
    break;

    case WRITE_DATE: //Set date
    display_array[0][1] = 1;
    display_array[1][1] = 0;
    display_array[2][1] = 1;
    display_array[3][1] = 0;
    display_array[4][1] = 1;
    rtc_read(DAY_ADDR, 0, 5);
    rtc_read(MONTH_ADDR, 3, 4);
    delay(200);
    display_array[1][0] = 10;
    display_array[2][0] = 10;
    delay(200);
    if (rtc_read(DATE_ADDR, 1, 2) + up < 32 & rtc_read(DATE_ADDR, 1, 2) + up > 0) {
    rtc_write(DATE_ADDR, up + rtc_read(DATE_ADDR, 1, 2));
    }
    else {
      up = 1;
      rtc_write(DATE_ADDR, up);
    }
    break;

    case WRITE_MONTH: //Set month
    display_array[0][1] = 1;
    display_array[1][1] = 0;
    display_array[2][1] = 1;
    display_array[3][1] = 0;
    display_array[4][1] = 1;
    rtc_read(DAY_ADDR, 0, 5);
    rtc_read(DATE_ADDR, 1, 2);
    rtc_read(MONTH_ADDR, 3, 4);
    delay(200);
    display_array[3][0] = 10;
    display_array[4][0] = 10;
    delay(200);
    if (rtc_read(MONTH_ADDR, 3, 4) + up < 13 & rtc_read(MONTH_ADDR, 3, 4) + up > 0) {
    rtc_write(MONTH_ADDR, up + rtc_read(MONTH_ADDR, 3, 4));
    }
    else {
      up = 1;
      rtc_write(MONTH_ADDR, up);
    }
    break;

    case WRITE_YEAR: //Set year
    display_array[0][1] = 0;
    display_array[1][1] = 0;
    display_array[2][1] = 0;
    display_array[3][1] = 0;
    display_array[4][1] = 0;
    display_array[1][0] = 2;
    display_array[2][0] = 0;
    delay(200);
    display_array[0][0] = 10;
    display_array[1][0] = 10;
    display_array[2][0] = 10;
    display_array[3][0] = 10;
    display_array[4][0] = 10;
    delay(200);
    if (rtc_read(YEAR_ADDR, 3, 4) + up < 100) {
    rtc_write(YEAR_ADDR, up + rtc_read(YEAR_ADDR, 3, 4));
    }
    else {
      up = 0;
      rtc_write(YEAR_ADDR, up);
    }
    break;

    default:
      display_array[0][0] = 2;
      display_array[1][0] = 1;
      display_array[2][0] = 3;
      display_array[3][0] = 7;
      display_array[4][0] = 10;
      display_array[0][1] = 0;
      display_array[1][1] = 1;
      display_array[2][1] = 0;
      display_array[3][1] = 0;
      display_array[4][1] = 0;
  }

}

//Read data from RTC and insert digits to positions in display array
uint8_t rtc_read(uint8_t address, uint8_t position1, uint8_t position2) {
  uint8_t data_bcd;
  uint8_t data_dec;
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(address);
  Wire.endTransmission();
  Wire.requestFrom(RTC_ADDR,1);
  if (Wire.available()) {
    data_bcd = Wire.read();
  }
  data_dec = (data_bcd / 16 * 10) + (data_bcd % 16);  //Convert RTS's BCD format to decimal
  display_array[position1][0] = data_dec / 10;      //Write most siginificant digit to position 1
  if (position2 < DIGITS_COUNT) {
  display_array[position2][0] = data_dec % 10;      //Write least siginificant digit to position 2
  }
  return data_dec;
}

//Write data to RTC
void rtc_write(uint8_t address, uint8_t value) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(address);
  Wire.write((value / 10*16) + (value % 10));
  Wire.endTransmission();
}

//Display array function
void display_write() {
  if (display_array[position][0] < 11) {
    PORTB &=  0b00111000;                                 //Reset B pins
    PORTD = digits_array[display_array[position][0]];     //Set segment configuration from digits_array look-up table
    PORTD |= (display_array[position][1] << PD7);         //Add dot point bit status to current digit
    PORTB |= position_array[position];                    //Add bit corresponding to position argument from position_array look-up table
    position++;                                           //Move to next digit position
    if (position == DIGITS_COUNT) {                       //Reset digit counter when last position is reached
      position = 0;                                 
    }
  }
}
