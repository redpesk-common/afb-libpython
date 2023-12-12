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

#include <json-c/json.h>
#include <libafb-binder.h>
#include <libafb/misc/afb-verbose.h>

#define GLUE_AFB_UID "#afb#"
#define SUBCALL_MAX_RPLY 8

typedef enum {
    GLUE_UNKNOWN_MAGIC_TAG=0, /**< Default unset object identity */
    GLUE_BINDER_MAGIC_TAG,    /**< Identify BINDER objects */
    GLUE_API_MAGIC_TAG,       /**< Identify API objects */
    GLUE_RQT_MAGIC_TAG,       /**< Identify REQUEST objects */
    GLUE_EVT_MAGIC_TAG,       /**< Identify EVENT objects */
    GLUE_TIMER_MAGIC_TAG,     /**< Identify TIMER objects */
    GLUE_JOB_MAGIC_TAG,       /**< Identify JOB objects */
    GLUE_POST_MAGIC_TAG,      /**< Identify POSTED JOB objects */
    GLUE_CALL_MAGIC_TAG,      /**< Identify ASYNCHRONOUS CALL objects */
}
    GlueMagicTagE;

typedef struct {
    char *uid;
    PyObject *callbackP;
    PyObject *userdataP;
} GlueAsyncCtxT;

typedef struct {
    AfbBinderHandleT *afb;
    //sem_t sem;
    PyObject *configP;
    PyThreadState *pyState;
} PyBinderHandleT;

typedef struct {
    struct afb_sched_lock *afb;
    afb_api_t  apiv4;
    long status;
    GlueAsyncCtxT async;
} PyJobHandleT;

typedef struct {
    afb_api_t  afb;
    PyObject *ctrlCb;
    PyObject *configP;
} PyApiHandleT;

typedef struct {
    struct PyApiHandleS *api;
    int replied;
    afb_req_t afb;
} PyRqtHandleT;

typedef struct {
    afb_event_t afb;
    afb_api_t apiv4;
    char *pattern;
    PyObject *configP;
    GlueAsyncCtxT async;
} PyEventHandleT;

typedef struct {
    afb_timer_t afb;
    afb_api_t apiv4;
    PyObject *configP;
    GlueAsyncCtxT async;
} PyTimerHandleT;

typedef struct {
    afb_api_t apiv4;
    PyObject *configP;
    GlueAsyncCtxT async;
} PyPostHandleT;

typedef struct {
    GlueMagicTagE magic;
    int usage;
    union {
        PyBinderHandleT binder;
        PyApiHandleT api;
        PyRqtHandleT rqt;
        PyTimerHandleT timer;
        PyEventHandleT event;
        PyPostHandleT post;
        PyJobHandleT job;
    };
} GlueHandleT;

typedef struct  {
    GlueMagicTagE magic;
    GlueHandleT *glue;
    GlueAsyncCtxT async;
} GlueCallHandleT;


extern GlueHandleT *afbMain;
