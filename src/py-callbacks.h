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

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "py-afb.h"
#include "py-utils.h"

void GlueEventCb(void *userdata, const char *event_name,	unsigned nparams, afb_data_x4_t const params[],	afb_api_t api);
void GlueApiEventCb(void *userdata, const char *event_name,	unsigned nparams, afb_data_x4_t const params[],	afb_api_t api);
void GlueApiSubcallCb(void *userdata, int status, unsigned nreplies, afb_data_t const replies[], afb_api_t api);
void GlueRqtSubcallCb(void *userdata, int status, unsigned nreplies, afb_data_t const replies[], afb_req_t req);

void GlueInfoCb(afb_req_t afbRqt, unsigned nparams, afb_data_t const params[]);
void GlueApiVerbCb(afb_req_t afbRqt, unsigned nparams, afb_data_t const params[]);
int  GlueCtrlCb(afb_api_t apiv4, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata);
int  GlueStartupCb(void *config, void *userdata);
void GlueInfoCb(afb_req_t afbRqt, unsigned nparams, afb_data_t const params[]);
void GlueTimerCb (afb_timer_x4_t timer, void *userdata, unsigned decount);

void GlueJobPostCb (int signum, void *userdata);
void GlueJobCallCb (int signum, void *userdata);
void GlueJobEnterCb (int signum, void *userdata, struct afb_sched_lock *afbLock);
void GlueFreeCapculeCb(PyObject *capculeP);
void GlueFreeHandleCb( GlueHandleT *handle);
