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
#include <structmember.h>

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "longobject.h"
#include "object.h"
#include "py-afb.h"
#include "py-utils.h"
#include "py-callbacks.h"
#include "tupleobject.h"


// global afbMain glue
GlueHandleT *afbMain=NULL;

typedef struct {
    PyObject_HEAD
    PyObject *statusP;
    PyObject *argsP;
} PyResponseObjectT;

void PyResponseFreecB (PyObject *self) {
    PyResponseObjectT *reply= (PyResponseObjectT*)self;
    Py_XDECREF(reply->statusP);
    Py_XDECREF(reply->argsP);
    Py_TYPE(reply)->tp_free(reply);
}

static PyObject *PyResponseNewCb (PyTypeObject *type, PyObject *argsP, PyObject *kwds) {
    int status;

    PyResponseObjectT *response= (PyResponseObjectT*) type->tp_alloc(type, 0);
    if (!response) goto OnErrorExit;

    status= PyArg_ParseTuple(argsP, "OO", &response->statusP, &response->argsP);
    if (status == 0) goto OnErrorExit;
    Py_INCREF(response->statusP);
    Py_INCREF(response->argsP);
    return (PyObject *)response;

OnErrorExit:
   PyErr_SetString(PyExc_RuntimeError, "syntax response(status, args)");
   return NULL;
}

static int GlueRepInitProc (PyObject *self, PyObject *argsP, PyObject *kwds) {
    int status;
    PyResponseObjectT *response= (PyResponseObjectT*)self;
    status= PyArg_ParseTuple(argsP, "OO", &response->statusP, &response->argsP);
    if (status == 0) goto OnErrorExit;
    Py_INCREF(response->statusP);
    Py_INCREF(response->argsP);
    return 0;

OnErrorExit:
   PyErr_SetString(PyExc_RuntimeError, "syntax response(status, args)");
   return -1;
}

static PyMemberDef PyResponseMembers[] = {
    {"status", T_OBJECT_EX, offsetof(PyResponseObjectT, statusP), 0, "status number"},
    {"args"  , T_OBJECT_EX, offsetof(PyResponseObjectT, argsP)  , 0, "reply datas"},
    {NULL}  /* Sentinel */
};

static PyTypeObject PyResponseType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_afbpyglue.response",
    .tp_doc = "AFB response object",
    .tp_basicsize = sizeof(PyResponseObjectT),
    .tp_itemsize = 0,
    .tp_flags    = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_init     = GlueRepInitProc,
    .tp_new      = PyResponseNewCb,
    .tp_dealloc  = PyResponseFreecB,
    .tp_members  = PyResponseMembers,
};

static PyObject * GluePrintInfo(PyObject *self, PyObject *argsP)
{
    PyPrintMsg(AFB_SYSLOG_LEVEL_INFO, self, argsP);
    Py_RETURN_NONE;
}

static PyObject * GluePrintError(PyObject *self, PyObject *argsP)
{
    PyPrintMsg(AFB_SYSLOG_LEVEL_ERROR, self, argsP);
    Py_RETURN_NONE;
}

static PyObject * GluePrintWarning(PyObject *self, PyObject *argsP)
{
    PyPrintMsg(AFB_SYSLOG_LEVEL_WARNING, self, argsP);
    Py_RETURN_NONE;
}

static PyObject * GluePrintNotice(PyObject *self, PyObject *argsP)
{
    PyPrintMsg(AFB_SYSLOG_LEVEL_NOTICE, self, argsP);
    Py_RETURN_NONE;
}

static PyObject * GluePrintDebug(PyObject *self, PyObject *argsP)
{
    PyPrintMsg(AFB_SYSLOG_LEVEL_DEBUG, self, argsP);
    Py_RETURN_NONE;
}

static PyObject *GlueBinderConf(PyObject *self, PyObject *argsP)
{
    const char *errorMsg="syntax: binder(config)";
    if (afbMain != NULL) {
        errorMsg="(hoops) binder(config) already loaded";
        goto OnErrorExit;
    }

    // allocate afbMain glue and parse config to jsonC
    afbMain= calloc(1, sizeof(GlueHandleT));
    if (afbMain == NULL) {
        errorMsg= "out of memory";
        goto OnErrorExit;
    }
    afbMain->magic= GLUE_BINDER_MAGIC_TAG;

#if PY_MINOR_VERSION < 7
    PyEval_InitThreads(); // from 3.7 this is useless
#endif

    if (!PyArg_ParseTuple(argsP, "O", &afbMain->binder.configP)) {
        errorMsg= "invalid config object";
        goto OnErrorExit;
    }
    json_object *configJ= pyObjToJson(afbMain->binder.configP);
    if (!configJ) {
        errorMsg="json incompatible config";
        goto OnErrorExit;
    }

    errorMsg= AfbBinderConfig(configJ, &afbMain->binder.afb, afbMain);
    if (errorMsg) goto OnErrorExit;

    // return afbMain glue as a Python capsule glue
    PyObject *capsule= PyCapsule_New(afbMain, GLUE_AFB_UID, NULL);
    return capsule;

OnErrorExit:
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    PyErr_Print();
    return NULL;
}

typedef enum {
	addApiAdd,
	addApiImport,
	addApiCreate
}
	addApiHow;

static PyObject *addApi(PyObject *self, PyObject *argsP, addApiHow how)
{
    const char *errorMsg = "syntax: apiadd (config)";

    if (!afbMain) {
        errorMsg= "Should use libafb.binder() first";
        goto OnErrorExit;
    }

    GlueHandleT *glue = calloc(1, sizeof(GlueHandleT));
    if (glue == NULL) {
        errorMsg= "out of memory";
        goto OnErrorExit;
    }
    glue->magic = GLUE_API_MAGIC_TAG;

    if (!PyArg_ParseTuple(argsP, "O", &glue->api.configP)) goto OnErrorExit;
    json_object *configJ= pyObjToJson(glue->api.configP);
    if (!configJ) {
        errorMsg="json incompatible config";
        goto OnErrorExit;
    }

    if (how == addApiAdd) {
        json_object *afbApiUri = json_object_object_get(configJ, "uri");
        how = afbApiUri ? addApiImport : addApiCreate;
    }

    if (how == addApiImport)
    {
        // imported shadow api
        errorMsg = AfbApiImport(afbMain->binder.afb, configJ);
    }
    else
    {
        // check if control python api function is defined
        afb_api_callback_x4_t usrApiCb = NULL;
        glue->api.ctrlCb= PyDict_GetItemString(glue->api.configP, "control");
        if (glue->api.ctrlCb) {
            if (!PyCallable_Check(glue->api.ctrlCb)) {
                errorMsg="API control function defined but not callable";
                goto OnErrorExit;
            }
            usrApiCb = GlueCtrlCb;
        }
        Py_BEGIN_ALLOW_THREADS
        errorMsg = AfbApiCreate(afbMain->binder.afb, configJ, &glue->api.afb, usrApiCb, GlueInfoCb, GlueApiVerbCb, GlueApiEventCb, glue);
        Py_END_ALLOW_THREADS
    }
    if (errorMsg)
        goto OnErrorExit;

    // return api glue
    PyObject *capsule= PyCapsule_New(glue, GLUE_AFB_UID, NULL);
    return capsule;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    PyErr_Print();
    return NULL;
}

static PyObject *GlueApiAdd(PyObject *self, PyObject *argsP)
{
	return addApi(self, argsP, addApiAdd);
}

static PyObject *GlueApiImport(PyObject *self, PyObject *argsP)
{
	return addApi(self, argsP, addApiImport);
}

static PyObject *GlueApiCreate(PyObject *self, PyObject *argsP)
{
	return addApi(self, argsP, addApiCreate);
}

// this routine execute within mainloop context when binder is ready to go
static PyObject* GlueLoopStart(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: loopstart(binder,[callback],[userdata])";
    int status;

    long count = PyTuple_GET_SIZE(argsP);
    if (count <1 || count >3) goto OnErrorExit;

    GlueAsyncCtxT *async= calloc(1, sizeof(GlueAsyncCtxT));
    if (async == NULL) {
        errorMsg= "out of memory";
        goto OnErrorExit;
    }

    GlueHandleT *glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || !GlueGetApi(glue)) goto OnErrorExit;

    if (count >= 2) {
        async->callbackP= PyTuple_GetItem(argsP,1);
        if (async->callbackP) {
            if (!PyCallable_Check(async->callbackP)) goto OnErrorExit;
            Py_IncRef(async->callbackP);
        }
    }

    if (count >= 3) {
        async->userdataP= PyTuple_GetItem(argsP,2);
        Py_IncRef(async->userdataP);
    }

    // main loop only return when binder startup func return status!=0
    GLUE_AFB_NOTICE(afbMain, "Entering binder mainloop");
    Py_BEGIN_ALLOW_THREADS
    status = AfbBinderStart(afbMain->binder.afb, async, GlueStartupCb, glue);
    Py_END_ALLOW_THREADS

    return PyLong_FromLong ((long)status);

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    PyErr_Print();
    return NULL;
}

static PyObject* GlueGetConfig(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: config(handle[,key])";
    PyObject *capsuleP, *configP, *resultP, *slotP, *keyP=NULL;
    if (!PyArg_ParseTuple(argsP, "O|O", &capsuleP, &keyP)) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(capsuleP, GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    switch (glue->magic)
    {
    case GLUE_API_MAGIC_TAG:
        configP = glue->api.configP;
        break;
    case GLUE_BINDER_MAGIC_TAG:
        configP = glue->binder.configP;
        break;
    case GLUE_TIMER_MAGIC_TAG:
        configP = glue->timer.configP;
        break;
    case GLUE_EVT_MAGIC_TAG:
        configP = glue->event.configP;
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
        if (!slotP) {
            Py_INCREF(Py_None);
            resultP= Py_None;
        } else {
            resultP= slotP;
            Py_INCREF(resultP);
        }
    }
    return resultP;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    PyErr_Print();
    return NULL;
}

static PyObject* GlueReply(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: reply(rqt, status, [arg1 ... argn])";
    PyObject *slotP;
    long status, count=0;

    count = PyTuple_GET_SIZE(argsP);
    afb_data_t reply[count];

    slotP=  PyTuple_GetItem(argsP,0);
    GlueHandleT* glue= PyCapsule_GetPointer(slotP, GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_RQT_MAGIC_TAG) goto OnErrorExit0;

    if (count < 2) goto OnErrorExit;

    json_object *slotJ;
    slotP=  PyTuple_GetItem(argsP,1);
    if (!PyLong_Check(slotP)) {
        errorMsg= "syntax: invalid status should be integer";
        goto OnErrorExit;
    }
    status= PyLong_AsLong(slotP);

    for (long idx=0; idx < count-2; idx++) {
        slotP= PyTuple_GetItem(argsP,idx+2);
        slotJ= pyObjToJson(slotP);
        if (!slotJ)
        {
            errorMsg = "(hoops) not json convertible response";
            goto OnErrorExit;
        }
        afb_create_data_raw(&reply[idx], AFB_PREDEFINED_TYPE_JSON_C, slotJ, 0, (void *)json_object_put, slotJ);
    }

    // respond request and free ressources.
    GlueAfbReply(glue, status, count-2, reply);
    Py_RETURN_NONE;

OnErrorExit:
    {
        afb_data_t reply;
        json_object *errorJ = PyJsonDbg(errorMsg);
        GLUE_AFB_WARNING(glue, "python=%s", json_object_get_string(errorJ));
        afb_create_data_raw(&reply, AFB_PREDEFINED_TYPE_JSON_C, errorJ, 0, (void *)json_object_put, errorJ);
        GlueAfbReply(glue, -1, 1, &reply);
    }
OnErrorExit0:
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
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueCallAsync(PyObject *self, PyObject *argsP)
{
    const char *errorMsg= "syntax: callasync(handle, api, verb, callback, context, ...)";
    long index=0;

    // parse input arguments
    long count = PyTuple_GET_SIZE(argsP);
    afb_data_t params[count];
    if (count < 5) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
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
    for (index= 0; index < count-5; index++)
    {
        json_object *argsJ = pyObjToJson(PyTuple_GetItem(argsP,index+5));
        if (!argsJ)
        {
            errorMsg = "invalid input argument type";
            goto OnErrorExit;
        }
        afb_create_data_raw(&params[index], AFB_PREDEFINED_TYPE_JSON_C, argsJ, 0, (void *)json_object_put, argsJ);
    }

    GlueCallHandleT *handle= calloc(1,sizeof(GlueCallHandleT));
    if (handle == NULL) {
        errorMsg= "out of memory";
        goto OnErrorExit;
    }
    handle->glue= glue;
    handle->magic= GLUE_CALL_MAGIC_TAG;
    handle->async.callbackP= callbackP;
    handle->async.userdataP= userdataP;
    Py_IncRef(handle->async.callbackP);

    switch (glue->magic) {
        case GLUE_RQT_MAGIC_TAG:
            Py_BEGIN_ALLOW_THREADS
            afb_req_subcall (glue->rqt.afb, apiname, verbname, (int)index, params, afb_req_subcall_catch_events, GlueRqtSubcallCb, (void*)handle);
            Py_END_ALLOW_THREADS
            break;
        default:
            if (!GlueGetApi(glue)) {
                errorMsg= "invalid api handle";
                goto OnErrorExit;
            }
            Py_BEGIN_ALLOW_THREADS
            afb_api_call(GlueGetApi(glue), apiname, verbname, (int)index, params, GlueApiSubcallCb, (void*)handle);
            Py_END_ALLOW_THREADS
    }
    Py_RETURN_NONE;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueCallSync(PyObject *self, PyObject *argsP)
{
    const char *errorMsg= "syntax: callsync(handle, api, verb, ...)";
    int err, status;
    long index=0, count = PyTuple_GET_SIZE(argsP);
    afb_data_t params[count];
    unsigned nreplies= SUBCALL_MAX_RPLY;
    afb_data_t replies[SUBCALL_MAX_RPLY];

    // parse input arguments
    if (count < 3) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    const char* apiname= PyUnicode_AsUTF8(PyTuple_GetItem(argsP,1));
    if (!apiname) goto OnErrorExit;

    const char* verbname= PyUnicode_AsUTF8(PyTuple_GetItem(argsP,2));
    if (!verbname) goto OnErrorExit;

    // retreive subcall api argument(s)
    for (index = 0; index < count-3; index++)
    {
        json_object *argsJ = pyObjToJson(PyTuple_GetItem(argsP,index+3));
        if (!argsJ)
        {
            errorMsg = "(hoops) afb_subcall_sync fail";
            goto OnErrorExit;
        }
        afb_create_data_raw(&params[index], AFB_PREDEFINED_TYPE_JSON_C, argsJ, 0, (void *)json_object_put, argsJ);
    }

    switch (glue->magic) {
        case GLUE_RQT_MAGIC_TAG:
            Py_BEGIN_ALLOW_THREADS
            err= afb_req_subcall_sync (glue->rqt.afb, apiname, verbname, (int)index, params, afb_req_subcall_catch_events, &status, &nreplies, replies);
            Py_END_ALLOW_THREADS
            break;
        case GLUE_JOB_MAGIC_TAG:
        case GLUE_API_MAGIC_TAG:
        case GLUE_BINDER_MAGIC_TAG:
            Py_BEGIN_ALLOW_THREADS
            err= afb_api_call_sync (GlueGetApi(glue), apiname, verbname, (int)index, params, &status, &nreplies, replies);
            Py_END_ALLOW_THREADS
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
    PyObject *paramsP= PyTuple_New(2);
    PyTuple_SetItem(paramsP, 0, PyLong_FromLong(status));

    PyObject *replyP= PyTuple_New(nreplies);
    PyTuple_SetItem(paramsP, 1, replyP);
    errorMsg= PyPushAfbReply (replyP, 0, nreplies, replies);
    if (errorMsg) goto OnErrorExit;

    PyObject *resultP  = PyObject_CallObject((PyObject*) &PyResponseType, paramsP);
    Py_DecRef(replyP);
    Py_DecRef(paramsP);
    return resultP;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueEvtPush(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: eventpush(evtid, [arg1...argn])";
    long count = PyTuple_GET_SIZE(argsP);
    afb_data_t params[count];
    long index=0;

    if (count < 1) goto OnErrorExit;
    afb_event_t evtid= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!evtid || !afb_event_is_valid(evtid)) goto OnErrorExit;

    // get response from PY and push them as afb-v4 object
    for (index= 0; index < count-1; index++)
    {
        json_object *argsJ = pyObjToJson(PyTuple_GetItem(argsP,index+1));
        if (!argsJ)
        {
            errorMsg = "invalid argument type";
            goto OnErrorExit;
        }
        afb_create_data_raw(&params[index], AFB_PREDEFINED_TYPE_JSON_C, argsJ, 0, (void *)json_object_put, argsJ);
    }

    int status;
    Py_BEGIN_ALLOW_THREADS
    status = afb_event_push(evtid, (int) index, params);
    Py_END_ALLOW_THREADS

    if (status < 0)
    {
        errorMsg = "afb_event_push fail sending event";
        goto OnErrorExit;
    }
    Py_RETURN_NONE;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueEvtSubscribe(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: subscribe(rqt,evtid)";

    long count = PyTuple_GET_SIZE(argsP);
    if (count != 2) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_RQT_MAGIC_TAG) goto OnErrorExit;

    afb_event_t evtid= PyCapsule_GetPointer(PyTuple_GetItem(argsP,1), GLUE_AFB_UID);
    if (!evtid || !afb_event_is_valid(evtid)) goto OnErrorExit;

    int err = afb_req_subscribe(glue->rqt.afb, evtid);
    if (err)
    {
        errorMsg = "fail subscribing afb event";
        goto OnErrorExit;
    }
    Py_RETURN_NONE;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueEvtUnsubscribe(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: unsubscribe(rqt,evtid)";

    long count = PyTuple_GET_SIZE(argsP);
    if (count != 2) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_RQT_MAGIC_TAG) goto OnErrorExit;

    afb_event_t evtid= PyCapsule_GetPointer(PyTuple_GetItem(argsP,1), GLUE_AFB_UID);
    if (!evtid || !afb_event_is_valid(evtid)) goto OnErrorExit;

    int err = afb_req_unsubscribe(glue->rqt.afb, evtid);
    if (err)
    {
        errorMsg = "(hoops) afb_req_unsubscribe fail";
        goto OnErrorExit;
    }
    Py_RETURN_NONE;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueEvtNew(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: evtid= eventnew(api,label)";
    afb_event_t evtid;
    int err;

    long count = PyTuple_GET_SIZE(argsP);
    if (count != 2) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || !GlueGetApi(glue)) goto OnErrorExit;

    char *label= pyObjToStr(PyTuple_GetItem(argsP,1));
    if (!label) goto OnErrorExit;

    err= afb_api_new_event(GlueGetApi(glue), label, &evtid);
    if (err)
    {
        free(label);
        errorMsg = "(hoops) afb-afb_api_new_event fail";
        goto OnErrorExit;
    }

    // push event afb handle as a PY opaque handle
    return PyCapsule_New(evtid, GLUE_AFB_UID, NULL);

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueVerbAdd(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: addverb(api, config, context)";

    long count = PyTuple_GET_SIZE(argsP);
    if (count != 3) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_API_MAGIC_TAG) goto OnErrorExit;

    json_object *configJ= pyObjToJson (PyTuple_GetItem(argsP,1));
    if (!configJ) goto OnErrorExit;

    PyObject *userdataP= PyTuple_GetItem(argsP,2);
    if (userdataP) Py_IncRef(userdataP);

    errorMsg= AfbAddOneVerb (afbMain->binder.afb, glue->api.afb, configJ, GlueApiVerbCb, userdataP);
    if (errorMsg) goto OnErrorExit;

    Py_RETURN_NONE;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueSetLoa(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: setloa(rqt, newloa)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != 2) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue && glue->magic != GLUE_RQT_MAGIC_TAG) goto OnErrorExit;

    int loa= (int)PyLong_AsLong(PyTuple_GetItem(argsP,1));
    if (loa < 0) goto OnErrorExit;

    int err= afb_req_session_set_LOA(glue->rqt.afb, loa);
    if (err < 0) {
        errorMsg="Invalid Rqt Session";
        goto OnErrorExit;
    }

    Py_RETURN_NONE;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueTimerAddref(PyObject *self, PyObject *argsP) {
    const char *errorMsg="syntax: timeraddref(handle)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != 1) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_TIMER_MAGIC_TAG) goto OnErrorExit;

    afb_timer_addref (glue->timer.afb);
    Py_IncRef(glue->timer.configP);
    glue->usage++;
    Py_RETURN_NONE;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueTimerUnref(PyObject *self, PyObject *argsP) {
    const char *errorMsg="syntax: timerunref(handle)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != 1) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_TIMER_MAGIC_TAG) goto OnErrorExit;

    GlueFreeHandleCb(glue);
    Py_RETURN_NONE;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueEvtHandler(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: evthandler(handle, {'uid':'xxx','pattern':'yyy','callback':'zzz'}, userdata)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != 3) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    // retreive API from py handle
    afb_api_t apiv4= GlueGetApi(glue);
    if (!apiv4) goto OnErrorExit;

    GlueHandleT *handle = calloc(1, sizeof(GlueHandleT));
    if (handle == NULL) goto OnErrorExit;
    handle->magic = GLUE_EVT_MAGIC_TAG;
    handle->event.apiv4= apiv4;

    PyObject *configP = PyTuple_GetItem(argsP,1);
    if (!PyDict_Check(configP)) goto OnErrorExit;
    Py_IncRef(configP);

    errorMsg= "config={'uid':'xxx','pattern':'yyy','callback':'zzz'}";
    PyObject *uidP= PyDict_GetItemString(configP, "uid");
    if (!uidP || !PyUnicode_Check(uidP)) goto OnErrorExit;
    handle->event.async.uid= pyObjToStr(uidP);
    if (!handle->event.async.uid) goto OnErrorExit;

    PyObject *patternP= PyDict_GetItemString(configP, "pattern");
    if (!patternP || !PyUnicode_Check(patternP)) goto OnErrorExit;
    handle->event.pattern= pyObjToStr(patternP);
    if (!handle->event.pattern) goto OnErrorExit;

    handle->event.async.callbackP= PyDict_GetItemString(configP, "callback");
    if (!handle->event.async.callbackP || !PyCallable_Check(handle->event.async.callbackP)) goto OnErrorExit;
    Py_IncRef(handle->event.async.callbackP);

    handle->event.async.userdataP = PyTuple_GetItem(argsP,2);
    if (handle->event.async.userdataP) Py_IncRef(handle->event.async.userdataP);

    errorMsg= AfbAddOneEvent(apiv4, handle->event.async.uid, handle->event.pattern, GlueEventCb, handle);
    if (errorMsg) goto OnErrorExit;

    // return api glue
    PyObject *capsule= PyCapsule_New(handle, GLUE_AFB_UID, GlueFreeCapsuleCb);
    Py_DecRef(uidP);
    Py_DecRef(patternP);

    return capsule;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueEvtDelete(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: evtdelete(handle)";
    void *userdata;
    long count = PyTuple_GET_SIZE(argsP);
    if (count != 1) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_EVT_MAGIC_TAG) goto OnErrorExit;

    // retreive API from py handle
    afb_api_t apiv4= GlueGetApi(glue);
    if (!apiv4) goto OnErrorExit;

    errorMsg= AfbDelOneEvent (apiv4, glue->event.pattern, &userdata);
    if (errorMsg) goto OnErrorExit;
    GlueHandleT *handle= (GlueHandleT*)userdata;
    assert (handle->magic == GLUE_EVT_MAGIC_TAG);
    GlueFreeHandleCb(handle);

    Py_RETURN_NONE;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueTimerNew(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "syntax: timernew(api, config, context)";
    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    GlueHandleT *handle= (GlueHandleT *)calloc(1, sizeof(GlueHandleT));
    if (handle == NULL) goto OnErrorExit;
    handle->magic = GLUE_TIMER_MAGIC_TAG;
    handle->timer.configP = PyTuple_GetItem(argsP,1);
    if (!PyDict_Check(handle->timer.configP)) goto OnErrorExit;
    Py_IncRef(handle->timer.configP);

    handle->timer.async.userdataP =PyTuple_GetItem(argsP,2);
    if (handle->timer.async.userdataP != Py_None) Py_IncRef(handle->timer.async.userdataP);

    // retreive API from py handle
    handle->timer.apiv4= GlueGetApi(glue);
    if (!handle->timer.apiv4) goto OnErrorExit;

    // parse config
    PyObject *slotP;
    errorMsg= "timerconfig= {'uid':'xxx', 'callback': MyCallback, 'period': timer(ms), 'count': 0-xx}";
    slotP= PyDict_GetItemString(handle->timer.configP, "uid");
    if (!slotP || !PyUnicode_Check(slotP)) goto OnErrorExit;
    handle->timer.async.uid= pyObjToStr(slotP);

    handle->timer.async.callbackP= PyDict_GetItemString(handle->timer.configP, "callback");
    if (!handle->timer.async.callbackP || !PyCallable_Check(handle->timer.async.callbackP)) goto OnErrorExit;

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
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueJobAbort(PyObject *self, PyObject *argsP)
{
    long count = PyTuple_GET_SIZE(argsP);
    const char *errorMsg = "syntax: jobabort(jobid)";
    if (count != 1) goto OnErrorExit;

    long jobid= PyLong_AsLong(PyTuple_GetItem(argsP,0));
    if (jobid <= 0) goto OnErrorExit;

    int err= afb_jobs_abort((int)jobid);
    if (err) goto OnErrorExit;
    Py_RETURN_NONE;

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueJobPost(PyObject *self, PyObject *argsP)
{
    const char *errorMsg = "jobpost(binder, callback, delay, [userdata])";
    GlueCallHandleT *handle=NULL;

    long count = PyTuple_GET_SIZE(argsP);
    if (count < 3) goto OnErrorExit;

    GlueHandleT *glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    // prepare handle for callback
    handle=calloc (1, sizeof(GlueCallHandleT));
    if (handle == NULL) {
        errorMsg="out of memory";
        goto OnErrorExit;
    }
    handle->magic= GLUE_POST_MAGIC_TAG;
    handle->glue=glue;

    handle->async.callbackP= PyTuple_GetItem(argsP,1);
    if (!PyCallable_Check(handle->async.callbackP)) {
        errorMsg="syntax: callback should be a valid callable function";
        goto OnErrorExit;
    }

    Py_IncRef(handle->async.callbackP);
    PyObject *uidP= PyDict_GetItemString(handle->async.callbackP, "__name__");
    if (uidP) handle->async.uid= pyObjToStr(uidP);
    Py_DecRef(uidP);

    long delay= PyLong_AsLong(PyTuple_GetItem(argsP,2));
    if (delay <= 0) goto OnErrorExit;

    if (PyTuple_GET_SIZE(argsP) > 3) {
        handle->async.userdataP =PyTuple_GetItem(argsP,3);
        if (handle->async.userdataP != Py_None) Py_IncRef(handle->async.userdataP);
    }

    // ms delay for OnTimerCB (delay is dynamic and depends on CURLOPT_LOW_SPEED_TIME)
    int jobid= afb_sched_post_job (NULL /*group*/, delay,  0 /*exec-timeout*/,GlueJobPostCb, handle, Afb_Sched_Mode_Start);
    if (jobid <= 0) goto OnErrorExit;

    return PyLong_FromLong(jobid);

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    if (handle) {
        Py_DecRef(handle->async.callbackP);
        if (handle->async.userdataP) Py_DecRef(handle->async.userdataP);
        free (handle);
    }
    return NULL;
}

// manual_lock means jobenter; !manual_lock means jobcall
PyObject* GlueJob(PyObject *self, PyObject *argsP, bool manual_lock)
{
    const char *errorMsg = "jobcall(binder, callback, timeout, [userdata])";
    GlueHandleT *handle = NULL;
    int err;

    long count = PyTuple_GET_SIZE(argsP);
    if (count < 3)
        goto OnErrorExit;

    // arg index 0: handle
    GlueHandleT *glue = PyCapsule_GetPointer(PyTuple_GetItem(argsP, 0), GLUE_AFB_UID);
    if (!glue)
        goto OnErrorExit;

    // arg index 1: callback
    // setup
    handle = calloc(1, sizeof(GlueHandleT));
    if (!handle) {
        errorMsg = "out of memory";
        goto OnErrorExit;
    }
    handle->magic = GLUE_JOB_MAGIC_TAG;
    handle->job.apiv4 = GlueGetApi(glue);
    // get callback from Python
    handle->job.async.callbackP = PyTuple_GetItem(argsP, 1);
    if (!PyCallable_Check(handle->job.async.callbackP)) {
        errorMsg = "callback should be a valid callable";
        goto OnErrorExit;
    }
    Py_IncRef(handle->job.async.callbackP);
    PyObject *uidP = PyDict_GetItemString(handle->job.async.callbackP, "__name__");
    if (uidP)
        handle->job.async.uid = pyObjToStr(uidP);
    Py_DecRef(uidP);

    // arg index 2: timeout
    int timeout = (int)PyLong_AsLong(PyTuple_GetItem(argsP, 2));
    if (timeout <= 0)
        goto OnErrorExit;

    // arg index 3: userdata
    if (PyTuple_GET_SIZE(argsP) > 3) {
        handle->job.async.userdataP = PyTuple_GetItem(argsP, 3);
        if (handle->job.async.userdataP != Py_None)
            Py_IncRef(handle->job.async.userdataP);
    }

    // call the damned thing
    Py_BEGIN_ALLOW_THREADS
    if (manual_lock)
        err = afb_sched_enter(NULL, timeout, GlueJobEnterCb, handle);
    else
        afb_sched_call(timeout, GlueJobCallCb, handle, Afb_Sched_Mode_Normal);
    Py_END_ALLOW_THREADS

    if (manual_lock && err < 0) {
        errorMsg = "afb_sched_enter errored (timeout?)";
        goto OnErrorExit;
    }

    // return job's status
    long status = handle->job.status;
    GlueFreeHandleCb(handle);
    return PyLong_FromLong(status);

OnErrorExit:
    GLUE_DBG_ERROR(afbMain, errorMsg);
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    if (handle)
        GlueFreeHandleCb(handle);
    return NULL;
}

static PyObject* GlueJobCall(PyObject *self, PyObject *argsP)
{
    return GlueJob(self, argsP, false);
}

static PyObject* GlueJobEnter(PyObject *self, PyObject *argsP)
{
    return GlueJob(self, argsP, true);
}

static PyObject* GlueJobLeave(PyObject *self, PyObject *argsP)
{
    int err;
    const char *errorMsg = "syntax: jobleave(job, status)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count !=  2) goto OnErrorExit0;

    GlueHandleT *glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_JOB_MAGIC_TAG) goto OnErrorExit0;
    glue->job.status= PyLong_AsLong(PyTuple_GetItem(argsP,1));

    err= afb_sched_leave(glue->job.afb);
    if (err) {
        errorMsg= "afb_sched_leave (invalid lock)";
        goto OnErrorExit;
    }

    Py_RETURN_NONE;

OnErrorExit:
    GLUE_DBG_ERROR(glue, errorMsg);
OnErrorExit0:
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueExit(PyObject *self, PyObject *argsP)
{
    const char *errorMsg= "syntax: exit(handle, status)";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != 2) goto OnErrorExit;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue) goto OnErrorExit;

    long exitCode= PyLong_AsLong(PyTuple_GetItem(argsP,1));

    AfbBinderExit(afbMain->binder.afb, (int)exitCode);
    Py_RETURN_NONE;

OnErrorExit:
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject* GlueClientInfo(PyObject *self, PyObject *argsP)
{
    PyObject *resultP;
    const char *errorMsg = "syntax: clientinfo(rqt, ['key'])";
    long count = PyTuple_GET_SIZE(argsP);
    if (count != 1 && count != 2) goto OnErrorExit0;

    GlueHandleT* glue= PyCapsule_GetPointer(PyTuple_GetItem(argsP,0), GLUE_AFB_UID);
    if (!glue || glue->magic != GLUE_RQT_MAGIC_TAG) goto OnErrorExit0;

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
    GLUE_DBG_ERROR(glue, errorMsg);
OnErrorExit0:
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    return NULL;
}

static PyObject *GluePingTest(PyObject *self, PyObject *argsP)
{
    static long count=0;
    long tid= pthread_self();
    fprintf (stderr, "GluePingTest count=%ld tid=%ld\n", count++, tid);
    return PyLong_FromLong(tid);
}

static PyMethodDef MethodsDef[] = {
    {"error"         , GluePrintError    , METH_VARARGS, "print level AFB_SYSLOG_LEVEL_ERROR"},
    {"warning"       , GluePrintWarning  , METH_VARARGS, "print level AFB_SYSLOG_LEVEL_WARNING"},
    {"notice"        , GluePrintNotice   , METH_VARARGS, "print level AFB_SYSLOG_LEVEL_NOTICE"},
    {"info"          , GluePrintInfo     , METH_VARARGS, "print level AFB_SYSLOG_LEVEL_INFO"},
    {"debug"         , GluePrintDebug    , METH_VARARGS, "print level AFB_SYSLOG_LEVEL_DEBUG"},
    {"ping"          , GluePingTest      , METH_VARARGS, "Check afb-libpython is loaded"},
    {"binder"        , GlueBinderConf    , METH_VARARGS, "Configure and create afbMain glue"},
    {"config"        , GlueGetConfig     , METH_VARARGS, "Return glue handle full/partial config"},
    {"apiadd"        , GlueApiAdd        , METH_VARARGS, "Add a new API to the binder"},
    {"apiimport"     , GlueApiImport     , METH_VARARGS, "Import a new API to the binder"},
    {"apicreate"     , GlueApiCreate     , METH_VARARGS, "Create a new API to the binder"},
    {"loopstart"     , GlueLoopStart     , METH_VARARGS, "Activate mainloop and exec startup callback"},
    {"reply"         , GlueReply         , METH_VARARGS, "Explicit response tp afb request"},
    {"binding"       , GlueBindingLoad   , METH_VARARGS, "Load binding an expose corresponding api/verbs"},
    {"callasync"     , GlueCallAsync     , METH_VARARGS, "AFB asynchronous subcall"},
    {"callsync"      , GlueCallSync      , METH_VARARGS, "AFB synchronous subcall"},
    {"verbadd"       , GlueVerbAdd       , METH_VARARGS, "Add a verb to a non sealed API"},
    {"evtsubscribe"  , GlueEvtSubscribe  , METH_VARARGS, "Subscribe to event"},
    {"evtunsubscribe", GlueEvtUnsubscribe, METH_VARARGS, "Unsubscribe to event"},
    {"evthandler"    , GlueEvtHandler    , METH_VARARGS, "Register event callback handler"},
    {"evtdelete"     , GlueEvtDelete     , METH_VARARGS, "Delete event callback handler"},
    {"evtnew"        , GlueEvtNew        , METH_VARARGS, "Create a new event"},
    {"evtpush"       , GlueEvtPush       , METH_VARARGS, "Push a given event"},
    {"timerunref"    , GlueTimerUnref    , METH_VARARGS, "Unref existing timer"},
    {"timeraddref"   , GlueTimerAddref   , METH_VARARGS, "Addref to existing timer"},
    {"timernew"      , GlueTimerNew      , METH_VARARGS, "Create a new timer"},
    {"setloa"        , GlueSetLoa        , METH_VARARGS, "Set LOA (LevelOfAssurance)"},
    {"jobcall"       , GlueJobCall       , METH_VARARGS, "Synchronously call job in the current thread"},
    {"jobenter"      , GlueJobEnter      , METH_VARARGS, "Register a mainloop waiting lock"},
    {"jobleave"      , GlueJobLeave      , METH_VARARGS, "Unlock jobenter"},
    {"jobpost"       , GlueJobPost       , METH_VARARGS, "Post a job after delay(ms)"},
    {"jobabort"      , GlueJobAbort      , METH_VARARGS, "Cancel a jobpost timer"},
    {"clientinfo"    , GlueClientInfo    , METH_VARARGS, "Return session info about client"},
    {"exit"          , GlueExit          , METH_VARARGS, "Exit binder with status"},

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
    int status=0;
    fprintf (stderr, "Entering Python module initialization function %s\n", __FUNCTION__);
    PyObject *module = PyModule_Create(&ModuleDef);

    status= PyType_Ready(&PyResponseType);
    if (status < 0) goto OnErrorExit;

    Py_INCREF(&PyResponseType);
    status= PyModule_AddObject(module, "response", (PyObject*)&PyResponseType);
    if (status < 0) goto OnErrorExit;

    return module;

OnErrorExit:
    return NULL;
}
