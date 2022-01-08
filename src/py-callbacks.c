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

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <wrap-json.h>

#include <glue-afb.h>
#include <glue-utils.h>

#include "py-afb.h"
#include "py-utils.h"
#include "py-callbacks.h"

void GlueEvtHandlerCb (void *userdata, const char *evtName, unsigned nparams, afb_data_x4_t const params[], afb_api_t api) {
    AfbHandleT *ctx= (AfbHandleT*) userdata;
    assert (ctx && ctx->magic==GLUE_HANDLER_MAGIC);
    const char *errorMsg = NULL;
    int err;
    afb_data_t argsD[nparams];
    json_object *argsJ[nparams];

    // prepare calling argument list
    PyObject *argsP= PyTuple_New(nparams+PY_THREE_ARG);
    PyTuple_SetItem (argsP, 0, PyCapsule_New(ctx, GLUE_AFB_UID, NULL));
    PyTuple_SetItem (argsP, 1, PyUnicode_FromString(evtName));
    PyTuple_SetItem (argsP, 2, ctx->handler.userdataP);
    if (ctx->handler.userdataP) Py_IncRef(ctx->handler.userdataP);

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
        PyTuple_SetItem(argsP, idx+PY_THREE_ARG, jsonToPyObj(argsJ[idx]));
    }

    // call python event handler code
    PyObject *resultP= PyObject_Call (ctx->handler.callbackP, argsP, NULL);
    if (!resultP) {
        errorMsg="Event handler callback fail";
        goto OnErrorExit;
    }

    // free json_object
    for (int idx=0; idx < nparams; idx++) json_object_put(argsJ[idx]);

    // update statistic
    ctx->handler.count++;
    return;

OnErrorExit:
    PY_DBG_ERROR(ctx, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return;
}

void GlueFreeHandleCb(PyObject *capculeP) {
   AfbHandleT *handle=   PyCapsule_GetPointer(capculeP, GLUE_AFB_UID);
   if (!handle) goto OnErrorExit;

    switch (handle->magic) {
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

void GlueSchedWaitCb (int signum, void *userdata, struct afb_sched_lock *afbLock) {

    const char *errorMsg = NULL;
    AfbHandleT *ctx= (AfbHandleT*)userdata;
    assert (ctx && ctx->magic == GLUE_LOCK_MAGIC);
    ctx->lock.afb= afbLock;

    // create a fake API for waitCB
    AfbHandleT *glue = calloc(1,sizeof(AfbHandleT));
    glue->magic= GLUE_API_MAGIC;
    glue->api.afb=ctx->lock.apiv4;

    // prepare calling argument list
    PyObject *argsP= PyTuple_New(PY_THREE_ARG);
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
    PY_DBG_ERROR(ctx, errorMsg);
}

void GlueSchedTimeoutCb (int signum, void *userdata) {
    const char *errorMsg = NULL;
    GlueHandleCbT *ctx= (GlueHandleCbT*)userdata;
    assert (ctx && ctx->magic == GLUE_SCHED_MAGIC);

    // timer not cancel
    if (signum != SIGABRT) {
        // prepare calling argument list
        PyObject *argsP= PyTuple_New(PY_TWO_ARG);
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
    PY_DBG_ERROR(ctx->handle, errorMsg);
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
    AfbHandleT *glue= PyRqtNew(afbRqt);

    // on first call we compile configJ to boost following py api/verb calls
    json_object *configJ = afb_req_get_vcbdata(afbRqt);
    if (!configJ)
    {
        errorMsg = "fail get verb config";
        goto OnErrorExit;
    }

    pyVerbDataT *pyVcData = json_object_get_userdata(configJ);
    if (!pyVcData)
    {
        json_object *funcJ=NULL;
        pyVcData = calloc(1, sizeof(pyVerbDataT));
        json_object_set_userdata(configJ, pyVcData, PyFreeJsonCtx);
        pyVcData->magic = GLUE_VCDATA_MAGIC;

        err = wrap_json_unpack(configJ, "{ss so s?s}", "verb", &pyVcData->verb, "callback", &funcJ, "info", &pyVcData->info);
        if (err)
        {
            errorMsg = "invalid verb json config";
            goto OnErrorExit;
        }

        // extract Python callable from funcJ
        pyVcData->funcP = json_object_get_userdata (funcJ);
        if (!pyVcData->funcP || !PyCallable_Check(pyVcData->funcP)) {
            errorMsg = "(hoops) verb has no callable function";
            goto OnErrorExit;
        }
    }
    else
    {
        if (pyVcData->magic != GLUE_VCDATA_MAGIC)
        {
            errorMsg = "fail to converting json to py table";
            goto OnErrorExit;
        }
    }
    glue->rqt.vcData = pyVcData;

    // prepare calling argument list
    PyObject *argsP= PyTuple_New(nparams+PY_ONE_ARG);
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

    PyObject *resultP= PyObject_Call (pyVcData->funcP, argsP, NULL);
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
            GlueReply(glue, status, count-1, reply);
            for (int idx=0; idx <nparams; idx++) json_object_put(argsJ[idx]);

        } else if (PyLong_Check(resultP)) {
            status= PyLong_AsLong(resultP);
            GlueReply(glue, status, 0, NULL);
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
        GlueReply(glue, -1, 1, &reply);
    }
}

int GlueCtrlCb(afb_api_t apiv4, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata) {
    AfbHandleT *ctx= (AfbHandleT*) userdata;
    static int orphan=0;
    const char *state;
    int status=0;

    // assert userdata validity
    assert (ctx && ctx->magic == GLUE_API_MAGIC);


    switch (ctlid) {
    case afb_ctlid_Root_Entry:
        state="root";
        break;

    case afb_ctlid_Pre_Init:
        state="config";
        ctx->api.afb= apiv4;
        break;

    case afb_ctlid_Init:
        state="ready";
        break;

    case afb_ctlid_Class_Ready:
        state="class";
        break;

    case afb_ctlid_Orphan_Event:
        GLUE_AFB_WARNING (ctx, "Orphan event=%s count=%d", ctlarg->orphan_event.name, orphan++);
        state="orphan";
        break;

    case afb_ctlid_Exiting:
        state="exit";
        break;

    default:
        break;
    }

    if (!ctx->api.ctrlCb) {
        GLUE_AFB_WARNING(ctx,"GlueCtrlCb: No init callback state=[%s]", state);

    } else {

        // effectively exec PY script code
        GLUE_AFB_NOTICE(ctx,"GlueCtrlCb: state=[%s]", state);
        PyObject *resultP= PyObject_CallFunction (ctx->api.ctrlCb, "Os", PyCapsule_New(ctx, GLUE_AFB_UID, NULL), state);
        if (!resultP) goto OnErrorExit;
        status= (int)PyLong_AsLong(resultP);
        Py_DECREF (resultP);
    }
    return status;

OnErrorExit:
    PY_DBG_ERROR(afbMain, "fail api control");
    return -1;
}


// this routine execute within mainloop userdata when binder is ready to go
int GlueStartupCb(void *callback, void *userdata)
{
    PyObject *callbackP= (PyObject*) callback;
    AfbHandleT *ctx = (AfbHandleT *)userdata;
    assert(ctx && ctx->magic == GLUE_BINDER_MAGIC);
    int status=0;

    if (callbackP)
    {
        // in 3.10 should be replace by PyObject_CallOneArg(callbackP, handleP);
        PyObject *argsP= PyTuple_New(PY_ONE_ARG);
        PyTuple_SetItem (argsP, 0, PyCapsule_New(userdata, GLUE_AFB_UID, NULL));
        PyObject *resultP= PyObject_Call (callbackP, argsP, NULL);
        if (!resultP) goto OnErrorExit;
        status= (int)PyLong_AsLong(resultP);
        Py_DECREF (resultP);
    }
    return status;

OnErrorExit:
    PY_DBG_ERROR(afbMain, "fail mainloop startup");
    return -1;
}

void GlueInfoCb(afb_req_t afbRqt, unsigned nparams, afb_data_t const params[])
{
    afb_api_t apiv4 = afb_req_get_api(afbRqt);
    afb_data_t reply;

    // retreive interpreteur from API
    AfbHandleT *glue = afb_api_get_userdata(apiv4);
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
            json_object_array_add(verbsJ, (json_object *)afbVerb->vcbdata);
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
    PyObject *argsP= PyTuple_New(nreplies+PY_THREE_ARG);
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
    GlueReply(ctx->glue, -1, 1, &reply);
    }
}

void GlueApiSubcallCb (void *userdata, int status, unsigned nreplies, afb_data_t const replies[], afb_api_t api) {
    GluePcallFunc (userdata, status, nreplies, replies);
}

void GlueRqtSubcallCb (void *userdata, int status, unsigned nreplies, afb_data_t const replies[], afb_req_t req) {
    GluePcallFunc (userdata, status, nreplies, replies);
}

void GlueTimerClear(AfbHandleT *glue) {

    afb_timer_unref (glue->timer.afb);
    Py_DecRef(glue->timer.configP);
    glue->timer.usage--;

    // free timer luaState and ctx
    if (glue->timer.usage <= 0) {
       glue->magic=0;
       Py_DecRef(glue->timer.userdataP);
       Py_DecRef(glue->timer.callbackP);
       free(glue);
    }
}

void GlueTimerCb (afb_timer_x4_t timer, void *userdata, int decount) {
    const char *errorMsg=NULL;
    AfbHandleT *ctx= (AfbHandleT*)userdata;
    assert (ctx && ctx->magic == GLUE_TIMER_MAGIC);
    long status=0;

    PyObject *argsP= PyTuple_New(PY_TWO_ARG);
    PyTuple_SetItem (argsP, 0, PyCapsule_New(ctx, GLUE_AFB_UID, NULL));
    PyTuple_SetItem (argsP, 1, ctx->timer.userdataP);
    if (ctx->timer.userdataP) Py_IncRef(ctx->timer.userdataP);

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
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
OnUnrefExit:
    GlueTimerClear(ctx);
}
