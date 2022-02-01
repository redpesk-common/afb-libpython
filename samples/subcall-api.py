#!/usr/bin/python3


"""
Copyright 2021 Fulup Ar Foll fulup@iot.bzh
Licence: $RP_BEGIN_LICENSE$ SPDX:MIT https://opensource.org/licenses/MIT $RP_END_LICENSE$

object:
    subcall-api.py
    - 1) load helloworld binding
    - 2) create a 'demo' api requiring 'helloworld' api
    - 3) check helloworld/ping is responsing before exposing http service (mainLoopCb)
    - 4) implement two verbs demo/sync|async those two verb subcall helloworld/testargs in synchronous/asynchronous mode
    - 5) subscribe event request timer event from helloworld-event api
    demo/sync|async|subscribe|unsubscribe can be requested from REST|websocket from a browser on http:localhost:1234

usage
    - from dev tree: LD_LIBRARY_PATH=../afb-libglue/build/src/ py samples/subcall-api.py
    - point your browser at http://localhost:1234/devtools

config: following should match your installation paths
    - devtools alias should point to right path alias= {'/devtools:/usr/share/afb-ui-devtools/binder'},
    - PYTHONPATH:'/my-py-module-path' (to _afbpyglue.so)
    - LD_LIBRARY_PATH:'/my-glulib-path' (to libafb-glue.so

"""

# import libafb python glue
from afbpyglue import libafb
import os

## static variables
count=0

## ping/pong test func
def pingCB(rqt, *args):
    global count
    count += 1
    libafb.notice  (rqt, "From pingCB count=%d", count)
    return (0, {"pong":count}) # implicit response

def helloEventCB (api, name, *data):
    libafb.notice  (api, "helloEventCB name=%s received", name)

def otherEventCB (api, name, *data):
    libafb.notice  (api, "otherEventCB name=%s data=%s", name, *data)

def asyncRespCB(rqt, status, ctx, *args):
    global count
    libafb.notice  (rqt, "asyncRespCB status=%d ctx:'%s', response:'%s'", status, ctx, args)
    libafb.reply (rqt, status, 'async helloworld/testargs', args)

def syncCB(rqt, *args):
    libafb.notice  (rqt, "syncCB calling helloworld/testargs *args=%s", args)
    response= libafb.callsync(rqt, "helloworld","testargs", args[0])

    if response.status != 0:
        libafb.reply (rqt, response.status, 'async helloworld/testargs fail')
    else:
        libafb.reply (rqt, response.status, 'async helloworld/testargs success')

def asyncCB(rqt, *args):
    userdata= "context-user-data"
    libafb.notice  (rqt, "asyncCB calling helloworld/testargs *args=%s", args)
    libafb.callasync (rqt,"helloworld", "testargs", asyncRespCB, userdata, args[0])
    # response within 'asyncRespCB' callback

def subscribeCB(rqt, *args):
    libafb.notice  (rqt, "subscribeCB helloworld-event/subscribe")
    response= libafb.callsync(rqt, "helloworld-event","subscribe")
    return (response.status) # implicit response

def unsubscribeCB(rqt, *args):
    libafb.notice  (rqt, "unsubscribeCB helloworld-event/unsubscribe")
    response= libafb.callsync(rqt, "helloworld-event","unsubscribe")
    return (response.status) # implicit response

# api control function
def controlApiCB(api, state):
    apiname= libafb.config(api, "api")
    libafb.notice(api, "api=[%s] state=[%s]", apiname, state)
    if state == 'config':
        libafb.notice(api, "config=%s", libafb.config(api))
    return 0 # ok

# executed when binder and all api/interfaces are ready to serv
def mainLoopCb(binder):
    libafb.notice(binder, "mainLoopCb=[%s]", libafb.config(binder, "uid"))
    # callsync return a tuple (status is [0])
    response= libafb.callsync(binder, "helloworld-event", "startTimer")
    if response.status != 0:
        # force an explicit response
        libafb.notice  (binder, "helloworld-event/startTimer fail status=%d", response.status, response.args)
    return response.status # negative status force loopstart exit

# api verb list
demoVerbs = [
    {'uid':'py-ping'       , 'verb':'ping'       , 'callback':pingCB        , 'info':'py ping demo function'},
    {'uid':'py-synccall'   , 'verb':'sync'       , 'callback':syncCB        , 'info':'synchronous subcall of private api' , 'sample':[{'cezam':'open'}, {'cezam':'close'}]},
    {'uid':'py-asyncall'   , 'verb':'async'      , 'callback':asyncCB       , 'info':'asynchronous subcall of private api', 'sample':[{'cezam':'open'}, {'cezam':'close'}]},
    {'uid':'py-subscribe'  , 'verb':'subscribe'  , 'callback':subscribeCB   , 'info':'Subscribe hello event'},
    {'uid':'py-unsubscribe', 'verb':'unsubscribe', 'callback':unsubscribeCB , 'info':'Unsubscribe event'},
]

demoEvents = [
    {'uid':'py-event' , 'pattern':'helloworld-event/timerCount', 'callback':helloEventCB , 'info':'timer event handler'},
    {'uid':'py-other' , 'pattern':'*', 'callback':otherEventCB , 'info':'any other event handler'},
]

# define and instantiate API
demoApi = {
    'uid'     : 'py-demo',
    'api'     : 'demo',
    'provide' : 'test',
    'info'    : 'py subcall demo',
    'verbose' : 9,
    'export'  : 'public',
    'require' : 'helloworld',
    'control' : controlApiCB,
    'events'  : demoEvents,
    'verbs'   : demoVerbs,
    'alias'   : ['/devtools:/usr/share/afb-ui-devtools/binder'],

}

# helloworld binding sample definition
HelloBinding = {
    'uid'    : 'helloworld',
    'export' : 'private',
    'path'   : 'afb-helloworld-skeleton.so',
}

# helloworld binding sample definition
EventBinding = {
    'uid'    : 'subscribe',
    'export' : 'private',
    'path'   : 'afb-helloworld-subscribe-event.so',
}

# define and instantiate libafb-binder
BinderOpts = {
    'uid'     : 'py-binder',
    'port'    : 1234,
    'verbose' : 9,
    'roothttp': './conf.d/project/htdocs',
    'rootdir' : '.',
    'ldpath' : [os.getenv('HOME') + '/opt/helloworld-binding/lib','/usr/local/helloworld-binding/lib'],
}

# create and start binder
libafb.binder(BinderOpts)
libafb.binding(HelloBinding)
libafb.binding(EventBinding)
libafb.apiadd(demoApi)

# should never return
status= libafb.loopstart(mainLoopCb)
if status < 0:
    libafb.error (binder, "OnError loopstart Exit")
else:
    libafb.notice(binder, "OnSuccess loopstart Exit")
