/***********************************************************************************************************************
* DISCLAIMER
* This software is supplied by Renesas Electronics Corporation and is only intended for use with Renesas products. No
* other uses are authorized. This software is owned by Renesas Electronics Corporation and is protected under all
* applicable laws, including copyright laws.
* THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
* THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED. TO THE MAXIMUM
* EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES
* SHALL BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR ANY REASON RELATED TO THIS
* SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
* Renesas reserves the right, without notice, to make changes to this software and to discontinue the availability of
* this software. By using this software, you agree to the additional terms and conditions found by accessing the
* following link:
* http://www.renesas.com/disclaimer
*
* Copyright (C) 2021 Renesas Electronics Corporation. All rights reserved.
***********************************************************************************************************************/
/***********************************************************************************************************************
* File Name   : main.c
* Description : The main function and the processes of application layer
***********************************************************************************************************************/
/**********************************************************************************************************************
* History : 28.05.2020  Ver.1.00
*         : 21.04.2021  Ver.1.10
***********************************************************************************************************************/

/***********************************************************************************************************************
* Includes <System Includes> , "Project Includes"
***********************************************************************************************************************/
#include <machine.h>
#include <math.h>
#include "iodefine.h"
#include "r_init_clock.h"

#include "mtr_common.h"
#include "main.h"
#include "mtr_3im_less_foc.h"
#include "mtr_ctrl_rx13t48.h"
#include "mtr_ctrl_rx13t48_t1102.h"
#include "mtr_ctrl_t1102.h"
#include "ICS2_RX13T.h"

/******************************************************************************
* Private global variables and functions
******************************************************************************/
uint8    g_u1_motor_status;                 /* motor status */
int16    com_s2_mode_system;                /* system mode */
int16    g_s2_mode_system;                  /* system mode */
int16    com_s2_direction;                  /* rotational direction (0:CW ,1:CCW) */
int16    com_s2_ref_speed_rpm;              /* motor speed reference [rpm] (electrical) */
int16    com_s2_get_vr1;
float32  com_f4_vr1_to_rpm;
float32  com_f4_kp_speed;                   /* proportional gain for speed PI control */
float32  com_f4_ki_speed;                   /* integral gain for speed PI control */
float32  com_f4_kp_iq;                      /* proportional gain for delta-axis current PI control */
float32  com_f4_ki_iq;                      /* integral gain for delta-axis current PI control */
float32  com_f4_speed_lpf_k;                /* speed LPF gain */
float32  com_f4_current_lpf_k;              /* current LPF gain */
float32  com_f4_mtr_rs;                     /* stator resistance */
float32  com_f4_mtr_rr;                     /* rotor resistance */
float32  com_f4_mtr_m;                      /* mutual inductance */
float32  com_f4_mtr_lls;                    /* stator leakage inductance */
float32  com_f4_mtr_llr;                    /* rotor leakage inductance */
float32  com_f4_offset_lpf_k;               /* current offset LPF gain */
int16    com_s2_max_speed_rpm;              /* max speed */
int16    com_s2_min_speed_rpm;              /* min speed */
float32  com_f4_ctrl_ref_id;                /* gamma-axis current reference */
float32  com_f4_boot_id_up_time;            /* rise time at gamma-axis current startup */
float32  com_f4_id_const_time;              /* gamma-axis current/flux stabilization wait time */
float32  com_f4_accel;                      /* speed command acceleration step [rad/s] */
float32  com_f4_fluctuation_limit;          /* speed fluctuation limit */
float32  com_f4_offset_calc_time;           /* current offset calculation time */
float32  com_f4_delay;                      /* g_f4_angle_compensation = com_f4_delay * MTR_CTRL_PERIOD */
float32  com_f4_voltage_drop;               /* voltage drop caused by dead time and Vce [V] */
float32  com_f4_voltage_drop_k;             /* voltage drop corrective gain [V/A] */
int16    com_s2_enable_write;               /* ICS write enable flag */
int16    g_s2_enable_write;                 /* ICS write enable flag */
MTR_ICS_INPUT ics_input;                    /* struct for ICS input */

uint8    g_u1_drive_sw_state;               /* drive ON/OFF sw status */
uint8    g_u1_err_reset_sw_state;           /* error reset sw status */
float32  g_f4_ref_speed_ad;                 /* ref speed value[digit] */
int16    g_s2_ref_speed_rpm;                /* ref speed value[rpm]   */

static void       ics_ui(void);
static void       software_init(void);

/* for DTC table */
#pragma section DTCTABLE
unsigned long dtc_table[256];
#pragma section

#define ADC_BIT_N 12
/*******************************************************************************
 Exported global variables and functions (to be accessed by other files)
 *******************************************************************************/
void main (void);

#pragma section End_of_RAM
uint32 dummy_ram;     /* Dummy ram for End of RAM */
#pragma section

extern volatile float32 g_f4_ref_speed_rad;
/******************************************************************************
* Function Name : main
* Description   : Initialization and main routine
* Arguments     : none
* Return Value  : none
******************************************************************************/
void main(void)
{
    float32 vr1_to_rpm;
    float32 rpm_to_rad;
clrpsw_i();                                                         /* interrupt disable */
    R_MTR_InitHardware();                                           /* initialize peripheral function */
    init_ui();                                                      /* initialize peripheral function for user interface */
    software_init();                                                /* initialize private global variables */
    R_MTR_InitSequence();                                           /* initialize sequence */
    ics2_init((void *)dtc_table, ICS_SCI1_PD3_PD5, ICS_INT_LEVEL,3 ,1);  /* initialize ics */
setpsw_i();                                                         /* interrupt enable */
    R_MTR_ExecEvent(MTR_EVENT_RESET);                               /* execute reset event */
clrpsw_i();                                                         /* interrupt disable */
    R_MTR_ChargeCapacitor();                                        /* wait for charging capacitor */
setpsw_i();

    /*** main routine ***/
    while (1)
    {
        ics_ui();                                                   /* user interface using ICS */

        vr1_to_rpm = (float32)((get_vr1() * CP_MAX_SPEED_RPM) >> ADC_BIT_N);

        rpm_to_rad = vr1_to_rpm * MTR_POLE_PAIRS * MTR_RPM_RAD;

        if (MTR_CW == ics_input.s2_direction) {
            g_f4_ref_speed_rad = rpm_to_rad;
        } else if (MTR_CCW == ics_input.s2_direction) {
            g_f4_ref_speed_rad = -rpm_to_rad;
        }

        clear_wdt();                                                /* watch dog timer clear */
    }
}
/*******************************************************************************
 End of function main
 *******************************************************************************/

/******************************************************************************
* Function Name : ics_ui
* Description   : User interface using ICS
* Arguments     : none
* Return Value  : none
******************************************************************************/
void ics_ui(void)
{
    int16 s2_temp;

    com_s2_get_vr1 = get_vr1();
    com_f4_vr1_to_rpm = (float32)((get_vr1() * CP_MAX_SPEED_RPM) >> ADC_BIT_N);
    /*============================*/
    /*      get ICS value         */
    /*============================*/
    com_s2_ref_speed_rpm = (com_s2_get_vr1 * CP_MAX_SPEED_RPM ) >> ADC_BIT_N;
    /***** limit variable *****/
    if (com_s2_ref_speed_rpm > com_s2_max_speed_rpm)
    {
        com_s2_ref_speed_rpm = com_s2_max_speed_rpm;
    }
    else if (com_s2_ref_speed_rpm < 0)
    {
        com_s2_ref_speed_rpm = 0;
    }

    /***** When com_s2_enable_sw and g_s2_enable_sw are same value, rewrite enable. *****/
    if (com_s2_enable_write == g_s2_enable_write)
    {
        /* rotation direction */
        if(R_MTR_GetDir() != com_s2_direction)
        {
            if (com_s2_direction > M_CCW)
            {
                com_s2_direction = M_CCW;
            }
        }
        ics_input.s2_direction = com_s2_direction;

        /* reference speed */
        ics_input.s2_ref_speed = com_s2_ref_speed_rpm;

        /* speed PI gain */
        ics_input.f4_kp_speed = com_f4_kp_speed;
        ics_input.f4_ki_speed = com_f4_ki_speed;

        /* q-axis current PI gain */
        ics_input.f4_kp_iq = com_f4_kp_iq;
        ics_input.f4_ki_iq = com_f4_ki_iq;

        /* speed LPF gain */
        ics_input.f4_speed_lpf_k = com_f4_speed_lpf_k;

        /* current LPF gain */
        ics_input.f4_current_lpf_k = com_f4_current_lpf_k;

        /* motor parameter */
        ics_input.f4_mtr_rs = com_f4_mtr_rs;
        ics_input.f4_mtr_rr = com_f4_mtr_rr;
        ics_input.f4_mtr_m = com_f4_mtr_m;
        ics_input.f4_mtr_lls = com_f4_mtr_lls;
        ics_input.f4_mtr_llr = com_f4_mtr_llr;

        /* current offset LPF gain */
        ics_input.f4_offset_lpf_k = com_f4_offset_lpf_k;

        /* max speed */
        ics_input.s2_max_speed = com_s2_max_speed_rpm;

        /* Id reference */
        ics_input.f4_ctrl_ref_id = com_f4_ctrl_ref_id;

        /* time to set id reference when boot */
        ics_input.f4_boot_id_up_time = com_f4_boot_id_up_time;

        /* id const time when boot */
        ics_input.f4_id_const_time = com_f4_id_const_time;

        /* accel value [rad/s^2] */
        ics_input.f4_accel = com_f4_accel;

        /* speed fluctuation limit */
        ics_input.f4_fluctuation_limit = com_f4_fluctuation_limit;

        /* g_f4_angle_compensation = ics_input.f4_delay * MTR_CTRL_PERIOD */
        ics_input.f4_delay = com_f4_delay;

        /* current offset calculation time */
        ics_input.f4_offset_calc_time = com_f4_offset_calc_time;

        /* voltage drop caused by dead time and Vce */
        ics_input.f4_voltage_drop = com_f4_voltage_drop;

        /* voltage drop corrective gain */
        ics_input.f4_voltage_drop_k = com_f4_voltage_drop_k;

        R_MTR_IcsInput(&ics_input);

        g_s2_enable_write ^= 1;                                 /* change every time 0 and 1 */
    }

    /*==============================*/
    /*        execute event         */
    /*==============================*/
    s2_temp = com_s2_mode_system;

    if (g_s2_mode_system != s2_temp)
    {
        g_s2_mode_system = s2_temp;
        R_MTR_ExecEvent((uint8)g_s2_mode_system);
    }

    g_u1_motor_status = R_MTR_GetStatus();              /* get status of motor control system */

    if (MTR_EVENT_RESET == g_s2_mode_system)
    {
        if (MTR_MODE_STOP == g_u1_motor_status)
        {
            software_init();                            /* initialize private global variables for reset event */
        }
        else if (MTR_MODE_ERROR == g_u1_motor_status)
        {
            g_s2_mode_system   = MTR_MODE_ERROR;
            com_s2_mode_system = MTR_MODE_ERROR;
        }
    }

    /***** LED control *****/
    if (g_u1_motor_status == MTR_MODE_STOP)             /* check motor status */
    {
        led1_off();                                     /* LED1 off */
        led2_off();                                     /* LED2 off */
    }
    else if (g_u1_motor_status == MTR_MODE_RUN)         /* check motor status */
    {
        led1_on();                                      /* LED1 on */
        led2_off();                                     /* LED2 off */
    }
    else if (g_u1_motor_status == MTR_MODE_ERROR)       /* check motor status */
    {
        led1_off();                                     /* LED1 off */
        led2_on();                                      /* LED2 on */
    }
    else
    {
        /* Do Nothing */
    }
}

/******************************************************************************
* Function Name : software_init
* Description   : Initialize private global variables
* Arguments     : none
* Return Value  : none
******************************************************************************/
void software_init(void)
{
    g_u1_motor_status              = 0;
    com_s2_mode_system             = 0;
    g_s2_mode_system               = 0;
    com_s2_get_vr1                 = 0;
    com_f4_vr1_to_rpm              = 0;
    com_s2_direction               = 0;
    com_s2_ref_speed_rpm           = 0;
    com_f4_kp_speed                = SPEED_PI_KP;
    com_f4_ki_speed                = SPEED_PI_KI;
    com_f4_kp_iq                   = IQ_PI_KP;
    com_f4_ki_iq                   = IQ_PI_KI;
    com_f4_speed_lpf_k             = SPEED_LPF_K;
    com_f4_current_lpf_k           = CURRENT_LPF_K;
    com_f4_mtr_rs                  = STATOR_RESISTANCE;
    com_f4_mtr_rr                  = ROTOR_RESISTANCE;
    com_f4_mtr_m                   = MUTUAL_INDUCTANCE;
    com_f4_mtr_lls                 = STATOR_LEAKAGE_INDUCTANCE;
    com_f4_mtr_llr                 = ROTOR_LEAKAGE_INDUCTANCE;
    com_f4_offset_lpf_k            = OFFSET_LPF_K;
    com_s2_max_speed_rpm           = MAX_SPEED;
    com_s2_min_speed_rpm           = MIN_SPEED;
    com_f4_ctrl_ref_id             = CTRL_REF_ID;
    com_f4_boot_id_up_time         = BOOT_ID_UP_TIME;
    com_f4_id_const_time           = ID_CONST_TIME;
    com_f4_accel                   = ACCEL_MODE0;
    com_f4_fluctuation_limit       = FLUCTUATION_LIMIT;
    com_f4_offset_calc_time        = OFFSET_CALC_TIME;
    com_f4_delay                   = DELAY;
    com_f4_voltage_drop            = VOLTAGE_DROP;
    com_f4_voltage_drop_k          = VOLTAGE_DROP_K;
    com_s2_enable_write            = 0;
    g_s2_enable_write              = 0;
    ics_input.s2_ref_speed         = 0;
    ics_input.s2_direction         = 0;
    ics_input.f4_kp_speed          = com_f4_kp_speed;
    ics_input.f4_ki_speed          = com_f4_ki_speed;
    ics_input.f4_kp_iq             = com_f4_kp_iq;
    ics_input.f4_ki_iq             = com_f4_ki_iq;
    ics_input.f4_speed_lpf_k       = com_f4_speed_lpf_k;
    ics_input.f4_current_lpf_k     = com_f4_current_lpf_k;
    ics_input.f4_mtr_rs            = com_f4_mtr_rs;
    ics_input.f4_mtr_rr            = com_f4_mtr_rr;
    ics_input.f4_mtr_m             = com_f4_mtr_m;
    ics_input.f4_mtr_lls           = com_f4_mtr_lls;
    ics_input.f4_mtr_llr           = com_f4_mtr_llr;
    ics_input.f4_offset_lpf_k      = com_f4_offset_lpf_k;
    ics_input.s2_max_speed         = com_s2_max_speed_rpm;
    ics_input.s2_min_speed         = com_s2_min_speed_rpm;
    ics_input.f4_ctrl_ref_id       = com_f4_ctrl_ref_id;
    ics_input.f4_boot_id_up_time   = com_f4_boot_id_up_time;
    ics_input.f4_id_const_time     = com_f4_id_const_time;
    ics_input.f4_accel             = com_f4_accel;
    ics_input.f4_fluctuation_limit = com_f4_fluctuation_limit;
    ics_input.f4_offset_calc_time  = com_f4_offset_calc_time;
    ics_input.f4_delay             = com_f4_delay;
    ics_input.f4_voltage_drop      = com_f4_voltage_drop;
    ics_input.f4_voltage_drop_k    = com_f4_voltage_drop_k;

    g_u1_drive_sw_state            = SW_OFF;
    g_u1_err_reset_sw_state        = SW_OFF;
    g_f4_ref_speed_ad              = 0;
    g_s2_ref_speed_rpm             = 0;
}

