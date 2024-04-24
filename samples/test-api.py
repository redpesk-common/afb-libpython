#!/usr/bin/python3

"""
Copyright 2021 Fulup Ar Foll fulup@iot.bzh
Licence: $RP_BEGIN_LICENSE$ SPDX:MIT https://opensource.org/licenses/MIT $RP_END_LICENSE$

object:
    test-api.py does not implement new api, but test existing APIs, it:
    - imports helloworld-event binding and api
    - call helloworld-event/startTimer to activate binding timer (1 evt per second)
    - call helloworld-event/subscribe to subscribe to evt
    - lock loopstart with aSyncEvtCheck and register the evtHandler (EvtReceiveCB) with loopstart lock
    - finally (EvtReceiveCB) count 5 evts and release the loopstart lock received from aSyncEvtCheck

usage
    - from dev tree: PYTHONPATH=./build/src/ python samples/test-api.py
    - result of the test position loopstart exit status

config: following should match your installation paths
    - devtools alias should point to right path alias= {'/devtools:/usr/share/afb-ui-devtools/binder'},
    - PYTHONPATH='/my-py-module-path' (to libafb.so)
"""

# import libafb python glue
import libafb
import sys

## static variables
evtCount=0
evtfd=None

def EvtReceiveCB(evt, name, ctx, *data):
    global evtCount
    libafb.notice (evt,"evt=%s data=%s count=%d", name, data, evtCount)
    evtCount += 1
    if evtCount >= ctx['count']:
        libafb.notice (evt, "*** EvtReceiveCB releasing job ***")
        libafb.jobleave (ctx['job'], evtCount) # jobleave(job, status)

def EvtSubscribe(binder, userdata):
    response= libafb.callsync(binder, "helloworld-event", "subscribe")
    return response.status

def EvtUnsubscribe(binder, userdata):
    response= libafb.callsync(binder, "helloworld-event", "unsubscribe")
    return response.status

def StartEvtTimer(binder, userdata):
    response= libafb.callsync(binder, "helloworld-event", "startTimer")
    return response.status

def EvtWaitCount(job, signum, userdata):
    global evtfd
    ctx= {'job':job, 'count':userdata['count']}
    userdata['evtfd']= libafb.evthandler(job, {'uid':'timer-evt', 'pattern':'helloworld-event/timerCount','callback':EvtReceiveCB}, ctx)
    return 0

def EvtGet5Test(binder, userdata):
    status= libafb.jobenter(binder, EvtWaitCount, userdata['timeout'], userdata)
    if status < 0:
        libafb.warning (binder, "timeout fused (should increase ?)")
        libafb.evtdelete(userdata['evtfd'])
    return status

# executed when binder and all api/interfaces are ready to serv
def StartTest(binder, testcase):
    #global myTestCase
    status=0
    libafb.notice(binder, "StartTest binder=[%s]", libafb.config(binder, "uid"))

    # loop on all tests
    for test in testcase:
        libafb.info (binder, "testing uid=%s info=%s" % (test['uid'], test['info']))
        status= test['callback'](binder, test['userdata'])
        if (status != test['expect']):
            libafb.error (binder, "test fail uid=%s status=%d info=%s", test['uid'], status, test['info'])
            raise Exception(test.uid)

    libafb.notice (binder, "test done status=%d", status)
    return(1) # 0 would keeps mainloop running

# helloworld binding sample definition
bindingOpts = {
    'uid'    : 'helloworld-event',
    'export' : 'private',
    'path'   : 'afb-helloworld-subscribe-event.so',
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
    {'uid':'evt-ticstart'   ,'callback': StartEvtTimer , 'userdata':None,'expect':0, 'info':'start helloworld binding timer'},
    {'uid':'evt-subscribe'  ,'callback': EvtSubscribe  , 'userdata':None,'expect':0, 'info':'subscribe to hellworld evt'},
    {'uid':'evt-getcount'   ,'callback': EvtGet5Test   , 'userdata':{'timeout':10,'count':5}, 'expect': 5, 'info':'wait for 5 helloworld evt'},
    {'uid':'evt-unsubscribe','callback': EvtUnsubscribe, 'userdata':None, 'expect': 0, 'info':'unsubscribe from evt'},
]

# enter binder main loop and launch test callback
try:
    status=libafb.loopstart(binder, StartTest, myTestCase)
except Exception:
    libafb.error(binder, "loopstart raise an exception error=%s", sys.exc_info()[0])
finally:
    libafb.notice(binder, "loopstart done status=%d", status)
