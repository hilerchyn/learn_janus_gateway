//
// Created by hiler on 2020/1/13.
//




#include "plugin.h"

#include <jansson.h>
#include <stun/constants.h>


#include "../debug.h"
#include "../refcount.h"
#include "../utils.h"
#include "../config.h"
#include "../rtp.h"
#include "sdp-utils.h"
#include "../record.h"


/* Plugin information */
#define JANUS_MULTILIVE_VERSION         1
#define JANUS_MULTILIVE_VERSION_STRING  "0.0.1"
#define JANUS_MULTILIVE_DESCRIPTION     "Multi live streaming"
#define JANUS_MULTILIVE_NAME            "Multi Live plugin"
#define JANUS_MULTILIVE_AUTHOR          "https://7shu.co"
#define JANUS_MULTILIVE_PACKAGE         "janus.plugin.multilive"


/* Plugin methods */
janus_plugin *create(void);
int janus_multilive_init(janus_callbacks *callback, const char *config_path);
void janus_multilive_destroy(void);


int janus_multilive_get_api_compatibility(void);
int janus_multilive_get_version(void);
const char *janus_multilive_get_version_string(void);
const char *janus_multilive_get_description(void);
const char *janus_multilive_get_name(void);
const char *janus_multilive_get_author(void);
const char *janus_multilive_get_package(void);


void janus_multilive_create_session(janus_plugin_session *handle, int *error);
struct janus_plugin_result *janus_multilive_handle_message(janus_plugin_session *handle, char *transaction, json_t *message, json_t *jsep);
json_t *janus_multilive_handle_admin_message(json_t *message);
void janus_multilive_setup_media(janus_plugin_session *handle);
void janus_multilive_incoming_rtp(janus_plugin_session *handle, int video, char *buf, int len);
void janus_multilive_incoming_rtcp(janus_plugin_session *handle, int video, char *buf, int len);
void janus_multilive_incoming_data(janus_plugin_session *handle, char *label, char *buf, int len);
void janus_multilive_slow_link(janus_plugin_session *handle, int uplink, int video);
void janus_multilive_hangup_media(janus_plugin_session *handle);
void janus_multilive_destroy_session(janus_plugin_session *handle, int *error);
json_t *janus_multilive_query_session(janus_plugin_session *handle);

static janus_plugin janus_multilive_plugin =
    JANUS_PLUGIN_INIT(
        .init = janus_multilive_init,
        .destroy = janus_multilive_destroy,

        .get_api_compatibility = janus_multilive_get_api_compatibility,
        .get_version = janus_multilive_get_version,
        .get_version_string = janus_multilive_get_version_string,
        .get_description = janus_multilive_get_description,
        .get_name = janus_multilive_get_name,
        .get_author = janus_multilive_get_author,
        .get_package = janus_multilive_get_package,


        .create_session = janus_multilive_create_session,
        .handle_message = janus_multilive_handle_message,
        .handle_admin_message = janus_multilive_handle_admin_message,
        .setup_media = janus_multilive_setup_media,
        .incoming_rtp = janus_multilive_incoming_rtp,
        .incoming_rtcp = janus_multilive_incoming_rtcp,
        .incoming_data = janus_multilive_incoming_data,
        .slow_link = janus_multilive_slow_link,
        .hangup_media = janus_multilive_hangup_media,
        .destroy_session = janus_multilive_destroy_session,
        .query_session = janus_multilive_query_session,
    );


/* Plugin creator */

janus_plugin *create(void){
    JANUS_LOG(LOG_VERB, "%s created!\n", JANUS_MULTILIVE_NAME);
    return &janus_multilive_plugin;
}


int janus_multilive_get_api_compatibility(void) {
    /* Important! This is what your plugin MUST always return: don't lie here or bad things will happen */
    return JANUS_PLUGIN_API_VERSION;
}

int janus_multilive_get_version(void) {
    return JANUS_MULTILIVE_VERSION;
}

const char *janus_multilive_get_version_string(void) {
    return JANUS_MULTILIVE_VERSION_STRING;
}

const char *janus_multilive_get_description(void) {
    return JANUS_MULTILIVE_DESCRIPTION;
}

const char *janus_multilive_get_name(void) {
    return JANUS_MULTILIVE_NAME;
}

const char *janus_multilive_get_author(void) {
    return JANUS_MULTILIVE_AUTHOR;
}

const char *janus_multilive_get_package(void) {
    return JANUS_MULTILIVE_PACKAGE;
}

/* Plugin implementation */
int janus_multilive_init(janus_callbacks *callback, const char *config_path) {

    if(callback == NULL || config_path == NULL) {
        /* Invalid arguments */
        return -1;
    }

    JANUS_LOG(LOG_INFO, "%s start initialize!\n", JANUS_MULTILIVE_NAME);

    /* Read configuration */
    char filename[255];
    g_snprintf(filename, 255, "%s/%s.jcfg", config_path, JANUS_MULTILIVE_PACKAGE);

    JANUS_LOG(LOG_INFO, "MultiLive plugin config file: %s\n", filename);



    JANUS_LOG(LOG_INFO, "%s initialized!\n", JANUS_MULTILIVE_NAME);
    return 0;
}


void janus_multilive_destroy(void) {
    JANUS_LOG(LOG_INFO, "%s destroyed!\n", JANUS_MULTILIVE_NAME);
}


//**********************************************

/* Error codes */
#define JANUS_MULTILIVE_ERROR_UNKNOWN_ERROR		499
#define JANUS_MULTILIVE_ERROR_NO_MESSAGE		421
#define JANUS_MULTILIVE_ERROR_INVALID_JSON		422
#define JANUS_MULTILIVE_ERROR_INVALID_REQUEST	423
#define JANUS_MULTILIVE_ERROR_JOIN_FIRST		424
#define JANUS_MULTILIVE_ERROR_ALREADY_JOINED	425
#define JANUS_MULTILIVE_ERROR_NO_SUCH_ROOM		426
#define JANUS_MULTILIVE_ERROR_ROOM_EXISTS		427
#define JANUS_MULTILIVE_ERROR_NO_SUCH_FEED		428
#define JANUS_MULTILIVE_ERROR_MISSING_ELEMENT	429
#define JANUS_MULTILIVE_ERROR_INVALID_ELEMENT	430
#define JANUS_MULTILIVE_ERROR_INVALID_SDP_TYPE	431
#define JANUS_MULTILIVE_ERROR_PUBLISHERS_FULL	432
#define JANUS_MULTILIVE_ERROR_UNAUTHORIZED		433
#define JANUS_MULTILIVE_ERROR_ALREADY_PUBLISHED	434
#define JANUS_MULTILIVE_ERROR_NOT_PUBLISHED		435
#define JANUS_MULTILIVE_ERROR_ID_EXISTS			436
#define JANUS_MULTILIVE_ERROR_INVALID_SDP		437

static janus_callbacks *gateway = NULL;

/* Parameter validation */
static struct janus_json_parameter request_parameters[] = {
        {"request", JSON_STRING, JANUS_JSON_PARAM_REQUIRED}
};
static struct janus_json_parameter adminkey_parameters[] = {
        {"admin_key", JSON_STRING, JANUS_JSON_PARAM_REQUIRED}
};
static struct janus_json_parameter create_parameters[] = {
        {"room", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"description", JSON_STRING, 0},
        {"is_private", JANUS_JSON_BOOL, 0},
        {"allowed", JSON_ARRAY, 0},
        {"secret", JSON_STRING, 0},
        {"pin", JSON_STRING, 0},
        {"require_pvtid", JANUS_JSON_BOOL, 0},
        {"bitrate", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"bitrate_cap", JANUS_JSON_BOOL, 0},
        {"fir_freq", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"publishers", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"audiocodec", JSON_STRING, 0},
        {"videocodec", JSON_STRING, 0},
        {"opus_fec", JANUS_JSON_BOOL, 0},
        {"video_svc", JANUS_JSON_BOOL, 0},
        {"audiolevel_ext", JANUS_JSON_BOOL, 0},
        {"audiolevel_event", JANUS_JSON_BOOL, 0},
        {"audio_active_packets", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"audio_level_average", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"videoorient_ext", JANUS_JSON_BOOL, 0},
        {"playoutdelay_ext", JANUS_JSON_BOOL, 0},
        {"transport_wide_cc_ext", JANUS_JSON_BOOL, 0},
        {"record", JANUS_JSON_BOOL, 0},
        {"rec_dir", JSON_STRING, 0},
        {"permanent", JANUS_JSON_BOOL, 0},
        {"notify_joining", JANUS_JSON_BOOL, 0},
};
static struct janus_json_parameter edit_parameters[] = {
        {"room", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE},
        {"secret", JSON_STRING, 0},
        {"new_description", JSON_STRING, 0},
        {"new_is_private", JANUS_JSON_BOOL, 0},
        {"new_secret", JSON_STRING, 0},
        {"new_pin", JSON_STRING, 0},
        {"new_require_pvtid", JANUS_JSON_BOOL, 0},
        {"new_bitrate", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"new_fir_freq", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"new_publishers", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"permanent", JANUS_JSON_BOOL, 0}
};
static struct janus_json_parameter room_parameters[] = {
        {"room", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE}
};
static struct janus_json_parameter destroy_parameters[] = {
        {"room", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE},
        {"permanent", JANUS_JSON_BOOL, 0}
};
static struct janus_json_parameter allowed_parameters[] = {
        {"room", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE},
        {"secret", JSON_STRING, 0},
        {"action", JSON_STRING, JANUS_JSON_PARAM_REQUIRED},
        {"allowed", JSON_ARRAY, 0}
};
static struct janus_json_parameter kick_parameters[] = {
        {"room", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE},
        {"secret", JSON_STRING, 0},
        {"id", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE}
};
static struct janus_json_parameter join_parameters[] = {
        {"room", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE},
        {"ptype", JSON_STRING, JANUS_JSON_PARAM_REQUIRED},
        {"audio", JANUS_JSON_BOOL, 0},
        {"video", JANUS_JSON_BOOL, 0},
        {"data", JANUS_JSON_BOOL, 0},
        {"bitrate", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"record", JANUS_JSON_BOOL, 0},
        {"filename", JSON_STRING, 0},
        {"token", JSON_STRING, 0}
};
static struct janus_json_parameter publish_parameters[] = {
        {"audio", JANUS_JSON_BOOL, 0},
        {"audiocodec", JSON_STRING, 0},
        {"video", JANUS_JSON_BOOL, 0},
        {"videocodec", JSON_STRING, 0},
        {"data", JANUS_JSON_BOOL, 0},
        {"bitrate", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"keyframe", JANUS_JSON_BOOL, 0},
        {"record", JANUS_JSON_BOOL, 0},
        {"filename", JSON_STRING, 0},
        {"display", JSON_STRING, 0},
        /* The following are just to force a renegotiation and/or an ICE restart */
        {"update", JANUS_JSON_BOOL, 0},
        {"restart", JANUS_JSON_BOOL, 0}
};
static struct janus_json_parameter rtp_forward_parameters[] = {
        {"room", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE},
        {"publisher_id", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE},
        {"video_port", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"video_rtcp_port", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"video_ssrc", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"video_pt", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"video_port_2", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"video_ssrc_2", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"video_pt_2", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"video_port_3", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"video_ssrc_3", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"video_pt_3", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"audio_port", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"audio_rtcp_port", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"audio_ssrc", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"audio_pt", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"data_port", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"host", JSON_STRING, JANUS_JSON_PARAM_REQUIRED},
        {"host_family", JSON_STRING, 0},
        {"simulcast", JANUS_JSON_BOOL, 0},
        {"srtp_suite", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"srtp_crypto", JSON_STRING, 0}
};
static struct janus_json_parameter stop_rtp_forward_parameters[] = {
        {"room", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE},
        {"publisher_id", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE},
        {"stream_id", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE}
};
static struct janus_json_parameter publisher_parameters[] = {
        {"id", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"display", JSON_STRING, 0}
};
static struct janus_json_parameter configure_parameters[] = {
        {"audio", JANUS_JSON_BOOL, 0},
        {"video", JANUS_JSON_BOOL, 0},
        {"data", JANUS_JSON_BOOL, 0},
        /* For VP8 (or H.264) simulcast */
        {"substream", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"temporal", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        /* For VP9 SVC */
        {"spatial_layer", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"temporal_layer", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        /* The following is to handle a renegotiation */
        {"update", JANUS_JSON_BOOL, 0},
};
static struct janus_json_parameter subscriber_parameters[] = {
        {"feed", JSON_INTEGER, JANUS_JSON_PARAM_REQUIRED | JANUS_JSON_PARAM_POSITIVE},
        {"private_id", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"close_pc", JANUS_JSON_BOOL, 0},
        {"audio", JANUS_JSON_BOOL, 0},
        {"video", JANUS_JSON_BOOL, 0},
        {"data", JANUS_JSON_BOOL, 0},
        {"offer_audio", JANUS_JSON_BOOL, 0},
        {"offer_video", JANUS_JSON_BOOL, 0},
        {"offer_data", JANUS_JSON_BOOL, 0},
        /* For VP8 (or H.264) simulcast */
        {"substream", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"temporal", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        /* For VP9 SVC */
        {"spatial_layer", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
        {"temporal_layer", JSON_INTEGER, JANUS_JSON_PARAM_POSITIVE},
};

/* Static configuration instance */
static janus_config *config = NULL;
static const char *config_folder = NULL;
static janus_mutex config_mutex = JANUS_MUTEX_INITIALIZER;

/* sruct */

typedef enum janus_multilive_p_type {
    janus_multilive_p_type_none = 0,
    janus_multilive_p_type_subscriber,			/* Generic subscriber */
    janus_multilive_p_type_publisher,			/* Participant (for receiving events) and optionally publisher */
} janus_multilive_p_type;


typedef struct janus_multilive {
    guint64 room_id;			/* Unique room ID */
    gchar *room_name;			/* Room description */
    gchar *room_secret;			/* Secret needed to manipulate (e.g., destroy) this room */
    gchar *room_pin;			/* Password needed to join this room, if any */
    gboolean is_private;		/* Whether this room is 'private' (as in hidden) or not */
    gboolean require_pvtid;		/* Whether subscriptions in this room require a private_id */
    int max_publishers;			/* Maximum number of concurrent publishers */
    uint32_t bitrate;			/* Global bitrate limit */
    gboolean bitrate_cap;		/* Whether the above limit is insormountable */
    uint16_t fir_freq;			/* Regular FIR frequency (0=disabled) */
    janus_audiocodec acodec[3];	/* Audio codec(s) to force on publishers */
    janus_videocodec vcodec[3];	/* Video codec(s) to force on publishers */
    gboolean do_opusfec;		/* Whether inband FEC must be negotiated (note: only available for Opus) */
    gboolean do_svc;			/* Whether SVC must be done for video (note: only available for VP9 right now) */
    gboolean audiolevel_ext;	/* Whether the ssrc-audio-level extension must be negotiated or not for new publishers */
    gboolean audiolevel_event;	/* Whether to emit event to other users about audiolevel */
    int audio_active_packets;	/* Amount of packets with audio level for checkup */
    int audio_level_average;	/* Average audio level */
    gboolean videoorient_ext;	/* Whether the video-orientation extension must be negotiated or not for new publishers */
    gboolean playoutdelay_ext;	/* Whether the playout-delay extension must be negotiated or not for new publishers */
    gboolean transport_wide_cc_ext;	/* Whether the transport wide cc extension must be negotiated or not for new publishers */
    gboolean record;			/* Whether the feeds from publishers in this room should be recorded */
    char *rec_dir;				/* Where to save the recordings of this room, if enabled */
    GHashTable *participants;	/* Map of potential publishers (we get subscribers from them) */
    GHashTable *private_ids;	/* Map of existing private IDs */
    volatile gint destroyed;	/* Whether this room has been destroyed */
    gboolean check_allowed;		/* Whether to check tokens when participants join (see below) */
    GHashTable *allowed;		/* Map of participants (as tokens) allowed to join */
    gboolean notify_joining;	/* Whether an event is sent to notify all participants if a new participant joins the room */
    janus_mutex mutex;			/* Mutex to lock this room instance */
    janus_refcount ref;			/* Reference counter for this room */
} janus_multilive;
static GHashTable *rooms;
static janus_mutex rooms_mutex = JANUS_MUTEX_INITIALIZER;
static char *admin_key = NULL;
static gboolean lock_rtpfwd = FALSE;

typedef struct janus_multilive_session {
    janus_plugin_session *handle;
    gint64 sdp_sessid;
    gint64 sdp_version;
    janus_multilive_p_type participant_type;
    gpointer participant;
    gboolean started;
    gboolean stopping;
    volatile gint hangingup;
    volatile gint destroyed;
    janus_mutex mutex;
    janus_refcount ref;
} janus_multilive_session;
static GHashTable *sessions;
static janus_mutex sessions_mutex = JANUS_MUTEX_INITIALIZER;



typedef struct janus_multilive_publisher {
    janus_multilive_session *session;
    janus_multilive *room;	/* Room */
    guint64 room_id;	/* Unique room ID */
    guint64 user_id;	/* Unique ID in the room */
    guint32 pvt_id;		/* This is sent to the publisher for mapping purposes, but shouldn't be shared with others */
    gchar *display;		/* Display name (just for fun) */
    gchar *sdp;			/* The SDP this publisher negotiated, if any */
    gboolean audio, video, data;		/* Whether audio, video and/or data is going to be sent by this publisher */
    janus_audiocodec acodec;	/* Audio codec this publisher is using */
    janus_videocodec vcodec;	/* Video codec this publisher is using */
    guint32 audio_pt;		/* Audio payload type (Opus) */
    guint32 video_pt;		/* Video payload type (depends on room configuration) */
    guint32 audio_ssrc;		/* Audio SSRC of this publisher */
    guint32 video_ssrc;		/* Video SSRC of this publisher */
    gboolean do_opusfec;	/* Whether this publisher is sending inband Opus FEC */
    uint32_t ssrc[3];		/* Only needed in case VP8 (or H.264) simulcasting is involved */
    char *rid[3];			/* Only needed if simulcasting is rid-based */
    int rid_extmap_id;		/* rid extmap ID */
    int framemarking_ext_id;			/* Frame marking extmap ID */
    guint8 audio_level_extmap_id;		/* Audio level extmap ID */
    guint8 video_orient_extmap_id;		/* Video orientation extmap ID */
    guint8 playout_delay_extmap_id;		/* Playout delay extmap ID */
    gboolean audio_active;
    gboolean video_active;
    int audio_dBov_level;		/* Value in dBov of the audio level (last value from extension) */
    int audio_active_packets;	/* Participant's number of audio packets to accumulate */
    int audio_dBov_sum;			/* Participant's accumulated dBov value for audio level*/
    gboolean talking;			/* Whether this participant is currently talking (uses audio levels extension) */
    gboolean data_active;
    gboolean firefox;	/* We send Firefox users a different kind of FIR */
    uint32_t bitrate;
    gint64 remb_startup;/* Incremental changes on REMB to reach the target at startup */
    gint64 remb_latest;	/* Time of latest sent REMB (to avoid flooding) */
    gint64 fir_latest;	/* Time of latest sent FIR (to avoid flooding) */
    gint fir_seq;		/* FIR sequence number */
    gboolean recording_active;	/* Whether this publisher has to be recorded or not */
    gchar *recording_base;	/* Base name for the recording (e.g., /path/to/filename, will generate /path/to/filename-audio.mjr and/or /path/to/filename-video.mjr */
    janus_recorder *arc;	/* The Janus recorder instance for this publisher's audio, if enabled */
    janus_recorder *vrc;	/* The Janus recorder instance for this user's video, if enabled */
    janus_recorder *drc;	/* The Janus recorder instance for this publisher's data, if enabled */
    janus_rtp_switching_context rec_ctx;
    janus_rtp_simulcasting_context rec_simctx;
    janus_mutex rec_mutex;	/* Mutex to protect the recorders from race conditions */
    GSList *subscribers;	/* Subscriptions to this publisher (who's watching this publisher)  */
    GSList *subscriptions;	/* Subscriptions this publisher has created (who this publisher is watching) */
    janus_mutex subscribers_mutex;
    GHashTable *rtp_forwarders;
    GHashTable *srtp_contexts;
    janus_mutex rtp_forwarders_mutex;
    int udp_sock; /* The udp socket on which to forward rtp packets */
    gboolean kicked;	/* Whether this participant has been kicked */
    volatile gint destroyed;
    janus_refcount ref;
} janus_multilive_publisher;
static guint32 janus_multilive_rtp_forwarder_add_helper(janus_multilive_publisher *p,
                                                        const gchar *host, int port, int rtcp_port, int pt, uint32_t ssrc,
                                                        gboolean simulcast, int srtp_suite, const char *srtp_crypto,
                                                        int substream, gboolean is_video, gboolean is_data);

typedef struct janus_multilive_subscriber {
    janus_multilive_session *session;
    janus_multilive *room;	/* Room */
    guint64 room_id;		/* Unique room ID */
    janus_multilive_publisher *feed;	/* Participant this subscriber is subscribed to */
    gboolean close_pc;		/* Whether we should automatically close the PeerConnection when the publisher goes away */
    guint32 pvt_id;			/* Private ID of the participant that is subscribing (if available/provided) */
    janus_sdp *sdp;			/* Offer we sent this listener (may be updated within renegotiations) */
    janus_rtp_switching_context context;	/* Needed in case there are publisher switches on this subscriber */
    janus_rtp_simulcasting_context sim_context;
    janus_vp8_simulcast_context vp8_context;
    gboolean audio, video, data;		/* Whether audio, video and/or data must be sent to this subscriber */
    /* As above, but can't change dynamically (says whether something was negotiated at all in SDP) */
    gboolean audio_offered, video_offered, data_offered;
    gboolean paused;
    gboolean kicked;	/* Whether this subscription belongs to a participant that has been kicked */
    /* The following are only relevant if we're doing VP9 SVC, and are not to be confused with plain
     * simulcast, which has similar info (substream/templayer) but in a completely different context */
    int spatial_layer, target_spatial_layer;
    gint64 last_spatial_layer[2];
    int temporal_layer, target_temporal_layer;
    volatile gint destroyed;
    janus_refcount ref;
} janus_multilive_subscriber;

/* Useful stuff */
static volatile gint initialized = 0, stopping = 0;

void janus_multilive_create_session(janus_plugin_session *handle, int *error) {
    if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
        *error = -1;
        return;
    }

    return;
}

static janus_multilive_session *janus_multilive_lookup_session(janus_plugin_session *handle) {
    janus_multilive_session *session = NULL;
    if (g_hash_table_contains(sessions, handle)) {
        session = (janus_multilive_session *)handle->plugin_handle;
    }
    return session;
}


static void janus_multilive_room_free(const janus_refcount *room_ref) {
    janus_multilive *room = janus_refcount_containerof(room_ref, janus_multilive, ref);
    /* This room can be destroyed, free all the resources */
    g_free(room->room_name);
    g_free(room->room_secret);
    g_free(room->room_pin);
    g_free(room->rec_dir);
    g_hash_table_destroy(room->participants);
    g_hash_table_destroy(room->private_ids);
    g_hash_table_destroy(room->allowed);
    g_free(room);
}

/* Freeing stuff */
static void janus_videoroom_subscriber_destroy(janus_multilive_subscriber *s) {
    if(s && g_atomic_int_compare_and_exchange(&s->destroyed, 0, 1))
    janus_refcount_decrease(&s->ref);
}


static void janus_multilive_subscriber_free(const janus_refcount *s_ref) {
    janus_multilive_subscriber *s = janus_refcount_containerof(s_ref, janus_multilive_subscriber, ref);
    /* This subscriber can be destroyed, free all the resources */
    janus_sdp_destroy(s->sdp);
    g_free(s);
}


struct janus_plugin_result *janus_multilive_handle_message(janus_plugin_session *handle, char *transaction, json_t *message, json_t *jsep) {
    if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
        return janus_plugin_result_new(JANUS_PLUGIN_ERROR, g_atomic_int_get(&stopping) ? "Shutting down" : "Plugin not initialized", NULL);

    /* Pre-parse the message */
    int error_code = 0;
    char error_cause[512];
    json_t *root = message;
    json_t *response = NULL;

    janus_mutex_lock(&sessions_mutex);
    janus_multilive_session *session = janus_multilive_lookup_session(handle);


    // set sesion to be NULL
    session = NULL;

    if(!session) {
        janus_mutex_unlock(&sessions_mutex);
        JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
        error_code = JANUS_MULTILIVE_ERROR_UNKNOWN_ERROR;
        g_snprintf(error_cause, 512, "%s", "No session associated with this handle...");
        goto plugin_response;
    }


    plugin_response:
    {
        if(error_code == 0 && !response) {
            error_code = JANUS_MULTILIVE_ERROR_UNKNOWN_ERROR;
            g_snprintf(error_cause, 512, "Invalid response");
        }
        if(error_code != 0) {
            /* Prepare JSON error event */
            json_t *event = json_object();
            json_object_set_new(event, "videoroom", json_string("event"));
            json_object_set_new(event, "error_code", json_integer(error_code));
            json_object_set_new(event, "error", json_string(error_cause));
            response = event;
        }
        if(root != NULL)
            json_decref(root);
        if(jsep != NULL)
            json_decref(jsep);
        g_free(transaction);

        if(session != NULL)
        janus_refcount_decrease(&session->ref);
        return janus_plugin_result_new(JANUS_PLUGIN_OK, NULL, response);
    }

}

static json_t *janus_videoroom_process_synchronous_request(janus_multilive_session *session, json_t *message) {

    return NULL;
}


json_t *janus_multilive_handle_admin_message(json_t *message) {
    /* Some requests (e.g., 'create' and 'destroy') can be handled via Admin API */
    int error_code = 0;
    char error_cause[512];
    json_t *response = NULL;

    JANUS_VALIDATE_JSON_OBJECT(message, request_parameters,
                               error_code, error_cause, TRUE,
                               JANUS_MULTILIVE_ERROR_MISSING_ELEMENT, JANUS_MULTILIVE_ERROR_INVALID_ELEMENT);
    if(error_code != 0)
        goto admin_response;
    json_t *request = json_object_get(message, "request");
    const char *request_text = json_string_value(request);
    if((response = janus_videoroom_process_synchronous_request(NULL, message)) != NULL) {
        /* We got a response, send it back */
        goto admin_response;
    } else {
        JANUS_LOG(LOG_VERB, "Unknown request '%s'\n", request_text);
        error_code = JANUS_MULTILIVE_ERROR_INVALID_REQUEST;
        g_snprintf(error_cause, 512, "Unknown request '%s'", request_text);
    }

    admin_response:
    {
        if(!response) {
            /* Prepare JSON error event */
            response = json_object();
            json_object_set_new(response, "streaming", json_string("event"));
            json_object_set_new(response, "error_code", json_integer(error_code));
            json_object_set_new(response, "error", json_string(error_cause));
        }
        return response;
    }

}

void janus_multilive_setup_media(janus_plugin_session *handle) {
    JANUS_LOG(LOG_INFO, "[%s-%p] WebRTC media is now available\n", JANUS_MULTILIVE_PACKAGE, handle);
    if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
        return;
    janus_mutex_lock(&sessions_mutex);
    janus_multilive_session *session = NULL; // janus_videoroom_lookup_session(handle);
    if(!session) {
        janus_mutex_unlock(&sessions_mutex);
        JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
        return;
    }

    janus_mutex_unlock(&sessions_mutex);
}

void janus_multilive_incoming_rtp(janus_plugin_session *handle, int video, char *buf, int len) {
    if(handle == NULL || g_atomic_int_get(&handle->stopped) || g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized) || !gateway)
        return;
    janus_multilive_session *session = (janus_multilive_session *)handle->plugin_handle;

    return;
}

void janus_multilive_incoming_rtcp(janus_plugin_session *handle, int video, char *buf, int len) {
    if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized))
        return;
    janus_multilive_session *session = (janus_multilive_session *)handle->plugin_handle;
    if(!session) {
        JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
        return;
    }

    return;

    if(g_atomic_int_get(&session->destroyed))
        return;

}

void janus_multilive_incoming_data(janus_plugin_session *handle, char *label, char *buf, int len) {
    if(handle == NULL || g_atomic_int_get(&handle->stopped) || g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized) || !gateway)
        return;
    if(buf == NULL || len <= 0)
        return;
    janus_multilive_session *session = (janus_multilive_session *)handle->plugin_handle;
    return;
}

void janus_multilive_slow_link(janus_plugin_session *handle, int uplink, int video) {
    /* The core is informing us that our peer got too many NACKs, are we pushing media too hard? */
    if(handle == NULL || g_atomic_int_get(&handle->stopped) || g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized) || !gateway)
        return;
    janus_mutex_lock(&sessions_mutex);
    janus_multilive_session *session = (janus_multilive_session *)handle->plugin_handle;
    return;
}

void janus_multilive_hangup_media(janus_plugin_session *handle) {
    JANUS_LOG(LOG_INFO, "[%s-%p] No WebRTC media anymore; %p %p\n", JANUS_MULTILIVE_PACKAGE, handle, handle->gateway_handle, handle->plugin_handle);

    return;
}

static void janus_multilive_hangup_subscriber(janus_multilive_subscriber * s) {

    JANUS_LOG(LOG_INFO, "[%s-%s] start.\n", JANUS_MULTILIVE_PACKAGE, __FUNCTION__ );

    /* Already hung up */
    if (!s->feed) {
        return;
    }

    return;
}



void janus_multilive_destroy_session(janus_plugin_session *handle, int *error) {
    if (g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
        *error = -1;
        return;
    }
    janus_mutex_lock(&sessions_mutex);
    janus_multilive_session *session = NULL; //janus_videoroom_lookup_session(handle);
    if (!session) {
        janus_mutex_unlock(&sessions_mutex);
        JANUS_LOG(LOG_ERR, "No VideoRoom session associated with this handle...\n");
        *error = -2;
        return;
    }
    if (g_atomic_int_get(&session->destroyed)) {
        janus_mutex_unlock(&sessions_mutex);
        JANUS_LOG(LOG_WARN, "VideoRoom session already marked as destroyed...\n");
        return;
    }

    return;
}

json_t *janus_multilive_query_session(janus_plugin_session *handle) {
    if (g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
        return NULL;
    }
    janus_mutex_lock(&sessions_mutex);
    janus_multilive_session *session = NULL; // janus_videoroom_lookup_session(handle);
    if (!session) {
        janus_mutex_unlock(&sessions_mutex);
        JANUS_LOG(LOG_ERR, "No session associated with this handle...\n");
        return NULL;
    }

    janus_mutex_unlock(&sessions_mutex);
    return NULL;
}