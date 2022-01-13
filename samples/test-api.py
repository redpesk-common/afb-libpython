#!/usr/bin/python3

"""
Copyright 2021 Fulup Ar Foll fulup@iot.bzh
Licence: $RP_BEGIN_LICENSE$ SPDX:MIT https://opensource.org/licenses/MIT $RP_END_LICENSE$

object:
    test-api.py does not implement new api, but test existing APIs, it:
    - imports helloworld-event binding and api
    - call helloworld-event/startTimer to activate binding timer (1 event per second)
    - call helloworld-event/subscribe to subscribe to event
    - lock mainloop with StartAsyncTest and register the eventHandler (EventReceiveCB) with mainloop lock
    - finally (EventReceiveCB) count 5 events and release the mainloop lock received from StartAsyncTest

usage
    - from dev tree: LD_LIBRARY_PATH=../afb-libglue/build/src/ py samples/test-api.py
    - result of the test position mainloop exit status

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

def EventReceiveCB(evt, name, lock, *data):
    global evtCount
    libafb.notice (evt, "event=%s data=%s", name, data)
    evtCount += 1
    if evtCount == 5:
        libafb.notice (evt, "*** EventReceiveCB releasing lock ***");
        libafb.schedunlock (evt, lock, evtCount) # schedunlock(handle, lock, status)

def EventSubscribe(binder):
    libafb.notice (binder, "helloworld-event", "startTimer")
    status= libafb.callsync(binder, "helloworld-event", "subscribe")[0]
    if status != 0:
        libafb.notice  (binder, "helloworld subscribe-event fail status=%d", status)
    return status

def StartEventTimer(binder):
    libafb.notice (binder, "helloworld-event/startTimer")
    status= libafb.callsync(binder, "helloworld-event", "startTimer")[0]
    if status != 0:
        libafb.notice  (binder, "helloworld event-timer fail status=%d", status)
    return status

def StartAsyncTest(api, lock, context):
    libafb.notice (api, "Schedlock timer-event handler register")
    libafb.evthandler(api, {'uid':'timer-event', 'pattern':'helloworld-event/timerCount','callback':EventReceiveCB}, lock)
    return 0

# executed when binder and all api/interfaces are ready to serv
def startTestCB(binder):
    status=0
    timeout=7 # seconds
    libafb.notice(binder, "startTestCB=[%s]", libafb.config(binder, "uid"))

    # implement here after your startup/testing code
    status= StartEventTimer(binder)
    if status != 0:
       raise Exception ('event-create')

    status= EventSubscribe(binder)
    if status != 0:
       raise Exception ('event-subscribe')

    libafb.notice (binder, "waiting (%ds) for test to finish", timeout)
    status= libafb.schedwait(binder, timeout, StartAsyncTest, None)

    libafb.notice (binder, "test done status=%d", status)
    return(status) # negative status force mainloop exit

# helloworld binding sample definition
bindingOpts = {
    'uid'    : 'helloworld-event',
    'export' : 'private',
    'path'   : 'afb-helloworld-subscribe-event.so',
    'ldpath' : [os.getenv("HOME")+'/opt/helloworld-binding/lib','/usr/local/helloworld-binding/lib'],
    'alias'  : ['/hello:'+os.getenv("HOME")+'/opt/helloworld-binding/htdocs', '/devtools:/usr/share/afb-ui-devtools/binder'],
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
hello = libafb.binding(bindingOpts)
status= 0

try:
    # enter binder main loop and launch test callback
    status=libafb.mainloop(startTestCB)
except:
    libafb.notice(binder, "startTestCB raise an exception")
finally:
    libafb.notice(binder, "Mainloop Exit status=%d", status)

