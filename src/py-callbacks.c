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
#include <Python.h>
#include <frameobject.h>

#include <libafb/core/afb-data.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <wrap-json.h>

#include <glue-afb.h>
#include <glue-utils.h>

#include "longobject.h"
#include "py-afb.h"
#include "py-utils.h"
#include "py-callbacks.h"

void GlueFreeHandleCb(GlueHandleT *handle) {
    if (!handle) goto OnErrorExit;
    handle->usage--;

    switch (handle->magic) {
        case GLUE_EVT_MAGIC:
            if (handle->usage < 0) {
                free (handle->event.pattern);
                Py_DecRef(handle->event.configP);
                if ( handle->event.configP) Py_DecRef(handle->event.configP);
            }
            break;
        case GLUE_JOB_MAGIC:
            if (handle->usage < 0) {
                Py_DecRef(handle->job.async.callbackP);
                if (handle->job.async.userdataP) Py_DecRef(handle->job.async.userdataP);
                free (handle->job.async.uid);
            }
            break;
        case GLUE_TIMER_MAGIC:
            afb_timer_unref (handle->timer.afb);
            if (handle->usage < 0) {
                Py_DecRef(handle->timer.async.callbackP);
                if (handle->timer.async.userdataP)Py_DecRef(handle->timer.async.userdataP);
            }
            break;

        case GLUE_API_MAGIC:    // as today removing API is not supported bu libafb
        case GLUE_RQT_MAGIC:    // rqt live cycle is handle directly by libafb
        case GLUE_BINDER_MAGIC: // afbmain should never be released
            handle->usage=1; // static handle
            break;

        default:
            goto OnErrorExit;
    }
    if (handle->usage < 0) free (handle);
    return;

OnErrorExit:
    ERROR ("try to release a protected handle type=%s", AfbMagicToString(handle->magic));
}

void GlueFreeCapculeCb(PyObject *capculeP) {
   GlueHandleT *handle= PyCapsule_GetPointer(capculeP, GLUE_AFB_UID);
   GlueFreeHandleCb (handle);
}

void GlueApiVerbCb(afb_req_t afbRqt, unsigned nparams, afb_data_t const params[]) {
    const char *errorMsg = NULL;
    int err;

    // new afb request
    GlueHandleT *glue= PyRqtNew(afbRqt);

    // on first call we compile configJ to boost following py api/verb calls
    AfbVcbDataT *vcbData= afb_req_get_vcbdata(afbRqt);
    if (vcbData->magic != (void*)AfbAddVerbs) {
        errorMsg = "(hoops) verb invalid vcbData handle";
        goto OnErrorExit;
    }

    // on first call we need to retreive original callback object from configJ
    if (!vcbData->callback)
    {
        json_object *callbackJ=json_object_object_get(vcbData->configJ, "callback");
        if (!callbackJ) {
            errorMsg = "(hoops) verb no callback defined";
            goto OnErrorExit;
        }

        // extract Python callable from callbackJ
        vcbData->callback = json_object_get_userdata (callbackJ);
        if (!vcbData->callback || !PyCallable_Check(vcbData->callback)) {
            errorMsg = "(hoops) verb has no callable function";
            goto OnErrorExit;
        }
    }

    // prepare calling argument list
    PyThreadState_Swap(GetPrivateData());
    PyObject *argsP= PyTuple_New(nparams+GLUE_ONE_ARG);
    glue->usage++;
    PyTuple_SetItem (argsP, 0, PyCapsule_New(glue, GLUE_AFB_UID, GlueFreeCapculeCb));

    // retreive input arguments and convert them to json
    for (int idx = 0; idx < nparams; idx++)
    {
        afb_data_t argD;
        json_object *argJ;
        err = afb_data_convert(params[idx], &afb_type_predefined_json_c, &argD);
        if (err)
        {
            errorMsg = "fail converting input params to json";
            goto OnErrorExit;
        }
        argJ  = afb_data_ro_pointer(argD);
        PyTuple_SetItem(argsP, idx+1, jsonToPyObj(argJ));
        afb_data_unref(argD);
    }

    PyObject *resultP= PyObject_Call ((PyObject*)vcbData->callback, argsP, NULL);
    if (!resultP) goto OnErrorExit;
    Py_DECREF(argsP);

    if (resultP) {
        PyObject *slotP;
        json_object *slotJ;
        long status, count;

        if (PyTuple_Check(resultP)) {
            count = PyTuple_GET_SIZE(resultP);
            afb_data_t reply[count];
            slotP=  PyTuple_GetItem(resultP,0);
            if (!PyLong_Check(slotP)) {
                errorMsg= "Response 1st element should be status/integer";
                goto OnErrorExit;
            }
            status= PyLong_AsLong(slotP);
            for (long idx=0; idx < count-1; idx++) {
                slotP= PyTuple_GetItem(resultP,idx+1);
                slotJ= pyObjToJson(slotP);
                if (!slotJ)
                {
                    errorMsg = "(hoops) not json convertible response";
                    goto OnErrorExit;
                }
                afb_create_data_raw(&reply[idx], AFB_PREDEFINED_TYPE_JSON_C, slotJ, 0, (void *)json_object_put, slotJ);
            }

            // respond request and free ressources.
            GlueAfbReply(glue, status, count-1, reply);

        } else if (PyLong_Check(resultP)) {
            status= PyLong_AsLong(resultP);
            GlueAfbReply(glue, status, 0, NULL);
        }
        //for (int idx=0; idx <nparams; idx++) afb_data_unref(argsD[idx]);
        Py_DECREF (resultP);
    }

    return;

OnErrorExit:
    {
        afb_data_t reply;
        json_object *errorJ = PyJsonDbg(errorMsg);
        GLUE_AFB_WARNING(glue, "verb=[%s] python=%s", afb_req_get_called_verb(afbRqt), json_object_get_string(errorJ));
        afb_create_data_raw(&reply, AFB_PREDEFINED_TYPE_JSON_C, errorJ, 0, (void *)json_object_put, errorJ);
        GlueAfbReply(glue, -1, 1, &reply);
    }
}

int GlueCtrlCb(afb_api_t apiv4, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata) {
    GlueHandleT *glue= (GlueHandleT*) userdata;
    static int orphan=0;
    const char *state;
    int status=0;

    // assert userdata validity
    assert (glue && glue->magic == GLUE_API_MAGIC);


    switch (ctlid) {
    case afb_ctlid_Root_Entry:
        state="root";
        break;

    case afb_ctlid_Pre_Init:
        state="config";
        glue->api.afb= apiv4;
        break;

    case afb_ctlid_Init:
        state="ready";
        break;

    case afb_ctlid_Class_Ready:
        state="class";
        break;

    case afb_ctlid_Orphan_Event:
        GLUE_AFB_WARNING (glue, "Orphan event=%s count=%d", ctlarg->orphan_event.name, orphan++);
        state="orphan";
        break;

    case afb_ctlid_Exiting:
        state="exit";
        break;

    default:
        break;
    }

    if (!glue->api.ctrlCb) {
        GLUE_AFB_WARNING(glue,"GlueCtrlCb: No init callback state=[%s]", state);

    } else {

        // effectively exec PY script code
        GLUE_AFB_NOTICE(glue,"GlueCtrlCb: state=[%s]", state);
        PyThreadState_Swap(GetPrivateData());
        glue->usage++;
        PyObject *resultP= PyObject_CallFunction (glue->api.ctrlCb, "Os", PyCapsule_New(glue, GLUE_AFB_UID, GlueFreeCapculeCb), state);
        if (!resultP) goto OnErrorExit;
        status= (int)PyLong_AsLong(resultP);
        Py_DECREF (resultP);
    }
    return status;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, "fail api control");
    return -1;
}

// this routine execute within mainloop userdata when binder is ready to go
int GlueStartupCb(void *config, void *userdata)
{
    GlueAsyncCtxT *async= (GlueAsyncCtxT*) config;
    GlueHandleT *ctx = (GlueHandleT *)userdata;
    assert(ctx && ctx->magic == GLUE_BINDER_MAGIC);
    int status=0;

    if (async->callbackP)
    {
        PyObject *argsP;
        PyThreadState_Swap(GetPrivateData());
        argsP= PyTuple_New(GLUE_TWO_ARG);

        if (!async->userdataP) PyTuple_SetItem (argsP, 1, Py_None);
        else PyTuple_SetItem (argsP, 1, async->userdataP);

        PyTuple_SetItem (argsP, 0, PyCapsule_New(userdata, GLUE_AFB_UID, NULL));
        PyObject *resultP= PyObject_Call (async->callbackP, argsP, NULL);
        if (!resultP) goto OnErrorExit;
        status= (int)PyLong_AsLong(resultP);
        Py_DECREF (resultP);
        Py_DECREF (async->callbackP);
        if (async->userdataP) Py_DECREF (async->userdataP);
        free (async);
    }
    return status;

OnErrorExit: {
    GLUE_AFB_WARNING(afbMain, "Mainloop killed");
    return -1;
}
}

void GlueInfoCb(afb_req_t afbRqt, unsigned nparams, afb_data_t const params[])
{
    afb_api_t apiv4 = afb_req_get_api(afbRqt);
    afb_data_t reply;

    // retreive interpreteur from API
    GlueHandleT *glue = afb_api_get_userdata(apiv4);
    assert(glue->magic == GLUE_API_MAGIC);

    // extract uid + info from API config
    const char  *uid, *info=NULL;
    PyObject *uidP = PyDict_GetItemString (glue->api.configP, "uid");
    PyObject *infoP= PyDict_GetItemString (glue->api.configP, "info");

    uid= PyUnicode_AsUTF8(uidP);
    if (infoP) info=PyUnicode_AsUTF8(infoP);

    json_object *metaJ;
    wrap_json_pack (&metaJ, "{ss ss*}"
        ,"uid", uid
        ,"info", info
    );

    // extract info from each verb
    json_object *verbsJ = json_object_new_array();
    for (int idx = 0; idx < afb_api_v4_verb_count(apiv4); idx++)
    {
        const afb_verb_t *afbVerb = afb_api_v4_verb_at(apiv4, idx);
        if (!afbVerb) break;
        if (afbVerb->vcbdata != glue) {
            AfbVcbDataT *vcbData= afbVerb->vcbdata;
            if (vcbData->magic != AfbAddVerbs) continue;
            json_object_array_add(verbsJ, vcbData->configJ);
            json_object_get(verbsJ);
        }
    }
    // info devtool require a group array
    json_object *groupsJ, *infoJ;
    wrap_json_pack(&groupsJ, "[{so}]", "verbs", verbsJ);

    wrap_json_pack(&infoJ, "{so so}", "metadata", metaJ, "groups", groupsJ);
    afb_create_data_raw(&reply, AFB_PREDEFINED_TYPE_JSON_C, infoJ, 0, (void *)json_object_put, infoJ);
    afb_req_reply(afbRqt, 0, 1, &reply);
    return;
}

static void GluePcallFunc (GlueHandleT *glue, GlueAsyncCtxT *async, const char *label, int status, unsigned nreplies, afb_data_t const replies[]) {
    const char *errorMsg = "internal-error";

    // subcall was refused
    if (AFB_IS_BINDER_ERRNO(status)) {
        errorMsg= afb_error_text(status);
        goto OnErrorExit;
    }

    // prepare calling argument list
    PyThreadState_Swap(GetPrivateData());
    PyObject *argsP= PyTuple_New(nreplies+GLUE_THREE_ARG);
    glue->usage++;
    PyTuple_SetItem (argsP, 0, PyCapsule_New(glue, GLUE_AFB_UID, GlueFreeCapculeCb));
    if (label) PyTuple_SetItem (argsP, 1, PyUnicode_FromString(label));
    else PyTuple_SetItem (argsP, 1, PyLong_FromLong((long)status));

    // add userdata if any (Fulup Py_IncRef needed ???)
    if (!async->userdataP) PyTuple_SetItem (argsP, 2, Py_None);
    else {
        PyTuple_SetItem (argsP, 2, async->userdataP);
        Py_IncRef(async->userdataP);
    }

    // push event data if any
    errorMsg= PyPushAfbReply (argsP, 3, nreplies, replies);
    if (errorMsg) goto OnErrorExit;

    PyObject *resultP= PyObject_Call (async->callbackP, argsP, NULL);
    if (!resultP) {
        errorMsg="function-fail";
        goto OnErrorExit;
    }
    return;

OnErrorExit: {
    const char*uid= async->uid;
    json_object *errorJ = PyJsonDbg(errorMsg);
    if (glue->magic != GLUE_RQT_MAGIC)  GLUE_AFB_WARNING(glue, "uid=%s info=%s error=%s", uid, errorMsg,  json_object_get_string(errorJ));
    else {
        afb_data_t reply;
        GLUE_AFB_WARNING(glue, "%s", json_object_get_string(errorJ));
        afb_create_data_raw(&reply, AFB_PREDEFINED_TYPE_JSON_C, errorJ, 0, (void *)json_object_put, errorJ);
        GlueAfbReply(glue, -1, 1, &reply);
    }
  }
}

void GlueJobStartCb (int signum, void *userdata, struct afb_sched_lock *afbLock) {

    GlueHandleT *glue= (GlueHandleT*)userdata;
    assert (glue->magic == GLUE_JOB_MAGIC);

    glue->job.afb= afbLock;
    GluePcallFunc (glue, &glue->job.async, NULL, signum, 0, NULL);
}

// used when declaring event with the api
void GlueApiEventCb (void *userdata, const char *label, unsigned nparams, afb_data_x4_t const params[], afb_api_t api) {
    const char *errorMsg;
    GlueHandleT *glue= (GlueHandleT*) afb_api_get_userdata(api);
    assert (glue->magic == GLUE_API_MAGIC);

    // on first call we compile configJ to boost following py api/verb calls
    AfbVcbDataT *vcbData= userdata;
    if (vcbData->magic != (void*)AfbAddVerbs) {
        errorMsg = "(hoops) event invalid vcbData handle";
        goto OnErrorExit;
    }

    // on first call we need to retreive original callback object from configJ
    if (!vcbData->callback)
    {
        json_object *callbackJ=json_object_object_get(vcbData->configJ, "callback");
        if (!callbackJ) {
            errorMsg = "(hoops) event no callback defined";
            goto OnErrorExit;
        }

        // create an async structure to use gluePcallFunc and extract callbackR from json userdata
        GlueAsyncCtxT *async= calloc (1, sizeof(GlueAsyncCtxT));
        async->callbackP=  json_object_get_userdata (callbackJ);
        if (!async->callbackP || !PyCallable_Check(async->callbackP)) {
            errorMsg = "(hoops) event has no callable function";
            goto OnErrorExit;
        }
        // extract Python callable from callbackJ
        vcbData->callback = async;
    }

    //GluePcallFunc (glue, (GlueAsyncCtxT*)vcbData->callback, label, 0, nparams, params);
    GluePcallFunc (glue, (GlueAsyncCtxT*)vcbData->callback, label, 0, 0, NULL);
    return;
OnErrorExit:
    GLUE_DBG_ERROR(glue, errorMsg);
}

// user when declaring event with libafb.evthandler
void GlueEventCb (void *userdata, const char *label, unsigned nparams, afb_data_x4_t const params[], afb_api_t api) {
    GlueHandleT *glue= (GlueHandleT*) userdata;
    assert (glue->magic == GLUE_EVT_MAGIC);
    GluePcallFunc (glue, &glue->event.async, label, 0, nparams, params);
}

void GlueTimerCb (afb_timer_x4_t timer, void *userdata, int decount) {
   GlueHandleT *glue= (GlueHandleT*) userdata;
   assert (glue->magic == GLUE_TIMER_MAGIC);
   GluePcallFunc (glue, &glue->timer.async, NULL, decount, 0, NULL);
}

void GlueJobPostCb (int signum, void *userdata) {
    GlueCallHandleT *handle= (GlueCallHandleT*) userdata;
    assert (handle->magic == GLUE_POST_MAGIC);
    if (!signum) GluePcallFunc (handle->glue, &handle->async, NULL, signum, 0, NULL);
    free (handle->async.uid);
    free (handle);
}

void GlueApiSubcallCb (void *userdata, int status, unsigned nreplies, afb_data_t const replies[], afb_api_t api) {
    GlueCallHandleT *handle= (GlueCallHandleT*) userdata;
    assert (handle->magic == GLUE_CALL_MAGIC);
    GluePcallFunc (handle->glue, &handle->async, NULL, status, nreplies, replies);
    free (handle->async.uid);
    free (handle);
}

void GlueRqtSubcallCb (void *userdata, int status, unsigned nreplies, afb_data_t const replies[], afb_req_t req) {
    GlueCallHandleT *handle= (GlueCallHandleT*) userdata;
    assert (handle->magic == GLUE_CALL_MAGIC);
    GluePcallFunc (handle->glue, &handle->async, NULL, status, nreplies, replies);
    free (handle->async.uid);
    free (handle);
}
