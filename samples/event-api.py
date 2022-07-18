#!/usr/bin/python3

"""
Copyright 2021 Fulup Ar Foll fulup@iot.bzh
Licence: $RP_BEGIN_LICENSE$ SPDX:MIT https://opensource.org/licenses/MIT $RP__LICENSE$

object:
    event-api.py
    - 1st create 'demo' api
    - 2nd when API ready, create an event named 'py-event'
    - 3rd creates a timer(py-timer) that tic every 3s and call timerCB that fire previsouly created event
    - 4rd implements two verbs demo/subscribe and demo/unscribe
    demo/subscribe|unsubscribe can be requested from REST|websocket from a browser on http:localhost:1234

usage
    - from dev tree: LD_LIBRARY_PATH=../afb-libglue/build/src/ py samples/event-api.py
    - point your browser at http://localhost:1234/devtools

config: following should match your installation paths
    - devtools alias should point to right path alias= {'/devtools:/usr/share/afb-ui-devtools/binder'},
    - PYTHONPATH':/my-py-module-path' (to _afbpyglue.so)
    - LD_LIBRARY_PATH':/my-glulib-path' (to libafb-glue.so
"""

# import libafb python glue
from afbpyglue import libafb

# static variables
count=0
tic=0
evtid=None

# timer handle callback
def timerCB (timer, count, userdata):
    global tic
    tic += 1
    libafb.notice (timer, "timer':%s' tic=%d, userdata=%s", libafb.config(timer, 'uid'), tic, userdata)
    libafb.evtpush(evtid, {'tic':tic})
    #return -1 # should exit timer

 # ping/pong event func
def pingCB(rqt, *args):
    global count
    count += 1
    libafb.notice  (rqt, "pingCB count=%d", count)
    return (0, {"pong":count}) # implicit response

def subscribeCB(rqt, *args):
    libafb.notice  (rqt, "subscribing api event")
    libafb.evtsubscribe (rqt, evtid)
    return 0 # implicit respond

def unsubscribeCB(rqt, *args):
    libafb.notice  (rqt, "unsubscribing api event")
    libafb.evtunsubscribe (rqt, evtid)
    return 0 # implicit respond


# When Api ready (state==init) start event & timer
def apiControlCb(api, state):
    global evtid

    apiname= libafb.config(api, "api")
    #WARNING: from Python 3.10 use switch-case as elseif replacement
    if state == 'config':
        libafb.notice(api, "api=[%s] 'info':[%s]", apiname, libafb.config(api, 'info'))

    elif state == 'ready':
        tictime= libafb.config(api,'tictime')*1000 # move from second to ms
        libafb.notice(api, "api=[%s] start event tictime=%dms", apiname, tictime)

        evtid= libafb.evtnew (api,'py-event')
        if (evtid is None):
            raise Exception ('fail to create event')

        timer= libafb.timernew (api, {'uid':'py-timer','callback':timerCB, 'period':tictime, 'count':0}, ["my_user-data"])
        if (timer is None):
            raise Exception ('fail to create timer')

    elif state == 'orphan':
        libafb.warning(api, "api=[%s] receive an orphan event", apiname)

    return 0 # 0=ok -1=fatal

# executed when binder and all api/interfaces are ready to serv
def mainLoopCb(binder, nohandle):
    libafb.notice(binder, "startBinderCb=[%s]", libafb.config(binder, "uid"))
    # implement here after your startup/eventing code
    # ...
    return 0 # negative status force loopstart exit

# api verb list
apiVerbs = [
    {'uid':'py-ping'       , 'verb':'ping'       , 'callback':pingCB       , 'info':'ping event def'},
    {'uid':'py-subscribe'  , 'verb':'subscribe'  , 'callback':subscribeCB  , 'info':'subscribe to event'},
    {'uid':'py-unsubscribe', 'verb':'unsubscribe', 'callback':unsubscribeCB, 'info':'unsubscribe to event'},
]

# define and instanciate API
apiOpts = {
    'uid'     : 'py-event',
    'info'    : 'py api event demonstration',
    'api'     : 'event',
    'provide' : 'py-test',
    'verbose' : 9,
    'export'  : 'public',
    'control' : apiControlCb,
    'tictime' : 3,
    'verbs'   : apiVerbs,
    'alias'  : ['/devtools:/usr/share/afb-ui-devtools/binder'],
}

# define and instanciate libafb-binder
binderOpts = {
    'uid'     : 'py-binder',
    'port'    : 1234,
    'verbose' : 9,
    'roothttp': './conf.d/project/htdocs',
    'rootdir' : '.'
}

# create and start binder
binder= libafb.binder(binderOpts)
myapi = libafb.apiadd(apiOpts)

# should never return
status= libafb.loopstart(binder, mainLoopCb)
if status < 0:
    libafb.error (binder, "OnError loopstart Exit")
else:
    libafb.notice(binder, "OnSuccess loopstart Exit")