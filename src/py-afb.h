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
#include <semaphore.h>

#include <rp-utils/rp-verbose.h>
#include <json-c/json.h>
#include <afb-binder.h>

#define GLUE_AFB_UID "#afb#"
#define SUBCALL_MAX_RPLY 8

typedef struct {
    char *uid;
    PyObject *callbackP;
    PyObject *userdataP;
} GlueAsyncCtxT;

struct PyBinderHandleS {
    AfbBinderHandleT *afb;
    //sem_t sem;
    PyObject *configP;
    PyThreadState *pyState;
};

struct PyJobHandleS {
    struct afb_sched_lock *afb;
    afb_api_t  apiv4;
    long status;
    GlueAsyncCtxT async;
};

struct PyApiHandleS {
    afb_api_t  afb;
    PyObject *ctrlCb;
    PyObject *configP;
};

struct PyRqtHandleS {
    struct PyApiHandleS *api;
    int replied;
    afb_req_t afb;
};

struct PyEventHandleS {
    afb_event_t afb;
    afb_api_t apiv4;
    char *pattern;
    PyObject *configP;
    GlueAsyncCtxT async;
};

struct PyTimerHandleS {
    afb_timer_t afb;
    afb_api_t apiv4;
    PyObject *configP;
    GlueAsyncCtxT async;
};

struct PyPostHandleS {
    afb_api_t apiv4;
    PyObject *configP;
    GlueAsyncCtxT async;
};

typedef struct {
    AfbMagicTagE magic;
    int usage;
    union {
        struct PyBinderHandleS binder;
        struct PyApiHandleS api;
        struct PyRqtHandleS rqt;
        struct PyTimerHandleS timer;
        struct PyEventHandleS event;
        struct PyPostHandleS post;
        struct PyJobHandleS job;
    };
} GlueHandleT;

typedef struct  {
    AfbMagicTagE magic;
    GlueHandleT *glue;
    GlueAsyncCtxT async;
} GlueCallHandleT;


extern GlueHandleT *afbMain;
