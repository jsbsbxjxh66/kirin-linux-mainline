

#ifndef __WLAN_TYPES_H__
#define __WLAN_TYPES_H__

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif


/*****************************************************************************
  1 ï¿½ï¿½ï¿½ï¿½Í·ï¿½Ä¼ï¿½ï¿½ï¿½ï¿½ï¿½
*****************************************************************************/
#include "wlan_spec.h"
#include "oal_ext_if.h"

/*****************************************************************************
  2 ï¿½ê¶¨ï¿½ï¿½
*****************************************************************************/

/*****************************************************************************
  2.1 ï¿½ï¿½ï¿½ï¿½ï¿½ê¶¨ï¿½ï¿½
*****************************************************************************/

#define MAC_BYTE_ALIGN_VALUE                4           /* 4ï¿½Ö½Ú¶ï¿½ï¿½ï¿½ */
/*MACÖ¡Í·ï¿½ï¿½ï¿½È·ï¿½ï¿½ï¿½Æ½Ì¨*/


/* 80211MACÖ¡Í·FCï¿½Ö¶Îºê¶¨ï¿½ï¿½ */
#define WLAN_PROTOCOL_VERSION               0x00        /* Ð­ï¿½ï¿½æ±¾ */
#define WLAN_FC0_TYPE_MGT                   0x00        /* ï¿½ï¿½ï¿½ï¿½Ö¡ */
#define WLAN_FC0_TYPE_CTL                   0x04        /* ï¿½ï¿½ï¿½ï¿½Ö¡ */
#define WLAN_FC0_TYPE_DATA                  0x08        /* ï¿½ï¿½ï¿½ï¿½Ö¡ */

/* ï¿½ï¿½ï¿½ï¿½Ö¡subtype */
#define WLAN_FC0_SUBTYPE_ASSOC_REQ          0x00
#define WLAN_FC0_SUBTYPE_ASSOC_RSP          0x10
#define WLAN_FC0_SUBTYPE_REASSOC_REQ        0x20
#define WLAN_FC0_SUBTYPE_REASSOC_RSP        0x30
#define WLAN_FC0_SUBTYPE_PROBE_REQ          0x40
#define WLAN_FC0_SUBTYPE_PROBE_RSP          0x50
#define WLAN_FC0_SUBTYPE_BEACON             0x80
#define WLAN_FC0_SUBTYPE_ATIM               0x90
#define WLAN_FC0_SUBTYPE_DISASSOC           0xa0
#define WLAN_FC0_SUBTYPE_AUTH               0xb0
#define WLAN_FC0_SUBTYPE_DEAUTH             0xc0
#define WLAN_FC0_SUBTYPE_ACTION             0xd0
#define WLAN_FC0_SUBTYPE_ACTION_NO_ACK      0xe0

/* ï¿½ï¿½ï¿½ï¿½Ö¡subtype */
#define WLAN_FC0_SUBTYPE_NDPA               0x50
#define WLAN_FC0_SUBTYPE_Control_Wrapper    0x70        /* For TxBF RC */
#define WLAN_FC0_SUBTYPE_BAR                0x80
#define WLAN_FC0_SUBTYPE_BA                 0x90

#define WLAN_FC0_SUBTYPE_PS_POLL            0xa0
#define WLAN_FC0_SUBTYPE_RTS                0xb0
#define WLAN_FC0_SUBTYPE_CTS                0xc0
#define WLAN_FC0_SUBTYPE_ACK                0xd0
#define WLAN_FC0_SUBTYPE_CF_END             0xe0
#define WLAN_FC0_SUBTYPE_CF_END_ACK         0xf0

/* ï¿½ï¿½ï¿½ï¿½Ö¡subtype */
#define WLAN_FC0_SUBTYPE_DATA               0x00
#define WLAN_FC0_SUBTYPE_CF_ACK             0x10
#define WLAN_FC0_SUBTYPE_CF_POLL            0x20
#define WLAN_FC0_SUBTYPE_CF_ACPL            0x30
#define WLAN_FC0_SUBTYPE_NODATA             0x40
#define WLAN_FC0_SUBTYPE_CFACK              0x50
#define WLAN_FC0_SUBTYPE_CFPOLL             0x60
#define WLAN_FC0_SUBTYPE_CF_ACK_CF_ACK      0x70
#define WLAN_FC0_SUBTYPE_QOS                0x80
#define WLAN_FC0_SUBTYPE_QOS_NULL           0xc0

#define WLAN_FC1_DIR_MASK                   0x03
#define WLAN_FC1_DIR_NODS                   0x00        /* STA->STA */
#define WLAN_FC1_DIR_TODS                   0x01        /* STA->AP  */
#define WLAN_FC1_DIR_FROMDS                 0x02        /* AP ->STA */
#define WLAN_FC1_DIR_DSTODS                 0x03        /* AP ->AP  */

#define WLAN_FC1_MORE_FRAG                  0x04
#define WLAN_FC1_RETRY                      0x08
#define WLAN_FC1_PWR_MGT                    0x10
#define WLAN_FC1_MORE_DATA                  0x20
#define WLAN_FC1_WEP                        0x40
#define WLAN_FC1_ORDER                      0x80

#define WLAN_HDR_DUR_OFFSET                 2           /* duartionÏà¶ÔÓÚmacÍ·µÄ×Ö½ÚÆ«ÒÆ */
#define WLAN_HDR_ADDR1_OFFSET               4           /* addr1Ïà¶ÔÓÚmacÍ·µÄ×Ö½ÚÆ«ÒÆ */
#define WLAN_HDR_ADDR2_OFFSET               10          /* addr1Ïà¶ÔÓÚmacÍ·µÄ×Ö½ÚÆ«ÒÆ */
#define WLAN_HDR_ADDR3_OFFSET               16          /* addr1Ïà¶ÔÓÚmacÍ·µÄ×Ö½ÚÆ«ÒÆ */
#define WLAN_HDR_FRAG_OFFSET                22          /* ·ÖÆ¬ÐòºÅÏà¶ÔÓÚmacµÄ×Ö½ÚÆ«ÒÆ */

#define WLAN_REASON_CODE_LEN                2

/* Ö¡Í·DSÎ» */
#define WLAN_FRAME_TO_AP                   0x0100
#define WLAN_FRAME_FROM_AP                 0x0200
/* FCS³¤¶È(4×Ö½Ú) */
#define WLAN_HDR_FCS_LENGTH                 4

#define WLAN_RANDOM_MAC_OUI_LEN            3            /* Ëæ»úmacµØÖ·OUI³¤¶È */

#define WLAN_MAX_BAR_DATA_LEN               20  /* BARÖ¡µÄ×î´ó³¤¶È */
#define WLAN_CHTXT_SIZE                     128 /* challenge textµÄ³¤¶È */

#define WLAN_SEQ_SHIFT                      4
/* AMPDU Delimeter³¤¶È(4×Ö½Ú) */
#define WLAN_DELIMETER_LENGTH               4

    /* È¡¾ø¶ÔÖµ */
#define GET_ABS(val)                        ((val) > 0 ? (val) : -(val))

/* ÅäÖÃÃüÁî×î´ó³¤¶È: ´ÓËã·¨Ãû³Æ¿ªÊ¼ËãÆð£¬²»°üÀ¨"alg" */
#define DMAC_ALG_CONFIG_MAX_ARG     7

/* ÐÅµÀÇÐ»»¼ÆÊý */
#define WLAN_CHAN_SWITCH_DEFAULT_CNT        6
#define WLAN_CHAN_SWITCH_DETECT_RADAR_CNT   1

/* 5G×ÓÆµ¶ÎÊýÄ¿ */
#define WLAN_5G_SUB_BAND_NUM        7
#define WLAN_5G_20M_SUB_BAND_NUM    7
#define WLAN_5G_80M_SUB_BAND_NUM    7
#define WLAN_5G_CALI_SUB_BAND_NUM   (WLAN_5G_20M_SUB_BAND_NUM + WLAN_5G_80M_SUB_BAND_NUM)

/* µ¥¸öÍ¨µÀµÄUPCÊý¾Ý¼Ä´æÆ÷ÊýÄ¿ */
#define WLAN_UPC_DATA_REG_NUM       4

#define WLAN_FIELD_TYPE_AID            0xC000
#define WLAN_AID(AID)                  ((AID) &~ 0xc000)

#define WLAN_MAX_VAP_NUM            2
#define WLAN_2G_SUB_BAND_NUM        13
#define WLAN_SIFS_OFDM_POWLVL_NUM   4

/*****************************************************************************
  2.2 WMEï¿½ê¶¨ï¿½ï¿½
*****************************************************************************/
/* TIDï¿½ï¿½ï¿½ï¿½ï¿½ï¿½,TBD ï¿½ï¿½ WLAN_TID_MAX_NUMï¿½ï¿½Ò» */
#define WLAN_WME_MAX_TID_NUM 8

#define WLAN_WME_AC_TO_TID(_ac) (       \
        ((_ac) == WLAN_WME_AC_VO) ? 6 : \
        ((_ac) == WLAN_WME_AC_VI) ? 5 : \
        ((_ac) == WLAN_WME_AC_BK) ? 1 : \
        0)



#define WLAN_WME_TID_TO_AC(_tid) (      \
        (((_tid) == 0) || ((_tid) == 3)) ? WLAN_WME_AC_BE : \
        (((_tid) == 1) || ((_tid) == 2)) ? WLAN_WME_AC_BK : \
        (((_tid) == 4) || ((_tid) == 5)) ? WLAN_WME_AC_VI : \
        WLAN_WME_AC_VO)

/*****************************************************************************
  2.3 HTï¿½ê¶¨ï¿½ï¿½
*****************************************************************************/
/* 11n: Maximum A-MSDU Length Indicates maximum A-MSDU length.See 9.11. Set to 0 for 3839 octetsSet to 1 for 7935 octets */
/* 11AC(9.11): A VHT STA that sets the Maximum MPDU Length in the VHT Capabilities element to indicate 3895 octets
   shall set the Maximum A-MSDU Length in the HT Capabilities element to indicate 3839 octets. A VHT STA
   that sets the Maximum MPDU Length in the VHT Capabilities element to indicate 7991 octets or 11 454 oc-
   tets shall set the Maximum A-MSDU Length in the HT Capabilities element to indicate 7935 octets. */

#define WLAN_AMSDU_FRAME_MAX_LEN_SHORT      3839
#define WLAN_AMSDU_FRAME_MAX_LEN_LONG       7935

/* Maximum A-MSDU Length Indicates maximum A-MSDU length.See 9.11. Set to 0 for 3839 octetsSet to 1 for 7935 octets */
#define WLAN_HT_GET_AMSDU_MAX_LEN(_bit_amsdu_max_length)  \
       ((0 == (_bit_amsdu_max_length)) ? WLAN_AMSDU_FRAME_MAX_LEN_SHORT : WLAN_AMSDU_FRAME_MAX_LEN_LONG)

/* RSSIÍ³¼ÆÂË²¨£¬RSSI·¶Î§ÊÇ-128~127, Ò»°ã²»»áµÈÓÚ127ÕâÃ´´ó£¬ËùÒÔ½«127ÉèÖÃÎªMARKER,¼´³õÊ¼Öµ */
#define WLAN_RSSI_DUMMY_MARKER              0x7F

/*****************************************************************************
  2.4 linkloss
*****************************************************************************/
#define WLAN_LINKLOSS_REPORT                10 /* linklossÃ¿10´Î´òÓ¡Ò»´ÎÎ¬²â*/
#define WLAN_LINKLOSS_MIN_THRESHOLD         10 /* linklossÃÅÏÞ×îÐ¡×îµÍÖµ */

/*****************************************************************************
  2.14 °²È«Ïà¹Øºê¶¨Òå
*****************************************************************************/
    /* cipher suite selectors */
#ifndef WLAN_CIPHER_SUITE_USE_GROUP
#define WLAN_CIPHER_SUITE_USE_GROUP 0x000FAC00
#endif
#ifndef WLAN_CIPHER_SUITE_WEP40
#define WLAN_CIPHER_SUITE_WEP40     0x000FAC01
#endif
#ifndef WLAN_CIPHER_SUITE_TKIP
#define WLAN_CIPHER_SUITE_TKIP      0x000FAC02
#endif
    /* reserved:                0x000FAC03 */
#ifndef WLAN_CIPHER_SUITE_CCMP
#define WLAN_CIPHER_SUITE_CCMP      0x000FAC04
#endif
#ifndef WLAN_CIPHER_SUITE_WEP104
#define WLAN_CIPHER_SUITE_WEP104    0x000FAC05
#endif
#ifndef WLAN_CIPHER_SUITE_AES_CMAC
#define WLAN_CIPHER_SUITE_AES_CMAC  0x000FAC06
#endif
#ifndef WLAN_CIPHER_SUITE_GCMP
#define WLAN_CIPHER_SUITE_GCMP      0x000FAC08
#endif
#ifndef WLAN_CIPHER_SUITE_GCMP_256
#define WLAN_CIPHER_SUITE_GCMP_256  0x000FAC09
#endif
#ifndef WLAN_CIPHER_SUITE_CCMP_256
#define WLAN_CIPHER_SUITE_CCMP_256  0x000FAC0A
#endif
#ifndef WLAN_CIPHER_SUITE_BIP_GMAC_128
#define WLAN_CIPHER_SUITE_BIP_GMAC_128  0x000FAC0B
#endif
#ifndef WLAN_CIPHER_SUITE_BIP_GMAC_256
#define WLAN_CIPHER_SUITE_BIP_GMAC_256  0x000FAC0C
#endif
#ifndef WLAN_CIPHER_SUITE_BIP_CMAC_256
#define WLAN_CIPHER_SUITE_BIP_CMAC_256  0x000FAC0D
#endif


#undef  WLAN_CIPHER_SUITE_SMS4
#define WLAN_CIPHER_SUITE_SMS4      0x00147201

/* AKM suite selectors */
#define WITP_WLAN_AKM_SUITE_8021X        0x000FAC01
#define WITP_WLAN_AKM_SUITE_PSK          0x000FAC02
#define WITP_WLAN_AKM_SUITE_WAPI_PSK     0x000FAC04
#define WITP_WLAN_AKM_SUITE_WAPI_CERT    0x000FAC12



#define WLAN_PMKID_LEN           16
#define WLAN_PMKID_CACHE_SIZE    32
#define WLAN_NONCE_LEN           32
#define WLAN_PTK_PREFIX_LEN      22
#define WLAN_GTK_PREFIX_LEN      19
#define WLAN_GTK_DATA_LEN        (NONCE_LEN + WLAN_MAC_ADDR_LEN)
#define WLAN_PTK_DATA_LEN        (2 * NONCE_LEN + 2 * WLAN_MAC_ADDR_LEN)


#define WLAN_KCK_LENGTH          16
#define WLAN_KEK_LENGTH          16
#define WLAN_TEMPORAL_KEY_LENGTH 16
#define WLAN_MIC_KEY_LENGTH      8

#define WLAN_PMK_SIZE        32 /* In Bytes */
#define WLAN_PTK_SIZE        64 /* In Bytes */
#define WLAN_GTK_SIZE        32 /* In Bytes */
#define WLAN_GMK_SIZE        32 /* In Bytes */

#define WLAN_WEP40_KEY_LEN              5
#define WLAN_WEP104_KEY_LEN             13
#define WLAN_TKIP_KEY_LEN               32      /* TKIP KEY length 256 BIT */
#define WLAN_CCMP_KEY_LEN               16      /* CCMP KEY length 128 BIT */
#define WLAN_BIP_KEY_LEN                16      /* BIP KEY length 128 BIT */

#define WLAN_NUM_DOT11WEPDEFAULTKEYVALUE       4
#define WLAN_MAX_WEP_STR_SIZE                  27 /* 5 for WEP 40; 13 for WEP 104 */
#define WLAN_WEP_SIZE_OFFSET                   0
#define WLAN_WEP_KEY_VALUE_OFFSET              1

#define WLAN_COUNTRY_STR_LEN        3

/* crypto status */
#define WLAN_ENCRYPT_BIT        (1 << 0)
#define WLAN_WEP_BIT            (1 << 1)
#define WLAN_WEP104_BIT         ((1 << 2) | (1 << 1))
#define WLAN_WPA_BIT            (1 << 3)
#define WLAN_WPA2_BIT           (1 << 4)
#define WLAN_CCMP_BIT           (1 << 5)
#define WLAN_TKIP_BIT           (1 << 6)

#define WLAN_WAPI_BIT           (1 << 5)

/* auth */
#define WLAN_OPEN_SYS_BIT       (1 << 0)
#define WLAN_SHARED_KEY_BIT     (1 << 1)
#define WLAN_8021X_BIT          (1 << 2)

#define WLAN_WITP_CAPABILITY_PRIVACY BIT4



/*****************************************************************************
  3 Ã¶¾Ù¶¨Òå
*****************************************************************************/
/*****************************************************************************
  3.1 »ù±¾Ã¶¾ÙÀàÐÍ
*****************************************************************************/
/* ¹ÜÀíÖ¡×ÓÀàÐÍ */
typedef enum
{
    WLAN_ASSOC_REQ              = 0,    /* 0000 */
    WLAN_ASSOC_RSP              = 1,    /* 0001 */
    WLAN_REASSOC_REQ            = 2,    /* 0010 */
    WLAN_REASSOC_RSP            = 3,    /* 0011 */
    WLAN_PROBE_REQ              = 4,    /* 0100 */
    WLAN_PROBE_RSP              = 5,    /* 0101 */
    WLAN_TIMING_AD              = 6,    /* 0110 */
    WLAN_MGMT_SUBTYPE_RESV1     = 7,    /* 0111 */
    WLAN_BEACON                 = 8,    /* 1000 */
    WLAN_ATIM                   = 9,    /* 1001 */
    WLAN_DISASOC                = 10,   /* 1010 */
    WLAN_AUTH                   = 11,   /* 1011 */
    WLAN_DEAUTH                 = 12,   /* 1100 */
    WLAN_ACTION                 = 13,   /* 1101 */
    WLAN_ACTION_NO_ACK          = 14,   /* 1110 */
    WLAN_MGMT_SUBTYPE_RESV2     = 15,   /* 1111 */

    WLAN_MGMT_SUBTYPE_BUTT      = 16,   /* Ò»¹²16ÖÖ¹ÜÀíÖ¡×ÓÀàÐÍ */
}wlan_frame_mgmt_subtype_enum;

/*TBD£¬²»Ó¦¸ÃBUTTºóÐø´æÔÚÃ¶¾Ù*/
typedef enum
{
    WLAN_WME_AC_BE = 0,    /* best effort */
    WLAN_WME_AC_BK = 1,    /* background */
    WLAN_WME_AC_VI = 2,    /* video */
    WLAN_WME_AC_VO = 3,    /* voice */

    WLAN_WME_AC_BUTT = 4,
    WLAN_WME_AC_MGMT = WLAN_WME_AC_BUTT   /* ¹ÜÀíAC£¬Ð­ÒéÃ»ÓÐ,¶ÔÓ¦Ó²¼þ¸ßÓÅÏÈ¼¶¶ÓÁÐ*/
}wlan_wme_ac_type_enum;
typedef oal_uint8 wlan_wme_ac_type_enum_uint8;

/* TID¸öÊýÎª8,0~7 */
#define WLAN_TID_MAX_NUM                    WLAN_TIDNO_BUTT

/* TID±àºÅÀà±ð */
typedef enum
{
    WLAN_TIDNO_BEST_EFFORT              = 0, /* BEÒµÎñ */
    WLAN_TIDNO_BACKGROUND               = 1, /* BKÒµÎñ */
    WLAN_TIDNO_UAPSD                    = 2, /* U-APSD */
    WLAN_TIDNO_ANT_TRAINING_LOW_PRIO    = 3, /* ÖÇÄÜÌìÏßµÍÓÅÏÈ¼¶ÑµÁ·Ö¡ */
    WLAN_TIDNO_ANT_TRAINING_HIGH_PRIO   = 4, /* ÖÇÄÜÌìÏß¸ßÓÅÏÈ¼¶ÑµÁ·Ö¡ */
    WLAN_TIDNO_VIDEO                    = 5, /* VIÒµÎñ */
    WLAN_TIDNO_VOICE                    = 6, /* VOÒµÎñ */
    WLAN_TIDNO_BCAST                    = 7, /* ¹ã²¥ÓÃ»§µÄ¹ã²¥»òÕß×é²¥±¨ÎÄ */

    WLAN_TIDNO_BUTT
}wlan_tidno_enum;
typedef oal_uint8 wlan_tidno_enum_uint8;

/* TID±àºÅÀà±ð·ÅÈëÆ½Ì¨ */

/* VAPµÄ¹¤×÷Ä£Ê½ */
typedef enum
{
    WLAN_VAP_MODE_CONFIG,        /* ÅäÖÃÄ£Ê½ */
    WLAN_VAP_MODE_BSS_STA,       /* BSS STAÄ£Ê½ */
    WLAN_VAP_MODE_BSS_AP,        /* BSS APÄ£Ê½ */

    WLAN_VAP_MODE_BUTT
}wlan_vap_mode_enum;
typedef oal_uint8 wlan_vap_mode_enum_uint8;

/* ï¿½ï¿½Ö¤ï¿½ï¿½transaction number */
typedef enum
{
    WLAN_AUTH_TRASACTION_NUM_ONE    = 0x0001,
    WLAN_AUTH_TRASACTION_NUM_TWO    = 0x0002,
    WLAN_AUTH_TRASACTION_NUM_THREE  = 0x0003,
    WLAN_AUTH_TRASACTION_NUM_FOUR   = 0x0004,

    WLAN_AUTH_TRASACTION_NUM_BUTT
}wlan_auth_transaction_number_enum;
typedef oal_uint8 wlan_auth_transaction_number_enum_uint8;
typedef enum
{
    WLAN_BAND_2G,
    WLAN_BAND_5G,

    WLAN_BAND_BUTT
} wlan_channel_band_enum;
typedef oal_uint8 wlan_channel_band_enum_uint8;

/* Protection mode for MAC */
typedef enum
{
    WLAN_PROT_NO,           /* Do not use any protection       */
    WLAN_PROT_ERP,          /* Protect all ERP frame exchanges */
    WLAN_PROT_HT,           /* Protect all HT frame exchanges  */
    WLAN_PROT_GF,           /* Protect all GF frame exchanges  */

    WLAN_PROT_BUTT
}wlan_prot_mode_enum;
typedef oal_uint8 wlan_prot_mode_enum_uint8;

typedef enum
{
    WLAN_RTS_RATE_SELECT_MODE_REG,  /* 0: RTSï¿½ï¿½ï¿½ï¿½ = PROT_DATARATEï¿½ï¿½ï¿½ï¿½ï¿½ï¿½                           */
    WLAN_RTS_RATE_SELECT_MODE_DESC, /* 1: RTSï¿½ï¿½ï¿½ï¿½ = Ó²ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½TXï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Öµ                    */
    WLAN_RTS_RATE_SELECT_MODE_MIN,  /* 2: RTSï¿½ï¿½ï¿½ï¿½ = min(PROT_DATARATE,Ó²ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½TXï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Öµ) */
    WLAN_RTS_RATE_SELECT_MODE_MAX,  /* 3: RTSï¿½ï¿½ï¿½ï¿½ = max(PROT_DATARATE,Ó²ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½TXï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Öµ) */

    WLAN_RTS_RATE_SELECT_MODE_BUTT
}wlan_rts_rate_select_mode_enum;

typedef enum
{
    WLAN_WITP_AUTH_OPEN_SYSTEM = 0,
    WLAN_WITP_AUTH_SHARED_KEY,
    WLAN_WITP_AUTH_FT,
    WLAN_WITP_AUTH_NETWORK_EAP,
    WLAN_WITP_AUTH_SAE,
    WLAN_WITP_AUTH_NUM,
    WLAN_WITP_AUTH_MAX = WLAN_WITP_AUTH_NUM - 1,
    WLAN_WITP_AUTH_AUTOMATIC,

    WLAN_WITP_ALG_AUTH_BUTT
}wlan_auth_alg_mode_enum;
typedef oal_uint8 wlan_auth_alg_enum_uint8;

typedef enum
{
    /*
    *  ×¢ï¿½ï¿½: wlan_cipher_key_type_enumï¿½ï¿½hal_key_type_enum ÖµÒ»ï¿½ï¿½,
    *        ï¿½ï¿½ï¿½Ó²ï¿½ï¿½ï¿½Ð¸Ä±ä£¬ï¿½ï¿½Ó¦ï¿½ï¿½ï¿½ï¿½HAL ï¿½ï¿½ï¿½×°
    */
    WLAN_KEY_TYPE_TX_GTK              = 0,  /* TX GTK */
    WLAN_KEY_TYPE_PTK                 = 1,  /* PTK */
    WLAN_KEY_TYPE_RX_GTK              = 2,  /* RX GTK */
    WLAN_KEY_TYPE_RX_GTK2             = 3,  /* RX GTK2 51ï¿½ï¿½ï¿½ï¿½ */
    WLAN_KEY_TYPE_BUTT
} wlan_cipher_key_type_enum;
typedef oal_uint8 wlan_cipher_key_type_enum_uint8;

typedef enum
{
    /*
    *  ×¢ï¿½ï¿½: wlan_key_origin_enum_uint8ï¿½ï¿½hal_key_origin_enum_uint8 ÖµÒ»ï¿½ï¿½,
    *        ï¿½ï¿½ï¿½Ó²ï¿½ï¿½ï¿½Ð¸Ä±ä£¬ï¿½ï¿½Ó¦ï¿½ï¿½ï¿½ï¿½HAL ï¿½ï¿½ï¿½×°
    */

    WLAN_AUTH_KEY = 0,      /* Indicates that for this key, this STA is the authenticator */
    WLAN_SUPP_KEY = 1,      /* Indicates that for this key, this STA is the supplicant */

    WLAN_KEY_ORIGIN_BUTT,
}wlan_key_origin_enum;
typedef oal_uint8 wlan_key_origin_enum_uint8;

typedef enum
{
    /* °´ÕÕ80211-2012 Ð­Òé Table 8-99 Cipher suite selectors ¶¨Òå */
    WLAN_80211_CIPHER_SUITE_GROUP_CIPHER = 0,
    WLAN_80211_CIPHER_SUITE_WEP_40       = 1,
    WLAN_80211_CIPHER_SUITE_TKIP         = 2,
    WLAN_80211_CIPHER_SUITE_RSV          = 3,
    WLAN_80211_CIPHER_SUITE_NO_ENCRYP    = WLAN_80211_CIPHER_SUITE_RSV, /* ²ÉÓÃÐ­Òé¶¨ÒåµÄ±£ÁôÖµ×ö²»¼ÓÃÜÀàÐÍ */
    WLAN_80211_CIPHER_SUITE_CCMP         = 4,
    WLAN_80211_CIPHER_SUITE_WEP_104      = 5,
    WLAN_80211_CIPHER_SUITE_BIP          = 6,
    WLAN_80211_CIPHER_SUITE_GROUP_DENYD  = 7,
    WLAN_80211_CIPHER_SUITE_GCMP         = 8,             /* GCMP-128 default for a DMG STA */
    WLAN_80211_CIPHER_SUITE_GCMP_256     = 9,
    WLAN_80211_CIPHER_SUITE_CCMP_256     = 10,
    WLAN_80211_CIPHER_SUITE_BIP_GMAC_128 = 11,           /* BIP GMAC 128 */
    WLAN_80211_CIPHER_SUITE_BIP_GMAC_256 = 12,           /* BIP GMAC 256 */
    WLAN_80211_CIPHER_SUITE_BIP_CMAC_256 = 13,           /* BIP CMAC 256 */
    WLAN_80211_CIPHER_SUITE_WAPI         = 14,      /* ËæÒâ¶¨£¬²»Ó°Ïì11i¼´¿É */
} wlan_ciper_protocol_type_enum;
typedef oal_uint8 wlan_ciper_protocol_type_enum_uint8;

/* °´ÕÕ80211-2012 Ð­Òé Table 8-101 AKM suite selectors ¶¨Òå */
#define WLAN_AUTH_SUITE_RSV              0
#define WLAN_AUTH_SUITE_1X               1
#define WLAN_AUTH_SUITE_PSK              2
#define WLAN_AUTH_SUITE_FT_1X            3
#define WLAN_AUTH_SUITE_FT_PSK           4
#define WLAN_AUTH_SUITE_1X_SHA256        5
#define WLAN_AUTH_SUITE_PSK_SHA256       6
#define WLAN_AUTH_SUITE_TDLS             7
#define WLAN_AUTH_SUITE_SAE_SHA256       8
#define WLAN_AUTH_SUITE_FT_SHA256        9

typedef enum
{
    WITP_WPA_VERSION_1 = 1,
    WITP_WPA_VERSION_2 = 2,
#ifdef _PRE_WLAN_FEATURE_WAPI
    WITP_WAPI_VERSION = 1 << 2,
#endif
}witp_wpa_versions_enum;
typedef oal_uint8 witp_wpa_versions_enum_uint8;


typedef struct
{
    wlan_cipher_key_type_enum_uint8          en_cipher_key_type;      /* ï¿½ï¿½Ô¿ID/ï¿½ï¿½ï¿½ï¿½ */
    wlan_ciper_protocol_type_enum_uint8      en_cipher_protocol_type;
    oal_uint8                                uc_cipher_key_id;
    oal_uint8                                auc_resv[1];
}wlan_security_txop_params_stru;

/* Ð­ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ã¶ï¿½ï¿½ */
typedef enum
{
    WLAN_PROTOCOL_CAP_LEGACY,
    WLAN_PROTOCOL_CAP_HT,
    WLAN_PROTOCOL_CAP_VHT,

    WLAN_PROTOCOL_CAP_BUTT,
}wlan_protocol_cap_enum;
typedef oal_uint8 wlan_protocol_cap_enum_uint8;

/* Æµï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ã¶ï¿½ï¿½ */
typedef enum
{
    WLAN_BAND_CAP_2G,        /* Ö»Ö§ï¿½ï¿½2G */
    WLAN_BAND_CAP_5G,        /* Ö»Ö§ï¿½ï¿½5G */
    WLAN_BAND_CAP_2G_5G,     /* Ö§ï¿½ï¿½2G 5G */

    WLAN_BAND_CAP_BUTT
}wlan_band_cap_enum;
typedef oal_uint8 wlan_band_cap_enum_uint8;

/* ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ã¶ï¿½ï¿½ */
typedef enum
{
    WLAN_BW_CAP_20M,
    WLAN_BW_CAP_40M,
    WLAN_BW_CAP_80M,
    WLAN_BW_CAP_160M,

    WLAN_BW_CAP_BUTT
}wlan_bw_cap_enum;
typedef oal_uint8 wlan_bw_cap_enum_uint8;


/* µ÷ÖÆ·½Ê½Ã¶¾Ù */
typedef enum
{
    WLAN_MOD_DSSS,
    WLAN_MOD_OFDM,

    WLAN_MOD_BUTT
}wlan_mod_enum;
typedef oal_uint8 wlan_mod_enum_uint8;

typedef enum
{
    WLAN_CSA_MODE_TX_ENABLE = 0,
    WLAN_CSA_MODE_TX_DISABLE,

    WLAN_CSA_MODE_TX_BUTT
}wlan_csa_mode_tx_enum;
typedef oal_uint8 wlan_csa_mode_tx_enum_uint8;

/*****************************************************************************
  3.4 VHTÃ¶ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
*****************************************************************************/
#ifdef _PRE_WLAN_NARROW_BAND
typedef enum
{
    NARROW_BW_10M = 0x80,
    NARROW_BW_5M = 0x81,
    NARROW_BW_1M = 0x82,
    NARROW_BW_BUTT
}mac_narrow_bw_enum;
typedef oal_uint8 mac_narrow_bw_enum_uint8;
#endif

typedef enum
{
    WLAN_VHT_MCS0,
    WLAN_VHT_MCS1,
    WLAN_VHT_MCS2,
    WLAN_VHT_MCS3,
    WLAN_VHT_MCS4,
    WLAN_VHT_MCS5,
    WLAN_VHT_MCS6,
    WLAN_VHT_MCS7,
    WLAN_VHT_MCS8,
    WLAN_VHT_MCS9,

    WLAN_VHT_MCS_BUTT,
}wlan_vht_mcs_enum;
typedef oal_uint8 wlan_vht_mcs_enum_uint8;

/*
    ï¿½ï¿½ï¿½ï¿½1101ï¿½ï¿½ï¿½ï¿½ï¿½Ë³ï¿½ï¿½
    TBDï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Sï¿½ï¿½ï¿½Ûºï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Êµï¿½ï¿½Èºï¿½Ë³ï¿½ï¿½
*/
typedef enum
{
    WLAN_LEGACY_11b_RESERVED1       = 0,
    WLAN_SHORT_11b_2M_BPS           = 1,
    WLAN_SHORT_11b_5_HALF_M_BPS     = 2,
    WLAN_SHORT_11b_11_M_BPS         = 3,

    WLAN_LONG_11b_1_M_BPS           = 4,
    WLAN_LONG_11b_2_M_BPS           = 5,
    WLAN_LONG_11b_5_HALF_M_BPS      = 6,
    WLAN_LONG_11b_11_M_BPS          = 7,

    WLAN_LEGACY_OFDM_48M_BPS        = 8,
    WLAN_LEGACY_OFDM_24M_BPS        = 9,
    WLAN_LEGACY_OFDM_12M_BPS        = 10,
    WLAN_LEGACY_OFDM_6M_BPS         = 11,
    WLAN_LEGACY_OFDM_54M_BPS        = 12,
    WLAN_LEGACY_OFDM_36M_BPS        = 13,
    WLAN_LEGACY_OFDM_18M_BPS        = 14,
    WLAN_LEGACY_OFDM_9M_BPS         = 15,

    WLAN_LEGACY_RATE_VALUE_BUTT
}wlan_legacy_rate_value_enum;
typedef oal_uint8 wlan_legacy_rate_value_enum_uint8;

/* WIFIÐ­ï¿½ï¿½ï¿½ï¿½ï¿½Í¶ï¿½ï¿½ï¿½ */
typedef enum
{
    WLAN_LEGACY_11A_MODE            = 0,    /* 11a, 5G, OFDM */
    WLAN_LEGACY_11B_MODE            = 1,    /* 11b, 2.4G */
    WLAN_LEGACY_11G_MODE            = 2,    /* ï¿½Éµï¿½11g onlyï¿½Ñ·ï¿½ï¿½ï¿½, 2.4G, OFDM */
    WLAN_MIXED_ONE_11G_MODE         = 3,    /* 11bg, 2.4G */
    WLAN_MIXED_TWO_11G_MODE         = 4,    /* 11g only, 2.4G */
    WLAN_HT_MODE                    = 5,    /* 11n(11bgnï¿½ï¿½ï¿½ï¿½11anï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Æµï¿½ï¿½ï¿½Ð¶ï¿½) */
    WLAN_VHT_MODE                   = 6,    /* 11ac */
    WLAN_HT_ONLY_MODE               = 7,    /* 11n only mode,Ö»ï¿½Ð´ï¿½HTï¿½ï¿½ï¿½è±¸ï¿½Å¿ï¿½ï¿½Ô½ï¿½ï¿½ï¿½ */
    WLAN_VHT_ONLY_MODE              = 8,    /* 11ac only mode Ö»ï¿½Ð´ï¿½VHTï¿½ï¿½ï¿½è±¸ï¿½Å¿ï¿½ï¿½Ô½ï¿½ï¿½ï¿½ */
    WLAN_HT_11G_MODE                = 9,    /* 11ng,ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½11b*/

    WLAN_PROTOCOL_BUTT
}wlan_protocol_enum;
typedef oal_uint8 wlan_protocol_enum_uint8;

/* ï¿½ï¿½Òª:ï¿½ï¿½ï¿½ï¿½VAPï¿½ï¿½preambleÐ­ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ê¹ï¿½Ã¸ï¿½Ã¶ï¿½Ù£ï¿½0ï¿½ï¿½Ê¾long preamble; 1ï¿½ï¿½Ê¾short preamble */
typedef enum
{
    WLAN_LEGACY_11B_MIB_LONG_PREAMBLE    = 0,
    WLAN_LEGACY_11B_MIB_SHORT_PREAMBLE   = 1,
}wlan_11b_mib_preamble_enum;
typedef oal_uint8 wlan_11b_mib_preamble_enum_uint8;

/* ï¿½ï¿½Òª:ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ó¿ï¿½Ê¹ï¿½ï¿½(ï¿½ï¿½Ê¾ï¿½ï¿½ï¿½Í¸ï¿½Ö¡Ê¹ï¿½Ãµï¿½prambleï¿½ï¿½ï¿½ï¿½)ï¿½ï¿½0ï¿½ï¿½Ê¾short preamble; 1ï¿½ï¿½Ê¾long preamble */
typedef enum
{
    WLAN_LEGACY_11B_DSCR_SHORT_PREAMBLE  = 0,
    WLAN_LEGACY_11B_DSCR_LONG_PREAMBLE   = 1,

    WLAN_LEGACY_11b_PREAMBLE_BUTT
}wlan_11b_dscr_preamble_enum;
typedef oal_uint8 wlan_11b_dscr_preamble_enum_uint8;

/*****************************************************************************
  3.3 HTÃ¶¾ÙÀàÐÍ
*****************************************************************************/
typedef enum
{
    WLAN_BAND_WIDTH_20M                 = 0,
    WLAN_BAND_WIDTH_40PLUS              = 1,    /* ´Ó20ÐÅµÀ+1 */
    WLAN_BAND_WIDTH_40MINUS             = 2,    /* ´Ó20ÐÅµÀ-1 */
    WLAN_BAND_WIDTH_80PLUSPLUS          = 3,    /* ´Ó20ÐÅµÀ+1, ´Ó40ÐÅµÀ+1 */
    WLAN_BAND_WIDTH_80PLUSMINUS         = 4,    /* ´Ó20ÐÅµÀ+1, ´Ó40ÐÅµÀ-1 */
    WLAN_BAND_WIDTH_80MINUSPLUS         = 5,    /* ´Ó20ÐÅµÀ-1, ´Ó40ÐÅµÀ+1 */
    WLAN_BAND_WIDTH_80MINUSMINUS        = 6,    /* ´Ó20ÐÅµÀ-1, ´Ó40ÐÅµÀ-1 */
    WLAN_BAND_WIDTH_40M                 = 7,
    WLAN_BAND_WIDTH_80M                 = 8,
    WLAN_BAND_WIDTH_BUTT
}wlan_channel_bandwidth_enum;
typedef oal_uint8 wlan_channel_bandwidth_enum_uint8;

typedef enum
{
    WLAN_CH_SWITCH_DONE     = 0,   /* ï¿½Åµï¿½ï¿½Ð»ï¿½ï¿½Ñ¾ï¿½ï¿½ï¿½É£ï¿½APï¿½ï¿½ï¿½ï¿½ï¿½Åµï¿½ï¿½ï¿½ï¿½ï¿½ */
    WLAN_CH_SWITCH_STATUS_1 = 1,   /* APï¿½ï¿½ï¿½Úµï¿½Ç°ï¿½Åµï¿½ï¿½ï¿½×¼ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Åµï¿½ï¿½Ð»ï¿½(ï¿½ï¿½ï¿½ï¿½CSAÖ¡/IE) */
    WLAN_CH_SWITCH_STATUS_2 = 2,

    WLAN_CH_SWITCH_BUTT
}wlan_ch_switch_status_enum;
typedef oal_uint8 wlan_ch_switch_status_enum_uint8;

typedef enum
{
    WLAN_BW_SWITCH_DONE     = 0,    /* Æµï¿½ï¿½ï¿½Ð»ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ */
    WLAN_BW_SWITCH_40_TO_20 = 1,    /* ï¿½ï¿½40MHzï¿½ï¿½ï¿½ï¿½ï¿½Ð»ï¿½ï¿½ï¿½20MHzï¿½ï¿½ï¿½ï¿½ */
    WLAN_BW_SWITCH_20_TO_40 = 2,    /* ï¿½ï¿½20MHzï¿½ï¿½ï¿½ï¿½ï¿½Ð»ï¿½ï¿½ï¿½40MHzï¿½ï¿½ï¿½ï¿½ */

    /* ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ */

    WLAN_BW_SWITCH_BUTT
}wlan_bw_switch_status_enum;
typedef oal_uint8 wlan_bw_switch_status_enum_uint8;


typedef enum
{
    WLAN_BAND_ASSEMBLE_20M                   = 0,

    WLAN_BAND_ASSEMBLE_40M                   = 4,
    WLAN_BAND_ASSEMBLE_40M_DUP               = 5,

    WLAN_BAND_ASSEMBLE_80M                   = 8,
    WLAN_BAND_ASSEMBLE_80M_DUP               = 9,

    WLAN_BAND_ASSEMBLE_160M                  = 12,
    WLAN_BAND_ASSEMBLE_160M_DUP              = 13,

    WLAN_BAND_ASSEMBLE_80M_80M               = 15,

    WLAN_BAND_ASSEMBLE_AUTO,

    WLAN_BAND_ASSEMBLE_BUTT
}hal_channel_assemble_enum;
typedef oal_uint8 hal_channel_assemble_enum_uint8;


typedef enum
{
    WLAN_HT_MIXED_PREAMBLE          = 0,
    WLAN_HT_GF_PREAMBLE             = 1,

    WLAN_HT_PREAMBLE_BUTT
}wlan_ht_preamble_enum;
typedef oal_uint8 wlan_ht_preamble_enum_uint8;

typedef enum
{
    WLAN_HT_MCS0,
    WLAN_HT_MCS1,
    WLAN_HT_MCS2,
    WLAN_HT_MCS3,
    WLAN_HT_MCS4,
    WLAN_HT_MCS5,
    WLAN_HT_MCS6,
    WLAN_HT_MCS7,
    WLAN_HT_MCS8,
    WLAN_HT_MCS9,
    WLAN_HT_MCS10,
    WLAN_HT_MCS11,
    WLAN_HT_MCS12,
    WLAN_HT_MCS13,
    WLAN_HT_MCS14,
    WLAN_HT_MCS15,

    WLAN_HT_MCS_BUTT
}wlan_ht_mcs_enum;
typedef oal_uint8 wlan_ht_mcs_enum_uint8;
#define WLAN_HT_MCS32   32
#define WLAN_MIN_MPDU_LEN_FOR_MCS32   12
#define WLAN_MIN_MPDU_LEN_FOR_MCS32_SHORTGI   13

typedef enum
{
    WLAN_SINGLE_NSS                 = 0,
    WLAN_DOUBLE_NSS                 = 1,
    WLAN_TRIPLE_NSS                 = 2,
    WLAN_FOUR_NSS                   = 3,

    WLAN_NSS_BUTT
}wlan_nss_enum;
typedef oal_uint8 wlan_nss_enum_uint8;

/************************By DOTA**************************/
/*
typedef enum
{
    WLAN_FIRST_BLOOD            = 0,
    WLAN_DOUBLE_KILL            = 1,
    WLAN_TRIPLE_KILL            = 2,
    WLAN_ULTRA_KILL             = 3,
    WLAN_RAMPAGE                = 4,

    WLAN_KILL_BUTT
}wlan_kill_enum;
typedef oal_uint8 wlan_kill_enum_uint8;
*/

typedef enum
{
    WLAN_HT_NON_STBC                   = 0,
    WLAN_HT_ADD_ONE_NTS                = 1,
    WLAN_HT_ADD_TWO_NTS                = 2,

    WLAN_HT_STBC_BUTT
}wlan_ht_stbc_enum;
typedef oal_uint8 wlan_ht_stbc_enum_uint8;

typedef enum
{
    WLAN_NO_SOUNDING                = 0,
    WLAN_NDP_SOUNDING               = 1,
    WLAN_STAGGERED_SOUNDING         = 2,
    WLAN_LEGACY_SOUNDING            = 3,

    WLAN_SOUNDING_BUTT
}wlan_sounding_enum;
typedef oal_uint8 wlan_sounding_enum_uint8;

typedef struct
{
    oal_uint8                               uc_group_id;      /* group_id   */
    oal_uint8                               uc_txop_ps_not_allowed;
    oal_uint16                              us_partial_aid;   /* partial_aid */
}wlan_groupid_partial_aid_stru;

/*****************************************************************************
  3.4 TXBFÃ¶ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
*****************************************************************************/
typedef enum
{
    WLAN_NON_TXBF                   = 0,
    WLAN_EXPLICIT_TXBF              = 1,
    WLAN_LEGACY_TXBF                = 2,

    WLAN_TXBF_BUTT
}wlan_txbf_enum;
typedef oal_uint8 wlan_txbf_enum_uint8;

/*****************************************************************************
  3.5 WMEÃ¶ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
*****************************************************************************/

/*WMMÃ¶ï¿½ï¿½ï¿½ï¿½ï¿½Í·ï¿½ï¿½ï¿½Æ½Ì¨*/

/* Ö¡ï¿½ï¿½ï¿½ï¿½ (2-bit) */
typedef enum
{
    WLAN_MANAGEMENT            = 0,
    WLAN_CONTROL               = 1,
    WLAN_DATA_BASICTYPE        = 2,
    //WLAN_DATA_NULL             = 3, /*QOS NULL DATA && NULL DATA*/
    WLAN_RESERVED              = 3,

    WLAN_FRAME_TYPE_BUTT
} wlan_frame_type_enum;
typedef oal_uint8 wlan_frame_type_enum_uint8;

/* Ö¡ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ (4-bit) */
/* ï¿½ï¿½ï¿½ï¿½Ö¡ï¿½ï¿½ï¿½ï¿½ï¿½Í·ï¿½ï¿½ï¿½Æ½Ì¨SPEC */

/* ï¿½ï¿½ï¿½ï¿½Ö¡Ö¡ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ */
typedef enum
{
    /* 0~6 reserved */
    WLAN_VHT_NDPA               = 5,     /* 0101 */
    WLAN_CONTROL_WRAPPER        = 7,
    WLAN_BLOCKACK_REQ           = 8,
    WLAN_BLOCKACK               = 9,
    WLAN_PS_POLL                = 10,
    WLAN_RTS                    = 11,
    WLAN_CTS                    = 12,
    WLAN_ACK                    = 13,
    WLAN_CF_END                 = 14,
    WLAN_CF_END_CF_ACK          = 15,

    WLAN_CONTROL_SUBTYPE_BUTT   = 16,
}wlan_frame_control_subtype_enum;

/* ï¿½ï¿½ï¿½ï¿½Ö¡ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ */
typedef enum
{
    WLAN_DATA                   = 0,
    WLAN_DATA_CF_ACK            = 1,
    WLAN_DATA_CF_POLL           = 2,
    WLAN_DATA_CF_ACK_POLL       = 3,
    WLAN_NULL_FRAME             = 4,
    WLAN_CF_ACK                 = 5,
    WLAN_CF_POLL                = 6,
    WLAN_CF_ACK_POLL            = 7,
    WLAN_QOS_DATA               = 8,
    WLAN_QOS_DATA_CF_ACK        = 9,
    WLAN_QOS_DATA_CF_POLL       = 10,
    WLAN_QOS_DATA_CF_ACK_POLL   = 11,
    WLAN_QOS_NULL_FRAME         = 12,
    WLAN_DATA_SUBTYPE_RESV1     = 13,
    WLAN_QOS_CF_POLL            = 14,
    WLAN_QOS_CF_ACK_POLL        = 15,

    WLAN_DATA_SUBTYPE_MGMT      = 16,
}wlan_frame_data_subtype_enum;

/* ACKï¿½ï¿½ï¿½Í¶ï¿½ï¿½ï¿½ */
typedef enum
{
    WLAN_TX_NORMAL_ACK = 0,
    WLAN_TX_NO_ACK,
    WLAN_TX_NO_EXPLICIT_ACK,
    WLAN_TX_BLOCK_ACK,

    WLAN_TX_NUM_ACK_BUTT
}wlan_tx_ack_policy_enum;
typedef oal_uint8   wlan_tx_ack_policy_enum_uint8;

/*****************************************************************************
  3.6 ï¿½Åµï¿½Ã¶ï¿½ï¿½
*****************************************************************************/

/* ï¿½Åµï¿½ï¿½ï¿½ï¿½ë·½Ê½ */
typedef enum
{
    WLAN_BCC_CODE                   = 0,
    WLAN_LDPC_CODE                  = 1,
    WLAN_CHANNEL_CODE_BUTT
}wlan_channel_code_enum;
typedef oal_uint8 wlan_channel_code_enum_uint8;

/* É¨ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ */
typedef enum
{
    WLAN_SCAN_TYPE_PASSIVE       = 0,
    WLAN_SCAN_TYPE_ACTIVE        = 1,

    WLAN_SCAN_TYPE_BUTT
}wlan_scan_type_enum;
typedef oal_uint8 wlan_scan_type_enum_uint8;

/* É¨ÃèÄ£Ê½ */
typedef enum
{
    WLAN_SCAN_MODE_FOREGROUND,          /* Ç°¾°É¨Ãè²»·ÖAP/STA(¼´³õÊ¼É¨Ãè£¬Á¬ÐøÊ½) */
    WLAN_SCAN_MODE_BACKGROUND_STA,      /* STA±³¾°É¨Ãè */
    WLAN_SCAN_MODE_BACKGROUND_AP,       /* AP±³¾°É¨Ãè(¼ä¸ôÊ½) */
    WLAN_SCAN_MODE_BACKGROUND_OBSS,     /* 20/40MHz¹²´æµÄobssÉ¨Ãè */
    WLAN_SCAN_MODE_BACKGROUND_CCA,
    WLAN_SCAN_MODE_BACKGROUND_PNO,      /* PNOµ÷¶ÈÉ¨Ãè */
#ifdef _PRE_WLAN_FEATURE_11K
    WLAN_SCAN_MODE_RRM_BEACON_REQ,
#endif
    WLAN_SCAN_MODE_BACKGROUND_CSA,      /* ÐÅµÀÇÐ»»É¨Ãè */

#ifdef _PRE_WLAN_FEATURE_FTM
    WLAN_SCAN_MODE_FTM_REQ,
#endif

#ifdef _PRE_WLAN_FEATURE_GNSS_SCAN
    WLAN_SCAN_MODE_GNSS_SCAN,
#endif

    WLAN_SCAN_MODE_BUTT
} wlan_scan_mode_enum;
typedef oal_uint8 wlan_scan_mode_enum_uint8;

/* É¨Ãè½á¹ûÃ¶¾Ù */
typedef enum
{
    WLAN_SCAN_EVENT_COMPLETE    = 0,
    WLAN_SCAN_EVENT_FAILED,
    WLAN_SCAN_EVENT_ABORT,  /* Ç¿ÖÆÖÕÖ¹£¬±ÈÈçÐ¶ÔØ */
    WLAN_SCAN_EVENT_TIMEOUT,
    WLAN_SCAN_EVENT_BUTT
}dmac_scan_event_enum;
typedef oal_uint8 wlan_scan_event_enum_uint8;

typedef enum
{
    WLAN_LEGACY_VAP_MODE     = 0,    /* ï¿½ï¿½P2Pï¿½è±¸ */
    WLAN_P2P_GO_MODE         = 1,    /* P2P_GO */
    WLAN_P2P_DEV_MODE        = 2,    /* P2P_Device */
    WLAN_P2P_CL_MODE         = 3,    /* P2P_CL */

    WLAN_P2P_BUTT
}wlan_p2p_mode_enum;
typedef oal_uint8 wlan_p2p_mode_enum_uint8;

/*****************************************************************************
  3.7 ¼ÓÃÜÃ¶¾Ù
*****************************************************************************/

/* pmfµÄÄÜÁ¦ */
typedef enum
{
    MAC_PMF_DISABLED  = 0, /* ²»Ö§³ÖpmfÄÜÁ¦ */
    MAC_PMF_ENABLED,       /* Ö§³ÖpmfÄÜÁ¦£¬ÇÒ²»Ç¿ÖÆ */
    MAC_PME_REQUIRED,       /* ÑÏ¸ñÖ´ÐÐpmfÄÜÁ¦ */

    MAC_PMF_BUTT
}wlan_pmf_cap_status;
typedef oal_uint8 wlan_pmf_cap_status_uint8;

/*****************************************************************************
  3.8 linkloss³¡¾°Ã¶¾Ù
*****************************************************************************/

/*linkloss³¡¾°Ã¶¾Ù*/
typedef enum
{
    WLAN_LINKLOSS_MODE_BT = 0,
    WLAN_LINKLOSS_MODE_DBAC,
    WLAN_LINKLOSS_MODE_NORMAL,

    WLAN_LINKLOSS_MODE_BUTT
}wlan_linkloss_mode_enum;
typedef oal_uint8 wlan_linkloss_mode_enum_uint8;

/*****************************************************************************
  3.9 extlnaÃ¶¾Ù
*****************************************************************************/
/* ¶¯Ì¬bypassÍâÖÃLNA×´Ì¬ */
typedef enum
{
    WLAN_EXTLNA_BYPASS_EN_OFF      = 0, /* ¶¯Ì¬bypassÍâÖÃLNA¹¦ÄÜ¹Ø */
    WLAN_EXTLNA_BYPASS_EN_ON       = 1, /* ¶¯Ì¬bypassÍâÖÃLNA¹¦ÄÜ¿ª */
    WLAN_EXTLNA_BYPASS_EN_FORCE    = 2, /* µÍ¹¦ºÄ³¡¾°Ç¿ÖÆ¹Ø±ÕÍâÖÃLNA */
    WLAN_EXTLNA_BYPASS_EN_SCANNING = 3, /* É¨ÃèÊ±ÔÝÊ±¹Ø±Õ¶¯Ì¬bypassÍâÖÃLNA¹¦ÄÜ(¼Ä´æÆ÷ÉèÖÃÄ¬ÈÏÖµ) */

    WLAN_EXTLNA_BYPASS_BUTT        = 4,
} wlan_extlna_bypass_enum;

/*****************************************************************************
  4 È«¾Ö±äÁ¿ÉùÃ÷
*****************************************************************************/


/*****************************************************************************
  5 ÏûÏ¢Í·¶¨Òå
*****************************************************************************/


/*****************************************************************************
  6 ÏûÏ¢¶¨Òå
*****************************************************************************/


/*****************************************************************************
  7 STRUCT¶¨Òå
*****************************************************************************/
typedef struct
{
    oal_uint32                      ul_cipher;                      /*¼ÓÃÜÀàÐÍ*/
    oal_uint32                      ul_key_len;                     /*ÃÜÔ¿³¤*/
    oal_uint32                      ul_seq_len;                     /*sequnece³¤*/
    oal_uint8                       auc_key[WLAN_WPA_KEY_LEN];      /*ÃÜÔ¿*/
    oal_uint8                       auc_seq[WLAN_WPA_SEQ_LEN];      /*sequence*/
}wlan_priv_key_param_stru;

/*****************************************************************************
  8 UNION¶¨Òå
*****************************************************************************/


/*****************************************************************************
  9 OTHERS¶¨Òå
*****************************************************************************/

OAL_STATIC OAL_INLINE oal_uint8  wlan_hdr_get_frame_type(oal_uint8 *puc_header)
{
    return ((puc_header[0] & (0x0c)) >> 2);
}
/*****************************************************************************
  10 ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
*****************************************************************************/


#ifdef __cplusplus
    #if __cplusplus
        }
    #endif
#endif

#endif /* end of wlan_types.h */
