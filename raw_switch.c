/*
   Copyright (C) 2016 Benjamin Abendroth

   raw_switch module for pilight.

   This file contains code from the pilight project.
*/

/*
	Copyright (C) 2014 CurlyMo

	This file is part of pilight.

	pilight is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

	pilight is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with pilight. If not, see	<http://www.gnu.org/licenses/>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>

#include "../../core/pilight.h"
#include "../../core/common.h"
#include "../../core/dso.h"
#include "../../core/log.h"
#include "../protocol.h"
#include "raw_switch.h"

#define MIN_FOOTER_LENGTH 9999

typedef struct settings_t {
   int id;
   int tolerance;
   int *up_code;
   int up_code_len;
   int *down_code;
   int down_code_len;
   struct settings_t *next;
} settings_t;

static struct settings_t *settings = NULL;

static void createMessage(int id, int state) {
	raw_switch->message = json_mkobject();
	json_append_member(raw_switch->message, "id", json_mknumber(id, 1));
	if (state) {
		json_append_member(raw_switch->message, "state", json_mkstring("on"));
	} else {
		json_append_member(raw_switch->message, "state", json_mkstring("off"));
	}
}

static int validate(void) {
   return 0;
}

static int matchPulses(int *pulses_a, int *pulses_b, int len, int tolerance) {
   if (! len)
      return 1;

   if (pulses_a[len-1] > MIN_FOOTER_LENGTH || pulses_b[len-1] > MIN_FOOTER_LENGTH) {
      --len; // skip footer pulse
   }

   while (len--) {
      float percent_diff;

      if (pulses_a[len] < pulses_b[len]) {
         percent_diff = (float) pulses_a[len] / pulses_b[len];
      } else {
         percent_diff = (float) pulses_b[len] / pulses_a[len];
      }

      percent_diff = (float) 100 - (percent_diff * 100);

      if ((int) percent_diff > tolerance) {
         return 0;
      }
   }

   return 1;
}

static void parseCode(void) {
   struct settings_t *setting = settings;

   while (setting) {
      if (setting->up_code_len == raw_switch->rawlen) {
         if (matchPulses(setting->up_code, raw_switch->raw, raw_switch->rawlen, settings->tolerance)) {
		      createMessage(setting->id, 1);
            return;
         }
      }
      if (setting->down_code_len == raw_switch->rawlen) {
         if (matchPulses(setting->down_code, raw_switch->raw, raw_switch->rawlen, settings->tolerance)) {
		      createMessage(setting->id, 0);
            return;
         }
      }

      setting = setting->next;
   }
}

static int createCode(JsonNode *code) {
	int id = -1;
	int state = -1;
	double itmp = 0;

	if (! json_find_number(code, "id", &itmp))
		id = (int)round(itmp);

	if (! json_find_number(code, "repeats", &itmp)) {
      raw_switch->txrpt = itmp;
   } else {
      raw_switch->txrpt = 10;
   }

	if (! json_find_number(code, "down", &itmp))
		state = 0;
	else if (! json_find_number(code, "up", &itmp))
		state = 1;

	if (id == -1 || state == -1) {
		logprintf(LOG_ERR, "raw_switch: insufficient number of arguments");
		return EXIT_FAILURE;
	} else {
		createMessage(id, state);
	}

	char *rcode = NULL;
	char **array = NULL;
	unsigned int i = 0;

   if (state) {
      if (json_find_string(code, "up-code", &rcode) != 0) {
         logprintf(LOG_ERR, "raw: insufficient number of arguments: missing up-code");
         return EXIT_FAILURE;
      }
   }
   else {
      if (json_find_string(code, "down-code", &rcode) != 0) {
         logprintf(LOG_ERR, "raw: insufficient number of arguments: missing down-code");
         return EXIT_FAILURE;
      }
   }

	raw_switch->rawlen = explode(rcode, " ", &array);
	for (i = 0; i < raw_switch->rawlen; i++) {
		raw_switch->raw[i] = atoi(array[i]);
	}
	array_free(&array, raw_switch->rawlen);

	return EXIT_SUCCESS;
}

static void printHelp(void) {
	printf("\t -t --on\t\t\tsend an on signal\n");
	printf("\t -f --off\t\t\tsend an off signal\n");
	printf("\t -u --up-code\t\t\tup signal code\n");
	printf("\t -d --down-code\t\t\tdown signal code\n");
	printf("\t -r --repeats\t\t\trepeat signal n times\n");
	printf("\t -i --id=id\t\t\tcontrol a device with this id\n");
}

static struct threadqueue_t *initDev(JsonNode *jdevice) {
   char *timings = NULL;
   char **array = NULL;
   int i;
   double itmp;

	struct settings_t *lnode = MALLOC(sizeof(struct settings_t));
   lnode->tolerance = 30;
   lnode->up_code = NULL;
   lnode->up_code_len = 0;
   lnode->down_code = NULL;
   lnode->down_code_len = 0;

   JsonNode *device_ids = json_find_member(jdevice, "id");
   JsonNode *device_id;
   json_foreach(device_id, device_ids) {
      if (! json_find_number(device_id, "id", &itmp)) {
         lnode->id = (int)round(itmp);
      }
   }

   if (! json_find_number(jdevice, "tolerance", &itmp)) {
      lnode->tolerance = (int)round(itmp);
   }

	if (! json_find_string(jdevice, "up-code", &timings)) {
      lnode->up_code_len = explode(timings, " ", &array);
      lnode->up_code = MALLOC(sizeof(int) * lnode->up_code_len);

      for (i = 0; i < lnode->up_code_len; ++i)
         lnode->up_code[i] = atoi(array[i]);
      array_free(&array, lnode->up_code_len);
      array = NULL;
   }

	if (! json_find_string(jdevice, "down-code", &timings)) {
      lnode->down_code_len = explode(timings, " ", &array);
      lnode->down_code = MALLOC(sizeof(int) * lnode->down_code_len);

      for (i = 0; i < lnode->down_code_len; ++i)
         lnode->down_code[i] = atoi(array[i]);
      array_free(&array, lnode->down_code_len);
      array = NULL;
   }

   lnode->next = settings;
   settings = lnode;

   return NULL;
}

static void threadGC(void) {
	struct settings_t *tmp;
	while(settings) {
		tmp = settings;
		if (tmp->up_code)   FREE(tmp->up_code);
		if (tmp->down_code) FREE(tmp->down_code);
		settings = settings->next;
		FREE(tmp);
	}
	if(settings != NULL) {
		FREE(settings);
	}
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
void rawSwitchInit(void) {
	protocol_register(&raw_switch);
	protocol_set_id(raw_switch, "raw_switch");
	protocol_device_add(raw_switch, "raw_switch", "raw switches");
	raw_switch->devtype = SWITCH;
	raw_switch->hwtype = RF433;

	options_add(&raw_switch->options, 't', "on", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);
	options_add(&raw_switch->options, 'f', "off", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);
	options_add(&raw_switch->options, 'u', "up-code", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_STRING, NULL, NULL);
	options_add(&raw_switch->options, 'd', "down-code", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_STRING, NULL, NULL);
	options_add(&raw_switch->options, 'r', "repeats", OPTION_OPT_VALUE, DEVICES_SETTING, JSON_NUMBER, NULL, NULL);
	options_add(&raw_switch->options,   0, "tolerance", OPTION_OPT_VALUE, DEVICES_SETTING, JSON_NUMBER, NULL, NULL);
	options_add(&raw_switch->options, 'i', "id", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^([0-9]{1,})$");

	options_add(&raw_switch->options, 0, "readonly", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");
	options_add(&raw_switch->options, 0, "confirm", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");

	raw_switch->validate=&validate;
	raw_switch->parseCode=&parseCode;
	raw_switch->createCode=&createCode;
	raw_switch->printHelp=&printHelp;
	raw_switch->initDev=&initDev;
	raw_switch->threadGC=&threadGC;
}

#if defined(MODULE) && !defined(_WIN32)
void compatibility(struct module_t *module) {
	module->name = "raw_switch";
	module->version = "1.0";
	module->reqversion = "6.0";
	module->reqcommit = "1";
}

void init(void) {
	rawSwitchInit();
}
#endif
