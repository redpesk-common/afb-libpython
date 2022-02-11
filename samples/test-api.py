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
import sys

## static variables
evtCount=0
evtfd=None

def EventReceiveCB(evt, name, ctx, *data):
    global evtCount
    libafb.notice (evt,"event=%s data=%s count=%d", name, data, evtCount)
    evtCount += 1
    if evtCount >= ctx['count']:
        libafb.notice (evt, "*** EventReceiveCB releasing job ***")
        libafb.jobkill (ctx['job'], evtCount) # jobkill(job, status)

def EventSubscribe(binder, userdata):
    libafb.notice (binder, "helloworld-event", "startTimer")
    response= libafb.callsync(binder, "helloworld-event", "subscribe")
    if response.status != 0:
        libafb.notice  (binder, "helloworld subscribe-event fail status=%d", response.status)
    return status

def StartEventTimer(binder, userdata):
    libafb.notice (binder, "helloworld-event/startTimer")
    response= libafb.callsync(binder, "helloworld-event", "startTimer")
    if response.status != 0:
        libafb.notice  (binder, "helloworld event-timer fail status=%d", response.status)
    return status

def EventWaitCount(job, signum, userdata):
    global evtfd
    libafb.notice (job, "jobstart timer-event handler register")
    ctx= {'job':job, 'count':userdata['count']}
    userdata['evtfd']= libafb.evthandler(job, {'uid':'timer-event', 'pattern':'helloworld-event/timerCount','callback':EventReceiveCB}, ctx)
    return 0

def EventGet5Test(binder, userdata):
    libafb.notice (binder, "waiting (%d) for test to finish", userdata['timeout'])
    status= libafb.jobstart(binder, EventWaitCount, userdata['timeout'], userdata)
    if status < 0:
        libafb.warning (binder, "timeout fused (should increase ?)")
        libafb.evtdelete(userdata['evtfd'])
    return status

# executed when binder and all api/interfaces are ready to serv
def StartTest(binder, testcase):
    #global myTestCase
    status=0
    timeout=5 # seconds
    libafb.notice(binder, "StartTest binder=[%s]", libafb.config(binder, "uid"))

    # loop on all tests
    for test in testcase:
        libafb.info (binder, "testing uid=%s info=%s" % (test['uid'], test['info']))
        status= test['callback'](binder, test['userdata'])
        if (status != test['expect']):
            libafb.error ("test fail uid=%s status=%d info=%s", test['uid'], status, test['info'])
            raise Exception(test.uid)

    libafb.notice (binder, "test done status=%d", status)
    return(status) # negative status force loopstart exit

# helloworld binding sample definition
bindingOpts = {
    'uid'    : 'helloworld-event',
    'export' : 'private',
    'path'   : 'afb-helloworld-subscribe-event.so',
    'ldpath' : [os.getenv("HOME")+'/opt/helloworld-binding/lib','/usr/local/helloworld-binding/lib'],
}

# define and instantiate libafb-binder
binderOpts = {
    'uid'     : 'py-binder',
    'verbose' : 9,
    'port'    : 0, # no httpd
    'roothttp': './conf.d/project/htdocs',
    'rootdir' : '.'
}

# create and start binder
binder= libafb.binder(binderOpts)
hello = libafb.binding(bindingOpts)
status= 0

# minimalist test framework
myTestCase = [
    {'uid':'event-ticstart' ,'callback': StartEventTimer, 'userdata':None,'expect':0, 'info':'start helloworld binding timer'},
    {'uid':'event-subscribe','callback': EventSubscribe , 'userdata':None,'expect':0, 'info':'subscribe to hellworld event'},
    {'uid':'event-getcount' ,'callback': EventGet5Test  , 'userdata':{'timeout':10,'count':5}, 'expect': 5, 'info':'wait for 5 helloworld event'},
]

# enter binder main loop and launch test callback
try:
    status=libafb.loopstart(binder, StartTest, myTestCase)
except Exception:
    libafb.error(binder, "loopstart raise an exception error=%s", sys.exc_info()[0])
finally:
    libafb.notice(binder, "loopstart done status=%d", status)

