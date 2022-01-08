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
    - 5rd attaches two events handler to the API evttimerCB/evtOtherCB those handlers requiere a subcall to subscribe some event
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
pyEvent=None

# timer handle callback
def timerCB (timer, evt):
    global tic
    tic += 1
    libafb.notice (evt, "timer':%s' event:%s' count=%d,", libafb.config(timer, 'uid'), libafb.config(evt, 'uid'), tic)
    libafb.evtpush(evt, {'tic':tic})
    #return -1 # should exit timer (TBD Jose afb_timer_unref )

 # ping/pong event func
def pingCB(rqt, *args):
    global count
    count += 1
    libafb.notice  (rqt, "pingCB count=%d", count)
    return (0, {"pong":count}) # implicit response

def subscribeCB(rqt, *args):
    libafb.notice  (rqt, "subscribing api event")
    libafb.evtsubscribe (rqt, pyEvent)
    return 0 # implicit respond

def unsubscribeCB(rqt, *args):
    libafb.notice  (rqt, "subscribing api event")
    libafb.evtunsubscribe (rqt, pyEvent)
    return 0 # implicit respond

def evttimerCB (api, name, data):
    libafb.notice  (rqt, "evttimerCB name=%s data=%s", name, data)

def evtOtherCB (api, name, data):
    libafb.notice  (rqt, "evtOtherCB name=%s data=%s", name, data)


# When Api ready (state==init) start event & timer
def apiControlCb(api, state):
    global pyEvent

    apiname= libafb.config(api, "api")
    #WARNING: from Python 3.10 use switch-case as elseif replacement
    if state == 'config':
        libafb.notice(api, "api=[%s] 'info':[%s]", apiname, libafb.config(api, 'info'))

    elif state == 'ready':
        tictime= libafb.config(api,'tictime')*1000 # move from second to ms
        libafb.notice(api, "api=[%s] start event tictime=%dms", apiname, tictime)

        pyEvent= libafb.evtnew (api,{'uid':'py-event', 'info':'py testing event sample'})
        if (pyEvent is None):
            raise Exception ('fail to create event')

        timer= libafb.timernew (api, {'uid':'py-timer','callback':timerCB, 'period':tictime, 'count':0}, pyEvent)
        if (timer is None):
            raise Exception ('fail to create timer')

    elif state == 'orphan':
        libafb.warning(api, "api=[%s] receive an orphan event", apiname)

    return 0 # 0=ok -1=fatal

# executed when binder and all api/interfaces are ready to serv
def mainLoopCb(binder):
    libafb.notice(binder, "startBinderCb=[%s]", libafb.config(binder, "uid"))
    # implement here after your startup/eventing code
    # ...
    return 0 # negative status force mainloop exit

# api verb list
apiVerbs = [
    {'uid':'py-ping'       , 'verb':'ping'       , 'callback':pingCB       , 'info':'ping event def'},
    {'uid':'py-subscribe'  , 'verb':'subscribe'  , 'callback':subscribeCB  , 'info':'subscribe to event'},
    {'uid':'py-unsubscribe', 'verb':'unsubscribe', 'callback':unsubscribeCB, 'info':'unsubscribe to event'},
]

apiEvents = [
    {'uid':'py-event' , 'pattern':'py-event', 'callback':evttimerCB , 'info':'timer event handler'},
    {'uid':'py-other' , 'pattern':'*', 'callback':evtOtherCB , 'info':'any other event handler'},
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
    'events'  : apiEvents,
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
status= libafb.mainloop(mainLoopCb)
if status < 0:
    libafb.error (binder, "OnError MainLoop Exit")
else:
    libafb.notice(binder, "OnSuccess Mainloop Exit")