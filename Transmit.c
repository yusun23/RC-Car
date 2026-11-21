#include <avr/io.h>
#include <util/delay.h>

#define F_CPU 16000000UL

void USART0_Init(void) {
    USART0.BAUD = 6666;  // Approx. 9600 baud
    USART0.CTRLC = USART_CMODE_ASYNCHRONOUS_gc |
                   USART_PMODE_DISABLED_gc |
                   USART_CHSIZE_8BIT_gc;
    PORTA.DIRSET = PIN0_bm;  // TX
    PORTA.DIRCLR = PIN1_bm;  // RX
    USART0.CTRLB = USART_TXEN_bm | USART_RXEN_bm;
}

void USART0_Transmit(uint8_t data) {
    while (!(USART0.STATUS & USART_DREIF_bm));
    USART0.TXDATAL = data;
}

void init_io(void) {
    // Buttons on PA6, PA7
    PORTA.DIRCLR = 0b11000000;
    
    // Joystick on PD1 (Y) and PD2 (X)
    PORTD.DIRCLR = PIN1_bm | PIN2_bm;
}

void ADC0_Init(void) {
    VREF.ADC0REF = 0b10000101;      // VDD reference
    ADC0.CTRLC = 0x0C;              // Prescaler to ~125kHz
    ADC0.CTRLA = 0b00000101;        // 10-bit mode, enable
    ADC0.INTCTRL = 0x01;           // Enable result ready flag
    ADC0.COMMAND = 0x01;           // Start initial conversion
}

uint16_t ADC0_Read(uint8_t channel) {
    ADC0.MUXPOS = channel;
    
    ADC0.COMMAND = 0x01;                  // Start conversion
    while (!(ADC0.INTFLAGS & 0x01));      // Wait until done
    ADC0.INTFLAGS = 0x01;                 // Clear flag
    
    return ADC0.RES;
}

int main(void) {
    CCP = 0xD8;
    CLKCTRL.OSCHFCTRLA = 0b00011100;     // 16 MHz
    while (CLKCTRL.MCLKSTATUS & 0x01);   // Wait for sync

    USART0_Init();
    init_io();
    ADC0_Init();

    uint8_t last_l_button_state = 1, l_state = 0;
    uint8_t last_t_button_state = 1, t_state = 0;
    uint8_t light = '0', turbo = '0';
    uint8_t old_light = '0', old_turbo = '0';
    uint8_t speed = 0, turn = 0;
    uint8_t old_speed = 0, old_turn = 0;

    while (1) {
        // Light button on PA7
        uint8_t current_l_button_state = (PORTA.IN & PIN7_bm);
        if (!current_l_button_state && last_l_button_state) {
            l_state ^= 1;
            light = l_state ? '1' : '0';
        }
        last_l_button_state = current_l_button_state;

        // Turbo button on PA6
        uint8_t current_t_button_state = (PORTA.IN & PIN6_bm);
        if (!current_t_button_state && last_t_button_state) {
            t_state ^= 1;
            turbo = t_state ? '1' : '0';
        }
        last_t_button_state = current_t_button_state;

        // Read joystick Y (speed) from PD1 (AIN1)
        speed = ADC0_Read(0x01) >> 3;
        if (speed >= 60 && speed <= 68)
            speed = 64;

        // Read joystick X (turn) from PD2 (AIN2)
        turn = ADC0_Read(0x02) >> 3;
        if (turn >= 60 && turn <= 68)
            turn = 64;

        // Transmit only if something changed
        if (light != old_light || turbo != old_turbo || speed != old_speed || turn != old_turn) {
            USART0_Transmit(light);
            USART0_Transmit(turbo);
            USART0_Transmit(speed);
            USART0_Transmit(turn);
            USART0_Transmit(0x00);

            old_light = light;
            old_turbo = turbo;
            old_speed = speed;
            old_turn = turn;
        }

        _delay_ms(10);  // Optional debounce / pacing
    }
}
