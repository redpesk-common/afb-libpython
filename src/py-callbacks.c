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

void GlueEvtHandlerCb (void *userdata, const char *evtName, unsigned nparams, afb_data_x4_t const params[], afb_api_t api) {
    const char *errorMsg = NULL;
    int err;
    afb_data_t argsD[nparams];
    json_object *argsJ[nparams];

    GlueHandleT *glue= (GlueHandleT*) afb_api_get_userdata(api);
    assert (glue);

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

        // extract Python callable from callbackJ
        vcbData->callback = json_object_get_userdata (callbackJ);
        if (!vcbData->callback || !PyCallable_Check((PyObject*)vcbData->callback)) {
            errorMsg = "(hoops) event has no callable function";
            goto OnErrorExit;
        }
    }

    // prepare calling argument list
    PyThreadState_Swap(GetPrivateData());
    PyObject *argsP= PyTuple_New(nparams+GLUE_THREE_ARG);
    PyTuple_SetItem (argsP, 0, PyCapsule_New(glue, GLUE_AFB_UID, NULL));
    PyTuple_SetItem (argsP, 1, PyUnicode_FromString(evtName));
    if (vcbData->userdata) {
        Py_IncRef((PyObject*)vcbData->userdata);
        PyTuple_SetItem (argsP, 2, (PyObject*)vcbData->userdata);
    } else {
        PyTuple_SetItem (argsP, 2, Py_None);
    }

    // retreive event data and convert them to json
    for (int idx = 0; idx < nparams; idx++)
    {
        err = afb_data_convert(params[idx], &afb_type_predefined_json_c, &argsD[idx]);
        if (err)
        {
            errorMsg = "fail converting input params to json";
            goto OnErrorExit;
        }
        argsJ[idx]  = afb_data_ro_pointer(argsD[idx]);
        PyTuple_SetItem(argsP, idx+GLUE_THREE_ARG, jsonToPyObj(argsJ[idx]));
    }

    // call python event handler code
    PyObject *resultP= PyObject_Call ((PyObject*)vcbData->callback, argsP, NULL);
    if (!resultP) {
        errorMsg="Event handler callback fail";
        goto OnErrorExit;
    }

    // free json_object
    for (int idx=0; idx < nparams; idx++) {
        afb_data_unref(argsD[idx]);
        json_object_put(argsJ[idx]);
    }
    return;

OnErrorExit:
    GLUE_DBG_ERROR(glue, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return;
}

void GlueFreeHandleCb(PyObject *capculeP) {
   GlueHandleT *handle=   PyCapsule_GetPointer(capculeP, GLUE_AFB_UID);
   if (!handle) goto OnErrorExit;

    switch (handle->magic) {
        case GLUE_EVT_MAGIC:
            if ( handle->evt.configP) Py_DecRef( handle->evt.configP);
            break;
        case GLUE_API_MAGIC:
            break;
        case GLUE_LOCK_MAGIC:
            if (handle->lock.userdataP) Py_DecRef(handle->lock.userdataP);
            if (handle->lock.callbackP) Py_DecRef(handle->lock.callbackP);
        default:
            goto OnErrorExit;

    }
    free (handle);

OnErrorExit:
    ERROR ("try to release a non glue handle");
}

void GlueJobStartCb (int signum, void *userdata, struct afb_sched_lock *afbLock) {

    const char *errorMsg = NULL;
    GlueHandleT *ctx= (GlueHandleT*)userdata;
    assert (ctx && ctx->magic == GLUE_LOCK_MAGIC);
    ctx->lock.afb= afbLock;

    // create a fake API for waitCB
    GlueHandleT *glue = calloc(1,sizeof(GlueHandleT));
    glue->magic= GLUE_API_MAGIC;
    glue->api.afb=ctx->lock.apiv4;

    // prepare calling argument list
    PyThreadState_Swap(GetPrivateData());
    PyObject *argsP= PyTuple_New(GLUE_THREE_ARG);
    PyTuple_SetItem (argsP, 0, PyCapsule_New(glue, GLUE_AFB_UID, GlueFreeHandleCb));
    PyTuple_SetItem (argsP, 1, PyCapsule_New(ctx, GLUE_AFB_UID, NULL));
    PyTuple_SetItem (argsP, 2, ctx->lock.userdataP);

    PyObject *resultP= PyObject_Call (ctx->lock.callbackP, argsP, NULL);
    if (!resultP) {
        errorMsg="async callback fail";
        goto OnErrorExit;
    }
    return;

OnErrorExit:
    GLUE_DBG_ERROR(ctx, errorMsg);
}

void GlueSchedTimeoutCb (int signum, void *userdata) {
    const char *errorMsg = NULL;
    GlueHandleCbT *ctx= (GlueHandleCbT*)userdata;
    assert (ctx && ctx->magic == GLUE_SCHED_MAGIC);
    PyThreadState_Swap(GetPrivateData());

    // timer not cancel
    if (signum != SIGABRT) {
        // prepare calling argument list
        PyThreadState_Swap(GetPrivateData());
        PyObject *argsP= PyTuple_New(GLUE_TWO_ARG);
        PyTuple_SetItem (argsP, 0, PyCapsule_New(ctx->handle, GLUE_AFB_UID, NULL));
        PyTuple_SetItem (argsP, 1, ctx->userdataP);

        PyObject *resultP= PyObject_Call (ctx->callbackP, argsP, NULL);
        if (!resultP) {
            errorMsg="async callback fail";
            goto OnErrorExit;
        }
    }
    Py_DECREF (ctx->callbackP);
    if (ctx->userdataP) Py_DECREF (ctx->userdataP);
    free (ctx);
    return;

OnErrorExit:
    GLUE_DBG_ERROR(ctx->handle, errorMsg);
    Py_DECREF (ctx->callbackP);
    if (ctx->userdataP) Py_DECREF (ctx->userdataP);
    free (ctx);
}

void GlueVerbCb(afb_req_t afbRqt, unsigned nparams, afb_data_t const params[]) {
    const char *errorMsg = NULL;
    int err;
    afb_data_t argsD[nparams];
    json_object *argsJ[nparams];

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
    PyTuple_SetItem (argsP, 0, PyCapsule_New(glue, GLUE_AFB_UID, NULL));

    // retreive input arguments and convert them to json
    for (int idx = 0; idx < nparams; idx++)
    {
        err = afb_data_convert(params[idx], &afb_type_predefined_json_c, &argsD[idx]);
        if (err)
        {
            errorMsg = "fail converting input params to json";
            goto OnErrorExit;
        }
        argsJ[idx]  = afb_data_ro_pointer(argsD[idx]);
        PyTuple_SetItem(argsP, idx+1, jsonToPyObj(argsJ[idx]));
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
        for (int idx=0; idx <nparams; idx++) {
            //json_object_put(argsJ[idx]); ?? TBD Jose
            afb_data_unref(argsD[idx]);
        }
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
        PyObject *resultP= PyObject_CallFunction (glue->api.ctrlCb, "Os", PyCapsule_New(glue, GLUE_AFB_UID, NULL), state);
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
int GlueStartupCb(void *callback, void *userdata)
{
    PyObject *callbackP= (PyObject*) callback;
    GlueHandleT *ctx = (GlueHandleT *)userdata;
    assert(ctx && ctx->magic == GLUE_BINDER_MAGIC);
    int status=0;

    if (callbackP)
    {
        // get callback name

        // in 3.10 should be replace by PyObject_CallOneArg(callbackP, handleP);
        PyThreadState_Swap(GetPrivateData());
        PyObject *argsP= PyTuple_New(GLUE_ONE_ARG);
        PyTuple_SetItem (argsP, 0, PyCapsule_New(userdata, GLUE_AFB_UID, NULL));
        PyObject *resultP= PyObject_Call (callbackP, argsP, NULL);
        if (!resultP) goto OnErrorExit;
        status= (int)PyLong_AsLong(resultP);
        Py_DECREF (resultP);
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


static void GluePcallFunc (void *userdata, int status, unsigned nreplies, afb_data_t const replies[]) {
    PyAsyncCtxT *ctx= (PyAsyncCtxT*) userdata;
    const char *errorMsg = NULL;

    // subcall was refused
    if (AFB_IS_BINDER_ERRNO(status)) {
        errorMsg= afb_error_text(status);
        goto OnErrorExit;
    }

    // prepare calling argument list
    PyThreadState_Swap(GetPrivateData());
    PyObject *argsP= PyTuple_New(nreplies+GLUE_THREE_ARG);
    PyTuple_SetItem (argsP, 0, PyCapsule_New(ctx->glue, GLUE_AFB_UID, NULL));
    PyTuple_SetItem (argsP, 1, PyLong_FromLong((long)status));
    PyTuple_SetItem (argsP, 2, ctx->userdataP);

    errorMsg= PyPushAfbReply (argsP, 3, nreplies, replies);
    if (errorMsg) goto OnErrorExit;

    PyObject *resultP= PyObject_Call (ctx->callbackP, argsP, NULL);
    if (!resultP) {
        errorMsg="async callback fail";
        goto OnErrorExit;
    }

    // free afb request and glue
    if (ctx->userdataP != Py_None) Py_DECREF(ctx->userdataP);
    Py_DECREF(ctx->callbackP);
    Py_DECREF(argsP);
    free (ctx);
    return;

OnErrorExit: {
    afb_data_t reply;
    json_object *errorJ = PyJsonDbg(errorMsg);
    GLUE_AFB_WARNING(ctx->glue, "python=%s", json_object_get_string(errorJ));
    afb_create_data_raw(&reply, AFB_PREDEFINED_TYPE_JSON_C, errorJ, 0, (void *)json_object_put, errorJ);
    GlueAfbReply(ctx->glue, -1, 1, &reply);
    free (ctx);
    }
}

void GlueApiSubcallCb (void *userdata, int status, unsigned nreplies, afb_data_t const replies[], afb_api_t api) {
    GluePcallFunc (userdata, status, nreplies, replies);
}

void GlueRqtSubcallCb (void *userdata, int status, unsigned nreplies, afb_data_t const replies[], afb_req_t req) {
    GluePcallFunc (userdata, status, nreplies, replies);
}

void GlueTimerClear(GlueHandleT *glue) {

    afb_timer_unref (glue->timer.afb);
    Py_DecRef(glue->timer.configP);
    glue->timer.usage--;

    // free timer pyState and ctx
    if (glue->timer.usage <= 0) {
       glue->magic=0;
       Py_DecRef(glue->timer.userdataP);
       Py_DecRef(glue->timer.callbackP);
       free(glue);
    }
}

void GlueTimerCb (afb_timer_x4_t timer, void *userdata, int decount) {
    const char *errorMsg=NULL;
    GlueHandleT *ctx= (GlueHandleT*)userdata;
    assert (ctx && ctx->magic == GLUE_TIMER_MAGIC);
    long status=0;

    PyThreadState_Swap(GetPrivateData());
    PyObject *argsP= PyTuple_New(GLUE_THREE_ARG);
    PyTuple_SetItem (argsP, 0, PyCapsule_New(ctx, GLUE_AFB_UID, NULL));
    PyTuple_SetItem (argsP, 1, ctx->timer.userdataP);
    if (ctx->timer.userdataP) Py_IncRef(ctx->timer.userdataP);
    PyTuple_SetItem (argsP, 2, PyLong_FromLong(decount));

    PyObject *resultP= PyObject_Call (ctx->timer.callbackP, argsP, NULL);
    if (!resultP) {
        errorMsg="timer callback fail";
        goto OnErrorExit;
    }
    if (resultP != Py_None) {
        if (!PyLong_Check(resultP)) {
            errorMsg="TimerCB returned status should be integer";
            goto OnErrorExit;
        }
        status= PyLong_AsLong(resultP);
    }

    // check for last timer interation
    if (decount == 1 || status != 0) goto OnUnrefExit;

    return;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
OnUnrefExit:
    GlueTimerClear(ctx);
}
