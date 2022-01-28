/*
 **
 ******************************************************************************
 * @file           : MESCfoc.c
 * @brief          : FOC running code and ADC buffers
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2020 David Molony.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 ******************************************************************************

 * MESCfoc.c
 *
 *  Created on: 18 Jul 2020
 *      Author: David Molony
 */

/* Includes ------------------------------------------------------------------*/
#include "MESCfoc.h"

#include <math.h>

#include "MESCBLDC.h"
#include "MESChw_setup.h"
#include "MESCmotor_state.h"
#include "sin_cos.h"

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim4;
#ifdef STM32F303xC
extern OPAMP_HandleTypeDef hopamp1, hopamp2, hopamp3;
#endif
extern ADC_HandleTypeDef hadc1, hadc2, hadc3, hadc4;
#ifdef STM32F303xC
extern COMP_HandleTypeDef hcomp1, hcomp2, hcomp4, hcomp7;
#endif
float one_on_sqrt6 = 0.408248;
float one_on_sqrt3 = 0.577350;
float one_on_sqrt2 = 0.707107;
float sqrt_two_on_3 = 0.816497;
float sqrt3_2 = 1.22474;
float sqrt2 = 1.41421;
float sqrt1_2 = 0.707107;
float sqrt3_on_2 = 0.866025;
float two_on_sqrt3 = 1.73205;
int adc_conv_end;
uint8_t b_write_flash = 0;
uint8_t b_read_flash = 0;

void MESCInit() {
#ifdef STM32F303xC
  HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
  HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
  HAL_ADCEx_Calibration_Start(&hadc3, ADC_SINGLE_ENDED);
  HAL_ADCEx_Calibration_Start(&hadc4, ADC_SINGLE_ENDED);
#endif
  HAL_Delay(3000);  // Give the everything else time to start up (e.g. throttle,
                    // controller, PWM source...)
#ifdef STM32F303xC
  HAL_OPAMP_Start(&hopamp1);
  HAL_OPAMP_Start(&hopamp2);
  HAL_OPAMP_Start(&hopamp3);
#endif
  hw_init();  // Populate the resistances, gains etc of the PCB - edit within
              // this function if compiling for other PCBs
  // motor.Rphase = 0.1; //Hack to make it skip over currently not used motor
  // parameter detection
  foc_vars.initing = 1;  // Tell it we ARE initing...
                         // BLDCInit();	//Not currently using this, since FOC
                         // has taken over as primary method of interest
  // Although we are using an exponential filter over thousands of samples to
  // find this offset, accuracy still improved by starting near to the final
  // value. Initiialise the hall sensor offsets
  foc_vars.hall_forwards_adjust = 5460;
  foc_vars.hall_backwards_adjust = 5460;

  measurement_buffers.ADCOffset[0] = 0;
  measurement_buffers.ADCOffset[1] = 0;
  measurement_buffers.ADCOffset[2] = 0;

  // Start the PWM channels, reset the counter to zero each time to avoid
  // triggering the ADC, which in turn triggers the ISR routine and wrecks the
  // startup
#ifdef STM32F303xC
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
  __HAL_TIM_SET_COUNTER(&htim1, 10);

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
  __HAL_TIM_SET_COUNTER(&htim1, 10);

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
  __HAL_TIM_SET_COUNTER(&htim1, 10);

  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, 1022);

  // Initialise the comparators - 3 overcurrent and 1 overvoltage,
  HAL_COMP_Start(&hcomp1);
  HAL_COMP_Start(&hcomp2);
  HAL_COMP_Start(&hcomp4);
  // HAL_COMP_Start(&hcomp7);  // OVP comparator, may be unwanted if operating
  // above the divider threshold, the ADC conversion can also be used to trigger
  // a protection event

  __HAL_TIM_SET_COUNTER(&htim1, 10);

  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&measurement_buffers.RawADC[0][0], 3);
  __HAL_TIM_SET_COUNTER(&htim1, 10);

  HAL_ADC_Start_DMA(&hadc2, (uint32_t *)&measurement_buffers.RawADC[1][0], 4);
  __HAL_TIM_SET_COUNTER(&htim1, 10);

  HAL_ADC_Start_DMA(&hadc3, (uint32_t *)&measurement_buffers.RawADC[2][0], 1);
  __HAL_TIM_SET_COUNTER(&htim1, 10);

  HAL_ADC_Start_DMA(&hadc4, (uint32_t *)&measurement_buffers.RawADC[3][0], 1);

  __HAL_ADC_ENABLE_IT(&hadc1, ADC_IT_AWD1);
  __HAL_ADC_ENABLE_IT(&hadc1, ADC_IT_AWD2);
  __HAL_ADC_ENABLE_IT(&hadc2, ADC_IT_AWD1);
  __HAL_ADC_ENABLE_IT(&hadc3, ADC_IT_AWD1);
  // Using the ADC AWD to detect overcurrent events.

  __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
  // Using the timer updates to commute the motor

#endif
#ifdef STM32F405xx
  HAL_ADCEx_InjectedStart_IT(&hadc1);
  HAL_ADCEx_InjectedStart(&hadc2);
  HAL_ADCEx_InjectedStart(&hadc3);

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
  htim1.Instance->CCR4 = 1022;
  __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);

#endif
  htim1.Instance->BDTR |=
      TIM_BDTR_MOE;  // initialising the comparators triggers the break state,
                     // so turn it back on
  // At this point we just let the whole thing run off into interrupt land, and
  // the fastLoop() starts to be triggered by the ADC conversion complete
  // interrupt
}

static int current_hall_state;

void fastLoop() {
  current_hall_state = getHallState();
  // Call this directly from the ADC callback IRQ
  ADCConversion();  // First thing we ever want to do is convert the ADC values
                    // to real, useable numbers.

  switch (MotorState) {
    case MOTOR_STATE_SENSORLESS_RUN:
      // Call the observer
      // Call the current and phase controller
      // Write the PWM values
      break;

    case MOTOR_STATE_HALL_RUN:
      // transform
      if (MotorControlType ==
          MOTOR_CONTROL_TYPE_BLDC) {  // BLDC is hopefully just a temporary "Get
                                      // it spinning" kind of thing, to be
                                      // deprecated in favour of FOC
        BLDCCurrentController();
        BLDCCommuteHall();
      }
      if (MotorControlType == MOTOR_CONTROL_TYPE_FOC) {
        hallAngleEstimator();
        angleObserver();
        MESCFOC();
        flux_observer();
        // fluxIntegrator();
      }
      break;

    case MOTOR_STATE_HALL_NEAR_STATIONARY:

      // Call GetHallState
      // Call the BLDC discrete controller - Override the normal current
      // controller, this is 6 step DC only Write the PWM values
      break;

    case MOTOR_STATE_OPEN_LOOP_STARTUP:
      // Same as open loop
      OLGenerateAngle();
      MESCFOC();
      // Write the PWM values
      break;

    case MOTOR_STATE_OPEN_LOOP_TRANSITION:
      // Run open loop
      // Run observer
      // RunFOC
      // Weighted average of the outputs N PWM cycles
      // Write the PWM values
      break;

    case MOTOR_STATE_IDLE:
      // Do basically nothing
      // ToDo Set PWM to no output state
      break;

    case MOTOR_STATE_DETECTING:;

      int test = GetHallState();

      if ((test == 6) || (test == 7)) {
        // no hall sensors detected
        MotorSensorMode = MOTOR_SENSOR_MODE_SENSORLESS;
      } else if (test == 8) {
        MotorState = MOTOR_STATE_ERROR;
        MotorError = MOTOR_ERROR_HALL7;
      }
      // ToDo add reporting
      else {
        // hall sensors detected
        MotorSensorMode = MOTOR_SENSOR_MODE_HALL;
        if (1) {
          getHallTable();
        } else {
          MotorState = MOTOR_STATE_HALL_RUN;
        }
        MESCFOC();
      }
      break;

    case MOTOR_STATE_MEASURING:
      if (b_read_flash) {
        MotorState = MOTOR_STATE_HALL_RUN;
        b_read_flash = 0;
        break;
      } else {
        motor_init();
      }
      if (motor.Rphase == 0) {  // Every PWM cycle we enter this function until
                                // the resistance measurement has converged at a
                                // good value. Once the measurement is complete,
                                // Rphase is set, and this is no longer called
        if (foc_vars.initing == 0) {
          measureResistance();
        }
        break;
      } else if (motor.Lphase ==
                 0)  // This is currently rolled into measureResistance() since
                     // it seemed pointless to re-write basically the same
                     // function...
      {
        // As per resistance measurement, this will be called until an
        // inductance measurement is converged.
        // measureInductance();
        break;
      }

      break;

    case MOTOR_STATE_ERROR:
      generateBreak();  // Generate a break state (software disabling all PWM
                        // phases, hardware OVCP reserved for fatal situations
                        // requiring reset)
                        // Now panic and freak out
      break;

    case MOTOR_STATE_ALIGN:
      // Turn on at a given voltage at electricalangle0;
      break;
    case MOTOR_STATE_TEST:
      // Double pulse test
      doublePulseTest();
      break;
    case MOTOR_STATE_RECOVERING:

      // No clue so far. Read the phase voltages and determine position
      // and attempt to restart? Should already be in break state, and
      // should stay there...
      break;
  }
}

#define MAX_ERROR_COUNT 3

// TODO: refactor this function. Is this function called by DMA interrupt?
void VICheck() {  // Check currents, voltages are within panic limits
  static int errorCount = 0;

  if ((measurement_buffers.RawADC[0][0] > g_hw_setup.RawCurrLim) ||
      (measurement_buffers.RawADC[1][0] > g_hw_setup.RawCurrLim) ||
      (measurement_buffers.RawADC[2][0] > g_hw_setup.RawCurrLim) ||
      (measurement_buffers.RawADC[0][1] > g_hw_setup.RawVoltLim) ||
#ifdef STM32F405xx
      (measurement_buffers.RawADC[3][0] > 3000)) {

#endif
#ifdef STM32F303xC

      (measurement_buffers.RawADC[3][0] < 1000)) {
#endif
        foc_vars.Idq_req[0] = foc_vars.Idq_req[0] * 0.9;
        foc_vars.Idq_req[1] = foc_vars.Idq_req[1] * 0.9;

        errorCount++;
        if (errorCount >= MAX_ERROR_COUNT) {
          generateBreak();
          measurement_buffers.adc1 = measurement_buffers.RawADC[0][0];
          measurement_buffers.adc2 = measurement_buffers.RawADC[1][0];
          measurement_buffers.adc3 = measurement_buffers.RawADC[2][0];
          measurement_buffers.adc4 = measurement_buffers.RawADC[0][1];

          MotorState = MOTOR_STATE_ERROR;
          MotorError = MOTOR_ERROR_OVER_LIMIT;
        }
      }
      else {
        errorCount = 0;
      }
  }

  void ADCConversion() {
    getRawADC();
#ifdef STM32F405xx
    VICheck();  // The f303 now uses the analog watchdog to process the over
                // limits
                // The f405 currently does not...
#endif

    // Here we take the raw ADC values, offset, cast to (float) and use the
    // hardware gain values to create volt and amp variables

    if (foc_vars.initing) {
      for (uint32_t i = 0; i < 3; i++) {
        measurement_buffers.ADCOffset[i] += measurement_buffers.RawADC[i][0];
      }

      static int initcycles = 0;
      initcycles = initcycles + 1;
      if (initcycles == 1000) {
        calculateGains();
        calculateVoltageGain();

        for (uint32_t i = 0; i < 3; i++) {
          measurement_buffers.ADCOffset[i] /= 1000;
        }
#ifdef STM32F303xC
        setAWDVals();
#endif
        htim1.Instance->BDTR |= TIM_BDTR_MOE;
        foc_vars.initing = 0;
      }
    }

    measurement_buffers.ConvertedADC[0][0] =
        (float)(measurement_buffers.RawADC[0][0] -
                measurement_buffers.ADCOffset[0]) *
        g_hw_setup.Igain;  // Currents
    measurement_buffers.ConvertedADC[1][0] =
        (float)(measurement_buffers.RawADC[1][0] -
                measurement_buffers.ADCOffset[1]) *
        g_hw_setup.Igain;
    measurement_buffers.ConvertedADC[2][0] =
        (float)(measurement_buffers.RawADC[2][0] -
                measurement_buffers.ADCOffset[2]) *
        g_hw_setup.Igain;
    measurement_buffers.ConvertedADC[0][1] =
        (float)measurement_buffers.RawADC[0][1] * g_hw_setup.VBGain;  // Vbus
    measurement_buffers.ConvertedADC[0][2] =
        (float)measurement_buffers.RawADC[0][2] * g_hw_setup.VBGain;  // Usw
    measurement_buffers.ConvertedADC[1][1] =
        (float)measurement_buffers.RawADC[1][1] * g_hw_setup.VBGain;  // Vsw
    measurement_buffers.ConvertedADC[1][2] =
        (float)measurement_buffers.RawADC[1][2] * g_hw_setup.VBGain;  // Wsw

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Here we do the FOC transforms - Clark and Park, using the previous sin
    // values, since they were the correct ones at the time of sampling
    // clang-format off

        // Clark - Power invariant version - 3 phases; tested, woring, but won't cope with high duty cycles without phase current sensors
        /*foc_vars.Iab[0] = 	(2.0f * measurement_buffers.ConvertedADC[0][0] -
        					measurement_buffers.ConvertedADC[1][0] -
							measurement_buffers.ConvertedADC[2][0]) * one_on_sqrt6;

        foc_vars.Iab[1] = 	(measurement_buffers.ConvertedADC[1][0] -
        					measurement_buffers.ConvertedADC[2][0]) * one_on_sqrt2;

        foc_vars.Iab[2] = 	(measurement_buffers.ConvertedADC[0][0] +
        					measurement_buffers.ConvertedADC[1][0] +
							measurement_buffers.ConvertedADC[2][0]) * 0.333f;
        */
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //Version of Clark transform that avoids low duty cycle ADC measurements - use only 2 phases
//        if(htim1.Instance->CCR2>900){
//        	//Clark using phase U and W
//        	foc_vars.Iab[0] = sqrt3_2*measurement_buffers.ConvertedADC[0][0];
//        	foc_vars.Iab[1] = -sqrt1_2*measurement_buffers.ConvertedADC[0][0]-sqrt2*measurement_buffers.ConvertedADC[2][0];
//        }
//        else if(htim1.Instance->CCR3>900){
//        //Clark using phase U and V
//    //I think this is wrong!!! Bonus - sign on Ibeta
//        	foc_vars.Iab[0] = sqrt3_2*measurement_buffers.ConvertedADC[0][0];
//        	foc_vars.Iab[1] = sqrt2*measurement_buffers.ConvertedADC[1][0]-sqrt1_2*measurement_buffers.ConvertedADC[0][0];
//        }
//        else{
//        	//Clark using phase V and W (hardware V1 has best ADC readings on channels V and W - U is plagued by the DCDC converter)
//        	foc_vars.Iab[0] = -sqrt3_2*measurement_buffers.ConvertedADC[1][0]-sqrt3_2*measurement_buffers.ConvertedADC[2][0];
//        	foc_vars.Iab[1] = sqrt1_2*measurement_buffers.ConvertedADC[1][0]-sqrt1_2*measurement_buffers.ConvertedADC[2][0];
//        }
//Power Variant transform
        if(htim1.Instance->CCR2>900){
        	//Clark using phase U and W
        	foc_vars.Iab[0] = measurement_buffers.ConvertedADC[ADCIU][I_CONV_NO];
        	foc_vars.Iab[1] = -one_on_sqrt3*measurement_buffers.ConvertedADC[ADCIU][I_CONV_NO] - two_on_sqrt3*measurement_buffers.ConvertedADC[ADCIW][I_CONV_NO];
        	}
        else if(htim1.Instance->CCR3>900){
        //Clark using phase U and V
        	foc_vars.Iab[0] = measurement_buffers.ConvertedADC[ADCIU][I_CONV_NO];
        	foc_vars.Iab[1] = two_on_sqrt3*measurement_buffers.ConvertedADC[ADCIV][I_CONV_NO] + one_on_sqrt3*two_on_sqrt3*measurement_buffers.ConvertedADC[ADCIU][I_CONV_NO];
        	}
        else if(htim1.Instance->CCR1>900){
        	//Clark using phase V and W (hardware V1 has best ADC readings on channels V and W - U is plagued by the DCDC converter)
        	foc_vars.Iab[0] = -measurement_buffers.ConvertedADC[ADCIV][I_CONV_NO]-measurement_buffers.ConvertedADC[ADCIW][I_CONV_NO];
        	foc_vars.Iab[1] = one_on_sqrt3*measurement_buffers.ConvertedADC[ADCIV][I_CONV_NO]-one_on_sqrt3*measurement_buffers.ConvertedADC[ADCIW][I_CONV_NO];
        }
        else{
        	//Do the full transform
        	foc_vars.Iab[0] = 0.66666*measurement_buffers.ConvertedADC[ADCIU][I_CONV_NO]-0.33333*measurement_buffers.ConvertedADC[ADCIV][I_CONV_NO] - 0.33333*measurement_buffers.ConvertedADC[ADCIW][I_CONV_NO];
        	foc_vars.Iab[1] = sqrt3_on_2*measurement_buffers.ConvertedADC[ADCIV][I_CONV_NO] - sqrt3_on_2*measurement_buffers.ConvertedADC[ADCIW][I_CONV_NO];
        }

        // Park
        foc_vars.Idq[0] = foc_vars.sincosangle[1] * foc_vars.Iab[0] + foc_vars.sincosangle[0] * foc_vars.Iab[1];
        foc_vars.Idq[1] = foc_vars.sincosangle[1] * foc_vars.Iab[1] - foc_vars.sincosangle[0] * foc_vars.Iab[0];
    // clang-format on

    adc_conv_end = htim1.Instance->CNT;
  }
  /////////////////////////////////////////////////////////////////////////////
  ////////Hall Sensor Implementation///////////////////////////////////////////
  static float dir = 1;
  static uint16_t current_hall_angle;
  static int last_hall_state;
  static uint16_t last_hall_angle;
  static float ticks_since_last_observer_change = 65535;
  static float last_observer_period = 65536;
  static float one_on_last_observer_period = 1;
  float angular_velocity = 0;
  static float angle_step = 0;

  static int hall_error = 0;
  void hallAngleEstimator() {  // Implementation using the mid point of the hall
                               // sensor angles, which should be much more
                               // reliable to generate that the edges

    if (current_hall_state != last_hall_state) {
      foc_vars.hall_update = 1;
      if (current_hall_state == 0) {
        MotorState = MOTOR_STATE_ERROR;
        MotorError = MOTOR_ERROR_HALL0;
      } else if (current_hall_state == 7) {
        MotorState = MOTOR_STATE_ERROR;
        MotorError = MOTOR_ERROR_HALL7;
      }
      //////////Implement the Hall table here, but the vector can be dynamically
      /// created/filled by another function/////////////
      current_hall_angle = foc_vars.hall_table[current_hall_state - 1][2];

      // Calculate Hall error

      uint16_t a;
      if ((a = current_hall_angle - last_hall_angle) < 32000)  // Forwards
      {
        hall_error =
            foc_vars.FOCAngle - foc_vars.hall_table[current_hall_state - 1][0];
        dir = 1.0f;
        // foc_vars.HallAngle = foc_vars.HallAngle - 5460;
      } else  // Backwards
      {
        hall_error =
            foc_vars.FOCAngle - foc_vars.hall_table[current_hall_state - 1][1];
        dir = -1.0f;
        // foc_vars.HallAngle = foc_vars.HallAngle + 5460;
      }
      if (hall_error > 32000) {
        hall_error = hall_error - 65536;
      }
      if (hall_error < -32000) {
        hall_error = hall_error + 65536;
      }
    }
  }
  static float BEMFa = 0.00001;
  static float BEMFb = 0.00001;
  static float Ia_last = 0;
  static float Ib_last = 0;
  static uint16_t angle = 0;
  static uint16_t angle_error = 0;

  void flux_observer() {
    // This function we are going to integrate Va-Ri and clamp it positively and
    // negatively the angle is then the arctangent of the integrals shifted 180
    // degrees
    BEMFa = BEMFa + foc_vars.Vab[0] - motor.Rphase * foc_vars.Iab[0] -
            motor.Lphase * (foc_vars.Iab[0] - Ia_last) * foc_vars.pwm_frequency;
    BEMFb = BEMFb + foc_vars.Vab[1] - motor.Rphase * foc_vars.Iab[1] -
            motor.Lphase * (foc_vars.Iab[1] - Ib_last) * foc_vars.pwm_frequency;
    Ia_last = foc_vars.Iab[0];
    Ib_last = foc_vars.Iab[1];


    if (BEMFa > motor.motor_flux) {
      BEMFa = motor.motor_flux;
    }
    if (BEMFa < -motor.motor_flux) {
      BEMFa = -motor.motor_flux;
    }
    if (BEMFb > motor.motor_flux) {
      BEMFb = motor.motor_flux;
    }
    if (BEMFb < -motor.motor_flux) {
      BEMFb = -motor.motor_flux;
    }

    angle = (uint16_t)(32768.0f + 10430.0f * fast_atan2(BEMFb, BEMFa)) - 32768;
    angle_error = angle - foc_vars.FOCAngle;
  }
  float min(float lhs, float rhs) { return (lhs < rhs) ? lhs : rhs; }
  float max(float lhs, float rhs) { return (lhs > rhs) ? lhs : rhs; }
  // based on https://math.stackexchange.com/a/1105038/81278
  // Via Odrive project
  // https://github.com/odriverobotics/ODrive/blob/master/Firmware/MotorControl/utils.cpp
  // This function is MIT licenced, copyright Oskar Weigl/Odrive Robotics
  // The origin for Odrive atan2 is public domain. Thanks to Odrive for making
  // it easy to borrow.
  float fast_atan2(float y, float x) {
    // a := min (|x|, |y|) / max (|x|, |y|)
    float abs_y = fabs(y);
    float abs_x = fabs(x);
    // inject FLT_MIN in denominator to avoid division by zero
    float a = min(abs_x, abs_y) / (max(abs_x, abs_y));
    // s := a * a
    float s = a * a;
    // r := ((-0.0464964749 * s + 0.15931422) * s - 0.327622764) * s * a + a
    float r =
        ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    // if |y| > |x| then r := 1.57079637 - r
    if (abs_y > abs_x) r = 1.57079637f - r;
    // if x < 0 then r := 3.14159274 - r
    if (x < 0.0f) r = 3.14159274f - r;
    // if y < 0 then r := -r
    if (y < 0.0f) r = -r;

    return r;
  }

  static float last_anglestep;
  static float observerkp = 0.005;
  static float observerki = 0.01;

  void angleObservernew() {
    // This version of the observer will eliminate all divide functions and
    // correct based on a time rather than per cycle
    if (foc_vars.hall_update == 1) {
      foc_vars.hall_update = 0;
      ticks_since_last_observer_change = ticks_since_last_observer_change + 1;
    }
    angle_step = angle_step - observerki * observerkp * hall_error;
    foc_vars.FOCAngle =
        foc_vars.FOCAngle +
        (uint16_t)(angle_step - observerkp * hall_error * angle_step * 0.0005);

    if (ticks_since_last_observer_change > 3000.0f) {
      ticks_since_last_observer_change = 1501.0f;
      foc_vars.FOCAngle = current_hall_angle;
    }
  }
  void angleObserver() {
    // This function should take the available data (hall change, BEMF crossing
    // etc...) and process it with a PLL type mechanism
    if (foc_vars.hall_update == 1) {
      foc_vars.hall_update = 0;
      last_observer_period = ticks_since_last_observer_change;
      float one_on_ticks = (1.0 / ticks_since_last_observer_change);
      one_on_last_observer_period =
          (4 * one_on_last_observer_period + (one_on_ticks)) * 0.2;  // ;
      angle_step =
          (4 * angle_step +
           (one_on_ticks)*foc_vars.hall_table[last_hall_state - 1][3]) *
          0.2;
      //                if(hall_error>200){
      //                	foc_vars.hall_table[current_hall_state -
      //                1][0]=foc_vars.hall_table[current_hall_state - 1][0]+1;
      //                	foc_vars.hall_table[last_hall_state -
      //                1][0]=foc_vars.hall_table[last_hall_state - 1][0]-1;
      //
      //
      //                }
      //                if(hall_error<-200){
      //                	foc_vars.hall_table[current_hall_state - 1][0] =
      //                foc_vars.hall_table[current_hall_state - 1][0]-1;
      //                	foc_vars.hall_table[last_hall_state -
      //                1][0]=foc_vars.hall_table[last_hall_state - 1][0]+1;
      //
      //                }
      //                if(last_anglestep>angle_step){
      //                	foc_vars.hall_table[last_hall_state - 1][3] =
      //                foc_vars.hall_table[last_hall_state - 1][3]+1;
      //                }
      //                if(last_anglestep<angle_step){
      //                	foc_vars.hall_table[last_hall_state - 1][3] =
      //                foc_vars.hall_table[last_hall_state - 1][3]-1;
      //                }
      //                last_anglestep=angle_step;
      // Attempting to make the halls auto adjust to reduce the error in
      // load-less spinning. Promising behaviour (current ripple reduced) but
      // seems to run away with itself

      // Reset the counters, track the previous state
      last_hall_state = current_hall_state;
      last_hall_angle = current_hall_angle;
      ticks_since_last_observer_change = 0;
    }
    if (foc_vars.BEMF_update == 1) {
      foc_vars.BEMF_update = 0;
      last_observer_period = ticks_since_last_observer_change;
      float one_on_ticks = (1.0 / ticks_since_last_observer_change);
      one_on_last_observer_period =
          one_on_ticks;  // (3*one_on_last_observer_period + (one_on_ticks)) *
                         // 0.25;  // ;
      angle_step = (angle_step + (one_on_ticks)*32768) * 0.5;
      // Reset the counters
      ticks_since_last_observer_change = 0;
      hall_error = foc_vars.FOCAngle -
                   foc_vars.state[2];  // FUDGE, Need to have different terms
                                       // for error from different sources
      dir = 1.0f;
      if (hall_error > 32000) {
        hall_error = hall_error - 65536;
      }
      if (hall_error < -32000) {
        hall_error = hall_error + 65536;
      }
    }
    // Run the counter
    ticks_since_last_observer_change = ticks_since_last_observer_change + 1;
    //    if (ticks_since_last_observer_change <= 2.0 * last_observer_period) {
    //      // foc_vars.HallAngle = foc_vars.HallAngle +
    //      (uint16_t)(dir*angle_step +
    //      // one_on_last_hall_period * (-0.9 * hall_error)); Does not work...
    //      Why?
    //
    //      if (dir >
    //          0) {  // Apply a gain to the error as well as the feed forward
    //                // from the last hall period. Gain of 0.9-1.1 seems to
    //                work
    //                // well when using corrected hall positions and spacings
    //        foc_vars.FOCAngle =
    //            foc_vars.FOCAngle +
    //            (uint16_t)(angle_step - one_on_last_observer_period *
    //            hall_error);
    //        // one_on_last_observer_period * (-0.2 * hall_error));
    //      } else if (dir < 0) {
    //        // foc_vars.HallAngle = foc_vars.HallAngle +
    //        (uint16_t)(-angle_step +
    //        // one_on_last_hall_period * (-0.9 * hall_error)); Also does not
    //        work,
    //        // Why??
    //        foc_vars.FOCAngle =
    //            foc_vars.FOCAngle -
    //            (uint16_t)(angle_step +
    //                       one_on_last_observer_period * (0.2 * hall_error));
    //      }
    //    }
    if (ticks_since_last_observer_change > 3000.0f) {
      ticks_since_last_observer_change = 1501.0f;
      last_observer_period = 500.0f;  //(ticks_since_last_hall_change);
      one_on_last_observer_period =
          1.0f / last_observer_period;  // / ticks_since_last_hall_change;
      foc_vars.FOCAngle = current_hall_angle;
    }
    foc_vars.FOCAngle =
        angle;  ////MASSIVE HACK THAT NEEDS REMOVING TO GET THE OBSERVER IN
  }
  uint16_t flux_blanking;
  float BEMF_integral_check[2];
  void fluxIntegrator() {
    // We integrate the Valpha and Vbeta cycle by cycle, and add a damper to
    // centre it about zero
    foc_vars.VBEMFintegral[0] =
        0.995f * (foc_vars.VBEMFintegral[0] + 0.5 * foc_vars.Vab[0] +
                  2.0f * motor.Rphase * foc_vars.Iab[0]);
    // ignore the inductance for now, since it requires current
    // derivatives, and motors on my desk are all low inductance
    foc_vars.VBEMFintegral[1] =
        0.995f * (foc_vars.VBEMFintegral[1] + 0.5 * foc_vars.Vab[1] +
                  2.0f * motor.Rphase * foc_vars.Iab[1]);

    flux_blanking++;
    if (flux_blanking > 10)  // Slight concern that when the crossing occurs, it
                             // could jitter due to ADC noise
    {
      if (foc_vars.VBEMFintegral[0] > foc_vars.VBEMFintegral[1]) {
        foc_vars.state[0] = 1;
      } else {
        foc_vars.state[0] = 0;
      }

      if (foc_vars.state[0] != foc_vars.state[1]) {
        BEMF_integral_check[0] = foc_vars.VBEMFintegral[0];
        BEMF_integral_check[1] = foc_vars.VBEMFintegral[1];

        flux_blanking = 0;
        foc_vars.BEMF_update =
            1;  // Signal to the observer that new change found
        foc_vars.state[1] = foc_vars.state[0];
        if (foc_vars.VBEMFintegral[0] > 0) {
          foc_vars.state[2] =
              40960;  // This returns the higher angle (40960) in
                      // forward direction, not tested reverse
        } else        // The angle is 45 degrees (8192)
        {
          foc_vars.state[2] = 8192;
        }
      }
    }
  }

  void OLGenerateAngle() {
    // ToDo
  }
  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // FOC PID
  // algorithms//////////////////////////////////////////////////////////////////////////////////////////
  void MESCFOC() {
    // Here we are going to adjust the angle of the hall sensors to improve
    // their accuracy This is not stable at startup or with field weakening,
    // therefore needs to be rolled into a setup routine.
    //    if (last_hall_period < 400)
    //    {  // Placeholder for switching off if there is a requested Id current
    //        if (foc_vars.Vdq[0] > 40)
    //        {
    //            foc_vars.hall_forwards_adjust = foc_vars.hall_forwards_adjust
    //            - 1;
    //        }
    //        else if (foc_vars.Vdq[0] < -40)
    //        {
    //            foc_vars.hall_forwards_adjust = foc_vars.hall_forwards_adjust
    //            + 1;
    //        }
    //    }

    // Here we are going to do a PID loop to control the dq currents, converting
    // Idq into Vdq Calculate the errors
    static float Idq_err[2];
    Idq_err[0] = foc_vars.Idq_req[0] - foc_vars.Idq[0];
    Idq_err[1] = foc_vars.Idq_req[1] - foc_vars.Idq[1];

    // Integral error
    static float Idq_int_err[2];
    Idq_int_err[0] =
        Idq_int_err[0] +
        foc_vars.Id_igain * Idq_err[0];  // Apply the integral gain at this
                                         // stage to enable bounding it
    Idq_int_err[1] = Idq_int_err[1] + foc_vars.Iq_igain * Idq_err[1];  //

    static int i = 0;
    if (i == 0) {  // set or release the PID controller; may want to do this for
                   // cycle skipping, which may help for high inductance motors
      // Bounding
      // clang-format off
        static float integral_d_limit = 150.0f;
        static float integral_q_limit = 512.0f;

        if (Idq_int_err[0] > foc_vars.Vd_max){Idq_int_err[0] = foc_vars.Vd_max;}
        if (Idq_int_err[0] < -foc_vars.Vd_max){Idq_int_err[0] = -foc_vars.Vd_max;}
        if (Idq_int_err[1] > foc_vars.Vq_max){Idq_int_err[1] = foc_vars.Vq_max;}
        if (Idq_int_err[1] < -foc_vars.Vq_max){Idq_int_err[1] = -foc_vars.Vq_max;}
      // clang-format on
      // Apply the PID, and potentially smooth the output for noise - sudden
      // changes in VDVQ may be undesirable for some motors. Integral error is
      // pre-bounded to avoid integral windup, proportional gain needs to have
      // effect even at max integral to stabilise and avoid trips
      foc_vars.Vdq[0] = foc_vars.Id_pgain * Idq_err[0] + Idq_int_err[0];
      foc_vars.Vdq[1] = foc_vars.Iq_pgain * Idq_err[1] + Idq_int_err[1];

      // Bounding final output
      // These limits are experimental, but result in close to 100% modulation.
      // Since Vd and Vq are orthogonal, limiting Vd is not especially helpful
      // in reducing overall voltage magnitude, since the relation
      // Vout=(Vd^2+Vq^2)^0.5 results in Vd having a small effect. Vd is
      // primarily used to drive the resistive part of the field; there is no
      // BEMF pushing against Vd and so it does not scale with RPM (except for
      // cross coupling).
      //      static float V_d_limit = 150.0f;
      //      static float V_q_limit = 512.0f;

      if (foc_vars.Vdq[0] > foc_vars.Vd_max)
        (foc_vars.Vdq[0] = foc_vars.Vd_max);
      if (foc_vars.Vdq[0] < -foc_vars.Vd_max)
        (foc_vars.Vdq[0] = -foc_vars.Vd_max);
      if (foc_vars.Vdq[1] > foc_vars.Vq_max)
        (foc_vars.Vdq[1] = foc_vars.Vq_max);
      if (foc_vars.Vdq[1] < -foc_vars.Vq_max)
        (foc_vars.Vdq[1] = -foc_vars.Vq_max);
      // foc_vars.Vdq_smoothed[1] = (999 * foc_vars.Vdq_smoothed[1] +
      // foc_vars.Vdq[1]) * 0.001;
      i = FOC_PERIODS;
      // Field weakening? - The below works pretty nicely, but needs turning
      // into an implementation where it is switchable by the user. Not useable
      // when manually setting Id, or if there is an MTPA implementation Can
      // result in problems e.g. tripping PSUs...
      //        if((foc_vars.Vdq[1]>300)){
      //        	foc_vars.Idq_req[0]=(foc_vars.Vdq[1]-300)*-0.1; //36A
      //        max field weakening current
      //        }
      //        else if((foc_vars.Vdq[1]<-300)){
      //        	foc_vars.Idq_req[0]=(foc_vars.Vdq[1]+300)*0.1; //36A max
      //        field weakening current
      //        }
      //        else{
      //        	foc_vars.Idq_req[0]=0; //30A max field weakening current
      //
      //        }
    }
    i = i - 1;

    // Now we update the sin and cos values, since when we do the inverse
    // transforms, we would like to use the most up to date versions(or even the
    // next predicted version...)
    foc_vars.sincosangle[0] = sinwave[foc_vars.FOCAngle >> 8];
    foc_vars.sincosangle[1] = sinwave[(foc_vars.FOCAngle >> 8) + 64];
    // Inverse Park transform
    foc_vars.Vab[0] = foc_vars.sincosangle[1] * foc_vars.Vdq[0] -
                      foc_vars.sincosangle[0] * foc_vars.Vdq[1];
    foc_vars.Vab[1] = foc_vars.sincosangle[0] * foc_vars.Vdq[0] +
                      foc_vars.sincosangle[1] * foc_vars.Vdq[1];
    foc_vars.Vab[2] = 0;
    // clang-format off

    // Inverse Clark transform - power invariant
    //foc_vars.inverterVoltage[0] = 0;
//    foc_vars.inverterVoltage[1] = -foc_vars.Vab[0] * one_on_sqrt6;
//    foc_vars.inverterVoltage[2] = foc_vars.inverterVoltage[1] - one_on_sqrt2 * foc_vars.Vab[1];
//    foc_vars.inverterVoltage[1] = foc_vars.inverterVoltage[1] + one_on_sqrt2 * foc_vars.Vab[1];
//    foc_vars.inverterVoltage[0] = sqrt_two_on_3 * foc_vars.Vab[0];

    // Inverse Clark transform - power variant
    	foc_vars.inverterVoltage[0] = foc_vars.Vab[0];
        foc_vars.inverterVoltage[1] = -0.5*foc_vars.inverterVoltage[0];
        foc_vars.inverterVoltage[2] = foc_vars.inverterVoltage[1] - sqrt3_on_2 * foc_vars.Vab[1];
        foc_vars.inverterVoltage[1] = foc_vars.inverterVoltage[1] + sqrt3_on_2 * foc_vars.Vab[1];
    writePWM();  // clang-format on
  }

  static float mid_value = 0;

  void writePWM() {
    ////////////////////////////////////////////////////////
    // SVPM implementation
    // Try to do this as a "midpoint clamp" where rather than finding the
    // lowest, we find the highest and lowest and subtract the middle
        float top_value = foc_vars.inverterVoltage[0];
        float bottom_value = top_value;

        if (foc_vars.inverterVoltage[1] > top_value) {
          top_value = foc_vars.inverterVoltage[1];
        }
        if (foc_vars.inverterVoltage[2] > top_value) {
          top_value = foc_vars.inverterVoltage[2];
        }
        if (foc_vars.inverterVoltage[1] < bottom_value) {
          bottom_value = foc_vars.inverterVoltage[1];
        }
        if (foc_vars.inverterVoltage[2] < bottom_value) {
          bottom_value = foc_vars.inverterVoltage[2];
        }

        mid_value = 512.0f - 0.5 * (top_value + bottom_value);

        //Slightly faster but less readable implementation
//    if (foc_vars.inverterVoltage[0] < foc_vars.inverterVoltage[1]) {
//      if (foc_vars.inverterVoltage[2] < foc_vars.inverterVoltage[0]) {
//        // C low B high
//        mid_value = 512.0f - 0.5 * (foc_vars.inverterVoltage[2] +
//                                    foc_vars.inverterVoltage[1]);
//      } else {
//        if (foc_vars.inverterVoltage[2] > foc_vars.inverterVoltage[1]) {
//          // A low C high
//          mid_value = 512.0f - 0.5 * (foc_vars.inverterVoltage[0] +
//                                      foc_vars.inverterVoltage[2]);
//        } else {
//          // A low B high
//          mid_value = 512.0f - 0.5 * (foc_vars.inverterVoltage[0] +
//                                      foc_vars.inverterVoltage[1]);
//        }
//      }
//    } else {
//      if (foc_vars.inverterVoltage[2] < foc_vars.inverterVoltage[1]) {
//        // C low A high
//        mid_value = 512.0f - 0.5 * (foc_vars.inverterVoltage[2] +
//                                    foc_vars.inverterVoltage[0]);
//      } else if (foc_vars.inverterVoltage[2] > foc_vars.inverterVoltage[0]) {
//        // B low C high
//        mid_value = 512.0f - 0.5 * (foc_vars.inverterVoltage[1] +
//                                    foc_vars.inverterVoltage[2]);
//      } else {
//        // B low A high
//        mid_value = 512.0f - 0.5 * (foc_vars.inverterVoltage[1] +
//                                    foc_vars.inverterVoltage[0]);
//      }
//    }

    ////////////////////////////////////////////////////////
    // Actually write the value to the timer registers
    htim1.Instance->CCR1 = (uint16_t)(
        foc_vars.Vab_to_PWM * foc_vars.inverterVoltage[0] + mid_value);
    htim1.Instance->CCR2 = (uint16_t)(
        foc_vars.Vab_to_PWM * foc_vars.inverterVoltage[1] + mid_value);
    htim1.Instance->CCR3 = (uint16_t)(
        foc_vars.Vab_to_PWM * foc_vars.inverterVoltage[2] + mid_value);
    /*
              ///////////////////////////////////////////////
        if ((foc_vars.Vdq[1] > 300) | (foc_vars.Vdq[1] < -300)) {
          // Bottom Clamp implementation. Implemented initially because I
       	  //thought this avoids
          // all nasty behaviour in the event of underflowing the timer CCRs; if
          // negative values fed to timers, they will saturate positive, which
       	  //will result in a near instant overcurrent event.

          float minimumValue = 0;

          if (foc_vars.inverterVoltage[0] < foc_vars.inverterVoltage[1]) {
            if (foc_vars.inverterVoltage[0] < foc_vars.inverterVoltage[2]) {
              minimumValue = foc_vars.inverterVoltage[0];
            } else {
              minimumValue = foc_vars.inverterVoltage[2];
            }
          } else if (foc_vars.inverterVoltage[1] < foc_vars.inverterVoltage[2])
       { minimumValue = foc_vars.inverterVoltage[1]; } else { minimumValue =
       foc_vars.inverterVoltage[2];
          }

          foc_vars.inverterVoltage[0] -= minimumValue;
          foc_vars.inverterVoltage[1] -= minimumValue;
          foc_vars.inverterVoltage[2] -= minimumValue;


          ////////////////////////////////////////////////////////
          // Actually write the value to the timer registers
          htim1.Instance->CCR1 = (uint16_t)(foc_vars.inverterVoltage[0]);
          htim1.Instance->CCR2 = (uint16_t)(foc_vars.inverterVoltage[1]);
          htim1.Instance->CCR3 = (uint16_t)(foc_vars.inverterVoltage[2]);
        }

        ///////////////////////////////////////////////

        else {
          // Sinusoidal implementation
          htim1.Instance->CCR1 = (uint16_t)(512.0f +
       foc_vars.inverterVoltage[0]); htim1.Instance->CCR2 = (uint16_t)(512.0f +
       foc_vars.inverterVoltage[1]); htim1.Instance->CCR3 = (uint16_t)(512.0f +
       foc_vars.inverterVoltage[2]);
        }
        */
  }

  // Here we set all the PWMoutputs to LOW, without triggering the timerBRK,
  // which should only be set by the hardware comparators, in the case of a
  // shoot-through or other catastrophic event This function means that the
  // timer can be left running, ADCs sampling etc which enables a recovery, or
  // single PWM period break in which the backEMF can be measured directly
  // This function needs implementing and testing before any high current or
  // voltage is applied, otherwise... DeadFETs
  void generateBreak() {
    phU_Break();
    phV_Break();
    phW_Break();
  }

  void measureResistance() {
    /*In this function, we are going to use an openloop  controller to
     * create a current, probably ~4A, through a pair of motor windings, keeping
     * the third tri-stated. We then generate a pair of V and I values, from the
     * bus voltage and duty cycle, and the current reading. We repeat this at
     * higher current, say ~12A, and then apply R=dV/dI from the two values to
     * generate a resistance. Don't use a single point, since this is subject to
     * anomalies from switching dead times, ADC sampling position...etc. Use of
     * the derivative eliminates all steady state error sources ToDo Repeat for
     * all phases? Or just assume they are all close enough that it doesn't
     * matter? Could be useful for disconnection detection...
     */
    static float currAcc1 = 0;
    static float currAcc2 = 0;
    static float currAcc3 = 0;
    static float currAcc4 = 0;

    static uint16_t PWMcycles = 0;

    if (0)  // isMotorRunning() //ToDo, implement this
    {
      // do nothing
    } else {
      // turn off phW, we are just going to measure RUV
      static uint16_t testPWM1 =
          0;  // Start it at zero point, adaptive current thingy can ramp it
      static uint16_t testPWM2 = 0;  //
      static uint16_t testPWM3 =
          0;  // Use this for the inductance measurement, calculate it later
      if (PWMcycles < 1) {
        htim1.Instance->CCR1 = 0;
        htim1.Instance->CCR2 = 0;
        htim1.Instance->CCR3 = 0;
      }
      phW_Break();
      phU_Enable();
      phV_Enable();
      ///////////////////////////////////////////////////////RESISTANCE/////////////////////////////////////////////////////////////////////

      if (PWMcycles < 5000)  // Resistance lower measurement point
      {
        if (measurement_buffers.ConvertedADC[1][0] >
            -3.0f) {  // Here we set the PWM duty automatically for this
                      // conversion to ensure a current between 3A and 10A
          testPWM1 = testPWM1 + 1;
        }
        if (measurement_buffers.ConvertedADC[1][0] < -10.0f) {
          testPWM1 = testPWM1 - 1;
        }

        htim1.Instance->CCR2 = 0;
        htim1.Instance->CCR1 = testPWM1;
        // Accumulate the currents with an exponential smoother. This
        // averaging should remove some noise and increase
        // effective resolution
        currAcc1 =
            (99 * currAcc1 + measurement_buffers.ConvertedADC[1][0]) * 0.01;
      }

      else if (PWMcycles < 10000)  // Resistance higher measurement point
      {
        if (measurement_buffers.ConvertedADC[1][0] >
            -10.0f) {  // Here we set the PWM to get a current between 10A and
                       // 20A
          testPWM2 = testPWM2 + 1;
        }
        if (measurement_buffers.ConvertedADC[1][0] < -20.0f) {
          testPWM2 = testPWM2 - 1;
        }

        htim1.Instance->CCR2 = 0;
        htim1.Instance->CCR1 = testPWM2;
        // Accumulate the currents with an exponential smoother
        currAcc2 =
            (99 * currAcc2 + measurement_buffers.ConvertedADC[1][0]) * 0.01;
      }
      ///////////////////////////////////////////////////////INDUCTANCE/////////////////////////////////////////////////////////////////////
      else if (PWMcycles == 10000) {
        // calculate the resistance from two accumulated currents and two
        // voltages
        testPWM3 = 2 * testPWM2;  // We assign the value we determined was OK
                                  // for the resistance measurement as the value
                                  // to use for inductance measurement, since we
                                  // need an absolutely  stable steady state
      }

      else if (PWMcycles <
               65000)  // Inductance measurement points are rolled into one
                       // loop, we will skip pulses on the PWM to generate a
                       // higher ripple. ToDo Untested with higher inductance
                       // motors (only 1.5uH, 6uH and ~60uH motors tested as of
                       // 20201224) may have to skip multiple pulses
      {
        static int a = 0;  // A variable local to here to track whether the PWM
                           // was high or low last time
        if (a > 3) {
          htim1.Instance->CCR1 = testPWM3;  // Write the high PWM, the next
          a = a - 1;
        }
        // cycle will be a higher current
        else if (a == 3) {
          htim1.Instance->CCR1 = 0;
          a = a - 1;
        } else if (a == 2) {
          currAcc4 =
              (999 * currAcc4 + measurement_buffers.ConvertedADC[1][0]) * 0.001;
          a = a - 1;

        } else if (a == 1) {
          a = a - 1;
        } else if (a == 0) {
          htim1.Instance->CCR1 =
              0;  // Write the PWM low, the next PWM pulse is
                  // skipped, and the current allowed to decay
          currAcc3 =
              (999 * currAcc3 + measurement_buffers.ConvertedADC[1][0]) * 0.001;

          a = 5;
        }
      }

      // This was a prototype where the sampling point was moved within the PWM
      // cycle.
      //(Un?)fortunately, the change in current was quite small, and so the
      // inductance measurement subject to noise.

      /*else if (PWMcycles < 15000)
      {                                 // Measure the inductance first point
          htim1.Instance->CCR4 = 1022;  // Move the ADC trigger point - It does
      not like being moved to 1023 for some reason... htim1.Instance->CCR1 =
      testPWM3; currAcc3 = (999 * currAcc3 +
      measurement_buffers.ConvertedADC[1][0]) * 0.001;
      }

      else if (PWMcycles < 20000)
      {  // Measure the inductance second point
          if (htim1.Instance->CCR4 > 222)
          {
              htim1.Instance->CCR4 = htim1.Instance->CCR4 - 1;
          }
          // Move the ADC trigger point gradually down to 500 counts from where
      it was
          // This method works OK, but the change in current is tiny, even for a
      low inductance motor (~0.5A).
          // Might be better to implement this as skipping cycles and changing
      CCR1 on a cycle by cycle basis. htim1.Instance->CCR1 = testPWM3; currAcc4
      = (999 * currAcc4 + measurement_buffers.ConvertedADC[1][0]) * 0.001;
      }*/

      else if (PWMcycles == 65000)  // This really does not need to be 65000
                                    // cycles, yet I don't want to change it :(
      {                             // Do the calcs
        // First let's just turn everything off. Nobody likes motors sitting
        // there getting hot while debugging.
        htim1.Instance->CCR1 = 0;
        htim1.Instance->CCR2 = 0;
        htim1.Instance->CCR3 = 0;

        phU_Break();
        phV_Break();
        phW_Break();

        motor.Rphase = (((float)(testPWM2 - testPWM1)) / (2.0f * 1024.0f) *
                        measurement_buffers.ConvertedADC[0][1]) /
                       (-(currAcc2 - currAcc1));
        motor.Lphase =
            ((currAcc3 + currAcc4) * motor.Rphase *
             (2.0f * 2048.0f / 72000000.0f) / ((currAcc4 - currAcc3)));
        motor.Lphase =
            motor.Lphase /
            2.0f;  // The above line calculates the phase to phase inductance,
                   // but since we want one phase inductance div2
        if (motor.Lphase <= 0) {
          motor.Lphase = 0.00001;
        }
        // L=iRdt/di, where R in this case is 2*motor.Rphase
        calculateGains();
        // MotorState = MOTOR_STATE_IDLE;  //
        MotorState = MOTOR_STATE_DETECTING;
        phU_Enable();
        phV_Enable();
        phW_Enable();
      }
    }
    PWMcycles = PWMcycles + 1;
  }

  void getHallTable() {
    static int firstturn = 1;
    static int hallstate;
    hallstate = getHallState();
    static int lasthallstate;
    static uint16_t pwm_count = 0;
    static int anglestep = 1;  // This defines how fast the motor spins
    static uint32_t hallangles[7][2];
    static int rollover;

    if (firstturn) {
      lasthallstate = hallstate;
      firstturn = 0;
    }

    ////// Align the rotor////////////////////
    static uint16_t a = 65535;
    if (a)  // Align time
    {
      foc_vars.Idq_req[0] = 10;
      foc_vars.Idq_req[1] = 0;

      foc_vars.FOCAngle = 0;
      a = a - 1;
    } else {
      foc_vars.Idq_req[0] = 10;
      foc_vars.Idq_req[1] = 0;
      static int dir = 1;
      if (pwm_count < 65534) {
        if (foc_vars.FOCAngle < (anglestep)) {
          rollover = hallstate;
        }
        if ((foc_vars.FOCAngle < (30000)) &&
            (foc_vars.FOCAngle > (30000 - anglestep))) {
          rollover = 0;
        }
        lasthallstate = hallstate;
        if (rollover == hallstate) {
          hallangles[hallstate][0] =
              hallangles[hallstate][0] +
              (uint32_t)65535;  // Accumulate the angles through the sweep
        }

        foc_vars.FOCAngle =
            foc_vars.FOCAngle + anglestep;  // Increment the angle
        hallangles[hallstate][0] =
            hallangles[hallstate][0] +
            foc_vars.FOCAngle;       // Accumulate the angles through the sweep
        hallangles[hallstate][1]++;  // Accumulate the number of PWM pulses for
                                     // this hall state
        pwm_count = pwm_count + 1;
      } else if (pwm_count < 65535) {
        if (dir == 1) {
          dir = 0;
          rollover = 0;
        }
        if ((foc_vars.FOCAngle < (12000)) && (hallstate != last_hall_state)) {
          rollover = hallstate;
        }
        if ((foc_vars.FOCAngle < (65535)) &&
            (foc_vars.FOCAngle > (65535 - anglestep))) {
          rollover = 0;
        }
        lasthallstate = hallstate;
        if (rollover == hallstate) {
          hallangles[hallstate][0] =
              hallangles[hallstate][0] +
              (uint32_t)65535;  // Accumulate the angles through the sweep
        }

        foc_vars.FOCAngle =
            foc_vars.FOCAngle - anglestep;  // Increment the angle
        hallangles[hallstate][0] =
            hallangles[hallstate][0] +
            foc_vars.FOCAngle;       // Accumulate the angles through the sweep
        hallangles[hallstate][1]++;  // Accumulate the number of PWM pulses for
                                     // this hall state
        pwm_count = pwm_count + 1;
      }
    }
    if (pwm_count == 65535) {
      generateBreak();  // Debugging
      for (int i = 1; i < 7; i++) {
        hallangles[i][0] = hallangles[i][0] / hallangles[i][1];
        if (hallangles[i][0] > 65535) {
          hallangles[i][0] = hallangles[i][0] - 65535;
        }
      }
      for (int i = 0; i < 6; i++) {  // clang-format off
            foc_vars.hall_table[i][2] = hallangles[i + 1][0];//This is the center angle of the hall state
            foc_vars.hall_table[i][3] = hallangles[i + 1][1];//This is the width of the hall state
            foc_vars.hall_table[i][0] = foc_vars.hall_table[i][2]-foc_vars.hall_table[i][3]/2;//This is the start angle of the hall state
            foc_vars.hall_table[i][1] = foc_vars.hall_table[i][2]+foc_vars.hall_table[i][3]/2;//This is the end angle of the hall state
                                     // clang-format on
      }
      b_write_flash = 1;
      MotorState = MOTOR_STATE_HALL_RUN;
      foc_vars.Idq_req[0] = 0;
      foc_vars.Idq_req[1] = 0;
      phU_Enable();
      phV_Enable();
      phW_Enable();
    }
  }

  void measureInductance()  // UNUSED, THIS HAS BEEN ROLLED INTO THE MEASURE
                            // RESISTANCE... no point in 2 functions really...
  {
    /*
     * In this function, we are going to run at a fixed duty cycle (perhaps as
     * determined by Measure Resistance?), pushing ~5A through the motor coils
     * (~100ADCcounts). We will then wait until steady state achieved... 1000
     * PWM cycles? before modulating CCR4, which triggers the ADC to capture
     * currents at at least 2 time points within the PWM cycle With this change
     * in current, and knowing R from previous measurement, we can calculate L
     * using L=Vdt/dI=IRdt/dI ToDo Actually do this... ToDo Determination of the
     * direct and quadrature inductances for MTPA in future?
     */
  }

  /*fixme: this variable is not scope limited, so it is not temporary. It needs
   * to get a better name and be placed in a .h file. */
  uint32_t tmpccmrx;
  // Temporary buffer which is used to turn on/off phase PWMs
  // Turn all phase U FETs off, Tristate the HBridge output - For BLDC mode
  // mainly, but also used for measuring, software fault detection and recovery
  // ToDo TEST THOROUGHLY The register manipulations for the break functions
  // were used previously on an STM32F042K6 for my first BLDC drive, on TIM1,
  // which should be identical, but definitely needs checking

  void phU_Break() {
    tmpccmrx = htim1.Instance->CCMR1;
    tmpccmrx &= ~TIM_CCMR1_OC1M;
    tmpccmrx &= ~TIM_CCMR1_CC1S;
    tmpccmrx |= TIM_OCMODE_FORCED_INACTIVE;
    htim1.Instance->CCMR1 = tmpccmrx;
    htim1.Instance->CCER &= ~TIM_CCER_CC1E;   // disable
    htim1.Instance->CCER &= ~TIM_CCER_CC1NE;  // disable
  }
  // Basically un-break phase U, opposite of above...
  void phU_Enable() {
    tmpccmrx = htim1.Instance->CCMR1;
    tmpccmrx &= ~TIM_CCMR1_OC1M;
    tmpccmrx &= ~TIM_CCMR1_CC1S;
    tmpccmrx |= TIM_OCMODE_PWM1;
    htim1.Instance->CCMR1 = tmpccmrx;
    htim1.Instance->CCER |= TIM_CCER_CC1E;   // enable
    htim1.Instance->CCER |= TIM_CCER_CC1NE;  // enable
  }

  void phV_Break() {
    tmpccmrx = htim1.Instance->CCMR1;
    tmpccmrx &= ~TIM_CCMR1_OC2M;
    tmpccmrx &= ~TIM_CCMR1_CC2S;
    tmpccmrx |= TIM_OCMODE_FORCED_INACTIVE << 8;
    htim1.Instance->CCMR1 = tmpccmrx;
    htim1.Instance->CCER &= ~TIM_CCER_CC2E;   // disable
    htim1.Instance->CCER &= ~TIM_CCER_CC2NE;  // disable
  }

  void phV_Enable() {
    tmpccmrx = htim1.Instance->CCMR1;
    tmpccmrx &= ~TIM_CCMR1_OC2M;
    tmpccmrx &= ~TIM_CCMR1_CC2S;
    tmpccmrx |= TIM_OCMODE_PWM1 << 8;
    htim1.Instance->CCMR1 = tmpccmrx;
    htim1.Instance->CCER |= TIM_CCER_CC2E;   // enable
    htim1.Instance->CCER |= TIM_CCER_CC2NE;  // enable
  }

  void phW_Break() {
    tmpccmrx = htim1.Instance->CCMR2;
    tmpccmrx &= ~TIM_CCMR2_OC3M;
    tmpccmrx &= ~TIM_CCMR2_CC3S;
    tmpccmrx |= TIM_OCMODE_FORCED_INACTIVE;
    htim1.Instance->CCMR2 = tmpccmrx;
    htim1.Instance->CCER &= ~TIM_CCER_CC3E;   // disable
    htim1.Instance->CCER &= ~TIM_CCER_CC3NE;  // disable
  }

  void phW_Enable() {
    tmpccmrx = htim1.Instance->CCMR2;
    tmpccmrx &= ~TIM_CCMR2_OC3M;
    tmpccmrx &= ~TIM_CCMR2_CC3S;
    tmpccmrx |= TIM_OCMODE_PWM1;
    htim1.Instance->CCMR2 = tmpccmrx;
    htim1.Instance->CCER |= TIM_CCER_CC3E;   // enable
    htim1.Instance->CCER |= TIM_CCER_CC3NE;  // enable
  }

  void calculateGains() {
    foc_vars.pwm_period =
        2.0f * (float)htim1.Instance->ARR / (float)HAL_RCC_GetHCLKFreq();

    foc_vars.pwm_frequency =
        (float)HAL_RCC_GetHCLKFreq() /
        (2 * (float)htim1.Instance->ARR * ((float)htim1.Instance->PSC + 1));

    foc_vars.Iq_pgain = foc_vars.pwm_frequency * motor.Lphase * 0.5;  // *
    //                        ((float)htim1.Instance->ARR * 0.5) /
    //                        (2 * measurement_buffers.ConvertedADC[0][1]);

    foc_vars.Iq_igain =
        0.10f * motor.Rphase * 0.5;  // * (htim1.Instance->ARR * 0.5) /
    //                       (2 * measurement_buffers.ConvertedADC[0][1]);

    foc_vars.Id_pgain = foc_vars.Iq_pgain;
    foc_vars.Id_igain = foc_vars.Iq_igain;
    foc_vars.Vdqres_to_Vdq =
        0.333f * measurement_buffers.ConvertedADC[0][1] / 677.0f;
    foc_vars.field_weakening_curr_max =
        0;  // test number, to be stored in user settings
  }

  void calculateVoltageGain() {
    // We need a number to convert between Va Vb and raw PWM register values
    // This number should be the bus voltage divided by the ARR register
    foc_vars.Vab_to_PWM =
        htim1.Instance->ARR / measurement_buffers.ConvertedADC[0][1];
    // We also need a number to set the maximum voltage that can be effectively
    // used by the SVPWM This is equal to
    // 0.5*Vbus*MAX_MODULATION*SVPWM_MULTIPLIER*Vd_MAX_PROPORTION
    foc_vars.Vd_max = 0.5 * measurement_buffers.ConvertedADC[0][1] *
                      MAX_MODULATION * SVPWM_MULTIPLIER * Vd_MAX_PROPORTION;
    foc_vars.Vq_max = 0.5 * measurement_buffers.ConvertedADC[0][1] *
                      MAX_MODULATION * SVPWM_MULTIPLIER * Vq_MAX_PROPORTION;
    foc_vars.field_weakening_threshold = foc_vars.Vq_max * 0.8;
  }

  void doublePulseTest() {
    static int dp_counter;
    static int dp_periods = 7;
    if (dp_counter < dp_periods) {
      htim1.Instance->CCR1 = 0;
      htim1.Instance->CCR2 = 0;
      htim1.Instance->CCR3 = 1023;
      phU_Break();
      test_vals.dp_current_final[dp_counter] =
          measurement_buffers.ConvertedADC[1][0];
      dp_counter++;
    } else if (dp_counter == dp_periods) {
      htim1.Instance->CCR2 = 0;
      htim1.Instance->CCR3 = 100;
      test_vals.dp_current_final[dp_counter] =
          measurement_buffers.ConvertedADC[1][0];
      dp_counter++;
    } else {
      htim1.Instance->CCR2 = 0;
      htim1.Instance->CCR3 = 0;
      test_vals.dp_current_final[dp_counter] =
          measurement_buffers.ConvertedADC[1][0];
      dp_counter = 0;
      MotorState = MOTOR_STATE_IDLE;
    }
  }

  void slowLoop(TIM_HandleTypeDef * htim) {
    // The slow loop runs at either 20Hz or at the PWM input frequency.
    // In this loop, we will fetch the throttle values, and run functions that
    // are critical, but do not need to be executed very often e.g. adjustment
    // for battery voltage change

    uint32_t fCC1 = __HAL_TIM_GET_FLAG(htim, TIM_FLAG_CC1) &
                    __HAL_TIM_GET_IT_SOURCE(htim, TIM_IT_CC1);
    uint32_t fUPD = __HAL_TIM_GET_FLAG(htim, TIM_FLAG_UPDATE) &
                    __HAL_TIM_GET_IT_SOURCE(htim, TIM_IT_UPDATE);

    HAL_TIM_IRQHandler(htim);

    // Adjust the SVPWM gains to account for the change in battery voltage etc
    calculateVoltageGain();

    // If the event was CC1...
    if (fCC1 != RESET) {
      if (measurement_buffers.RawADC[1][3] > 1200) {
        foc_vars.Idq_req[1] =
            foc_vars.Idq_req[1] +
            (((float)(measurement_buffers.RawADC[1][3] - 1200)) * -0.02f);
      } else {
      }
    }
    // If the event was UPDATE ...
    else if (fUPD != RESET) {
      if (measurement_buffers.RawADC[1][3] > 1200) {
        foc_vars.Idq_req[1] =
            (((float)(measurement_buffers.RawADC[1][3] - 1200)) * -0.02f);
      } else {
        foc_vars.Idq_req[1] = 0.0f;
      }
    }

    // Run field weakening (and maybe MTPA later)
    if (fabs(foc_vars.Vdq[1]) > foc_vars.field_weakening_threshold) {
      foc_vars.Idq_req[0] =
          foc_vars.field_weakening_curr_max *
          (foc_vars.field_weakening_threshold - fabs(foc_vars.Vdq[1]));
      foc_vars.field_weakening_flag = 1;
    } else {
      foc_vars.Idq_req[0] = 0;
      foc_vars.field_weakening_flag = 0;
    }

    // For anything else...
    foc_vars.rawThrottleVal[1] = foc_vars.Idq_req[1];
    foc_vars.currentPower = fabs(foc_vars.Vdq_smoothed[1] * foc_vars.Idq[1] *
                                 foc_vars.Vdqres_to_Vdq);
    foc_vars.reqPower = fabs(foc_vars.Vdq_smoothed[1] * foc_vars.Idq_req[1] *
                             foc_vars.Vdqres_to_Vdq);
    if (foc_vars.reqPower >
        g_hw_setup.battMaxPower) {  // foc_vars.Vdq[0]*foc_vars.Idq[0]+
      // foc_vars.Idq_req[1] = foc_vars.Idq_req[1] * g_hw_setup.battMaxPower /
      // foc_vars.reqPower;
      foc_vars.Idq_req[1] =
          g_hw_setup.battMaxPower /
          (fabs(foc_vars.Vdq_smoothed[1]) * foc_vars.Vdqres_to_Vdq);
    }
  }