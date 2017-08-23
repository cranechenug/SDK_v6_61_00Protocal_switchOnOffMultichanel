/***************************************************************************
*
* Copyright (c) 2001-2011
* Sigma Designs, Inc.
* All Rights Reserved
*
*---------------------------------------------------------------------------
*
* Description: SwitchOnOff source file
*
* Author:
*
* Last Changed By:  $Author:  $
* Revision:         $Revision:  $
* Last Changed:     $Date:  $
*
****************************************************************************/


/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/

#include "config_app.h"

#include <slave_learn.h>
#include <ZW_slave_api.h>
#ifdef ZW_SLAVE_32
#include <ZW_slave_32_api.h>
#else
#include <ZW_slave_routing_api.h>
#endif  /* ZW_SLAVE_32 */

#include <ZW_classcmd.h>
#include <ZW_mem_api.h>
#include <ZW_TransportLayer.h>

#include <eeprom.h>
#include <ZW_uart_api.h>

#include <misc.h>
#ifdef BOOTLOADER_ENABLED
#include <ota_util.h>
#include <CommandClassFirmwareUpdate.h>
#endif

/*IO control*/
//#include <ZW_pindefs.h>
//#include <ZW_evaldefs.h>
#include <io_zdp03a.h>
#include <keyman.h>

#ifdef ZW_ISD51_DEBUG
#include "ISD51.h"
#endif

#include <association_plus.h>
#include <agi.h>
#include <CommandClassAssociation.h>
#include <CommandClassAssociationGroupInfo.h>
#include <CommandClassVersion.h>
#include <CommandClassZWavePlusInfo.h>
#include <CommandClassPowerLevel.h>
#include <CommandClassDeviceResetLocally.h>
#include <manufacturer_specific_device_id.h>
#include <CommandClassBasic.h>
#include <CommandClassBinarySwitch.h>

#ifdef MULTI_CHANNEL_TRANSPORT
#include <CommandClassMultiChan.h>
#include <CommandClassMultiChanAssociation.h>
#endif /*MULTI_CHANNEL_TRANSPORT*/

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/
#ifdef ZW_DEBUG_SWITCH
#define ZW_DEBUG_SWITCH_SEND_BYTE(data) ZW_DEBUG_SEND_BYTE(data)
#define ZW_DEBUG_SWITCH_SEND_STR(STR) ZW_DEBUG_SEND_STR(STR)
#define ZW_DEBUG_SWITCH_SEND_NUM(data)  ZW_DEBUG_SEND_NUM(data)
#define ZW_DEBUG_SWITCH_SEND_WORD_NUM(data) ZW_DEBUG_SEND_WORD_NUM(data)
#define ZW_DEBUG_SWITCH_SEND_NL()  ZW_DEBUG_SEND_NL()
#else
#define ZW_DEBUG_SWITCH_SEND_BYTE(data)
#define ZW_DEBUG_SWITCH_SEND_STR(STR)
#define ZW_DEBUG_SWITCH_SEND_NUM(data)
#define ZW_DEBUG_SWITCH_SEND_WORD_NUM(data)
#define ZW_DEBUG_SWITCH_SEND_NL()
#endif


typedef enum _EVENT_APP_
{
  EVENT_EMPTY = DEFINE_EVENT_APP_NBR,
  EVENT_APP_INIT,
  EVENT_APP_OTA_START,
  EVENT_APP_LEARN_MODE_FINISH,
  EVENT_APP_WAKEUP_NVR_ERROR_FINSIH,
  EVENT_APP_REFRESH_MMI
} EVENT_APP;

typedef enum _STATE_APP_
{
  STATE_APP_STARTUP,
  STATE_APP_IDLE,
  STATE_APP_LEARN_MODE,
  STATE_APP_LOCAL_RESET,
  STATE_APP_OTA,
  STATE_APP_NVR_ERROR
} STATE_APP;


/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/

/* CHANGE - Change the list of command classes to fit your product */

/**
 * Unsecure node information list.
 * Be sure Command classes are not duplicated in both lists.
 * CHANGE THIS - Add all supported non-secure command classes here
 **/
static code BYTE cmdClassListNonSecureNotIncluded[] =
{
  COMMAND_CLASS_ZWAVEPLUS_INFO,
  COMMAND_CLASS_SWITCH_BINARY,
  COMMAND_CLASS_ASSOCIATION,
  COMMAND_CLASS_ASSOCIATION_GRP_INFO,
  COMMAND_CLASS_VERSION,
  COMMAND_CLASS_MANUFACTURER_SPECIFIC,
  COMMAND_CLASS_DEVICE_RESET_LOCALLY,
  COMMAND_CLASS_POWERLEVEL
#ifdef SECURITY
  ,COMMAND_CLASS_SECURITY
#endif
#ifdef BOOTLOADER_ENABLED
  ,COMMAND_CLASS_FIRMWARE_UPDATE_MD_V2
#endif
};

/**
 * Unsecure node information list Secure included.
 * Be sure Command classes are not duplicated in both lists.
 * CHANGE THIS - Add all supported non-secure command classes here
 **/
static code BYTE cmdClassListNonSecureIncludedSecure[] =
{
#ifdef SECURITY
  COMMAND_CLASS_ZWAVEPLUS_INFO,
  COMMAND_CLASS_POWERLEVEL,
  COMMAND_CLASS_SECURITY
#else
  NULL
#endif
};


/**
 * Secure node inforamtion list.
 * Be sure Command classes are not duplicated in both lists.
 * CHANGE THIS - Add all supported secure command classes here
 **/
static code BYTE cmdClassListSecure[] =
{
#ifdef SECURITY
  COMMAND_CLASS_VERSION,
  COMMAND_CLASS_SWITCH_BINARY,
  COMMAND_CLASS_ASSOCIATION,
  COMMAND_CLASS_ASSOCIATION_GRP_INFO,
  COMMAND_CLASS_MANUFACTURER_SPECIFIC,
  COMMAND_CLASS_DEVICE_RESET_LOCALLY
#ifdef BOOTLOADER_ENABLED
  ,COMMAND_CLASS_FIRMWARE_UPDATE_MD_V2
#endif
#else
  NULL
#endif
};

/**
 * Structure includes application node information list's and device type.
 */
APP_NODE_INFORMATION m_AppNIF =
{
  cmdClassListNonSecureNotIncluded, sizeof(cmdClassListNonSecureNotIncluded),
  cmdClassListNonSecureIncludedSecure, sizeof(cmdClassListNonSecureIncludedSecure),
  cmdClassListSecure, sizeof(cmdClassListSecure),
  DEVICE_OPTIONS_MASK, GENERIC_TYPE, SPECIFIC_TYPE
};

const char GroupName[]   = "Lifeline";
CMD_CLASS_GRP  agiTableLifeLine[] = {AGITABLE_LIFELINE_GROUP};

BYTE myNodeID = 0;

/*Handle only one event!*/
static EVENT_APP eventQueue = EVENT_EMPTY;

static STATE_APP currentState = STATE_APP_IDLE;
BYTE wakeupReason;
BOOL ValidNVR = FALSE;
static BYTE onOffState;


/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/


/****************************************************************************/
/*                            PRIVATE FUNCTIONS                             */
/****************************************************************************/
void LoadConfiguration(void);
void SetDefaultConfiguration(void);
void SaveConfiguration(void);
void ZCB_DeviceResetLocallyDone( BYTE status);
STATE_APP AppState();
void AppStateManager( EVENT_APP ev);
void ChangeState( STATE_APP st);
BOOL AddEvent(EVENT_APP ev);
void EventSchedularInit();
void EventSchedular(void);

#ifdef BOOTLOADER_ENABLED
void ZCB_OTAFinish(OTA_STATUS otaStatus);
BOOL ZCB_OTAStart();
#endif

void ToggleLed(void);
void RefreshMMI(void);
void ZCB_ErrorNvrFlashFinish();
/****************************************************************************/
/*                           EXPORTED FUNCTIONS                             */
/****************************************************************************/

/*===========================   ApplicationRfNotify   ===========================
**    Notify the application when the radio switch state
**    Called from the Z-Wave PROTOCOL When radio switch from Rx to Tx or from Tx to Rx
**    or when the modulator PA (Power Amplifier) turn on/off
**---------------------------------------------------------------------------------*/
void          /*RET Nothing */
ApplicationRfNotify(
  BYTE rfState)         /* IN state of the RF, the available values is as follow:
                               ZW_RF_TX_MODE: The RF switch from the Rx to Tx mode, the modualtor is started and PA is on
                               ZW_RF_PA_ON: The RF in the Tx mode, the modualtor PA is turned on
                               ZW_RF_PA_OFF: the Rf in the Tx mode, the modulator PA is turned off
                               ZW_RF_RX_MODE: The RF switch from Tx to Rx mode, the demodulator is started.*/
{
  UNUSED(rfState);
}


/*============================   ApplicationInitHW   ========================
**    Initialization of non Z-Wave module hardware
**
**    Side effects:
**       Returning FALSE from this function will force the API into
**       production test mode.
**--------------------------------------------------------------------------*/
BYTE                       /* RET TRUE        */
ApplicationInitHW(
  BYTE bWakeupReason)      /* IN  Nothing     */
{
#ifdef ZW_ISD51_DEBUG
  ISD_UART_init();
#endif
  wakeupReason = bWakeupReason;
  /* hardware initialization */
  ValidNVR = InitZDP03A();
  SetPinIn(S1,1); //PIN_IN(P24, 1); /*s1 ZDP03A*/
  SetPinIn(S2,1); //PIN_IN(P36, 1); /*s2 ZDP03A*/

  SetPinOut(LED1_OUT); //PIN_OUT(LED1)
  Led(LED1_OUT,OFF); //LED_OFF(1);
  SetPinOut(LED2_OUT); //PIN_OUT(LED1)
  Led(LED2_OUT,OFF); //LED_OFF(1);
  Transport_OnApplicationInitHW(bWakeupReason);
  return(TRUE);
}

/*===========================   ApplicationInitSW   =========================
**    Initialization of the Application Software variables and states
**
**--------------------------------------------------------------------------*/
BYTE                      /*RET  TRUE       */
ApplicationInitSW( void ) /* IN   Nothing   */
{
  /* Init state machine*/
  currentState = STATE_APP_STARTUP;
  /* Do not reinitialize the UART if already initialized for ISD51 in ApplicationInitHW() */
#ifndef ZW_ISD51_DEBUG
  ZW_DEBUG_INIT(1152);
#endif

  ZW_DEBUG_SWITCH_SEND_STR("AppInitSW ");
  ZW_DEBUG_SWITCH_SEND_NUM(wakeupReason);
  ZW_DEBUG_SWITCH_SEND_NL();
#ifdef WATCHDOG_ENABLED
  ZW_WatchDogEnable();
#endif


  /* Signal that the sensor is awake */
  LoadConfiguration();

  AssociationInit(FALSE);
  /* Setup AGI group lists*/
  AGI_Init();
  AGI_LifeLineGroupSetup(agiTableLifeLine, (sizeof(agiTableLifeLine)/sizeof(CMD_CLASS_GRP)), GroupName);



  /* Init key manager*/
  InitKeyManager(AppStateManager, NULL);

#ifdef BOOTLOADER_ENABLED
  /* Initialize OTA module */
  OtaInit( ZWAVE_PLUS_TX_OPTIONS, ZCB_OTAStart, ZCB_OTAFinish);
#endif


  Transport_OnApplicationInitSW( &m_AppNIF, NULL);

  EventSchedularInit();

  if(FALSE == ValidNVR)
  {
    wakeupReason = EVENT_WAKEUP_NVR_ERROR;
  }
  /* Init state machine*/
  AppStateManager((EVENT_WAKEUP)wakeupReason);

  return(TRUE);
}

/*============================   ApplicationTestPoll   ======================
**    Function description
**      This function is called when the slave enters test mode.
**
**    Side effects:
**       Code will not exit until it is reset
**--------------------------------------------------------------------------*/
void
ApplicationTestPoll(void)
{
}

/*=============================  ApplicationPoll   =========================
**    Application poll function for the slave application
**
**    Side effects:
**
**--------------------------------------------------------------------------*/
void                    /*RET  Nothing                  */
ApplicationPoll( void ) /* IN  Nothing                  */
{

#ifdef WATCHDOG_ENABLED
  ZW_WatchDogKick();
#endif

  /*Key manager scan for input port changes and send events to AppStateManager(..)*/
  KeyScan();

  /* Check for event in queue*/
  EventSchedular();

}


/*========================   Transport_ApplicationCommandHandler   ====================
**    Handling of a received application commands and requests
**
**
**--------------------------------------------------------------------------*/
void                              /*RET Nothing                  */
Transport_ApplicationCommandHandlerEx(
  RECEIVE_OPTIONS_TYPE_EX *rxOpt, /* IN receive options of type RECEIVE_OPTIONS_TYPE_EX  */
  ZW_APPLICATION_TX_BUFFER *pCmd, /* IN  Payload from the received frame */
  BYTE   cmdLength)               /* IN Number of command bytes including the command */
{

  /* Call command class handlers */
  switch (pCmd->ZW_Common.cmdClass)
  {
    case COMMAND_CLASS_VERSION:
      handleCommandClassVersion(rxOpt, pCmd, cmdLength);
      break;

#ifdef BOOTLOADER_ENABLED
    case COMMAND_CLASS_FIRMWARE_UPDATE_MD_V2:
      handleCommandClassFWUpdate(rxOpt, pCmd, cmdLength);
      break;
#endif

    case COMMAND_CLASS_ASSOCIATION_GRP_INFO:
      handleCommandClassAssociationGroupInfo( rxOpt, pCmd, cmdLength);
      break;

    case COMMAND_CLASS_ASSOCIATION:
			handleCommandClassAssociation(rxOpt, pCmd, cmdLength);
      break;

    case COMMAND_CLASS_POWERLEVEL:
      handleCommandClassPowerLevel(rxOpt, pCmd, cmdLength);
      break;

    case COMMAND_CLASS_MANUFACTURER_SPECIFIC:
      handleCommandClassManufacturerSpecific(rxOpt, pCmd, cmdLength);
      break;

    case COMMAND_CLASS_ZWAVEPLUS_INFO:
      handleCommandClassZWavePlusInfo(rxOpt, pCmd, cmdLength);
      break;

    case COMMAND_CLASS_BASIC:
      handleCommandClassBasic(rxOpt, pCmd, cmdLength);
      break;

    case COMMAND_CLASS_SWITCH_BINARY:
      handleCommandClassBinarySwitch(rxOpt, pCmd, cmdLength);
      break;

#ifdef MULTI_CHANNEL_TRANSPORT
    case COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2:
      handleCommandClassMultiChannelAssociation(rxOpt, pCmd, cmdLength);
      break;
#endif /*MULTI_CHANNEL_TRANSPORT*/
  }
}


/*========================   handleCommandClassVersionAppl  =========================
**   Application specific Command Class Version handler
**   return none
**
**   Side effects: none
**--------------------------------------------------------------------------*/
BYTE
handleCommandClassVersionAppl( BYTE cmdClass )
{
  BYTE commandClassVersion = UNKNOWN_VERSION;

  switch (cmdClass)
  {
    case COMMAND_CLASS_VERSION:
     commandClassVersion = CommandClassVersionVersionGet();
      break;

#ifdef BOOTLOADER_ENABLED
    case COMMAND_CLASS_FIRMWARE_UPDATE_MD:
      commandClassVersion = CommandClassFirmwareUpdateMdVersionGet();
      break;
#endif

    case COMMAND_CLASS_POWERLEVEL:
     commandClassVersion = CommandClassPowerLevelVersionGet();
      break;

    case COMMAND_CLASS_MANUFACTURER_SPECIFIC:
     commandClassVersion = CommandClassManufacturerVersionGet();
      break;

    case COMMAND_CLASS_ASSOCIATION:
     commandClassVersion = CommandClassAssociactionVersionGet();
      break;

    case COMMAND_CLASS_ASSOCIATION_GRP_INFO:
     commandClassVersion = CommandClassAssociationGroupInfoVersionGet();
      break;

    case COMMAND_CLASS_DEVICE_RESET_LOCALLY:
     commandClassVersion = CommandClassDeviceResetLocallyVersionGet();
      break;

    case COMMAND_CLASS_ZWAVEPLUS_INFO:
     commandClassVersion = CommandClassZWavePlusVersion();
      break;
    case COMMAND_CLASS_BASIC:
     commandClassVersion =  CommandClassBasicVersionGet();
      break;
    case COMMAND_CLASS_SWITCH_BINARY:
     commandClassVersion = CommandClassBinarySwitchVersionGet();
      break;

#ifdef MULTI_CHANNEL_TRANSPORT
    case COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2:
      commandClassVersion = CmdClassMultiChannelAssociationVersion();
      break;
    case COMMAND_CLASS_MULTI_CHANNEL_V3:
      commandClassVersion = CmdClassMultiChannelGet();
      break;
#endif /*MULTI_CHANNEL_TRANSPORT*/

#ifdef SECURITY
    case COMMAND_CLASS_SECURITY:
     commandClassVersion = CommandClassSecurityVersionGet();
      break;
#endif /*SECURITY*/
    default:
     commandClassVersion = UNKNOWN_VERSION;
  }
  return commandClassVersion;
}


/*==========================   ApplicationSlaveUpdate   =======================
**   Inform a slave application that a node information is received.
**   Called from the slave command handler when a node information frame
**   is received and the Z-Wave protocol is not in a state where it is needed.
**
**--------------------------------------------------------------------------*/
void
ApplicationSlaveUpdate(
  BYTE bStatus,     /*IN  Status event */
  BYTE bNodeID,     /*IN  Node id of the node that send node info */
  BYTE* pCmd,       /*IN  Pointer to Application Node information */
  BYTE bLen)       /*IN  Node info length                        */
{
  UNUSED(bStatus);
  UNUSED(bNodeID);
  UNUSED(pCmd);
  UNUSED(bLen);
}


/*============================ handleNbrFirmwareVersions ===================
** Function description
** Read number of firmwares in application.
**
**-------------------------------------------------------------------------*/
BYTE
handleNbrFirmwareVersions(void)
{
  return 1; /*CHANGE THIS - firmware 0 version*/
}


/*============================ handleGetFirmwareVersion ====================
** Function description
** Read application firmware version informations
**
**-------------------------------------------------------------------------*/
void
handleGetFirmwareVersion( BYTE bFirmwareNumber, VG_VERSION_REPORT_V2_VG* pVariantgroup)
{
  /*firmware 0 version and sub version*/
  if(bFirmwareNumber == 0)
  {
    pVariantgroup->firmwareVersion = APP_VERSION;
    pVariantgroup->firmwareSubVersion = APP_REVISION;
  }
  else
  {
    /*Just set it to 0 if firmware n is not present*/
    pVariantgroup->firmwareVersion = 0;
    pVariantgroup->firmwareSubVersion = 0;
  }
}


/*============================   LearnCompleted   ========================
**    Callback which is called on learnmode completed
**  Application specific handling of LearnModeCompleted - called from
**  slave_learn.c
**--------------------------------------------------------------------------*/
void
LearnCompleted(BYTE bNodeID)                 /*IN The nodeID assigned*/
{

  ZW_DEBUG_SWITCH_SEND_STR("\r\n LearnCompleted(");
  ZW_DEBUG_SWITCH_SEND_NUM(bNodeID);
  ZW_DEBUG_SWITCH_SEND_BYTE(')');
  ZW_DEBUG_SWITCH_SEND_NL();
/*If bNodeID= 0xff.. learn mode failed*/
  if(bNodeID != NODE_BROADCAST)
  {
    /*Success*/
    myNodeID = bNodeID;
    if (myNodeID == 0)
    {
      /*Clear association*/
      AssociationInit(TRUE);
      SetDefaultConfiguration();
    }
  }
  AppStateManager(EVENT_APP_LEARN_MODE_FINISH);
  Transport_OnLearnCompleted(bNodeID);
}




/*========================   GetMyNodeID   =================================
**    Get the device node ID
**
**   Side effects: none
**--------------------------------------------------------------------------*/
BYTE
GetMyNodeID(void)
{
	return myNodeID;
}


/*============================ AppState ===============================
** Function description
** Return application statemachine state
**
** Side effects:
**
**-------------------------------------------------------------------------*/
STATE_APP
AppState()
{
  return currentState;
}


/*============================ AppStateManager ===============================
** Function description
** This function...
**
** Side effects:
**
**-------------------------------------------------------------------------*/
void
AppStateManager( EVENT_APP ev)
{
   ZW_DEBUG_SWITCH_SEND_STR("AppStateManager ev ");
   ZW_DEBUG_SWITCH_SEND_NUM(ev);
   ZW_DEBUG_SWITCH_SEND_STR(" st ");
   ZW_DEBUG_SWITCH_SEND_NUM(currentState);
   ZW_DEBUG_SWITCH_SEND_NL();


  switch(currentState)
  {
    case STATE_APP_STARTUP:
      if(EVENT_WAKEUP_NVR_ERROR == ev)
      {
        ZW_DEBUG_SWITCH_SEND_STR("ERROR_NVR_FLASH_SEC");
        ZW_DEBUG_SWITCH_SEND_NL();
        LedErrorFlash(ERROR_NVR_FLASH_SEC, ZCB_ErrorNvrFlashFinish);
        ChangeState(STATE_APP_NVR_ERROR);
      }
      else{
        ChangeState(STATE_APP_IDLE);
        AddEvent(EVENT_APP_REFRESH_MMI);
      }
      break;

    case STATE_APP_IDLE:
      if(EVENT_APP_REFRESH_MMI == ev)
      {
        Led(LED1_OUT,OFF);
        RefreshMMI();
      }
      if(EVENT_KEY_B0_PRESS == ev)
      {
        ZW_DEBUG_SWITCH_SEND_STR("+NIF ");
        ZW_SendNodeInformation(NODE_BROADCAST, 0, NULL);
      }
      if(EVENT_KEY_B0_RELEASE == ev){ }

      if(EVENT_KEY_B0_TRIPLE_PRESS == ev)
      {
        if (myNodeID){
          ZW_DEBUG_SWITCH_SEND_STR("LEARN_MODE_EXCLUSION");
          StartLearnModeNow(LEARN_MODE_EXCLUSION_NWE);
        }
        else{
          ZW_DEBUG_SWITCH_SEND_STR("LEARN_MODE_INCLUSION");
          StartLearnModeNow(LEARN_MODE_INCLUSION);
        }
        Led(LED1_OUT,ON);
        ChangeState(STATE_APP_LEARN_MODE);
      }

      if(EVENT_KEY_B0_HELD == ev)
      {
        handleCommandClassDeviceResetLocally(ZCB_DeviceResetLocallyDone);
        ChangeState(STATE_APP_LOCAL_RESET);
      }

      if(EVENT_KEY_B1_PRESS == ev)
      {
        ZW_DEBUG_SWITCH_SEND_STR("+ToggleLed ");
        ToggleLed();
      }

      if(EVENT_KEY_B1_RELEASE == ev){ }
      break;

    case STATE_APP_LEARN_MODE:
      if(EVENT_KEY_B0_TRIPLE_PRESS == ev)
      {
        ZW_DEBUG_SWITCH_SEND_STR("End LEARNMODE ");
        StartLearnModeNow(LEARN_MODE_DISABLE);
        Led(LED1_OUT,OFF);
        ChangeState(STATE_APP_IDLE);
      }

      if( EVENT_APP_LEARN_MODE_FINISH == ev)
      {
        Led(LED1_OUT,OFF);
        ChangeState(STATE_APP_IDLE);
      }
      break;

    case STATE_APP_LOCAL_RESET:
      //device reboot in this state by ZCB_CommandClassDeviceResetLocally
      break;
    case STATE_APP_OTA:
      /*OTA state... do nothing until firmware update is finish*/
      break;

    case STATE_APP_NVR_ERROR:
      if(EVENT_APP_WAKEUP_NVR_ERROR_FINSIH == ev)
      {
        ZW_DEBUG_SWITCH_SEND_STR("EVENT_APP_WAKEUP_NVR_ERROR_FINSIH");
        ZW_DEBUG_SWITCH_SEND_NL();
        ChangeState(STATE_APP_IDLE);
        AddEvent(EVENT_APP_REFRESH_MMI);
      }
      break;
  }
}


/*============================ ChangeState ===============================
** Function description
** Change state
**
**-------------------------------------------------------------------------*/
void
ChangeState( STATE_APP st)
{
 ZW_DEBUG_SWITCH_SEND_STR("ChangeState st = ");
 ZW_DEBUG_SWITCH_SEND_NUM(currentState);
 ZW_DEBUG_SWITCH_SEND_STR(" -> new st = ");
 ZW_DEBUG_SWITCH_SEND_NUM(st);
 ZW_DEBUG_SWITCH_SEND_NL();

 currentState = st;
}


code const void (code * ZCB_DeviceResetLocallyDone_p)(BYTE txStatus) = &ZCB_DeviceResetLocallyDone;
/*==============================   ZCB_DeviceResetLocallyDone====================
**
**  Function:  callback function perform reset device
**
**  Side effects: None
**
**--------------------------------------------------------------------------*/
void
ZCB_DeviceResetLocallyDone(BYTE status)
{
  UNUSED(status);
	/* CHANGE THIS - clean your own application data from NVM*/
  Transport_SetDefault();
  ZW_SetDefault();
  ZW_WatchDogEnable(); /*reset asic*/
  for (;;) {}
}


#ifdef BOOTLOADER_ENABLED
/*============================ OTA_Finish ===============================
** Function description
** OTA is finish.
**
** Side effects:
**
**-------------------------------------------------------------------------*/
code const void (code * ZCB_OTAFinish_p)(OTA_STATUS otaStatus) = &ZCB_OTAFinish;
void
ZCB_OTAFinish(OTA_STATUS otaStatus) /*Status on OTA*/
{
  UNUSED(otaStatus);
  /*Just reboot node to cleanup and start on new FW.*/
  ZW_WatchDogEnable(); /*reset asic*/
  while(1);
}

/*============================ OTA_Start ===============================
** Function description
** Ota_Util calls this function when firmware update is ready to start.
** Return FALSE if OTA should be rejected else TRUE
**
** Side effects:
**
**-------------------------------------------------------------------------*/
code const BOOL (code * ZCB_OTAStart_p)(void) = &ZCB_OTAStart;
BOOL   /*Return FALSE if OTA should be rejected else TRUE*/
ZCB_OTAStart()
{
  BOOL  status = FALSE;
  if (STATE_APP_IDLE == AppState())
  {
    AppStateManager(EVENT_APP_OTA_START);
    status = TRUE;
  }
  return status;
}
#endif


/*========================   handleBasicSetCommand   ===========================
**    Handling of a Basic Set Command
**
**   Side effects: none
**--------------------------------------------------------------------------*/
void
handleBasicSetCommand(  BYTE val )
{
  ZCB_CmdCBinarySwitchSupportSet(val);
}

/*========================   getAppBasicReport   ===========================
**    return the On / Off state
**
**   Side effects: none
**--------------------------------------------------------------------------*/
BYTE
getAppBasicReport(void)
{
  return handleAppltBinarySwitchGet();
}


/*========================   sendApplReport   ======================
**    Handling of a Application specific Binary Switch Report
**
**   Side effects: none
**--------------------------------------------------------------------------*/
BYTE
handleAppltBinarySwitchGet(void)
{
  return MemoryGetByte((WORD)&OnOffState_far);
}


/*========================   handleApplBinarySwitchSet   ======================
**    Handling of a Application specific Binary Switch Set
**
**   Side effects: none
**--------------------------------------------------------------------------*/
void
handleApplBinarySwitchSet(
  CMD_CLASS_BIN_SW_VAL val
)
{
  onOffState = val;
  MemoryPutByte((WORD)&OnOffState_far, onOffState);
  if (onOffState == CMD_CLASS_BIN_OFF)
  {
    Led(LED2_OUT,OFF); //LED_OFF(1);
  }
  else if (onOffState == CMD_CLASS_BIN_ON)
  {
    Led(LED2_OUT,ON); //LED_ON(1);
  }
}


/*============================   SaveConfiguration   ======================
**    This function saves the current configuration to EEPROM
**
**    Side effects:
**
**--------------------------------------------------------------------------*/
void                   /*RET  Nothing       */
SaveConfiguration(void)
{
  ZW_DEBUG_SWITCH_SEND_BYTE('C');
  ZW_DEBUG_SWITCH_SEND_BYTE('s');
  /* Mark stored configuration as OK */
  MemoryPutByte((WORD)&OnOffState_far, onOffState);
  MemoryPutByte((WORD)&EEOFFSET_MAGIC_far, APPL_MAGIC_VALUE);
}


/*============================   SetDefaultConfiguration   ======================
**    Function resets configuration to default values.
**
**    Side effects:
**
**--------------------------------------------------------------------------*/
void                   /*RET  Nothing       */
SetDefaultConfiguration(void)
{
  ZW_DEBUG_SWITCH_SEND_BYTE('C');
  ZW_DEBUG_SWITCH_SEND_BYTE('d');
  onOffState = CMD_CLASS_BIN_OFF;
  SaveConfiguration();
}


/*============================   LoadConfiguration   ======================
**    This function loads the application settings from EEPROM.
**    If no settings are found, default values are used and saved.
**    Side effects:
**
**--------------------------------------------------------------------------*/
void                   /* RET  Nothing      */
LoadConfiguration(void)
{
  /* Get this sensors identification on the network */
  MemoryGetID( NULL, &myNodeID);
  ManufacturerSpecificDeviceIDInit();
  /* Check to see, if any valid configuration is stored in the EEPROM */
  if (MemoryGetByte((WORD)&EEOFFSET_MAGIC_far) == APPL_MAGIC_VALUE)
  {
    loadStatusPowerLevel(NULL,NULL);
    /* There is a configuration stored, so load it */
    onOffState = MemoryGetByte((WORD)&OnOffState_far);
    ZW_DEBUG_SWITCH_SEND_BYTE('C');
    ZW_DEBUG_SWITCH_SEND_BYTE('l');
  }
  else
  {
    /* Initialize transport layer NVM */
    Transport_SetDefault();
    /* Reset protocol */
    ZW_SetDefault();

    /* Apparently there is no valid configuration in EEPROM, so load */
    /* default values and save them to EEPROM. */
    SetDefaultConfiguration();
    loadInitStatusPowerLevel(NULL, NULL);
  }
  RefreshMMI();
}



/*============================   CheckButtonPress   ==========================
**    Check and handle button press from ApplicationPoll
**
**
**--------------------------------------------------------------------------*/
void
ToggleLed(void)
{
  if (onOffState)
  {
    onOffState = CMD_CLASS_BIN_OFF;
  }
  else
  {
    onOffState = CMD_CLASS_BIN_ON;
  }
  RefreshMMI();
  MemoryPutByte((WORD)&OnOffState_far, onOffState);
}


/*============================ RefreshMMI ===============================
** Function description
** Refresh MMI
**
** Side effects:
**
**-------------------------------------------------------------------------*/
void RefreshMMI()
{
  if (CMD_CLASS_BIN_OFF == onOffState)
  {
    Led(LED2_OUT,OFF);
  }
  else if (CMD_CLASS_BIN_ON == onOffState)
  {
    Led(LED2_OUT,ON);
  }
}

/*============================ ZCB_ErrorNvrFlashFinish ===============================
** Function description
** Callback function when error led is finish flashing
**
** Side effects:
**
**-------------------------------------------------------------------------*/
PCB(ZCB_ErrorNvrFlashFinish)(void)
{
  AppStateManager(EVENT_APP_WAKEUP_NVR_ERROR_FINSIH);
}


/*============================ AddEvent ===============================
** Function description
** Add event to queue. If return FALSE is queue full!
**
**-------------------------------------------------------------------------*/
BOOL
AddEvent(EVENT_APP ev)
{
  if(EVENT_EMPTY == eventQueue)
  {
    eventQueue = ev;
    return TRUE;
  }

  return FALSE;
}

/*============================ EventSchedularInit ===============================
** Function description
** Init queue
**
** Side effects:
**
**-------------------------------------------------------------------------*/
void
EventSchedularInit()
{
  eventQueue = EVENT_EMPTY;
}
/*============================ EventSchedular ===============================
** Function description
** Event handler... can only handle one event
**
**-------------------------------------------------------------------------*/
void
EventSchedular(void)
{
  if(EVENT_EMPTY != eventQueue)
  {
    BYTE tempEventQ = eventQueue;
    /*Empty queue before calling AppStateManager. AppStateManager can add new
      event in the queue*/
    eventQueue = EVENT_EMPTY;
    AppStateManager(tempEventQ);
  }
}
