#!/usr/bin/python3

"""
Copyright 2021 Fulup Ar Foll fulup@iot.bzh
Licence: $RP_BEGIN_LICENSE$ SPDX:MIT https://opensource.org/licenses/MIT $RP_END_LICENSE$

object:
    test-api.py does not implement new api, but test existing APIs, it:
    - imports helloworld-event binding and api
    - call helloworld-event/startTimer to activate binding timer (1 event per second)
    - call helloworld-event/subscribe to subscribe to event
    - lock loopstart with aSyncEvtCheck and register the eventHandler (EventReceiveCB) with loopstart lock
    - finally (EventReceiveCB) count 5 events and release the loopstart lock received from aSyncEvtCheck

usage
    - from dev tree: LD_LIBRARY_PATH=../afb-libglue/build/src/ py samples/test-api.py
    - result of the test position loopstart exit status

config: following should match your installation paths
    - devtools alias should point to right path alias= {'/devtools:/usr/share/afb-ui-devtools/binder'},
    - PYTHONPATH='/my-py-module-path' (to _afbpyglue.so)
    - LD_LIBRARY_PATH='/my-glulib-path' (to libafb-glue.so
"""
# import libafb python glue
from afbpyglue import libafb
import os

## static variables
evtCount=0
evtfd=None

def EventReceiveCB(evt, name, job, *data):
    global evtCount
    libafb.notice (evt, "event=%s data=%s", name, data)
    evtCount += 1
    if evtCount >= 5:
        libafb.notice (evt, "*** EventReceiveCB releasing job ***");
        libafb.jobkill (evt, job, evtCount) # jobkill(handle, lock, status)

def SyncEvtSub(binder):
    libafb.notice (binder, "helloworld-event", "startTimer")
    response= libafb.callsync(binder, "helloworld-event", "subscribe")
    if response.status != 0:
        libafb.notice  (binder, "helloworld subscribe-event fail status=%d", response.status)
    return status

def SyncStartTimer(binder):
    libafb.notice (binder, "helloworld-event/startTimer")
    response= libafb.callsync(binder, "helloworld-event", "startTimer")
    if response.status != 0:
        libafb.notice  (binder, "helloworld event-timer fail status=%d", response.status)
    return status

def aSyncEvtCheck(api, job, context):
    global evtfd
    libafb.notice (api, "Schedlock timer-event handler register")
    evtfd= libafb.evthandler(api, {'uid':'timer-event', 'pattern':'helloworld-event/timerCount','callback':EventReceiveCB}, job)
    return 0

# executed when binder and all api/interfaces are ready to serv
def startTestCB(binder):
    status=0
    timeout=5 # seconds
    libafb.notice(binder, "startTestCB=[%s]", libafb.config(binder, "uid"))

    # implement here after your startup/testing code
    status= SyncStartTimer(binder)
    if status != 0:
       raise Exception('event-create')

    status= SyncEvtSub(binder)
    if status != 0:
       raise Exception('event-subscribe')

    libafb.notice (binder, "waiting (%ds) for test to finish", timeout)
    status= libafb.jobstart(binder, timeout, aSyncEvtCheck, None)
    if status < 0:
        libafb.warning (evtfd, "timeout fused (should increase ?)")
        libafb.evtdelete(evtfd)
        raise Exception('event-timeout')

    libafb.notice (binder, "test done status=%d", status)
    return(status) # negative status force loopstart exit

# helloworld binding sample definition
bindingOpts = {
    'uid'    : 'helloworld-event',
    'export' : 'private',
    'path'   : 'afb-helloworld-subscribe-event.so',
    'ldpath' : [os.getenv("HOME")+'/opt/helloworld-binding/lib','/usr/local/helloworld-binding/lib'],
    'alias'  : ['/hello:'+os.getenv("HOME")+'/opt/helloworld-binding/htdocs', '/devtools:/usr/share/afb-ui-devtools/binder'],
}

# define and instantiate libafb-binder
binderOpts = {
    'uid'     : 'py-binder',
    'verbose' : 9,
    'roothttp': './conf.d/project/htdocs',
    'rootdir' : '.'
}

# create and start binder
binder= libafb.binder(binderOpts)
hello = libafb.binding(bindingOpts)
status= 0

# enter binder main loop and launch test callback
try:
    status=libafb.loopstart(startTestCB)
except Exception:
    libafb.error(binder, "loopstart raise an exception error=%s", sys.exc_info()[0])
finally:
    libafb.notice(binder, "loopstart done status=%d", status)

