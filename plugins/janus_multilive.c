//
// Created by hiler on 2020/1/13.
//




#include "plugin.h"


#include "../debug.h"


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

static janus_plugin janus_multilive_plugin = JANUS_PLUGIN_INIT(
        .init = janus_multilive_init,
        .destroy = janus_multilive_destroy,
        );


/* Plugin creator */

janus_plugin *create(void){

    JANUS_LOG(LOG_VERB, "%s created!\n", JANUS_MULTILIVE_NAME);

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



    JANUS_LOG(LOG_INFO, "%s initialized!\n", JANUS_MULTILIVE_NAME);
    return 0;
}


void janus_multilive_destroy(void) {
    JANUS_LOG(LOG_INFO, "%s destroyed!\n", JANUS_MULTILIVE_NAME);
}

