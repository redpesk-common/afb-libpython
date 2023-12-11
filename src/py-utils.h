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

#include <Python.h>
#include <json-c/json.h>

void PyThreadSave(void);
void PyThreadRestore(void);

void PyInfoDbg (GlueHandleT *handle, enum afb_syslog_levels level, const char*funcname, const char * format, ...) ;
void PyPrintMsg (enum afb_syslog_levels level, PyObject *self, PyObject *args);
void GlueVerbose (GlueHandleT *afbHandle, int level, const char *file, int line, const char *func, const char *fmt, ...);
json_object *PyJsonDbg(const char *message);
#define GLUE_AFB_INFO(Glue,...)    GlueVerbose (Glue,AFB_SYSLOG_LEVEL_INFO,__file__,__LINE__,__func__,__VA_ARGS__)
#define GLUE_AFB_NOTICE(Glue,...)  GlueVerbose (Glue,AFB_SYSLOG_LEVEL_NOTICE,__file__,__LINE__,__func__,__VA_ARGS__)
#define GLUE_AFB_WARNING(Glue,...) GlueVerbose (Glue,AFB_SYSLOG_LEVEL_WARNING,__file__,__LINE__,__func__,__VA_ARGS__)
#define GLUE_AFB_ERROR(Glue,...)   GlueVerbose (Glue,AFB_SYSLOG_LEVEL_ERROR,__file__,__LINE__,__func__,__VA_ARGS__)
#define GLUE_DBG_ERROR(Glue,...)   PyInfoDbg (Glue, AFB_SYSLOG_LEVEL_ERROR, __func__, __VA_ARGS__);

char *pyObjToStr(PyObject* objP);
json_object *pyObjToJson(PyObject* objP);
PyObject * jsonToPyObj(json_object *argsJ);
void PyFreeJsonCtx (json_object *configJ, void *userdata) ;

GlueHandleT *PyRqtNew(afb_req_t afbRqt);
void PyRqtAddref(GlueHandleT *pyRqt);
void PyRqtUnref(GlueHandleT *pyRqt);
int InitPrivateData (GlueHandleT*glue);

afb_api_t GlueGetApi(GlueHandleT*glue);
int GlueAfbReply(GlueHandleT *glue, long status, long nbreply, afb_data_t *reply);
const char *PyPushAfbReply (PyObject *responseP, int start, unsigned nreplies, const afb_data_t *replies);