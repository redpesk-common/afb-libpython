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

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <wrap-json.h>

#include "py-afb.h"
#include "py-utils.h"
#include "py-callbacks.h"

#include <glue-afb.h>
#include <glue-utils.h>

// global afbMain glue
AfbHandleT *afbMain=NULL;


static PyObject * PyPrintInfo(PyObject *self, PyObject *argsP)
{
    PyPrintMsg(AFB_SYSLOG_LEVEL_INFO, self, argsP);
    Py_RETURN_NONE;
}

static PyObject * PyPrintError(PyObject *self, PyObject *argsP)
{
    PyPrintMsg(AFB_SYSLOG_LEVEL_ERROR, self, argsP);
    Py_RETURN_NONE;
}

static PyObject * PyPrintWarning(PyObject *self, PyObject *argsP)
{
    PyPrintMsg(AFB_SYSLOG_LEVEL_WARNING, self, argsP);
    Py_RETURN_NONE;
}

static PyObject * PyPrintNotice(PyObject *self, PyObject *argsP)
{
    PyPrintMsg(AFB_SYSLOG_LEVEL_NOTICE, self, argsP);
    Py_RETURN_NONE;
}

static PyObject * PyPrintDebug(PyObject *self, PyObject *argsP)
{
    PyPrintMsg(AFB_SYSLOG_LEVEL_DEBUG, self, argsP);
    Py_RETURN_NONE;
}

static PyObject *GlueBinderConf(PyObject *self, PyObject *argsP)
{
    const char *errorMsg="syntax: binder(config)";
    if (afbMain) {
        errorMsg="(hoops) binder(config) already loaded";
        goto OnErrorExit;
    }

    // allocate afbMain glue and parse config to jsonC
    afbMain= calloc(1, sizeof(AfbHandleT));
    afbMain->magic= GLUE_BINDER_MAGIC;

    if (!PyArg_ParseTuple(argsP, "O", &afbMain->binder.configP)) {
        errorMsg= "invalid config object";
        goto OnErrorExit;
    }
    json_object *configJ= pyObjToJson(afbMain->binder.configP);
    if (!configJ) {
        errorMsg="json incompatible config";
        goto OnErrorExit;
    }

    errorMsg= AfbBinderConfig(configJ, &afbMain->binder.afb);
    if (errorMsg) goto OnErrorExit;

    // return afbMain glue as a Python capcule glue
    PyObject *capcule= PyCapsule_New(afbMain, GLUE_AFB_UID, NULL);
    return capcule;

OnErrorExit:
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    PyErr_Print();
    Py_RETURN_NONE;
}

static PyObject *GlueApiCreate(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: apiadd (config)";

    if (!afbMain) {
        errorMsg= "Should use libafb.binder() first";
        goto OnErrorExit;
    }

    AfbHandleT *glue = calloc(1, sizeof(AfbHandleT));
    glue->magic = GLUE_API_MAGIC;

    if (!PyArg_ParseTuple(argsP, "O", &glue->api.configP)) goto OnErrorExit;
    json_object *configJ= pyObjToJson(glue->api.configP);
    if (!configJ) {
        errorMsg="json incompatible config";
        goto OnErrorExit;
    }

    const char *afbApiUri = NULL;
    wrap_json_unpack(configJ, "{s?s}", "uri", &afbApiUri);
    if (afbApiUri)
    {
        // imported shadow api
        errorMsg = AfbApiImport(afbMain->binder.afb, configJ);
    }
    else
    {
        // check if control python api function is defined
        glue->api.ctrlCb= PyDict_GetItemString(glue->api.configP, "control");
        if (glue->api.ctrlCb) {
            if (!PyCallable_Check(glue->api.ctrlCb)) {
                errorMsg="APi control func defined but but callable";
                goto OnErrorExit;
            }
            errorMsg = AfbApiCreate(afbMain->binder.afb, configJ, &glue->api.afb, GlueCtrlCb, GlueInfoCb, GlueVerbCb, GlueEvtHandlerCb, glue);
        } else {
            errorMsg = AfbApiCreate(afbMain->binder.afb, configJ, &glue->api.afb, NULL, GlueInfoCb, GlueVerbCb, GlueEvtHandlerCb, glue);
        }
    }
    if (errorMsg)
        goto OnErrorExit;

    // return api glue
    PyObject *capcule= PyCapsule_New(glue, GLUE_AFB_UID, NULL);
    return capcule;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    PyErr_Print();
    Py_RETURN_NONE;
}

// this routine execute within mainloop context when binder is ready to go
static PyObject* GlueMainLoop(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: mainloop([callback])";
    int status;


    long count = PyTuple_GET_SIZE(argsP);
    if (count > PY_ONE_ARG) goto OnErrorExit;

    PyObject *callbackP= PyTuple_GetItem(argsP,0);
    if (callbackP) {
        if (!PyCallable_Check(callbackP)) goto OnErrorExit;
        Py_IncRef(callbackP);
    }

    // main loop only return when binder startup func return status!=0
    GLUE_AFB_NOTICE(afbMain, "Entering binder mainloop");
    status = AfbBinderStart(afbMain->binder.afb, callbackP, GlueStartupCb, afbMain);

    return PyLong_FromLong ((long)status);

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    PyErr_Print();
    Py_RETURN_NONE;
}

static PyObject* PyGetConfig(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: config(handle[,key])";
    PyObject *capculeP, *configP, *resultP, *slotP, *keyP=NULL;
    if (!PyArg_ParseTuple(argsP, "O|O", &capculeP, &keyP)) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(capculeP, GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    switch (glue->magic)
    {
    case GLUE_API_MAGIC:
        configP = glue->api.configP;
        break;
    case GLUE_BINDER_MAGIC:
        configP = glue->binder.configP;
        break;
    case GLUE_TIMER_MAGIC:
        configP = glue->timer.configP;
        break;
    case GLUE_EVT_MAGIC:
        configP = glue->evt.configP;
        break;
    default:
        errorMsg = "GlueGetConfig: unsupported py/afb handle";
        goto OnErrorExit;
    }

    if (!configP) {
        errorMsg= "PyHandle config missing";
        goto OnErrorExit;
    }

    if (!keyP)
    {
        resultP= configP;
        Py_INCREF(resultP);
    }
    else
    {
        slotP= PyDict_GetItem(configP, keyP);
        if (!slotP)
        {
            errorMsg = "GlueGetConfig: unknown config key";
            goto OnErrorExit;
        }
        resultP= slotP;
        Py_INCREF(resultP);
    }
    return resultP;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    PyErr_Print();
    Py_RETURN_NONE;
}

static PyObject* GlueRespond(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: response(rqt, status, [arg1 ... argn])";
    PyObject *slotP;
    long status, count=0;

    count = PyTuple_GET_SIZE(argsP);
    afb_data_t reply[count];
    if (count < PY_TWO_ARG) goto OnErrorExit;

    slotP=  PyTuple_GetItem(argsP,0);
    AfbHandleT* glue= PyCapsule_GetPointer(slotP, GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_RQT_MAGIC) goto OnErrorExit;

    json_object *slotJ;
    slotP=  PyTuple_GetItem(argsP,1);
    if (!PyLong_Check(slotP)) {
        errorMsg= "syntax: invalid status should be integer";
        goto OnErrorExit;
    }
    status= PyLong_AsLong(slotP);

    for (long idx=0; idx < count-PY_TWO_ARG; idx++) {
        slotP= PyTuple_GetItem(argsP,idx+PY_TWO_ARG);
        slotJ= pyObjToJson(slotP);
        if (!slotJ)
        {
            errorMsg = "(hoops) not json convertible response";
            goto OnErrorExit;
        }
        afb_create_data_raw(&reply[idx], AFB_PREDEFINED_TYPE_JSON_C, slotJ, 0, (void *)json_object_put, slotJ);
    }

    // respond request and free ressources.
    GlueReply(glue, status, count-2, reply);
    Py_RETURN_NONE;

OnErrorExit:
    {
        afb_data_t reply;
        json_object *errorJ = PyJsonDbg(errorMsg);
        GLUE_AFB_WARNING(glue, "python=%s", json_object_get_string(errorJ));
        afb_create_data_raw(&reply, AFB_PREDEFINED_TYPE_JSON_C, errorJ, 0, (void *)json_object_put, errorJ);
        GlueReply(glue, -1, 1, &reply);
    }
    Py_RETURN_NONE;
}


static PyObject* GlueBindingLoad(PyObject *self, PyObject *argsP)
{
    const char *errorMsg =  "syntax: binding(config)";
    PyObject *configP;

    if (!PyArg_ParseTuple(argsP, "O", &configP)) goto OnErrorExit;

    json_object *configJ= pyObjToJson(configP);
    if (!configJ) goto OnErrorExit;

    errorMsg = AfbBindingLoad(afbMain->binder.afb, configJ);
    if (errorMsg) goto OnErrorExit;

    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueAsyncCall(PyObject *self, PyObject *argsP)
{
    const char *errorMsg= "syntax: callasync(handle, api, verb, callback, context, ...)";
    long index=0;

    // parse input arguments
    long count = PyTuple_GET_SIZE(argsP);
    afb_data_t params[count];
    if (count < PY_FIVE_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    const char* apiname= PyUnicode_AsUTF8(PyTuple_GetItem(argsP,1));
    if (!apiname) goto OnErrorExit;

    const char* verbname= PyUnicode_AsUTF8(PyTuple_GetItem(argsP,2));
    if (!verbname) goto OnErrorExit;

    PyObject *callbackP=PyTuple_GetItem(argsP,3);
    // check callback is a valid function
    if (!PyCallable_Check(callbackP)) goto OnErrorExit;

    PyObject *userdataP =PyTuple_GetItem(argsP,4);
    if (userdataP != Py_None) Py_IncRef(userdataP);

    // retreive subcall optional argument(s)
    for (index= 0; index < count-PY_FIVE_ARG; index++)
    {
        json_object *argsJ = pyObjToJson(PyTuple_GetItem(argsP,index+PY_FIVE_ARG));
        if (!argsJ)
        {
            errorMsg = "invalid input argument type";
            goto OnErrorExit;
        }
        afb_create_data_raw(&params[index], AFB_PREDEFINED_TYPE_JSON_C, argsJ, 0, (void *)json_object_put, argsJ);
    }

    PyAsyncCtxT *cbHandle= calloc(1,sizeof(PyAsyncCtxT));
    cbHandle->glue= glue;
    cbHandle->callbackP= callbackP;
    cbHandle->userdataP= userdataP;
    Py_IncRef(cbHandle->callbackP);

    switch (glue->magic) {
        case GLUE_RQT_MAGIC:
            afb_req_subcall (glue->rqt.afb, apiname, verbname, (int)index, params, afb_req_subcall_catch_events, GlueRqtSubcallCb, (void*)cbHandle);
            break;
        case GLUE_LOCK_MAGIC:
        case GLUE_API_MAGIC:
        case GLUE_BINDER_MAGIC:
            afb_api_call(GlueGetApi(glue), apiname, verbname, (int)index, params, GlueApiSubcallCb, (void*)cbHandle);
            break;

        default:
            errorMsg = "handle should be a req|api";
            goto OnErrorExit;
    }
    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueSyncCall(PyObject *self, PyObject *argsP)
{
    const char *errorMsg= "syntax: callsync(handle, api, verb, ...)";
    int err, status;
    long index=0, count = PyTuple_GET_SIZE(argsP);
    afb_data_t params[count];
    unsigned nreplies= SUBCALL_MAX_RPLY;
    afb_data_t replies[SUBCALL_MAX_RPLY];

    // parse input arguments
    if (count < PY_THREE_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    const char* apiname= PyUnicode_AsUTF8(PyTuple_GetItem(argsP,1));
    if (!apiname) goto OnErrorExit;

    const char* verbname= PyUnicode_AsUTF8(PyTuple_GetItem(argsP,2));
    if (!verbname) goto OnErrorExit;

    // retreive subcall api argument(s)
    for (index = 0; index < count-PY_THREE_ARG; index++)
    {
        json_object *argsJ = pyObjToJson(PyTuple_GetItem(argsP,index+PY_THREE_ARG));
        if (!argsJ)
        {
            errorMsg = "(hoops) afb_subcall_sync fail";
            goto OnErrorExit;
        }
        afb_create_data_raw(&params[index], AFB_PREDEFINED_TYPE_JSON_C, argsJ, 0, (void *)json_object_put, argsJ);
    }

    switch (glue->magic) {
        case GLUE_RQT_MAGIC:
            err= afb_req_subcall_sync (glue->rqt.afb, apiname, verbname, (int)index, params, afb_req_subcall_catch_events, &status, &nreplies, replies);
            break;
        case GLUE_LOCK_MAGIC:
        case GLUE_API_MAGIC:
        case GLUE_BINDER_MAGIC:
            err= afb_api_call_sync (GlueGetApi(glue), apiname, verbname, (int)index, params, &status, &nreplies, replies);
            break;

        default:
            errorMsg = "handle should be a req|api";
            goto OnErrorExit;
    }

    if (err) {
        status   = err;
        errorMsg= "api subcall fail";
        goto OnErrorExit;
    }
    // subcall was refused
    if (AFB_IS_BINDER_ERRNO(status)) {
        errorMsg= afb_error_text(status);
        goto OnErrorExit;
    }

    // retreive response and build Python response
    PyObject *resultP= PyTuple_New(nreplies+1);
    PyTuple_SetItem(resultP, 0, PyLong_FromLong((long)status));

    errorMsg= PyPushAfbReply (resultP, 1, nreplies, replies);
    if (errorMsg) goto OnErrorExit;

    return resultP;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueEventPush(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: eventpush(event, [arg1...argn])";
    long count = PyTuple_GET_SIZE(argsP);
    afb_data_t params[count];
    long index=0;

    if (count < PY_ONE_ARG) goto OnErrorExit;
    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_EVT_MAGIC) goto OnErrorExit;

    // get response from PY and push them as afb-v4 object
    for (index= 0; index < count-PY_ONE_ARG; index++)
    {
        json_object *argsJ = pyObjToJson(PyTuple_GetItem(argsP,index+PY_ONE_ARG));
        if (!argsJ)
        {
            errorMsg = "invalid argument type";
            goto OnErrorExit;
        }
        afb_create_data_raw(&params[index], AFB_PREDEFINED_TYPE_JSON_C, argsJ, 0, (void *)json_object_put, argsJ);
    }

    int status = afb_event_push(glue->evt.afb, (int) index, params);
    if (status < 0)
    {
        errorMsg = "afb_event_push fail sending event";
        goto OnErrorExit;
    }
    glue->evt.count++;
    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueEventSubscribe(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: subscribe(rqt, event)";

    long count = PyTuple_GET_SIZE(argsP);
    if (count != PY_TWO_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_RQT_MAGIC) goto OnErrorExit;

    AfbHandleT* handle= PyCapsule_GetPointer(PyTuple_GetItem(argsP,1), GLUE_AFB_UID);
    if (!handle || handle->magic != GLUE_EVT_MAGIC) goto OnErrorExit;

    int err = afb_req_subscribe(glue->rqt.afb, handle->evt.afb);
    if (err)
    {
        errorMsg = "fail subscribing afb event";
        goto OnErrorExit;
    }
    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueEventUnsubscribe(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: unsubscribe(rqt, event)";

    long count = PyTuple_GET_SIZE(argsP);
    if (count != PY_TWO_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_RQT_MAGIC) goto OnErrorExit;

    AfbHandleT* handle= PyCapsule_GetPointer(PyTuple_GetItem(argsP,1), GLUE_AFB_UID);
    if (!handle || handle->magic != GLUE_EVT_MAGIC) goto OnErrorExit;

    int err = afb_req_unsubscribe(glue->rqt.afb, handle->evt.afb);
    if (err)
    {
        errorMsg = "(hoops) afb_req_unsubscribe fail";
        goto OnErrorExit;
    }
    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueEventNew(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: eventnew(api, config)";
    PyObject *slotP;
    int err;

    long count = PyTuple_GET_SIZE(argsP);
    if (count != PY_TWO_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_API_MAGIC) goto OnErrorExit;

    // create a new binder event
    errorMsg = "evtconf={'uid':'xxx', 'name':'yyyy'}";
    AfbHandleT *pyEvt = calloc(1, sizeof(AfbHandleT));
    pyEvt->magic = GLUE_EVT_MAGIC;
    pyEvt->evt.apiv4= GlueGetApi(glue);

    pyEvt->evt.configP = PyTuple_GetItem(argsP,1);
    if (!PyDict_Check(pyEvt->evt.configP)) goto OnErrorExit;
    Py_IncRef(pyEvt->evt.configP);

    slotP= PyDict_GetItemString(pyEvt->evt.configP, "uid");
    if (!slotP || !PyUnicode_Check(slotP)) goto OnErrorExit;
    pyEvt->evt.uid= PyUnicode_AsUTF8(slotP);

    slotP= PyDict_GetItemString(pyEvt->evt.configP, "name");
    if (!slotP) pyEvt->evt.name = pyEvt->evt.uid;
    else {
        if (!PyUnicode_Check(slotP)) goto OnErrorExit;
        pyEvt->evt.name= PyUnicode_AsUTF8(slotP);
    }

    err= afb_api_new_event(glue->api.afb, pyEvt->evt.name, &pyEvt->evt.afb);
    if (err)
    {
        errorMsg = "(hoops) afb-afb_api_new_event fail";
        goto OnErrorExit;
    }

    // push event handler as a PY opaque handle
    return PyCapsule_New(pyEvt, GLUE_AFB_UID, NULL);

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueVerbAdd(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: addverb(api, config, context)";

    long count = PyTuple_GET_SIZE(argsP);
    if (count != PY_THREE_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_API_MAGIC) goto OnErrorExit;

    json_object *configJ= pyObjToJson (PyTuple_GetItem(argsP,1));
    if (!configJ) goto OnErrorExit;

    PyObject *userdataP= PyTuple_GetItem(argsP,2);
    if (userdataP) Py_IncRef(userdataP);

    errorMsg= AfbAddOneVerb (afbMain->binder.afb, glue->api.afb, configJ, GlueVerbCb, userdataP);
    if (errorMsg) goto OnErrorExit;

    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueSetLoa(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: setloa(rqt, newloa)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != PY_TWO_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue && glue->magic != GLUE_RQT_MAGIC) goto OnErrorExit;

    int loa= (int)PyLong_AsLong(PyTuple_GetItem(argsP,1));
    if (loa < 0) goto OnErrorExit;

    int err= afb_req_session_set_LOA(glue->rqt.afb, loa);
    if (err < 0) {
        errorMsg="Invalid Rqt Session";
        goto OnErrorExit;
    }

    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueTimerAddref(PyObject *self, PyObject *argsP) {
    const char *errorMsg="syntax: timeraddref(handle)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != PY_ONE_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_TIMER_MAGIC) goto OnErrorExit;

    afb_timer_addref (glue->timer.afb);
    Py_IncRef(glue->timer.configP);
    glue->timer.usage++;
    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueTimerUnref(PyObject *self, PyObject *argsP) {
    const char *errorMsg="syntax: timerunref(handle)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != PY_ONE_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_TIMER_MAGIC) goto OnErrorExit;

    GlueTimerClear(glue);
    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueEventHandler(PyObject *self, PyObject *argsP)
{
    PyObject *slotP;
    const char *errorMsg = "syntax: evthandler(handle, config, userdata)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != PY_THREE_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    // retreive API from lua handle
    afb_api_t afbApi= GlueGetApi(glue);
    if (!afbApi) goto OnErrorExit;

    AfbHandleT *handle= calloc(1, sizeof(AfbHandleT));
    handle->magic= GLUE_HANDLER_MAGIC;
    handle->handler.apiv4= afbApi;

    handle->handler.configP = PyTuple_GetItem(argsP,1);
    if (!PyDict_Check(handle->handler.configP)) goto OnErrorExit;
    Py_IncRef(handle->handler.configP);

    errorMsg= "config={'uid':'xxx','pattern':'yyy','callback':'zzz'}";
    slotP= PyDict_GetItemString(handle->handler.configP, "uid");
    if (!slotP || !PyUnicode_Check(slotP)) goto OnErrorExit;
    handle->handler.uid= PyUnicode_AsUTF8(slotP);

    slotP= PyDict_GetItemString(handle->handler.configP, "pattern");
    if (!slotP || !PyUnicode_Check(slotP)) goto OnErrorExit;
    const char *pattern= PyUnicode_AsUTF8(slotP);

    handle->handler.callbackP= PyDict_GetItemString(handle->handler.configP, "callback");
    if (!slotP || !PyCallable_Check(handle->handler.callbackP)) goto OnErrorExit;
    Py_IncRef(handle->handler.callbackP);

    handle->handler.userdataP = PyTuple_GetItem(argsP,2);
    if (handle->handler.userdataP) Py_IncRef(handle->handler.userdataP);

    errorMsg= AfbAddOneEvent (afbApi, handle->handler.uid, pattern, GlueEvtHandlerCb, handle);
    if (errorMsg) goto OnErrorExit;

    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueTimerNew(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: timernew(api, config, context)";
    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    AfbHandleT *handle= (AfbHandleT *)calloc(1, sizeof(AfbHandleT));
    handle->magic = GLUE_TIMER_MAGIC;
    handle->timer.configP = PyTuple_GetItem(argsP,1);
    if (!PyDict_Check(handle->timer.configP)) goto OnErrorExit;
    Py_IncRef(handle->timer.configP);

    handle->timer.userdataP =PyTuple_GetItem(argsP,2);
    if (handle->timer.userdataP != Py_None) Py_IncRef(handle->timer.userdataP);

    // parse config
    PyObject *slotP;
    errorMsg= "timerconfig= {'uid':'xxx', 'callback': MyCallback, 'period': timer(ms), 'count': 0-xx}";
    slotP= PyDict_GetItemString(handle->timer.configP, "uid");
    if (!slotP || !PyUnicode_Check(slotP)) goto OnErrorExit;
    handle->timer.uid= PyUnicode_AsUTF8(slotP);

    handle->timer.callbackP= PyDict_GetItemString(handle->timer.configP, "callback");
    if (!handle->timer.callbackP || !PyCallable_Check(handle->timer.callbackP)) goto OnErrorExit;

    slotP= PyDict_GetItemString(handle->timer.configP, "period");
    if (!slotP || !PyLong_Check(slotP)) goto OnErrorExit;
    long period= PyLong_AsLong(slotP);
    if (period <= 0) goto OnErrorExit;


    slotP= PyDict_GetItemString(handle->timer.configP, "count");
    if (!slotP || !PyLong_Check(slotP)) goto OnErrorExit;
    long count= PyLong_AsLong(slotP);
    if (period < 0) goto OnErrorExit;

    int err= afb_timer_create (&handle->timer.afb, 0, 0, 0, (int)count, (int)period, 0, GlueTimerCb, (void*)handle, 0);
    if (err) {
        errorMsg= "(hoops) afb_timer_create fail";
        goto OnErrorExit;
    }

    return PyCapsule_New(handle, GLUE_AFB_UID, NULL);

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueSchedWait(PyObject *self, PyObject *argsP)
{
    AfbHandleT *lock=NULL;
    int err;

    long count = PyTuple_GET_SIZE(argsP);
    const char *errorMsg = "schedwait(handle, timeout, callback, [context])";
    if (count < PY_THREE_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    long timeout= PyLong_AsLong(PyTuple_GetItem(argsP,1));
    if (timeout < 0) goto OnErrorExit;

    PyObject *callbackP= PyTuple_GetItem(argsP,2);
    if (!PyCallable_Check(callbackP)) goto OnErrorExit;
    Py_IncRef(callbackP);

    PyObject *userdataP= PyTuple_GetItem(argsP,3);
    if (userdataP != Py_None) Py_IncRef(userdataP);

    lock= calloc (1, sizeof(AfbHandleT));
    lock->magic= GLUE_LOCK_MAGIC;
    lock->lock.apiv4= GlueGetApi(glue);
    lock->lock.callbackP= callbackP;
    lock->lock.userdataP= userdataP;

    err= afb_sched_enter(NULL, (int)timeout, GlueSchedWaitCb, lock);
    if (err) {
        errorMsg= "fail to register afb_sched_enter";
        goto OnErrorExit;
    }

    // free lock handle
    Py_DecRef(lock->lock.callbackP);
    if (lock->lock.userdataP) Py_DecRef(lock->lock.userdataP);
    long status= lock->lock.status;
    free (lock);

    return PyLong_FromLong(status);

OnErrorExit:
    if (lock) {
        Py_DecRef(lock->lock.callbackP);
        if (lock->lock.userdataP) Py_DecRef(lock->lock.userdataP);
        free (lock);
    }
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueSchedCancel(PyObject *self, PyObject *argsP)
{
    long count = PyTuple_GET_SIZE(argsP);
    const char *errorMsg = "syntax: schedcancel(jobid)";
    if (count != PY_ONE_ARG) goto OnErrorExit;

    long jobid= PyLong_AsLong(PyTuple_GetItem(argsP,0));
    if (jobid <= 0) goto OnErrorExit;

    int err= afb_jobs_abort((int)jobid);
    if (err) goto OnErrorExit;
    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueSchedPost(PyObject *self, PyObject *argsP)
{
    GlueHandleCbT *glue=calloc (1, sizeof(GlueHandleCbT));
    glue->magic = GLUE_SCHED_MAGIC;

    long count = PyTuple_GET_SIZE(argsP);
    const char *errorMsg = "syntax: schedpost(glue, timeout, callback [,userdata])";
    if (count != PY_FOUR_ARG) goto OnErrorExit;

    glue->handle= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue->handle) goto OnErrorExit;

    long timeout= PyLong_AsLong(PyTuple_GetItem(argsP,1));
    if (timeout <= 0) goto OnErrorExit;

    glue->callbackP= PyTuple_GetItem(argsP,2);
    if (!PyCallable_Check(glue->callbackP)) {
        errorMsg="syntax: callback should be a valid callable function";
        goto OnErrorExit;
    }
    Py_IncRef(glue->callbackP);

    glue->userdataP =PyTuple_GetItem(argsP,3);
    if (glue->userdataP != Py_None) Py_IncRef(glue->userdataP);

    // ms delay for OnTimerCB (timeout is dynamic and depends on CURLOPT_LOW_SPEED_TIME)
    int jobid= afb_sched_post_job (NULL /*group*/, timeout,  0 /*exec-timeout*/,GlueSchedTimeoutCb, glue, Afb_Sched_Mode_Start);
	if (jobid <= 0) goto OnErrorExit;

    return PyLong_FromLong(jobid);

OnErrorExit:
    PY_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueSchedUnlock(PyObject *self, PyObject *argsP)
{
    int err;
    const char *errorMsg = "syntax: schedunlock(handle, lock, status)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count !=  PY_THREE_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    AfbHandleT *lock= PyCapsule_GetPointer(PyTuple_GetItem(argsP,1), GLUE_AFB_UID);
    if (!lock || lock->magic != GLUE_LOCK_MAGIC) goto OnErrorExit;

    lock->lock.status= PyLong_AsLong(PyTuple_GetItem(argsP,2));

    err= afb_sched_leave(lock->lock.afb);
    if (err) {
        errorMsg= "fail to register afb_sched_enter";
        goto OnErrorExit;
    }

    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(glue, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueExit(PyObject *self, PyObject *argsP)
{
    const char *errorMsg= "syntax: exit(handle, status)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != PY_TWO_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    long exitCode= PyLong_AsLong(PyTuple_GetItem(argsP,1));

    AfbBinderExit(afbMain->binder.afb, (int)exitCode);
    Py_RETURN_NONE;

OnErrorExit:
    PY_DBG_ERROR(glue, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject* GlueClientInfo(PyObject *self, PyObject *argsP)
{
    PyObject *resultP;
    const char *errorMsg = "syntax: clientinfo(rqt, ['key'])";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != PY_ONE_ARG && count != PY_TWO_ARG) goto OnErrorExit;

    AfbHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_RQT_MAGIC) goto OnErrorExit;

    PyObject *keyP= PyTuple_GetItem(argsP,1);
    if (keyP && !PyUnicode_Check(keyP)) goto OnErrorExit;

    json_object *clientJ= afb_req_get_client_info(glue->rqt.afb);
    if (!clientJ) {
        errorMsg= "(hoops) afb_req_get_client_info no session info";
        goto OnErrorExit;
    }

    if (!keyP) {
        resultP = jsonToPyObj(clientJ);
    } else {
        json_object *keyJ= json_object_object_get(clientJ, PyUnicode_AsUTF8(keyP));
        if (!keyJ) {
            errorMsg= "unknown client info key";
            goto OnErrorExit;
        }
        resultP = jsonToPyObj(keyJ);
    }
    return resultP;

OnErrorExit:
    PY_DBG_ERROR(glue, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    Py_RETURN_NONE;
}

static PyObject *PyPingTest(PyObject *self, PyObject *argsP)
{
    static long count=0;
    fprintf (stderr, "PyPingTest count=%ld\n", count);
    return PyLong_FromLong(count++);
}

static PyMethodDef MethodsDef[] = {
    {"error"         , PyPrintError         , METH_VARARGS, "print level AFB_SYSLOG_LEVEL_ERROR"},
    {"warning"       , PyPrintWarning       , METH_VARARGS, "print level AFB_SYSLOG_LEVEL_WARNING"},
    {"notice"        , PyPrintNotice        , METH_VARARGS, "print level AFB_SYSLOG_LEVEL_NOTICE"},
    {"info"          , PyPrintInfo          , METH_VARARGS, "print level AFB_SYSLOG_LEVEL_INFO"},
    {"debug"         , PyPrintDebug         , METH_VARARGS, "print level AFB_SYSLOG_LEVEL_DEBUG"},
    {"ping"          , PyPingTest           , METH_VARARGS, "Check afb-libpython is loaded"},
    {"binder"        , GlueBinderConf       , METH_VARARGS, "Configure and create afbMain glue"},
    {"config"        , PyGetConfig          , METH_VARARGS, "Return glue handle full/partial config"},
    {"apiadd"        , GlueApiCreate        , METH_VARARGS, "Add a new API to the binder"},
    {"mainloop"      , GlueMainLoop         , METH_VARARGS, "Activate mainloop and exec startup callback"},
    {"reply"         , GlueRespond          , METH_VARARGS, "Explicit response tp afb request"},
    {"binding"       , GlueBindingLoad      , METH_VARARGS, "Load binding an expose corresponding api/verbs"},
    {"callasync"     , GlueAsyncCall        , METH_VARARGS, "AFB asynchronous subcall"},
    {"callsync"      , GlueSyncCall         , METH_VARARGS, "AFB synchronous subcall"},
    {"verbadd"       , GlueVerbAdd          , METH_VARARGS, "Add a verb to a non sealed API"},
    {"evtsubscribe"  , GlueEventSubscribe   , METH_VARARGS, "Subscribe to event"},
    {"evtunsubscribe", GlueEventUnsubscribe , METH_VARARGS, "Unsubscribe to event"},
    {"evthandler"    , GlueEventHandler     , METH_VARARGS, "Register event callback handler"},
    {"evtnew"        , GlueEventNew         , METH_VARARGS, "Create a new event"},
    {"evtpush"       , GlueEventPush        , METH_VARARGS, "Push a given event"},
    {"timerunref"    , GlueTimerUnref       , METH_VARARGS, "Unref existing timer"},
    {"timeraddref"   , GlueTimerAddref      , METH_VARARGS, "Addref to existing timer"},
    {"timernew"      , GlueTimerNew         , METH_VARARGS, "Create a new timer"},
    {"setloa"        , GlueSetLoa           , METH_VARARGS, "Set LOA (LevelOfAssurance)"},
    {"schedwait"     , GlueSchedWait        , METH_VARARGS, "Register a mainloop waiting lock"},
    {"schedunlock"   , GlueSchedUnlock      , METH_VARARGS, "Unlock schedwait"},
    {"schedpost"     , GlueSchedPost        , METH_VARARGS, "Post a job after timeout(ms)"},
    {"schedcancel"   , GlueSchedCancel      , METH_VARARGS, "Cancel a schedpost timer"},
    {"clientinfo"    , GlueClientInfo       , METH_VARARGS, "Return seesion info about client"},
    {"exit"          , GlueExit             , METH_VARARGS, "Exit binder with status"},


    {NULL}  /* sentinel */
};

static PyModuleDef ModuleDef = {
    PyModuleDef_HEAD_INIT,
    "_afbpyglue",
    "Python 'afb-glue' expose 'afb-libafb' to Python scripting language.",
    -1, // Rationale for Per-module State https://www.python.org/dev/peps/pep-0630/
    MethodsDef,
};

// Init redpak native module
PyObject* PyInit__afbpyglue(void) {

    fprintf (stderr, "I'm in python\n");
    PyObject *module = PyModule_Create(&ModuleDef);
    return module;
}