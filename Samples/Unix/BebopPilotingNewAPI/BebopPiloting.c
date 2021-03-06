/*
  Copyright (C) 2014 Parrot SA

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  * Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in
  the documentation and/or other materials provided with the
  distribution.
  * Neither the name of Parrot nor the names
  of its contributors may be used to endorse or promote products
  derived from this software without specific prior written
  permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
  AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
  OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
  SUCH DAMAGE.
*/
/**
 * @file BebopPiloting.c
 * @brief This file contains sources about basic arsdk example sending commands to a bebop drone for piloting it and make it jump it and receiving its battery level
 * @date 15/01/2015
 */

/*****************************************
 *
 *             include file :
 *
 *****************************************/

#include <stdlib.h>
#include <curses.h> //  유닉스 계열 OS를 위한 터미널 제어 라이브러리
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <libARSAL/ARSAL.h> // AR Socket Absraction Layer
#include <libARController/ARController.h>
#include <libARDiscovery/ARDiscovery.h>

#include "BebopPiloting.h"
#include "ihm.h"

/*****************************************
 *
 *             define :
 *
 *****************************************/
#define TAG "SDKExample"

#define ERROR_STR_LENGTH 2048

#define BEBOP_IP_ADDRESS "192.168.42.1" // 드론 IP
#define BEBOP_DISCOVERY_PORT 44444

#define DISPLAY_WITH_MPLAYER 0 // Boolean, to run Mplayer, 1(run), 0(without)

#define FIFO_DIR_PATTERN "/tmp/arsdk_XXXXXX"
#define FIFO_NAME "arsdk_fifo"

#define IHM
/*****************************************
 *
 *             private header:
 *
 ****************************************/


/*****************************************
 *
 *             implementation :
 *
 *****************************************/

static char fifo_dir[] = FIFO_DIR_PATTERN;
static char fifo_name[128] = "";

int gIHMRun = 1;
char gErrorStr[ERROR_STR_LENGTH];
IHM_t *ihm = NULL;

FILE *videoOut = NULL;
int frameNb = 0;
ARSAL_Sem_t stateSem;
int isBebop2 = 0;

static void signal_handler(int signal)
{
    gIHMRun = 0;
}

/*
main Method
 Preprocessing Task -> Take a loop to take a keyboard input (=>Input~>IHM) -> Postprocessing Task
*/
int main (int argc, char *argv[])
{
    // local declarations
    int failed = 0;
    ARDISCOVERY_Device_t *device = NULL;
    ARCONTROLLER_Device_t *deviceController = NULL;
    eARCONTROLLER_ERROR error = ARCONTROLLER_OK;
    eARCONTROLLER_DEVICE_STATE deviceState = ARCONTROLLER_DEVICE_STATE_MAX;
    pid_t child = 0;

    /* Set signal handlers */
    struct sigaction sig_action = {
        .sa_handler = signal_handler,
    };

    /*
    Ctrl + C에 대한 시그널 처리
    @arg1 : Ctrl + C
    @Return : 0(성공), -1(실패)
    */
    int ret = sigaction(SIGINT, &sig_action, NULL);
    if (ret < 0) // 시그널 처리 실패
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, "ERROR", "Unable to set SIGINT handler : %d(%s)",
                    errno, strerror(errno));
        return 1;
    }

    /*
    사용하려는 파이프 상태에 대한 시그널 처리
    @arg1 : 파이프 시그널
    @Return : 0(성공), -1(실패)
    */
    ret = sigaction(SIGPIPE, &sig_action, NULL);
    if (ret < 0)  //  시그널 처리 실패
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, "ERROR", "Unable to set SIGPIPE handler : %d(%s)",
                    errno, strerror(errno));
        return 1;
    }

    /*
    mkdtemp : Make directory temporarily
    */
    if (mkdtemp(fifo_dir) == NULL)  //  임시 디렉토리 생성 실패
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, "ERROR", "Mkdtemp failed."); //error message about making directory
        return 1;
    }
    /*
    snprintf : arg1버퍼로 arg2사이즈 만큼 arg3형식화된 args 저장
    */
    snprintf(fifo_name, sizeof(fifo_name), "%s/%s", fifo_dir, FIFO_NAME);

    /*
    fifo생성
    @arg1 : fifo 이름
    @arg2 : fifo의 접근권한
    @Return : 0(성공), -1(실패)
    */
    if(mkfifo(fifo_name, 0666) < 0) // fifo 생성 실패
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, "ERROR", "Mkfifo failed: %d, %s", errno, strerror(errno));
        return 1;
    }

    /*
    세마포어 초기화
    @arg1 : 초기화할 세마포어
    @arg2 : Flag asking for a multi-process shared semaphore
    @arg3 : Initial value of the semaphore
    @Return : 0(성공), -1(실패)
    */
    ARSAL_Sem_Init (&(stateSem), 0, 0);
    
//    ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "Select your Bebop : Bebop (1) ; Bebop2 (2)");
//    Bebop(1)과 (2)선택사항을 주석처리
    char answer = '2'; //항상 Bebop(2)를 선택
//    scanf(" %c", &answer);  //  User input을 주석처리
    if (answer == '2') //Bebop2
    {
        isBebop2 = 1; //  true
    }

    if(isBebop2)  //  If Bebop2

    {
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "-- Bebop 2 Piloting --"); //Bebop2일 경우 Piloting Message 출력
    }
    else  //  If Bebop1
    {
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "-- Bebop Piloting --");
    }

    if (!failed)  //  failed Dfl->0, If did not failure
    {
        if (DISPLAY_WITH_MPLAYER) // Dfl->1
        {
            // fork the process to launch mplayer
            if ((child = fork()) == 0)  //  자식 프로세스
            {
                // 파일 이름 : xterm, 인수 : xterm -e mplayer -demuxer h264es "fifo" -benchmark -really-quiet
                execlp("xterm", "xterm", "-e", "mplayer", "-demuxer",  "h264es", fifo_name, "-benchmark", "-really-quiet", NULL);
                ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "Missing mplayer, you will not see the video. Please install mplayer and xterm.");  // 프로세스 실행 실패
                return -1;
            }
            //  부모 프로세스
        }

        if (DISPLAY_WITH_MPLAYER) //  Dfl->1
        {
            videoOut = fopen(fifo_name, "w"); //  fifo 오픈
        }
    }

#ifdef IHM
    /**
     * IHM 구조체 생성
     * @arg : 해당 코드 하단에 정의된 콜백 메소드
     * @Return : IHM 객체
     * */
    ihm = IHM_New (&onInputEvent);
    if (ihm != NULL)    // IHM 객체 생성 성공
    {
        gErrorStr[0] = '\0';    //  첫째 자리에 널 캐릭터 삽입
        ARSAL_Print_SetCallback (customPrintCallback); //use a custom callback to print, for not disturb ncurses IHM

        if(isBebop2)
        {
            IHM_PrintHeader (ihm, "-- Bebop 2 Piloting --\nNice connexion :D");
        }
        else
        {
            /* NEVER CALL THIS PART */
            IHM_PrintHeader (ihm, "-- Bebop Piloting -- >>IHM Message");
        }
    }
    else    //  IHM 객체 생성 실패
    {
        ARSAL_PRINT (ARSAL_PRINT_ERROR, TAG, "Creation of IHM failed.");
        failed = 1;
    }
#endif

    // create a discovery device
    if (!failed)    //  초기에는 무조건 0
    {
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- init discovey device ... ");
        eARDISCOVERY_ERROR errorDiscovery = ARDISCOVERY_OK; //  ARDISCOVERY_OK : No error

        /**
         * Create a new Discovery device
         * @post : ARDISCOVERY_Device_Delete() must be called to delete the Discovery Device and free the memory allc
         * @arg[out] : executing error
         * @return : ARDISCOVERY_Device_t / the new Discovery Device
         * */
        device = ARDISCOVERY_Device_New (&errorDiscovery);

        if (errorDiscovery == ARDISCOVERY_OK)   //  No error
        {
            ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "    - ARDISCOVERY_Device_InitWifi ...");
            // create a Bebop drone discovery device (ARDISCOVERY_PRODUCT_ARDRONE)

            if(isBebop2)    //  Bebop2
            {
                /**
                 * Initialize the Discovery Device with a wifi device
                 * @arg1 : ARDISCOVERY_Device_t
                 * @arg2 : eARDISCOVERY_PRODUCT
                 * @arg3 : Device name; must be Null - terminated
                 * @arg4 : char*; IP addr; must be Null - terminated
                 * @arg5 : int; port number
                 * @return : eARDISCOVERY_ERROR; executing error
                 * */
                errorDiscovery = ARDISCOVERY_Device_InitWifi (device, ARDISCOVERY_PRODUCT_BEBOP_2, "bebop2", BEBOP_IP_ADDRESS, BEBOP_DISCOVERY_PORT);
            }
            else    //  Bebop1
            {
                errorDiscovery = ARDISCOVERY_Device_InitWifi (device, ARDISCOVERY_PRODUCT_ARDRONE, "bebop", BEBOP_IP_ADDRESS, BEBOP_DISCOVERY_PORT);
            }

            if (errorDiscovery != ARDISCOVERY_OK)   //  ARDISCOVERY_Device_InitWifi Failure
            {
                failed = 1;
                ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "Discovery error :%s", ARDISCOVERY_Error_ToString(errorDiscovery));
            }
        }
        else    //  occur error
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "Discovery error :%s", ARDISCOVERY_Error_ToString(errorDiscovery));
            failed = 1;
        }
    }

    // create a device controller
    if (!failed)       //  Can be FALSE by IHM_New Failure, ARDISCOVERY_Device_New Failure, ARDISCOVERY_Device_InitWifi Failure
    {
        deviceController = ARCONTROLLER_Device_New (device, &error);

        if (error != ARCONTROLLER_OK)   //  occur error
        {
            ARSAL_PRINT (ARSAL_PRINT_ERROR, TAG, "Creation of deviceController failed.");
            failed = 1;
        }
        else    //  succeed
        {
            IHM_setCustomData(ihm, deviceController);
        }
    }

    if (!failed)
    {
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- delete discovey device ... ");
        ARDISCOVERY_Device_Delete (&device);
    }

    // add the state change callback to be informed when the device controller starts, stops...
    if (!failed)
    {
        error = ARCONTROLLER_Device_AddStateChangedCallback (deviceController, stateChanged, deviceController);

        if (error != ARCONTROLLER_OK)
        {
            ARSAL_PRINT (ARSAL_PRINT_ERROR, TAG, "add State callback failed.");
            failed = 1;
        }
    }

    // add the command received callback to be informed when a command has been received from the device
    if (!failed)
    {
        error = ARCONTROLLER_Device_AddCommandReceivedCallback (deviceController, commandReceived, deviceController);

        if (error != ARCONTROLLER_OK)
        {
            ARSAL_PRINT (ARSAL_PRINT_ERROR, TAG, "add callback failed.");
            failed = 1;
        }
    }

    // add the frame received callback to be informed when a streaming frame has been received from the device
    if (!failed)
    {
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- set Video callback ... ");
        error = ARCONTROLLER_Device_SetVideoStreamCallbacks (deviceController, decoderConfigCallback, didReceiveFrameCallback, NULL , NULL);

        if (error != ARCONTROLLER_OK)
        {
            failed = 1;
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "- error :%", ARCONTROLLER_Error_ToString(error));
        }
    }

    if (!failed)
    {
        IHM_PrintInfo(ihm, "Connecting ...");
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "Connecting ...");
        error = ARCONTROLLER_Device_Start (deviceController);

        if (error != ARCONTROLLER_OK)
        {
            failed = 1;
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "- error :%s", ARCONTROLLER_Error_ToString(error));
        }
    }

    if (!failed)
    {
        // wait state update update
        ARSAL_Sem_Wait (&(stateSem));

        deviceState = ARCONTROLLER_Device_GetState (deviceController, &error);

        if ((error != ARCONTROLLER_OK) || (deviceState != ARCONTROLLER_DEVICE_STATE_RUNNING))
        {
            failed = 1;
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "- deviceState :%d", deviceState);
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "- error :%s", ARCONTROLLER_Error_ToString(error));
        }
    }

    // send the command that tells to the Bebop to begin its streaming
    if (!failed)
    {
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- send StreamingVideoEnable ... ");
        error = deviceController->aRDrone3->sendMediaStreamingVideoEnable (deviceController->aRDrone3, 1);
        if (error != ARCONTROLLER_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "- error :%s", ARCONTROLLER_Error_ToString(error));
            failed = 1;
        }
    }

    /********************************
     * -- Main routine start here --
     * ******************************/
    if (!failed)
    {   //control key code
        IHM_PrintInfo(ihm, "Running ... \n(t)take-off \n(space)land \n(e)emergency \n(up)up (down)down (left)turn-left (right)turn-right \n(r)go (f)reverse (d)move-left (g)move-right \n(q)quit");
 
#ifdef IHM
        while (gIHMRun) // var in signal handler; Keep doing before SIGINT OR SIGPIPE
        {
            usleep(50); //  usleep : micro-sec sleep; 1000*1000=1s
        }
#else
        /**
        * IF IHM did not work
        */
        int i = 20;
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- sleep 20sec ... ");  //  only sleep in 20sec
        while (gIHMRun && i--)
            sleep(1);
#endif
    }

    /******************************************
     * -- Postprocessing routine start here --
     ******************************************/
#ifdef IHM
    IHM_Delete (&ihm);  //  delete IHM
#endif

    // we are here because of a disconnection or user has quit IHM, so safely delete everything
    if (deviceController != NULL)
    {
        /**
         * Get device state through device controller
         * @arg1[in] : deviceController
         * @arg2[out] : error data
         * @return : eARCONTROLLER_DEVICE_STATE
         */
        deviceState = ARCONTROLLER_Device_GetState (deviceController, &error);
        if ((error == ARCONTROLLER_OK) && (deviceState != ARCONTROLLER_DEVICE_STATE_STOPPED))   //  No error AND device running
        {
            IHM_PrintInfo(ihm, "Disconnecting ...");
            ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "Disconnecting ...");

            error = ARCONTROLLER_Device_Stop (deviceController);    //  Take a stop process

            if (error == ARCONTROLLER_OK)   //  No error
            {
                // wait state update update
                ARSAL_Sem_Wait (&(stateSem));   //  Make a Semaphore to wait
            }
        }

        IHM_PrintInfo(ihm, "");
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "ARCONTROLLER_Device_Delete ...");
        ARCONTROLLER_Device_Delete (&deviceController);

        /**
         * Close Mplayer
         */
        if (DISPLAY_WITH_MPLAYER)
        {
            fflush (videoOut);
            fclose (videoOut);

            if (child > 0)
            {
                kill(child, SIGKILL);
            }
        }
    }

    /**
     * Destroy the semaphore
     */
    ARSAL_Sem_Destroy (&(stateSem));

    /**
     * Destroy the FIFO
     */
    unlink(fifo_name);
    rmdir(fifo_dir);

    ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "-- END --");

    return EXIT_SUCCESS;
}
// end of main

/*****************************************
 *
 *             private implementation:
 *
 ****************************************/

// called when the state of the device controller has changed
/*
changed as input
*/
void stateChanged (eARCONTROLLER_DEVICE_STATE newState, eARCONTROLLER_ERROR error, void *customData)
{
    ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "    - stateChanged newState: %d .....", newState);

    switch (newState)
    {
    case ARCONTROLLER_DEVICE_STATE_STOPPED: //  Device stop
        ARSAL_Sem_Post (&(stateSem));   //  Increment a semaphore
        //stop
        gIHMRun = 0;    //  Same as SIGINT and SIGPIPE; Stop the loop

        break;

    case ARCONTROLLER_DEVICE_STATE_RUNNING:
        ARSAL_Sem_Post (&(stateSem));   //  Increment a semaphore
        break;

    default:
        break;
    }
}

// called when a command has been received from the drone
// DRONE -> Raspi
void commandReceived (eARCONTROLLER_DICTIONARY_KEY commandKey, ARCONTROLLER_DICTIONARY_ELEMENT_t *elementDictionary, void *customData)
{
    ARCONTROLLER_Device_t *deviceController = customData;

    if (deviceController != NULL)
    {
        /******
         * MY PLAYGROUND
         *//*
        if ((commandKey == ARCONTROLLER_DICTIONARY_KEY_COMMON_SETTINGSSTATE_AUTOCOUNTRYCHANGED) && (elementDictionary != NULL))
        {
            ARCONTROLLER_DICTIONARY_ARG_t *arg = NULL;
            ARCONTROLLER_DICTIONARY_ELEMENT_t *element = NULL;
            HASH_FIND_STR (elementDictionary, ARCONTROLLER_DICTIONARY_SINGLE_KEY, element);
            if (element != NULL)
            {
                HASH_FIND_STR (element->arguments, ARCONTROLLER_DICTIONARY_KEY_COMMON_SETTINGSSTATE_AUTOCOUNTRYCHANGED_AUTOMATIC, arg);
                if (arg != NULL)
                {
                    uint8_t automatic = arg->value.U8;
                    deviceController->common->sendSettingsAutoCountry(deviceController->common, (uint8_t)automatic);
                    IHM_PrintInfo(ihm, automatic);
                } else {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "arg is NULL");
                }
            } else {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "element is NULL");
            }
        }*/
        /****
         * END of PLAYGROUND
         */
        // if the command received is a battery state changed
        if (commandKey == ARCONTROLLER_DICTIONARY_KEY_COMMON_COMMONSTATE_BATTERYSTATECHANGED)
        {
            ARCONTROLLER_DICTIONARY_ARG_t *arg = NULL;
            ARCONTROLLER_DICTIONARY_ELEMENT_t *singleElement = NULL;

            if (elementDictionary != NULL)
            {
                // get the command received in the device controller
                HASH_FIND_STR (elementDictionary, ARCONTROLLER_DICTIONARY_SINGLE_KEY, singleElement);

                if (singleElement != NULL)
                {
                    // get the value
                    HASH_FIND_STR (singleElement->arguments, ARCONTROLLER_DICTIONARY_KEY_COMMON_COMMONSTATE_BATTERYSTATECHANGED_PERCENT, arg);

                    if (arg != NULL)
                    {
                        // update UI
                        batteryStateChanged (arg->value.U8);
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "arg is NULL");
                    }
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "singleElement is NULL");
                }
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "elements is NULL");
            }
        }

        if ((commandKey == ARCONTROLLER_DICTIONARY_KEY_COMMON_WIFISETTINGSSTATE_OUTDOORSETTINGSCHANGED) && (elementDictionary != NULL))
        {
            ARCONTROLLER_DICTIONARY_ARG_t *arg = NULL;
            ARCONTROLLER_DICTIONARY_ELEMENT_t *element = NULL;
            HASH_FIND_STR (elementDictionary, ARCONTROLLER_DICTIONARY_SINGLE_KEY, element);
            if (element != NULL)
            {
                HASH_FIND_STR (element->arguments, ARCONTROLLER_DICTIONARY_KEY_COMMON_WIFISETTINGSSTATE_OUTDOORSETTINGSCHANGED_OUTDOOR, arg);
                if (arg != NULL)
                {
                    uint8_t outdoor = arg->value.U8;
                    if(outdoor != 1){
                        deviceController->common->sendWifiSettingsOutdoorSetting(deviceController->common, (uint8_t)outdoor);
                        IHM_PrintValue(ihm, "INDOOR", outdoor);
                    }
                    IHM_PrintValue(ihm, "OUTDOOR", outdoor);
                }
            }
        }
    }

    if (commandKey == ARCONTROLLER_DICTIONARY_KEY_COMMON_COMMONSTATE_SENSORSSTATESLISTCHANGED)
    {
        ARCONTROLLER_DICTIONARY_ARG_t *arg = NULL;

        if (elementDictionary != NULL)
        {
            ARCONTROLLER_DICTIONARY_ELEMENT_t *dictElement = NULL;
            ARCONTROLLER_DICTIONARY_ELEMENT_t *dictTmp = NULL;

            eARCOMMANDS_COMMON_COMMONSTATE_SENSORSSTATESLISTCHANGED_SENSORNAME sensorName = ARCOMMANDS_COMMON_COMMONSTATE_SENSORSSTATESLISTCHANGED_SENSORNAME_MAX;
            int sensorState = 0;

            HASH_ITER(hh, elementDictionary, dictElement, dictTmp)
            {
                // get the Name
                HASH_FIND_STR (dictElement->arguments, ARCONTROLLER_DICTIONARY_KEY_COMMON_COMMONSTATE_SENSORSSTATESLISTCHANGED_SENSORNAME, arg);
                if (arg != NULL)
                {
                    sensorName = arg->value.I32;
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "arg sensorName is NULL");
                }

                // get the state
                HASH_FIND_STR (dictElement->arguments, ARCONTROLLER_DICTIONARY_KEY_COMMON_COMMONSTATE_SENSORSSTATESLISTCHANGED_SENSORSTATE, arg);
                if (arg != NULL)
                {
                    sensorState = arg->value.U8;

                    ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "sensorName %d ; sensorState: %d", sensorName, sensorState);
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "arg sensorState is NULL");
                }
            }
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "elements is NULL");
        }
    }
}

void batteryStateChanged (uint8_t percent)
{
    // callback of changing of battery level

    if (ihm != NULL)
    {
        IHM_PrintBattery (ihm, percent);
    }

}

eARCONTROLLER_ERROR decoderConfigCallback (ARCONTROLLER_Stream_Codec_t codec, void *customData)
{
    if (videoOut != NULL)
    {
        if (codec.type == ARCONTROLLER_STREAM_CODEC_TYPE_H264)
        {
            if (DISPLAY_WITH_MPLAYER)
            {
                fwrite(codec.parameters.h264parameters.spsBuffer, codec.parameters.h264parameters.spsSize, 1, videoOut);
                fwrite(codec.parameters.h264parameters.ppsBuffer, codec.parameters.h264parameters.ppsSize, 1, videoOut);

                fflush (videoOut);
            }
        }

    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, TAG, "videoOut is NULL.");
    }

    return ARCONTROLLER_OK;
}


eARCONTROLLER_ERROR didReceiveFrameCallback (ARCONTROLLER_Frame_t *frame, void *customData)
{
    if (videoOut != NULL)
    {
        if (frame != NULL)
        {
            if (DISPLAY_WITH_MPLAYER)
            {
                fwrite(frame->data, frame->used, 1, videoOut);

                fflush (videoOut);
            }
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, TAG, "frame is NULL.");
        }
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, TAG, "videoOut is NULL.");
    }

    return ARCONTROLLER_OK;
}


// IHM callbacks:
/***
 * Key 이벤트 발생시 IHM.c의 IHM_InputProcessing FUNC에서 해당 키에 맞는 파라미터를
 * 이 FUNC의 arg1으로 전달
 * 이 FUNC에서는 해당 키에 맞는 FUNC호출
 * 결과적으로 이 FUNC에서 Drone으로 이벤트 전달 수행
 */
void onInputEvent (eIHM_INPUT_EVENT event, void *customData)
{
    // Manage IHM input events
    ARCONTROLLER_Device_t *deviceController = (ARCONTROLLER_Device_t *)customData;
    eARCONTROLLER_ERROR error = ARCONTROLLER_OK;

    switch (event)
    {
    case IHM_INPUT_EVENT_EXIT:
        IHM_PrintInfo(ihm, "IHM_INPUT_EVENT_EXIT ...");
        gIHMRun = 0;
        break;
    case IHM_INPUT_EVENT_EMERGENCY:
        if(deviceController != NULL)
        {
            // send a Emergency command to the drone
            error = deviceController->aRDrone3->sendPilotingEmergency(deviceController->aRDrone3);
        }
        break;
    case IHM_INPUT_EVENT_LAND:
        if(deviceController != NULL)
        {
            // send a takeoff command to the drone
            error = deviceController->aRDrone3->sendPilotingLanding(deviceController->aRDrone3);
        }
        break;
    case IHM_INPUT_EVENT_TAKEOFF:
        if(deviceController != NULL)
        {
            // send a landing command to the drone
            error = deviceController->aRDrone3->sendPilotingTakeOff(deviceController->aRDrone3);
        }
        break;
    case IHM_INPUT_EVENT_UP:
        if(deviceController != NULL)
        {
            // set the flag and speed value of the piloting command
            error = deviceController->aRDrone3->setPilotingPCMDGaz(deviceController->aRDrone3, 50);
        }
        break;
    case IHM_INPUT_EVENT_DOWN:
        if(deviceController != NULL)
        {
            error = deviceController->aRDrone3->setPilotingPCMDGaz(deviceController->aRDrone3, -50);
        }
        break;
    case IHM_INPUT_EVENT_RIGHT:
        if(deviceController != NULL)
        {
            error = deviceController->aRDrone3->setPilotingPCMDYaw(deviceController->aRDrone3, 50);
        }
        break;
    case IHM_INPUT_EVENT_LEFT:
        if(deviceController != NULL)
        {
            error = deviceController->aRDrone3->setPilotingPCMDYaw(deviceController->aRDrone3, -50);
        }
        break;
    case IHM_INPUT_EVENT_FORWARD:
        if(deviceController != NULL)
        {
            error = deviceController->aRDrone3->setPilotingPCMDPitch(deviceController->aRDrone3, 50);
            error = deviceController->aRDrone3->setPilotingPCMDFlag(deviceController->aRDrone3, 1);
        }
        break;
    case IHM_INPUT_EVENT_BACK:
        if(deviceController != NULL)
        {
            error = deviceController->aRDrone3->setPilotingPCMDPitch(deviceController->aRDrone3, -50);
            error = deviceController->aRDrone3->setPilotingPCMDFlag(deviceController->aRDrone3, 1);
        }
        break;
    case IHM_INPUT_EVENT_ROLL_LEFT:
        if(deviceController != NULL)
        {
            error = deviceController->aRDrone3->setPilotingPCMDRoll(deviceController->aRDrone3, -50);
            error = deviceController->aRDrone3->setPilotingPCMDFlag(deviceController->aRDrone3, 1);
        }
        break;
    case IHM_INPUT_EVENT_ROLL_RIGHT:
        if(deviceController != NULL)
        {
            error = deviceController->aRDrone3->setPilotingPCMDRoll(deviceController->aRDrone3, 50);
            error = deviceController->aRDrone3->setPilotingPCMDFlag(deviceController->aRDrone3, 1);
        }
        break;
    case IHM_INPUT_EVENT_NONE:
        if(deviceController != NULL)
        {
            error = deviceController->aRDrone3->setPilotingPCMD(deviceController->aRDrone3, 0, 0, 0, 0, 0, 0);
        }
        break;
    default:
        break;
    }

    // This should be improved, here it just displays that one error occured
    if (error != ARCONTROLLER_OK)
    {
        IHM_PrintInfo(ihm, "Error sending an event");
    }
}

int customPrintCallback (eARSAL_PRINT_LEVEL level, const char *tag, const char *format, va_list va)
{
    // Custom callback used when ncurses is runing for not disturb the IHM

    if ((level == ARSAL_PRINT_ERROR) && (strcmp(TAG, tag) == 0))
    {
        // Save the last Error
        vsnprintf(gErrorStr, (ERROR_STR_LENGTH - 1), format, va);
        gErrorStr[ERROR_STR_LENGTH - 1] = '\0';
    }

    return 1;
}
