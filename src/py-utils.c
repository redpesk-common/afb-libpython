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
#include <pthread.h>

#include <glue-afb.h>
#include <glue-utils.h>

#include "py-afb.h"
#include "py-utils.h"


static pthread_once_t onceKey= PTHREAD_ONCE_INIT;
static pthread_key_t dataKey;

typedef struct {
    pyGlueMagicsE magic;
    PyThreadState *pyInterp;
    void *ctx;
} PyThreadUserDataT;

static void FreePrivateData(void *ctx) {
    PyThreadUserDataT *data= (PyThreadUserDataT*)ctx;
    assert (data && data->magic==GLUE_THREAD_DATA);
    data->magic=-1;
    Py_EndInterpreter(data->pyInterp);
    free (data);
}

static void NewPrivateData (void) {
    pthread_key_create(&dataKey, FreePrivateData);
}

PyThreadState *GetPrivateData(void) {
    // get current interpretor from poisix thread private data
    PyThreadUserDataT *tPrivate = pthread_getspecific(dataKey);
    if (!tPrivate) {
        tPrivate= calloc (1, sizeof(PyThreadUserDataT));
        tPrivate->magic= GLUE_THREAD_DATA;
        tPrivate->pyInterp= Py_NewInterpreter();
        pthread_setspecific(dataKey, tPrivate);
    }

    assert (tPrivate && tPrivate->magic == GLUE_THREAD_DATA);
    return tPrivate->pyInterp;
}

// initialise per thread user data key
int InitPrivateData (AfbHandleT*glue) {
#if PY_MINOR_VERSION < 7
    PyEval_InitThreads(); // from 3.7 this is useless
#endif
    glue->binder.pyState= PyThreadState_Get();
    (void) pthread_once(&onceKey, NewPrivateData);
    return 0;
}

// retreive API from py handle
afb_api_t GlueGetApi(AfbHandleT*glue) {
   afb_api_t afbApi;
    switch (glue->magic) {
        case GLUE_API_MAGIC:
            afbApi= glue->api.afb;
            break;
        case GLUE_RQT_MAGIC:
            afbApi= afb_req_get_api(glue->rqt.afb);
            break;
        case GLUE_BINDER_MAGIC:
            afbApi= AfbBinderGetApi(glue->binder.afb);
            break;
        case GLUE_LOCK_MAGIC:
            afbApi= glue->lock.apiv4;
            break;
        case GLUE_EVT_MAGIC:
            afbApi= glue->evt.apiv4;
            break;
        default:
            afbApi=NULL;
    }
    return afbApi;
}

// retreive subcall response and build PY response
const char *PyPushAfbReply (PyObject *resultP, int start, unsigned nreplies, const afb_data_t *replies) {
    const char *errorMsg=NULL;

    for (int idx = 0; idx < nreplies; idx++)
    {
        if (replies[idx]) {
            switch (afb_typeid(afb_data_type(replies[idx])))  {

                case Afb_Typeid_Predefined_Stringz: {
                    const char *value= (char*)afb_data_ro_pointer(replies[idx]);
                    if (value && value[0]) {
                        PyTuple_SetItem(resultP, idx+start, PyUnicode_FromString(value));
                    } else {
                        PyTuple_SetItem(resultP, idx+start, Py_None);
                    }
                    break;
                }
                case Afb_Typeid_Predefined_Bool: {
                    const long *value= (long*)afb_data_ro_pointer(replies[idx]);
                    PyTuple_SetItem(resultP, idx+start, PyBool_FromLong(*value));
                    break;
                }
                case Afb_Typeid_Predefined_I8:
                case Afb_Typeid_Predefined_U8:
                case Afb_Typeid_Predefined_I16:
                case Afb_Typeid_Predefined_U16:
                case Afb_Typeid_Predefined_I64:
                case Afb_Typeid_Predefined_U64:
                case Afb_Typeid_Predefined_I32:
                case Afb_Typeid_Predefined_U32: {
                    const long *value= (long*)afb_data_ro_pointer(replies[idx]);
                    PyTuple_SetItem(resultP, idx+start, PyBool_FromLong(*value));
                    break;
                }
                case Afb_Typeid_Predefined_Double:
                case Afb_Typeid_Predefined_Float: {
                    const double *value= (double*)afb_data_ro_pointer(replies[idx]);
                    PyTuple_SetItem(resultP, idx+start, PyFloat_FromDouble(*value));
                    break;
                }

                case  Afb_Typeid_Predefined_Json: {
                    afb_data_t data;
                    json_object *valueJ;
                    int err;

                    err = afb_data_convert(replies[idx], &afb_type_predefined_json_c, &data);
                    if (err) {
                        errorMsg= "unsupported json string";
                        goto OnErrorExit;
                    }
                    valueJ= (json_object*)afb_data_ro_pointer(data);
                    PyTuple_SetItem(resultP, idx+start, jsonToPyObj(valueJ));
                    afb_data_unref(data);
                    break;
                }
                case  Afb_Typeid_Predefined_Json_C: {
                    json_object *valueJ= (json_object*)afb_data_ro_pointer(replies[idx]);
                    if (valueJ) {
                        PyTuple_SetItem(resultP, idx+start, jsonToPyObj(valueJ));
                    } else {
                        PyTuple_SetItem(resultP, idx+start, Py_None);
                    }
                    break;
                }
                default:
                    errorMsg= "unsupported return data type";
                    goto OnErrorExit;
            }
        }
    }
    return NULL;

OnErrorExit:
    return errorMsg;
}

void GlueVerbose(AfbHandleT *handle, int level, const char *file, int line, const char *func, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    switch (handle->magic)
    {
    case GLUE_API_MAGIC:
    case GLUE_EVT_MAGIC:
    case GLUE_LOCK_MAGIC:
        afb_api_vverbose(GlueGetApi(handle), level, file, line, func, fmt, args);
        break;

    case GLUE_RQT_MAGIC:
        afb_req_vverbose(handle->rqt.afb, level, file, line, func, fmt, args);
        break;

    default:
        vverbose(level, file, line, func, fmt, args);
        break;
    }
    return;
}

void PyInfoDbg (AfbHandleT *handle, enum afb_syslog_levels level, const char*funcname, const char * format, ...) {
    char const *info=NULL, *filename=NULL;
    int linenum=-1;
    va_list args;

    //PyErr_Print();
    PyObject *typeP, *valueP=NULL, *tracebackP;
    PyErr_Fetch(&typeP, &valueP, &tracebackP);
    if (valueP) info= PyUnicode_AsUTF8(valueP);
    if (tracebackP) {
        PyTracebackObject* traceback = (PyTracebackObject*)tracebackP;
        linenum= traceback->tb_lineno;
        filename= PyUnicode_AsUTF8(traceback->tb_frame->f_code->co_filename);
        if (filename) funcname=filename;
    }

    GlueVerbose(handle, level, info, linenum, funcname, format, args);
}

// reference: https://bbs.archlinux.org/viewtopic.php?id=31087
void PyPrintMsg (enum afb_syslog_levels level, PyObject *self, PyObject *args) {
    char const* errorMsg=NULL;
    char const* filename=NULL;
    char const* funcname=NULL;
    int linenum=0;

    if (level > AFB_SYSLOG_LEVEL_NOTICE) {
        // retreive debug info looping on frame would pop Python calling trace
        PyThreadState *ts= PyThreadState_Get();
        PyFrameObject *frame= ts->frame;
        if (frame != 0)
        {
            filename = _PyUnicode_AsString(frame->f_code->co_filename);
            linenum  = PyCode_Addr2Line(frame->f_code, frame->f_lasti);
            funcname = _PyUnicode_AsString(frame->f_code->co_name);
        }
    }

    Py_ssize_t tupleSize = PyTuple_Size(args);
    if (tupleSize < 2) {
        errorMsg= "syntax afbprint(handle, format, ...)";
        goto OnErrorExit;
    }

    PyObject *afbHandleP= PyTuple_GetItem(args,0);
    if (!PyCapsule_CheckExact(afbHandleP)) {
        errorMsg= "syntax afbprint(handle: is not a valid opaque Glue handle)";
        goto OnErrorExit;
    }

    AfbHandleT *handle= PyCapsule_GetPointer (afbHandleP, GLUE_AFB_UID);
    if (!handle) {
        errorMsg= "syntax afbprint(handle: is not a valid Glue handle)";
        goto OnErrorExit;
    }

    PyObject *formatP= PyTuple_GetItem(args,1);
    if (!PyUnicode_Check(formatP)) {
        errorMsg="Format should be a valid string";
        goto OnErrorExit;
    }
    const char *format= PyUnicode_AsUTF8(formatP);
    if (tupleSize > 2) {
        int count=0, index=0;
        void *param[10];
        json_object *paramJ[10];

        for (int idx=2; idx < tupleSize; idx ++) {
            PyObject *argP= PyTuple_GetItem(args,idx);

            if (PyLong_Check(argP)) {
                param[count++]= (void*)PyLong_AsLong(argP);
            }
            else if (PyUnicode_Check(argP)) {
                param[count++]= (void*)PyUnicode_AsUTF8(argP);
            }
            else if (argP == Py_None) {
                param[count++]=NULL;
            }
            else {
                paramJ[index]= pyObjToJson(argP);
                if (!paramJ[index]) param[count++]=NULL;
                else {
                    param[count++]= (void*)json_object_get_string(paramJ[index]);
                    index ++;
                }
            }

            // reach max number of arguments
            if (count == sizeof(param)/sizeof(void*)) break;

        }
        GlueVerbose(handle, level, filename, linenum, funcname, format, param[0],param[1],param[2],param[3],param[4],param[5],param[6],param[7],param[8],param[9]);

        // release json object of nay
        for (int idx=0; idx < index; idx++) json_object_put(paramJ[idx]);

    } else {
        GlueVerbose(handle, level, filename, linenum, funcname, format);
    }
    PyErr_Clear();
    return;

OnErrorExit:
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    PyErr_Print();
}

void PyFreeJsonCtx (json_object *configJ, void *userdata) {
    PyObject *pyObj= (PyObject*) userdata;
    Py_DecRef (pyObj);
}

json_object *PyJsonDbg(const char *message)
{
    json_object *errorJ;
    char const* filename=NULL;
    char const* funcname=NULL;
    char const* info=NULL;
    int linenum=0;

    PyObject *typeP, *valueP, *tracebackP;
    PyErr_Fetch(&typeP, &valueP, &tracebackP);
    if (valueP) info= PyUnicode_AsUTF8(valueP);
    if (tracebackP) {
        PyTracebackObject* traceback = (PyTracebackObject*)tracebackP;
        linenum= traceback->tb_lineno;
        filename= PyUnicode_AsUTF8(traceback->tb_frame->f_code->co_filename);
        funcname= PyUnicode_AsUTF8(traceback->tb_frame->f_code->co_name);
    }

    wrap_json_pack(&errorJ, "{ss* ss* si* ss* ss*}", "message", message, "source", filename, "line", linenum, "name", funcname, "info", info);
    return (errorJ);
}

// move from python object representation to json_object representation
json_object *pyObjToJson(PyObject* objP)
{
    json_object *valueJ = NULL;

    if (PyLong_Check(objP))
        valueJ = json_object_new_int ((int)PyLong_AsLong(objP));

    else if (PyFloat_Check(objP))
        valueJ = json_object_new_double(PyFloat_AsDouble(objP));

    else if (PyBool_Check(objP))
        valueJ = json_object_new_boolean((json_bool)PyLong_AsLong(objP));

    else if (PyDict_Check(objP)) {
        valueJ = json_object_new_object();
        PyObject *keyP, *slotP;
        Py_ssize_t index = 0;
        while (PyDict_Next(objP, &index, &keyP, &slotP)) {
            const char *key= PyUnicode_AsUTF8(keyP);
            json_object *slotJ= pyObjToJson(slotP);
            json_object_object_add(valueJ, key, slotJ);
        }
    }

    else if (PyList_Check(objP)) {
        valueJ = json_object_new_array();
        for (int idx=0; idx < PyList_GET_SIZE (objP); idx++) {
            PyObject *slotP= PyList_GetItem(objP, idx);
            if (slotP) {
                json_object *slotJ= pyObjToJson(slotP);
                json_object_array_add (valueJ, slotJ);
            }
        }
    }

    else if (PyTuple_Check(objP)) {
        valueJ = json_object_new_array();
        for (int idx=0; idx < PyTuple_GET_SIZE(objP); idx++) {
            PyObject *slotP= PyTuple_GetItem(objP, idx);
            if (slotP) {
                json_object *slotJ= pyObjToJson(slotP);
                json_object_array_add (valueJ, slotJ);
            }
        }
    }

    else if (PyUnicode_Check(objP))
            valueJ = json_object_new_string(PyUnicode_AsUTF8(objP));

    else if (objP == Py_None)
            valueJ= NULL;

    // python function is not json compatible, also we keep it an object userdata context
    else if (PyCallable_Check(objP)) {
            valueJ = json_object_new_string("pyCB");
            json_object_set_userdata(valueJ, objP, PyFreeJsonCtx);
            Py_IncRef(objP);
    }
    else {
        ERROR("pyObjToJson: Unsupported value=%s", PyUnicode_AsUTF8(objP));
        valueJ = NULL;
    }

    return valueJ;
}


// Move from json_object to pythopn object representation
PyObject * jsonToPyObj(json_object *argsJ)
{
    PyObject *resultP;
    int err;

    json_type jtype = json_object_get_type(argsJ);
    switch (jtype)
    {
    case json_type_object:
    {
        resultP= PyDict_New();
        json_object_object_foreach(argsJ, key, valJ)
        {
            PyObject *valP= jsonToPyObj(valJ);
            PyObject *keyP= PyUnicode_FromString(key);
            if (!valP) {
                goto OnErrorExit;
            }
            err= PyDict_SetItem (resultP, keyP, valP);
            if (err) {
                goto OnErrorExit;
            }
        }
        break;
    }
    case json_type_array:
    {
        int length = (int)json_object_array_length(argsJ);
        resultP=PyList_New(length);
        for (int idx = 0; idx < length; idx++)
        {
            json_object *valJ = json_object_array_get_idx(argsJ, idx);
            PyObject *valP= jsonToPyObj(valJ);
            PyList_SetItem(resultP, idx, valP);
        }
        break;
    }
    case json_type_int:
        resultP=PyLong_FromLong((long)json_object_get_int64(argsJ));
        break;
    case json_type_string:
        resultP= PyUnicode_FromStringAndSize(json_object_get_string(argsJ), json_object_get_string_len(argsJ));
        break;
    case json_type_boolean:
        resultP= PyBool_FromLong(json_object_get_boolean(argsJ));
        break;
    case json_type_double:
        resultP= PyFloat_FromDouble(json_object_get_double(argsJ));
        break;
    case json_type_null:
        NOTICE("PyPushOneArg: NULL object type %s", json_object_to_json_string(argsJ));
        resultP=NULL;
        break;
    default:
        ERROR("PyPushOneArg: unsupported Json object type %s", json_object_to_json_string(argsJ));
        goto OnErrorExit;
    }
    return resultP;

OnErrorExit:
    return NULL;
}

static void PyRqtFree(void *userdata)
{
    AfbHandleT *glue= (AfbHandleT*)userdata;
    assert (glue && (glue->magic == GLUE_RQT_MAGIC));

    free(glue);
    return;
}

// add a reference on Glue handle
void PyRqtAddref(AfbHandleT *glue) {
    if (glue->magic == GLUE_RQT_MAGIC) {
        afb_req_unref (glue->rqt.afb);
    }
}

// add a reference on Glue handle
void PyRqtUnref(AfbHandleT *glue) {
    if (glue->magic == GLUE_RQT_MAGIC) {
        afb_req_unref (glue->rqt.afb);
    }

}

// allocate and push a py request handle
AfbHandleT *PyRqtNew(afb_req_t afbRqt)
{
    assert(afbRqt);

    // retreive interpreteur from API
    AfbHandleT *Glue = afb_api_get_userdata(afb_req_get_api(afbRqt));
    assert(Glue->magic == GLUE_API_MAGIC);

    AfbHandleT *glue = (AfbHandleT *)calloc(1, sizeof(AfbHandleT));
    glue->magic = GLUE_RQT_MAGIC;
    glue->rqt.afb = afbRqt;

    // add py rqt handle to afb request livecycle
    afb_req_v4_set_userdata (afbRqt, (void*)glue, PyRqtFree);

    return glue;
}


// reply afb request only once and unref py handle
int GlueReply(AfbHandleT *glue, long status, long nbreply, afb_data_t *reply)
{
    if (glue->rqt.replied) goto OnErrorExit;
    afb_req_reply(glue->rqt.afb, (int)status, (int)nbreply, reply);
    glue->rqt.replied = 1;
    return 0;

OnErrorExit:
    GLUE_DBG_ERROR(glue, "unique response require");
    return -1;
}
