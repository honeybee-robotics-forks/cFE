/*
**  GSC-18128-1, "Core Flight Executive Version 6.7"
**
**  Copyright (c) 2006-2019 United States Government as represented by
**  the Administrator of the National Aeronautics and Space Administration.
**  All Rights Reserved.
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**    http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
*/

/*
** File: cfe_time_task.c
**
** Subsystem: cFE TIME Task
**
** Author: S. Walling (Microtel)
**
** Notes:
**
*/

/*
** Required header files...
*/
#include "cfe_time_module_all.h"
#include "cfe_version.h"
#include "cfe_time_verify.h"

/*
** Time task global data...
*/
CFE_TIME_Global_t CFE_TIME_Global;

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_EarlyInit
 *
 * Implemented per public API
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_EarlyInit(void)
{
    /*
    ** Initialize global Time Services nonzero data...
    */
    CFE_TIME_InitData();

    return (CFE_SUCCESS);
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_TaskMain
 *
 * Implemented per public API
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
void CFE_TIME_TaskMain(void)
{
    int32            Status;
    CFE_SB_Buffer_t *SBBufPtr;

    CFE_ES_PerfLogEntry(CFE_MISSION_TIME_MAIN_PERF_ID);

    Status = CFE_TIME_TaskInit();

    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Application Init Failed,RC=0x%08X\n", __func__, (unsigned int)Status);
        CFE_ES_PerfLogExit(CFE_MISSION_TIME_MAIN_PERF_ID);
        /* Note: CFE_ES_ExitApp will not return */
        CFE_ES_ExitApp(CFE_ES_RunStatus_CORE_APP_INIT_ERROR);
    } /* end if */

    /*
     * Wait for other apps to start.
     * It is important that the core apps are present before this starts receiving
     * messages from the command pipe, as some of those handlers might depend on
     * the other core apps.
     */
    CFE_ES_WaitForSystemState(CFE_ES_SystemState_CORE_READY, CFE_PLATFORM_CORE_MAX_STARTUP_MSEC);

    /* Main loop */
    while (Status == CFE_SUCCESS)
    {

        /* Increment the Main task Execution Counter */
        CFE_ES_IncrementTaskCounter();

        CFE_ES_PerfLogExit(CFE_MISSION_TIME_MAIN_PERF_ID);

        /* Pend on receipt of packet */
        Status = CFE_SB_ReceiveBuffer(&SBBufPtr, CFE_TIME_Global.CmdPipe, CFE_SB_PEND_FOREVER);

        CFE_ES_PerfLogEntry(CFE_MISSION_TIME_MAIN_PERF_ID);

        if (Status == CFE_SUCCESS)
        {
            /* Process cmd pipe msg */
            CFE_TIME_TaskPipe(SBBufPtr);
        }
        else
        {
            CFE_ES_WriteToSysLog("%s: Error reading cmd pipe,RC=0x%08X\n", __func__, (unsigned int)Status);
        } /* end if */

    } /* end while */

    /* while loop exits only if CFE_SB_ReceiveBuffer returns error */
    CFE_ES_ExitApp(CFE_ES_RunStatus_CORE_APP_RUNTIME_ERROR);
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_TaskInit
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_TaskInit(void)
{
    int32     Status;
    osal_id_t TimeBaseId;
    osal_id_t TimerId;

    Status = CFE_EVS_Register(NULL, 0, 0);
    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Call to CFE_EVS_Register Failed:RC=0x%08X\n", __func__, (unsigned int)Status);
        return Status;
    } /* end if */

    Status = OS_BinSemCreate(&CFE_TIME_Global.ToneSemaphore, CFE_TIME_SEM_TONE_NAME, CFE_TIME_SEM_VALUE,
                             CFE_TIME_SEM_OPTIONS);
    if (Status != OS_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error creating tone semaphore:RC=0x%08X\n", __func__, (unsigned int)Status);
        return Status;
    } /* end if */

    Status = OS_BinSemCreate(&CFE_TIME_Global.LocalSemaphore, CFE_TIME_SEM_1HZ_NAME, CFE_TIME_SEM_VALUE,
                             CFE_TIME_SEM_OPTIONS);
    if (Status != OS_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error creating local semaphore:RC=0x%08X\n", __func__, (unsigned int)Status);
        return Status;
    } /* end if */

    Status = CFE_ES_CreateChildTask(&CFE_TIME_Global.ToneTaskID, CFE_TIME_TASK_TONE_NAME, CFE_TIME_Tone1HzTask,
                                    CFE_TIME_TASK_STACK_PTR, CFE_PLATFORM_TIME_TONE_TASK_STACK_SIZE,
                                    CFE_PLATFORM_TIME_TONE_TASK_PRIORITY, CFE_TIME_TASK_FLAGS);
    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error creating tone 1Hz child task:RC=0x%08X\n", __func__, (unsigned int)Status);
        return Status;
    } /* end if */

    Status = CFE_ES_CreateChildTask(&CFE_TIME_Global.LocalTaskID, CFE_TIME_TASK_1HZ_NAME, CFE_TIME_Local1HzTask,
                                    CFE_TIME_TASK_STACK_PTR, CFE_PLATFORM_TIME_1HZ_TASK_STACK_SIZE,
                                    CFE_PLATFORM_TIME_1HZ_TASK_PRIORITY, CFE_TIME_TASK_FLAGS);
    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error creating local 1Hz child task:RC=0x%08X\n", __func__, (unsigned int)Status);
        return Status;
    } /* end if */

    Status = CFE_SB_CreatePipe(&CFE_TIME_Global.CmdPipe, CFE_TIME_TASK_PIPE_DEPTH, CFE_TIME_TASK_PIPE_NAME);
    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error creating cmd pipe:RC=0x%08X\n", __func__, (unsigned int)Status);
        return Status;
    } /* end if */

    Status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(CFE_TIME_SEND_HK_MID), CFE_TIME_Global.CmdPipe);
    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error subscribing to HK Request:RC=0x%08X\n", __func__, (unsigned int)Status);
        return Status;
    } /* end if */

/*
** Subscribe to time at the tone "signal" commands...
*/
#if (CFE_PLATFORM_TIME_CFG_CLIENT == true)
    Status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(CFE_TIME_TONE_CMD_MID), CFE_TIME_Global.CmdPipe);
#endif

#if (CFE_PLATFORM_TIME_CFG_SERVER == true)
    Status = CFE_SB_SubscribeLocal(CFE_SB_ValueToMsgId(CFE_TIME_TONE_CMD_MID), CFE_TIME_Global.CmdPipe, 4);
#endif
    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error subscribing to tone cmd:RC=0x%08X\n", __func__, (unsigned int)Status);
        return Status;
    } /* end if */

/*
** Subscribe to time at the tone "data" commands...
*/
#if (CFE_PLATFORM_TIME_CFG_CLIENT == true)
    Status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(CFE_TIME_DATA_CMD_MID), CFE_TIME_Global.CmdPipe);
#endif

#if (CFE_PLATFORM_TIME_CFG_SERVER == true)
    Status = CFE_SB_SubscribeLocal(CFE_SB_ValueToMsgId(CFE_TIME_DATA_CMD_MID), CFE_TIME_Global.CmdPipe, 4);
#endif
    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error subscribing to time data cmd:RC=0x%08X\n", __func__, (unsigned int)Status);
        return Status;
    } /* end if */

/*
** Subscribe to 1Hz signal commands...
*/
#if (CFE_PLATFORM_TIME_CFG_CLIENT == true)
    Status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(CFE_TIME_1HZ_CMD_MID), CFE_TIME_Global.CmdPipe);
#endif

#if (CFE_PLATFORM_TIME_CFG_SERVER == true)
    Status = CFE_SB_SubscribeLocal(CFE_SB_ValueToMsgId(CFE_TIME_1HZ_CMD_MID), CFE_TIME_Global.CmdPipe, 4);
#endif

    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error subscribing to fake tone signal cmds:RC=0x%08X\n", __func__,
                             (unsigned int)Status);
        return Status;
    } /* end if */

/*
** Subscribe to time at the tone "request data" commands...
*/
#if (CFE_PLATFORM_TIME_CFG_SERVER == true)
    Status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(CFE_TIME_SEND_CMD_MID), CFE_TIME_Global.CmdPipe);
    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error subscribing to time at the tone request data cmds:RC=0x%08X\n", __func__,
                             (unsigned int)Status);
        return Status;
    } /* end if */
#endif

    /*
    ** Subscribe to Time task ground command packets...
    */
    Status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(CFE_TIME_CMD_MID), CFE_TIME_Global.CmdPipe);
    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error subscribing to time task gnd cmds:RC=0x%08X\n", __func__, (unsigned int)Status);
        return Status;
    } /* end if */

    Status = CFE_EVS_SendEvent(CFE_TIME_INIT_EID, CFE_EVS_EventType_INFORMATION, "cFE TIME Initialized: %s",
                               CFE_VERSION_STRING);
    if (Status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("%s: Error sending init event:RC=0x%08X\n", __func__, (unsigned int)Status);
        return Status;
    } /* end if */

/*
** Select primary vs redundant tone interrupt signal...
*/
#if (CFE_PLATFORM_TIME_CFG_SIGNAL == true)
    OS_SelectTone(CFE_TIME_Global.ClockSignal);
#endif

    /*
     * Check to see if the OSAL in use implements the TimeBase API
     * and if the PSP has set up a system time base.  If so, then create
     * a 1Hz callback based on that system time base.  This call should
     * return OS_ERR_NOT_IMPLEMENTED if the OSAL does not support this,
     * or OS_ERR_NAME_NOT_FOUND if the PSP didn't set this up.  Either
     * way any error here means the PSP must use the "old way" and call
     * the 1hz function directly.
     */
    Status = OS_TimeBaseGetIdByName(&TimeBaseId, "cFS-Master");
    if (Status == OS_SUCCESS)
    {
        /* Create the 1Hz callback */
        Status = OS_TimerAdd(&TimerId, "cFS-1Hz", TimeBaseId, CFE_TIME_Local1HzTimerCallback, NULL);
        if (Status == OS_SUCCESS)
        {
            Status = OS_TimerSet(TimerId, 500000, 1000000);
            if (Status != OS_SUCCESS)
            {
                CFE_ES_WriteToSysLog("%s: 1Hz OS_TimerSet failed:RC=0x%08X\n", __func__, (unsigned int)Status);
            }
        }
        else
        {
            CFE_ES_WriteToSysLog("%s: 1Hz OS_TimerAdd failed:RC=0x%08X\n", __func__, (unsigned int)Status);
        }
    }

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_VerifyCmdLength
 *
 * Internal helper routine only, not part of API.
 *
 * Function to verify the length of incoming TIME command packets
 *
 *-----------------------------------------------------------------*/
bool CFE_TIME_VerifyCmdLength(CFE_MSG_Message_t *MsgPtr, size_t ExpectedLength)
{
    bool              result       = true;
    CFE_MSG_Size_t    ActualLength = 0;
    CFE_MSG_FcnCode_t FcnCode      = 0;
    CFE_SB_MsgId_t    MsgId        = CFE_SB_INVALID_MSG_ID;

    CFE_MSG_GetSize(MsgPtr, &ActualLength);

    /*
    ** Verify the command packet length
    */
    if (ExpectedLength != ActualLength)
    {
        CFE_MSG_GetMsgId(MsgPtr, &MsgId);
        CFE_MSG_GetFcnCode(MsgPtr, &FcnCode);

        CFE_EVS_SendEvent(CFE_TIME_LEN_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Invalid msg length: ID = 0x%X,  CC = %u, Len = %u, Expected = %u",
                          (unsigned int)CFE_SB_MsgIdToValue(MsgId), (unsigned int)FcnCode, (unsigned int)ActualLength,
                          (unsigned int)ExpectedLength);
        result = false;
        ++CFE_TIME_Global.CommandErrorCounter;
    }

    return (result);
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_TaskPipe
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
void CFE_TIME_TaskPipe(CFE_SB_Buffer_t *SBBufPtr)
{
    CFE_SB_MsgId_t    MessageID   = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;

    CFE_MSG_GetMsgId(&SBBufPtr->Msg, &MessageID);

    switch (CFE_SB_MsgIdToValue(MessageID))
    {
        /*
        ** Housekeeping telemetry request...
        */
        case CFE_TIME_SEND_HK_MID:
            CFE_TIME_HousekeepingCmd((CFE_MSG_CommandHeader_t *)SBBufPtr);
            break;

        /*
        ** Time at the tone "signal"...
        */
        case CFE_TIME_TONE_CMD_MID:
            CFE_TIME_ToneSignalCmd((CFE_TIME_ToneSignalCmd_t *)SBBufPtr);
            break;

        /*
        ** Time at the tone "data"...
        */
        case CFE_TIME_DATA_CMD_MID:
            CFE_TIME_ToneDataCmd((CFE_TIME_ToneDataCmd_t *)SBBufPtr);
            break;

        /*
        ** Run time state machine at 1Hz...
        */
        case CFE_TIME_1HZ_CMD_MID:
            CFE_TIME_OneHzCmd((CFE_TIME_1HzCmd_t *)SBBufPtr);
            break;

/*
** Request for time at the tone "data"...
*/
#if (CFE_PLATFORM_TIME_CFG_SERVER == true)
        case CFE_TIME_SEND_CMD_MID:
            CFE_TIME_ToneSendCmd((CFE_TIME_FakeToneCmd_t *)SBBufPtr);
            break;
#endif

        /*
        ** Time task ground commands...
        */
        case CFE_TIME_CMD_MID:

            CFE_MSG_GetFcnCode(&SBBufPtr->Msg, &CommandCode);
            switch (CommandCode)
            {
                case CFE_TIME_NOOP_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_NoopCmd_t)))
                    {
                        CFE_TIME_NoopCmd((CFE_TIME_NoopCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_RESET_COUNTERS_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_ResetCountersCmd_t)))
                    {
                        CFE_TIME_ResetCountersCmd((CFE_TIME_ResetCountersCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_SEND_DIAGNOSTIC_TLM_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_SendDiagnosticCmd_t)))
                    {
                        CFE_TIME_SendDiagnosticTlm((CFE_TIME_SendDiagnosticCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_SET_STATE_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_SetStateCmd_t)))
                    {
                        CFE_TIME_SetStateCmd((CFE_TIME_SetStateCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_SET_SOURCE_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_SetSourceCmd_t)))
                    {
                        CFE_TIME_SetSourceCmd((CFE_TIME_SetSourceCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_SET_SIGNAL_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_SetSignalCmd_t)))
                    {
                        CFE_TIME_SetSignalCmd((CFE_TIME_SetSignalCmd_t *)SBBufPtr);
                    }
                    break;

                /*
                ** Time Clients process "tone delay" commands...
                */
                case CFE_TIME_ADD_DELAY_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_AddDelayCmd_t)))
                    {
                        CFE_TIME_AddDelayCmd((CFE_TIME_AddDelayCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_SUB_DELAY_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_SubDelayCmd_t)))
                    {
                        CFE_TIME_SubDelayCmd((CFE_TIME_SubDelayCmd_t *)SBBufPtr);
                    }
                    break;

                /*
                ** Time Servers process "set time" commands...
                */
                case CFE_TIME_SET_TIME_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_SetTimeCmd_t)))
                    {
                        CFE_TIME_SetTimeCmd((CFE_TIME_SetTimeCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_SET_MET_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_SetMETCmd_t)))
                    {
                        CFE_TIME_SetMETCmd((CFE_TIME_SetMETCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_SET_STCF_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_SetSTCFCmd_t)))
                    {
                        CFE_TIME_SetSTCFCmd((CFE_TIME_SetSTCFCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_SET_LEAP_SECONDS_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_SetLeapSecondsCmd_t)))
                    {
                        CFE_TIME_SetLeapSecondsCmd((CFE_TIME_SetLeapSecondsCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_ADD_ADJUST_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_AddAdjustCmd_t)))
                    {
                        CFE_TIME_AddAdjustCmd((CFE_TIME_AddAdjustCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_SUB_ADJUST_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_SubAdjustCmd_t)))
                    {
                        CFE_TIME_SubAdjustCmd((CFE_TIME_SubAdjustCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_ADD_1HZ_ADJUSTMENT_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_Add1HZAdjustmentCmd_t)))
                    {
                        CFE_TIME_Add1HZAdjustmentCmd((CFE_TIME_Add1HZAdjustmentCmd_t *)SBBufPtr);
                    }
                    break;

                case CFE_TIME_SUB_1HZ_ADJUSTMENT_CC:
                    if (CFE_TIME_VerifyCmdLength(&SBBufPtr->Msg, sizeof(CFE_TIME_Sub1HZAdjustmentCmd_t)))
                    {
                        CFE_TIME_Sub1HZAdjustmentCmd((CFE_TIME_Sub1HZAdjustmentCmd_t *)SBBufPtr);
                    }
                    break;

                default:

                    CFE_TIME_Global.CommandErrorCounter++;
                    CFE_EVS_SendEvent(CFE_TIME_CC_ERR_EID, CFE_EVS_EventType_ERROR,
                                      "Invalid command code -- ID = 0x%X, CC = %d",
                                      (unsigned int)CFE_SB_MsgIdToValue(MessageID), (int)CommandCode);
                    break;
            } /* switch (CFE_TIME_CMD_MID -- command code)*/
            break;

        default:

            /*
            ** Note: we only increment the command error counter when
            **    processing CFE_TIME_CMD_MID commands...
            */
            CFE_EVS_SendEvent(CFE_TIME_ID_ERR_EID, CFE_EVS_EventType_ERROR, "Invalid message ID -- ID = 0x%X",
                              (unsigned int)CFE_SB_MsgIdToValue(MessageID));
            break;

    } /* switch (message ID) */

    return;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_HousekeepingCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_HousekeepingCmd(const CFE_MSG_CommandHeader_t *data)
{
    CFE_TIME_Reference_t Reference;

    /*
    ** Get reference time values (local time, time at tone, etc.)...
    */
    CFE_TIME_GetReference(&Reference);

    /*
    ** Update TIME portion of Critical Data Store...
    */
    CFE_TIME_UpdateResetVars(&Reference);

    /*
    ** Collect housekeeping data from Time Services utilities...
    */
    CFE_TIME_GetHkData(&Reference);

    /*
    ** Send housekeeping telemetry packet...
    */
    CFE_SB_TimeStampMsg(&CFE_TIME_Global.HkPacket.TlmHeader.Msg);
    CFE_SB_TransmitMsg(&CFE_TIME_Global.HkPacket.TlmHeader.Msg, true);

    /*
    ** Note: we only increment the command execution counter when
    **   processing CFE_TIME_CMD_MID commands...
    */
    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_ToneSignalCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_ToneSignalCmd(const CFE_TIME_ToneSignalCmd_t *data)
{
    /*
    ** Indication that tone signal occurred recently...
    */
    CFE_TIME_ToneSignal();

    /*
    ** Note: we only increment the command execution counter when
    **   processing CFE_TIME_CMD_MID commands...
    */
    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_ToneDataCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_ToneDataCmd(const CFE_TIME_ToneDataCmd_t *data)
{
    /*
    ** This command packet contains "time at the tone" data...
    */
    CFE_TIME_ToneData(&data->Payload);

    /*
    ** Note: we only increment the command execution counter when
    **   processing CFE_TIME_CMD_MID commands...
    */
    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_OneHzCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_OneHzCmd(const CFE_TIME_1HzCmd_t *data)
{
    /*
     * Run the state machine updates required at 1Hz.
     *
     * This task used to be performed as part of the 1Hz ISR, but this was unsafe on SMP
     * as the updates cannot be synchronized with the command handlers in this environment
     */
    CFE_TIME_Local1HzStateMachine();

#if (CFE_MISSION_TIME_CFG_FAKE_TONE == true)
    /*
    ** Fake the call-back from the "real" h/w ISR...
    */
    CFE_TIME_Tone1HzISR();
#endif /* CFE_MISSION_TIME_CFG_FAKE_TONE */

    /*
    ** Note: we only increment the command execution counter when
    **   processing CFE_TIME_CMD_MID commands...
    */
    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_ToneSendCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
#if (CFE_PLATFORM_TIME_CFG_SERVER == true)
int32 CFE_TIME_ToneSendCmd(const CFE_TIME_FakeToneCmd_t *data)
{
    /*
    ** Request for "time at tone" data packet (probably scheduler)...
    */
    CFE_TIME_ToneSend();

    /*
    ** Note: we only increment the command execution counter when
    **   processing CFE_TIME_CMD_MID commands...
    */
    return CFE_SUCCESS;
}
#endif /* CFE_PLATFORM_TIME_CFG_SERVER */

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_NoopCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_NoopCmd(const CFE_TIME_NoopCmd_t *data)
{

    CFE_TIME_Global.CommandCounter++;

    CFE_EVS_SendEvent(CFE_TIME_NOOP_EID, CFE_EVS_EventType_INFORMATION, "No-op Cmd Rcvd: %s", CFE_VERSION_STRING);

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_ResetCountersCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_ResetCountersCmd(const CFE_TIME_ResetCountersCmd_t *data)
{

    CFE_TIME_Global.CommandCounter      = 0;
    CFE_TIME_Global.CommandErrorCounter = 0;

    CFE_TIME_Global.ToneMatchCounter      = 0;
    CFE_TIME_Global.ToneMatchErrorCounter = 0;

    CFE_TIME_Global.ToneSignalCounter = 0;
    CFE_TIME_Global.ToneDataCounter   = 0;

    CFE_TIME_Global.ToneIntCounter      = 0;
    CFE_TIME_Global.ToneIntErrorCounter = 0;
    CFE_TIME_Global.ToneTaskCounter     = 0;

    /*
     * Note: Not resetting "LastVersion" counter here, that might
     * disturb access to the time reference data by other tasks
     */
    CFE_TIME_Global.ResetVersionCounter = CFE_TIME_Global.LastVersionCounter;

    CFE_TIME_Global.LocalIntCounter  = 0;
    CFE_TIME_Global.LocalTaskCounter = 0;

    CFE_TIME_Global.InternalCount = 0;
    CFE_TIME_Global.ExternalCount = 0;

    CFE_EVS_SendEvent(CFE_TIME_RESET_EID, CFE_EVS_EventType_DEBUG, "Reset Counters command");

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_SendDiagnosticTlm
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_SendDiagnosticTlm(const CFE_TIME_SendDiagnosticCmd_t *data)
{
    CFE_TIME_Global.CommandCounter++;

    /*
    ** Collect diagnostics data from Time Services utilities...
    */
    CFE_TIME_GetDiagData();

    /*
    ** Send diagnostics telemetry packet...
    */
    CFE_SB_TimeStampMsg(&CFE_TIME_Global.DiagPacket.TlmHeader.Msg);
    CFE_SB_TransmitMsg(&CFE_TIME_Global.DiagPacket.TlmHeader.Msg, true);

    CFE_EVS_SendEvent(CFE_TIME_DIAG_EID, CFE_EVS_EventType_DEBUG, "Request diagnostics command");

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_SetStateCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_SetStateCmd(const CFE_TIME_SetStateCmd_t *data)
{
    const CFE_TIME_StateCmd_Payload_t *CommandPtr = &data->Payload;
    const char *                       ClockStateText;

    /*
    ** Verify command argument value (clock state)...
    */
    if ((CommandPtr->ClockState == CFE_TIME_ClockState_INVALID) ||
        (CommandPtr->ClockState == CFE_TIME_ClockState_VALID) ||
        (CommandPtr->ClockState == CFE_TIME_ClockState_FLYWHEEL))
    {
        CFE_TIME_SetState(CommandPtr->ClockState);

        /*
        ** Select appropriate text for event message...
        */
        if (CommandPtr->ClockState == CFE_TIME_ClockState_INVALID)
        {
            ClockStateText = "INVALID";
        }
        else if (CommandPtr->ClockState == CFE_TIME_ClockState_VALID)
        {
            ClockStateText = "VALID";
        }
        else
        {
            ClockStateText = "FLYWHEEL";
        }

        CFE_TIME_Global.CommandCounter++;
        CFE_EVS_SendEvent(CFE_TIME_STATE_EID, CFE_EVS_EventType_INFORMATION, "Set Clock State = %s", ClockStateText);
    }
    else
    {
        CFE_TIME_Global.CommandErrorCounter++;
        CFE_EVS_SendEvent(CFE_TIME_STATE_ERR_EID, CFE_EVS_EventType_ERROR, "Invalid Clock State = 0x%X",
                          (unsigned int)CommandPtr->ClockState);
    }

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_SetSourceCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_SetSourceCmd(const CFE_TIME_SetSourceCmd_t *data)
{
    const CFE_TIME_SourceCmd_Payload_t *CommandPtr = &data->Payload;

#if (CFE_PLATFORM_TIME_CFG_SOURCE == true)
    const char *TimeSourceText;
#endif

    /*
    ** Verify command argument value (time data source)...
    */
    if ((CommandPtr->TimeSource == CFE_TIME_SourceSelect_INTERNAL) ||
        (CommandPtr->TimeSource == CFE_TIME_SourceSelect_EXTERNAL))
    {
#if (CFE_PLATFORM_TIME_CFG_SOURCE == true)
        /*
        ** Only systems configured to select source of time data...
        */
        CFE_TIME_Global.CommandCounter++;

        CFE_TIME_SetSource(CommandPtr->TimeSource);

        /*
        ** Select appropriate text for event message...
        */
        if (CommandPtr->TimeSource == CFE_TIME_SourceSelect_INTERNAL)
        {
            TimeSourceText = "INTERNAL";
        }
        else
        {
            TimeSourceText = "EXTERNAL";
        }

        CFE_EVS_SendEvent(CFE_TIME_SOURCE_EID, CFE_EVS_EventType_INFORMATION, "Set Time Source = %s", TimeSourceText);

#else /* not CFE_PLATFORM_TIME_CFG_SOURCE */
        /*
        ** We want to know if disabled commands are being sent...
        */
        CFE_TIME_Global.CommandErrorCounter++;

        CFE_EVS_SendEvent(CFE_TIME_SOURCE_CFG_EID, CFE_EVS_EventType_ERROR,
                          "Set Source commands invalid without CFE_PLATFORM_TIME_CFG_SOURCE set to TRUE");

#endif /* CFE_PLATFORM_TIME_CFG_SOURCE */
    }
    else
    {
        /*
        ** Ground system database will prevent most of these errors...
        */
        CFE_TIME_Global.CommandErrorCounter++;

        CFE_EVS_SendEvent(CFE_TIME_SOURCE_ERR_EID, CFE_EVS_EventType_ERROR, "Invalid Time Source = 0x%X",
                          (unsigned int)CommandPtr->TimeSource);
    }

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_SetSignalCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_SetSignalCmd(const CFE_TIME_SetSignalCmd_t *data)
{
    const CFE_TIME_SignalCmd_Payload_t *CommandPtr = &data->Payload;

#if (CFE_PLATFORM_TIME_CFG_SIGNAL == true)
    const char *ToneSourceText;
#endif

    /*
    ** Verify command argument value (tone source)...
    */
    if ((CommandPtr->ToneSource == CFE_TIME_ToneSignalSelect_PRIMARY) ||
        (CommandPtr->ToneSource == CFE_TIME_ToneSignalSelect_REDUNDANT))
    {
#if (CFE_PLATFORM_TIME_CFG_SIGNAL == true)
        /*
        ** Only systems configured to select tone signal...
        */
        CFE_TIME_Global.CommandCounter++;

        CFE_TIME_SetSignal(CommandPtr->ToneSource);

        /*
        ** Select appropriate text for event message...
        */
        if (CommandPtr->ToneSource == CFE_TIME_ToneSignalSelect_PRIMARY)
        {
            ToneSourceText = "PRIMARY";
        }
        else
        {
            ToneSourceText = "REDUNDANT";
        }

        CFE_EVS_SendEvent(CFE_TIME_SIGNAL_EID, CFE_EVS_EventType_INFORMATION, "Set Tone Source = %s", ToneSourceText);

#else /* not CFE_PLATFORM_TIME_CFG_SIGNAL */
        /*
        ** We want to know if disabled commands are being sent...
        */
        CFE_TIME_Global.CommandErrorCounter++;

        CFE_EVS_SendEvent(CFE_TIME_SIGNAL_CFG_EID, CFE_EVS_EventType_ERROR,
                          "Set Signal commands invalid without CFE_PLATFORM_TIME_CFG_SIGNAL set to TRUE");

#endif /* CFE_PLATFORM_TIME_CFG_SIGNAL */
    }
    else
    {
        /*
        ** Ground system database will prevent most of these errors...
        */
        CFE_TIME_Global.CommandErrorCounter++;

        CFE_EVS_SendEvent(CFE_TIME_SIGNAL_ERR_EID, CFE_EVS_EventType_ERROR, "Invalid Tone Source = 0x%X",
                          (unsigned int)CommandPtr->ToneSource);
    }

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_SetDelayImpl
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
void CFE_TIME_SetDelayImpl(const CFE_TIME_TimeCmd_Payload_t *CommandPtr, CFE_TIME_AdjustDirection_Enum_t Direction)
{
    /*
    ** Verify "micro-seconds" command argument...
    */
    if (CommandPtr->MicroSeconds < 1000000)
    {
#if (CFE_PLATFORM_TIME_CFG_CLIENT == true)

        CFE_TIME_SysTime_t Delay;

        Delay.Seconds    = CommandPtr->Seconds;
        Delay.Subseconds = CFE_TIME_Micro2SubSecs(CommandPtr->MicroSeconds);

        CFE_TIME_SetDelay(Delay, Direction);

        CFE_TIME_Global.CommandCounter++;
        CFE_EVS_SendEvent(CFE_TIME_DELAY_EID, CFE_EVS_EventType_INFORMATION,
                          "Set Tone Delay -- secs = %u, usecs = %u, ssecs = 0x%X, dir = %d",
                          (unsigned int)CommandPtr->Seconds, (unsigned int)CommandPtr->MicroSeconds,
                          (unsigned int)CFE_TIME_Micro2SubSecs(CommandPtr->MicroSeconds), (int)Direction);

#else /* not CFE_PLATFORM_TIME_CFG_CLIENT */
        /*
        ** We want to know if disabled commands are being sent...
        */
        CFE_TIME_Global.CommandErrorCounter++;

        CFE_EVS_SendEvent(CFE_TIME_DELAY_CFG_EID, CFE_EVS_EventType_ERROR,
                          "Set Delay commands invalid without CFE_PLATFORM_TIME_CFG_CLIENT set to TRUE");

#endif /* CFE_PLATFORM_TIME_CFG_CLIENT */
    }
    else
    {
        CFE_TIME_Global.CommandErrorCounter++;
        CFE_EVS_SendEvent(CFE_TIME_DELAY_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Invalid Tone Delay -- secs = %u, usecs = %u", (unsigned int)CommandPtr->Seconds,
                          (unsigned int)CommandPtr->MicroSeconds);
    }
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_AddDelayCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_AddDelayCmd(const CFE_TIME_AddDelayCmd_t *data)
{
    CFE_TIME_SetDelayImpl(&data->Payload, CFE_TIME_AdjustDirection_ADD);
    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_SubDelayCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_SubDelayCmd(const CFE_TIME_SubDelayCmd_t *data)
{
    CFE_TIME_SetDelayImpl(&data->Payload, CFE_TIME_AdjustDirection_SUBTRACT);
    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_SetTimeCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_SetTimeCmd(const CFE_TIME_SetTimeCmd_t *data)
{
    const CFE_TIME_TimeCmd_Payload_t *CommandPtr = &data->Payload;

    /*
    ** Verify "micro-seconds" command argument...
    */
    if (CommandPtr->MicroSeconds < 1000000)
    {
#if (CFE_PLATFORM_TIME_CFG_SERVER == true)

        CFE_TIME_SysTime_t NewTime;

        NewTime.Seconds    = CommandPtr->Seconds;
        NewTime.Subseconds = CFE_TIME_Micro2SubSecs(CommandPtr->MicroSeconds);

        CFE_TIME_SetTime(NewTime);

        CFE_TIME_Global.CommandCounter++;
        CFE_EVS_SendEvent(CFE_TIME_TIME_EID, CFE_EVS_EventType_INFORMATION,
                          "Set Time -- secs = %u, usecs = %u, ssecs = 0x%X", (unsigned int)CommandPtr->Seconds,
                          (unsigned int)CommandPtr->MicroSeconds,
                          (unsigned int)CFE_TIME_Micro2SubSecs(CommandPtr->MicroSeconds));

#else /* not CFE_PLATFORM_TIME_CFG_SERVER */
        /*
        ** We want to know if disabled commands are being sent...
        */
        CFE_TIME_Global.CommandErrorCounter++;

        CFE_EVS_SendEvent(CFE_TIME_TIME_CFG_EID, CFE_EVS_EventType_ERROR,
                          "Set Time commands invalid without CFE_PLATFORM_TIME_CFG_SERVER set to TRUE");

#endif /* CFE_PLATFORM_TIME_CFG_SERVER */
    }
    else
    {
        CFE_TIME_Global.CommandErrorCounter++;
        CFE_EVS_SendEvent(CFE_TIME_TIME_ERR_EID, CFE_EVS_EventType_ERROR, "Invalid Time -- secs = %u, usecs = %u",
                          (unsigned int)CommandPtr->Seconds, (unsigned int)CommandPtr->MicroSeconds);
    }

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_SetMETCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_SetMETCmd(const CFE_TIME_SetMETCmd_t *data)
{
    const CFE_TIME_TimeCmd_Payload_t *CommandPtr = &data->Payload;

    /*
    ** Verify "micro-seconds" command argument...
    */
    if (CommandPtr->MicroSeconds < 1000000)
    {
#if (CFE_PLATFORM_TIME_CFG_SERVER == true)

        CFE_TIME_SysTime_t NewMET;

        NewMET.Seconds    = CommandPtr->Seconds;
        NewMET.Subseconds = CFE_TIME_Micro2SubSecs(CommandPtr->MicroSeconds);

        CFE_TIME_SetMET(NewMET);

        CFE_TIME_Global.CommandCounter++;
        CFE_EVS_SendEvent(CFE_TIME_MET_EID, CFE_EVS_EventType_INFORMATION,
                          "Set MET -- secs = %u, usecs = %u, ssecs = 0x%X", (unsigned int)CommandPtr->Seconds,
                          (unsigned int)CommandPtr->MicroSeconds,
                          (unsigned int)CFE_TIME_Micro2SubSecs(CommandPtr->MicroSeconds));

#else /* not CFE_PLATFORM_TIME_CFG_SERVER */
        /*
        ** We want to know if disabled commands are being sent...
        */
        CFE_TIME_Global.CommandErrorCounter++;

        CFE_EVS_SendEvent(CFE_TIME_MET_CFG_EID, CFE_EVS_EventType_ERROR,
                          "Set MET commands invalid without CFE_PLATFORM_TIME_CFG_SERVER set to TRUE");

#endif /* CFE_PLATFORM_TIME_CFG_SERVER */
    }
    else
    {
        CFE_TIME_Global.CommandErrorCounter++;
        CFE_EVS_SendEvent(CFE_TIME_MET_ERR_EID, CFE_EVS_EventType_ERROR, "Invalid MET -- secs = %u, usecs = %u",
                          (unsigned int)CommandPtr->Seconds, (unsigned int)CommandPtr->MicroSeconds);
    }

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_SetSTCFCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_SetSTCFCmd(const CFE_TIME_SetSTCFCmd_t *data)
{
    const CFE_TIME_TimeCmd_Payload_t *CommandPtr = &data->Payload;

    /*
    ** Verify "micro-seconds" command argument...
    */
    if (CommandPtr->MicroSeconds < 1000000)
    {
#if (CFE_PLATFORM_TIME_CFG_SERVER == true)

        CFE_TIME_SysTime_t NewSTCF;

        NewSTCF.Seconds    = CommandPtr->Seconds;
        NewSTCF.Subseconds = CFE_TIME_Micro2SubSecs(CommandPtr->MicroSeconds);

        CFE_TIME_SetSTCF(NewSTCF);

        CFE_TIME_Global.CommandCounter++;
        CFE_EVS_SendEvent(CFE_TIME_STCF_EID, CFE_EVS_EventType_INFORMATION,
                          "Set STCF -- secs = %u, usecs = %u, ssecs = 0x%X", (unsigned int)CommandPtr->Seconds,
                          (unsigned int)CommandPtr->MicroSeconds,
                          (unsigned int)CFE_TIME_Micro2SubSecs(CommandPtr->MicroSeconds));

#else /* not CFE_PLATFORM_TIME_CFG_SERVER */
        /*
        ** We want to know if disabled commands are being sent...
        */
        CFE_TIME_Global.CommandErrorCounter++;

        CFE_EVS_SendEvent(CFE_TIME_STCF_CFG_EID, CFE_EVS_EventType_ERROR,
                          "Set STCF commands invalid without CFE_PLATFORM_TIME_CFG_SERVER set to TRUE");

#endif /* CFE_PLATFORM_TIME_CFG_SERVER */
    }
    else
    {
        CFE_TIME_Global.CommandErrorCounter++;
        CFE_EVS_SendEvent(CFE_TIME_STCF_ERR_EID, CFE_EVS_EventType_ERROR, "Invalid STCF -- secs = %u, usecs = %u",
                          (unsigned int)CommandPtr->Seconds, (unsigned int)CommandPtr->MicroSeconds);
    }

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_SetLeapSecondsCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_SetLeapSecondsCmd(const CFE_TIME_SetLeapSecondsCmd_t *data)
{
#if (CFE_PLATFORM_TIME_CFG_SERVER == true)

    const CFE_TIME_LeapsCmd_Payload_t *CommandPtr = &data->Payload;

    /*
    ** No value checking (leaps may be positive or negative)...
    */
    CFE_TIME_SetLeapSeconds(CommandPtr->LeapSeconds);

    CFE_TIME_Global.CommandCounter++;

    CFE_EVS_SendEvent(CFE_TIME_LEAPS_EID, CFE_EVS_EventType_INFORMATION, "Set Leap Seconds = %d",
                      (int)CommandPtr->LeapSeconds);

#else /* not CFE_PLATFORM_TIME_CFG_SERVER */
    /*
    ** We want to know if disabled commands are being sent...
    */
    CFE_TIME_Global.CommandErrorCounter++;

    CFE_EVS_SendEvent(CFE_TIME_LEAPS_CFG_EID, CFE_EVS_EventType_ERROR,
                      "Set Leaps commands invalid without CFE_PLATFORM_TIME_CFG_SERVER set to TRUE");

#endif /* CFE_PLATFORM_TIME_CFG_SERVER */

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_AdjustImpl
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
void CFE_TIME_AdjustImpl(const CFE_TIME_TimeCmd_Payload_t *CommandPtr, CFE_TIME_AdjustDirection_Enum_t Direction)
{
    /*
    ** Verify command arguments...
    */
    if (CommandPtr->MicroSeconds < 1000000)
    {
#if (CFE_PLATFORM_TIME_CFG_SERVER == true)

        CFE_TIME_SysTime_t Adjust;

        Adjust.Seconds    = CommandPtr->Seconds;
        Adjust.Subseconds = CFE_TIME_Micro2SubSecs(CommandPtr->MicroSeconds);

        CFE_TIME_SetAdjust(Adjust, Direction);

        CFE_TIME_Global.CommandCounter++;
        CFE_EVS_SendEvent(CFE_TIME_DELTA_EID, CFE_EVS_EventType_INFORMATION,
                          "STCF Adjust -- secs = %u, usecs = %u, ssecs = 0x%X, dir[1=Pos, 2=Neg] = %d",
                          (unsigned int)CommandPtr->Seconds, (unsigned int)CommandPtr->MicroSeconds,
                          (unsigned int)CFE_TIME_Micro2SubSecs(CommandPtr->MicroSeconds), (int)Direction);

#else /* not CFE_PLATFORM_TIME_CFG_SERVER */
        /*
        ** We want to know if disabled commands are being sent...
        */
        CFE_TIME_Global.CommandErrorCounter++;

        CFE_EVS_SendEvent(CFE_TIME_DELTA_CFG_EID, CFE_EVS_EventType_ERROR,
                          "STCF Adjust commands invalid without CFE_PLATFORM_TIME_CFG_SERVER set to TRUE");

#endif /* CFE_PLATFORM_TIME_CFG_SERVER */
    }
    else
    {
        CFE_TIME_Global.CommandErrorCounter++;
        CFE_EVS_SendEvent(CFE_TIME_DELTA_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Invalid STCF Adjust -- secs = %u, usecs = %u, dir[1=Pos, 2=Neg] = %d",
                          (unsigned int)CommandPtr->Seconds, (unsigned int)CommandPtr->MicroSeconds, (int)Direction);
    }
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_AddAdjustCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_AddAdjustCmd(const CFE_TIME_AddAdjustCmd_t *data)
{
    CFE_TIME_AdjustImpl(&data->Payload, CFE_TIME_AdjustDirection_ADD);
    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_SubAdjustCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_SubAdjustCmd(const CFE_TIME_SubAdjustCmd_t *data)
{
    CFE_TIME_AdjustImpl(&data->Payload, CFE_TIME_AdjustDirection_SUBTRACT);
    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_1HzAdjImpl
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
void CFE_TIME_1HzAdjImpl(const CFE_TIME_OneHzAdjustmentCmd_Payload_t *CommandPtr,
                         CFE_TIME_AdjustDirection_Enum_t              Direction)
{
/*
** 1Hz adjustments are only valid for "Time Servers"...
*/
#if (CFE_PLATFORM_TIME_CFG_SERVER == true)

    CFE_TIME_SysTime_t Adjust;

    CFE_TIME_Copy(&Adjust, CommandPtr);

    CFE_TIME_Set1HzAdj(Adjust, Direction);

    CFE_TIME_Global.CommandCounter++;
    CFE_EVS_SendEvent(CFE_TIME_1HZ_EID, CFE_EVS_EventType_INFORMATION,
                      "STCF 1Hz Adjust -- secs = %d, ssecs = 0x%X, dir[1=Pos, 2=Neg] = %d", (int)CommandPtr->Seconds,
                      (unsigned int)CommandPtr->Subseconds, (int)Direction);

#else /* not CFE_PLATFORM_TIME_CFG_SERVER */
    /*
    ** We want to know if disabled commands are being sent...
    */
    CFE_TIME_Global.CommandErrorCounter++;

    CFE_EVS_SendEvent(CFE_TIME_1HZ_CFG_EID, CFE_EVS_EventType_ERROR,
                      "1Hz Adjust commands invalid without CFE_PLATFORM_TIME_CFG_SERVER set to TRUE");

#endif /* CFE_PLATFORM_TIME_CFG_SERVER */
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_Add1HZAdjustmentCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_Add1HZAdjustmentCmd(const CFE_TIME_Add1HZAdjustmentCmd_t *data)
{
    CFE_TIME_1HzAdjImpl(&data->Payload, CFE_TIME_AdjustDirection_ADD);
    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: CFE_TIME_Sub1HZAdjustmentCmd
 *
 * Application-scope internal function
 * See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 CFE_TIME_Sub1HZAdjustmentCmd(const CFE_TIME_Sub1HZAdjustmentCmd_t *data)
{
    CFE_TIME_1HzAdjImpl(&data->Payload, CFE_TIME_AdjustDirection_SUBTRACT);
    return CFE_SUCCESS;
}
