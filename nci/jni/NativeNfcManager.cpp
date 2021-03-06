/******************************************************************************
 *
 *  Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *  Not a Contribution.
 *
 *  Copyright (C) 2015 NXP Semiconductors
 *  The original Work has been changed by NXP Semiconductors.
 *
 *  Copyright (C) 2012 The Android Open Source Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/
#include <semaphore.h>
#include <errno.h>
#include "OverrideLog.h"
#include "NfcJniUtil.h"
#include "NfcAdaptation.h"
#include "SyncEvent.h"
#include "PeerToPeer.h"
#include "SecureElement.h"
#include "RoutingManager.h"
#include "NfcTag.h"
#include "config.h"
#include "PowerSwitch.h"
#include "JavaClassConstants.h"
#include "Pn544Interop.h"
#include <ScopedLocalRef.h>
#include <ScopedUtfChars.h>
#include <sys/time.h>
#include "HciRFParams.h"
#include <pthread.h>
#include <ScopedPrimitiveArray.h>
#if((NFC_NXP_ESE == TRUE)&&(NXP_EXTNS == TRUE))
#include <signal.h>
#include <sys/types.h>
#endif
#include "DwpChannel.h"
#include <fcntl.h>
extern "C"
{
    #include "nfc_api.h"
    #include "nfa_api.h"
    #include "nfa_p2p_api.h"
    #include "rw_api.h"
    #include "nfa_ee_api.h"
    #include "nfc_brcm_defs.h"
    #include "ce_api.h"
    #include "phNxpExtns.h"
    #include "phNxpConfig.h"

#if (NFC_NXP_ESE == TRUE)
    #include "JcDnld.h"
    #include "IChannel.h"
#endif
}

#define SAK_VALUE_AT 17
extern bool                   gReaderNotificationflag;
extern const UINT8            nfca_version_string [];
extern const UINT8            nfa_version_string [];
extern tNFA_DM_DISC_FREQ_CFG* p_nfa_dm_rf_disc_freq_cfg;
bool                          sHCEEnabled = true;

#if(NXP_EXTNS == TRUE)
#define RETRY_COUNT         0x10
#define DEFAULT_COUNT       0x03
#define ENABLE_DISCOVERY    0x01
#define DISABLE_DISCOVERY   0x02
#define ENABLE_P2P          0x04
#define T3T_CONFIGURE       0x08
#define RE_ROUTING          0x10
#define CLEAR_ENABLE_DISABLE_PARAM   0xFC
/* Delay to wait for SE intialization */
#define SE_INIT_DELAY       50*1000
#define NFCEE_DISC_TIMEOUT_SEC      2
#define JCOP_INFO_PATH              "/data/nfc/jcop_info.txt"
#define OSU_NOT_STARTED             00
#define OSU_COMPLETE                03
#define UICC_HANDLE         0x402
#define UICC2_HANDLE        0x481
#define ESE_HANDLE          0x4C0
#define NFC_PIPE_STATUS_OFFSET       4
#if(NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE)
#define MAX_JCOP_TIMEOUT_VALUE 60000 /*Maximum Jcop OSU timeout value*/
#define MAX_WAIT_TIME_FOR_RETRY 8 /*Maximum wait for retry in usec*/
#endif
extern nfcee_disc_state     sNfcee_disc_state;
extern bool                 recovery;
extern uint8_t              swp_getconfig_status;
extern int                  gUICCVirtualWiredProtectMask;
extern int                  gEseVirtualWiredProtectMask;
static INT32                gNfcInitTimeout;
INT32                       gdisc_timeout;
INT32                       gSeDiscoverycount = 0;
INT32                       gActualSeCount = 0;
UINT16                      sCurrentSelectedUICCSlot = 1;
SyncEvent                   gNfceeDiscCbEvent;
#if (NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
UINT8                       sSelectedUicc = 0;
#endif
#if ((NFC_NXP_ESE ==  TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
extern Rdr_req_ntf_info_t   swp_rdr_req_ntf_info;
#endif
#if ((NXP_NFCC_ESE_UICC_CONCURRENT_ACCESS_PROTECTION == TRUE) && (NFC_NXP_ESE == TRUE))
Mutex gDiscMutex;
#endif
#if(NXP_NFCC_HCE_F == TRUE)
bool nfcManager_getTransanctionRequest(int t3thandle, bool registerRequest);
#endif
#if ((NXP_EXTNS == TRUE) && (NFC_NXP_ESE == TRUE))
#if ((NXP_ESE_SVDD_SYNC == TRUE) || (NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE) || (NXP_NFCC_SPI_FW_DOWNLOAD_SYNC == TRUE) || (NXP_ESE_DWP_SPI_SYNC_ENABLE == TRUE))
extern bool createSPIEvtHandlerThread();
extern void releaseSPIEvtHandlerThread();
#endif
#endif
#endif
namespace android
{
    extern bool gIsTagDeactivating;
    extern bool gIsSelectingRfInterface;
    extern void nativeNfcTag_doTransceiveStatus (tNFA_STATUS status, uint8_t * buf, uint32_t buflen);
    extern void nativeNfcTag_notifyRfTimeout ();
    extern void nativeNfcTag_doConnectStatus (jboolean is_connect_ok);
    extern void nativeNfcTag_doDeactivateStatus (int status);
    extern void nativeNfcTag_doWriteStatus (jboolean is_write_ok);
    extern void nativeNfcTag_doCheckNdefResult (tNFA_STATUS status, uint32_t max_size, uint32_t current_size, uint8_t flags);
    extern void nativeNfcTag_doMakeReadonlyResult (tNFA_STATUS status);
    extern void nativeNfcTag_doPresenceCheckResult (tNFA_STATUS status);
    extern void nativeNfcTag_formatStatus (bool is_ok);
    extern void nativeNfcTag_resetPresenceCheck ();
    extern void nativeNfcTag_doReadCompleted (tNFA_STATUS status);
    extern void nativeNfcTag_setRfInterface (tNFA_ACTIVATED& activated);
    extern void nativeNfcTag_abortWaits ();
    extern void doDwpChannel_ForceExit();
    extern void nativeLlcpConnectionlessSocket_abortWait ();
    extern tNFA_STATUS EmvCo_dosetPoll(jboolean enable);
    extern void nativeNfcTag_registerNdefTypeHandler ();
    extern void nativeLlcpConnectionlessSocket_receiveData (uint8_t* data, uint32_t len, uint32_t remote_sap);
    extern tNFA_STATUS SetScreenState(int state);
    extern tNFA_STATUS SendAutonomousMode(int state , uint8_t num);
    //Factory Test Code --start
    extern tNFA_STATUS Nxp_SelfTest(uint8_t testcase, uint8_t* param);
    extern void SetCbStatus(tNFA_STATUS status);
    extern tNFA_STATUS GetCbStatus(void);
    static void nfaNxpSelfTestNtfTimerCb (union sigval);
    extern tNFA_STATUS ResetEseSession();
    //Factory Test Code --end
    extern bool getReconnectState(void);
    extern tNFA_STATUS SetVenConfigValue(jint nfcMode);
    extern tNFA_STATUS SetHfoConfigValue(void);
    extern tNFA_STATUS SetUICC_SWPBitRate(bool);
    extern tNFA_STATUS GetNumNFCEEConfigured(void);
    extern void acquireRfInterfaceMutexLock();
    extern void releaseRfInterfaceMutexLock();
    extern tNFA_STATUS NxpNfc_Write_Cmd_Common(uint8_t retlen, uint8_t* buffer);
#if(NXP_EXTNS == TRUE)
    extern bool gIsWaiting4Deact2SleepNtf;
    extern bool gGotDeact2IdleNtf;
    bool nfcManager_isTransanctionOnGoing(bool isInstallRequest);
    extern tNFA_STATUS enableSWPInterface();
#if(NXP_NFCC_SPI_FW_DOWNLOAD_SYNC == TRUE)
    jmethodID               gCachedNfcManagerNotifyFwDwnldRequested;
#endif
#if(NFC_NXP_CHIP_TYPE != PN547C2)
    extern tNFA_STATUS SendAGCDebugCommand();
#endif
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
    extern tNFA_STATUS Set_EERegisterValue(UINT16 RegAddr, uint8_t bitVal);
#endif
#if(NFC_NXP_NON_STD_CARD == TRUE)
    extern void nativeNfcTag_cacheNonNciCardDetection();
    extern void nativeNfcTag_handleNonNciCardDetection(tNFA_CONN_EVT_DATA* eventData);
    extern void nativeNfcTag_handleNonNciMultiCardDetection(UINT8 connEvent, tNFA_CONN_EVT_DATA* eventData);
    extern uint8_t checkTagNtf;
    extern uint8_t checkCmdSent;
#endif
#endif
}


/*****************************************************************************
**
** public variables and functions
**
*****************************************************************************/
bool                        gActivated = false;
SyncEvent                   gDeactivatedEvent;

namespace android
{
#if(NXP_EXTNS == TRUE)
    int                     gMaxEERecoveryTimeout = MAX_EE_RECOVERY_TIMEOUT;
#endif
    int                     gGeneralPowershutDown = 0;
    jmethodID               gCachedNfcManagerNotifyNdefMessageListeners;
    jmethodID               gCachedNfcManagerNotifyTransactionListeners;
    jmethodID               gCachedNfcManagerNotifyConnectivityListeners;
    jmethodID               gCachedNfcManagerNotifyEmvcoMultiCardDetectedListeners;
    jmethodID               gCachedNfcManagerNotifyLlcpLinkActivation;
    jmethodID               gCachedNfcManagerNotifyLlcpLinkDeactivated;
    jmethodID               gCachedNfcManagerNotifyLlcpFirstPacketReceived;
    jmethodID               gCachedNfcManagerNotifySeFieldActivated;
    jmethodID               gCachedNfcManagerNotifySeFieldDeactivated;
    jmethodID               gCachedNfcManagerNotifySeListenActivated;
    jmethodID               gCachedNfcManagerNotifySeListenDeactivated;
    jmethodID               gCachedNfcManagerNotifyHostEmuActivated;
    jmethodID               gCachedNfcManagerNotifyHostEmuData;
    jmethodID               gCachedNfcManagerNotifyHostEmuDeactivated;
    jmethodID               gCachedNfcManagerNotifyRfFieldActivated;
    jmethodID               gCachedNfcManagerNotifyRfFieldDeactivated;
    jmethodID               gCachedNfcManagerNotifySWPReaderRequested;
    jmethodID               gCachedNfcManagerNotifySWPReaderRequestedFail;
    jmethodID               gCachedNfcManagerNotifySWPReaderActivated;
    jmethodID               gCachedNfcManagerNotifyAidRoutingTableFull;
#if(NXP_EXTNS == TRUE)
#if((NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
    jmethodID               gCachedNfcManagerNotifyETSIReaderModeStartConfig;
    jmethodID               gCachedNfcManagerNotifyETSIReaderModeStopConfig;
    jmethodID               gCachedNfcManagerNotifyETSIReaderModeSwpTimeout;
#endif
#if((NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE))
    jmethodID               gCachedNfcManagerNotifyUiccStatusEvent;
#endif
#if(NXP_NFCC_HCE_F == TRUE)
    jmethodID               gCachedNfcManagerNotifyT3tConfigure;
#endif
#if(NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE)
    jmethodID               gCachedNfcManagerNotifyJcosDownloadInProgress;
#endif
    jmethodID               gCachedNfcManagerNotifyReRoutingEntry;
#endif
    const char*             gNativeP2pDeviceClassName                 = "com/android/nfc/dhimpl/NativeP2pDevice";
    const char*             gNativeLlcpServiceSocketClassName         = "com/android/nfc/dhimpl/NativeLlcpServiceSocket";
    const char*             gNativeLlcpConnectionlessSocketClassName  = "com/android/nfc/dhimpl/NativeLlcpConnectionlessSocket";
    const char*             gNativeLlcpSocketClassName                = "com/android/nfc/dhimpl/NativeLlcpSocket";
    const char*             gNativeNfcTagClassName                    = "com/android/nfc/dhimpl/NativeNfcTag";
    const char*             gNativeNfcManagerClassName                = "com/android/nfc/dhimpl/NativeNfcManager";
    const char*             gNativeNfcSecureElementClassName          = "com/android/nfc/dhimpl/NativeNfcSecureElement";
    const char*             gNativeNfcAlaClassName                    = "com/android/nfc/dhimpl/NativeNfcAla";
    void                    doStartupConfig ();
    void                    startStopPolling (bool isStartPolling);
    void                    startRfDiscovery (bool isStart);
    void                    setUiccIdleTimeout (bool enable);
    bool                    isDiscoveryStarted ();
#if(NXP_NFCC_SPI_FW_DOWNLOAD_SYNC == TRUE)
    void                    requestFwDownload();
#endif
}


/*****************************************************************************
**
** private variables and functions
**
*****************************************************************************/
namespace android
{
static jint                 sLastError = ERROR_BUFFER_TOO_SMALL;
static jmethodID            sCachedNfcManagerNotifySeApduReceived;
static jmethodID            sCachedNfcManagerNotifySeMifareAccess;
static jmethodID            sCachedNfcManagerNotifySeEmvCardRemoval;
static jmethodID            sCachedNfcManagerNotifyTargetDeselected;
static SyncEvent            sNfaEnableEvent;  //event for NFA_Enable()
static SyncEvent            sNfaDisableEvent;  //event for NFA_Disable()
SyncEvent                   sNfaEnableDisablePollingEvent;  //event for NFA_EnablePolling(), NFA_DisablePolling()
SyncEvent                   sNfaSetConfigEvent;  // event for Set_Config....
SyncEvent                   sNfaGetConfigEvent;  // event for Get_Config....
#if(NXP_EXTNS == TRUE)
SyncEvent                   sNfaGetRoutingEvent;  // event for Get_Routing....
static bool                 sProvisionMode = false;
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
SyncEvent                   sNfceeHciCbEnableEvent;
SyncEvent                   sNfceeHciCbDisableEvent;
#endif
#endif

static bool                 sIsNfaEnabled = false;
static bool                 sDiscoveryEnabled = false;  //is polling or listening
static bool                 sPollingEnabled = false;  //is polling for tag?
static bool                 sIsDisabling = false;
static bool                 sRfEnabled = false; // whether RF discovery is enabled
static bool                 sSeRfActive = false;  // whether RF with SE is likely active
static bool                 sReaderModeEnabled = false; // whether we're only reading tags, not allowing P2p/card emu
static bool                 sP2pEnabled = false;
static bool                 sP2pActive = false; // whether p2p was last active
static bool                 sAbortConnlessWait = false;
static jint                 sLfT3tMax = 0;

static UINT8                sIsSecElemSelected = 0;  //has NFC service selected a sec elem
static UINT8                sIsSecElemDetected = 0;  //has NFC service deselected a sec elem
static bool                 sDiscCmdwhleNfcOff = false;
static UINT8                sAutonomousSet = 0;
#if (NXP_EXTNS == TRUE)
static bool                 gsNfaPartialEnabled = false;
#endif

#define CONFIG_UPDATE_TECH_MASK     (1 << 1)
#define TRANSACTION_TIMER_VALUE     50
#define DEFAULT_TECH_MASK           (NFA_TECHNOLOGY_MASK_A \
                                     | NFA_TECHNOLOGY_MASK_B \
                                     | NFA_TECHNOLOGY_MASK_F \
                                     | NFA_TECHNOLOGY_MASK_ISO15693 \
                                     | NFA_TECHNOLOGY_MASK_B_PRIME \
                                     | NFA_TECHNOLOGY_MASK_A_ACTIVE \
                                     | NFA_TECHNOLOGY_MASK_F_ACTIVE \
                                     | NFA_TECHNOLOGY_MASK_KOVIO)
#define DEFAULT_DISCOVERY_DURATION       500
#define READER_MODE_DISCOVERY_DURATION   200
#if(NXP_EXTNS == TRUE)
#define DUAL_UICC_FEATURE_NOT_AVAILABLE   0xED;
#define STATUS_UNKNOWN_ERROR              0xEF;
#endif
#if((NXP_EXTNS == TRUE) && ((NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE) || (NFC_NXP_STAT_DUAL_UICC_WO_EXT_SWITCH == TRUE)))
enum
{
    UICC_CONFIGURED,
    UICC_NOT_CONFIGURED
};
typedef enum dual_uicc_error_states{
    DUAL_UICC_ERROR_NFCC_BUSY = 0x02,
    DUAL_UICC_ERROR_SELECT_FAILED,
    DUAL_UICC_ERROR_NFC_TURNING_OFF,
    DUAL_UICC_ERROR_INVALID_SLOT,
    DUAL_UICC_ERROR_STATUS_UNKNOWN
}dual_uicc_error_state_t;
#endif

static int screenstate = 0;
static bool pendingScreenState = false;
static void nfcManager_doSetScreenState(JNIEnv* e, jobject o, jint state);
static void nfcManager_doSetScreenOrPowerState (JNIEnv* e, jobject o, jint state);
static void StoreScreenState(int state);
int getScreenState();
#if(NFC_NXP_ESE == TRUE && (NFC_NXP_CHIP_TYPE != PN547C2))
bool isp2pActivated();
#endif
static void nfaConnectionCallback (UINT8 event, tNFA_CONN_EVT_DATA *eventData);
static void nfaDeviceManagementCallback (UINT8 event, tNFA_DM_CBACK_DATA *eventData);
static bool isPeerToPeer (tNFA_ACTIVATED& activated);
static bool isListenMode(tNFA_ACTIVATED& activated);
static void enableDisableLptd (bool enable);
static tNFA_STATUS stopPolling_rfDiscoveryDisabled();
static tNFA_STATUS startPolling_rfDiscoveryDisabled(tNFA_TECHNOLOGY_MASK tech_mask);

static int nfcManager_getChipVer(JNIEnv* e, jobject o);
static jbyteArray nfcManager_getFwFileName(JNIEnv* e, jobject o);
static int nfcManager_getNfcInitTimeout(JNIEnv* e, jobject o);
static int nfcManager_doJcosDownload(JNIEnv* e, jobject o);
static void nfcManager_doCommitRouting(JNIEnv* e, jobject o);
#if(NXP_EXTNS == TRUE)
static void nfcManager_doSetNfcMode (JNIEnv *e, jobject o, jint nfcMode);
#if (NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE)
static bool nfcManager_doCheckJCOPOsDownLoad();
#endif
#endif
static void nfcManager_doSetVenConfigValue (JNIEnv *e, jobject o, jint venconfig);
static jint nfcManager_getSecureElementTechList(JNIEnv* e, jobject o);
static void nfcManager_setSecureElementListenTechMask(JNIEnv *e, jobject o, jint tech_mask);
static void notifyPollingEventwhileNfcOff();

#if (NFC_NXP_ESE == TRUE)
static UINT8 getJCOPOS_UpdaterState();
void DWPChannel_init(IChannel_t *DWP);
IChannel_t Dwp;
#endif
static UINT16 sCurrentConfigLen;
static UINT8 sConfig[256];
#if((NXP_EXTNS == TRUE) && (NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE))
typedef struct
{
    UINT8    sUicc1Cntx[256];
    UINT16   sUicc1CntxLen;
    UINT8    sUicc2Cntx[256];
    UINT16   sUicc2CntxLen;
    UINT8    sUicc1TechCapblty[12];
    UINT8    sUicc2TechCapblty[12];
    UINT8    sUicc1SessionId[8];
    UINT8    sUicc2SessionId[8];
    UINT8    sUicc1SessionIdLen;
    UINT8    sUicc2SessionIdLen;
    UINT8    uiccActivStat = 0;
    UINT8    uiccConfigStat = 0;
    unsigned long  dualUiccEnable = 0;
}dual_uicc_info_t;
dual_uicc_info_t dualUiccInfo;
typedef enum
{
    UICC_CONNECTED_0,
    UICC_CONNECTED_1,
    UICC_CONNECTED_2
}uicc_enumeration_t;

#endif
static UINT8 sLongGuardTime[] = { 0x00, 0x20 };
static UINT8 sDefaultGuardTime[] = { 0x00, 0x11 };
#if(NXP_EXTNS == TRUE)
Mutex                       gTransactionMutex;
const char *                cur_transaction_handle = NULL;
/*Proprietary cmd sent to HAL to send reader mode flag
* Last byte of sProprietaryCmdBuf contains ReaderMode flag */
#define PROPRIETARY_CMD_FELICA_READER_MODE 0xFE
static UINT8   sProprietaryCmdBuf[]={0xFE,0xFE,0xFE,0x00};
UINT8 felicaReader_Disc_id;
static void    NxpResponsePropCmd_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param);
static int    sTechMask = 0; // Copy of Tech Mask used in doEnableReaderMode
static SyncEvent sRespCbEvent;
bool  rfActivation = false;
static void* pollT3TThread(void *arg);
static bool switchP2PToT3TRead(UINT8 disc_id);
static bool isActivatedTypeF(tNFA_ACTIVATED& activated);
typedef enum felicaReaderMode_state
{
    STATE_IDLE = 0x00,
    STATE_NFCDEP_ACTIVATED_NFCDEP_INTF,
    STATE_DEACTIVATED_TO_SLEEP,
    STATE_FRAMERF_INTF_SELECTED,
}eFelicaReaderModeState_t;
static eFelicaReaderModeState_t gFelicaReaderState=STATE_IDLE;

UINT16 sRoutingBuffLen;
static UINT8 sRoutingBuff[MAX_GET_ROUTING_BUFFER_SIZE];
static UINT8 sNfceeConfigured;
static UINT8 sCheckNfceeFlag;
void checkforNfceeBuffer();
void checkforNfceeConfig(UINT8 type);
static void performHCIInitialization (JNIEnv* e, jobject o);
void performNfceeETSI12Config();
tNFA_STATUS getUICC_RF_Param_SetSWPBitRate();
//self test start
static IntervalTimer nfaNxpSelfTestNtfTimer; // notification timer for swp self test
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
static IntervalTimer uiccEventTimer; // notification timer for uicc select
static void notifyUiccEvent (union sigval);
#endif
static SyncEvent sNfaNxpNtfEvent;
static void nfaNxpSelfTestNtfTimerCb (union sigval);
static int nfcManager_setPreferredSimSlot(JNIEnv* e, jobject o, jint uiccSlot);
static void nfcManager_doSetEEPROM(JNIEnv* e, jobject o, jbyteArray val);
static jint nfcManager_getFwVersion(JNIEnv* e, jobject o);
static jint nfcManager_SWPSelfTest(JNIEnv* e, jobject o, jint ch);
static void nfcManager_doPrbsOff(JNIEnv* e, jobject o);
static void nfcManager_doPrbsOn(JNIEnv* e, jobject o, jint prbs, jint hw_prbs, jint tech, jint rate);
static void nfcManager_Enablep2p(JNIEnv* e, jobject o, jboolean p2pFlag);
//self test end
static void nfcManager_setProvisionMode(JNIEnv* e, jobject o, jboolean provisionMode);
static bool nfcManager_doPartialInitialize ();
static bool nfcManager_doPartialDeInitialize();
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
static int nfcManager_doSelectUicc(JNIEnv* e, jobject o, jint uiccSlot);
static int nfcManager_doGetSelectedUicc(JNIEnv* e, jobject o);
static void getUiccContext(int uiccSlot);
static void update_uicc_context_info();
static int getUiccSession();
static void read_uicc_context(UINT8 *uiccContext, UINT16 uiccContextLen, UINT8 *uiccTechCap, UINT16 uiccTechCapLen, UINT8 block, uint8_t slotnum);
static void write_uicc_context(UINT8 *uiccContext, UINT16 uiccContextLen, UINT8 *uiccTechCap, UINT16 uiccTechCapLen, UINT8 block, uint8_t slotnum);
static UINT16 calc_crc16(UINT8* pBuff, UINT16 wLen);
#endif
#if((NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE) || (NFC_NXP_STAT_DUAL_UICC_WO_EXT_SWITCH == TRUE))
static int nfcManager_staticDualUicc_Precondition(int uiccSlot);
#endif
#endif

#if((NXP_EXTNS == TRUE) && (NXP_NFCC_EMPTY_DATA_PACKET == TRUE))
bool nfcManager_sendEmptyDataMsg();
bool gIsEmptyRspSentByHceFApk = FALSE;
#endif

static int nfcManager_doGetSeInterface(JNIEnv* e, jobject o, jint type);

#if(NXP_EXTNS == TRUE && NFC_NXP_NON_STD_CARD == TRUE)
extern bool scoreGenericNtf;
#endif
#if(NXP_EXTNS == TRUE)
tNFC_FW_VERSION get_fw_version();
#if (NXP_NFCEE_REMOVED_NTF_RECOVERY == TRUE)
bool isNfcInitializationDone();
#endif
static UINT16 discDuration = 0x00;
UINT16 getrfDiscoveryDuration();
#if(NFC_NXP_CHIP_TYPE != PN547C2)
typedef struct enableAGC_debug
{
    long enableAGC; // config param
    bool AGCdebugstarted;// flag to indicate agc ongoing
    bool AGCdebugrunning;//flag to indicate agc running or stopped.
}enableAGC_debug_t;
static enableAGC_debug_t menableAGC_debug_t;
void *enableAGCThread(void *arg);
static void nfcManagerEnableAGCDebug(UINT8 connEvent);
void set_AGC_process_state(bool state);
bool get_AGC_process_state();
#endif
#endif

void checkforTranscation(UINT8 connEvent ,void * eventData);
#if (JCOP_WA_ENABLE == TRUE)
void sig_handler(int signo);
#endif
void cleanup_timer();
/* Transaction Events in order */
typedef enum transcation_events
{
    NFA_TRANS_DEFAULT = 0x00,
    NFA_TRANS_ACTIVATED_EVT,
    NFA_TRANS_EE_ACTION_EVT,
    NFA_TRANS_DM_RF_FIELD_EVT,
    NFA_TRANS_DM_RF_FIELD_EVT_ON,
    NFA_TRANS_DM_RF_TRANS_START,
    NFA_TRANS_DM_RF_FIELD_EVT_OFF,
    NFA_TRANS_DM_RF_TRANS_PROGRESS,
    NFA_TRANS_DM_RF_TRANS_END,
    NFA_TRANS_MIFARE_ACT_EVT,
    NFA_TRANS_CE_ACTIVATED = 0x18,
    NFA_TRANS_CE_DEACTIVATED = 0x19,
}eTranscation_events_t;

/* Structure to store screen state */
typedef enum screen_state
{
    NFA_SCREEN_STATE_DEFAULT = 0x00,
    NFA_SCREEN_STATE_OFF,
    NFA_SCREEN_STATE_LOCKED,
    NFA_SCREEN_STATE_UNLOCKED
}eScreenState_t;

typedef enum se_client
{
    DEFAULT = 0x00,
    LDR_SRVCE,
    JCOP_SRVCE,
    LTSM_SRVCE
}seClient_t;

/*Structure to store  discovery parameters*/
typedef struct discovery_Parameters
{
    int technologies_mask;
    bool enable_lptd;
    bool reader_mode;
    bool enable_p2p;
    bool restart;
}discovery_Parameters_t;

/*Structure to store transcation result*/
typedef struct Transcation_Check
{
    bool trans_in_progress;
    char last_request;
    struct nfc_jni_native_data *transaction_nat;
    eScreenState_t last_screen_state_request;
    eTranscation_events_t current_transcation_state;
    discovery_Parameters_t discovery_params;
#if(NXP_EXTNS == TRUE)
#if(NXP_NFCC_HCE_F == TRUE)
    int t3thandle;
    bool isInstallRequest;
#endif
#endif
} Transcation_Check_t;
static struct nfc_jni_native_data *gNativeData = NULL;
#if((NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE) && (NXP_EXTNS == TRUE))
static struct nfc_jni_native_data* nat = NULL;
#endif
extern tNFA_INTF_TYPE   sCurrentRfInterface;
static Transcation_Check_t transaction_data;
static void nfcManager_enableDiscovery (JNIEnv* e, jobject o, jint technologies_mask,
        jboolean enable_lptd, jboolean reader_mode, jboolean enable_p2p,
        jboolean restart);
void nfcManager_disableDiscovery (JNIEnv*, jobject);
static char get_last_request(void);
static void set_last_request(char status, struct nfc_jni_native_data *nat);
static eScreenState_t get_lastScreenStateRequest(void);
static void set_lastScreenStateRequest(eScreenState_t status);
void *enableThread(void *arg);
static IntervalTimer scleanupTimerProc_transaction;
static bool gIsDtaEnabled=false;

#if(NXP_EXTNS == TRUE)
static bool sRfFieldOff = true;
/***P2P-Prio Logic for Multiprotocol***/
static UINT8 multiprotocol_flag = 1;
static UINT8 multiprotocol_detected = 0;
void *p2p_prio_logic_multiprotocol(void *arg);
static IntervalTimer multiprotocol_timer;
pthread_t multiprotocol_thread;
void reconfigure_poll_cb(union sigval);
void clear_multiprotocol();
void multiprotocol_clear_flag(union sigval);
bool update_transaction_stat(const char * req_handle, transaction_state_t req_state);
#endif

static jbyteArray nfcManager_getAdditionalConfigOptions(JNIEnv *e, jobject o);

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

/*******************************************************************************
**
** Function:        getNative
**
** Description:     Get native data
**
** Returns:         Native data structure.
**
*******************************************************************************/
nfc_jni_native_data *getNative (JNIEnv* e, jobject o)
{
    static struct nfc_jni_native_data *sCachedNat = NULL;
    if (e)
    {
        sCachedNat = nfc_jni_get_nat(e, o);
    }
    return sCachedNat;
}


/*******************************************************************************
**
** Function:        handleRfDiscoveryEvent
**
** Description:     Handle RF-discovery events from the stack.
**                  discoveredDevice: Discovered device.
**
** Returns:         None
**
*******************************************************************************/
static void handleRfDiscoveryEvent (tNFC_RESULT_DEVT* discoveredDevice)
{
int thread_ret;
    if(discoveredDevice->more == NCI_DISCOVER_NTF_MORE)
    {

        //there is more discovery notification coming
        NfcTag::getInstance ().mNumDiscNtf++;
        return;
    }
    NfcTag::getInstance ().mNumDiscNtf++;
    ALOGD("Total Notifications - %d ", NfcTag::getInstance ().mNumDiscNtf);
    if(NfcTag::getInstance ().mNumDiscNtf > 1)
    {
        NfcTag::getInstance().mIsMultiProtocolTag = true;
    }
    bool isP2p = NfcTag::getInstance ().isP2pDiscovered ();
    if (!sReaderModeEnabled && isP2p)
    {
        //select the peer that supports P2P
        ALOGD(" select P2P");
#if(NXP_EXTNS == TRUE)
        if(multiprotocol_detected == 1)
        {
            multiprotocol_timer.kill();
        }
#endif
        NfcTag::getInstance ().selectP2p();
    }
#if(NXP_EXTNS == TRUE)
    else if(!sReaderModeEnabled && multiprotocol_flag)
    {
        NfcTag::getInstance ().mNumDiscNtf = 0x00;
        multiprotocol_flag = 0;
        multiprotocol_detected = 1;
        ALOGD("Prio_Logic_multiprotocol Logic");
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        thread_ret = pthread_create(&multiprotocol_thread, &attr,
                p2p_prio_logic_multiprotocol, NULL);
        if(thread_ret != 0)
            ALOGD("unable to create the thread");
        pthread_attr_destroy(&attr);
        ALOGD("Prio_Logic_multiprotocol start timer");
        multiprotocol_timer.set (300, reconfigure_poll_cb);
    }
#endif
    else
    {
#if(NXP_EXTNS == TRUE)
        NfcTag::getInstance ().mNumDiscNtf--;
#endif
        //select the first of multiple tags that is discovered
        NfcTag::getInstance ().selectFirstTag();
        multiprotocol_flag = 1;
    }
}

#if(NXP_EXTNS == TRUE)
void *p2p_prio_logic_multiprotocol(void* /* arg */)
{
tNFA_STATUS status = NFA_STATUS_FAILED;
tNFA_TECHNOLOGY_MASK tech_mask = 0;

    ALOGD ("%s  ", __FUNCTION__);
/* Do not need if it is already in screen off state */
if ((getScreenState() != NFA_SCREEN_STATE_OFF))
{
    if (sRfEnabled) {
        // Stop RF discovery to reconfigure
        startRfDiscovery(false);
    }

    {
        SyncEventGuard guard (sNfaEnableDisablePollingEvent);
        status = NFA_DisablePolling ();
        if (status == NFA_STATUS_OK)
        {
            sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_DISABLED_EVT
        }else
        ALOGE ("%s: Failed to disable polling; error=0x%X", __FUNCTION__, status);
    }

    if(multiprotocol_detected)
    {
        ALOGD ("Enable Polling for TYPE F");
        tech_mask = NFA_TECHNOLOGY_MASK_F;
    }
    else
    {
        ALOGD ("Enable Polling for ALL");
        unsigned long num = 0;
        if (GetNumValue(NAME_POLLING_TECH_MASK, &num, sizeof(num)))
            tech_mask = num;
        else
            tech_mask = DEFAULT_TECH_MASK;
    }

    {
        SyncEventGuard guard (sNfaEnableDisablePollingEvent);
        status = NFA_EnablePolling (tech_mask);
        if (status == NFA_STATUS_OK)
        {
            ALOGD ("%s: wait for enable event", __FUNCTION__);
            sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_ENABLED_EVT
        }
        else
        {
            ALOGE ("%s: fail enable polling; error=0x%X", __FUNCTION__, status);
        }
    }

    /* start polling */
    if (!sRfEnabled)
    {
        // Start RF discovery to reconfigure
        startRfDiscovery(true);
    }
}
return NULL;
}

void reconfigure_poll_cb(union sigval)
{
    ALOGD ("Prio_Logic_multiprotocol timer expire");
    ALOGD ("CallBack Reconfiguring the POLL to Default");
    clear_multiprotocol();
    multiprotocol_timer.set (300, multiprotocol_clear_flag);
}

void clear_multiprotocol()
{
int thread_ret;

    ALOGD ("clear_multiprotocol");
    multiprotocol_detected = 0;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    thread_ret = pthread_create(&multiprotocol_thread, &attr, p2p_prio_logic_multiprotocol, NULL);
    if(thread_ret != 0)
        ALOGD("unable to create the thread");
    pthread_attr_destroy(&attr);
}

void multiprotocol_clear_flag(union sigval)
{
    ALOGD ("multiprotocol_clear_flag");
    multiprotocol_flag = 1;
}
#endif

/*******************************************************************************
**
** Function:        nfaConnectionCallback
**
** Description:     Receive connection-related events from stack.
**                  connEvent: Event code.
**                  eventData: Event data.
**
** Returns:         None
**
*******************************************************************************/
static void nfaConnectionCallback (UINT8 connEvent, tNFA_CONN_EVT_DATA* eventData)
{
    tNFA_STATUS status          = NFA_STATUS_FAILED;
    static UINT8 prev_more_val  = 0x00;
    UINT8 cur_more_val          = 0x00;

    ALOGD("%s: Connection Event = %u", __FUNCTION__, connEvent);

    switch (connEvent)
    {
        case NFA_POLL_ENABLED_EVT:
        {
            ALOGD("%s: NFA_POLL_ENABLED_EVT: status = 0x%0X", __FUNCTION__, eventData->status);
            SyncEventGuard guard (sNfaEnableDisablePollingEvent);
            sNfaEnableDisablePollingEvent.notifyOne ();
        }
        break;

        case NFA_POLL_DISABLED_EVT:
        {
            ALOGD("%s: NFA_POLL_DISABLED_EVT: status = 0x%0X", __FUNCTION__, eventData->status);
            SyncEventGuard guard (sNfaEnableDisablePollingEvent);
            sNfaEnableDisablePollingEvent.notifyOne ();
        }
        break;

        case NFA_RF_DISCOVERY_STARTED_EVT:
        {
            ALOGD("%s: NFA_RF_DISCOVERY_STARTED_EVT: status = 0x%0X", __FUNCTION__, eventData->status);
            SyncEventGuard guard (sNfaEnableDisablePollingEvent);
            sNfaEnableDisablePollingEvent.notifyOne ();
        }
        break;

        case NFA_RF_DISCOVERY_STOPPED_EVT:
        {
            ALOGD("%s: NFA_RF_DISCOVERY_STOPPED_EVT: status = 0x%0X", __FUNCTION__, eventData->status);
            notifyPollingEventwhileNfcOff();
            if (getReconnectState() == true)
            {
                eventData->deactivated.type = NFA_DEACTIVATE_TYPE_SLEEP;
                NfcTag::getInstance().setDeactivationState (eventData->deactivated);
                if (gIsTagDeactivating)
                {
                    NfcTag::getInstance().setActive(false);
                    nativeNfcTag_doDeactivateStatus(0);
                }
            }
            /* sNfaEnableDisablePollingEvent shall be notified in all cases
             * otherwise RF stop activity will block wait */
            SyncEventGuard guard (sNfaEnableDisablePollingEvent);
            sNfaEnableDisablePollingEvent.notifyOne ();
        }
        break;

        case NFA_DISC_RESULT_EVT:
        {
            status = eventData->disc_result.status;
            ALOGD("%s: NFA_DISC_RESULT_EVT: status = 0x%0X", __FUNCTION__, status);
            cur_more_val = eventData->disc_result.discovery_ntf.more;
            if((cur_more_val == 0x01) && (prev_more_val != 0x02))
            {
                ALOGE("%s: NFA_DISC_RESULT_EVT: Failed", __FUNCTION__);
                status = NFA_STATUS_FAILED;
            }
            else
            {
                ALOGD("%s: NFA_DISC_RESULT_EVT: Success", __FUNCTION__);
                status = NFA_STATUS_OK;
                prev_more_val = cur_more_val;
            }
#if (NXP_EXTNS == TRUE)
#if (NFC_NXP_NON_STD_CARD == TRUE)
            if (gIsSelectingRfInterface)
            {
                ALOGE("%s: NFA_DISC_RESULT_EVT: reSelect function didn't save the modification", __FUNCTION__);
                if(cur_more_val == 0x00)
                {
                    ALOGE("%s: NFA_DISC_RESULT_EVT: error, select any one tag", __FUNCTION__);
                    multiprotocol_flag = 0;
                }
            }
#endif
#endif
            if (status != NFA_STATUS_OK)
            {
                ALOGE("%s: NFA_DISC_RESULT_EVT: error, status = 0x%0X", __FUNCTION__, status);
                NfcTag::getInstance ().mNumDiscNtf = 0;
            }
            else
            {
                NfcTag::getInstance().connectionEventHandler(connEvent, eventData);
                handleRfDiscoveryEvent(&eventData->disc_result.discovery_ntf);
            }
        }
        break;

        case NFA_SELECT_RESULT_EVT:
        {
            ALOGD("%s: NFA_SELECT_RESULT_EVT: status = 0x%0X, gIsSelectingRfInterface = %d, sIsDisabling = %d", __FUNCTION__, eventData->status, gIsSelectingRfInterface, sIsDisabling);

            if (sIsDisabling)
                break;

            if (eventData->status != NFA_STATUS_OK)
            {
                if (gIsSelectingRfInterface)
                {
#if (NXP_EXTNS == TRUE)
#if (NFC_NXP_NON_STD_CARD == TRUE)
                    nativeNfcTag_cacheNonNciCardDetection();
#endif
#endif
                    nativeNfcTag_doConnectStatus(false);
                }
#if(NXP_EXTNS == TRUE)
                NfcTag::getInstance().selectCompleteStatus(false);
                NfcTag::getInstance ().mNumDiscNtf = 0x00;
#endif
                NfcTag::getInstance().mTechListIndex = 0;
                ALOGE("%s: NFA_SELECT_RESULT_EVT: error, status = 0x%0X", __FUNCTION__, eventData->status);
                NFA_Deactivate (FALSE);
            }
#if(NXP_EXTNS == TRUE)
            else if (sReaderModeEnabled && (gFelicaReaderState == STATE_DEACTIVATED_TO_SLEEP))
            {
                SyncEventGuard g (sRespCbEvent);
                ALOGD("%s: Sending Sem Post for Select Event", __FUNCTION__);
                sRespCbEvent.notifyOne ();
                ALOGD("%s: NFA_SELECT_RESULT_EVT: Frame RF Interface Selected", __FUNCTION__);
                gFelicaReaderState = STATE_FRAMERF_INTF_SELECTED;
            }
#endif
        }
        break;

        case NFA_DEACTIVATE_FAIL_EVT:
        {
            ALOGD("%s: NFA_DEACTIVATE_FAIL_EVT: status = 0x%0X", __FUNCTION__, eventData->status);
            {
                SyncEventGuard guard (gDeactivatedEvent);
                gActivated = false;
                gDeactivatedEvent.notifyOne ();
            }
            {
                SyncEventGuard guard (sNfaEnableDisablePollingEvent);
                sNfaEnableDisablePollingEvent.notifyOne ();
            }
        }
        break;

        case NFA_ACTIVATED_EVT:
        {
            ALOGD("%s: NFA_ACTIVATED_EVT: gIsSelectingRfInterface=%d, sIsDisabling=%d", __FUNCTION__, gIsSelectingRfInterface, sIsDisabling);
#if(NXP_EXTNS == TRUE)
            rfActivation = true;

            checkforTranscation(NFA_ACTIVATED_EVT, (void *)eventData);

            NfcTag::getInstance().selectCompleteStatus(true);

            /***P2P-Prio Logic for Multiprotocol***/
            if( (eventData->activated.activate_ntf.protocol == NFA_PROTOCOL_NFC_DEP) && (multiprotocol_detected == 1) )
            {
                ALOGD("Prio_Logic_multiprotocol stop timer");
                multiprotocol_timer.kill();
            }

            if( (eventData->activated.activate_ntf.protocol == NFA_PROTOCOL_T3T) && (multiprotocol_detected == 1) )
            {
                ALOGD("Prio_Logic_multiprotocol stop timer");
                multiprotocol_timer.kill();
                clear_multiprotocol();
            }
#endif
#if ((NFC_NXP_ESE ==  TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
            /*
             * Handle Reader over SWP START_READER_EVENT
             * */
            if(eventData->activated.activate_ntf.intf_param.type == NCI_INTERFACE_UICC_DIRECT || eventData->activated.activate_ntf.intf_param.type == NCI_INTERFACE_ESE_DIRECT )
            {
                SecureElement::getInstance().notifyEEReaderEvent(NFA_RD_SWP_READER_START, eventData->activated.activate_ntf.rf_tech_param.mode);
                gReaderNotificationflag = true;
                break;
            }
#endif
            if((eventData->activated.activate_ntf.protocol != NFA_PROTOCOL_NFC_DEP) && (!isListenMode(eventData->activated)))
            {
                nativeNfcTag_setRfInterface(eventData->activated);
            }

            if (EXTNS_GetConnectFlag() == TRUE)
            {
                NfcTag::getInstance().setActivationState ();
                nativeNfcTag_doConnectStatus(true);
                break;
            }

            NfcTag::getInstance().setActive(true);

            if (sIsDisabling || !sIsNfaEnabled)
                break;

            gActivated = true;

            NfcTag::getInstance().setActivationState ();

            if (gIsSelectingRfInterface)
            {
                nativeNfcTag_doConnectStatus(true);
                if (NfcTag::getInstance ().isCashBeeActivated() == true || NfcTag::getInstance ().isEzLinkTagActivated() == true)
                {
                    NfcTag::getInstance().connectionEventHandler (NFA_ACTIVATED_UPDATE_EVT, eventData);
                }
                break;
            }

            nativeNfcTag_resetPresenceCheck();

            if (isPeerToPeer(eventData->activated))
            {
                if (sReaderModeEnabled)
                {
#if(NXP_EXTNS == TRUE)
                    /* If last transaction is complete or prev state is idle
                     * then proceed to next state*/
                    if (isActivatedTypeF(eventData->activated) &&
                            (sTechMask & NFA_TECHNOLOGY_MASK_F) &&
                            ((gFelicaReaderState == STATE_IDLE) ||
                                    (gFelicaReaderState == STATE_FRAMERF_INTF_SELECTED)))
                    {
                        ALOGD("%s: Activating Reader Mode in P2P ", __FUNCTION__);
                        gFelicaReaderState = STATE_NFCDEP_ACTIVATED_NFCDEP_INTF;
                        switchP2PToT3TRead(eventData->activated.activate_ntf.rf_disc_id);
                    }
                    else
                    {
                        ALOGD("%s: Invalid FelicaReaderState : %d  ", __FUNCTION__,gFelicaReaderState);
                        gFelicaReaderState = STATE_IDLE;
#endif
                        ALOGD("%s: Ignoring P2P target in reader mode.", __FUNCTION__);
                        NFA_Deactivate (FALSE);
#if(NXP_EXTNS == TRUE)
                    }
#endif
                    break;
                }
                sP2pActive = true;
                ALOGD("%s: NFA_ACTIVATED_EVT: P2P is activated", __FUNCTION__);
#if((NFC_NXP_ESE == TRUE)&&(NXP_NFCC_ESE_UICC_CONCURRENT_ACCESS_PROTECTION == TRUE))
                if (SecureElement::getInstance().mIsWiredModeOpen && SecureElement::getInstance().mPassiveListenEnabled == true)
                {
                    SecureElement::getInstance().mPassiveListenTimer.kill();
                }
#endif
                /* For Secure Element, consider the field to be on while P2P is active */
                SecureElement::getInstance().notifyRfFieldEvent (true);
#if((NXP_EXTNS == TRUE) && (NFC_NXP_ESE == TRUE))
#if (NXP_ESE_DUAL_MODE_PRIO_SCHEME != NXP_ESE_WIRED_MODE_RESUME)
                SecureElement::getInstance().setDwpTranseiveState(false, NFCC_ACTIVATED_NTF);
#endif
#endif
            }
            else if (pn544InteropIsBusy() == false)
            {
#if(NXP_EXTNS == TRUE && NFC_NXP_NON_STD_CARD == TRUE)
                nativeNfcTag_handleNonNciMultiCardDetection(connEvent, eventData);
                ALOGD("%s: scoreGenericNtf = 0x%x", __FUNCTION__ ,scoreGenericNtf);
                if(scoreGenericNtf == true)
                {
                    if( (eventData->activated.activate_ntf.intf_param.type == NFC_INTERFACE_ISO_DEP) && (eventData->activated.activate_ntf.protocol == NFC_PROTOCOL_ISO_DEP) )
                    {
                        nativeNfcTag_handleNonNciCardDetection(eventData);
                    }
                    scoreGenericNtf = false;
                }
#else
                NfcTag::getInstance().connectionEventHandler (connEvent, eventData);

                if(NfcTag::getInstance ().mNumDiscNtf)
                {
                    NFA_Deactivate (TRUE);
                }
#endif
                /* We know it is not activating for P2P.  If it activated in
                 * listen mode then it is likely for an SE transaction.
                 * Send the RF Event */
                if (isListenMode(eventData->activated))
                {
                    sSeRfActive = true;
                    SecureElement::getInstance().notifyListenModeState (true);
#if((NFC_NXP_ESE == TRUE)&&(NXP_NFCC_ESE_UICC_CONCURRENT_ACCESS_PROTECTION == TRUE))
                    if (SecureElement::getInstance().mIsWiredModeOpen && SecureElement::getInstance().mPassiveListenEnabled == true)
                    {
                        SecureElement::getInstance().mPassiveListenTimer.kill();
                    }
#endif
                }
            }
        }
        break;

        case NFA_DEACTIVATED_EVT:
        {
            ALOGD("%s: NFA_DEACTIVATED_EVT: Type=%u, gIsTagDeactivating=%d", __FUNCTION__, eventData->deactivated.type, gIsTagDeactivating);
#if (NXP_EXTNS == TRUE)
            rfActivation = false;
#if (NFC_NXP_CHIP_TYPE == PN547C2)
            if(eventData->deactivated.type == NFA_DEACTIVATE_TYPE_IDLE)
            {
                checkforTranscation(NFA_DEACTIVATED_EVT, (void *)eventData);
            }
#endif
#if (NFC_NXP_NON_STD_CARD == TRUE)
            if(checkCmdSent == 1 && eventData->deactivated.type == 0)
            {
                ALOGD("%s: NFA_DEACTIVATED_EVT: Setting check flag  to one", __FUNCTION__);
                checkTagNtf = 1;
            }
#endif
#endif

            notifyPollingEventwhileNfcOff();

            if (true == getReconnectState())
            {
                ALOGD("Reconnect in progress : Do nothing");
                break;
            }

            gReaderNotificationflag = false;

#if(NXP_EXTNS == TRUE)
            /* P2P-priority logic for multiprotocol tags */
            if( (multiprotocol_detected == 1) && (sP2pActive == 1) )
            {
                NfcTag::getInstance ().mNumDiscNtf = 0;
                clear_multiprotocol();
                multiprotocol_flag = 1;
            }
            if(gIsWaiting4Deact2SleepNtf)
            {
                if(eventData->deactivated.type == NFA_DEACTIVATE_TYPE_IDLE)
                {
                    gGotDeact2IdleNtf = true;
                }
                else if(eventData->deactivated.type == NFA_DEACTIVATE_TYPE_SLEEP)
                {
                    gIsWaiting4Deact2SleepNtf = false;
                }
            }
#endif
            NfcTag::getInstance().setDeactivationState (eventData->deactivated);

            if(NfcTag::getInstance ().mNumDiscNtf)
            {
                NfcTag::getInstance ().mNumDiscNtf--;
                NfcTag::getInstance().selectNextTag();
            }

            if (eventData->deactivated.type != NFA_DEACTIVATE_TYPE_SLEEP)
            {
                {
                    SyncEventGuard guard (gDeactivatedEvent);
                    gActivated = false;
                    gDeactivatedEvent.notifyOne ();
                }
#if((NFC_NXP_ESE == TRUE)&&(NXP_NFCC_ESE_UICC_CONCURRENT_ACCESS_PROTECTION == TRUE))
                if (SecureElement::getInstance().mIsWiredModeOpen && SecureElement::getInstance().mPassiveListenEnabled)
                {
                    SecureElement::getInstance().startThread(0x00);
                }
#endif
                NfcTag::getInstance ().mNumDiscNtf = 0;
                NfcTag::getInstance ().mTechListIndex =0;
                nativeNfcTag_resetPresenceCheck();
                NfcTag::getInstance().connectionEventHandler (connEvent, eventData);
                nativeNfcTag_abortWaits();
                NfcTag::getInstance().abort ();
                NfcTag::getInstance().mIsMultiProtocolTag = false;
            }
            else if (gIsTagDeactivating)
            {
                NfcTag::getInstance ().setActive (false);
                nativeNfcTag_doDeactivateStatus (0);
            }
            else if (EXTNS_GetDeactivateFlag () == TRUE)
            {
                NfcTag::getInstance ().setActive (false);
                nativeNfcTag_doDeactivateStatus (0);
            }

            /* If RF is activated for what we think is a Secure Element transaction
             * and it is deactivated to either IDLE or DISCOVERY mode, notify wait event */
            if ( (eventData->deactivated.type == NFA_DEACTIVATE_TYPE_IDLE) ||
                 (eventData->deactivated.type == NFA_DEACTIVATE_TYPE_DISCOVERY) )
            {
#if(NXP_EXTNS == TRUE)
#if (NXP_NFCEE_REMOVED_NTF_RECOVERY == TRUE)
                if(RoutingManager::getInstance().is_ee_recovery_ongoing())
                {
                    recovery=FALSE;
                    SyncEventGuard guard (SecureElement::getInstance().mEEdatapacketEvent);
                    SecureElement::getInstance().mEEdatapacketEvent.notifyOne();
                }
#endif
#endif
                if (sSeRfActive)
                {
                    sSeRfActive = false;
                    if (!sIsDisabling && sIsNfaEnabled)
                        SecureElement::getInstance().notifyListenModeState (false);
                }
                else if (sP2pActive)
                {
                    sP2pActive = false;
                    SecureElement::getInstance().notifyRfFieldEvent (false);
                    ALOGD("%s: NFA_DEACTIVATED_EVT: is p2p", __FUNCTION__);
                }
            }
#if(NXP_EXTNS == TRUE)
            if (sReaderModeEnabled && (eventData->deactivated.type == NFA_DEACTIVATE_TYPE_SLEEP))
            {
                if(gFelicaReaderState == STATE_NFCDEP_ACTIVATED_NFCDEP_INTF)
                {
                    SyncEventGuard g (sRespCbEvent);
                    ALOGD("%s: Sending Sem Post for Deactivated", __FUNCTION__);
                    sRespCbEvent.notifyOne ();
                    ALOGD("Switching to T3T\n");
                    gFelicaReaderState = STATE_DEACTIVATED_TO_SLEEP;
                }
                else
                {
                    ALOGD("%s: FelicaReaderState Invalid", __FUNCTION__);
                    gFelicaReaderState = STATE_IDLE;
                }
            }
#endif
        }
        break;

        case NFA_TLV_DETECT_EVT:
        {
            status = eventData->tlv_detect.status;
            ALOGD("%s: NFA_TLV_DETECT_EVT status = 0x%0X, protocol = %d, num_tlvs = %d, num_bytes = %d",
                 __FUNCTION__, status, eventData->tlv_detect.protocol,
                 eventData->tlv_detect.num_tlvs, eventData->tlv_detect.num_bytes);
            if (status != NFA_STATUS_OK)
            {
                ALOGE("%s: NFA_TLV_DETECT_EVT error: status = 0x%0X", __FUNCTION__, status);
            }
        }
        break;

        case NFA_NDEF_DETECT_EVT:
        {
            /* NDEF Detection procedure is completed,
             * if status is failure, it means the tag does not contain any or valid NDEF data
             * pass the failure status to the NFC Service */
            status = eventData->ndef_detect.status;
            ALOGD("%s: NFA_NDEF_DETECT_EVT status = 0x%0X, protocol = %u, "
                 "max_size = %lu, cur_size = %lu, flags = 0x%X", __FUNCTION__,
                 status,
                 eventData->ndef_detect.protocol, eventData->ndef_detect.max_size,
                 eventData->ndef_detect.cur_size, eventData->ndef_detect.flags);
            NfcTag::getInstance().connectionEventHandler (connEvent, eventData);
            nativeNfcTag_doCheckNdefResult(status,
                eventData->ndef_detect.max_size, eventData->ndef_detect.cur_size,
                eventData->ndef_detect.flags);
        }
        break;

        case NFA_DATA_EVT:
        {
            /* Data message received (for non-NDEF reads) */
            ALOGD("%s: NFA_DATA_EVT: status = 0x%X, len = %d", __FUNCTION__, eventData->status, eventData->data.len);
            nativeNfcTag_doTransceiveStatus(eventData->status, eventData->data.p_data, eventData->data.len);
        }
        break;

        case NFA_RW_INTF_ERROR_EVT:
        {
            ALOGD("%s: NFA_RW_INTF_ERROR_EVT", __FUNCTION__);
            nativeNfcTag_notifyRfTimeout();
            nativeNfcTag_doReadCompleted (NFA_STATUS_TIMEOUT);
        }
        break;

        case NFA_SELECT_CPLT_EVT:
        {
            status = eventData->status;
            ALOGD("%s: NFA_SELECT_CPLT_EVT: status = 0x%0X", __FUNCTION__, status);
            if (status != NFA_STATUS_OK)
            {
                ALOGE("%s: NFA_SELECT_CPLT_EVT error: status = 0x%0X", __FUNCTION__, status);
            }
        }
        break;

        case NFA_READ_CPLT_EVT:
        {
            ALOGD("%s: NFA_READ_CPLT_EVT: status = 0x%0X", __FUNCTION__, eventData->status);
            nativeNfcTag_doReadCompleted (eventData->status);
            NfcTag::getInstance().connectionEventHandler (connEvent, eventData);
        }
        break;

        case NFA_WRITE_CPLT_EVT:
        {
            ALOGD("%s: NFA_WRITE_CPLT_EVT: status = 0x%0X", __FUNCTION__, eventData->status);
            nativeNfcTag_doWriteStatus (eventData->status == NFA_STATUS_OK);
        }
        break;

        case NFA_SET_TAG_RO_EVT:
        {
            ALOGD("%s: NFA_SET_TAG_RO_EVT: status = 0x%0X", __FUNCTION__, eventData->status);
            nativeNfcTag_doMakeReadonlyResult(eventData->status);
        }
        break;

        case NFA_CE_NDEF_WRITE_START_EVT:
        {
            ALOGD("%s: NFA_CE_NDEF_WRITE_START_EVT: status: 0x%0X", __FUNCTION__, eventData->status);
            if (eventData->status != NFA_STATUS_OK)
                ALOGE("%s: NFA_CE_NDEF_WRITE_START_EVT error: status = 0x%0X", __FUNCTION__, eventData->status);
        }
        break;

        case NFA_CE_NDEF_WRITE_CPLT_EVT:
        {
            ALOGD("%s: NFA_CE_NDEF_WRITE_CPLT_EVT: len = %lu", __FUNCTION__, eventData->ndef_write_cplt.len);
        }
        break;

        case NFA_LLCP_ACTIVATED_EVT:
        {
            ALOGD("%s: NFA_LLCP_ACTIVATED_EVT: is_initiator: %d  remote_wks: %d, remote_lsc: %d, remote_link_miu: %d, local_link_miu: %d",
                 __FUNCTION__,
                 eventData->llcp_activated.is_initiator,
                 eventData->llcp_activated.remote_wks,
                 eventData->llcp_activated.remote_lsc,
                 eventData->llcp_activated.remote_link_miu,
                 eventData->llcp_activated.local_link_miu);
            PeerToPeer::getInstance().llcpActivatedHandler (getNative(0, 0), eventData->llcp_activated);
        }
        break;

        case NFA_LLCP_DEACTIVATED_EVT:
        {
            ALOGD("%s: NFA_LLCP_DEACTIVATED_EVT", __FUNCTION__);
            PeerToPeer::getInstance().llcpDeactivatedHandler (getNative(0, 0), eventData->llcp_deactivated);
        }
        break;

        case NFA_LLCP_FIRST_PACKET_RECEIVED_EVT:
        {
            ALOGD("%s: NFA_LLCP_FIRST_PACKET_RECEIVED_EVT", __FUNCTION__);
            PeerToPeer::getInstance().llcpFirstPacketHandler (getNative(0, 0));
        }
        break;

        case NFA_PRESENCE_CHECK_EVT:
        {
            ALOGD("%s: NFA_PRESENCE_CHECK_EVT", __FUNCTION__);
            nativeNfcTag_doPresenceCheckResult (eventData->status);
        }
        break;

        case NFA_FORMAT_CPLT_EVT:
        {
            ALOGD("%s: NFA_FORMAT_CPLT_EVT: status = 0x%0X", __FUNCTION__, eventData->status);
            nativeNfcTag_formatStatus (eventData->status == NFA_STATUS_OK);
        }
        break;

        case NFA_I93_CMD_CPLT_EVT:
        {
            ALOGD("%s: NFA_I93_CMD_CPLT_EVT: status = 0x%0X", __FUNCTION__, eventData->status);
        }
        break;

        case NFA_CE_UICC_LISTEN_CONFIGURED_EVT :
        {
            ALOGD("%s: NFA_CE_UICC_LISTEN_CONFIGURED_EVT : status = 0x%0X", __FUNCTION__, eventData->status);
            SecureElement::getInstance().connectionEventHandler (connEvent, eventData);
        }
        break;

        case NFA_CE_ESE_LISTEN_CONFIGURED_EVT :
        {
            ALOGD("%s: NFA_CE_ESE_LISTEN_CONFIGURED_EVT : status = 0x%0X", __FUNCTION__, eventData->status);
            SecureElement::getInstance().connectionEventHandler (connEvent, eventData);
        }
        break;

        case NFA_SET_P2P_LISTEN_TECH_EVT:
        {
            ALOGD("%s: NFA_SET_P2P_LISTEN_TECH_EVT", __FUNCTION__);
            PeerToPeer::getInstance().connectionEventHandler (connEvent, eventData);
        }
        break;

        case NFA_CE_LOCAL_TAG_CONFIGURED_EVT:
        {
            ALOGD("%s: NFA_CE_LOCAL_TAG_CONFIGURED_EVT", __FUNCTION__);
        }
        break;
#if((NXP_EXTNS == TRUE) && (NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
        case NFA_RECOVERY_EVT:
        {
            ALOGD("%s: NFA_RECOVERY_EVT: Discovery Started in lower layer:Updating status in JNI", __FUNCTION__);
            if(RoutingManager::getInstance().getEtsiReaederState() == STATE_SE_RDR_MODE_STOP_IN_PROGRESS)
            {
                ALOGD("%s: Reset the ETSI Reader State to STATE_SE_RDR_MODE_STOPPED", __FUNCTION__);
                RoutingManager::getInstance().setEtsiReaederState(STATE_SE_RDR_MODE_STOPPED);
            }
        }
        break;
#endif
#if((NXP_EXTNS == TRUE) && (NFC_NXP_ESE == TRUE) && (NXP_NFCC_ESE_UICC_CONCURRENT_ACCESS_PROTECTION == TRUE))
        case NFA_PASSIVE_LISTEN_DISABLED_EVT:
        {
            ALOGD("%s: NFA_PASSIVE_LISTEN_DISABLED_EVT", __FUNCTION__);
            SyncEventGuard g (SecureElement::getInstance().mPassiveListenEvt);
            SecureElement::getInstance().mPassiveListenEvt.notifyOne();
        }
        break;

        case NFA_LISTEN_ENABLED_EVT:
        {
            ALOGD("%s: NFA_LISTEN_ENABLED_EVT", __FUNCTION__);
            SyncEventGuard g (SecureElement::getInstance().mPassiveListenEvt);
            SecureElement::getInstance().mPassiveListenEvt.notifyOne();
        }
        break;
#endif
        default:
            ALOGE("%s: unknown event ????", __FUNCTION__);
        break;
    }
}


/*******************************************************************************
**
** Function:        nfcManager_initNativeStruc
**
** Description:     Initialize variables.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_initNativeStruc (JNIEnv* e, jobject o)
{
    ALOGD ("%s: enter", __FUNCTION__);

#if((NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE) && (NXP_EXTNS == TRUE))
    nat = (nfc_jni_native_data*)malloc(sizeof(struct nfc_jni_native_data));
#else
    nfc_jni_native_data* nat = (nfc_jni_native_data*)malloc(sizeof(struct nfc_jni_native_data));
#endif
    if (nat == NULL)
    {
        ALOGE ("%s: fail allocate native data", __FUNCTION__);
        return JNI_FALSE;
    }

    memset (nat, 0, sizeof(*nat));
    e->GetJavaVM(&(nat->vm));
    nat->env_version = e->GetVersion();
    nat->manager = e->NewGlobalRef(o);

    ScopedLocalRef<jclass> cls(e, e->GetObjectClass(o));
    jfieldID f = e->GetFieldID(cls.get(), "mNative", "J");
    e->SetLongField(o, f, (jlong)nat);

    /* Initialize native cached references */
    gCachedNfcManagerNotifyNdefMessageListeners = e->GetMethodID(cls.get(),
            "notifyNdefMessageListeners", "(Lcom/android/nfc/dhimpl/NativeNfcTag;)V");
    gCachedNfcManagerNotifyTransactionListeners = e->GetMethodID(cls.get(),
            "notifyTransactionListeners", "([B[BI)V");
    gCachedNfcManagerNotifyConnectivityListeners = e->GetMethodID(cls.get(),
                "notifyConnectivityListeners", "(I)V");
    gCachedNfcManagerNotifyEmvcoMultiCardDetectedListeners = e->GetMethodID(cls.get(),
                "notifyEmvcoMultiCardDetectedListeners", "()V");
    gCachedNfcManagerNotifyLlcpLinkActivation = e->GetMethodID(cls.get(),
            "notifyLlcpLinkActivation", "(Lcom/android/nfc/dhimpl/NativeP2pDevice;)V");
    gCachedNfcManagerNotifyLlcpLinkDeactivated = e->GetMethodID(cls.get(),
            "notifyLlcpLinkDeactivated", "(Lcom/android/nfc/dhimpl/NativeP2pDevice;)V");
    gCachedNfcManagerNotifyLlcpFirstPacketReceived = e->GetMethodID(cls.get(),
            "notifyLlcpLinkFirstPacketReceived", "(Lcom/android/nfc/dhimpl/NativeP2pDevice;)V");
    sCachedNfcManagerNotifyTargetDeselected = e->GetMethodID(cls.get(),
            "notifyTargetDeselected","()V");
    gCachedNfcManagerNotifySeFieldActivated = e->GetMethodID(cls.get(),
            "notifySeFieldActivated", "()V");
    gCachedNfcManagerNotifySeFieldDeactivated = e->GetMethodID(cls.get(),
            "notifySeFieldDeactivated", "()V");
    gCachedNfcManagerNotifySeListenActivated = e->GetMethodID(cls.get(),
            "notifySeListenActivated", "()V");
    gCachedNfcManagerNotifySeListenDeactivated = e->GetMethodID(cls.get(),
            "notifySeListenDeactivated", "()V");

    gCachedNfcManagerNotifyHostEmuActivated = e->GetMethodID(cls.get(),
            "notifyHostEmuActivated", "(I)V");

    gCachedNfcManagerNotifyAidRoutingTableFull = e->GetMethodID(cls.get(),
            "notifyAidRoutingTableFull", "()V");

    gCachedNfcManagerNotifyHostEmuData = e->GetMethodID(cls.get(),
            "notifyHostEmuData", "(I[B)V");

    gCachedNfcManagerNotifyHostEmuDeactivated = e->GetMethodID(cls.get(),
            "notifyHostEmuDeactivated", "(I)V");

    gCachedNfcManagerNotifyRfFieldActivated = e->GetMethodID(cls.get(),
            "notifyRfFieldActivated", "()V");
    gCachedNfcManagerNotifyRfFieldDeactivated = e->GetMethodID(cls.get(),
            "notifyRfFieldDeactivated", "()V");

    sCachedNfcManagerNotifySeApduReceived = e->GetMethodID(cls.get(),
            "notifySeApduReceived", "([B)V");

    sCachedNfcManagerNotifySeMifareAccess = e->GetMethodID(cls.get(),
            "notifySeMifareAccess", "([B)V");

    sCachedNfcManagerNotifySeEmvCardRemoval =  e->GetMethodID(cls.get(),
            "notifySeEmvCardRemoval", "()V");

    gCachedNfcManagerNotifySWPReaderRequested = e->GetMethodID (cls.get(),
            "notifySWPReaderRequested", "(ZZ)V");

    gCachedNfcManagerNotifySWPReaderRequestedFail= e->GetMethodID (cls.get(),
            "notifySWPReaderRequestedFail", "(I)V");

    gCachedNfcManagerNotifySWPReaderActivated = e->GetMethodID (cls.get(),
            "notifySWPReaderActivated", "()V");
#if(NXP_EXTNS == TRUE)
    gCachedNfcManagerNotifyReRoutingEntry = e->GetMethodID(cls.get(),
            "notifyReRoutingEntry", "()V");
#if((NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
    gCachedNfcManagerNotifyETSIReaderModeStartConfig = e->GetMethodID (cls.get(),
            "notifyonETSIReaderModeStartConfig", "(I)V");

    gCachedNfcManagerNotifyETSIReaderModeStopConfig = e->GetMethodID (cls.get(),
            "notifyonETSIReaderModeStopConfig", "(I)V");

    gCachedNfcManagerNotifyETSIReaderModeSwpTimeout = e->GetMethodID (cls.get(),
            "notifyonETSIReaderModeSwpTimeout", "(I)V");
#endif
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
    gCachedNfcManagerNotifyUiccStatusEvent= e->GetMethodID (cls.get(),
            "notifyUiccStatusEvent", "(I)V");
#endif
#if(NXP_NFCC_HCE_F == TRUE)
    gCachedNfcManagerNotifyT3tConfigure = e->GetMethodID(cls.get(),
            "notifyT3tConfigure", "()V");
#endif
#if (NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE)
    gCachedNfcManagerNotifyJcosDownloadInProgress = e->GetMethodID(cls.get(),
            "notifyJcosDownloadInProgress", "(I)V");
#endif
#if(NXP_NFCC_SPI_FW_DOWNLOAD_SYNC == TRUE)
    gCachedNfcManagerNotifyFwDwnldRequested = e->GetMethodID(cls.get(),
            "notifyFwDwnldRequested", "()V");
#endif
#endif
    if (nfc_jni_cache_object(e, gNativeNfcTagClassName, &(nat->cached_NfcTag)) == -1)
    {
        ALOGE ("%s: fail cache NativeNfcTag", __FUNCTION__);
        return JNI_FALSE;
    }

    if (nfc_jni_cache_object(e, gNativeP2pDeviceClassName, &(nat->cached_P2pDevice)) == -1)
    {
        ALOGE ("%s: fail cache NativeP2pDevice", __FUNCTION__);
        return JNI_FALSE;
    }

    gNativeData = getNative(e,o);
    ALOGD ("%s: exit", __FUNCTION__);
    return JNI_TRUE;
}


/*******************************************************************************
**
** Function:        nfaDeviceManagementCallback
**
** Description:     Receive device management events from stack.
**                  dmEvent: Device-management event ID.
**                  eventData: Data associated with event ID.
**
** Returns:         None
**
*******************************************************************************/
void nfaDeviceManagementCallback (UINT8 dmEvent, tNFA_DM_CBACK_DATA* eventData)
{
    ALOGD ("%s: enter; event=0x%X", __FUNCTION__, dmEvent);

    switch (dmEvent)
    {
    case NFA_DM_ENABLE_EVT: /* Result of NFA_Enable */
        {
            SyncEventGuard guard (sNfaEnableEvent);
            ALOGD ("%s: NFA_DM_ENABLE_EVT; status=0x%X",
                    __FUNCTION__, eventData->status);
            sIsNfaEnabled = eventData->status == NFA_STATUS_OK;
            sIsDisabling = false;
            sNfaEnableEvent.notifyOne ();
        }
        break;

    case NFA_DM_DISABLE_EVT: /* Result of NFA_Disable */
        {
            SyncEventGuard guard (sNfaDisableEvent);
            ALOGD ("%s: NFA_DM_DISABLE_EVT", __FUNCTION__);
            sIsNfaEnabled = false;
            sIsDisabling = false;
            sNfaDisableEvent.notifyOne ();
        }
        break;

    case NFA_DM_SET_CONFIG_EVT: //result of NFA_SetConfig
        ALOGD ("%s: NFA_DM_SET_CONFIG_EVT", __FUNCTION__);
        {
            SyncEventGuard guard (sNfaSetConfigEvent);
            sNfaSetConfigEvent.notifyOne();
        }
        break;

    case NFA_DM_GET_CONFIG_EVT: /* Result of NFA_GetConfig */
        ALOGD ("%s: NFA_DM_GET_CONFIG_EVT", __FUNCTION__);
        {
            HciRFParams::getInstance().connectionEventHandler(dmEvent,eventData);
            SyncEventGuard guard (sNfaGetConfigEvent);
            if (eventData->status == NFA_STATUS_OK &&
                    eventData->get_config.tlv_size <= sizeof(sConfig))
            {
                sCurrentConfigLen = eventData->get_config.tlv_size;
                memcpy(sConfig, eventData->get_config.param_tlvs, eventData->get_config.tlv_size);

#if(NXP_EXTNS == TRUE)
                if(sCheckNfceeFlag)
                    checkforNfceeBuffer();
#endif
            }
            else
            {
                ALOGE("%s: NFA_DM_GET_CONFIG failed", __FUNCTION__);
                sCurrentConfigLen = 0;
            }
            sNfaGetConfigEvent.notifyOne();
        }
        break;

    case NFA_DM_RF_FIELD_EVT:
        checkforTranscation(NFA_TRANS_DM_RF_FIELD_EVT, (void *)eventData);
        ALOGD ("%s: NFA_DM_RF_FIELD_EVT; status=0x%X; field status=%u", __FUNCTION__,
              eventData->rf_field.status, eventData->rf_field.rf_field_status);
        if (sIsDisabling || !sIsNfaEnabled)
            break;

        if (!sP2pActive && eventData->rf_field.status == NFA_STATUS_OK)
        {
            SecureElement::getInstance().notifyRfFieldEvent (
                    eventData->rf_field.rf_field_status == NFA_DM_RF_FIELD_ON);
            struct nfc_jni_native_data *nat = getNative(NULL, NULL);
            JNIEnv* e = NULL;
            ScopedAttach attach(nat->vm, &e);
            if (e == NULL)
            {
                ALOGE ("jni env is null");
                return;
            }
            if (eventData->rf_field.rf_field_status == NFA_DM_RF_FIELD_ON)
             {
                if(!update_transaction_stat("RF_FIELD_EVT",SET_TRANSACTION_STATE))
                {
                    ALOGD ("%s: RF field on evnt Not allowing to set", __FUNCTION__);
                }
                sRfFieldOff = false;
                e->CallVoidMethod (nat->manager, android::gCachedNfcManagerNotifyRfFieldActivated);
             }
            else
            {
                if(!update_transaction_stat("RF_FIELD_EVT",RESET_TRANSACTION_STATE))
                {
                    ALOGD ("%s: RF field off evnt Not allowing to reset", __FUNCTION__);
                }
                sRfFieldOff = true;
                e->CallVoidMethod (nat->manager, android::gCachedNfcManagerNotifyRfFieldDeactivated);
            }
        }
        break;

    case NFA_DM_NFCC_TRANSPORT_ERR_EVT:
    case NFA_DM_NFCC_TIMEOUT_EVT:
        {
            if (dmEvent == NFA_DM_NFCC_TIMEOUT_EVT)
                ALOGE ("%s: NFA_DM_NFCC_TIMEOUT_EVT; abort", __FUNCTION__);
            else if (dmEvent == NFA_DM_NFCC_TRANSPORT_ERR_EVT)
                ALOGE ("%s: NFA_DM_NFCC_TRANSPORT_ERR_EVT; abort", __FUNCTION__);
#if (JCOP_WA_ENABLE == FALSE)
            NFA_HciW4eSETransaction_Complete(Wait);
#endif
            nativeNfcTag_abortWaits();
            NfcTag::getInstance().abort ();
            sAbortConnlessWait = true;
            nativeLlcpConnectionlessSocket_abortWait();
            {
                ALOGD ("%s: aborting  sNfaEnableDisablePollingEvent", __FUNCTION__);
                SyncEventGuard guard (sNfaEnableDisablePollingEvent);
                sNfaEnableDisablePollingEvent.notifyOne();
            }
            {
                ALOGD ("%s: aborting  sNfaEnableEvent", __FUNCTION__);
                SyncEventGuard guard (sNfaEnableEvent);
                sNfaEnableEvent.notifyOne();
            }
            {
                ALOGD ("%s: aborting  sNfaDisableEvent", __FUNCTION__);
                SyncEventGuard guard (sNfaDisableEvent);
                sNfaDisableEvent.notifyOne();
            }
            sDiscoveryEnabled = false;
            sPollingEnabled = false;
            PowerSwitch::getInstance ().abort ();

            if (!sIsDisabling && sIsNfaEnabled)
            {
                EXTNS_Close ();
                NFA_Disable(FALSE);
                sIsDisabling = true;
            }
            else
            {
                sIsNfaEnabled = false;
                sIsDisabling = false;
            }
            PowerSwitch::getInstance ().initialize (PowerSwitch::UNKNOWN_LEVEL);
#if(NXP_EXTNS == TRUE)
            if(eventData->status == NFA_STATUS_FAILED)
            {
                ALOGE ("%s: Disabling NFC service", __FUNCTION__);
            }
            else
            {
#endif
                ALOGE ("%s: crash NFC service", __FUNCTION__);
                //////////////////////////////////////////////
                //crash the NFC service process so it can restart automatically
                abort ();
                //////////////////////////////////////////////
#if(NXP_EXTNS == TRUE)
            }
#endif
        }
        break;

    case NFA_DM_PWR_MODE_CHANGE_EVT:
        PowerSwitch::getInstance ().deviceManagementCallback (dmEvent, eventData);
        break;

#if(NXP_EXTNS == TRUE)
    case NFA_DM_SET_ROUTE_CONFIG_REVT:
        ALOGD ("%s: NFA_DM_SET_ROUTE_CONFIG_REVT; status=0x%X",
                __FUNCTION__, eventData->status);
        if(eventData->status != NFA_STATUS_OK)
        {
            ALOGD("AID Routing table configuration Failed!!!");
        }
        else
        {
            ALOGD("AID Routing Table configured.");
        }
        RoutingManager::getInstance().mLmrtEvent.notifyOne();
        break;

    case NFA_DM_GET_ROUTE_CONFIG_REVT:
    {
        RoutingManager::getInstance().processGetRoutingRsp(eventData,sRoutingBuff);
        if (eventData->status == NFA_STATUS_OK)
        {
            SyncEventGuard guard (sNfaGetRoutingEvent);
            sNfaGetRoutingEvent.notifyOne();
        }
        break;
    }
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
    case NFA_DM_EE_HCI_DISABLE:
    {
        ALOGD("NFA_DM_EE_HCI_DISABLE wait releasing");
        sNfceeHciCbDisableEvent.notifyOne();
        ALOGD("NFA_DM_EE_HCI_DISABLE wait released");
        break;
    }
    case NFA_DM_EE_HCI_ENABLE:
    {
        ALOGD("NFA_DM_EE_HCI_ENABLE wait releasing");
        sNfceeHciCbEnableEvent.notifyOne();
        ALOGD("NFA_DM_EE_HCI_ENABLE wait released");
        break;
    }
#endif
#endif

    case NFA_DM_EMVCO_PCD_COLLISION_EVT:
        ALOGD("STATUS_EMVCO_PCD_COLLISION - Multiple card detected");
        SecureElement::getInstance().notifyEmvcoMultiCardDetectedListeners();
        break;

    default:
        ALOGD ("%s: unhandled event", __FUNCTION__);
        break;
    }
}

/*******************************************************************************
**
** Function:        nfcManager_sendRawFrame
**
** Description:     Send a raw frame.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_sendRawFrame (JNIEnv* e, jobject, jbyteArray data)
{
    size_t bufLen = 0x00;
    uint8_t* buf = NULL;
    if(data != NULL)
    {
        ScopedByteArrayRO bytes(e, data);
        buf = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&bytes[0]));
        bufLen = bytes.size();
    }
#if((NXP_EXTNS == TRUE) && (NXP_NFCC_EMPTY_DATA_PACKET == TRUE))
    RoutingManager::getInstance().mNfcFRspTimer.kill();
    if(bufLen == 0)
        gIsEmptyRspSentByHceFApk = TRUE;
#endif
    ALOGD("nfcManager_sendRawFrame(): bufLen:%lu", bufLen);
    tNFA_STATUS status = NFA_SendRawFrame (buf, bufLen, 0);
    return (status == NFA_STATUS_OK);
}

/*******************************************************************************
**
** Function:        nfcManager_routeAid
**
** Description:     Route an AID to an EE
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_routeAid (JNIEnv* e, jobject, jbyteArray aid, jint route, jint power, jboolean isprefix)
{
    ScopedByteArrayRO bytes(e, aid);
    uint8_t* buf = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&bytes[0]));
    size_t bufLen = bytes.size();
#if(NFC_NXP_STAT_DUAL_UICC_WO_EXT_SWITCH == TRUE)
    if(route == 2 || route == 4) { //UICC or UICC2 HANDLE
        ALOGD ("sCurrentSelectedUICCSlot:  %d", sCurrentSelectedUICCSlot);
        route = (sCurrentSelectedUICCSlot != 0x02) ? 0x02 : 0x04;
    }
#endif
#if(NXP_EXTNS == TRUE)
    if(nfcManager_isTransanctionOnGoing(TRUE))
    {
       return FALSE;
    }
    bool result = RoutingManager::getInstance().addAidRouting(buf, bufLen, route, power, isprefix);
#else
    bool result = RoutingManager::getInstance().addAidRouting(buf, bufLen, route);

#endif
    return result;
}

/*******************************************************************************
**
** Function:        nfcManager_unrouteAid
**
** Description:     Remove a AID routing
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_unrouteAid (JNIEnv* e, jobject, jbyteArray aid)
{
    ScopedByteArrayRO bytes(e, aid);
    uint8_t* buf = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&bytes[0]));
    size_t bufLen = bytes.size();
    bool result = RoutingManager::getInstance().removeAidRouting(buf, bufLen);
    return result;
}

#if(NXP_EXTNS == TRUE)
/*******************************************************************************
**
** Function:        nfcManager_setRoutingEntry
**
** Description:     Set the routing entry in routing table
**                  e: JVM environment.
**                  o: Java object.
**                  type: technology or protocol routing
**                       0x01 - Technology
**                       0x02 - Protocol
**                  value: technology /protocol value
**                  route: routing destination
**                       0x00 : Device Host
**                       0x01 : ESE
**                       0x02 : UICC
**                  power: power state for the routing entry
*******************************************************************************/

static jboolean nfcManager_setRoutingEntry (JNIEnv*, jobject, jint type, jint value, jint route, jint power)
{
    jboolean result = FALSE;

    result = RoutingManager::getInstance().setRoutingEntry(type, value, route, power);
    return result;
}
/*******************************************************************************
**
** Function:        nfcManager_clearRoutingEntry
**
** Description:     Set the routing entry in routing table
**                  e: JVM environment.
**                  o: Java object.
**                  type:technology/protocol/aid clear routing
**
*******************************************************************************/

static jboolean nfcManager_clearRoutingEntry (JNIEnv*, jobject, jint type)
{
    jboolean result = FALSE;

    result = RoutingManager::getInstance().clearRoutingEntry(type);
    return result;
}
#endif

/*******************************************************************************
**
** Function:        nfcManager_setDefaultRoute
**
** Description:     Set the default route in routing table
**                  e: JVM environment.
**                  o: Java object.
**
*******************************************************************************/

static jboolean nfcManager_setDefaultRoute (JNIEnv*, jobject, jint defaultRouteEntry, jint defaultProtoRouteEntry, jint defaultTechRouteEntry)
{
    jboolean result = FALSE;
    ALOGD ("%s : enter", __FUNCTION__);
#if (NXP_EXTNS == TRUE)
    if(!update_transaction_stat("setDefaultRoute",SET_TRANSACTION_STATE))
    {
        ALOGE("%s : Transaction in progress, Store the request",__FUNCTION__);
        set_last_request(RE_ROUTING, NULL);
        return result;
    }
#endif
    if (sRfEnabled)
    {
        // Stop RF discovery to reconfigure
        startRfDiscovery(false);
    }

#if (NXP_EXTNS == TRUE)
    result = RoutingManager::getInstance().setDefaultRoute(defaultRouteEntry, defaultProtoRouteEntry, defaultTechRouteEntry);
    if(result)
        result = RoutingManager::getInstance().commitRouting();
    else
        ALOGD ("%s : Commit routing failed ", __FUNCTION__);
#else
    result = RoutingManager::getInstance().setDefaultRouting();
#endif

    startRfDiscovery(true);
#if (NXP_EXTNS == TRUE)
    if(!update_transaction_stat("setDefaultRoute",RESET_TRANSACTION_STATE))
    {
        ALOGE("%s: Can not reset transaction state", __FUNCTION__);
    }
#endif
    ALOGD ("%s : exit", __FUNCTION__);
    return result;
}

/*******************************************************************************
**
** Function:        nfcManager_getAidTableSize
** Description:     Get the maximum supported size for AID routing table.
**
**                  e: JVM environment.
**                  o: Java object.
**
*******************************************************************************/
static jint nfcManager_getAidTableSize (JNIEnv*, jobject )
{
    return NFA_GetAidTableSize();
}

/*******************************************************************************
**
** Function:        nfcManager_getRemainingAidTableSize
** Description:     Get the remaining size of AID routing table.
**
**                  e: JVM environment.
**                  o: Java object.
**
*******************************************************************************/
static jint nfcManager_getRemainingAidTableSize (JNIEnv* , jobject )
{
    return NFA_GetRemainingAidTableSize();
}
/*******************************************************************************
**
** Function:        nfcManager_clearAidTable
**
** Description:     Clean all AIDs in routing table
**                  e: JVM environment.
**                  o: Java object.
**
*******************************************************************************/
static bool nfcManager_clearAidTable (JNIEnv*, jobject)
{
#if(NXP_EXTNS == TRUE)
    if(nfcManager_isTransanctionOnGoing(TRUE))
    {
       return FALSE;
    }
#endif
    return RoutingManager::getInstance().clearAidTable();
}
/*******************************************************************************
**
** Function:        nfcManager_doRegisterT3tIdentifier
**
** Description:     Registers LF_T3T_IDENTIFIER for NFC-F.
**                  e: JVM environment.
**                  o: Java object.
**                  t3tIdentifier: LF_T3T_IDENTIFIER value (10 or 18 bytes)
**
** Returns:         Handle retrieve from RoutingManager.
**
*******************************************************************************/
static jint nfcManager_doRegisterT3tIdentifier(JNIEnv* e, jobject, jbyteArray t3tIdentifier)
{
    ALOGD ("%s: enter", __FUNCTION__);
    ScopedByteArrayRO bytes(e, t3tIdentifier);
    uint8_t* buf = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&bytes[0]));
    size_t bufLen = bytes.size();
    int handle = RoutingManager::getInstance().registerT3tIdentifier(buf, bufLen);
    ALOGD ("%s: exit", __FUNCTION__);
    return handle;
}

/*******************************************************************************
**
** Function:        nfcManager_doDeregisterT3tIdentifier
**
** Description:     Deregisters LF_T3T_IDENTIFIER for NFC-F.
**                  e: JVM environment.
**                  o: Java object.
**                  handle: Handle retrieve from libnfc-nci.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doDeregisterT3tIdentifier(JNIEnv*, jobject, jint handle)
{
    ALOGD ("%s: enter", __FUNCTION__);
    RoutingManager::getInstance().deregisterT3tIdentifier(handle);
    ALOGD ("%s: exit", __FUNCTION__);
}

/*******************************************************************************
**
** Function:        nfcManager_getLfT3tMax
**
** Description:     Returns LF_T3T_MAX value.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         LF_T3T_MAX value.
**
*******************************************************************************/
static jint nfcManager_getLfT3tMax(JNIEnv*, jobject)
{
    return sLfT3tMax;
}

/*******************************************************************************
**
** Function:        nfcManager_doInitialize
**
** Description:     Turn on NFC.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_doInitialize (JNIEnv* e, jobject o)
{
    tNFA_MW_VERSION mwVer;
    gSeDiscoverycount = 0;
    gActualSeCount = 0;
    UINT8 configData = 0;

#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
    UINT8 switchToUiccSlot = 0;
#endif
#if(NXP_EXTNS == TRUE)
    rfActivation = false;
    tNFA_PMID ven_config_addr[]  = {0xA0, 0x07};
    bool isSuccess = false;
    sNfcee_disc_state = UICC_SESSION_NOT_INTIALIZED;
#endif
    ALOGD ("%s: enter; ver=%s nfa=%s NCI_VERSION=0x%02X",
        __FUNCTION__, nfca_version_string, nfa_version_string, NCI_VERSION);
    mwVer=  NFA_GetMwVersion();
    ALOGD ("%s:  MW Version: NFC_NCIHALx_AR%X.%x.%x.%x",
            __FUNCTION__, mwVer.validation, mwVer.android_version,
            mwVer.major_version,mwVer.minor_version);

    tNFA_STATUS stat = NFA_STATUS_OK;
    NfcTag::getInstance ().mNfcDisableinProgress = false;
    PowerSwitch & powerSwitch = PowerSwitch::getInstance ();
#if((NFC_NXP_ESE == TRUE)&&(NXP_EXTNS == TRUE))
    struct sigaction sig;

    memset(&sig, 0, sizeof(struct sigaction));
    sig.sa_sigaction = spi_prio_signal_handler;
    sig.sa_flags = SA_SIGINFO;
    if(sigaction(SIG_NFC, &sig, NULL) < 0)
    {
        ALOGE("Failed to register spi prio session signal handler");
    }
#endif
    if (sIsNfaEnabled)
    {
        ALOGD ("%s: already enabled", __FUNCTION__);
        goto TheEnd;
    }
#if(NXP_EXTNS == TRUE)
    if(gsNfaPartialEnabled)
    {
        ALOGD ("%s: already  partial enable calling deinitialize", __FUNCTION__);
        nfcManager_doPartialDeInitialize();
    }
#endif
#if (JCOP_WA_ENABLE == FALSE)
if ((signal(SIGABRT, sig_handler) == SIG_ERR) &&
        (signal(SIGSEGV, sig_handler) == SIG_ERR))
    {
        ALOGE("Failed to register signal handler");
     }
#endif
    powerSwitch.initialize (PowerSwitch::FULL_POWER);

    {
        unsigned long num = 0;

        NfcAdaptation& theInstance = NfcAdaptation::GetInstance();
        theInstance.Initialize(); //start GKI, NCI task, NFC task

        {
#if(NXP_EXTNS == TRUE)
#if((NFC_NXP_ESE == TRUE) && (NXP_WIRED_MODE_STANDBY == TRUE))
            int state = getJCOPOS_UpdaterState();
            if((state != OSU_COMPLETE) &&
               (state != OSU_NOT_STARTED))
            {
                ALOGD("JCOP is in OSU mode");
                NFA_SetBootMode(NFA_OSU_BOOT_MODE);
            }
            else
#endif
            {
                NFA_SetBootMode(NFA_NORMAL_BOOT_MODE);
            }
#endif
            SyncEventGuard guard (sNfaEnableEvent);
            tHAL_NFC_ENTRY* halFuncEntries = theInstance.GetHalEntryFuncs ();
            NFA_Init (halFuncEntries);
            stat = NFA_Enable (nfaDeviceManagementCallback, nfaConnectionCallback);
            if (stat == NFA_STATUS_OK)
            {
                num = initializeGlobalAppLogLevel ();
                CE_SetTraceLevel (num);
                LLCP_SetTraceLevel (num);
                NFC_SetTraceLevel (num);
                RW_SetTraceLevel (num);
                NFA_SetTraceLevel (num);
                NFA_P2pSetTraceLevel (num);
                sNfaEnableEvent.wait(); //wait for NFA command to finish
            }
            EXTNS_Init (nfaDeviceManagementCallback, nfaConnectionCallback);
        }

        if (stat == NFA_STATUS_OK )
        {
            //sIsNfaEnabled indicates whether stack started successfully
            if (sIsNfaEnabled)
            {
                SecureElement::getInstance().initialize (getNative(e, o));
                //setListenMode();
                RoutingManager::getInstance().initialize(getNative(e, o));
                HciRFParams::getInstance().initialize ();
                sIsSecElemSelected = (SecureElement::getInstance().getActualNumEe() - 1 );
                sIsSecElemDetected = sIsSecElemSelected;
                nativeNfcTag_registerNdefTypeHandler ();
                NfcTag::getInstance().initialize (getNative(e, o));
                PeerToPeer::getInstance().initialize ();
                PeerToPeer::getInstance().handleNfcOnOff (true);
#if(NXP_EXTNS == TRUE)
                if(GetNxpNumValue(NAME_NXP_DEFAULT_NFCEE_DISC_TIMEOUT, (void *)&gdisc_timeout, sizeof(gdisc_timeout))==false)
                {
                    ALOGD ("NAME_NXP_DEFAULT_NFCEE_DISC_TIMEOUT not found");
                    gdisc_timeout = NFCEE_DISC_TIMEOUT_SEC; /*Default nfcee discover timeout*/
                }
                gdisc_timeout = gdisc_timeout * 1000;
                if (NFA_STATUS_OK == GetNumNFCEEConfigured())
                {
                    ALOGD(" gSeDiscoverycount = %d gActualSeCount=%d", gSeDiscoverycount,gActualSeCount);
                    if (gSeDiscoverycount < gActualSeCount)
                    {
                        ALOGD("Wait for ESE to discover, gdisc_timeout = %d", gdisc_timeout);
                        SyncEventGuard g(gNfceeDiscCbEvent);
                        if(gNfceeDiscCbEvent.wait(gdisc_timeout) == false)
                        {
                            ALOGE ("%s: timeout waiting for nfcee dis event", __FUNCTION__);
                        }
                        ALOGD("gSeDiscoverycount  = %d gActualSeCount=%d", gSeDiscoverycount,gActualSeCount);
                    }
                    else
                    {
                        ALOGD("All ESE are discovered ");
                    }
                }
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
                GetNxpNumValue (NAME_NXP_DUAL_UICC_ENABLE, (void*)&dualUiccInfo.dualUiccEnable, sizeof(dualUiccInfo.dualUiccEnable));
                if(dualUiccInfo.dualUiccEnable == 0x01)
                {
                    checkforNfceeConfig(UICC1 | UICC2 | ESE);
                    dualUiccInfo.uiccActivStat = 0x00;
                    if(SecureElement::getInstance().getEeStatus(UICC_HANDLE)!=NFC_NFCEE_STATUS_REMOVED)
                    {
                        dualUiccInfo.uiccActivStat = (sSelectedUicc & 0x0F);
                    }
                    switchToUiccSlot = ((sSelectedUicc & 0x0F) == 0x01) ? 0x02 : 0x01;
                    nfcManager_doSelectUicc(e,o,switchToUiccSlot);
                    if(SecureElement::getInstance().getEeStatus(UICC_HANDLE)!=NFC_NFCEE_STATUS_REMOVED)
                    {
                        dualUiccInfo.uiccActivStat |= (sSelectedUicc & 0x0F);
                    }
                    uiccEventTimer.set (1, notifyUiccEvent);
                }
                else
#endif
                SecureElement::getInstance().updateEEStatus();
#if (JCOP_WA_ENABLE == TRUE)
                RoutingManager::getInstance().handleSERemovedNtf();
#endif
                ALOGD("Discovered se count %ld",gSeDiscoverycount);
                /*Check for ETSI12 Configuration for SEs detected in the HCI Network*/
                performNfceeETSI12Config();
#if (NFC_NXP_ESE_ETSI12_PROP_INIT == TRUE)
                if(swp_getconfig_status & SWP2_ESE)
                    performHCIInitialization (e,o);
#endif
                SecureElement::getInstance().getSETechnology(ESE_HANDLE);
                checkforNfceeConfig(UICC1 | UICC2 | ESE);
                /*Pending clear all pipe handling*/
                if(sNfcee_disc_state == UICC_CLEAR_ALL_PIPE_NTF_RECEIVED)
                {
                    ALOGD("Perform pending UICC clear all pipe handling");
                    sNfcee_disc_state = UICC_SESSION_INTIALIZATION_DONE;
                    /*Handle UICC clear all pipe notification*/
                    checkforNfceeConfig(UICC1 | UICC2);
                }
                    sNfcee_disc_state = UICC_SESSION_INTIALIZATION_DONE;
#endif
#if(NFC_NXP_GP_CONTINOUS_PROCESSING == TRUE)
                if(isNxpConfigModified())
                {
                    ALOGD("Set JCOP CP Timeout");
                    SecureElement::getInstance().setCPTimeout();
                }
                else
                {
                    ALOGD("No Need to set JCOP CP Timeout");
                }
#endif
                /////////////////////////////////////////////////////////////////////////////////
                // Add extra configuration here (work-arounds, etc.)
#if (NXP_EXTNS == FALSE)
#if (NFC_NXP_ESE ==  TRUE)
#if ((NXP_ESE_SVDD_SYNC == TRUE) || (NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE) || (NXP_NFCC_SPI_FW_DOWNLOAD_SYNC == TRUE)||(NXP_ESE_DWP_SPI_SYNC_ENABLE == TRUE))
                isSuccess = createSPIEvtHandlerThread();
#endif
#if (NXP_ESE_DWP_SPI_SYNC_ENABLE == TRUE)
                if(isSuccess)
                SecureElement::getInstance().enableDwp();
#else
                if(!isSuccess)
                    ALOGD("Failed to start SPI Event Handler Thread");
#endif
#endif
                    update_transaction_stat(NULL, RESET_TRANSACTION_STATE);
                    pendingScreenState = false;
                    {
                        SyncEventGuard guard (android::sNfaGetConfigEvent);
                        stat = NFA_GetConfig(0x01,ven_config_addr);
                        if(stat == NFA_STATUS_OK)
                        {
                            android::sNfaGetConfigEvent.wait();
                        }
                        /*sCurrentConfigLen should be > 4 (num_tlv:1 + addr:2 + value:1) and
                         *pos 4 gives the current eeprom value*/
                        if((sCurrentConfigLen > 4)&&(sConfig[4] == 0x03))
                        {
                            ALOGD ("%s: No need to update VEN_CONFIG. Already set to 0x%02x", __FUNCTION__,sConfig[4]);
                        }
                        else
                        {
                            SetVenConfigValue(NFC_MODE_ON);
                            if (stat != NFA_STATUS_OK)
                            {
                                ALOGE ("%s: fail enable SetVenConfigValue; error=0x%X", __FUNCTION__, stat);
                            }
                        }
                        gGeneralPowershutDown = 0;
                    }
                    if(gIsDtaEnabled == true){
                        configData = 0x01;    /**< Poll NFC-DEP : Highest Available Bit Rates */
                        NFA_SetConfig(NFC_PMID_BITR_NFC_DEP, sizeof(UINT8), &configData);
                        configData = 0x0B;    /**< Listen NFC-DEP : Waiting Time */
                        NFA_SetConfig(NFC_PMID_WT, sizeof(UINT8), &configData);
                        configData = 0x0F;    /**< Specific Parameters for NFC-DEP RF Interface */
                        NFA_SetConfig(NFC_PMID_NFC_DEP_OP, sizeof(UINT8), &configData);
                    }

#endif
                struct nfc_jni_native_data *nat = getNative(e, o);

                if ( nat )
                {
                    if (GetNumValue(NAME_POLLING_TECH_MASK, &num, sizeof(num)))
                        nat->tech_mask = num;
                    else
                        nat->tech_mask = DEFAULT_TECH_MASK;
                    ALOGD ("%s: tag polling tech mask=0x%X", __FUNCTION__, nat->tech_mask);
                }

                // if this value exists, set polling interval.
                if (GetNumValue(NAME_NFA_DM_DISC_DURATION_POLL, &num, sizeof(num)))
                    nat->discovery_duration = num;
                else
                    nat->discovery_duration = DEFAULT_DISCOVERY_DURATION;
#if(NXP_EXTNS == TRUE)
                discDuration = nat->discovery_duration;
#endif
                NFA_SetRfDiscoveryDuration(nat->discovery_duration);

                // get LF_T3T_MAX
                {
                    SyncEventGuard guard (sNfaGetConfigEvent);
                    tNFA_PMID configParam[1] = {NCI_PARAM_ID_LF_T3T_MAX};
                    stat = NFA_GetConfig(1, configParam);
                    if (stat == NFA_STATUS_OK)
                    {
                        sNfaGetConfigEvent.wait ();
                        if (sCurrentConfigLen >= 4 || sConfig[1] == NCI_PARAM_ID_LF_T3T_MAX) {
                            ALOGD("%s: lfT3tMax=%d", __FUNCTION__, sConfig[3]);
                            sLfT3tMax = sConfig[3];
                        }
                    }
                }
                if (GetNxpNumValue (NAME_NXP_CE_ROUTE_STRICT_DISABLE, (void*)&num, sizeof(num)) == false)
                    num = 0x01; // default value

//TODO: Check this in L_OSP_EXT[PN547C2]
//                NFA_SetCEStrictDisable(num);
                RoutingManager::getInstance().setCeRouteStrictDisable(num);

#if(NXP_EXTNS != TRUE)
                // Do custom NFCA startup configuration.
                doStartupConfig();
#endif
                goto TheEnd;
            }
        }

        ALOGE ("%s: fail nfa enable; error=0x%X", __FUNCTION__, stat);

        if (sIsNfaEnabled)
        {
            EXTNS_Close ();
            stat = NFA_Disable (FALSE /* ungraceful */);
        }

        theInstance.Finalize();
    }

TheEnd:
    if (sIsNfaEnabled)
        PowerSwitch::getInstance ().setLevel (PowerSwitch::LOW_POWER);
    ALOGD ("%s: exit", __FUNCTION__);
#if (NXP_EXTNS == TRUE)
    if (isNxpConfigModified())
    {
        updateNxpConfigTimestamp();
    }
#endif
    return sIsNfaEnabled ? JNI_TRUE : JNI_FALSE;
}

/*******************************************************************************
**
** Function:        nfcManager_doEnableDtaMode
**
** Description:     Enable the DTA mode in NFC service.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doEnableDtaMode (JNIEnv* /* e */, jobject /* o */)
{
    gIsDtaEnabled = true;
}

#if (NXP_EXTNS == FALSE)
/*******************************************************************************
 **
 ** Function:        nfcManager_checkNfcStateBusy()
 **
 ** Description      This function returns whether NFC process is busy or not.
 **
 ** Returns          if Nfc state busy return true otherwise false.
 **
 *******************************************************************************/
bool nfcManager_checkNfcStateBusy()
{
    bool status = false;

    if(NFA_checkNfcStateBusy() == TRUE)
        status = true;

    return status;
}
/*******************************************************************************
 **
 ** Function:        requestFwDownload
 **
 ** Description      This function is to complete the FW Dwnld to complete the
 **                  the pending request due to SPI session ongoing .
 **
 ** Returns          void.
 **
 *******************************************************************************/
void requestFwDownload()
{
    JNIEnv* e = NULL;
    uint8_t fwDnldRequest = FALSE;
    int status = NFA_STATUS_OK;

    NfcAdaptation& theInstance = NfcAdaptation::GetInstance();
    status = theInstance.HalGetFwDwnldFlag(&fwDnldRequest);
    ALOGD ("Enter: %s fwDnldRequest = %d",__FUNCTION__, fwDnldRequest);
    if (status == NFA_STATUS_OK)
    {
        if(fwDnldRequest == TRUE)
        {
            ScopedAttach attach(RoutingManager::getInstance().mNativeData->vm, &e);
            if (e == NULL)
            {
                ALOGE ("Exit:%s jni env is null",__FUNCTION__);
                return;
            }
#if(NXP_NFCC_SPI_FW_DOWNLOAD_SYNC == TRUE)
            e->CallVoidMethod (gNativeData->manager,
                android::gCachedNfcManagerNotifyFwDwnldRequested);
#endif
            if (e->ExceptionCheck())
            {
                e->ExceptionClear();
                ALOGE ("Exit:%s fail notify",__FUNCTION__);
            }

        }
        else
        {
            ALOGE ("Exit:%s Firmware download request:%d ",__FUNCTION__, fwDnldRequest);
        }
    }
    else
    {
        ALOGE ("Exit:%s HalGetFwDwnldFlag status:%d ",__FUNCTION__, status);
    }
}
#endif

/*******************************************************************************
**
** Function:        nfcManager_doDisableDtaMode
**
** Description:     Disable the DTA mode in NFC service.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doDisableDtaMode(JNIEnv* /* e */, jobject /* o */)
{
    gIsDtaEnabled = false;
}

#if(NXP_EXTNS == TRUE)
/*******************************************************************************
 **
** Function:        nfcManager_Enablep2p
**
** Description:     enable P2P
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_Enablep2p(JNIEnv* /* e */, jobject /* o */, jboolean p2pFlag)
{
    ALOGD ("Enter :%s  p2pFlag = %d", __FUNCTION__, p2pFlag);
    /* if another transaction is already in progress, store this request */
    if(!update_transaction_stat("enablep2p",SET_TRANSACTION_STATE))
    {
        ALOGD("Transaction in progress, Store the request");
        set_last_request(ENABLE_P2P, NULL);
        transaction_data.discovery_params.enable_p2p = p2pFlag;
        return;
    }
    if(sRfEnabled && p2pFlag)
    {
        /* Stop discovery if already ON */
        startRfDiscovery(false);
    }

    /* if already Polling, change to listen Mode */
    if (sPollingEnabled)
    {
        if (p2pFlag && !sP2pEnabled)
        {
            /* enable P2P listening, if we were not already listening */
            sP2pEnabled = true;
            PeerToPeer::getInstance().enableP2pListening (true);
        }
    }
    /* Beam ON - Discovery ON */
    if(p2pFlag)
    {
        NFA_ResumeP2p();
        startRfDiscovery (p2pFlag);
    }
    if(!update_transaction_stat("enablep2p",RESET_TRANSACTION_STATE))
    {
        ALOGE("%s: Can not reset transaction state", __FUNCTION__);
    }
}
#endif
/*******************************************************************************
**
** Function:        nfcManager_enableDiscovery
**
** Description:     Start polling and listening for devices.
**                  e: JVM environment.
**                  o: Java object.
**                  technologies_mask: the bitmask of technologies for which to enable discovery
**                  enable_lptd: whether to enable low power polling (default: false)
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_enableDiscovery (JNIEnv* e, jobject o, jint technologies_mask,
    jboolean enable_lptd, jboolean reader_mode, jboolean enable_p2p,
    jboolean restart)
{
    tNFA_STATUS status = NFA_STATUS_OK;
    tNFA_STATUS stat = NFA_STATUS_OK;
    tNFA_TECHNOLOGY_MASK tech_mask = DEFAULT_TECH_MASK;
    unsigned long num = 0;

    tNFA_HANDLE handle = NFA_HANDLE_INVALID;
    struct nfc_jni_native_data *nat = NULL;

#if((NXP_EXTNS == TRUE) && (NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
    tNFA_TECHNOLOGY_MASK etsi_tech_mask = 0;
#endif
    if(e == NULL && o == NULL)
    {
        nat = transaction_data.transaction_nat;
    }
    else
    {
        nat = getNative(e, o);
    }
#if (NXP_EXTNS == TRUE)
    if(!update_transaction_stat("enableDiscovery",SET_TRANSACTION_STATE))
    {
        ALOGD("Transaction is in progress store the request");
        set_last_request(ENABLE_DISCOVERY, nat);
        transaction_data.discovery_params.technologies_mask = technologies_mask;
        transaction_data.discovery_params.enable_lptd = enable_lptd;
        transaction_data.discovery_params.reader_mode = reader_mode;
        transaction_data.discovery_params.enable_p2p = enable_p2p;
        transaction_data.discovery_params.restart = restart;
        return;
    }
#endif

#if((NXP_EXTNS == TRUE) && (NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
    if(RoutingManager::getInstance().getEtsiReaederState() == STATE_SE_RDR_MODE_STARTED)
    {
        ALOGD ("%s: enter STATE_SE_RDR_MODE_START_CONFIG", __FUNCTION__);
        Rdr_req_ntf_info_t mSwp_info = RoutingManager::getInstance().getSwpRrdReqInfo();
        {
            SyncEventGuard guard (android::sNfaEnableDisablePollingEvent);
            ALOGD ("%s: disable polling", __FUNCTION__);
            status = NFA_DisablePolling ();
            if (status == NFA_STATUS_OK)
            {
                android::sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_DISABLED_EVT
            }
            else
            {
                ALOGE ("%s: fail disable polling; error=0x%X", __FUNCTION__, status);
            }
        }

        if(mSwp_info.swp_rd_req_info.tech_mask & NFA_TECHNOLOGY_MASK_A)
            etsi_tech_mask |= NFA_TECHNOLOGY_MASK_A;
        if(mSwp_info.swp_rd_req_info.tech_mask & NFA_TECHNOLOGY_MASK_B)
            etsi_tech_mask |= NFA_TECHNOLOGY_MASK_B;

        {
            SyncEventGuard guard (android::sNfaEnableDisablePollingEvent);
            status = NFA_EnablePolling (etsi_tech_mask);
            if (status == NFA_STATUS_OK)
            {
                ALOGD ("%s: wait for enable event", __FUNCTION__);
                android::sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_ENABLED_EVT
            }
            else
            {
                ALOGE ("%s: fail enable polling; error=0x%X", __FUNCTION__, status);
            }
        }
        startRfDiscovery (true);
        if(!update_transaction_stat("enableDiscovery",RESET_TRANSACTION_STATE))
        {
            ALOGE("%s: Can not reset transaction state", __FUNCTION__);
        }

        if(!update_transaction_stat("etsiReader", SET_TRANSACTION_STATE))
        {
            ALOGE ("%s: update_transaction_stat Failed", __FUNCTION__);
        }
        goto TheEnd;
    }
#endif

    if (technologies_mask == -1 && nat)
        tech_mask = (tNFA_TECHNOLOGY_MASK)nat->tech_mask;
    else if (technologies_mask != -1)
        tech_mask = (tNFA_TECHNOLOGY_MASK) technologies_mask;
    ALOGD ("%s: enter; tech_mask = %02x", __FUNCTION__, tech_mask);

    if( sDiscoveryEnabled && !restart)
    {
        ALOGE ("%s: already discovering", __FUNCTION__);
#if(NXP_EXTNS == TRUE)
        goto TheEnd;
#else
        return;
#endif
    }

    ALOGD ("%s: sIsSecElemSelected=%u", __FUNCTION__, sIsSecElemSelected);
    acquireRfInterfaceMutexLock();
    PowerSwitch::getInstance ().setLevel (PowerSwitch::FULL_POWER);

    if (sRfEnabled) {
        // Stop RF discovery to reconfigure
        startRfDiscovery(false);
    }

    if ((GetNumValue(NAME_UICC_LISTEN_TECH_MASK, &num, sizeof(num))))
    {
        ALOGD ("%s:UICC_LISTEN_MASK=0x0%lu;", __FUNCTION__, num);
    }


    // Check polling configuration
    if (tech_mask != 0)
    {
        ALOGD ("%s: Disable p2pListening", __FUNCTION__);
        PeerToPeer::getInstance().enableP2pListening (false);
        stopPolling_rfDiscoveryDisabled();
        //enableDisableLptd(enable_lptd);
        startPolling_rfDiscoveryDisabled(tech_mask);

        // Start P2P listening if tag polling was enabled
        if (sPollingEnabled)
        {
            ALOGD ("%s: Enable p2pListening", __FUNCTION__);

            if (enable_p2p && !sP2pEnabled) {
                sP2pEnabled = true;
                PeerToPeer::getInstance().enableP2pListening (true);
                NFA_ResumeP2p();
           } else if (!enable_p2p && sP2pEnabled) {
                sP2pEnabled = false;
                PeerToPeer::getInstance().enableP2pListening (false);
                NFA_PauseP2p();
            }

            if (reader_mode && !sReaderModeEnabled)
            {
                sReaderModeEnabled = true;
#if(NXP_EXTNS == TRUE)
                NFA_SetReaderMode(true,0);
                /*Send the state of readmode flag to Hal using proprietary command*/
                sProprietaryCmdBuf[3]=0x01;
                status |= NFA_SendNxpNciCommand(sizeof(sProprietaryCmdBuf),sProprietaryCmdBuf,NxpResponsePropCmd_Cb);
                if (status == NFA_STATUS_OK)
                {
                    SyncEventGuard guard (sNfaNxpNtfEvent);
                    sNfaNxpNtfEvent.wait(500); //wait for callback
                }
                else
                {
                    ALOGE ("%s: Failed NFA_SendNxpNciCommand", __FUNCTION__);
                }
                ALOGD ("%s: FRM Enable", __FUNCTION__);
#endif
                NFA_DisableListening();
#if(NXP_EXTNS == TRUE)
                sTechMask = tech_mask;

                discDuration = READER_MODE_DISCOVERY_DURATION;
#endif
                NFA_SetRfDiscoveryDuration(READER_MODE_DISCOVERY_DURATION);
            }
            else if (!reader_mode && sReaderModeEnabled)
            {
                struct nfc_jni_native_data *nat = getNative(e, o);
                sReaderModeEnabled = false;
#if(NXP_EXTNS == TRUE)
                NFA_SetReaderMode(false,0);
                gFelicaReaderState = STATE_IDLE;
                /*Send the state of readmode flag to Hal using proprietary command*/
                sProprietaryCmdBuf[3]=0x00;
                status |= NFA_SendNxpNciCommand(sizeof(sProprietaryCmdBuf),sProprietaryCmdBuf,NxpResponsePropCmd_Cb);
                if (status == NFA_STATUS_OK)
                {
                    SyncEventGuard guard (sNfaNxpNtfEvent);
                    sNfaNxpNtfEvent.wait(500); //wait for callback
                }
                else
                {
                    ALOGE ("%s: Failed NFA_SendNxpNciCommand", __FUNCTION__);
                }
                ALOGD ("%s: FRM Disable", __FUNCTION__);
#endif
#if((NXP_ESE_DUAL_MODE_PRIO_SCHEME == NXP_ESE_EXCLUSIVE_WIRED_MODE) ||(NXP_ESE_UICC_EXCLUSIVE_WIRED_MODE == TRUE))
                if(!SecureElement::getInstance().mlistenDisabled){
                    NFA_EnableListening();
                }
#else
                NFA_EnableListening();
#endif

#if(NXP_EXTNS == TRUE)
                discDuration = nat->discovery_duration;
#endif
                NFA_SetRfDiscoveryDuration(nat->discovery_duration);
            }
            else
            {
                {
                    ALOGD ("%s: restart UICC listen mode (%02lX)", __FUNCTION__, (num & 0xC7));
                    handle = SecureElement::getInstance().getEseHandleFromGenericId(SecureElement::UICC_ID);
                    SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
                    stat = NFA_CeConfigureUiccListenTech (handle, 0x00);
                    if(stat == NFA_STATUS_OK)
                    {
                        SecureElement::getInstance().mUiccListenEvent.wait ();
                    }
                    else
                        ALOGE ("fail to stop UICC listen");
                }
                {
                    SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
                    stat = NFA_CeConfigureUiccListenTech (handle, (num & 0xC7));
                    if(stat == NFA_STATUS_OK)
                    {
                        SecureElement::getInstance().mUiccListenEvent.wait ();
                    }
                    else
                        ALOGE ("fail to start UICC listen");
                }
            }
        }
    }
    else
    {
        // No technologies configured, stop polling
        stopPolling_rfDiscoveryDisabled();
    }

    // Start P2P listening if tag polling was enabled or the mask was 0.
    if (sDiscoveryEnabled || (tech_mask == 0))
    {
        handle = SecureElement::getInstance().getEseHandleFromGenericId(SecureElement::UICC_ID);

#if(NXP_EXTNS == TRUE)
        if((getScreenState() == NFA_SCREEN_STATE_UNLOCKED) || sProvisionMode)
        {
            ALOGD ("%s: Enable p2pListening", __FUNCTION__);
            PeerToPeer::getInstance().enableP2pListening (true);
        }
        else
        {
            ALOGD ("%s: Disable p2pListening", __FUNCTION__);
            PeerToPeer::getInstance().enableP2pListening (false);
        }
#endif

        {
            SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
            stat = NFA_CeConfigureUiccListenTech (handle, 0x00);
            if(stat == NFA_STATUS_OK)
            {
                SecureElement::getInstance().mUiccListenEvent.wait ();
            }
            else
                ALOGE ("fail to start UICC listen");
        }

        {
            SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
            stat = NFA_CeConfigureUiccListenTech (handle, (num & 0xC7));
            if(stat == NFA_STATUS_OK)
            {
                SecureElement::getInstance().mUiccListenEvent.wait ();
            }
            else
                ALOGE ("fail to start UICC listen");
        }
    }
    // Actually start discovery.
    startRfDiscovery (true);
    sDiscoveryEnabled = true;

    PowerSwitch::getInstance ().setModeOn (PowerSwitch::DISCOVERY);
    releaseRfInterfaceMutexLock();

#if (NXP_EXTNS == TRUE)
TheEnd:
    if(!update_transaction_stat("enableDiscovery",RESET_TRANSACTION_STATE))
    {
        ALOGE("%s: Can not reset transaction state", __FUNCTION__);
    }
#endif
    ALOGD ("%s: exit", __FUNCTION__);
}


/*******************************************************************************
**
** Function:        nfcManager_disableDiscovery
**
** Description:     Stop polling and listening for devices.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
void nfcManager_disableDiscovery (JNIEnv* e, jobject o)
{
    (void)e;
    (void)o;
    tNFA_STATUS status = NFA_STATUS_OK;
    unsigned long num = 0;
    unsigned long p2p_listen_mask =0;
    tNFA_HANDLE handle = NFA_HANDLE_INVALID;
    ALOGD ("%s: enter;", __FUNCTION__);
#if (NXP_EXTNS == TRUE)
    if(!update_transaction_stat("disableDiscovery",SET_TRANSACTION_STATE))
    {
        ALOGD("Transaction in progress, Store the request");
        set_last_request(DISABLE_DISCOVERY, NULL);
        return;
    }
#endif
    pn544InteropAbortNow ();
#if((NXP_EXTNS == TRUE) && (NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
    if(RoutingManager::getInstance().getEtsiReaederState() == STATE_SE_RDR_MODE_START_IN_PROGRESS)
    {
        Rdr_req_ntf_info_t mSwp_info = RoutingManager::getInstance().getSwpRrdReqInfo();
//        if(android::isDiscoveryStarted() == true)
        android::startRfDiscovery(false);
        PeerToPeer::getInstance().enableP2pListening (false);
        {
            SyncEventGuard guard ( SecureElement::getInstance().mUiccListenEvent);
            status = NFA_CeConfigureUiccListenTech (mSwp_info.swp_rd_req_info.src, 0x00);
            if (status == NFA_STATUS_OK)
            {
                SecureElement::getInstance().mUiccListenEvent.wait ();
            }
            else
            {
                ALOGE ("fail to stop listen");
            }
        }
        goto TheEnd;
    }
    else if(RoutingManager::getInstance().getEtsiReaederState() == STATE_SE_RDR_MODE_STOP_IN_PROGRESS)
    {
        android::startRfDiscovery(false);
        goto TheEnd;
    }
#endif

    if (sDiscoveryEnabled == false)
    {
        ALOGD ("%s: already disabled", __FUNCTION__);
        goto TheEnd;
    }
    acquireRfInterfaceMutexLock();
    // Stop RF Discovery.
    startRfDiscovery (false);

    if (sPollingEnabled)
        status = stopPolling_rfDiscoveryDisabled();
    sDiscoveryEnabled = false;

    if ((GetNumValue(NAME_UICC_LISTEN_TECH_MASK, &num, sizeof(num))))
    {
        ALOGD ("%s:UICC_LISTEN_MASK=0x0%lu;", __FUNCTION__, num);
    }
    if ((GetNumValue("P2P_LISTEN_TECH_MASK", &p2p_listen_mask, sizeof(p2p_listen_mask))))
    {
        ALOGD ("%s:P2P_LISTEN_MASK=0x0%lu;", __FUNCTION__, p2p_listen_mask);
    }

    PeerToPeer::getInstance().enableP2pListening (false);
    NFA_PauseP2p();

    if (sIsSecElemSelected)
    {
        handle = SecureElement::getInstance().getEseHandleFromGenericId(SecureElement::UICC_ID);
        {
            SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
            status = NFA_CeConfigureUiccListenTech (handle, 0x00);
            if (status == NFA_STATUS_OK)
            {
                SecureElement::getInstance().mUiccListenEvent.wait ();
            }
            else
                ALOGE ("fail to start UICC listen");
        }

        {
            SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
            status = NFA_CeConfigureUiccListenTech (handle, (num & 0x07));
            if(status == NFA_STATUS_OK)
            {
                SecureElement::getInstance().mUiccListenEvent.wait ();
            }
            else
                ALOGE ("fail to start UICC listen");
        }

        PeerToPeer::getInstance().enableP2pListening (false);
        startRfDiscovery (true);
    }

    sP2pEnabled = false;
    //if nothing is active after this, then tell the controller to power down
    //if (! PowerSwitch::getInstance ().setModeOff (PowerSwitch::DISCOVERY))
        //PowerSwitch::getInstance ().setLevel (PowerSwitch::LOW_POWER);

    // We may have had RF field notifications that did not cause
    // any activate/deactive events. For example, caused by wireless
    // charging orbs. Those may cause us to go to sleep while the last
    // field event was indicating a field. To prevent sticking in that
    // state, always reset the rf field status when we disable discovery.
    SecureElement::getInstance().resetRfFieldStatus();
    releaseRfInterfaceMutexLock();
TheEnd:
#if (NXP_EXTNS == TRUE)
if(!update_transaction_stat("disableDiscovery",RESET_TRANSACTION_STATE))
{
    ALOGE("%s: Can not reset transaction state", __FUNCTION__);
}
#endif
    ALOGD ("%s: exit", __FUNCTION__);
}

void enableDisableLongGuardTime (bool enable)
{
    // TODO
    // This is basically a work-around for an issue
    // in BCM20791B5: if a reader is configured as follows
    // 1) Only polls for NFC-A
    // 2) Cuts field between polls
    // 3) Has a short guard time (~5ms)
    // the BCM20791B5 doesn't wake up when such a reader
    // is polling it. Unfortunately the default reader
    // mode configuration on Android matches those
    // criteria. To avoid the issue, increase the guard
    // time when in reader mode.
    //
    // Proper fix is firmware patch for B5 controllers.
    SyncEventGuard guard(sNfaSetConfigEvent);
    tNFA_STATUS stat = NFA_SetConfig(NCI_PARAM_ID_T1T_RDR_ONLY, 2,
            enable ? sLongGuardTime : sDefaultGuardTime);
    if (stat == NFA_STATUS_OK)
        sNfaSetConfigEvent.wait ();
    else
        ALOGE("%s: Could not configure longer guard time", __FUNCTION__);
    return;
}

void enableDisableLptd (bool enable)
{
    // This method is *NOT* thread-safe. Right now
    // it is only called from the same thread so it's
    // not an issue.
    static bool sCheckedLptd = false;
    static bool sHasLptd = false;

    tNFA_STATUS stat = NFA_STATUS_OK;
    if (!sCheckedLptd)
    {
        sCheckedLptd = true;
        SyncEventGuard guard (sNfaGetConfigEvent);
        tNFA_PMID configParam[1] = {NCI_PARAM_ID_TAGSNIFF_CFG};
        stat = NFA_GetConfig(1, configParam);
        if (stat != NFA_STATUS_OK)
        {
            ALOGE("%s: NFA_GetConfig failed", __FUNCTION__);
            return;
        }
        sNfaGetConfigEvent.wait ();
        if (sCurrentConfigLen < 4 || sConfig[1] != NCI_PARAM_ID_TAGSNIFF_CFG) {
            ALOGE("%s: Config TLV length %d returned is too short", __FUNCTION__,
                    sCurrentConfigLen);
            return;
        }
        if (sConfig[3] == 0) {
            ALOGE("%s: LPTD is disabled, not enabling in current config", __FUNCTION__);
            return;
        }
        sHasLptd = true;
    }
    // Bail if we checked and didn't find any LPTD config before
    if (!sHasLptd) return;
    UINT8 enable_byte = enable ? 0x01 : 0x00;

    SyncEventGuard guard(sNfaSetConfigEvent);

    stat = NFA_SetConfig(NCI_PARAM_ID_TAGSNIFF_CFG, 1, &enable_byte);
    if (stat == NFA_STATUS_OK)
        sNfaSetConfigEvent.wait ();
    else
        ALOGE("%s: Could not configure LPTD feature", __FUNCTION__);
    return;
}

void setUiccIdleTimeout (bool enable)
{
    // This method is *NOT* thread-safe. Right now
    // it is only called from the same thread so it's
    // not an issue.
    tNFA_STATUS stat = NFA_STATUS_OK;
    UINT8 swp_cfg_byte0 = 0x00;
    {
        SyncEventGuard guard (sNfaGetConfigEvent);
        tNFA_PMID configParam[1] = {0xC2};
        stat = NFA_GetConfig(1, configParam);
        if (stat != NFA_STATUS_OK)
        {
            ALOGE("%s: NFA_GetConfig failed", __FUNCTION__);
            return;
        }
        sNfaGetConfigEvent.wait ();
        if (sCurrentConfigLen < 4 || sConfig[1] != 0xC2) {
            ALOGE("%s: Config TLV length %d returned is too short", __FUNCTION__,
                    sCurrentConfigLen);
            return;
        }
        swp_cfg_byte0 = sConfig[3];
    }
    SyncEventGuard guard(sNfaSetConfigEvent);
    if (enable)
        swp_cfg_byte0 |= 0x01;
    else
        swp_cfg_byte0 &= ~0x01;

    stat = NFA_SetConfig(0xC2, 1, &swp_cfg_byte0);
    if (stat == NFA_STATUS_OK)
        sNfaSetConfigEvent.wait ();
    else
        ALOGE("%s: Could not configure UICC idle timeout feature", __FUNCTION__);
    return;
}


/*******************************************************************************
**
** Function:        nfcManager_doCreateLlcpServiceSocket
**
** Description:     Create a new LLCP server socket.
**                  e: JVM environment.
**                  o: Java object.
**                  nSap: Service access point.
**                  sn: Service name
**                  miu: Maximum information unit.
**                  rw: Receive window size.
**                  linearBufferLength: Max buffer size.
**
** Returns:         NativeLlcpServiceSocket Java object.
**
*******************************************************************************/
static jobject nfcManager_doCreateLlcpServiceSocket (JNIEnv* e, jobject, jint nSap, jstring sn, jint miu, jint rw, jint linearBufferLength)
{
    PeerToPeer::tJNI_HANDLE jniHandle = PeerToPeer::getInstance().getNewJniHandle ();

    if(sn == NULL)
    {
        ALOGE ("%s: Service name is NULL", __FUNCTION__);
        return NULL;
    }

    ScopedUtfChars serviceName(e, sn);
    if (serviceName.c_str() == NULL)
    {
        ALOGE ("%s: service name can not be null error", __FUNCTION__);
        return NULL;
    }

    ALOGD ("%s: enter: sap=%i; name=%s; miu=%i; rw=%i; buffLen=%i", __FUNCTION__, nSap, serviceName.c_str(), miu, rw, linearBufferLength);

    /* Create new NativeLlcpServiceSocket object */
    jobject serviceSocket = NULL;
    if (nfc_jni_cache_object_local(e, gNativeLlcpServiceSocketClassName, &(serviceSocket)) == -1)
    {
        ALOGE ("%s: Llcp socket object creation error", __FUNCTION__);
        return NULL;
    }

    /* Get NativeLlcpServiceSocket class object */
    ScopedLocalRef<jclass> clsNativeLlcpServiceSocket(e, e->GetObjectClass(serviceSocket));
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE("%s: Llcp Socket get object class error", __FUNCTION__);
        return NULL;
    }

    if (!PeerToPeer::getInstance().registerServer (jniHandle, serviceName.c_str()))
    {
        ALOGE("%s: RegisterServer error", __FUNCTION__);
        return NULL;
    }

    jfieldID f;

    /* Set socket handle to be the same as the NfaHandle*/
    f = e->GetFieldID(clsNativeLlcpServiceSocket.get(), "mHandle", "I");
    e->SetIntField(serviceSocket, f, (jint) jniHandle);
    ALOGD ("%s: socket Handle = 0x%X", __FUNCTION__, jniHandle);

    /* Set socket linear buffer length */
    f = e->GetFieldID(clsNativeLlcpServiceSocket.get(), "mLocalLinearBufferLength", "I");
    e->SetIntField(serviceSocket, f,(jint)linearBufferLength);
    ALOGD ("%s: buffer length = %d", __FUNCTION__, linearBufferLength);

    /* Set socket MIU */
    f = e->GetFieldID(clsNativeLlcpServiceSocket.get(), "mLocalMiu", "I");
    e->SetIntField(serviceSocket, f,(jint)miu);
    ALOGD ("%s: MIU = %d", __FUNCTION__, miu);

    /* Set socket RW */
    f = e->GetFieldID(clsNativeLlcpServiceSocket.get(), "mLocalRw", "I");
    e->SetIntField(serviceSocket, f,(jint)rw);
    ALOGD ("%s:  RW = %d", __FUNCTION__, rw);

    sLastError = 0;
    ALOGD ("%s: exit", __FUNCTION__);
    return serviceSocket;
}


/*******************************************************************************
**
** Function:        nfcManager_doGetLastError
**
** Description:     Get the last error code.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         Last error code.
**
*******************************************************************************/
static jint nfcManager_doGetLastError(JNIEnv*, jobject)
{
    ALOGD ("%s: last error=%i", __FUNCTION__, sLastError);
    return sLastError;
}


/*******************************************************************************
**
** Function:        nfcManager_doDeinitialize
**
** Description:     Turn off NFC.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_doDeinitialize (JNIEnv* e, jobject obj)
{
    ALOGD ("%s: enter", __FUNCTION__);
    sIsDisabling = true;
#if((NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE) && (NXP_EXTNS == TRUE))
    if(SecureElement::getInstance().mDownloadMode == JCOP_DOWNLOAD)
    {
        if (e != NULL)
        {
            unsigned long maxTimeout = 0;
            unsigned long elapsedTimeout = 0;
            if(!GetNxpNumValue(NAME_OS_DOWNLOAD_TIMEOUT_VALUE, &maxTimeout,
                sizeof(maxTimeout)))
            {
                maxTimeout = MAX_JCOP_TIMEOUT_VALUE;
                ALOGE("%s: Failed to get timeout value = %ld",
                    __FUNCTION__, maxTimeout);
            }
            while(SecureElement::getInstance().mDownloadMode ==
                JCOP_DOWNLOAD) {
                ALOGD ("%s: timeout waiting for Os Dowload",
                     __FUNCTION__);
                usleep(MAX_WAIT_TIME_FOR_RETRY*1000000);
                     e->CallVoidMethod (gNativeData->manager,
                        android::gCachedNfcManagerNotifyJcosDownloadInProgress,
                             true);
                if (e->ExceptionCheck())
                {
                    e->ExceptionClear();
                    DwpChannel::getInstance().forceClose();
                    ALOGE ("%s: fail notify", __FUNCTION__);
                }
                elapsedTimeout += MAX_WAIT_TIME_FOR_RETRY;
                if(elapsedTimeout*1000 > maxTimeout)
                {
                    DwpChannel::getInstance().forceClose();
                    ALOGD ("%s: Time elapsed force close DWP channel",
                        __FUNCTION__);
                }
            }
        }
        else
        {
           DwpChannel::getInstance().forceClose();
           ALOGE ("%s: Force close DWP channel as JNIEnv is null",
               __FUNCTION__);
        }
    }
#endif
#if(NXP_EXTNS == TRUE)
#if (JCOP_WA_ENABLE == TRUE)
    rfActivation = false;
#endif
#endif
    doDwpChannel_ForceExit();
#if (JCOP_WA_ENABLE == FALSE)
    NFA_HciW4eSETransaction_Complete(Wait);
#endif
    pn544InteropAbortNow ();

    RoutingManager::getInstance().onNfccShutdown();
    SecureElement::getInstance().finalize ();
    PowerSwitch::getInstance ().initialize (PowerSwitch::UNKNOWN_LEVEL);
    //Stop the discovery before calling NFA_Disable.
    if(sRfEnabled)
        startRfDiscovery(false);
    tNFA_STATUS stat = NFA_STATUS_OK;

    if (sIsNfaEnabled)
    {
        /*
         During device Power-Off while Nfc-On, Nfc mode will be NFC_MODE_ON
         NFC_MODE_OFF indicates Nfc is turning off and only in this case reset the venConfigValue
         */
        if(gGeneralPowershutDown == NFC_MODE_OFF)
        {
            stat = SetVenConfigValue(NFC_MODE_OFF);

            if (stat != NFA_STATUS_OK)
            {
                ALOGE ("%s: fail enable SetVenConfigValue; error=0x%X", __FUNCTION__, stat);
            }
        }
        SyncEventGuard guard (sNfaDisableEvent);
        EXTNS_Close ();
        stat = NFA_Disable (TRUE /* graceful */);
        if (stat == NFA_STATUS_OK)
        {
            ALOGD ("%s: wait for completion", __FUNCTION__);
            sNfaDisableEvent.wait (); //wait for NFA command to finish
            PeerToPeer::getInstance ().handleNfcOnOff (false);
        }
        else
        {
            ALOGE ("%s: fail disable; error=0x%X", __FUNCTION__, stat);
        }
    }
    NfcTag::getInstance ().mNfcDisableinProgress = true;
    nativeNfcTag_abortWaits();
    NfcTag::getInstance().abort ();
    sAbortConnlessWait = true;
    nativeLlcpConnectionlessSocket_abortWait();
    sIsNfaEnabled = false;
    sDiscoveryEnabled = false;
    sIsDisabling = false;
    sPollingEnabled = false;
//    sIsSecElemSelected = false;
    sIsSecElemSelected = 0;
    gActivated = false;
    sP2pEnabled = false;
    sLfT3tMax = 0;
    {
        //unblock NFA_EnablePolling() and NFA_DisablePolling()
        SyncEventGuard guard (sNfaEnableDisablePollingEvent);
        sNfaEnableDisablePollingEvent.notifyOne ();
    }
#if (NXP_EXTNS == TRUE) && (NFC_NXP_ESE == TRUE)
#if ((NXP_ESE_DWP_SPI_SYNC_ENABLE == TRUE)||(NXP_ESE_SVDD_SYNC == TRUE) || (NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE) ||\
     (NXP_NFCC_SPI_FW_DOWNLOAD_SYNC == TRUE))
    releaseSPIEvtHandlerThread();
#endif
#endif
    NfcAdaptation& theInstance = NfcAdaptation::GetInstance();
    theInstance.Finalize();

    ALOGD ("%s: exit", __FUNCTION__);
    return JNI_TRUE;
}


/*******************************************************************************
**
** Function:        nfcManager_doCreateLlcpSocket
**
** Description:     Create a LLCP connection-oriented socket.
**                  e: JVM environment.
**                  o: Java object.
**                  nSap: Service access point.
**                  miu: Maximum information unit.
**                  rw: Receive window size.
**                  linearBufferLength: Max buffer size.
**
** Returns:         NativeLlcpSocket Java object.
**
*******************************************************************************/
static jobject nfcManager_doCreateLlcpSocket (JNIEnv* e, jobject, jint nSap, jint miu, jint rw, jint linearBufferLength)
{
    ALOGD ("%s: enter; sap=%d; miu=%d; rw=%d; buffer len=%d", __FUNCTION__, nSap, miu, rw, linearBufferLength);

    PeerToPeer::tJNI_HANDLE jniHandle = PeerToPeer::getInstance().getNewJniHandle ();
    PeerToPeer::getInstance().createClient (jniHandle, miu, rw);

    /* Create new NativeLlcpSocket object */
    jobject clientSocket = NULL;
    if (nfc_jni_cache_object_local(e, gNativeLlcpSocketClassName, &(clientSocket)) == -1)
    {
        ALOGE ("%s: fail Llcp socket creation", __FUNCTION__);
        return clientSocket;
    }

    /* Get NativeConnectionless class object */
    ScopedLocalRef<jclass> clsNativeLlcpSocket(e, e->GetObjectClass(clientSocket));
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("%s: fail get class object", __FUNCTION__);
        return clientSocket;
    }

    jfieldID f;

    /* Set socket SAP */
    f = e->GetFieldID (clsNativeLlcpSocket.get(), "mSap", "I");
    e->SetIntField (clientSocket, f, (jint) nSap);

    /* Set socket handle */
    f = e->GetFieldID (clsNativeLlcpSocket.get(), "mHandle", "I");
    e->SetIntField (clientSocket, f, (jint) jniHandle);

    /* Set socket MIU */
    f = e->GetFieldID (clsNativeLlcpSocket.get(), "mLocalMiu", "I");
    e->SetIntField (clientSocket, f, (jint) miu);

    /* Set socket RW */
    f = e->GetFieldID (clsNativeLlcpSocket.get(), "mLocalRw", "I");
    e->SetIntField (clientSocket, f, (jint) rw);

    ALOGD ("%s: exit", __FUNCTION__);
    return clientSocket;
}


/*******************************************************************************
**
** Function:        nfcManager_doCreateLlcpConnectionlessSocket
**
** Description:     Create a connection-less socket.
**                  e: JVM environment.
**                  o: Java object.
**                  nSap: Service access point.
**                  sn: Service name.
**
** Returns:         NativeLlcpConnectionlessSocket Java object.
**
*******************************************************************************/
static jobject nfcManager_doCreateLlcpConnectionlessSocket (JNIEnv *, jobject, jint nSap, jstring /*sn*/)
{
    ALOGD ("%s: nSap=0x%X", __FUNCTION__, nSap);
    return NULL;
}


/*******************************************************************************
**
** Function:        nfcManager_doGetSecureElementList
**
** Description:     Get a list of secure element handles.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         List of secure element handles.
**
*******************************************************************************/
static jintArray nfcManager_doGetSecureElementList(JNIEnv* e, jobject)
{
    ALOGD ("%s", __FUNCTION__);
    return SecureElement::getInstance().getListOfEeHandles(e);
}

/*******************************************************************************
**
** Function:        setListenMode
**
** Description:     NFC controller starts routing data in listen mode.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
#if 0
inline static void setListenMode()  /*defined as inline to eliminate warning defined but not used*/
{
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_HANDLE ee_handleList[NFA_EE_MAX_EE_SUPPORTED];
    UINT8 i, seId, count;

    PowerSwitch::getInstance ().setLevel (PowerSwitch::FULL_POWER);

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }
    SecureElement::getInstance().getEeHandleList(ee_handleList, &count);
    if (count > NFA_EE_MAX_EE_SUPPORTED) {
        count = NFA_EE_MAX_EE_SUPPORTED;
        ALOGD ("Count is more than NFA_EE_MAX_EE_SUPPORTED ,Forcing to NFA_EE_MAX_EE_SUPPORTED");
    }
    for ( i = 0; i < count; i++)
    {
        seId = SecureElement::getInstance().getGenericEseId(ee_handleList[i]);
        SecureElement::getInstance().activate (seId);
        sIsSecElemSelected++;
    }

    startRfDiscovery (true);
    PowerSwitch::getInstance ().setModeOn (PowerSwitch::SE_ROUTING);
//TheEnd:                           /*commented to eliminate warning label defined but not used*/
    ALOGD ("%s: exit", __FUNCTION__);
}
#endif


/*******************************************************************************
**
** Function:        nfcManager_doSelectSecureElement
**
** Description:     NFC controller starts routing data in listen mode.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doSelectSecureElement(JNIEnv *e, jobject o, jint seId)
{
    (void)e;
    (void)o;
    ALOGD ("%s: enter", __FUNCTION__);
    bool stat = true;

    if (sIsSecElemSelected >= sIsSecElemDetected)
    {
        ALOGD ("%s: already selected", __FUNCTION__);
        goto TheEnd;
    }

    PowerSwitch::getInstance ().setLevel (PowerSwitch::FULL_POWER);

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }

    stat = SecureElement::getInstance().activate (seId);
    if (stat)
    {
        SecureElement::getInstance().routeToSecureElement ();
        sIsSecElemSelected++;
//        if(sHCEEnabled == false)
//        {
//            RoutingManager::getInstance().setRouting(false);
//        }
    }
//    sIsSecElemSelected = true;

    startRfDiscovery (true);
    PowerSwitch::getInstance ().setModeOn (PowerSwitch::SE_ROUTING);
TheEnd:
    ALOGD ("%s: exit", __FUNCTION__);
}

/*******************************************************************************
**
** Function:        nfcManager_doSetSEPowerOffState
**
** Description:     NFC controller enable/disabe card emulation in power off
**                  state from EE.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doSetSEPowerOffState(JNIEnv *e, jobject o, jint seId, jboolean enable)
{
    (void)e;
    (void)o;
    tNFA_HANDLE ee_handle;
    UINT8 power_state_mask = ~NFA_EE_PWR_STATE_SWITCH_OFF;

    if(enable == true)
    {
        power_state_mask = NFA_EE_PWR_STATE_SWITCH_OFF;
    }

    ee_handle = SecureElement::getInstance().getEseHandleFromGenericId(seId);

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }

    tNFA_STATUS status = NFA_AddEePowerState(ee_handle,power_state_mask);


    // Commit the routing configuration
    status |= NFA_EeUpdateNow();

    if (status != NFA_STATUS_OK)
        ALOGE("Failed to commit routing configuration");

    startRfDiscovery (true);

//    TheEnd:                   /*commented to eliminate warning label defined but not used*/
        ALOGD ("%s: exit", __FUNCTION__);

}


/*******************************************************************************
**
** Function:        nfcManager_GetDefaultSE
**
** Description:     Get default Secure Element.
**
**
** Returns:         Returns 0.
**
*******************************************************************************/
static jint nfcManager_GetDefaultSE(JNIEnv *e, jobject o)
{
    (void)e;
    (void)o;
    unsigned long num;
    GetNxpNumValue (NAME_NXP_DEFAULT_SE, (void*)&num, sizeof(num));
    ALOGD ("%lu: nfcManager_GetDefaultSE", num);
    return num;

}


static jint nfcManager_getSecureElementTechList(JNIEnv *e, jobject o)
{
    (void)e;
    (void)o;
    uint8_t sak;
    jint tech = 0x00;
    ALOGD ("nfcManager_getSecureElementTechList -Enter");
    sak = HciRFParams::getInstance().getESeSak();
    bool isTypeBPresent = HciRFParams::getInstance().isTypeBSupported();

    ALOGD ("nfcManager_getSecureElementTechList - sak is %0x", sak);

    if(sak & 0x08)
    {
        tech |= TARGET_TYPE_MIFARE_CLASSIC;
    }

    if( sak & 0x20 )
    {
        tech |= NFA_TECHNOLOGY_MASK_A;
    }

    if( isTypeBPresent == true)
    {
        tech |= NFA_TECHNOLOGY_MASK_B;
    }
    ALOGD ("nfcManager_getSecureElementTechList - tech is %0x", tech);
    return tech;

}

static jintArray nfcManager_getActiveSecureElementList(JNIEnv *e, jobject o)
{
    (void)e;
    (void)o;
    return SecureElement::getInstance().getActiveSecureElementList(e);
}

static void nfcManager_setSecureElementListenTechMask(JNIEnv *e, jobject o, jint tech_mask)
{
    (void)e;
    (void)o;
    ALOGD ("%s: ENTER", __FUNCTION__);
//    tNFA_STATUS status;                   /*commented to eliminate unused variable warning*/

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }
    SecureElement::getInstance().setEseListenTechMask(tech_mask);

    startRfDiscovery (true);

    ALOGD ("%s: EXIT", __FUNCTION__);
}


static jbyteArray nfcManager_getSecureElementUid(JNIEnv* e, jobject /* o */)
{
    jbyteArray jbuff = NULL;
    uint8_t bufflen = 0;
    uint8_t buf[16] = {0,};

    ALOGD ("nfcManager_getSecureElementUid -Enter");
    HciRFParams::getInstance().getESeUid(&buf[0], &bufflen);
    if(bufflen > 0)
     {
       jbuff = e->NewByteArray (bufflen);
       e->SetByteArrayRegion (jbuff, 0, bufflen, (jbyte*) buf);
     }
    return jbuff;
}

static tNFA_STATUS nfcManager_setEmvCoPollProfile(JNIEnv* /* e */, jobject /* o */,
        jboolean enable, jint route)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    tNFA_TECHNOLOGY_MASK tech_mask = 0;

    ALOGE("In nfcManager_setEmvCoPollProfile enable = 0x%x route = 0x%x", enable, route);
    /* Stop polling */
    if ( isDiscoveryStarted())
    {
        // Stop RF discovery to reconfigure
        startRfDiscovery(false);
    }

    status = EmvCo_dosetPoll(enable);
    if (status != NFA_STATUS_OK)
    {
        ALOGE ("%s: fail enable polling; error=0x%X", __FUNCTION__, status);
        goto TheEnd;
    }

    if (enable)
    {
        if (route == 0x00)
        {
            /* DH enable polling for A and B*/
            tech_mask = NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_B;
        }
        else if(route == 0x01)
        {
            /* UICC is end-point at present not supported by FW */
            /* TBD : Get eeinfo (use handle appropirately, depending up
             * on it enable the polling */
        }
        else if(route == 0x02)
        {
            /* ESE is end-point at present not supported by FW */
            /* TBD : Get eeinfo (use handle appropirately, depending up
             * on it enable the polling */
        }
        else
        {

        }
    }
    else
    {
        unsigned long num = 0;
        if (GetNumValue(NAME_POLLING_TECH_MASK, &num, sizeof(num)))
            tech_mask = num;
    }

    ALOGD ("%s: enable polling", __FUNCTION__);
    {
        SyncEventGuard guard (sNfaEnableDisablePollingEvent);
        status = NFA_EnablePolling (tech_mask);
        if (status == NFA_STATUS_OK)
        {
            ALOGD ("%s: wait for enable event", __FUNCTION__);
            sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_ENABLED_EVT
        }
        else
        {
            ALOGE ("%s: fail enable polling; error=0x%X", __FUNCTION__, status);
        }
    }

TheEnd:
    /* start polling */
    if ( !isDiscoveryStarted())
    {
        // Start RF discovery to reconfigure
        startRfDiscovery(true);
    }
    return status;

}

/*******************************************************************************
**
** Function:        nfcManager_doDeselectSecureElement
**
** Description:     NFC controller stops routing data in listen mode.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doDeselectSecureElement(JNIEnv *e, jobject o,  jint seId)
{
    (void)e;
    (void)o;
    ALOGD ("%s: enter", __FUNCTION__);
    bool stat = false;
    bool bRestartDiscovery = false;

    if (! sIsSecElemSelected)
    {
        ALOGE ("%s: already deselected", __FUNCTION__);
        goto TheEnd2;
    }

    if (PowerSwitch::getInstance ().getLevel() == PowerSwitch::LOW_POWER)
    {
        ALOGD ("%s: do not deselect while power is OFF", __FUNCTION__);
//        sIsSecElemSelected = false;
        sIsSecElemSelected--;
        goto TheEnd;
    }

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
        bRestartDiscovery = true;
    }
    //sIsSecElemSelected = false;
    //sIsSecElemSelected--;

    //if controller is not routing to sec elems AND there is no pipe connected,
    //then turn off the sec elems
    if (SecureElement::getInstance().isBusy() == false)
    {
        //SecureElement::getInstance().deactivate (0xABCDEF);
        stat = SecureElement::getInstance().deactivate (seId);
        if(stat)
        {
            sIsSecElemSelected--;
//            RoutingManager::getInstance().commitRouting();
        }
    }

TheEnd:
     /*
     * conditional check is added to avoid multiple dicovery cmds
     * at the time of NFC OFF in progress
     */
    if ((gGeneralPowershutDown != NFC_MODE_OFF) && bRestartDiscovery)
        startRfDiscovery (true);

    //if nothing is active after this, then tell the controller to power down
    if (! PowerSwitch::getInstance ().setModeOff (PowerSwitch::SE_ROUTING))
        PowerSwitch::getInstance ().setLevel (PowerSwitch::LOW_POWER);

TheEnd2:
    ALOGD ("%s: exit", __FUNCTION__);
}

/*******************************************************************************
**
** Function:        nfcManager_getDefaultAidRoute
**
** Description:     Get the default Aid Route Entry.
**                  e: JVM environment.
**                  o: Java object.
**                  mode: Not used.
**
** Returns:         None
**
*******************************************************************************/
static jint nfcManager_getDefaultAidRoute (JNIEnv* /* e */, jobject /* o */)
{
    unsigned long num = 0;
#if(NXP_EXTNS == TRUE)
    GetNxpNumValue(NAME_DEFAULT_AID_ROUTE, &num, sizeof(num));
#endif
    return num;
}
/*******************************************************************************
**
** Function:        nfcManager_getDefaultDesfireRoute
**
** Description:     Get the default Desfire Route Entry.
**                  e: JVM environment.
**                  o: Java object.
**                  mode: Not used.
**
** Returns:         None
**
*******************************************************************************/
static jint nfcManager_getDefaultDesfireRoute (JNIEnv* /* e */, jobject /* o */)
{
    unsigned long num = 0;
#if(NXP_EXTNS == TRUE)
    GetNxpNumValue(NAME_DEFAULT_DESFIRE_ROUTE, (void*)&num, sizeof(num));
    ALOGD ("%s: enter; NAME_DEFAULT_DESFIRE_ROUTE = %02lx", __FUNCTION__, num);
#endif
    return num;
}
/*******************************************************************************
**
** Function:        nfcManager_getDefaultMifareCLTRoute
**
** Description:     Get the default mifare CLT Route Entry.
**                  e: JVM environment.
**                  o: Java object.
**                  mode: Not used.
**
** Returns:         None
**
*******************************************************************************/
static jint nfcManager_getDefaultMifareCLTRoute (JNIEnv* /* e */, jobject /* o */)
{
    unsigned long num = 0;
#if(NXP_EXTNS == TRUE)
    GetNxpNumValue(NAME_DEFAULT_MIFARE_CLT_ROUTE, &num, sizeof(num));
#endif
    return num;
}
#if(NXP_EXTNS == TRUE)
/*******************************************************************************
**
** Function:        nfcManager_getDefaultAidPowerState
**
** Description:     Get the default Desfire Power States.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         Power State
**
*******************************************************************************/
static jint nfcManager_getDefaultAidPowerState (JNIEnv* /* e */, jobject /* o */)
{
    unsigned long num = 0;
    GetNxpNumValue(NAME_DEFAULT_AID_PWR_STATE, &num, sizeof(num));
    return num;
}

/*******************************************************************************
**
** Function:        nfcManager_getDefaultDesfirePowerState
**
** Description:     Get the default Desfire Power States.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         Power State
**
*******************************************************************************/
static jint nfcManager_getDefaultDesfirePowerState (JNIEnv* /* e */, jobject /* o */)
{
    unsigned long num = 0;
    GetNxpNumValue(NAME_DEFAULT_DESFIRE_PWR_STATE, &num, sizeof(num));
    return num;
}
/*******************************************************************************
**
** Function:        nfcManager_getDefaultMifareCLTPowerState
**
** Description:     Get the default mifare CLT Power States.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         Power State
**
*******************************************************************************/
static jint nfcManager_getDefaultMifareCLTPowerState (JNIEnv* /* e */, jobject /* o */)
{
    unsigned long num = 0;
    GetNxpNumValue(NAME_DEFAULT_MIFARE_CLT_PWR_STATE, &num, sizeof(num));
    return num;
}
/*******************************************************************************
**
** Function:        nfcManager_setDefaultTechRoute
**
** Description:     Setting Default Technology Routing
**                  e:  JVM environment.
**                  o:  Java object.
**                  seId:  SecureElement Id
**                  tech_swithon:  technology switch_on
**                  tech_switchoff:  technology switch_off
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_setDefaultTechRoute(JNIEnv* /* e */, jobject /* o */, jint seId,
        jint tech_switchon, jint tech_switchoff)
{
    ALOGD ("%s: ENTER", __FUNCTION__);
//    tNFA_STATUS status;                   /*commented to eliminate unused variable warning*/

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }
    RoutingManager::getInstance().setDefaultTechRouting (seId, tech_switchon, tech_switchoff);
    // start discovery.
    startRfDiscovery (true);
}

/*******************************************************************************
**
** Function:        nfcManager_setDefaultProtoRoute
**
** Description:     Setting Default Protocol Routing
**
**                  e:  JVM environment.
**                  o:  Java object.
**                  seId:  SecureElement Id
**                  proto_swithon:  Protocol switch_on
**                  proto_switchoff:  Protocol switch_off
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_setDefaultProtoRoute(JNIEnv *e, jobject o, jint seId,
        jint proto_switchon, jint proto_switchoff)
{
    (void)e;
    (void)o;
    ALOGD ("%s: ENTER", __FUNCTION__);
//    tNFA_STATUS status;                   /*commented to eliminate unused variable warning*/
//    if (sRfEnabled) {
//        // Stop RF Discovery if we were polling
//        startRfDiscovery (false);
//    }
    RoutingManager::getInstance().setDefaultProtoRouting (seId, proto_switchon, proto_switchoff);
    // start discovery.
//    startRfDiscovery (true);
}

/*******************************************************************************
 **
 ** Function:        nfcManager_setPreferredSimSlot()
 **
 ** Description:     This api is used to select a particular UICC slot.
 **
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
static int nfcManager_setPreferredSimSlot(JNIEnv* e, jobject o, jint uiccSlot)
{
    ALOGD("%s : uiccslot : %d : enter", __FUNCTION__, uiccSlot);

    tNFA_STATUS status = NFA_STATUS_OK;
#if(NFC_NXP_STAT_DUAL_UICC_WO_EXT_SWITCH == TRUE)
    sCurrentSelectedUICCSlot = uiccSlot;
    NFA_SetPreferredUiccId((uiccSlot == 2)?HOST_TYPE_UICC2 : HOST_TYPE_UICC1);
#endif
    return status;
}

/*******************************************************************************
**
** Function:        nfcManager_isVzwFeatureEnabled
**
** Description:     Check vzw feature is enabled or not
**
** Returns:         True if the VZW_FEATURE_ENABLE is set.
**
*******************************************************************************/
static bool nfcManager_isVzwFeatureEnabled (JNIEnv* /* e */, jobject /* o */)
{
    unsigned int num = 0;
    bool mStat = false;

    if (GetNxpNumValue("VZW_FEATURE_ENABLE", &num, sizeof(num)))
    {
        if(num == 0x01)
        {
            mStat = true;
        }
        else
        {
            mStat = false;
        }
    }
    else{
        mStat = false;
    }
    return mStat;
}
/*******************************************************************************
**
** Function:        nfcManager_isNfccBusy
**
** Description:     Check If NFCC is busy
**
** Returns:         True if NFCC is busy.
**
*******************************************************************************/
static bool nfcManager_isNfccBusy(JNIEnv*, jobject)
{
    ALOGD ("%s: ENTER", __FUNCTION__);
    bool statBusy = false;
    if(SecureElement::getInstance().isBusy())
    {
        ALOGE("%s:FAIL  SE wired-mode : busy", __FUNCTION__);
        statBusy = true;
    }
    else if(rfActivation)
    {
        ALOGE("%s:FAIL  RF session ongoing", __FUNCTION__);
        statBusy = true;
    }
    else if(transaction_data.trans_in_progress)
    {
        ALOGE ("%s: FAIL Transaction in progress", __FUNCTION__);
        statBusy = true;
    }

    ALOGD ("%s: Exit statBusy : 0x%02x", __FUNCTION__,statBusy);
    return statBusy;
}
#endif
/*******************************************************************************
**
** Function:        isPeerToPeer
**
** Description:     Whether the activation data indicates the peer supports NFC-DEP.
**                  activated: Activation data.
**
** Returns:         True if the peer supports NFC-DEP.
**
*******************************************************************************/
static bool isPeerToPeer (tNFA_ACTIVATED& activated)
{
    return activated.activate_ntf.protocol == NFA_PROTOCOL_NFC_DEP;
}

/*******************************************************************************
**
** Function:        isListenMode
**
** Description:     Indicates whether the activation data indicates it is
**                  listen mode.
**
** Returns:         True if this listen mode.
**
*******************************************************************************/
static bool isListenMode(tNFA_ACTIVATED& activated)
{
    return ((NFC_DISCOVERY_TYPE_LISTEN_A == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_B == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_F == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_A_ACTIVE == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_F_ACTIVE == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_ISO15693 == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_B_PRIME == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_INTERFACE_EE_DIRECT_RF == activated.activate_ntf.intf_param.type));
}

/*******************************************************************************
**
** Function:        nfcManager_doCheckLlcp
**
** Description:     Not used.
**
** Returns:         True
**
*******************************************************************************/
static jboolean nfcManager_doCheckLlcp(JNIEnv*, jobject)
{
    ALOGD("%s", __FUNCTION__);
    return JNI_TRUE;
}


static jboolean nfcManager_doCheckJcopDlAtBoot(JNIEnv* /* e */, jobject /* o */)
{
    unsigned int num = 0;
    ALOGD("%s", __FUNCTION__);
    if(GetNxpNumValue(NAME_NXP_JCOPDL_AT_BOOT_ENABLE,(void*)&num,sizeof(num))) {
        if(num == 0x01) {
            return JNI_TRUE;
        }
        else {
            return JNI_FALSE;
        }
    }
    else {
        return JNI_FALSE;
    }
}


/*******************************************************************************
**
** Function:        nfcManager_doActivateLlcp
**
** Description:     Not used.
**
** Returns:         True
**
*******************************************************************************/
static jboolean nfcManager_doActivateLlcp(JNIEnv*, jobject)
{
    ALOGD("%s", __FUNCTION__);
    return JNI_TRUE;
}


/*******************************************************************************
**
** Function:        nfcManager_doAbort
**
** Description:     Not used.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doAbort(JNIEnv*, jobject)
{
    ALOGE("%s: abort()", __FUNCTION__);
    abort();
}


/*******************************************************************************
**
** Function:        nfcManager_doDownload
**
** Description:     Download firmware patch files.  Do not turn on NFC.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_doDownload(JNIEnv*, jobject)
{
    ALOGD ("%s: enter", __FUNCTION__);
    NfcAdaptation& theInstance = NfcAdaptation::GetInstance();

    theInstance.Initialize(); //start GKI, NCI task, NFC task
    theInstance.DownloadFirmware ();
    theInstance.Finalize();
    ALOGD ("%s: exit", __FUNCTION__);
    return JNI_TRUE;
}


/*******************************************************************************
**
** Function:        nfcManager_doResetTimeouts
**
** Description:     Not used.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doResetTimeouts(JNIEnv*, jobject)
{
    ALOGD ("%s", __FUNCTION__);
    NfcTag::getInstance().resetAllTransceiveTimeouts ();
}


/*******************************************************************************
**
** Function:        nfcManager_doSetTimeout
**
** Description:     Set timeout value.
**                  e: JVM environment.
**                  o: Java object.
**                  tech: technology ID.
**                  timeout: Timeout value.
**
** Returns:         True if ok.
**
*******************************************************************************/
static bool nfcManager_doSetTimeout(JNIEnv*, jobject, jint tech, jint timeout)
{
    if (timeout <= 0)
    {
        ALOGE("%s: Timeout must be positive.",__FUNCTION__);
        return false;
    }
    ALOGD ("%s: tech=%d, timeout=%d", __FUNCTION__, tech, timeout);

    NfcTag::getInstance().setTransceiveTimeout (tech, timeout);
    return true;
}


/*******************************************************************************
**
** Function:        nfcManager_doGetTimeout
**
** Description:     Get timeout value.
**                  e: JVM environment.
**                  o: Java object.
**                  tech: technology ID.
**
** Returns:         Timeout value.
**
*******************************************************************************/
static jint nfcManager_doGetTimeout(JNIEnv*, jobject, jint tech)
{
    int timeout = NfcTag::getInstance().getTransceiveTimeout (tech);
    ALOGD ("%s: tech=%d, timeout=%d", __FUNCTION__, tech, timeout);
    return timeout;
}


/*******************************************************************************
**
** Function:        nfcManager_doDump
**
** Description:     Not used.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         Text dump.
**
*******************************************************************************/
static jstring nfcManager_doDump(JNIEnv* e, jobject)
{
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "libnfc llc error_count=%u", /*libnfc_llc_error_count*/ 0);
    return e->NewStringUTF(buffer);
}


/*******************************************************************************
**
** Function:        nfcManager_doSetP2pInitiatorModes
**
** Description:     Set P2P initiator's activation modes.
**                  e: JVM environment.
**                  o: Java object.
**                  modes: Active and/or passive modes.  The values are specified
**                          in external/libnfc-nxp/inc/phNfcTypes.h.  See
**                          enum phNfc_eP2PMode_t.
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_doSetP2pInitiatorModes (JNIEnv *e, jobject o, jint modes)
{
    ALOGD ("%s: modes=0x%X", __FUNCTION__, modes);
    struct nfc_jni_native_data *nat = getNative(e, o);

    tNFA_TECHNOLOGY_MASK mask = 0;
    if (modes & 0x01) mask |= NFA_TECHNOLOGY_MASK_A;
    if (modes & 0x02) mask |= NFA_TECHNOLOGY_MASK_F;
    if (modes & 0x04) mask |= NFA_TECHNOLOGY_MASK_F;
    if (modes & 0x08) mask |= NFA_TECHNOLOGY_MASK_A_ACTIVE;
    if (modes & 0x10) mask |= NFA_TECHNOLOGY_MASK_F_ACTIVE;
    if (modes & 0x20) mask |= NFA_TECHNOLOGY_MASK_F_ACTIVE;
    nat->tech_mask = mask;
}

#if(NXP_EXTNS == TRUE)
/*******************************************************************************
**
** Function:        nfcManager_getRouting
**
** Description:     Get Routing Table information.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         Current routing Settings.
**
*******************************************************************************/
static jbyteArray nfcManager_getRouting (JNIEnv *e, jobject /* o */)
{
    ALOGD ("%s : Enter", __FUNCTION__);
    jbyteArray jbuff = NULL;
    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }
    SyncEventGuard guard (sNfaGetRoutingEvent);
    sRoutingBuffLen = 0;
    RoutingManager::getInstance().getRouting();
    sNfaGetRoutingEvent.wait ();
    if(sRoutingBuffLen > 0)
    {
        jbuff = e->NewByteArray (sRoutingBuffLen);
        e->SetByteArrayRegion (jbuff, 0, sRoutingBuffLen, (jbyte*) sRoutingBuff);
    }

    startRfDiscovery(true);
    return jbuff;
}

/*******************************************************************************
**
** Function:        nfcManager_getNfcInitTimeout
**
** Description:     Gets the chip version.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         timeout in seconds
**
*******************************************************************************/
static int nfcManager_getNfcInitTimeout(JNIEnv* e, jobject o)
{
    (void)e;
    (void)o;
    ALOGD ("%s: enter", __FUNCTION__);
    unsigned long disc_timeout =0;
    unsigned long session_id_timeout =0;
    disc_timeout = 0;
    gNfcInitTimeout = 0;
    gdisc_timeout = 0;

    if(GetNxpNumValue(NAME_NXP_DEFAULT_NFCEE_DISC_TIMEOUT, (void *)&disc_timeout, sizeof(disc_timeout))==false)
    {
        ALOGD ("NAME_NXP_DEFAULT_NFCEE_DISC_TIMEOUT not found");
        disc_timeout = 0;
    }
    if(GetNxpNumValue(NAME_NXP_DEFAULT_NFCEE_TIMEOUT, (void *)&session_id_timeout,
            sizeof(session_id_timeout))==false)
    {
        ALOGD ("NAME_NXP_DEFAULT_NFCEE_TIMEOUT not found");
        session_id_timeout = 0;
    }

    gNfcInitTimeout = (disc_timeout + session_id_timeout) *1000;
    gdisc_timeout = disc_timeout *1000;

    ALOGD ("%s gNfcInitTimeout = %ld: gdisc_timeout = %ld nfcManager_getNfcInitTimeout",
            __func__, gNfcInitTimeout, gdisc_timeout);
    return gNfcInitTimeout;
}

#endif

/*******************************************************************************
**
** Function:        nfcManager_doSetP2pTargetModes
**
** Description:     Set P2P target's activation modes.
**                  e: JVM environment.
**                  o: Java object.
**                  modes: Active and/or passive modes.
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_doSetP2pTargetModes (JNIEnv*, jobject, jint modes)
{
    ALOGD ("%s: modes=0x%X", __FUNCTION__, modes);
    // Map in the right modes
    tNFA_TECHNOLOGY_MASK mask = 0;
    if (modes & 0x01) mask |= NFA_TECHNOLOGY_MASK_A;
    if (modes & 0x02) mask |= NFA_TECHNOLOGY_MASK_F;
    if (modes & 0x04) mask |= NFA_TECHNOLOGY_MASK_F;
    if (modes & 0x08) mask |= NFA_TECHNOLOGY_MASK_A_ACTIVE | NFA_TECHNOLOGY_MASK_F_ACTIVE;

    PeerToPeer::getInstance().setP2pListenMask(mask);
}

#if(NXP_EXTNS == TRUE)
/*******************************************************************************
**
** Function:        nfcManager_dosetEtsiReaederState
**
** Description:     Set ETSI reader state
**                  e: JVM environment.
**                  o: Java object.
**                  newState : new state to be set
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_dosetEtsiReaederState (JNIEnv*, jobject, se_rd_req_state_t newState)
{
    ALOGD ("%s: Enter ", __FUNCTION__);
#if(NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE)
    RoutingManager::getInstance().setEtsiReaederState(newState);
#endif
}

/*******************************************************************************
**
** Function:        nfcManager_dogetEtsiReaederState
**
** Description:     Get current ETSI reader state
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         State.
**
*******************************************************************************/
static int nfcManager_dogetEtsiReaederState (JNIEnv*, jobject)
{
    ALOGD ("%s: Enter ", __FUNCTION__);
#if(NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE)
    return RoutingManager::getInstance().getEtsiReaederState();
#else
    return STATE_SE_RDR_MODE_STOPPED;
#endif
}

/*******************************************************************************
**
** Function:        nfcManager_doEtsiReaderConfig
**
** Description:     Configuring to Emvco profile
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_doEtsiReaderConfig (JNIEnv*, jobject, int eeHandle)
{
    tNFC_STATUS status;
    ALOGD ("%s: Enter ", __FUNCTION__);
#if(NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE)
    status = SecureElement::getInstance().etsiReaderConfig(eeHandle);
    if(status != NFA_STATUS_OK)
    {
        ALOGD ("%s: etsiReaderConfig Failed ", __FUNCTION__);
    }
    else
    {
        ALOGD ("%s: etsiReaderConfig Success ", __FUNCTION__);
    }
#endif
}

/*******************************************************************************
**
** Function:        nfcManager_doEtsiResetReaderConfig
**
** Description:     Configuring to Nfc forum profile
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_doEtsiResetReaderConfig (JNIEnv*, jobject)
{
    tNFC_STATUS status;
    ALOGD ("%s: Enter ", __FUNCTION__);
#if(NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE)
    status = SecureElement::getInstance().etsiResetReaderConfig();
    if(status != NFA_STATUS_OK)
    {
        ALOGD ("%s: etsiReaderConfig Failed ", __FUNCTION__);
    }
    else
    {
        ALOGD ("%s: etsiReaderConfig Success ", __FUNCTION__);
    }
#endif
}

/*******************************************************************************
**
** Function:        nfcManager_doNotifyEEReaderEvent
**
** Description:     Notify with the Reader event
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_doNotifyEEReaderEvent (JNIEnv*, jobject, int evt)
{
    ALOGD ("%s: Enter ", __FUNCTION__);
#if(NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE)
    SecureElement::getInstance().notifyEEReaderEvent(evt,swp_rdr_req_ntf_info.swp_rd_req_info.tech_mask);
#endif
}

/*******************************************************************************
**
** Function:        nfcManager_doEtsiInitConfig
**
** Description:     Chnage the ETSI state before start configuration
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_doEtsiInitConfig (JNIEnv*, jobject, int /* evt */)
{
    ALOGD ("%s: Enter ", __FUNCTION__);
#if(NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE)
    SecureElement::getInstance().etsiInitConfig();
#endif
}
#endif

static void nfcManager_doEnableScreenOffSuspend(JNIEnv* /* e */, jobject /* o */)
{
    PowerSwitch::getInstance().setScreenOffPowerState(PowerSwitch::POWER_STATE_FULL);
}

static void nfcManager_doDisableScreenOffSuspend(JNIEnv* /* e */, jobject /* o */)
{
    PowerSwitch::getInstance().setScreenOffPowerState(PowerSwitch::POWER_STATE_OFF);
}
#if(NXP_EXTNS == TRUE)
/*******************************************************************************
**
** Function:        nfcManager_doUpdateScreenState
**
** Description:     Update If any Pending screen state is present
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_doUpdateScreenState(JNIEnv* /* e */, jobject /* o */)
{
    ALOGD ("%s: Enter ", __FUNCTION__);
#if(NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE)
    eScreenState_t last_screen_state_request;

    if(pendingScreenState == true)
    {
        ALOGD ("%s: pendingScreenState = TRUE ", __FUNCTION__);
        pendingScreenState = false;
        last_screen_state_request = get_lastScreenStateRequest();
        nfcManager_doSetScreenState(NULL,NULL,last_screen_state_request);
    }
    else
    {
        ALOGD ("%s: pendingScreenState = FALSE ", __FUNCTION__);
    }
#endif
}
#endif
#if(NXP_EXTNS == TRUE)
/*******************************************************************************
 **
 ** Function:        nfcManager_doSelectUicc()
 **
 ** Description:     Issue any single TLV set config command as per input
 ** register values and bit values
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
static int nfcManager_doSelectUicc(JNIEnv* e, jobject o, jint uiccSlot)
{
    (void)e;
    (void)o;
    uint8_t retStat = STATUS_UNKNOWN_ERROR;

#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
    tNFA_STATUS status = NFC_STATUS_FAILED;
    ALOGD("%s: enter", __FUNCTION__);
    ALOGD("%s: sUicc1CntxLen : 0x%02x  sUicc2CntxLen : 0x%02x", __FUNCTION__,dualUiccInfo.sUicc1CntxLen,dualUiccInfo.sUicc2CntxLen);
    UINT16 RegAddr = 0xA0EC;
    uint8_t bitVal;
    eScreenState_t last_screen_state_request;
    dualUiccInfo.uiccConfigStat = UICC_NOT_CONFIGURED;

    RoutingManager& routingManager = RoutingManager::getInstance();
    SecureElement &se = SecureElement::getInstance();

    retStat = nfcManager_staticDualUicc_Precondition(uiccSlot);

    if(retStat != UICC_NOT_CONFIGURED)
    {
        goto endSwitch;
    }

    if(sRfEnabled)
    {
        startRfDiscovery(false);
    }

    bitVal = ((0x10) | uiccSlot);

    getUiccContext(uiccSlot);

    if((dualUiccInfo.sUicc1CntxLen !=0)||(dualUiccInfo.sUicc2CntxLen !=0))
    {

        if((bitVal == 0x11)&&(dualUiccInfo.sUicc1CntxLen !=0))
        {
            ALOGD ("%s : update uicc1 context information ", __FUNCTION__);
            UINT8 cfg[256] = {0x20,0x02};

            memcpy(cfg+3, dualUiccInfo.sUicc1Cntx, dualUiccInfo.sUicc1CntxLen);
            cfg[2] = dualUiccInfo.sUicc1CntxLen-1;
            status = NxpNfc_Write_Cmd_Common(dualUiccInfo.sUicc1CntxLen+2, cfg);

            memcpy(cfg+3, dualUiccInfo.sUicc1TechCapblty, 10);
            cfg[2] = 9;
            status = NxpNfc_Write_Cmd_Common(12, cfg);

        }
        else  if((bitVal == 0x12)&&(dualUiccInfo.sUicc2CntxLen !=0))
        {
            ALOGD ("%s : update uicc2 context information", __FUNCTION__);
            UINT8 cfg[256] = {0x20,0x02};
            memcpy(cfg+3, dualUiccInfo.sUicc2Cntx, dualUiccInfo.sUicc2CntxLen);
            cfg[2] = dualUiccInfo.sUicc2CntxLen-1;
            status = NxpNfc_Write_Cmd_Common(dualUiccInfo.sUicc2CntxLen+2, cfg);

            memcpy(cfg+3, dualUiccInfo.sUicc2TechCapblty, 10);
            cfg[2] = 9;
            status = NxpNfc_Write_Cmd_Common(12, cfg);
        }
    }

    /*Update NFCC SWIO line accordingly*/
    if((Set_EERegisterValue(RegAddr, bitVal) != NFCSTATUS_OK))
    {
        retStat = DUAL_UICC_ERROR_SELECT_FAILED;
        ALOGE ("%s : Set_EERegisterValue Failed", __FUNCTION__);
        goto endSwitch;
    }

    /*Mode Set Off for UICC*/
    {
        SyncEventGuard guard (routingManager.mEeSetModeEvent);
        if ((NFA_EeModeSet (0x02, NFA_EE_MD_DEACTIVATE)) == NFA_STATUS_OK)
        {
            routingManager.mEeSetModeEvent.wait (); //wait for NFA_EE_MODE_SET_EVT
        }
        else
        {
            ALOGE ("%s : Failed to set EE inactive", __FUNCTION__);
            goto endSwitch;
        }
    }
    gSeDiscoverycount  = 0;
    /*Perform HAL re-initialisation
     * NFA EE and HCI Subsystem de-init*/
    {
        SyncEventGuard guard (sNfceeHciCbDisableEvent);
        NFA_EE_HCI_Control(false);
        sNfceeHciCbDisableEvent.wait(500);
    }

    /*Reset Nfcc*/
    status = NFA_ResetNfcc();
    /*Perform NFA EE and HCI Subsystem initialisation*/
    {
        SyncEventGuard guard (sNfceeHciCbEnableEvent);
        NFA_EE_HCI_Control(true);
        sNfceeHciCbEnableEvent.wait (500);
    }

    {
        se.updateEEStatus();
        //setListenMode();
        routingManager.initialize(getNative(e, o));
        HciRFParams::getInstance().initialize ();
        sIsSecElemSelected = (se.getActualNumEe() - 1 );
        sIsSecElemDetected = sIsSecElemSelected;
    }

    ALOGD("%s : gSeDiscoverycount = %ld", __FUNCTION__ , gSeDiscoverycount);
    {
        SyncEventGuard g(gNfceeDiscCbEvent);
        /*Get the SWP1 and SWP2 lines status*/
        if (NFA_STATUS_OK == GetNumNFCEEConfigured())
        {
         /*The SWP lines enabled and SE's discovered*/
            if (gSeDiscoverycount < gActualSeCount)
            {
                ALOGD("%s : Wait for ESE to discover, gdisc_timeout = %ld", __FUNCTION__, gdisc_timeout);
                if(gNfceeDiscCbEvent.wait(gdisc_timeout) == false)
                {
                    ALOGE ("%s: timeout waiting for nfcee dis event", __FUNCTION__);
                }
            }
            else
            {
                ALOGD("%s : All ESE are discovered ", __FUNCTION__);
            }
        }
    }
    /*Get the eSE and UICC parameters for RF*/
    checkforNfceeConfig(UICC1 | UICC2 | ESE);

    if(se.getEeStatus(UICC_HANDLE) == NFC_NFCEE_STATUS_REMOVED)
    {
        ALOGD("%s : UICC 0x%02x status : NFC_NFCEE_STATUS_REMOVED. Clearing buffer", __FUNCTION__,sSelectedUicc);
        if((sSelectedUicc == 0x01)&&(dualUiccInfo.sUicc1CntxLen != 0x00))
        {
            memset(dualUiccInfo.sUicc1Cntx,0x00,sizeof(dualUiccInfo.sUicc1Cntx));
            memset(dualUiccInfo.sUicc1TechCapblty,0x00,10);
            dualUiccInfo.sUicc1CntxLen = 0x00;
            write_uicc_context(dualUiccInfo.sUicc1Cntx,  dualUiccInfo.sUicc1CntxLen, dualUiccInfo.sUicc1TechCapblty, 10, 1, sSelectedUicc);
        }
        else if((sSelectedUicc == 0x02)&&(dualUiccInfo.sUicc2CntxLen != 0x00))
        {
            memset(dualUiccInfo.sUicc2Cntx,0x00,sizeof(dualUiccInfo.sUicc2Cntx));
            memset(dualUiccInfo.sUicc2TechCapblty,0x00,10);
            dualUiccInfo.sUicc2CntxLen = 0x00;
            write_uicc_context(dualUiccInfo.sUicc2Cntx,  dualUiccInfo.sUicc2CntxLen, dualUiccInfo.sUicc2TechCapblty, 10, 1, sSelectedUicc);
        }
    }

    retStat = dualUiccInfo.uiccConfigStat;

    endSwitch:
    if((retStat == UICC_CONFIGURED) || (retStat == UICC_NOT_CONFIGURED))
    {
        if(update_transaction_stat("staticDualUicc",RESET_TRANSACTION_STATE))
        {
            /*Apply screen state if pending*/
            ALOGD("%s: Apply screen state if pending", __FUNCTION__);
            if(pendingScreenState == true)
            {
                pendingScreenState = false;
                last_screen_state_request = get_lastScreenStateRequest();
                nfcManager_doSetScreenState(NULL,NULL,last_screen_state_request);
            }
        }
        else
        {
            ALOGE("%s: Can not reset transaction state", __FUNCTION__);
        }
    }

    /*If retStat is success then routing table will be reconfigured from NfcService
     * As a part of commitRouting startRfDiscovery will be called.
     * If retStat is failed then NfcService will not reconfigured routing table
     * So do startRfDiscovery here*/
    if((retStat != UICC_CONFIGURED) && (retStat != UICC_NOT_CONFIGURED) && (!sRfEnabled))
    {
        startRfDiscovery(true);
    }

    ALOGD("%s: exit retStat = %d", __FUNCTION__, retStat);
#elif(NFC_NXP_STAT_DUAL_UICC_WO_EXT_SWITCH == TRUE)
    retStat = nfcManager_staticDualUicc_Precondition(uiccSlot);

    if(retStat != UICC_NOT_CONFIGURED)
    {
        ALOGD("staticDualUicc_Precondition failed.");
        return retStat;
    }

    nfcManager_setPreferredSimSlot(NULL, NULL,uiccSlot);
    retStat = UICC_CONFIGURED;
    RoutingManager::getInstance().cleanRouting();
    if(!update_transaction_stat("staticDualUicc",RESET_TRANSACTION_STATE))
    {
        ALOGE("%s: Transaction in progress. Can not reset", __FUNCTION__);
    }
#else
    retStat = DUAL_UICC_FEATURE_NOT_AVAILABLE;
    ALOGD("%s: Dual uicc not supported retStat = %d", __FUNCTION__, retStat);
#endif
    return retStat;
}

/*******************************************************************************
 **
 ** Function:        nfcManager_doGetSelectedUicc()
 **
 ** Description:     get the current selected active UICC
 **
 ** Returns:         UICC id
 **
 *******************************************************************************/
static int nfcManager_doGetSelectedUicc(JNIEnv* e, jobject o)
{
    uint8_t uicc_stat = STATUS_UNKNOWN_ERROR;
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
    ALOGD ("%s: enter ",__FUNCTION__);
    uicc_stat = SecureElement::getInstance().getUiccStatus(sSelectedUicc);
#elif (NFC_NXP_STAT_DUAL_UICC_WO_EXT_SWITCH == TRUE)
    uicc_stat = SecureElement::getInstance().getUiccStatus(sCurrentSelectedUICCSlot);
#else
    ALOGD ("%s: dual uicc not supported ",__FUNCTION__);
    uicc_stat = DUAL_UICC_FEATURE_NOT_AVAILABLE;
#endif
    return uicc_stat;
}
#endif

/*****************************************************************************
**
** JNI functions for android-4.0.1_r1
**
*****************************************************************************/
static JNINativeMethod gMethods[] =
{
    {"doDownload", "()Z",
            (void *)nfcManager_doDownload},

    {"initializeNativeStructure", "()Z",
            (void*) nfcManager_initNativeStruc},

    {"doInitialize", "()Z",
            (void*) nfcManager_doInitialize},

    {"doDeinitialize", "()Z",
            (void*) nfcManager_doDeinitialize},

    {"sendRawFrame", "([B)Z",
            (void*) nfcManager_sendRawFrame},

    {"doRouteAid", "([BIIZ)Z",
            (void*) nfcManager_routeAid},

    {"doUnrouteAid", "([B)Z",
            (void*) nfcManager_unrouteAid},

    {"doSetRoutingEntry", "(IIII)Z",
            (void*)nfcManager_setRoutingEntry},

    {"doClearRoutingEntry", "(I)Z",
            (void*)nfcManager_clearRoutingEntry},

    {"clearAidTable", "()Z",
            (void*) nfcManager_clearAidTable},

    {"setDefaultRoute", "(III)Z",
            (void*) nfcManager_setDefaultRoute},

    {"getAidTableSize", "()I",
            (void*) nfcManager_getAidTableSize},

    {"getRemainingAidTableSize", "()I",
            (void*) nfcManager_getRemainingAidTableSize},

    {"getDefaultAidRoute", "()I",
            (void*) nfcManager_getDefaultAidRoute},

    {"getDefaultDesfireRoute", "()I",
            (void*) nfcManager_getDefaultDesfireRoute},

    {"getDefaultMifareCLTRoute", "()I",
            (void*) nfcManager_getDefaultMifareCLTRoute},
#if(NXP_EXTNS == TRUE)
    {"getDefaultAidPowerState", "()I",
            (void*) nfcManager_getDefaultAidPowerState},

    {"getDefaultDesfirePowerState", "()I",
            (void*) nfcManager_getDefaultDesfirePowerState},

    {"getDefaultMifareCLTPowerState", "()I",
            (void*) nfcManager_getDefaultMifareCLTPowerState},
#endif
    {"doRegisterT3tIdentifier", "([B)I",
            (void*) nfcManager_doRegisterT3tIdentifier},

    {"doDeregisterT3tIdentifier", "(I)V",
            (void*) nfcManager_doDeregisterT3tIdentifier},

    {"getLfT3tMax", "()I",
            (void*) nfcManager_getLfT3tMax},

    {"doEnableDiscovery", "(IZZZZ)V",
            (void*) nfcManager_enableDiscovery},

    {"doGetSecureElementList", "()[I",
            (void *)nfcManager_doGetSecureElementList},

    {"doSelectSecureElement", "(I)V",
            (void *)nfcManager_doSelectSecureElement},

    {"doDeselectSecureElement", "(I)V",
            (void *)nfcManager_doDeselectSecureElement},

    {"doSetSEPowerOffState", "(IZ)V",
            (void *)nfcManager_doSetSEPowerOffState},
    {"setDefaultTechRoute", "(III)V",
            (void *)nfcManager_setDefaultTechRoute},

    {"setDefaultProtoRoute", "(III)V",
            (void *)nfcManager_setDefaultProtoRoute},

    {"GetDefaultSE", "()I",
            (void *)nfcManager_GetDefaultSE},

    {"doCheckLlcp", "()Z",
            (void *)nfcManager_doCheckLlcp},

    {"doActivateLlcp", "()Z",
            (void *)nfcManager_doActivateLlcp},

    {"doCreateLlcpConnectionlessSocket", "(ILjava/lang/String;)Lcom/android/nfc/dhimpl/NativeLlcpConnectionlessSocket;",
            (void *)nfcManager_doCreateLlcpConnectionlessSocket},

    {"doCreateLlcpServiceSocket", "(ILjava/lang/String;III)Lcom/android/nfc/dhimpl/NativeLlcpServiceSocket;",
            (void*) nfcManager_doCreateLlcpServiceSocket},

    {"doCreateLlcpSocket", "(IIII)Lcom/android/nfc/dhimpl/NativeLlcpSocket;",
            (void*) nfcManager_doCreateLlcpSocket},

    {"doGetLastError", "()I",
            (void*) nfcManager_doGetLastError},

    {"disableDiscovery", "()V",
            (void*) nfcManager_disableDiscovery},

    {"doSetTimeout", "(II)Z",
            (void *)nfcManager_doSetTimeout},

    {"doGetTimeout", "(I)I",
            (void *)nfcManager_doGetTimeout},

    {"doResetTimeouts", "()V",
            (void *)nfcManager_doResetTimeouts},

    {"doAbort", "()V",
            (void *)nfcManager_doAbort},

    {"doSetP2pInitiatorModes", "(I)V",
            (void *)nfcManager_doSetP2pInitiatorModes},

    {"doSetP2pTargetModes", "(I)V",
            (void *)nfcManager_doSetP2pTargetModes},

    {"doEnableScreenOffSuspend", "()V",
            (void *)nfcManager_doEnableScreenOffSuspend},

    {"doDisableScreenOffSuspend", "()V",
            (void *)nfcManager_doDisableScreenOffSuspend},

    {"doDump", "()Ljava/lang/String;",
            (void *)nfcManager_doDump},

    {"getChipVer", "()I",
             (void *)nfcManager_getChipVer},

    {"getFwFileName", "()[B",
            (void *)nfcManager_getFwFileName},

    {"JCOSDownload", "()I",
            (void *)nfcManager_doJcosDownload},
    {"doCommitRouting", "()V",
            (void *)nfcManager_doCommitRouting},
#if(NXP_EXTNS == TRUE)
    {"doSetNfcMode", "(I)V",
            (void *)nfcManager_doSetNfcMode},
#endif
    {"doGetSecureElementTechList", "()I",
            (void *)nfcManager_getSecureElementTechList},

    {"doGetActiveSecureElementList", "()[I",
            (void *)nfcManager_getActiveSecureElementList},

    {"doGetSecureElementUid", "()[B",
            (void *)nfcManager_getSecureElementUid},

    {"setEmvCoPollProfile", "(ZI)I",
            (void *)nfcManager_setEmvCoPollProfile},

    {"doSetSecureElementListenTechMask", "(I)V",
            (void *)nfcManager_setSecureElementListenTechMask},
    {"doSetScreenState", "(I)V",
            (void*)nfcManager_doSetScreenState},
    {"doSetScreenOrPowerState", "(I)V",
            (void*)nfcManager_doSetScreenOrPowerState},
    //Factory Test Code
    {"doPrbsOn", "(IIII)V",
            (void *)nfcManager_doPrbsOn},
    {"doPrbsOff", "()V",
            (void *)nfcManager_doPrbsOff},
    // SWP self test
    {"SWPSelfTest", "(I)I",
            (void *)nfcManager_SWPSelfTest},
    // check firmware version
    {"getFWVersion", "()I",
            (void *)nfcManager_getFwVersion},
#if(NXP_EXTNS == TRUE)
    {"setEtsiReaederState", "(I)V",
        (void *)nfcManager_dosetEtsiReaederState},

    {"getEtsiReaederState", "()I",
        (void *)nfcManager_dogetEtsiReaederState},

    {"etsiReaderConfig", "(I)V",
            (void *)nfcManager_doEtsiReaderConfig},

    {"etsiResetReaderConfig", "()V",
            (void *)nfcManager_doEtsiResetReaderConfig},

    {"notifyEEReaderEvent", "(I)V",
            (void *)nfcManager_doNotifyEEReaderEvent},

    {"etsiInitConfig", "()V",
            (void *)nfcManager_doEtsiInitConfig},

    {"updateScreenState", "()V",
            (void *)nfcManager_doUpdateScreenState},
#endif
#if(NXP_EXTNS == TRUE)
    {"doEnablep2p", "(Z)V",
            (void*)nfcManager_Enablep2p},
    {"doSetProvisionMode", "(Z)V",
            (void *)nfcManager_setProvisionMode},
    {"doGetRouting", "()[B",
            (void *)nfcManager_getRouting},
    {"getNfcInitTimeout", "()I",
             (void *)nfcManager_getNfcInitTimeout},
    {"isVzwFeatureEnabled", "()Z",
            (void *)nfcManager_isVzwFeatureEnabled},
    {"isNfccBusy", "()Z",
            (void *)nfcManager_isNfccBusy},
#endif
    {"doSetEEPROM", "([B)V",
            (void*)nfcManager_doSetEEPROM},
    {"doGetSeInterface","(I)I",
            (void*)nfcManager_doGetSeInterface},
    //Factory Test Code
    {"doCheckJcopDlAtBoot", "()Z",
            (void *)nfcManager_doCheckJcopDlAtBoot},
    {"doEnableDtaMode", "()V",
            (void*) nfcManager_doEnableDtaMode},
    {"doDisableDtaMode", "()V",
            (void*) nfcManager_doDisableDtaMode},
    {"getAdditionalConfigOptions", "()[B",
            (void *)nfcManager_getAdditionalConfigOptions}
#if(NXP_EXTNS == TRUE)
    ,{"doselectUicc", "(I)I",
            (void*) nfcManager_doSelectUicc},
     {"doGetSelectedUicc", "()I",
            (void*) nfcManager_doGetSelectedUicc},
     {"setPreferredSimSlot", "(I)I",
            (void *)nfcManager_setPreferredSimSlot},
#endif
};


/*******************************************************************************
**
** Function:        register_com_android_nfc_NativeNfcManager
**
** Description:     Regisgter JNI functions with Java Virtual Machine.
**                  e: Environment of JVM.
**
** Returns:         Status of registration.
**
*******************************************************************************/
int register_com_android_nfc_NativeNfcManager (JNIEnv *e)
{
    ALOGD ("%s: enter", __FUNCTION__);
    PowerSwitch::getInstance ().initialize (PowerSwitch::UNKNOWN_LEVEL);
    ALOGD ("%s: exit", __FUNCTION__);
    return jniRegisterNativeMethods (e, gNativeNfcManagerClassName, gMethods, NELEM (gMethods));
}


/*******************************************************************************
**
** Function:        startRfDiscovery
**
** Description:     Ask stack to start polling and listening for devices.
**                  isStart: Whether to start.
**
** Returns:         None
**
*******************************************************************************/
void startRfDiscovery(bool isStart)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    if(sAutonomousSet == 1)
    {
        ALOGD("Autonomous mode set don't start RF disc %d",isStart);
        return;
    }

#if((NXP_EXTNS == TRUE) && (NFC_NXP_ESE == TRUE) && (NXP_NFCC_ESE_UICC_CONCURRENT_ACCESS_PROTECTION == TRUE))
    gDiscMutex.lock();
#endif
    ALOGD ("%s: is start=%d", __FUNCTION__, isStart);
    SyncEventGuard guard (sNfaEnableDisablePollingEvent);
    status  = isStart ? NFA_StartRfDiscovery () : NFA_StopRfDiscovery ();
    if (status == NFA_STATUS_OK)
    {
        if(gGeneralPowershutDown == NFC_MODE_OFF)
            sDiscCmdwhleNfcOff = true;
        sNfaEnableDisablePollingEvent.wait (); //wait for NFA_RF_DISCOVERY_xxxx_EVT
        sRfEnabled = isStart;
        sDiscCmdwhleNfcOff = false;
    }
    else
    {
        ALOGE ("%s: Failed to start/stop RF discovery; error=0x%X", __FUNCTION__, status);
    }
#if((NXP_EXTNS == TRUE) && (NFC_NXP_ESE == TRUE) && (NXP_NFCC_ESE_UICC_CONCURRENT_ACCESS_PROTECTION == TRUE))
    gDiscMutex.unlock();
#endif
    ALOGD ("%s: is exit=%d", __FUNCTION__, isStart);
}

/*******************************************************************************
 **
** Function:        isDiscoveryStarted
**
** Description:     Indicates whether the discovery is started.
**
** Returns:         True if discovery is started
**
*******************************************************************************/
bool isDiscoveryStarted ()
{
    return sRfEnabled;
}

/*******************************************************************************
**
** Function:        notifyPollingEventwhileNfcOff
**
** Description:     Notifies sNfaEnableDisablePollingEvent if tag operations
**                  is in progress at the time Nfc Off is in progress to avoid
**                  NFC off thread infinite block.
**
** Returns:         None
**
*******************************************************************************/
static void notifyPollingEventwhileNfcOff()
{
    ALOGD ("%s: sDiscCmdwhleNfcOff=%x", __FUNCTION__, sDiscCmdwhleNfcOff);
    if(sDiscCmdwhleNfcOff == true)
    {
        SyncEventGuard guard (sNfaEnableDisablePollingEvent);
        sNfaEnableDisablePollingEvent.notifyOne ();
    }
}

/*******************************************************************************
**
** Function:        doStartupConfig
**
** Description:     Configure the NFC controller.
**
** Returns:         None
**
*******************************************************************************/
void doStartupConfig()
{
    struct nfc_jni_native_data *nat = getNative(0, 0);
    tNFA_STATUS stat = NFA_STATUS_FAILED;
    int actualLen = 0;

    // If polling for Active mode, set the ordering so that we choose Active over Passive mode first.
    if (nat && (nat->tech_mask & (NFA_TECHNOLOGY_MASK_A_ACTIVE | NFA_TECHNOLOGY_MASK_F_ACTIVE)))
    {
        UINT8  act_mode_order_param[] = { 0x01 };
        SyncEventGuard guard (sNfaSetConfigEvent);
        stat = NFA_SetConfig(NCI_PARAM_ID_ACT_ORDER, sizeof(act_mode_order_param), &act_mode_order_param[0]);
        if (stat == NFA_STATUS_OK)
            sNfaSetConfigEvent.wait ();
    }

    //configure RF polling frequency for each technology
    static tNFA_DM_DISC_FREQ_CFG nfa_dm_disc_freq_cfg;
    //values in the polling_frequency[] map to members of nfa_dm_disc_freq_cfg
    UINT8 polling_frequency [8] = {1, 1, 1, 1, 1, 1, 1, 1};
    actualLen = GetStrValue(NAME_POLL_FREQUENCY, (char*)polling_frequency, 8);
    if (actualLen == 8)
    {
        ALOGD ("%s: polling frequency", __FUNCTION__);
        memset (&nfa_dm_disc_freq_cfg, 0, sizeof(nfa_dm_disc_freq_cfg));
        nfa_dm_disc_freq_cfg.pa = polling_frequency [0];
        nfa_dm_disc_freq_cfg.pb = polling_frequency [1];
        nfa_dm_disc_freq_cfg.pf = polling_frequency [2];
        nfa_dm_disc_freq_cfg.pi93 = polling_frequency [3];
        nfa_dm_disc_freq_cfg.pbp = polling_frequency [4];
        nfa_dm_disc_freq_cfg.pk = polling_frequency [5];
        nfa_dm_disc_freq_cfg.paa = polling_frequency [6];
        nfa_dm_disc_freq_cfg.pfa = polling_frequency [7];
        p_nfa_dm_rf_disc_freq_cfg = &nfa_dm_disc_freq_cfg;
    }
}


/*******************************************************************************
**
** Function:        nfcManager_isNfcActive
**
** Description:     Used externaly to determine if NFC is active or not.
**
** Returns:         'true' if the NFC stack is running, else 'false'.
**
*******************************************************************************/
bool nfcManager_isNfcActive()
{
    return sIsNfaEnabled;
}

/*******************************************************************************
**
** Function:        startStopPolling
**
** Description:     Start or stop polling.
**                  isStartPolling: true to start polling; false to stop polling.
**
** Returns:         None.
**
*******************************************************************************/
void startStopPolling (bool isStartPolling)
{
    ALOGD ("%s: enter; isStart=%u", __FUNCTION__, isStartPolling);
    startRfDiscovery (false);

    if (isStartPolling) startPolling_rfDiscoveryDisabled(0);
    else stopPolling_rfDiscoveryDisabled();

    startRfDiscovery (true);
    ALOGD ("%s: exit", __FUNCTION__);
}

static tNFA_STATUS startPolling_rfDiscoveryDisabled(tNFA_TECHNOLOGY_MASK tech_mask) {
    tNFA_STATUS stat = NFA_STATUS_FAILED;

    unsigned long num = 0;

    if (tech_mask == 0 && GetNumValue(NAME_POLLING_TECH_MASK, &num, sizeof(num)))
        tech_mask = num;
    else if (tech_mask == 0) tech_mask = DEFAULT_TECH_MASK;

    SyncEventGuard guard (sNfaEnableDisablePollingEvent);
    ALOGD ("%s: enable polling", __FUNCTION__);
    stat = NFA_EnablePolling (tech_mask);
    if (stat == NFA_STATUS_OK)
    {
        ALOGD ("%s: wait for enable event", __FUNCTION__);
        sPollingEnabled = true;
        sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_ENABLED_EVT
    }
    else
    {
        ALOGE ("%s: fail enable polling; error=0x%X", __FUNCTION__, stat);
    }

    return stat;
}

static tNFA_STATUS stopPolling_rfDiscoveryDisabled() {
    tNFA_STATUS stat = NFA_STATUS_FAILED;

    SyncEventGuard guard (sNfaEnableDisablePollingEvent);
    ALOGD ("%s: disable polling", __FUNCTION__);
    stat = NFA_DisablePolling ();
    if (stat == NFA_STATUS_OK) {
        sPollingEnabled = false;
        sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_DISABLED_EVT
    } else {
        ALOGE ("%s: fail disable polling; error=0x%X", __FUNCTION__, stat);
    }

    return stat;
}


/*******************************************************************************
**
** Function:        nfcManager_getChipVer
**
** Description:     Gets the chip version.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None      0x00
**                  PN547C2   0x01
**                  PN65T     0x02 .
**
*******************************************************************************/
static int nfcManager_getChipVer(JNIEnv* /* e */, jobject /* o */)
{
    ALOGD ("%s: enter", __FUNCTION__);
    unsigned long num =0;

    GetNxpNumValue(NAME_NXP_NFC_CHIP, (void *)&num, sizeof(num));
    ALOGD ("%ld: nfcManager_getChipVer", num);
    return num;
}

/*******************************************************************************
**
** Function:        nfcManager_getFwFileName
**
** Description:     Read Fw file name from config file.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         jbyteArray: File name read from config file
**                  NULL in case file name not found
**
*******************************************************************************/
static jbyteArray nfcManager_getFwFileName(JNIEnv* e, jobject o)
{
    (void)o;
    ALOGD ("%s: enter", __FUNCTION__);
    char fwFileName[256];
    int fileLen = 0;
    jbyteArray jbuff = NULL;

    if(GetNxpStrValue(NAME_NXP_FW_NAME, (char*)fwFileName, sizeof(fwFileName)) == TRUE)
    {
        ALOGD ("%s: FW_NAME read success = %s", __FUNCTION__, fwFileName);
        fileLen = strlen(fwFileName);
        jbuff = e->NewByteArray (fileLen);
        e->SetByteArrayRegion (jbuff, 0, fileLen, (jbyte*) fwFileName);
    }
    else
    {
        ALOGD ("%s: FW_NAME not found", __FUNCTION__);
    }

    return jbuff;
}

/*******************************************************************************
**
** Function:        DWPChannel_init
**
** Description:     Initializes the DWP channel functions.
**
** Returns:         True if ok.
**
*******************************************************************************/
#if (NFC_NXP_ESE == TRUE)
void DWPChannel_init(IChannel_t *DWP)
{
    ALOGD ("%s: enter", __FUNCTION__);
    DWP->open = open;
    DWP->close = close;
    DWP->transceive = transceive;
    DWP->doeSE_Reset = doeSE_Reset;
    DWP->doeSE_JcopDownLoadReset = doeSE_JcopDownLoadReset;
}
#endif
/*******************************************************************************
**
** Function:        nfcManager_doJcosDownload
**
** Description:     start jcos download.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static int nfcManager_doJcosDownload(JNIEnv* /* e */, jobject /* o */)
{
#if (NFC_NXP_ESE == TRUE && NXP_EXTNS == TRUE)
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
    bool stat = false;
    SecureElement &se = SecureElement::getInstance();


#if (NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE)
    int ret_val = -1;
    NFCSTATUS ese_status = NFA_STATUS_FAILED;
    p61_access_state_t p61_current_state = P61_STATE_INVALID;
    eScreenState_t last_screen_state_request = get_lastScreenStateRequest();

    if (sIsDisabling || !sIsNfaEnabled || nfcManager_checkNfcStateBusy())
    {
        return NFA_STATUS_FAILED;
    }

    ret_val = NFC_GetP61Status ((void *)&p61_current_state);
    if (ret_val < 0)
    {
        ALOGD("NFC_GetP61Status failed");
        return NFA_STATUS_FAILED;
    }

    if(p61_current_state & P61_STATE_JCP_DWNLD || p61_current_state & P61_STATE_WIRED
       ||p61_current_state & P61_STATE_SPI || p61_current_state & P61_STATE_SPI_PRIO)
          return NFA_STATUS_BUSY;

    if (sIsDisabling || !sIsNfaEnabled || nfcManager_checkNfcStateBusy())
    {
        return NFA_STATUS_FAILED;
    }

    ALOGE("%s: start JcopOs_Download 0x%X", __FUNCTION__,p61_current_state);

    ret_val = NFC_SetP61Status((void *)&ese_status, JCP_DWNLD_START);
    if (ret_val < 0)
    {
       ALOGD("NFC_SetP61Status failed");
    }
    else
    {
        if (ese_status != NFCSTATUS_SUCCESS)
        {
            ALOGD("Denying to set Jcop OS Download state");
            status = ese_status;
        }
        else
        {
            if(!update_transaction_stat("jcosDownload",SET_TRANSACTION_STATE))
            {
                ALOGE("%s: Transaction in progress. Returning", __FUNCTION__);
                return NFA_STATUS_FAILED;
            }
#endif
            if (sRfEnabled) {
                // Stop RF Discovery if we were polling
               startRfDiscovery (false);
            }
            DWPChannel_init(&Dwp);
            status = JCDNLD_Init(&Dwp);
            if(status != NFA_STATUS_OK)
            {
                ALOGE("%s: JCDND initialization failed", __FUNCTION__);
            }
            else
            {
                ALOGE("%s: start JcopOs_Download", __FUNCTION__);
                se.mDownloadMode = JCOP_DOWNLOAD;
#if(NXP_WIRED_MODE_STANDBY == TRUE)
                se.setNfccPwrConfig(se.NFCC_DECIDES);
#endif
                status = JCDNLD_StartDownload();
            }
#if (NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE)
            ret_val = NFC_SetP61Status ((void *)&ese_status, JCP_DWP_DWNLD_COMPLETE);
            if (ret_val < 0)
            {
                ALOGD("NFC_SetP61Status failed Deinit and starting discovery");
            }
            else
            {
                if (ese_status != NFCSTATUS_SUCCESS)
                {
                    ALOGD("Denying to set Jcop OS Download complete state");
                    status = ese_status;
                }
            }
#endif
        stat = JCDNLD_DeInit();
#if (NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE)
        if(update_transaction_stat("jcosDownload",RESET_TRANSACTION_STATE))
        {
            if(pendingScreenState == true)
            {
                pendingScreenState = false;
                last_screen_state_request = get_lastScreenStateRequest();
                nfcManager_doSetScreenState(NULL,NULL,last_screen_state_request);
            }
        }
        else
        {
            ALOGE("%s: Can not reset transaction state", __FUNCTION__);
        }
        }
    }
#endif
    startRfDiscovery (true);
    se.mDownloadMode = NONE;
    ALOGD ("%s: exit; status =0x%X", __FUNCTION__,status);
#else
    tNFA_STATUS status = 0x0F;
    ALOGD ("%s: No p61", __FUNCTION__);
#endif
    return status;
}

#if((NXP_EXTNS == TRUE) && (NFC_NXP_ESE == TRUE))
UINT8 getJCOPOS_UpdaterState()
{
    static const char fn [] = "getJCOPOS_UpdaterState";
    FILE *fp;
    unsigned int val = 0;
    UINT8 state = 0;
    INT32 result = 0;
    ALOGD("%s: enter", fn);

    fp = fopen(JCOP_INFO_PATH, "r");
    if (fp != NULL) {
        result = fscanf(fp, "%u", &val);
        if (result != 0)
        {
            state = (UINT8)(val & 0x000000FF);
            ALOGD("JcopOsState %d", state);
        }
        fclose(fp);
    }
    else
    {
        ALOGE("file <%s> not exits for reading-: %s",
                JCOP_INFO_PATH, strerror(errno));
    }
    return state;
}
#endif

static void nfcManager_doCommitRouting(JNIEnv* e, jobject o)
{
    (void)e;
    (void)o;
    ALOGD ("%s: enter", __FUNCTION__);
    PowerSwitch::getInstance ().setLevel (PowerSwitch::FULL_POWER);
    PowerSwitch::getInstance ().setModeOn (PowerSwitch::HOST_ROUTING);
#if(NXP_EXTNS == TRUE && NXP_NFCC_HCE_F == TRUE)
    if(!update_transaction_stat("commitRouting",SET_TRANSACTION_STATE))
    {
        ALOGD ("%s: Not allowing to commit the routing", __FUNCTION__);
    }
    else
    {
#endif
        if (sRfEnabled) {
           /*Stop RF discovery to reconfigure*/
           startRfDiscovery(false);
        }
        RoutingManager::getInstance().commitRouting();
        startRfDiscovery(true);
#if(NXP_EXTNS == TRUE && NXP_NFCC_HCE_F == TRUE)
    }
#endif

    RoutingManager::getInstance().commitRouting();
    startRfDiscovery(true);
#if(NXP_EXTNS == TRUE && NXP_NFCC_HCE_F == TRUE)
    if(!update_transaction_stat("commitRouting",RESET_TRANSACTION_STATE))
    {
        ALOGE("%s: Can not reset transaction state", __FUNCTION__);
    }
#endif
    ALOGD ("%s: exit", __FUNCTION__);
}
#if(NXP_EXTNS == TRUE)
static void nfcManager_doSetNfcMode(JNIEnv* /* e */, jobject /* o */, jint nfcMode)
{
    /* Store the shutdown state */
    gGeneralPowershutDown = nfcMode;
}
#endif
#if (NXP_NFCEE_REMOVED_NTF_RECOVERY == TRUE)
bool isNfcInitializationDone()
{
    return sIsNfaEnabled;
}
#endif
/*******************************************************************************
**
** Function:        StoreScreenState
**
** Description:     Sets  screen state
**
** Returns:         None
**
*******************************************************************************/
static void StoreScreenState(int state)
{
    ALOGD ("%s: enter", __FUNCTION__);
    screenstate = state;
    nfc_ncif_storeScreenState(state);
    ALOGD ("%s: exit", __FUNCTION__);
}

/*******************************************************************************
**
** Function:        getScreenState
**
** Description:     returns screen state
**
** Returns:         int
**
*******************************************************************************/
int getScreenState()
{
    return screenstate;
}

#if(NFC_NXP_ESE == TRUE && (NFC_NXP_CHIP_TYPE != PN547C2))
/*******************************************************************************
**
** Function:        isp2pActivated
**
** Description:     returns p2pActive state
**
** Returns:         bool
**
*******************************************************************************/
bool isp2pActivated()
{
    return sP2pActive;
}
#endif

/*******************************************************************************
**
** Function:        nfcManager_doSetScreenState
**
** Description:     Set screen state
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doSetScreenState (JNIEnv* /* e */, jobject /* o */, jint state)
{
    tNFA_STATUS status = NFA_STATUS_OK;
    unsigned long auto_num = 0;
    uint8_t standby_num = 0x00;
    uint8_t *buffer = NULL;
    long bufflen = 260;
    long retlen = 0;
    int isfound;
    UINT8 core_reset_cfg[8] = {0x20,0x00,0x01,0x00};
    UINT8 core_init_cfg[8] = {0x20,0x01,0x00};

    ALOGD ("%s: state = %d", __FUNCTION__, state);
    if (sIsDisabling || !sIsNfaEnabled)
        return;

    int old = getScreenState();
    if(old == state) {
        ALOGD("Screen state is not changed.");
        return;
    }

#if (NXP_EXTNS == TRUE)
    if(!update_transaction_stat("setScreenState",SET_TRANSACTION_STATE))
    {
        ALOGE("Payment is in progress stopping enable/disable discovery");
        set_lastScreenStateRequest((eScreenState_t)state);
        pendingScreenState = true;
        return;
    }
#endif
    acquireRfInterfaceMutexLock();
    if (state) {
        if (sRfEnabled) {
            // Stop RF discovery to reconfigure
            startRfDiscovery(false);
        }

    if(state == NFA_SCREEN_STATE_LOCKED || state == NFA_SCREEN_STATE_OFF)
    {
        SyncEventGuard guard (sNfaEnableDisablePollingEvent);
        status = NFA_DisablePolling ();
        if (status == NFA_STATUS_OK)
        {
            sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_DISABLED_EVT
        }else
        ALOGE ("%s: Failed to disable polling; error=0x%X", __FUNCTION__, status);
    }

    if(GetNxpNumValue(NAME_NXP_CORE_SCRN_OFF_AUTONOMOUS_ENABLE,&auto_num ,sizeof(auto_num)))
            {
                 ALOGD ("%s: enter; NAME_NXP_CORE_SCRN_OFF_AUTONOMOUS_ENABLE = %02lx", __FUNCTION__, auto_num);
            }
            if(auto_num == 0x01)
            {
                buffer = (uint8_t*) malloc(bufflen*sizeof(uint8_t));
                if(buffer == NULL)
                {
                    ALOGD ("%s: enter; NAME_NXP_CORE_STANDBY buffer is NULL", __FUNCTION__);
                }
                else
                {
                     isfound = GetNxpByteArrayValue(NAME_NXP_CORE_STANDBY, (char *) buffer,bufflen, &retlen);
                     if (retlen > 0)
                     {
                          standby_num = buffer[3];
                          ALOGD ("%s: enter; NAME_NXP_CORE_STANDBY = %02x", __FUNCTION__, standby_num);
                          SendAutonomousMode(state,standby_num);
                     }
                     else
                     {
                          ALOGD ("%s: enter; NAME_NXP_CORE_STANDBY = %02x", __FUNCTION__, standby_num);
                          SendAutonomousMode(state , standby_num);
                     }
                }
            }
            else
            {
                 ALOGD ("%s: enter; NAME_NXP_CORE_SCRN_OFF_AUTONOMOUS_ENABLE = %02lx", __FUNCTION__, auto_num);
            }
            if(buffer)
            {
                 free(buffer);
            }

        status = SetScreenState(state);
        if (status != NFA_STATUS_OK)
        {
            ALOGE ("%s: fail enable SetScreenState; error=0x%X", __FUNCTION__, status);
        }
        else
        {
            if ((old == NFA_SCREEN_STATE_OFF && state == NFA_SCREEN_STATE_LOCKED)||
#if(NXP_EXTNS == TRUE)
                    (old == NFA_SCREEN_STATE_LOCKED && state == NFA_SCREEN_STATE_UNLOCKED && sProvisionMode)||
#endif
                    (old == NFA_SCREEN_STATE_LOCKED && state == NFA_SCREEN_STATE_OFF && sIsSecElemSelected) )
            {
                if(auto_num != 0x01)
                {
                    ALOGD ("Start RF discovery");
                    startRfDiscovery(true);
                }
            }
            StoreScreenState(state);
        }
        if(sAutonomousSet == 1)
        {
            ALOGD ("Send Core reset");
            status = NxpNfc_Write_Cmd_Common(4, core_reset_cfg);
            status = NxpNfc_Write_Cmd_Common(3, core_init_cfg);
        }
        ALOGD ("%s: auto_num : %d  sAutonomousSet : %d  sRfFieldOff : %d", __FUNCTION__,auto_num,sAutonomousSet,sRfFieldOff);
        if((auto_num == 0x01) && (sAutonomousSet != 1) &&
                (sRfFieldOff == true) && (state == NFA_SCREEN_STATE_OFF))
        {
            buffer = (uint8_t*) malloc(bufflen*sizeof(uint8_t));
            if(buffer == NULL)
            {
                ALOGD ("%s: enter; NAME_NXP_CORE_STANDBY buffer is NULL", __FUNCTION__);
            }
            else
            {
                isfound = GetNxpByteArrayValue(NAME_NXP_CORE_STANDBY, (char *) buffer,bufflen, &retlen);
                if (retlen > 0)
                {
                    standby_num = buffer[3];
                }
                status = SendAutonomousMode(state,standby_num);
                sAutonomousSet = 1;
            }
        }
        else
        {
            sAutonomousSet = 0;
            ALOGD ("Not sending AUTONOMOUS command state is %d", state);
            if (!sRfEnabled) {
                // Start RF discovery if not
                startRfDiscovery(true);
            }

        }
        if(buffer)
        {
            free(buffer);
        }

    }
    releaseRfInterfaceMutexLock();
#if (NXP_EXTNS == TRUE)
    if(!update_transaction_stat("setScreenState",RESET_TRANSACTION_STATE))
    {
        ALOGE("%s: Can not reset transaction state", __FUNCTION__);
    }
#endif
}
#if(NXP_EXTNS == TRUE)
/*******************************************************************************
**
** Function:        nfcManager_doSetScreenOrPowerState
**                  This function combines both screen state and power state(ven power) values.
**
** Description:     Set screen or power state
**                  e: JVM environment.
**                  o: Java object.
**                  state:represents power or screen state (0-3 screen state),6 (power on),7(power off)
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doSetScreenOrPowerState (JNIEnv* e, jobject o, jint state)
{
    ALOGE ("%s: Enter", __FUNCTION__);
    if (state <= NFA_SCREEN_STATE_UNLOCKED ) // SCREEN_STATE
        nfcManager_doSetScreenState(e, o, state);
    else if (state == VEN_POWER_STATE_ON) // POWER_ON NFC_OFF
    {
        nfcManager_doSetNfcMode(e , o, NFC_MODE_OFF);
    }
    else if (state == VEN_POWER_STATE_OFF) // POWER_OFF
    {
        if(sIsNfaEnabled)
        {
#if(NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE)
            if(SecureElement::getInstance().mDownloadMode == JCOP_DOWNLOAD)
            {
               DwpChannel::getInstance().forceClose();
            }
#endif
            nfcManager_doSetNfcMode(e , o, NFC_MODE_ON); //POWER_OFF NFC_ON
        }
        else
        {
            nfcManager_doSetNfcMode(e , o, NFC_MODE_OFF); //POWER_OFF NFC_OFF
        }
    }
    else
        ALOGE ("%s: unknown screen or power state. state=%d", __FUNCTION__, state);
}

#endif
/*******************************************************************************
 **
 ** Function:       get_last_request
 **
 ** Description:    returns the last enable/disable discovery event
 **
 ** Returns:        last request (char) .
 **
 *******************************************************************************/
static char get_last_request()
{
    return(transaction_data.last_request);
}
/*******************************************************************************
 **
 ** Function:       set_last_request
 **
 ** Description:    stores the last enable/disable discovery event
 **
 ** Returns:        None .
 **
 *******************************************************************************/
static void set_last_request(char status, struct nfc_jni_native_data *nat)
{
    if((status == ENABLE_DISCOVERY) || (status == DISABLE_DISCOVERY))
    {
        transaction_data.last_request &= CLEAR_ENABLE_DISABLE_PARAM;
    }
#if(NXP_EXTNS == TRUE && NXP_NFCC_HCE_F == TRUE)
    transaction_data.last_request |= status;
#else
    transaction_data.last_request = status;
#endif
    if (nat != NULL)
    {
        transaction_data.transaction_nat = nat;
    }
}
/*******************************************************************************
 **
 ** Function:       get_lastScreenStateRequest
 **
 ** Description:    returns the last screen state request
 **
 ** Returns:        last screen state request event (eScreenState_t) .
 **
 *******************************************************************************/
 static eScreenState_t get_lastScreenStateRequest()
{
    ALOGD ("%s: %d", __FUNCTION__, transaction_data.last_screen_state_request);
    return(transaction_data.last_screen_state_request);
}

/*******************************************************************************
 **
 ** Function:       set_lastScreenStateRequest
 **
 ** Description:    stores the last screen state request
 **
 ** Returns:        None .
 **
 *******************************************************************************/
static void set_lastScreenStateRequest(eScreenState_t status)
{
    ALOGD ("%s: current=%d, new=%d", __FUNCTION__, transaction_data.last_screen_state_request, status);
    transaction_data.last_screen_state_request = status;
}


/*******************************************************************************
**
** Function:        switchBackTimerProc_transaction
**
** Description:     Callback function for interval timer.
**
** Returns:         None
**
*******************************************************************************/
static void cleanupTimerProc_transaction(union sigval)
{
    ALOGD("Inside cleanupTimerProc");
    cleanup_timer();
}

void cleanup_timer()
{
ALOGD("Inside cleanup");
    pthread_t transaction_thread;
    int irret = -1;
    ALOGD ("%s", __FUNCTION__);

    /* Transcation is done process the last request*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    irret = pthread_create(&transaction_thread, &attr, enableThread, NULL);
    if(irret != 0)
    {
        ALOGE("Unable to create the thread");
    }
    pthread_attr_destroy(&attr);
    transaction_data.current_transcation_state = NFA_TRANS_DM_RF_TRANS_END;
}
#if (NXP_EXTNS == TRUE)
/*******************************************************************************
 **
 ** Function:       update_transaction_stat
 **
 ** Description:    updates the transaction status set/reset
 **                 req_handle : Requesting handle - Module name
 **                 req_state : SET_TRANSACTION_STATE / RESET_TRANSACTION_STATE
 **
 ** Returns:        update status
 **                 ret_stat : true/false
 **
 *******************************************************************************/
bool update_transaction_stat(const char * req_handle, transaction_state_t req_state)
{
    bool ret_stat = false;

    gTransactionMutex.lock();
    /*Check if it is initialisation*/
    if((req_handle == NULL)&&(req_state == RESET_TRANSACTION_STATE))
    {
        ALOGD ("%s: Initialisation. Resetting transaction data", __FUNCTION__);
        transaction_data.trans_in_progress = false;
        cur_transaction_handle = NULL;
        ret_stat = true;
    }
    else
    {
        ALOGD ("%s: Enter. Requested by : %s   Requested state : %s", __FUNCTION__,req_handle,(req_state?"SET":"RESET"));
    }

    /*Check if no transaction is currently ongoing*/
    if(!transaction_data.trans_in_progress)
    {
        if(req_state == SET_TRANSACTION_STATE)
        {
            transaction_data.trans_in_progress = req_state;
            cur_transaction_handle = req_handle;
            ret_stat = true;
            /*Using a backup reset procedure as a timer to reset Transaction state,
             *in case Transaction state is set but not reset because of some reason
             *
             *Also timer should not be started for below handles as these may take
             *more time to reset depending on the transaction duration
             **/
            if((req_handle != NULL) && strcmp(req_handle,"NFA_ACTIVATED_EVT") &&
                    strcmp(req_handle,"NFA_EE_ACTION_EVT") &&
                    strcmp(req_handle,"NFA_TRANS_CE_ACTIVATED") &&
                    strcmp(req_handle,"RF_FIELD_EVT") )
            {
                scleanupTimerProc_transaction.set (10000, cleanupTimerProc_transaction);
            }

        }
        else
        {
            ALOGD ("%s:Transaction state is already free. Returning", __FUNCTION__);
            cur_transaction_handle = NULL;
            ret_stat = true;
            scleanupTimerProc_transaction.kill ();
        }
    }
    else
    {
        /*If transaction_stat is already set (transaction is ongoing) it can not be set again*/
        if(req_state == SET_TRANSACTION_STATE)
        {
            ALOGD ("%s:Transaction is in progress by : %s . Returning", __FUNCTION__,cur_transaction_handle);
            ret_stat = false;
        }
        else
        {
            /*If transaction_stat is already set only authorised module can reset it
             *It should be either cur_transaction_handle (which has set transaction_stat) or
             *exec_pending_req*/
            if(cur_transaction_handle != NULL)
            {
                if(!strcmp(cur_transaction_handle,req_handle) || !strcmp(req_handle,"exec_pending_req"))
                {
                    transaction_data.trans_in_progress = req_state;
                    cur_transaction_handle = NULL;
                    ret_stat = true;
                    scleanupTimerProc_transaction.kill ();
                }
                else
                {
                    ALOGD ("%s:Handle Mismatch. Returning ..cur_transaction_handle : %s   Requested handle  : %s ", __FUNCTION__,cur_transaction_handle,req_handle);
                    ret_stat = false;
                }
            }
            else
            {
                ALOGD ("%s: No cur_transaction_handle. Allowing requested handle  : %s ", __FUNCTION__,req_handle);
                transaction_data.trans_in_progress = req_state;
                cur_transaction_handle = req_handle;
                ret_stat = true;
                scleanupTimerProc_transaction.kill ();
            }
        }
    }
    ALOGD ("%s: Exit.  Requested by : %s   Requested state : %s  status : %s", __FUNCTION__,req_handle,(req_state?"SET":"RESET"), (ret_stat?"SUCCESS":"FAILED"));
    gTransactionMutex.unlock();
    return ret_stat;
}
#endif
/*******************************************************************************
 **
 ** Function:        checkforTranscation
 **
 ** Description:     Receive connection-related events from stack.
 **                  connEvent: Event code.
 **                  eventData: Event data.
 **
 ** Returns:         None
 **
 *******************************************************************************/
void checkforTranscation(UINT8 connEvent, void* eventData)
{
    tNFA_CONN_EVT_DATA *eventAct_Data = (tNFA_CONN_EVT_DATA*) eventData;
    tNFA_DM_CBACK_DATA* eventDM_Conn_data = (tNFA_DM_CBACK_DATA *) eventData;
#if(NFC_NXP_CHIP_TYPE == PN547C2)
    tNFA_EE_CBACK_DATA* ee_action_data = (tNFA_EE_CBACK_DATA *) eventData;
    tNFA_EE_ACTION& action = ee_action_data->action;
#endif
    ALOGD ("%s: enter; event=0x%X transaction_data.current_transcation_state = 0x%x", __FUNCTION__, connEvent,
            transaction_data.current_transcation_state);
    switch(connEvent)
    {
    case NFA_ACTIVATED_EVT:
        if((eventAct_Data->activated.activate_ntf.protocol != NFA_PROTOCOL_NFC_DEP) && (isListenMode(eventAct_Data->activated)))
        {
            ALOGD("ACTIVATED_EVT setting flag");
            transaction_data.current_transcation_state = NFA_TRANS_ACTIVATED_EVT;
#if (NXP_EXTNS == TRUE)
            if(!update_transaction_stat("NFA_ACTIVATED_EVT",SET_TRANSACTION_STATE))
            {
                ALOGE("%s: Transaction in progress. Can not set", __FUNCTION__);
            }
#endif
        }else{
//            ALOGD("other event clearing flag ");
//            memset(&transaction_data, 0x00, sizeof(Transcation_Check_t));
        }
        break;
    case NFA_EE_ACTION_EVT:
        if (transaction_data.current_transcation_state == NFA_TRANS_DEFAULT
            || transaction_data.current_transcation_state == NFA_TRANS_ACTIVATED_EVT)
        {
            if(getScreenState() == NFA_SCREEN_STATE_OFF)
            {
                if (!sP2pActive && eventDM_Conn_data->rf_field.status == NFA_STATUS_OK)
                    SecureElement::getInstance().notifyRfFieldEvent (true);
            }
#if(NFC_NXP_CHIP_TYPE == PN547C2)
            if((action.param.technology == NFC_RF_TECHNOLOGY_A)&&(( getScreenState () == NFA_SCREEN_STATE_OFF ||  getScreenState () == NFA_SCREEN_STATE_LOCKED)))
            {
                transaction_data.current_transcation_state = NFA_TRANS_MIFARE_ACT_EVT;
#if (NXP_EXTNS == TRUE)
                if(!update_transaction_stat("NFA_EE_ACTION_EVT",SET_TRANSACTION_STATE))
                {
                    ALOGE("%s: Transaction in progress. Can not set", __FUNCTION__);
                }
#endif
            }
            else
#endif
            {
                transaction_data.current_transcation_state = NFA_TRANS_EE_ACTION_EVT;
#if (NXP_EXTNS == TRUE)
                if(!update_transaction_stat("NFA_EE_ACTION_EVT",SET_TRANSACTION_STATE))
                {
                    ALOGE("%s: Transaction in progress. Can not set", __FUNCTION__);
                }
#endif
            }
        }
        break;
    case NFA_TRANS_CE_ACTIVATED:
        if (transaction_data.current_transcation_state == NFA_TRANS_DEFAULT || transaction_data.current_transcation_state == NFA_TRANS_ACTIVATED_EVT)
            {
            if(getScreenState() == NFA_SCREEN_STATE_OFF)
            {
                if (!sP2pActive && eventDM_Conn_data->rf_field.status == NFA_STATUS_OK)
                    SecureElement::getInstance().notifyRfFieldEvent (true);
            }
                transaction_data.current_transcation_state = NFA_TRANS_CE_ACTIVATED;
#if (NXP_EXTNS == TRUE)
                if(!update_transaction_stat("NFA_TRANS_CE_ACTIVATED",SET_TRANSACTION_STATE))
                {
                    ALOGE("%s: Transaction in progress. Can not set", __FUNCTION__);
                }
#endif
            }
        break;
    case NFA_TRANS_CE_DEACTIVATED:
        rfActivation = false;
#if (NXP_EXTNS == TRUE)
        if (transaction_data.current_transcation_state == NFA_TRANS_CE_ACTIVATED)
            {
                transaction_data.current_transcation_state = NFA_TRANS_CE_DEACTIVATED;
            }
#endif
        gActivated = false;
        break;
#if (NFC_NXP_CHIP_TYPE == PN547C2)
    case NFA_DEACTIVATED_EVT:
        if(transaction_data.current_transcation_state == NFA_TRANS_MIFARE_ACT_EVT)
        {
            cleanup_timer();
        }
        break;
#endif
    case NFA_TRANS_DM_RF_FIELD_EVT:
        if (eventDM_Conn_data->rf_field.status == NFA_STATUS_OK &&
                (transaction_data.current_transcation_state == NFA_TRANS_EE_ACTION_EVT
                        || transaction_data.current_transcation_state == NFA_TRANS_CE_DEACTIVATED)
                && eventDM_Conn_data->rf_field.rf_field_status == 0)
        {
            ALOGD("start_timer");
#if(NFC_NXP_CHIP_TYPE != PN547C2)
            set_AGC_process_state(false);
#endif
            transaction_data.current_transcation_state = NFA_TRANS_DM_RF_FIELD_EVT_OFF;
            scleanupTimerProc_transaction.set (50, cleanupTimerProc_transaction);
        }
        else if (eventDM_Conn_data->rf_field.status == NFA_STATUS_OK &&
                transaction_data.current_transcation_state == NFA_TRANS_DM_RF_FIELD_EVT_OFF &&
                eventDM_Conn_data->rf_field.rf_field_status == 1)
        {
#if(NFC_NXP_CHIP_TYPE != PN547C2)
            nfcManagerEnableAGCDebug(connEvent);
#endif
            transaction_data.current_transcation_state = NFA_TRANS_DM_RF_FIELD_EVT_ON;
            ALOGD("Payment is in progress hold the screen on/off request ");
            transaction_data.current_transcation_state = NFA_TRANS_DM_RF_TRANS_START;
            scleanupTimerProc_transaction.kill ();

        }
        else if (eventDM_Conn_data->rf_field.status == NFA_STATUS_OK &&
                transaction_data.current_transcation_state == NFA_TRANS_DM_RF_TRANS_START &&
                eventDM_Conn_data->rf_field.rf_field_status == 0)
        {
            ALOGD("Transcation is done");
#if(NFC_NXP_CHIP_TYPE != PN547C2)
            set_AGC_process_state(false);
#endif
            transaction_data.current_transcation_state = NFA_TRANS_DM_RF_TRANS_PROGRESS;
            cleanup_timer();
        }else if(eventDM_Conn_data->rf_field.status == NFA_STATUS_OK &&
                transaction_data.current_transcation_state == NFA_TRANS_ACTIVATED_EVT &&
                eventDM_Conn_data->rf_field.rf_field_status == 0)
        {

            ALOGD("No transaction done cleaning up the variables");
            cleanup_timer();
        }
        break;
    default:
        break;
    }

    ALOGD ("%s: exit; event=0x%X transaction_data.current_transcation_state = 0x%x", __FUNCTION__, connEvent,
            transaction_data.current_transcation_state);
}

/*******************************************************************************
 **
 ** Function:       enableThread
 **
 ** Description:    thread to trigger enable/disable discovery related events
 **
 ** Returns:        None .
 **
 *******************************************************************************/
void *enableThread(void *arg)
{
    (void)arg;
    ALOGD ("%s: enter", __FUNCTION__);
    char last_request = get_last_request();
    eScreenState_t last_screen_state_request = get_lastScreenStateRequest();
#if(NFC_NXP_CHIP_TYPE != PN547C2)
    set_AGC_process_state(false);
#endif
#if (NXP_EXTNS == TRUE)
    if(!update_transaction_stat("exec_pending_req",RESET_TRANSACTION_STATE))
    {
        ALOGE("%s: Transaction in progress. Can not reset", __FUNCTION__);
    }
#endif
    bool screen_lock_flag = false;
    bool disable_discovery = false;

    if(sIsNfaEnabled != true || sIsDisabling == true)
        goto TheEnd;

    if (last_screen_state_request != NFA_SCREEN_STATE_DEFAULT)
    {
        ALOGD("update last screen state request: %d", last_screen_state_request);
        nfcManager_doSetScreenState(NULL, NULL, last_screen_state_request);
        if( last_screen_state_request == NFA_SCREEN_STATE_LOCKED)
            screen_lock_flag = true;
    }
    else
    {
        ALOGD("No request pending");
    }

    if (last_request & ENABLE_DISCOVERY)
    {
        ALOGD("send the last request enable");
        sDiscoveryEnabled = false;
        sPollingEnabled = false;

        nfcManager_enableDiscovery(NULL, NULL, transaction_data.discovery_params.technologies_mask, transaction_data.discovery_params.enable_lptd,
                                         transaction_data.discovery_params.reader_mode, transaction_data.discovery_params.enable_p2p,
                                         transaction_data.discovery_params.restart);
    }

    if (last_request & DISABLE_DISCOVERY)
    {
        ALOGD("send the last request disable");
        nfcManager_disableDiscovery(NULL, NULL);
        disable_discovery = true;
    }
#if(NXP_EXTNS == TRUE)
    if (last_request & ENABLE_P2P)
    {
        ALOGD("send the last request to enable P2P ");
        nfcManager_Enablep2p(NULL, NULL, transaction_data.discovery_params.enable_p2p);
    }
#if(NXP_NFCC_HCE_F == TRUE)
    if(last_request & T3T_CONFIGURE)
    {
        ALOGD(" transaction_data.t3thandle %d ", transaction_data.t3thandle);
        if(transaction_data.t3thandle != NULL)
        {
           RoutingManager::getInstance().deregisterT3tIdentifier(transaction_data.t3thandle);
        }
        RoutingManager::getInstance().notifyT3tConfigure();
    }
#endif
    if(last_request & RE_ROUTING)
    {
        ALOGD(" transaction_data.isInstallRequest %d ", transaction_data.isInstallRequest);
        if(!transaction_data.isInstallRequest)
        {
            RoutingManager::getInstance().clearAidTable();
            nfcManager_doCommitRouting(NULL,NULL);
        }
        RoutingManager::getInstance().notifyReRoutingEntry();
    }
#endif
    if(screen_lock_flag && disable_discovery)
    {
        startRfDiscovery(true);
    }
    screen_lock_flag = false;
    disable_discovery = false;
    memset(&transaction_data, 0x00, sizeof(Transcation_Check_t));
#if(NFC_NXP_CHIP_TYPE != PN547C2)
    memset(&menableAGC_debug_t, 0x00, sizeof(enableAGC_debug_t));
#endif
TheEnd:
    ALOGD ("%s: exit", __FUNCTION__);
    pthread_exit(NULL);
    return NULL;
}
#if (JCOP_WA_ENABLE == FALSE)
/*******************************************************************************
**
** Function         sig_handler
**
** Description      This function is used to handle the different types of
**                  signal events.
**
** Returns          None
**
*******************************************************************************/
void sig_handler(int signo)
{
    switch (signo)
    {
        case SIGINT:
            ALOGE("received SIGINT\n");
            break;
        case SIGABRT:
            ALOGE("received SIGABRT\n");
#if((NXP_EXTNS == TRUE) && (NXP_NFCC_MW_RCVRY_BLK_FW_DNLD == TRUE))
            NFA_MW_Fwdnlwd_Recovery(true);
#endif
            NFA_HciW4eSETransaction_Complete(Wait);
            break;
        case SIGSEGV:
            ALOGE("received SIGSEGV\n");
            break;
        case SIGHUP:
            ALOGE("received SIGHUP\n");
            break;
    }
}
#endif

/*******************************************************************************
**
** Function         nfcManager_doGetSeInterface
**
** Description      This function is used to get the eSE Client interfaces.
**
** Returns          integer - Physical medium
**
*******************************************************************************/
static int nfcManager_doGetSeInterface(JNIEnv* /* e */, jobject /* o */, jint type)
{
    unsigned long num = 0;
    switch(type)
    {
    case LDR_SRVCE:
        if(GetNxpNumValue (NAME_NXP_P61_LS_DEFAULT_INTERFACE, (void*)&num, sizeof(num))==false)
        {
            ALOGD ("NAME_NXP_P61_LS_DEFAULT_INTERFACE not found");
            num = 1;
        }
        break;
    case JCOP_SRVCE:
        if(GetNxpNumValue (NAME_NXP_P61_JCOP_DEFAULT_INTERFACE, (void*)&num, sizeof(num))==false)
        {
            ALOGD ("NAME_NXP_P61_JCOP_DEFAULT_INTERFACE not found");
            num = 1;
        }
        break;
    case LTSM_SRVCE:
        if(GetNxpNumValue (NAME_NXP_P61_LTSM_DEFAULT_INTERFACE, (void*)&num, sizeof(num))==false)
        {
            ALOGD ("NAME_NXP_P61_LTSM_DEFAULT_INTERFACE not found");
            num = 1;
        }
        break;
    default:
        break;
    }
    ALOGD ("%ld: nfcManager_doGetSeInterface", num);
    return num;
}

#if(NXP_EXTNS == TRUE)
/**********************************************************************************
**
** Function:       pollT3TThread
**
** Description:    This thread sends commands to switch from P2P to T3T
**                 When ReaderMode is enabled, When P2P is detected,Switch to T3T
**                 with Frame RF interface and Poll for T3T
**
** Returns:         None.
**
**********************************************************************************/
static void* pollT3TThread(void *arg)
{
    ALOGD ("%s: enter", __FUNCTION__);
    bool status=false;

    if (sReaderModeEnabled && (sTechMask & NFA_TECHNOLOGY_MASK_F))
    {
     /*Deactivate RF to go to W4_HOST_SELECT state
          *Send Select Command to Switch to FrameRF interface from NFCDEP interface
          *After NFC-DEP activation with FrameRF Intf, invoke T3T Polling Cmd*/
        {
            SyncEventGuard g (sRespCbEvent);
            if (NFA_STATUS_OK != (status = NFA_Deactivate (TRUE))) //deactivate to sleep state
            {
                ALOGE ("%s: deactivate failed, status = %d", __FUNCTION__, status);
            }
            if (sRespCbEvent.wait (2000) == false) //if timeout occurred
            {
                ALOGE ("%s: timeout waiting for deactivate", __FUNCTION__);
            }
        }
        {
            SyncEventGuard g2 (sRespCbEvent);
            ALOGD ("Switching RF Interface from NFC-DEP to FrameRF for T3T\n");
            if (NFA_STATUS_OK != (status = NFA_Select (*((UINT8*)arg), NFA_PROTOCOL_T3T, NFA_INTERFACE_FRAME)))
            {
                ALOGE ("%s: NFA_Select failed, status = %d", __FUNCTION__, status);
            }
            if (sRespCbEvent.wait (2000) == false) //if timeout occured
            {
                ALOGE ("%s: timeout waiting for select", __FUNCTION__);
            }
        }
    }
    ALOGD ("%s: exit", __FUNCTION__);
    pthread_exit(NULL);
    return NULL;
}

/**********************************************************************************
**
** Function:       switchP2PToT3TRead
**
** Description:    Create a thread to change the RF interface by Deactivating to Sleep
**
** Returns:         None.
**
**********************************************************************************/
static bool switchP2PToT3TRead(UINT8 disc_id)
{
    pthread_t pollT3TThreadId;
    int irret = -1;
    ALOGD ("%s:entry", __FUNCTION__);
    felicaReader_Disc_id = disc_id;

    /* Transcation is done process the last request*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    irret = pthread_create(&pollT3TThreadId, &attr, pollT3TThread, (void*)&felicaReader_Disc_id);
    if(irret != 0)
    {
        ALOGE("Unable to create the thread");
    }
    pthread_attr_destroy(&attr);
    ALOGD ("%s:exit", __FUNCTION__);
    return irret;
}

static void NxpResponsePropCmd_Cb(UINT8 /* event */, UINT16 param_len, UINT8 *p_param)
{
    ALOGD("NxpResponsePropCmd_Cb Received length data = 0x%x status = 0x%x", param_len, p_param[3]);
    SyncEventGuard guard (sNfaNxpNtfEvent);
    sNfaNxpNtfEvent.notifyOne ();
}

/*******************************************************************************
**
** Function:        nfcManager_setProvisionMode
**
** Description:     set/reset provision mode
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_setProvisionMode(JNIEnv* e, jobject o, jboolean provisionMode)
{
    ALOGD ("Enter :%s  provisionMode = %d", __FUNCTION__,provisionMode);
    sProvisionMode = provisionMode;
    NFA_setProvisionMode(provisionMode);
    // When disabling provisioning mode, make sure configuration of routing table is also updated
    // this is required to make sure p2p is blocked during locked screen
    if ( !provisionMode )
    {
       RoutingManager::getInstance().commitRouting();
    }
}

/*******************************************************************************
 **
 ** Function:        isActivatedTypeF
 **
 ** Description:     Indicates whether the activation data indicates it is
 **                  TypeF technology.
 **
 ** Returns:         True if activated technology is TypeF.
 **
 *******************************************************************************/
static bool isActivatedTypeF(tNFA_ACTIVATED& activated)
{
    return ((NFC_DISCOVERY_TYPE_POLL_F == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_POLL_F_ACTIVE == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_F == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_F_ACTIVE == activated.activate_ntf.rf_tech_param.mode));
}

/**********************************************************************************
 **
 ** Function:        checkforNfceeBuffer
 **
 ** Description:    checking for the Nfcee Buffer (GetConfigs for SWP_INT_SESSION_ID (EA and EB))
 **
 ** Returns:         None .
 **
 **********************************************************************************/
void checkforNfceeBuffer()
{
    int i, count = 0;
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
/*    unsigned long uicc_active_state = 0;
    if(!GetNxpNumValue (NAME_NXP_DUAL_UICC_ENABLE, (void*)&uicc_active_state, sizeof(uicc_active_state)))
    {
        ALOGE ("NXP_DUAL_UICC_ENABLE Not found taking default value 0x00");
        uicc_active_state = 0x00;
    }*/
#endif

    for(i=4;i<12;i++)
    {
        if(sConfig[i] == 0xff)
            count++;
    }

    if(count >= 8)
    {
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
        /*If session ID received all 0xff for UICC and dual UICC feature is enabled then
         * clear the corresponding buffer (invalid session ID)
         * */
        if((sConfig[1] == 0xA0) && (sConfig[2] == 0xEA) &&
                (dualUiccInfo.dualUiccEnable == 0x01))
        {
            if(sSelectedUicc == 0x01)
            {
                memset(dualUiccInfo.sUicc1SessionId,0x00,sizeof(dualUiccInfo.sUicc1SessionId));
                dualUiccInfo.sUicc1SessionIdLen = 0;
            }
            else
            {
                memset(dualUiccInfo.sUicc2SessionId,0x00,sizeof(dualUiccInfo.sUicc2SessionId));
                dualUiccInfo.sUicc2SessionIdLen = 0;
            }
        }
#endif
        sNfceeConfigured = 1;
    }
    else
    {
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
        if((sConfig[1] == 0xA0) && (sConfig[2] == 0xEA) &&
                (dualUiccInfo.dualUiccEnable == 0x01))
        {
            sNfceeConfigured = getUiccSession();
        }
        else
#endif
            sNfceeConfigured = 0;
    }

    memset (sConfig, 0, sizeof (sConfig));

}
/**********************************************************************************
 **
 ** Function:        checkforNfceeConfig
 **
 ** Description:    checking for the Nfcee is configured or not (GetConfigs for SWP_INT_SESSION_ID (EA and EB))
 **
 ** Returns:         None .
 **
 **********************************************************************************/
void checkforNfceeConfig(UINT8 type)
{
    UINT8 uicc_flag = 0,ese_flag = 0;
    UINT8 uicc2_flag=0; /*For Dynamic Dual UICC*/
    unsigned long timeout_buff_val=0,check_cnt=0,retry_cnt=0;

    tNFA_STATUS status;
    tNFA_PMID param_ids_UICC[]                  = {0xA0, 0xEA};
    tNFA_PMID param_ids_eSE[]                   = {0xA0, 0xEB};
#if(NXP_UICC_CREATE_CONNECTIVITY_PIPE == TRUE)
    tNFA_PMID param_uicc1[] = {0xA0, 0x24};
#endif
#if(NXP_NFCC_DYNAMIC_DUAL_UICC == TRUE)
    tNFA_PMID param_ids_UICC2[]                 = {0xA0, 0x1E};
    tNFA_PMID param_uicc2[] = {0xA0, 0xE9};
#endif

    ALOGD ("%s: enter, type=%x", __FUNCTION__, type);
    UINT8 pipeId = 0;
    SecureElement::getInstance().updateEEStatus();
    bool configureuicc1 = false;
#if((NXP_NFCC_DYNAMIC_DUAL_UICC == TRUE) || (NXP_UICC_CREATE_CONNECTIVITY_PIPE == TRUE))
    bool configureuicc2 = false;
#endif

    status = GetNxpNumValue(NAME_NXP_DEFAULT_NFCEE_TIMEOUT, (void*)&timeout_buff_val, sizeof(timeout_buff_val));

    if(status == TRUE)
    {
        check_cnt = timeout_buff_val*RETRY_COUNT;
    }
    else
    {
        check_cnt = DEFAULT_COUNT*RETRY_COUNT;
    }

    ALOGD ("NAME_DEFAULT_NFCEE_TIMEOUT = %lu", check_cnt);

    if(SecureElement::getInstance().getEeStatus(ESE_HANDLE) == NFC_NFCEE_STATUS_ACTIVE)
    {
        ese_flag = 0x01;
        ALOGD("eSE_flag SET");
    }
    if(SecureElement::getInstance().getEeStatus(UICC_HANDLE) == NFC_NFCEE_STATUS_ACTIVE)
    {
        uicc_flag = 0x01;
        ALOGD("uicc_flag SET");
    }
#if(NXP_NFCC_DYNAMIC_DUAL_UICC == TRUE)
    if(SecureElement::getInstance().getEeStatus(UICC2_HANDLE) == NFC_NFCEE_STATUS_ACTIVE)
    {
        uicc2_flag = 0x01;
        ALOGD("uicc2_flag SET");
    }
#endif

#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
    else if (dualUiccInfo.dualUiccEnable == 0x01)
    {
        if(sSelectedUicc == 0x01)
        {
            memset(dualUiccInfo.sUicc1SessionId,0x00,sizeof(dualUiccInfo.sUicc1SessionId));
            dualUiccInfo.sUicc1SessionIdLen = 0;
        }
        else
        {
            memset(dualUiccInfo.sUicc2SessionId,0x00,sizeof(dualUiccInfo.sUicc2SessionId));
            dualUiccInfo.sUicc2SessionIdLen = 0;
        }
    }
#endif
    if((ese_flag == 0x01)||(uicc_flag == 0x01)||(uicc2_flag == 0x01))
    {
#if(NFC_NXP_ESE == TRUE)
        if(ese_flag && ((type & ESE) == ESE))
        {
            sCheckNfceeFlag = 1;
            {
                SyncEventGuard guard (android::sNfaGetConfigEvent);
                while(check_cnt > retry_cnt)
                {
                    status = NFA_GetConfig(0x01,param_ids_eSE);
                    if(status == NFA_STATUS_OK)
                    {
                        android::sNfaGetConfigEvent.wait();
                    }
                    if(sNfceeConfigured == 1)
                    {
                        SecureElement::getInstance().meSESessionIdOk = false;
                        ALOGD("eSE Not Configured");
                    }
                    else
                    {
                        SecureElement::getInstance().meSESessionIdOk = true;
                        ALOGD("eSE Configured");
                        break;
                    }

                    usleep(100000);
                    retry_cnt++;
                }
            }
            if(check_cnt <= retry_cnt)
                ALOGD("eSE Not Configured");
            retry_cnt=0;
        }
#endif

#if(NXP_NFCC_DYNAMIC_DUAL_UICC == TRUE)
        if(uicc2_flag && ((type & UICC2) == UICC2))
        {
            sCheckNfceeFlag = 1;
            {
                SyncEventGuard guard (android::sNfaGetConfigEvent);
                while(check_cnt > retry_cnt)
                {
                    status = NFA_GetConfig(0x01,param_ids_UICC2);
                    if(status == NFA_STATUS_OK)
                    {
                        android::sNfaGetConfigEvent.wait();
                    }

                    if(sNfceeConfigured == 1)
                    {
                        ALOGD("UICC2 Not Configured");
                    }
                    else
                    {
                    ALOGD("UICC2 Configured connectivity pipeId = %x",pipeId);
                    configureuicc2 = true;
#if(NXP_UICC_CREATE_CONNECTIVITY_PIPE != TRUE)
                    break;
#endif
                    }
#if(NXP_UICC_CREATE_CONNECTIVITY_PIPE == TRUE)
                if((configureuicc2 == true) || (check_cnt == retry_cnt))
                {
                    configureuicc2 = false;
                    pipeId =SecureElement::getInstance().getUiccGateAndPipeList(HOST_TYPE_UICC2);
                    if(pipeId == 0)
                    {
                        ALOGD ("Add pipe information");
                        sCheckNfceeFlag = 0;
                        status = NFA_GetConfig(0x01,param_uicc2);
                        pipeId = 0x23;

                        if(status == NFA_STATUS_OK)
                        {
                            android::sNfaGetConfigEvent.wait();
                        }
                        sCheckNfceeFlag = 1;
                        ALOGD("UICC2 connectivity gate present = %s", (sConfig[NFC_PIPE_STATUS_OFFSET]?"TRUE":"FALSE"));
                        /*If pipe is present and opened update MW status*/
                        if(sConfig[NFC_PIPE_STATUS_OFFSET] > PIPE_DELETED)
                        {
                            SyncEventGuard guard(SecureElement::getInstance().mHciAddStaticPipe);
                            status = NFA_HciAddStaticPipe(SecureElement::getInstance().getHciHandleInfo(),
                                0x81, NFA_HCI_CONNECTIVITY_GATE, pipeId);
                            if(status == NFA_STATUS_OK)
                            {
                                SecureElement::getInstance().mHciAddStaticPipe.wait(500);
                            }
                        }
                    }
                    break;
                }
#endif
                    usleep(100000);
                    retry_cnt++;
                }
            }

            if(check_cnt <= retry_cnt)
                ALOGD("UICC2 Not Configured");
            retry_cnt=0;
            pipeId = 0;
        }
#endif
        if(uicc_flag && ((type & UICC1) == UICC1))
        {
            sCheckNfceeFlag = 1;
            {
                SyncEventGuard guard (android::sNfaGetConfigEvent);
                while(check_cnt > retry_cnt)
                {
                    status = NFA_GetConfig(0x01,param_ids_UICC);
                    if(status == NFA_STATUS_OK)
                    {
                        android::sNfaGetConfigEvent.wait();
                    }

                    if(sNfceeConfigured == 1)
                    {
                        ALOGD("UICC Not Configured");
                    }
                    else
                    {
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
                        dualUiccInfo.uiccConfigStat = UICC_CONFIGURED;
#endif
                    ALOGD("UICC1 Configured connectivity pipeId = %x",pipeId);
                    configureuicc1 = true;
#if(NXP_UICC_CREATE_CONNECTIVITY_PIPE != TRUE)
                    break;
#endif
                    }
#if(NXP_UICC_CREATE_CONNECTIVITY_PIPE == TRUE)
                if((configureuicc1 == true) || (check_cnt == retry_cnt))
                {
                    configureuicc1 = false;
                    pipeId =SecureElement::getInstance().getUiccGateAndPipeList(HOST_TYPE_UICC1);
                    if(pipeId == 0)
                    {
                        ALOGD ("Add pipe information");
                        sCheckNfceeFlag = 0;
                        status = NFA_GetConfig(0x01,param_uicc1);
                        pipeId = 0x0A;
                        if(status == NFA_STATUS_OK)
                        {
                            android::sNfaGetConfigEvent.wait();
                        }
                        sCheckNfceeFlag = 1;
                        ALOGD("UICC1 connectivity gate present = %s", (sConfig[NFC_PIPE_STATUS_OFFSET]?"TRUE":"FALSE"));
                        /*If pipe is present and opened update MW status*/
                        if(sConfig[NFC_PIPE_STATUS_OFFSET] > PIPE_DELETED)
                        {
                            SyncEventGuard guard(SecureElement::getInstance().mHciAddStaticPipe);
                            status = NFA_HciAddStaticPipe(SecureElement::getInstance().getHciHandleInfo(),
                                0x02, NFA_HCI_CONNECTIVITY_GATE, pipeId);
                            if(status == NFA_STATUS_OK)
                            {
                                SecureElement::getInstance().mHciAddStaticPipe.wait();
                            }
                        }
                    }
                    break;
                }
#endif
                    usleep(100000);
                    retry_cnt++;
                }
            }

            if(check_cnt <= retry_cnt)
                ALOGE("UICC Not Configured");
            retry_cnt=0;
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
            sCheckNfceeFlag = 0;
#endif
        }
    }

    sCheckNfceeFlag = 0;

#if (JCOP_WA_ENABLE == TRUE)
    RoutingManager::getInstance().handleSERemovedNtf();
#endif

}
#endif

#if(NXP_EXTNS == TRUE)
static void nfaNxpSelfTestNtfTimerCb (union sigval)
{
    ALOGD ("%s", __FUNCTION__);
    ALOGD("NXP SWP SelfTest : Can't get a notification about SWP Status!!");
    SyncEventGuard guard (sNfaNxpNtfEvent);
    sNfaNxpNtfEvent.notifyOne ();
    SetCbStatus(NFA_STATUS_FAILED);
}

/**********************************************************************************
 **
 ** Function:        performNfceeETSI12Config
 **
 ** Description:    checking for Nfcee ETSI 12 Compliancy and configure if compliant
 **
 ** Returns:         None .
 **
 **********************************************************************************/
void performNfceeETSI12Config()
{
    bool status;
    tNFA_STATUS configstatus = NFA_STATUS_FAILED;
    ALOGD ("%s", __FUNCTION__);

    status = SecureElement::getInstance().configureNfceeETSI12();
    if(status == TRUE)
    {
        {
            SyncEventGuard guard (SecureElement::getInstance().mNfceeInitCbEvent);
            if(SecureElement::getInstance().mNfceeInitCbEvent.wait(4000) == false)
            {
                ALOGE ("%s:     timeout waiting for Nfcee Init event", __FUNCTION__);
            }
        }
        if(SecureElement::getInstance().mETSI12InitStatus != NFA_STATUS_OK)
        {
            //check for recovery
            configstatus = ResetEseSession();
            if(configstatus == NFA_STATUS_OK)
            {
                SecureElement::getInstance().meseETSI12Recovery = true;
                SecureElement::getInstance().SecEle_Modeset(0x00);
                usleep(50*1000);
                SecureElement::getInstance().SecEle_Modeset(0x01);
                SecureElement::getInstance().meseETSI12Recovery = false;
            }
        }
#if (NXP_WIRED_MODE_STANDBY == TRUE)
        SecureElement::getInstance().
            setNfccPwrConfig(SecureElement::getInstance().
                NFCC_DECIDES);
#endif
    }
}

/**********************************************************************************
 **
 ** Function:       performHCIInitialization
 **
 ** Description:    Performs HCI and SWP interface Initialization
 **
 ** Returns:         None .
 **
 **********************************************************************************/
static void performHCIInitialization (JNIEnv* e, jobject o)
{
    NFCSTATUS status = NFA_STATUS_FAILED;
    ALOGD ("%s", __FUNCTION__);
    GetNumNFCEEConfigured();
    status = android::enableSWPInterface();
    if(status == NFA_STATUS_OK)
    {
        /*Update Actual SE count gActualSeCount*/
        GetNumNFCEEConfigured();
        RoutingManager::getInstance().nfaEEConnect();
        sIsSecElemSelected = (SecureElement::getInstance().getActualNumEe() - 1 );
        sIsSecElemDetected = sIsSecElemSelected;
    }
    else
    {
        ALOGE("No UICC update required/failed to enable SWP interfaces");
    }
}
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
/**********************************************************************************
 **
 ** Function:        getUiccContext
 **
 ** Description:     Read and store UICC context values
 **                  Respective context will be applied during next switching
 **
 ** Returns:         None
 **
 **********************************************************************************/
static void getUiccContext(int uiccSlot)
{
    UINT8 i;
    tNFA_STATUS status;
    tNFA_PMID param_ids_UICC_getContext[]       = {0xA0, 0xF4};

    ALOGD ("%s: Enter", __FUNCTION__);

    SyncEventGuard guard (android::sNfaGetConfigEvent);
    status = NFA_GetConfig(0x01,param_ids_UICC_getContext);
    if(status == NFA_STATUS_OK)
    {
        android::sNfaGetConfigEvent.wait();
    }

    ALOGD ("%s: UICC context Info : Len = %x", __FUNCTION__,sCurrentConfigLen);
    /*If the session ID is changed or uicc changed*/

    if((dualUiccInfo.sUicc1CntxLen != 0)&&(sSelectedUicc == 0x01))
    {
        for(i= 0 ; i < dualUiccInfo.sUicc1CntxLen; i++)
        {
            if(sConfig[i] != dualUiccInfo.sUicc1Cntx[i])
                break;
        }
        if(i != dualUiccInfo.sUicc1CntxLen)
        {
            ALOGD ("%s: copying UICC1 info", __FUNCTION__);
            update_uicc_context_info();
        }
    }
    /*If the session ID is changed or uicc changed*/
    if((dualUiccInfo.sUicc2CntxLen != 0)&&(sSelectedUicc == 0x02))
    {
        for(i= 0 ; i < dualUiccInfo.sUicc2CntxLen; i++)
        {
            if(sConfig[i] != dualUiccInfo.sUicc2Cntx[i])
                break;
        }
        if(i != dualUiccInfo.sUicc1CntxLen)
        {
            ALOGD ("%s: copying UICC2 info", __FUNCTION__);
            update_uicc_context_info();
        }
    }

    /*For the first power cycle for uicc1*/
    if((dualUiccInfo.sUicc1CntxLen == 0)&&(sSelectedUicc == 0x01))
    {
        ALOGD ("%s:  power cycle storing UICC1 info",__FUNCTION__);
        dualUiccInfo.sUicc1CntxLen = sCurrentConfigLen;
        for(i= 5 ; i < 13; i++)
        {
            if(sConfig[i] != (UINT8)0xFF)
                break;
        }
        if(i == 13)
        {
            dualUiccInfo.sUicc1CntxLen = 0;
        }
        else
        {
            ALOGD ("%s: copying UICC1 info", __FUNCTION__);
            update_uicc_context_info();
        }
    }
    /*For the first power cycle for uicc2*/
    else if((dualUiccInfo.sUicc2CntxLen == 0)&&(sSelectedUicc == 0x02))
    {
        ALOGD ("%s:  power cycle storing UICC2 info",__FUNCTION__);
        dualUiccInfo.sUicc2CntxLen = sCurrentConfigLen;
        for(i= 5 ; i < 13; i++)
        {
            if(sConfig[i] != (UINT8)0xFF)
                break;
        }
        if(i == 13)
        {
            dualUiccInfo.sUicc2CntxLen = 0;
        }
        else
        {
            ALOGD ("%s: copying UICC2 info", __FUNCTION__);
            update_uicc_context_info();
        }
    }
    else
    {
        ALOGD ("%s: UICC info are already stored..",__FUNCTION__);
    }

    if((uiccSlot == 0x01)&&(dualUiccInfo.sUicc1CntxLen == 0x00))
    {
        read_uicc_context(dualUiccInfo.sUicc1Cntx, dualUiccInfo.sUicc1CntxLen,
                dualUiccInfo.sUicc1TechCapblty, sizeof(dualUiccInfo.sUicc1TechCapblty), 1, uiccSlot);
    }
    else if((uiccSlot == 0x02)&&(dualUiccInfo.sUicc2CntxLen == 0x00))
    {
        read_uicc_context(dualUiccInfo.sUicc2Cntx, dualUiccInfo.sUicc2CntxLen,
                dualUiccInfo.sUicc2TechCapblty, sizeof(dualUiccInfo.sUicc2TechCapblty), 1, uiccSlot);
    }

    ALOGD ("%s: Exit", __FUNCTION__);
}

/**********************************************************************************
 **
 ** Function:        update_uicc_context_info
 **
 ** Description:     updates UICC context related info to buffere and file
 **
 ** Returns:         none
 **
 **********************************************************************************/
static void update_uicc_context_info()
{
    ALOGD ("%s: Enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
    tNFA_PMID param_ids_UICC_getOtherContext[]  = {0xA0, 0xF5};
    if(sSelectedUicc == 0x01)
    {
        memcpy(dualUiccInfo.sUicc1Cntx, sConfig, sCurrentConfigLen);
        status = NFA_GetConfig(0x01,param_ids_UICC_getOtherContext);
        if(status == NFA_STATUS_OK)
        {
            android::sNfaGetConfigEvent.wait();
        }
        memcpy(dualUiccInfo.sUicc1TechCapblty, sConfig, sCurrentConfigLen);
        write_uicc_context(dualUiccInfo.sUicc1Cntx,  dualUiccInfo.sUicc1CntxLen, dualUiccInfo.sUicc1TechCapblty, 10, 1, sSelectedUicc);
    }
    else if(sSelectedUicc == 0x02)
    {
        memcpy(dualUiccInfo.sUicc2Cntx, sConfig, sCurrentConfigLen);
        status = NFA_GetConfig(0x01,param_ids_UICC_getOtherContext);
        if(status == NFA_STATUS_OK)
        {
            android::sNfaGetConfigEvent.wait();
        }
        memcpy(dualUiccInfo.sUicc2TechCapblty, sConfig, sCurrentConfigLen);
        write_uicc_context(dualUiccInfo.sUicc2Cntx,  dualUiccInfo.sUicc2CntxLen, dualUiccInfo.sUicc2TechCapblty, 10, 1, sSelectedUicc);

    }
    ALOGD ("%s: Exit", __FUNCTION__);
}

/**********************************************************************************
 **
 ** Function:        write_uicc_context
 **
 ** Description:     write UICC context to file
 **
 ** Returns:         none
 **
 **********************************************************************************/
void write_uicc_context(UINT8 *uiccContext, UINT16 uiccContextLen, UINT8 *uiccTechCap, UINT16 uiccTechCapLen, UINT8 block, uint8_t slotnum)
{
    char filename[256], filename2[256];
    UINT8   cntx_len = 128;
    UINT8   techCap = 10;
    UINT8*  frameByte;
    UINT16  crcVal = 0;
    ALOGD("%s : enter", __FUNCTION__);

    memset (filename, 0, sizeof(filename));
    memset (filename2, 0, sizeof(filename2));
    strcpy(filename2, "/data/nfc");
    strncat(filename2, "/nxpStorage.bin", sizeof(filename2)-strlen(filename2)-1);

    if (strlen(filename2) > 200)
    {
        ALOGE ("%s: filename too long", __FUNCTION__);
        return;
    }
    sprintf (filename, "%s%u", filename2, block);
    ALOGD ("%s: bytes=%u; file=%s slotnum=%d", __FUNCTION__, uiccContextLen, filename, slotnum);

    int fileStream = 0;

    fileStream = open (filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fileStream >= 0)
    {
        size_t actualWrittenCntx = 0;
        size_t actualWrittenCrc = 0;
        size_t actualWrittenTechCap = 0;
        size_t actualWrittenCntxLen = 0;

        if(slotnum == 1)
        {
            lseek(fileStream, 0, SEEK_SET);
        }
        else if(slotnum == 2)
        {
            lseek(fileStream, sizeof(dualUiccInfo.sUicc1Cntx)+sizeof(dualUiccInfo.sUicc1TechCapblty), SEEK_SET);
        }

        actualWrittenCntxLen = write(fileStream, &uiccContextLen, 1);
        if(uiccContextLen > 0x00)
        {
            cntx_len = uiccContextLen;
            techCap  = uiccTechCapLen;
            crcVal   = calc_crc16(uiccContext,cntx_len);
        }

        frameByte = (UINT8 *)&crcVal;
        ALOGD ("%s:CRC calculated %02x %02x", __FUNCTION__, frameByte[0],frameByte[1]);

        actualWrittenCntx = write (fileStream, uiccContext, cntx_len);
        actualWrittenCrc  = write (fileStream, frameByte, sizeof(crcVal));
        actualWrittenTechCap = write (fileStream, uiccTechCap, techCap);

        ALOGD ("%s: %zu bytes written", __FUNCTION__, cntx_len);
        if ((actualWrittenCntx == cntx_len) && (actualWrittenTechCap == techCap))
        {
            ALOGD("Write Success!");
        }
        else
        {
            ALOGE ("%s: fail to write", __FUNCTION__);
        }
        close (fileStream);
    }
    else
    {
        ALOGE ("%s: fail to open, error = %d", __FUNCTION__, errno);
    }
    ALOGD("%s : exit", __FUNCTION__);
}

/**********************************************************************************
 **
 ** Function:        read_uicc_context
 **
 ** Description:     read UICC context from file
 **
 ** Returns:         none
 **
 **********************************************************************************/
void read_uicc_context(UINT8 *uiccContext, UINT16 uiccContextLen, UINT8 *uiccTechCap, UINT16 uiccTechCapLen, UINT8 block, uint8_t slotnum)
{
    char filename[256], filename2[256];
    UINT8*  readCrc = NULL;
    UINT8*  frameByte = NULL;
    UINT16  crcVal;
    UINT8   cmpStat;
    ALOGD("%s : enter", __FUNCTION__);

    memset (filename, 0, sizeof(filename));
    memset (filename2, 0, sizeof(filename2));
    strcpy(filename2, "/data/nfc");
    strncat(filename2, "/nxpStorage.bin", sizeof(filename2)-strlen(filename2)-1);
    if (strlen(filename2) > 200)
    {
        ALOGE ("%s: filename too long", __FUNCTION__);
        return;
    }
    sprintf (filename, "%s%u", filename2, block);

    ALOGD ("%s: buffer len=%u; file=%s, slotnum=%d", __FUNCTION__, uiccContextLen, filename, slotnum);
    int fileStream = open (filename, O_RDONLY);
    if (fileStream >= 0)
    {
        size_t actualReadCntx = 0;
        size_t actualReadCntxLen = 0;
        size_t actualReadCrc = 0;
        size_t actualReadTechCap = 0;
        uint8_t readCntxLen = 0;

        if(slotnum == 1)
        {
            lseek(fileStream, 0, SEEK_SET);
        }
        else if(slotnum == 2)
        {
            lseek(fileStream, sizeof(dualUiccInfo.sUicc1Cntx)+sizeof(dualUiccInfo.sUicc1TechCapblty), SEEK_SET);
        }
        actualReadCntxLen = read(fileStream, &readCntxLen, 1);
        if(readCntxLen > 0x00)
        {
            actualReadCntx      = read (fileStream, uiccContext, readCntxLen);
            readCrc = (UINT8*) malloc(2*sizeof(UINT8));
            actualReadCrc       = read (fileStream, readCrc, sizeof(crcVal));
            crcVal   = calc_crc16(uiccContext,readCntxLen);
            frameByte = (UINT8 *)&crcVal;
            actualReadTechCap   = read (fileStream, uiccTechCap, uiccTechCapLen);

            ALOGD ("%s:CRC calculated %02x %02x -- CRC read %02x %02x", __FUNCTION__, frameByte[0],frameByte[1],readCrc[0],readCrc[1]);
            cmpStat             = memcmp (readCrc, frameByte, sizeof(crcVal));
            if(cmpStat == 0)
            {
                ALOGD ("%s:CRC check result - success",__FUNCTION__);
            }
            else
            {
                ALOGD ("%s:CRC check result - failed. Resetting buffer",__FUNCTION__);
                memset(uiccContext,0x00,128);
                memset(uiccTechCap,0x00,10);
                write_uicc_context(uiccContext,  0, uiccTechCap, 10, 1, slotnum);
            }
            free(readCrc);
        }
        else
        {
            memset(uiccContext,0x00,128);
            memset(uiccTechCap,0x00,10);
        }

        if(slotnum == 1)      dualUiccInfo.sUicc1CntxLen = readCntxLen;
        else if(slotnum == 2) dualUiccInfo.sUicc2CntxLen = readCntxLen;

        close (fileStream);
        if (actualReadCntx > 0)
        {
            ALOGD ("%s: data size=%zu", __FUNCTION__, actualReadCntx);
        }
        else
        {
            ALOGE ("%s: fail to read", __FUNCTION__);
        }
    }
    else
    {
        ALOGD ("%s: fail to open", __FUNCTION__);
    }
    ALOGD("%s : exit", __FUNCTION__);
}

/*******************************************************************************
**
** Function         calc_crc16
**
** Description      Calculates CRC16 for the frame buffer
**
** Parameters       pBuff - CRC16 calculation input buffer
**                  wLen  - input buffer length
**
** Returns          wCrc  - computed 2 byte CRC16 value
**
*******************************************************************************/
UINT16 calc_crc16(UINT8* pBuff, UINT16 wLen)
{
    UINT16 wTmp;
    UINT16 wValue;
    UINT16 wCrc = 0xffff;
    UINT32 i;
    UINT16 aCrcTab[256] = {
            0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad,
            0xe1ce, 0xf1ef, 0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6, 0x9339, 0x8318, 0xb37b,
            0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de, 0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
            0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d, 0x3653, 0x2672, 0x1611, 0x0630, 0x76d7,
            0x66f6, 0x5695, 0x46b4, 0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc, 0x48c4, 0x58e5,
            0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823, 0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a,
            0xb92b, 0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e,
            0x9b79, 0x8b58, 0xbb3b, 0xab1a, 0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae,
            0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49, 0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32,
            0x1e51, 0x0e70, 0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78, 0x9188, 0x81a9, 0xb1ca,
            0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f, 0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
            0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e, 0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235,
            0x5214, 0x6277, 0x7256, 0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d, 0x34e2, 0x24c3,
            0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d,
            0xd73c, 0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634, 0xd94c, 0xc96d, 0xf90e, 0xe92f,
            0x99c8, 0x89e9, 0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3, 0xcb7d,
            0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a, 0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0,
            0x2ab3, 0x3a92, 0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07, 0x5c64,
            0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1, 0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
            0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0 };

    if((NULL == pBuff) || (0 == wLen))
    {
        ALOGD ("%s: Invalid Params supplied", __FUNCTION__);
    }
    else
    {
        /* Perform CRC calculation according to ccitt with a initial value of 0x1d0f */
        for (i = 0; i < wLen; i++)
        {
            wValue = 0x00ffU & (UINT16) pBuff[i];
            wTmp = (wCrc >> 8U) ^ wValue;
            wCrc = (wCrc << 8U) ^ aCrcTab[wTmp];
        }
    }

    return wCrc;
}

/**********************************************************************************
 **
 ** Function:        getUiccSession
 **
 ** Description:     Read and store UICC session values
 **
 ** Returns:         UICC Configured status
 **                  1 : failed
 **                  0 : success
 **
 **********************************************************************************/
static int getUiccSession()
{
    ALOGD ("%s: Enter", __FUNCTION__);

    int cmpStat = 0, sUiccConfigured = 1;
    /*techInfo will be set if any DISCOVERY_REQ_NTF is received for current UICC
     *It will be used to validate received session id belongs to current selected UICC or not
     * */
    bool techInfo = SecureElement::getInstance().isTeckInfoReceived (UICC_HANDLE);
    ALOGD ("%s: techInfo 0x%02x", __FUNCTION__,techInfo);

    /* sConfig will have session ID received
     * If received different from previous UICC save it in corresponding UICC buffer
     * If same, reset the UICC buffer
     * */
    if(sSelectedUicc == 0x01)
    {
        if(dualUiccInfo.sUicc2SessionIdLen != 0)
        {
            cmpStat = memcmp (sConfig + 4, dualUiccInfo.sUicc2SessionId, dualUiccInfo.sUicc2SessionIdLen);
            if((cmpStat == 0)||(!techInfo))
            {
                memset(dualUiccInfo.sUicc1SessionId,0x00,sizeof(dualUiccInfo.sUicc1SessionId));
                dualUiccInfo.sUicc1SessionIdLen = 0;
                sUiccConfigured = 1;
            }
            else
            {
                memcpy(dualUiccInfo.sUicc1SessionId, sConfig+4, 8);
                dualUiccInfo.sUicc1SessionIdLen = 8;
                sUiccConfigured = 0;
            }
        }
        else if(techInfo)
        {
            memcpy(dualUiccInfo.sUicc1SessionId, sConfig+4, 8);
            dualUiccInfo.sUicc1SessionIdLen = 8;
            sUiccConfigured = 0;
        }
    }
    else if(sSelectedUicc == 0x02)
    {
        if(dualUiccInfo.sUicc1SessionIdLen != 0)
        {
            cmpStat = memcmp (sConfig + 4, dualUiccInfo.sUicc1SessionId, dualUiccInfo.sUicc1SessionIdLen);
            if((cmpStat == 0)||(!techInfo))
            {
                memset(dualUiccInfo.sUicc2SessionId,0x00,sizeof(dualUiccInfo.sUicc2SessionId));
                dualUiccInfo.sUicc2SessionIdLen = 0;
                sUiccConfigured = 1;
            }
            else
            {
                memcpy(dualUiccInfo.sUicc2SessionId, sConfig+4, 8);
                dualUiccInfo.sUicc2SessionIdLen = 8;
                sUiccConfigured = 0;
            }
        }
        else if(techInfo)
        {
            memcpy(dualUiccInfo.sUicc2SessionId, sConfig+4, 8);
            dualUiccInfo.sUicc2SessionIdLen = 8;
            sUiccConfigured = 0;
        }
    }
    return sUiccConfigured;
}
/**********************************************************************************
 **
 ** Function:        notifyUiccEvent
 **
 ** Description:     Notifies UICC event sto Service
 **                  Possible values:
 **                  UICC_CONNECTED_0 - 0 UICC connected
 **                  UICC_CONNECTED_1 - 1 UICC connected
 **                  UICC_CONNECTED_2 - 2 UICCs connected
 **
 ** Returns:         None
 **
 **********************************************************************************/
static void notifyUiccEvent (union sigval)
{
    ALOGD ("%s", __FUNCTION__);
    struct nfc_jni_native_data *nat = getNative(NULL, NULL);
    JNIEnv* e;
    ScopedAttach attach(nat->vm, &e);
    if (e == NULL)
    {
        ALOGE ("jni env is null");
        return;
    }
    if(dualUiccInfo.uiccActivStat == 0x00) /*No UICC Detected*/
    {
        e->CallVoidMethod (nat->manager, android::gCachedNfcManagerNotifyUiccStatusEvent, UICC_CONNECTED_0);
    }
    else if((dualUiccInfo.uiccActivStat == 0x01)||(dualUiccInfo.uiccActivStat == 0x02)) /*One UICC Detected*/
    {
        e->CallVoidMethod (nat->manager, android::gCachedNfcManagerNotifyUiccStatusEvent, UICC_CONNECTED_1);
    }
    else if(dualUiccInfo.uiccActivStat == 0x03) /*Two UICC Detected*/
    {
        e->CallVoidMethod (nat->manager, android::gCachedNfcManagerNotifyUiccStatusEvent, UICC_CONNECTED_2);
    }

}
#endif

#if((NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE) || (NFC_NXP_STAT_DUAL_UICC_WO_EXT_SWITCH == TRUE))
static int nfcManager_staticDualUicc_Precondition(int uiccSlot)
{

#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
    unsigned long uicc_active_state = 0;
#endif

    uint8_t retStat = UICC_NOT_CONFIGURED;
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
    if(GetNxpNumValue (NAME_NXP_DUAL_UICC_ENABLE, (void*)&uicc_active_state, sizeof(uicc_active_state)))
    {
        ALOGD ("NXP_DUAL_UICC_ENABLE  : 0x%02lx",uicc_active_state);
    }
    else
    {
        ALOGE ("NXP_DUAL_UICC_ENABLE Not found taking default value 0x00");
        uicc_active_state = 0x00;
    }

    if(uicc_active_state != 0x01)
    {
        ALOGE("%s:FAIL Dual UICC feature not available", __FUNCTION__);
        retStat = DUAL_UICC_FEATURE_NOT_AVAILABLE;
    }
    else
#endif
    if(sIsDisabling)
    {
        ALOGE("%s:FAIL Nfc is Disabling : Switch UICC not allowed", __FUNCTION__);
        retStat = DUAL_UICC_ERROR_NFC_TURNING_OFF;
    }
    else if(SecureElement::getInstance().isBusy())
    {
        ALOGE("%s:FAIL  SE wired-mode : busy", __FUNCTION__);
        retStat = DUAL_UICC_ERROR_NFCC_BUSY;
    }
    else if(rfActivation)
    {
        ALOGE("%s:FAIL  RF session ongoing", __FUNCTION__);
        retStat = DUAL_UICC_ERROR_NFCC_BUSY;
    }
    else if((uiccSlot != 0x01) && (uiccSlot != 0x02))
    {
        ALOGE("%s: Invalid slot id", __FUNCTION__);
        retStat = DUAL_UICC_ERROR_INVALID_SLOT;
    }
    else if(SecureElement::getInstance().isRfFieldOn())
    {
        ALOGE("%s:FAIL  RF field on", __FUNCTION__);
        retStat = DUAL_UICC_ERROR_NFCC_BUSY;
    }
    else if(sDiscoveryEnabled || sRfEnabled)
    {
        if(!update_transaction_stat("staticDualUicc",SET_TRANSACTION_STATE))
        {
            ALOGE("%s: Transaction in progress. Can not set", __FUNCTION__);
            retStat = DUAL_UICC_ERROR_NFCC_BUSY;
        }
        else
        {
            ALOGE ("%s: Transaction state enabled", __FUNCTION__);
        }

    }
    return retStat;
}
#endif

static void nfaNxpSelfTestNtfCallback(UINT8 event, UINT16 param_len, UINT8 *p_param)
{
    (void)event;
    ALOGD ("%s", __FUNCTION__);

    if(param_len == 0x05 && p_param[3] == 00) //p_param[4]  0x00:SWP Link OK 0x03:SWP link dead.
    {
        ALOGD("NXP SWP SelfTest : SWP Link OK ");
        SetCbStatus(NFA_STATUS_OK);
    }
    else
    {
        if(p_param[3] == 0x03) ALOGD("NXP SWP SelfTest : SWP Link dead ");
        SetCbStatus(NFA_STATUS_FAILED);
    }

    switch(p_param[4]){ //information of PMUVCC.
        case 0x00 : ALOGD("NXP SWP SelfTest : No PMUVCC ");break;
        case 0x01 : ALOGD("NXP SWP SelfTest : PMUVCC = 1.8V ");break;
        case 0x02 : ALOGD("NXP SWP SelfTest : PMUVCC = 3.3V ");break;
        case 0x03 : ALOGD("NXP SWP SelfTest : PMUVCC = undetermined ");break;
        default   : ALOGD("NXP SWP SelfTest : unknown PMUVCC ");break;
    }

    SyncEventGuard guard (sNfaNxpNtfEvent);
    sNfaNxpNtfEvent.notifyOne ();
}

static void nfcManager_doPrbsOn(JNIEnv* e, jobject o, jint prbs, jint hw_prbs, jint tech, jint rate)
{
    (void)e;
    (void)o;
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
//    bool stat = false;                    /*commented to eliminate unused variable warning*/

    if (!sIsNfaEnabled) {
        ALOGD("NFC does not enabled!!");
        return;
    }

    if (sDiscoveryEnabled) {
        ALOGD("Discovery must not be enabled for SelfTest");
        return ;
    }

    if(tech < 0 || tech > 2)
    {
        ALOGD("Invalid tech! please choose A or B or F");
        return;
    }

    if(rate < 0 || rate > 3){
        ALOGD("Invalid bitrate! please choose 106 or 212 or 424 or 848");
        return;
    }

    //Technology to stream          0x00:TypeA 0x01:TypeB 0x02:TypeF
    //Bitrate                       0x00:106kbps 0x01:212kbps 0x02:424kbps 0x03:848kbps
    //prbs and hw_prbs              0x00 or 0x01 two extra parameters included in case of pn548AD
#if(NFC_NXP_CHIP_TYPE != PN547C2)
    UINT8 param[4];
    memset(param, 0x00, sizeof(param));
    param[0] = prbs;
    param[1] = hw_prbs;
    param[2] = tech;    //technology
    param[3] = rate;    //bitrate
    ALOGD("phNxpNciHal_getPrbsCmd: PRBS = %d  HW_PRBS = %d", prbs, hw_prbs);
#else
    UINT8 param[2];
    memset(param, 0x00, sizeof(param));
    param[0] = tech;
    param[1] = rate;
#endif
    switch (tech)
    {
        case 0x00:
             ALOGD("phNxpNciHal_getPrbsCmd - NFC_RF_TECHNOLOGY_A");
             break;
        case 0x01:
             ALOGD("phNxpNciHal_getPrbsCmd - NFC_RF_TECHNOLOGY_B");
             break;
        case 0x02:
             ALOGD("phNxpNciHal_getPrbsCmd - NFC_RF_TECHNOLOGY_F");
             break;
        default:
             break;
    }
    switch (rate)
    {
        case 0x00:
             ALOGD("phNxpNciHal_getPrbsCmd - NFC_BIT_RATE_106");
             break;
        case 0x01:
             ALOGD("phNxpNciHal_getPrbsCmd - NFC_BIT_RATE_212");
             break;
        case 0x02:
             ALOGD("phNxpNciHal_getPrbsCmd - NFC_BIT_RATE_424");
             break;
        case 0x03:
             ALOGD("phNxpNciHal_getPrbsCmd - NFC_BIT_RATE_848");
             break;
        default:
             break;
    }
    //step2. PRBS Test stop : CORE RESET_CMD
    status = Nxp_SelfTest(3, param);   //CORE_RESET_CMD
    if(NFA_STATUS_OK != status)
    {
        ALOGD("%s: CORE RESET_CMD Fail!", __FUNCTION__);
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }
    //step3. PRBS Test stop : CORE_INIT_CMD
    status = Nxp_SelfTest(4, param);   //CORE_INIT_CMD
    if(NFA_STATUS_OK != status)
    {
        ALOGD("%s: CORE_INIT_CMD Fail!", __FUNCTION__);
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }
    //step4. : NXP_ACT_PROP_EXTN
    status = Nxp_SelfTest(5, param);   //NXP_ACT_PROP_EXTN
    if(NFA_STATUS_OK != status)
    {
        ALOGD("%s: NXP_ACT_PROP_EXTN Fail!", __FUNCTION__);
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    status = Nxp_SelfTest(1, param);
    ALOGD ("%s: exit; status =0x%X", __FUNCTION__,status);

    TheEnd:
        //Factory Test Code
        ALOGD ("%s: exit; status =0x%X", __FUNCTION__,status);
    return;
}

static void nfcManager_doPrbsOff(JNIEnv* e, jobject o)
{
    (void)e;
    (void)o;
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
//    bool stat = false;                    /*commented to eliminate unused variable warning*/
    UINT8 param;

    if (!sIsNfaEnabled) {
        ALOGD("NFC does not enabled!!");
        return;
    }

    if (sDiscoveryEnabled) {
        ALOGD("Discovery must not be enabled for SelfTest");
        return;
    }

    //Factory Test Code
    //step1. PRBS Test stop : VEN RESET
    status = Nxp_SelfTest(2, &param);   //VEN RESET
    if(NFA_STATUS_OK != status)
    {
        ALOGD("step1. PRBS Test stop : VEN RESET Fail!");
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    TheEnd:
    //Factory Test Code
    ALOGD ("%s: exit; status =0x%X", __FUNCTION__,status);

    return;
}

static jint nfcManager_SWPSelfTest(JNIEnv* e, jobject o, jint ch)
{
    (void)e;
    (void)o;
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
    tNFA_STATUS regcb_stat = NFA_STATUS_FAILED;
    UINT8 param[1];

    if (!sIsNfaEnabled) {
        ALOGD("NFC does not enabled!!");
        return status;
    }

    if (sDiscoveryEnabled) {
        ALOGD("Discovery must not be enabled for SelfTest");
        return status;
    }

    if (ch < 0 || ch > 1){
        ALOGD("Invalid channel!! please choose 0 or 1");
        return status;
    }


    //step1.  : CORE RESET_CMD
    status = Nxp_SelfTest(3, param);   //CORE_RESET_CMD
    if(NFA_STATUS_OK != status)
    {
        ALOGD("step2. PRBS Test stop : CORE RESET_CMD Fail!");
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    //step2. : CORE_INIT_CMD
    status = Nxp_SelfTest(4, param);   //CORE_INIT_CMD
    if(NFA_STATUS_OK != status)
    {
        ALOGD("step3. PRBS Test stop : CORE_INIT_CMD Fail!");
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    //step3. : NXP_ACT_PROP_EXTN
    status = Nxp_SelfTest(5, param);   //NXP_ACT_PROP_EXTN
    if(NFA_STATUS_OK != status)
    {
        ALOGD("step: NXP_ACT_PROP_EXTN Fail!");
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    regcb_stat = NFA_RegVSCback (true,nfaNxpSelfTestNtfCallback); //Register CallBack for NXP NTF
    if(NFA_STATUS_OK != regcb_stat)
    {
        ALOGD("To Regist Ntf Callback is Fail!");
        goto TheEnd;
    }

    param[0] = ch; // SWP channel 0x00 : SWP1(UICC) 0x01:SWP2(eSE)
    status = Nxp_SelfTest(0, param);
    if(NFA_STATUS_OK != status)
    {
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    {
        ALOGD("NFC NXP SelfTest wait for Notificaiton");
        nfaNxpSelfTestNtfTimer.set(1000, nfaNxpSelfTestNtfTimerCb);
        SyncEventGuard guard (sNfaNxpNtfEvent);
        sNfaNxpNtfEvent.wait(); //wait for NXP Self NTF to come
    }

    status = GetCbStatus();
    if(NFA_STATUS_OK != status)
    {
        status = NFA_STATUS_FAILED;
    }

    TheEnd:
    if(NFA_STATUS_OK == regcb_stat) {
        regcb_stat = NFA_RegVSCback (false,nfaNxpSelfTestNtfCallback); //DeRegister CallBack for NXP NTF
    }
    nfaNxpSelfTestNtfTimer.kill();
    ALOGD ("%s: exit; status =0x%X", __FUNCTION__,status);
    return status;
}

#if 0
/*******************************************************************************
 **
 ** Function:       nfcManager_doPartialInitialize
 **
 ** Description:    Initializes the NFC partially if it is not initialized.
 **                 This will be required  for transceive  during NFC off.
 **
 **
 ** Returns:        true/false .
 **
 *******************************************************************************/
static bool nfcManager_doPartialInitialize ()
{

    ALOGD("%s enter", __FUNCTION__);
    tNFA_STATUS stat = NFA_STATUS_OK;
    if (sIsNfaEnabled || gsNfaPartialEnabled)
    {
        ALOGD ("%s: NFC already enabled", __FUNCTION__);
        return true;
    }
    NfcAdaptation& theInstance = NfcAdaptation::GetInstance();
    theInstance.MinInitialize();

    tHAL_NFC_ENTRY* halFuncEntries = theInstance.GetHalEntryFuncs ();
    ALOGD("%s: calling nfa init", __FUNCTION__);
    if(NULL == halFuncEntries)
    {
        theInstance.Finalize();
        gsNfaPartialEnabled = false;
        return false;
    }

    NFA_SetBootMode(NFA_FAST_BOOT_MODE);
    NFA_Init (halFuncEntries);
    ALOGD("%s: calling enable", __FUNCTION__);
    stat = NFA_Enable (nfaDeviceManagementCallback, nfaConnectionCallback);
    if (stat == NFA_STATUS_OK)
    {
        SyncEventGuard guard (sNfaEnableEvent);
        sNfaEnableEvent.wait(); //wait for NFA command to finish
    }

    if (sIsNfaEnabled)
    {
        gsNfaPartialEnabled = true;
        sIsNfaEnabled = false;
    }
    else
    {
        NFA_Disable (FALSE /* ungraceful */);
        theInstance.Finalize();
        gsNfaPartialEnabled = false;
    }

    ALOGD("%s exit status = 0x%x",  __FUNCTION__ ,gsNfaPartialEnabled);
    return gsNfaPartialEnabled;
}
#endif
/*******************************************************************************
 **
 ** Function:       nfcManager_doPartialDeInitialize
 **
 ** Description:    DeInitializes the NFC partially if it is partially initialized.
 **
 ** Returns:        true/false .
 **
 *******************************************************************************/
static bool nfcManager_doPartialDeInitialize()
{
    tNFA_STATUS stat = NFA_STATUS_OK;
    if(!gsNfaPartialEnabled)
    {
        ALOGD ("%s: cannot deinitialize NFC , not partially initilaized", __FUNCTION__);
        return true;
    }
    ALOGD ("%s:enter", __FUNCTION__);
    stat = NFA_Disable (TRUE /* graceful */);
    if (stat == NFA_STATUS_OK)
    {
        ALOGD ("%s: wait for completion", __FUNCTION__);
        SyncEventGuard guard (sNfaDisableEvent);
        sNfaDisableEvent.wait (); //wait for NFA command to finish
    }
    else
    {
        ALOGE ("%s: fail disable; error=0x%X", __FUNCTION__, stat);
    }
    NfcAdaptation& theInstance = NfcAdaptation::GetInstance();
    theInstance.Finalize();
    NFA_SetBootMode(NFA_NORMAL_BOOT_MODE);
    gsNfaPartialEnabled = false;
    return true;
}

/**********************************************************************************
 **
 ** Function:        nfcManager_getFwVersion
 **
 ** Description:     To get the FW Version
 **
 ** Returns:         int fw version as below four byte format
 **                  [0x00  0xROM_CODE_V  0xFW_MAJOR_NO  0xFW_MINOR_NO]
 **
 **********************************************************************************/

static jint nfcManager_getFwVersion(JNIEnv* e, jobject o)
{
    (void)e;
    (void)o;
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
//    bool stat = false;                        /*commented to eliminate unused variable warning*/
    jint version = 0, temp = 0;
    tNFC_FW_VERSION nfc_native_fw_version;

    if (!sIsNfaEnabled) {
        ALOGD("NFC does not enabled!!");
        return status;
    }
    memset(&nfc_native_fw_version, 0, sizeof(nfc_native_fw_version));

    nfc_native_fw_version = nfc_ncif_getFWVersion();
    ALOGD ("FW Version: %x.%x.%x", nfc_native_fw_version.rom_code_version,
               nfc_native_fw_version.major_version,nfc_native_fw_version.minor_version);

    temp = nfc_native_fw_version.rom_code_version;
    version = temp << 16;
    temp = nfc_native_fw_version.major_version;
    version |= temp << 8;
    version |= nfc_native_fw_version.minor_version;

    ALOGD ("%s: exit; version =0x%X", __FUNCTION__,version);
    return version;
}

static void nfcManager_doSetEEPROM(JNIEnv* e, jobject o, jbyteArray val)
{
    (void)e;
    (void)o;
    (void)val;
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
//    bool stat = false;                        /*commented to eliminate unused variable warning*/
//    UINT8 param;                              /*commented to eliminate unused variable warning*/

    if (!sIsNfaEnabled) {
        ALOGD("NFC does not enabled!!");
        return;
    }

    ALOGD ("%s: exit; status =0x%X", __FUNCTION__,status);

    return;
}

/*******************************************************************************
 **
 ** Function:        getUICC_RF_Param_SetSWPBitRate()
 **
 ** Description:     Get All UICC Parameters and set SWP bit rate
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
tNFA_STATUS getUICC_RF_Param_SetSWPBitRate()
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    tNFA_PMID rf_params_NFCEE_UICC[] = {0xA0, 0xEF};
    uint8_t sakValue = 0x00;
    bool isMifareSupported;

    ALOGD("%s: enter", __FUNCTION__);

    SyncEventGuard guard (android::sNfaGetConfigEvent);
    status = NFA_GetConfig(0x01, rf_params_NFCEE_UICC);
    if (status != NFA_STATUS_OK)
    {
        ALOGE("%s: NFA_GetConfig failed", __FUNCTION__);
        return status;
    }
    android::sNfaGetConfigEvent.wait();
    sakValue = sConfig[SAK_VALUE_AT];
    ALOGD ("SAK Value =0x%X",sakValue);
    if((sakValue & 0x08) == 0x00)
    {
        isMifareSupported = false;
    }
    else
    {
        isMifareSupported = true;
    }
    status = SetUICC_SWPBitRate(isMifareSupported);

    return status;
}

#if(NFC_NXP_CHIP_TYPE != PN547C2)
/*******************************************************************************
**
** Function:        nfcManagerEnableAGCDebug
**
** Description:     Enable/Disable Dynamic RSSI feature.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManagerEnableAGCDebug(UINT8 connEvent)
{
    unsigned long enableAGCDebug = 0;
    int retvalue = 0xFF;
    GetNxpNumValue (NAME_NXP_AGC_DEBUG_ENABLE, (void*)&enableAGCDebug, sizeof(enableAGCDebug));
    menableAGC_debug_t.enableAGC = enableAGCDebug;
    ALOGD ("%s ,%lu:", __FUNCTION__, enableAGCDebug);
    if(sIsNfaEnabled != true || sIsDisabling == true)
        return;
    if(!menableAGC_debug_t.enableAGC)
    {
        ALOGD ("%s AGCDebug not enabled", __FUNCTION__);
        return;
    }
    if(connEvent == NFA_TRANS_DM_RF_FIELD_EVT &&
       menableAGC_debug_t.AGCdebugstarted == false)
    {
        pthread_t agcThread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        retvalue = pthread_create(&agcThread, &attr, enableAGCThread, NULL);
        pthread_attr_destroy(&attr);
        if(retvalue == 0)
        {
            menableAGC_debug_t.AGCdebugstarted = true;
            set_AGC_process_state(true);
        }
    }
}

void *enableAGCThread(void* /* arg */)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    while( menableAGC_debug_t.AGCdebugstarted == true )
    {
        if(get_AGC_process_state() == false)
        {
            sleep(10000);
            continue;
        }
        status = SendAGCDebugCommand();
        if(status == NFA_STATUS_OK)
        {
            ALOGD ("%s:  enable success exit", __FUNCTION__);
        }
        usleep(500000);
    }
    ALOGD ("%s: exit", __FUNCTION__);
    pthread_exit(NULL);
    return NULL;
}
/*******************************************************************************
 **
 ** Function:       set_AGC_process_state
 **
 ** Description:    sets the AGC process to stop
 **
 ** Returns:        None .
 **
 *******************************************************************************/
void set_AGC_process_state(bool state)
{
    menableAGC_debug_t.AGCdebugrunning = state;
}

/*******************************************************************************
 **
 ** Function:       get_AGC_process_state
 **
 ** Description:    returns the AGC process state.
 **
 ** Returns:        true/false .
 **
 *******************************************************************************/
bool get_AGC_process_state()
{
    return menableAGC_debug_t.AGCdebugrunning;
}
#endif

/*******************************************************************************
 **
 ** Function:        getrfDiscoveryDuration()
 **
 ** Description:     gets the current rf discovery duration.
 **
 ** Returns:         UINT16
 **
 *******************************************************************************/
UINT16 getrfDiscoveryDuration()
{
    return discDuration;
}

#if(NXP_NFCC_HCE_F == TRUE)
/*******************************************************************************
 **
 ** Function:       nfcManager_getTransanctionRequest
 **
 ** Description:    returns the payment check for HCE-F
 **
 ** Returns:        true/false .
 **
 *******************************************************************************/
bool nfcManager_getTransanctionRequest(int t3thandle, bool registerRequest)
{
    BOOLEAN stat = false;

    if(!update_transaction_stat("getTransanctionRequest",SET_TRANSACTION_STATE))
    {
        ALOGD("Transcation is in progress store the requst %d %d", t3thandle ,registerRequest);
        set_last_request(T3T_CONFIGURE, NULL);
        if(!registerRequest)
            transaction_data.t3thandle = t3thandle;
        stat = true;
    }
    else
    {
        if(!update_transaction_stat("getTransanctionRequest",RESET_TRANSACTION_STATE))
        {
            ALOGE("%s: Can not reset transaction state", __FUNCTION__);
        }
    }
    return stat;
}
#endif

/*******************************************************************************
 **
 ** Function:        nfcManager_isTransanctionOnGoing(bool isInstallRequest)
 **
 ** Description:     Base on the input parameter.It update the parameter for
 **                  install/uninstall request.
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
bool nfcManager_isTransanctionOnGoing(bool isInstallRequest)
{
    if(!update_transaction_stat("isTransanctionOnGoing",SET_TRANSACTION_STATE))
    {
        ALOGD(" Transcation is in progress store the requst");
        set_last_request(RE_ROUTING, NULL);
        if(!isInstallRequest)
            transaction_data.isInstallRequest = isInstallRequest;
        return TRUE;
    }
    else
    {
        if(!update_transaction_stat("isTransanctionOnGoing",RESET_TRANSACTION_STATE))
        {
            ALOGE("%s: Can not reset transaction state", __FUNCTION__);
        }
    }
    return FALSE;
}
#if(NFC_NXP_ESE == TRUE)
#if (NXP_ESE_JCOP_DWNLD_PROTECTION == TRUE)
/*******************************************************************************
 **
 ** Function:        nfcManager_doCheckJCOPOsDownLoad()
 **
 ** Description:     This api checks whether JCOP OS download is ongoing.
 **
 ** Returns:         true/false
 **
*******************************************************************************/
static bool nfcManager_doCheckJCOPOsDownLoad()
{
    jint ret_val = -1;
    bool checkJcopDwnld = false;
    p61_access_state_t p61_current_state = P61_STATE_INVALID;

    ret_val = NFC_GetP61Status ((void *)&p61_current_state);
    if (ret_val < 0)
    {
        ALOGD("NFC_GetP61Status failed");
        return false;
    }
    if(p61_current_state & P61_STATE_JCP_DWNLD)
        checkJcopDwnld = true;

    return checkJcopDwnld;
}
#endif
#endif
#endif
#if((NXP_EXTNS == TRUE) && (NXP_NFCC_EMPTY_DATA_PACKET == TRUE))
/*******************************************************************************
 **
 ** Function:        nfcManager_sendEmptyDataMsg()
 **
 ** Description:     Sends Empty Data packet
 **
 ** Returns:         True/False
 **
*******************************************************************************/
bool nfcManager_sendEmptyDataMsg()
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    size_t bufLen = 0;
    uint8_t* buf = NULL;
    ALOGD("nfcManager_sendEmptyRawFrame");

    status = NFA_SendRawFrame (buf, bufLen, 0);

    return (status == NFA_STATUS_OK);
}
#endif

static jbyteArray nfcManager_getAdditionalConfigOptions(JNIEnv* e, jobject /* o */) {
    char configoptions[256] = {0};
    long retlen = 0;
    jbyteArray result = NULL;
    GetNxpByteArrayValue (NAME_NXP_ADDITIONAL_CONFIG_OPTIONS, configoptions, sizeof(configoptions), &retlen);
    if(retlen > 0)
    {
        result = e->NewByteArray(retlen);
        if(result != NULL)
        {
            e->SetByteArrayRegion(result, 0, retlen, (jbyte *) configoptions);
        }
    }
    else
    {
        result = e->NewByteArray(0);
    }
    return result;

}
}
/* namespace android */
