/*
 * File:   main.c
 * Author: henry
 *
 * Created on July 28, 2021, 6:10 PM
 */

#include <xc.h> //compiler

//standard C libraries
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

//code common to all RCUs
#include "libpicutil/config_mem.h" //configuration fuses for chip options
#include "libpicutil/time.h"
#include "libpicutil/uart_debug.h"
#include "libcan/can.h"

//code specific to this RCU
#include "encoders.h"
#include "motors.h"
#include "solenoids.h"
#include "libpicutil/adc.h"

//#define _XTAL_FREQ 64000000UL //needed for delays to work, but not much else

#define RCU_ID RCU_ID_ENGINE_VALVE_RCU //this is the Engine Valve RCU

/*
 * 
 */

uint16_t last_200Hz_time,
    last_10Hz_time,
    last_2Hz_time,
    last_hb_rx_time;

uint8_t hb_rx_flag;

Motor_t ox_main = {.which = 1};    //motor 1
Motor_t fuel_press = {.which = 2}; //motor 2

ValveControl_t valve_cmd;

Heartbeat_t hb;

char msg[64];
uint16_t adcval;

void on_can_rx(const can_msg_t *msg);

int main()
{

    //RC3 is a digital output
    ANSELCbits.ANSELC3 = 0;
    TRISCbits.TRISC3 = 0;

    INTCON0bits.GIE = 1; //enable global interrupts

    time_init();
    uart_init();
    adc_init();
    can_rx_callback = &on_can_rx;
    can_init();
    solenoids_init();
    encoders_init();
    motors_init();

    //    //flash RC3
    //    for (uint8_t x = 0; x < 5; x++) {
    //        LATCbits.LATC3 = 1;
    //        __delay_ms(100);
    //
    //        LATCbits.LATC3 = 0;
    //        __delay_ms(100);
    //    }

    while (1)
    {
        if (one_kHz_flag)
        {
            one_kHz_flag = 0;
            encoders_update();
        }
        //if valve control CAN rx flag:
        //clear flag
        //load RAM structs with control data from CAN peripheral
        //reenable CAN receive
        if (time_millis() - last_200Hz_time > 5)
        { //200Hz
            last_200Hz_time = time_millis();
            //size_t n_chars = (size_t)sprintf(msg, "E1: %6u\tE2: %6u\n\r", enc_1_count, enc_2_count);
            //uart_tx((uint8_t*)msg, n_chars);
            //valve control
            motor_control(&ox_main);
            motor_control(&fuel_press);
            solenoids_set(&(valve_cmd.solenoids));
        }

        if (time_millis() - last_10Hz_time > 100)
        { //10Hz
            last_10Hz_time = time_millis();
            //send motor status msgs for both motors
            can_txq_push(ID_OX_MAIN_MOTOR_STATUS | RCU_ID_ENGINE_VALVE_RCU, sizeof(MotorStatus_t), (uint8_t *)&ox_main.status);
            can_txq_push(ID_FUEL_PRESS_MOTOR_STATUS | RCU_ID_ENGINE_VALVE_RCU, sizeof(MotorStatus_t), (uint8_t *)&fuel_press.status);
            
            //adc test
            adcval = adc_read(18); //RC2
            uint8_t txlen = sprintf(msg, "ADC:%6u\n", adcval);
            uart_tx((uint8_t*)msg, txlen);
        }
        if (time_millis() - last_2Hz_time > 500)
        { //2Hz
            last_2Hz_time = time_millis();
            LATCbits.LC3 = ~LATCbits.LC3; //blink LED at 1Hz
            //send a heartbeat msg
            hb.health = HEALTH_NOMINAL;
            hb.uptime_s = time_secs();
            can_txq_push(ID_HEARTBEAT | RCU_ID_ENGINE_VALVE_RCU, sizeof(Heartbeat_t), (uint8_t *)&hb);
        }
    }
}

//on CAN frame RX
//set flag based on ID
//disable CAN RX

void on_can_rx(const can_msg_t *msg)
{
    switch (msg->id)
    {
    case (ID_VALVE_CONTROL | RCU_ID_MAIN_RCU): //valve control message from main RCU
        //update local ValveControl_t with data from message
        if (msg->len == sizeof(ValveControl_t))
        {
            valve_cmd = *((ValveControl_t *)msg->data);
        }
        break;
    case (ID_HEARTBEAT | RCU_ID_MAIN_RCU): //heartbeat from main RCU
        //set flag to indicate heartbeat received. main loop can note the time
        if (msg->len == sizeof(Heartbeat_t))
        {
            hb_rx_flag = 1;
        }
        break;
    }
}
