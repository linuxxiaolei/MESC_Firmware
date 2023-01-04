/*
 * TTerm
 *
 * Copyright (c) 2020 Thorben Zethoff, Jens Kerrinnes
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "hfi.h"
#include "string.h"
#include "MESCfoc.h"
#include "MESChw_setup.h"

#define APP_NAME "hfi"
#define APP_DESCRIPTION "HFI test"
#define APP_STACK 512

static uint8_t CMD_main(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args);
static void TASK_main(void *pvParameters);
static uint8_t INPUT_handler(TERMINAL_HANDLE * handle, uint16_t c);


uint8_t REGISTER_hfi(TermCommandDescriptor * desc){
    TERM_addCommand(CMD_main, APP_NAME, APP_DESCRIPTION, 0, desc); 
    return pdTRUE;
}

static uint8_t CMD_main(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args){
    
    uint8_t currArg = 0;
    uint8_t returnCode = TERM_CMD_EXIT_SUCCESS;
    char ** cpy_args=NULL;
    argCount++;
    if(argCount){
        cpy_args = pvPortMalloc(sizeof(char*)*argCount);
        cpy_args[0] = pvPortMalloc(sizeof(APP_NAME));
        cpy_args[0]=memcpy(cpy_args[0], APP_NAME, sizeof(APP_NAME));
        for(;currArg<argCount-1; currArg++){
            uint16_t len = strlen(args[currArg])+1;
            cpy_args[currArg+1] = pvPortMalloc(len);
            memcpy(cpy_args[currArg+1], args[currArg], len);
        }
    }
    TermProgram * prog = pvPortMalloc(sizeof(TermProgram));
    prog->inputHandler = INPUT_handler;
    prog->args = cpy_args;
    prog->argCount = argCount;
    TERM_sendVT100Code(handle, _VT100_RESET, 0); TERM_sendVT100Code(handle, _VT100_CURSOR_POS1, 0);
    returnCode = xTaskCreate(TASK_main, APP_NAME, APP_STACK, handle, tskIDLE_PRIORITY + 1, &prog->task) ? TERM_CMD_EXIT_PROC_STARTED : TERM_CMD_EXIT_ERROR;
    if(returnCode == TERM_CMD_EXIT_PROC_STARTED) TERM_attachProgramm(handle, prog);
    return returnCode;
}

static void bargraph(TERMINAL_HANDLE * handle, float min, float max, float val){
	char buffer[45];
	memset(buffer,0,45);
	buffer[0]='|';

	float norm = 40.0/max*val;

	for(int i=0; i<40;i++){
		buffer[i+1] = i<norm? '#' : '_';
	}
	buffer[41]='|';
	ttprintf("%s %f ", buffer, val);

}


#define ARROW_UP 	0x1005
#define ARROW_DOWN 	0x1006
#define ARROW_LEFT 	0x1004
#define ARROW_RIGHT 0x1003

#define MAX_ITEMS	3

static void highlight(TERMINAL_HANDLE * handle, char * text ,int index, int count){
	if(count==index){
		TERM_sendVT100Code(handle, _VT100_FOREGROUND_COLOR, _VT100_GREEN);
	}else{
		TERM_sendVT100Code(handle, _VT100_FOREGROUND_COLOR, _VT100_WHITE);
	}
	ttprintf("%s", text);
	TERM_sendVT100Code(handle, _VT100_FOREGROUND_COLOR, _VT100_WHITE);
}


static void TASK_main(void *pvParameters){

	MESC_motor_typedef * motor_curr = &mtr[0];

    TERMINAL_HANDLE * handle = (TERMINAL_HANDLE*)pvParameters;
    TERM_sendVT100Code(handle,_VT100_CURSOR_DISABLE, 0);
    uint16_t c=0;
    int selected=0;

    TERM_setCursorPos(handle, MAX_ITEMS*2+3, 0);
    ttprintf("Keys: [UP/DOWN] Select item, [-/+] Modify values, [r] Reset value, [CTRL+C] Quit");

    do{
    	TERM_sendVT100Code(handle,_VT100_CURSOR_DISABLE, 0);
    	TERM_setCursorPos(handle, 1, 0);
    	highlight(handle, "Angle:", 0, selected);
    	TERM_setCursorPos(handle, 2, 0);
    	bargraph(handle, 0, 65536, motor_curr->FOC.FOCAngle);

    	if(selected==0){
    		if(c=='r'){
    			motor_curr->FOC.FOCAngle=32768;
    		}
			if(c=='-'){
				motor_curr->FOC.FOCAngle-=100;
			}
			if(c=='+'){
				motor_curr->FOC.FOCAngle+=100;
			}
    	}


    	TERM_setCursorPos(handle, 3, 0);
    	highlight(handle, "Voltage:", 1, selected);
		TERM_setCursorPos(handle, 4, 0);
		bargraph(handle, 0, motor_curr->Conv.Vbus, motor_curr->meas.hfi_voltage);

		if(selected==1){
			if(c=='r'){
				motor_curr->meas.hfi_voltage = HFI_VOLTAGE;
			}
			if(c=='-'){
				motor_curr->meas.hfi_voltage -= 1.0f;
				if(motor_curr->meas.hfi_voltage<0) motor_curr->meas.hfi_voltage=0;
			}
			if(c=='+'){
				motor_curr->meas.hfi_voltage += 1.0f;
			}
		}

		TERM_setCursorPos(handle, 5, 0);
		highlight(handle, "Threshold:", 2, selected);
		TERM_setCursorPos(handle, 6, 0);
		bargraph(handle, 0, 4095, motor_curr->FOC.HFI_Threshold);

		if(selected==2){
			if(c=='r'){
				motor_curr->FOC.HFI_Threshold = HFI_THRESHOLD;
			}
			if(c=='-'){
				motor_curr->FOC.HFI_Threshold -= 0.1f;
			}
			if(c=='+'){
				motor_curr->FOC.HFI_Threshold += 0.1f;
			}
		}

		if(c==ARROW_DOWN){
			selected++;
			if(selected>MAX_ITEMS) selected = 0;
		}
		if(c==ARROW_UP){
			selected--;
			if(selected<0) selected = MAX_ITEMS-1;
		}

        c=0;
        xStreamBufferReceive(handle->currProgram->inputStream,&c,sizeof(c),pdMS_TO_TICKS(100));
    }while(c!=CTRL_C);
    TERM_sendVT100Code(handle,_VT100_CURSOR_ENABLE, 0);
    TERM_killProgramm(handle);
}

static uint8_t INPUT_handler(TERMINAL_HANDLE * handle, uint16_t c){
    if(handle->currProgram->inputStream==NULL) return TERM_CMD_EXIT_SUCCESS;
  	xStreamBufferSend(handle->currProgram->inputStream,&c,2,20);
  	return TERM_CMD_CONTINUE;
}
