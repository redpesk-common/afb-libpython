/*
 * Copyright (C) 2015-2021 IoT.bzh Company
 * Author: Fulup Ar Foll <fulup@iot.bzh>
 *
 * $RP_BEGIN_LICENSE$
 * Commercial License Usage
 *  Licensees holding valid commercial IoT.bzh licenses may use this file in
 *  accordance with the commercial license agreement provided with the
 *  Software or, alternatively, in accordance with the terms contained in
 *  a written agreement between you and The IoT.bzh Company. For licensing terms
 *  and conditions see https://www.iot.bzh/terms-conditions. For further
 *  information use the contact form at https://www.iot.bzh/contact.
 *
 * GNU General Public License Usage
 *  Alternatively, this file may be used under the terms of the GNU General
 *  Public license version 3. This license is as published by the Free Software
 *  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
 *  of this file. Please review the following information to ensure the GNU
 *  General Public License requirements will be met
 *  https://www.gnu.org/licenses/gpl-3.0.html.
 * $RP_END_LICENSE$
 */
#pragma once

#include <libafb/sys/verbose.h>

#include "glue-afb.h"
#include <json-c/json.h>

#define GLUE_AFB_UID "#afb#"
#define SUBCALL_MAX_RPLY 8

typedef enum {
    PY_NO_ARG=0,
    PY_ONE_ARG=1,
    PY_TWO_ARG=2,
    PY_THREE_ARG=3,
    PY_FOUR_ARG=4,
    PY_FIVE_ARG=5,
    PY_SIX_ARG=6,
} pyNumberFixArgs;

typedef enum {
    GLUE_BINDER_MAGIC=936714582,
    GLUE_API_MAGIC=852951357,
    GLUE_RQT_MAGIC=684756123,
    GLUE_EVT_MAGIC=894576231,
    GLUE_VCDATA_MAGIC=684756123,
    GLUE_TIMER_MAGIC=4628170,
    GLUE_LOCK_MAGIC=379645852,
    GLUE_HANDLER_MAGIC=579315863,
    GLUE_SCHED_MAGIC=73498127,
} pyGlueMagicsE;

// compiled AFB verb context data
typedef struct {
    int magic;
    const char *verb;
    const char *info;
    PyObject   *funcP;
} pyVerbDataT;

struct PyBinderHandleS {
    AfbBinderHandleT *afb;
    PyObject *configP;
};

struct PySchedWaitS {
    PyObject *callbackP;
    PyObject *userdataP;
    struct afb_sched_lock *afb;
    afb_api_t  apiv4;
    long status;
};

struct PyApiHandleS {
    afb_api_t  afb;
    PyObject *ctrlCb;
    PyObject *configP;
};

struct PyRqtHandleS {
    struct PyApiHandleS *api;
    pyVerbDataT  *vcData;
    int responded;
    afb_req_t afb;
};

struct PyTimerHandleS {
    const char *uid;
    afb_timer_t afb;
    PyObject *callbackP;
    PyObject *configP;
    PyObject *userdataP;
    int usage;
};

struct PyEvtHandleS {
    const char *uid;
    const char *name;
    afb_event_t afb;
    PyObject *configP;
    afb_api_t apiv4;
    int count;
};

struct PyHandlerHandleS {
    const char *uid;
    PyObject *callbackP;
    PyObject *configP;
    PyObject *userdataP;
    afb_api_t apiv4;
    int count;
};

typedef struct {
    pyGlueMagicsE magic;
    //PyThreadState *pyState;
    //PyGILState_STATE glState;
    union {
        struct PyBinderHandleS binder;
        struct PyEvtHandleS evt;
        struct PyApiHandleS api;
        struct PyRqtHandleS rqt;
        struct PyTimerHandleS timer;
        struct PySchedWaitS lock;
        struct PyHandlerHandleS handler;
    };
} AfbHandleT;

typedef struct {
    int magic;
    AfbHandleT *handle;
    PyObject *callbackP;
    PyObject *userdataP;
} GlueHandleCbT;

extern AfbHandleT *afbMain;
