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
#include <pthread.h>

#include "py-afb.h"
#include "py-utils.h"

#include <semaphore.h>

// retreive API from py handle
afb_api_t GlueGetApi(GlueHandleT *glue) {
   afb_api_t afbApi;
    switch (glue->magic) {
        case GLUE_API_MAGIC_TAG:
            afbApi= glue->api.afb;
            break;
        case GLUE_RQT_MAGIC_TAG:
            afbApi= afb_req_get_api(glue->rqt.afb);
            break;
        case GLUE_BINDER_MAGIC_TAG:
            afbApi= AfbBinderGetApi(glue->binder.afb);
            break;
        case GLUE_JOB_MAGIC_TAG:
            afbApi= glue->job.apiv4;
            break;
        case GLUE_EVT_MAGIC_TAG:
            afbApi= glue->event.apiv4;
            break;
        case GLUE_TIMER_MAGIC_TAG:
            afbApi= glue->timer.apiv4;
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
                case Afb_Typeid_Predefined_I32:
                case Afb_Typeid_Predefined_U32:
                case Afb_Typeid_Predefined_I64: {
                    /* We suppose here a 64bit arch, where sizeof(long) == sizeof(long long) == 8 */
                    const long *value= (long*)afb_data_ro_pointer(replies[idx]);
                    PyTuple_SetItem(resultP, idx+start, PyLong_FromLong(*value));
                    break;
                }
                case Afb_Typeid_Predefined_U64: {
                    /* We suppose here a 64bit arch, where sizeof(long) == sizeof(long long) == 8 */
                    const unsigned long *value= (unsigned long*)afb_data_ro_pointer(replies[idx]);
                    PyTuple_SetItem(resultP, idx+start, PyLong_FromUnsignedLong(*value));
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
                    afb_data_t cvt;

                    // 1) try convert to JSON-C
                    if (afb_data_convert(replies[idx], &afb_type_predefined_json_c, &cvt) >= 0) {
                        json_object *valueJ = (json_object*)afb_data_ro_pointer(cvt);
                        PyTuple_SetItem(resultP, idx+start, valueJ ? jsonToPyObj(valueJ) : Py_None);
                        afb_data_unref(cvt);
                        break;
                    }

                    // 2) try convert to STRINGZ
                    if (afb_data_convert(replies[idx], &afb_type_predefined_stringz, &cvt) >= 0) {
                        const char *s = (const char*)afb_data_ro_pointer(cvt);
                        PyTuple_SetItem(resultP, idx+start, s ? PyUnicode_FromString(s) : Py_None);
                        afb_data_unref(cvt);
                        break;
                    }

                    // 3) fallback: if a buffer exists, expose bytes
                    size_t sz = afb_data_size(replies[idx]);
                    if (sz > 0) {
                        const void *buf = afb_data_ro_pointer(replies[idx]);
                        PyTuple_SetItem(resultP, idx+start, PyBytes_FromStringAndSize(buf, (Py_ssize_t)sz));
                        break;
                    }

                    errorMsg = "unsupported return data type";
                    goto OnErrorExit;
            }
        }
    }
    return NULL;

OnErrorExit:
    return errorMsg;
}

void GlueVerbose(GlueHandleT *handle, int level, const char *file, int line, const char *func, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    switch (handle->magic)
    {
    case GLUE_API_MAGIC_TAG:
    case GLUE_EVT_MAGIC_TAG:
    case GLUE_JOB_MAGIC_TAG:
        afb_api_vverbose(GlueGetApi(handle), level, file, line, func, fmt, args);
        break;

    case GLUE_RQT_MAGIC_TAG:
        afb_req_vverbose(handle->rqt.afb, level, file, line, func, fmt, args);
        break;

    default:
        afb_vverbose(level, file, line, func, fmt, args);
        break;
    }
    return;
}

void PyInfoDbg (GlueHandleT *handle, enum afb_syslog_levels level, const char*funcname, const char * format, ...) {
    char const *info=NULL, *filename=NULL;
    int linenum=-1;
    va_list args;
    PyCodeObject *code = NULL;

    //PyErr_Print();
    PyObject *typeP, *valueP=NULL, *tracebackP;
    PyErr_Fetch(&typeP, &valueP, &tracebackP);
    if (valueP) info= PyUnicode_AsUTF8(valueP);
    if (tracebackP) {
        PyTracebackObject* traceback = (PyTracebackObject*)tracebackP;
        linenum= traceback->tb_lineno;
#if PY_VERSION_HEX >= 0x030a0000
        code = PyFrame_GetCode(traceback->tb_frame);
        filename= PyUnicode_AsUTF8(code->co_filename);
#else
        filename= PyUnicode_AsUTF8(traceback->tb_frame->f_code->co_filename);
#endif
        if (filename) funcname=filename;
    }

    GlueVerbose(handle, level, info, linenum, funcname, format, args);
    Py_XDECREF(code);
}

// reference: https://bbs.archlinux.org/viewtopic.php?id=31087
void PyPrintMsg (enum afb_syslog_levels level, PyObject *self, PyObject *args) {
    char const* errorMsg=NULL;
    char const* filename=NULL;
    char const* funcname=NULL;
    int linenum=0;
    PyCodeObject *code = NULL;

    if (level > AFB_SYSLOG_LEVEL_NOTICE) {
        // retreive debug info looping on frame would pop Python calling trace
        PyThreadState *ts= PyThreadState_Get();
#if PY_VERSION_HEX >= 0x030a0000
        PyFrameObject *frame= PyThreadState_GetFrame(ts);
        if (frame != 0)
        {
            code = PyFrame_GetCode(frame);
            filename = _PyUnicode_AsString(code->co_filename);
            linenum  = PyFrame_GetLineNumber(frame);
            funcname = _PyUnicode_AsString(code->co_name);
            Py_DECREF(frame);
#else
        PyFrameObject *frame= ts->frame;
        if (frame != 0)
        {
            filename = _PyUnicode_AsString(frame->f_code->co_filename);
            linenum  = PyCode_Addr2Line(frame->f_code, frame->f_lasti);
            funcname = _PyUnicode_AsString(frame->f_code->co_name);
#endif
        }
    }

    Py_ssize_t tupleSize = PyTuple_Size(args);
    if (tupleSize < 2) {
        errorMsg= "syntax error: not enough arguments for afbprint(handle, format, ...)";
        goto OnErrorExit;
    }

    PyObject *afbHandleP= PyTuple_GetItem(args,0);
    if (!PyCapsule_CheckExact(afbHandleP)) {
        errorMsg= "syntax afbprint(handle: is not a valid opaque Glue handle)";
        goto OnErrorExit;
    }

    GlueHandleT *handle= PyCapsule_GetPointer (afbHandleP, GLUE_AFB_UID);
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
    Py_XDECREF(code);
    return;

OnErrorExit:
    PyErr_SetString(PyExc_RuntimeError, errorMsg);
    PyErr_Print();
    Py_XDECREF(code);
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
    PyCodeObject *code = NULL;

    PyObject *typeP, *valueP, *tracebackP;
    PyErr_Fetch(&typeP, &valueP, &tracebackP);
    if (valueP) {
        info= PyUnicode_AsUTF8(valueP);
        if (!info) {
            // certain Python errors trigger NameError exceptions in the
            // interpreter but are not reported via valueP. We thus mention this
            // here to hint the user as what to do.
            // This is the case with references to a global from function scope
            // w/o having declared it in the global scope
            info = "unspecified Python error (likely NameError). Check the statement scope.";
        }
    }

    if (tracebackP) {
        PyTracebackObject* traceback = (PyTracebackObject*)tracebackP;
        linenum= traceback->tb_lineno;
#if PY_VERSION_HEX >= 0x030a0000
        code = PyFrame_GetCode(traceback->tb_frame);
        filename= PyUnicode_AsUTF8(code->co_filename);
        funcname= PyUnicode_AsUTF8(code->co_name);
#else
        filename= PyUnicode_AsUTF8(traceback->tb_frame->f_code->co_filename);
        funcname= PyUnicode_AsUTF8(traceback->tb_frame->f_code->co_name);
#endif
    }

    errorJ = json_object_new_object();
    if (message != NULL)
        json_object_object_add(errorJ, "message", json_object_new_string(message));
    if (filename != NULL)
        json_object_object_add(errorJ, "source", json_object_new_string(filename));
    if (linenum != 0)
        json_object_object_add(errorJ, "line", json_object_new_int(linenum));
    if (funcname != NULL)
        json_object_object_add(errorJ, "name", json_object_new_string(funcname));
    if (info != NULL)
        json_object_object_add(errorJ, "info", json_object_new_string(info));
    Py_XDECREF(code);
    return (errorJ);
}

// move from python object representation to json_object representation
json_object *pyObjToJson(PyObject* objP)
{
    json_object *valueJ = NULL;

    if (PyBool_Check(objP))
        valueJ = json_object_new_boolean((json_bool)PyLong_AsLong(objP));

    else if (PyLong_Check(objP)) {
        // We suppose a 64bit arch where sizeof(int) == 4, sizeof(long) == 8
        int overflow = 0;
        long longValue = PyLong_AsLongAndOverflow(objP, &overflow);
        if (overflow) {
            PyErr_SetString(PyExc_ValueError, "A Python integer overflows the supported size of JSON integers");
            return NULL;
        }
        int intValue = (int)longValue;
        if (intValue == longValue)
            valueJ = json_object_new_int(intValue);
        else
            valueJ = json_object_new_int64(longValue);
    }

    else if (PyFloat_Check(objP))
        valueJ = json_object_new_double(PyFloat_AsDouble(objP));

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
            const char *funcname;
            PyObject *funcnameP= PyDict_GetItemString(objP, "__name__");
            if (funcnameP) {
                funcname= strdup(PyUnicode_AsUTF8(funcnameP));
            }
            else {
                // Note: should this be a fatal error?
                funcname="UnknownCallbackFuncName";
            }
            valueJ = json_object_new_string(funcname);
            json_object_set_userdata(valueJ, objP, PyFreeJsonCtx);
            Py_IncRef(objP);
            if (funcnameP) Py_DecRef(funcnameP);
    }
    else {
        LIBAFB_ERROR("pyObjToJson: Unsupported value=%s", PyUnicode_AsUTF8(objP));
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
        LIBAFB_NOTICE("PyPushOneArg: NULL object type %s", json_object_to_json_string(argsJ));
#if PY_VERSION_HEX >= 0x030a0000
        resultP=Py_NewRef(Py_None);
#else
        resultP=Py_None;
        Py_INCREF(resultP);
#endif
        break;
    default:
        LIBAFB_ERROR("PyPushOneArg: unsupported Json object type %s", json_object_to_json_string(argsJ));
        goto OnErrorExit;
    }
    return resultP;

OnErrorExit:
    return NULL;
}

static void PyRqtFree(void *userdata)
{
    GlueHandleT *glue= (GlueHandleT*)userdata;
    assert (glue && (glue->magic == GLUE_RQT_MAGIC_TAG));

    free(glue);
    return;
}

// add a reference on Glue handle
void PyRqtAddref(GlueHandleT *glue) {
    if (glue->magic == GLUE_RQT_MAGIC_TAG) {
        afb_req_addref (glue->rqt.afb);
    }
}

// add a reference on Glue handle
void PyRqtUnref(GlueHandleT *glue) {
    if (glue->magic == GLUE_RQT_MAGIC_TAG) {
        afb_req_unref (glue->rqt.afb);
    }

}

// allocate and push a py request handle
GlueHandleT *PyRqtNew(afb_req_t afbRqt)
{
    assert(afbRqt);

    GlueHandleT *glue = (GlueHandleT *)calloc(1, sizeof(GlueHandleT));
    if (glue != NULL) {
        glue->magic = GLUE_RQT_MAGIC_TAG;
        glue->rqt.afb = afbRqt;

        // add py rqt handle to afb request livecycle
        afb_req_v4_set_userdata (afbRqt, (void*)glue, PyRqtFree);
    }
    return glue;
}


// reply afb request only once and unref py handle
int GlueAfbReply(GlueHandleT *glue, long status, long nbreply, afb_data_t *reply)
{
    if (glue->rqt.replied) goto OnErrorExit;

    Py_BEGIN_ALLOW_THREADS
    afb_req_reply(glue->rqt.afb, (int)status, (int)nbreply, reply);
    Py_END_ALLOW_THREADS

    glue->rqt.replied = 1;
    return 0;

OnErrorExit:
    GLUE_DBG_ERROR(glue, "unique response require");
    return -1;
}

char *pyObjToStr(PyObject* objP)
{
    Py_ssize_t sz;
    const char *cstr = PyUnicode_AsUTF8AndSize(objP, &sz);
    char *str = cstr == NULL ? NULL : malloc((size_t)(++sz));
    if (str != NULL)
        memcpy(str, cstr, sz);
    return str;
}


