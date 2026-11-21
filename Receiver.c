/*
 * File:   Receiver.c
 * Author: yuxia
 *
 * Created on June 28, 2025, 5:19 PM
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>

#define F_CPU 16000000UL

#define RX_BUFFER_SIZE 4
volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
volatile uint8_t rx_index = 0;
volatile uint8_t packet_ready = 0;
volatile uint16_t servo_pulse = 1500;  // In microseconds (default center)
volatile uint8_t pulse_phase = 0;      // 0 = HIGH phase, 1 = LOW phase
volatile uint16_t CCMP_On = 0;
volatile uint16_t CCMP_Off = 0;

/* --------- USART0 Init (only 1 line fixed) --------- */
void USART_Init(void) {
    USART0.BAUD = 6666;  // 9600 baud at 16MHz
    USART0.CTRLC = USART_CMODE_ASYNCHRONOUS_gc |
                   USART_PMODE_DISABLED_gc |
                   USART_CHSIZE_8BIT_gc;

    PORTA.DIRSET = PIN0_bm;  // TX (output)
    PORTA.DIRCLR = PIN1_bm;  // RX (input)

    USART0.CTRLB = USART_RXEN_bm;      // Enable receiver
    USART0.CTRLA = USART_RXCIE_bm;     // Enable RX Complete interrupt

    sei();  // Enable global interrupts
}
/*----------------PWM (Servo)----------------*/
void servo_Init(void){
    TCB1.CCMP = 20000;                         // Sets frequency to 50 Hz
    TCB1.CTRLA = 0b00000101;                   // Sets clock ticks to TCA0 and enables peripheral
    TCB1.CTRLB = TCB_CNTMODE_INT_gc;           // Periodic Interrupt is selected
    TCB1.INTCTRL = TCB_CAPT_bm;                // Interrupt is enabled
    sei();
}

/*-------Servo Calculations for Bit_Banging------*/
void servo_Calculations(uint8_t timing){
    double pulse = (double)timing;
    CCMP_On = (1.5+(64.0-pulse)/64.0)*1000.0;
    CCMP_Off = (18.5-((64.0-pulse)/64.0))*1000.0;
}

/* -------------USART RX Complete ISR------------- */
ISR(USART0_RXC_vect) {
    int8_t c = USART0.RXDATAL;
    if (c == 0x00) {
        rx_index = 0; // reset to start of buffer
        packet_ready = 1;
    } 
    else if (rx_index < RX_BUFFER_SIZE) {
        rx_buffer[rx_index++] = c;
    }
}

ISR(TCB1_INT_vect) {
    if (pulse_phase == 0) {
        // Start of pulse: set PA2 HIGH
        PORTA.OUT |= 0b00000100;
        if (CCMP_On < TCB1.CNT)
            TCB1.CNT = 0;         // Reset counter if CCMP is behind CNT
        TCB1.CCMP = CCMP_On;
        pulse_phase = 1;
    } else {
        PORTA.OUT &= 0b11111011;
        if (CCMP_Off < TCB1.CNT)
            TCB1.CNT = 0;         // Reset counter if CCMP is behind CNT
        TCB1.CCMP = CCMP_Off;
        TCB1.CNT = 0;          // Resets Count
        pulse_phase = 0;
    }
    // Clear interrupt flag
    TCB1.INTFLAGS = TCB_CAPT_bm;  // Clear interrupt
}


/* ----------------------   MAIN   ---------------------- */
int main(void) {
    /* === Clock === */
    CCP = 0xD8;
    CLKCTRL.OSCHFCTRLA = 0b00011100;                 // 8 MHz
    while (CLKCTRL.MCLKSTATUS & 0x01);               // Makes sure clock is ready 
    // Enable USART
    USART_Init();     // sets up USART + interrupt
    servo_Init();     // Sets up TCB1
    
    /* === PWM Setup (unchanged) === */
    PORTMUX.TCAROUTEA = 0x02;                  // Change to PORTC
    TCA0.SINGLE.CTRLA = 0b00001001;            // 16 MHz/16
    TCA0.SINGLE.CTRLB = 0x03 | (1<<4) | (1<<5);    // Normal Mode and enable CMP0, CMP1
    TCA0.SINGLE.PER   = 31;                   // Sets frequency to 16kHz
    TCA0.SINGLE.CMP0  = 0;                     // CMP0 for forward
    TCA0.SINGLE.CMP1  = 0;                     // CMP1 for backward
    
    /* === I/O Config (unchanged) === */
    PORTC.DIRSET = 0b00000011;                 // Sets PC0, PC1 as output
    PORTA.DIRSET = 0b10000100;                 // PA7 and PA2 as outputs

   /*Storage Variables*/
    uint8_t speed = 0, turning = 0, lights = 0, turbo = 0;

    while (1) {
        if (packet_ready) {
            packet_ready = 0;
            lights = rx_buffer[0];
            turbo = rx_buffer[1];
            speed = rx_buffer[2];
            turning = rx_buffer[3];
            
            // Wiggle Room for speed and turning, stopping and centering
            if (turning   < 30)
                turning = 30;
            else if (turning > 98) 
                turning = 98;
        }
        // Establishes speed and outputs as pwm
        int top_speed = 0;
        if (turbo == '1')
            top_speed = 25;
        else if (turbo == '0')
            top_speed = 20;
        int16_t signed_speed = speed - 64;
        int16_t duty = abs(signed_speed) * top_speed / 64;
        if (duty > top_speed) 
            duty = top_speed;
        else if (duty < 0)
            duty = 0;
        // Sets PWM for motor
        if (signed_speed < 0) {
            TCA0.SINGLE.CMP0 = duty;
            TCA0.SINGLE.CMP1 = 0;
        } else if (signed_speed > 0) {
            TCA0.SINGLE.CMP0 = 0;
            TCA0.SINGLE.CMP1 = duty;
        } else {
            TCA0.SINGLE.CMP0 = 0;
            TCA0.SINGLE.CMP1 = 0;
        }
        // Safeties to keep servo within range
        if (turning > 128)
            turning = 128;
        // Calculates Servo numbers
        servo_Calculations(turning);
        
        if(lights == '1')
            PORTA.OUT |= 0b10000000;
        else if (lights == '0')
            PORTA.OUT &= 0b01111111;
    }
    return 0;
}