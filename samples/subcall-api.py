#!/usr/bin/python3


"""
Copyright 2021 Fulup Ar Foll fulup@iot.bzh
Licence: $RP_BEGIN_LICENSE$ SPDX:MIT https://opensource.org/licenses/MIT $RP_END_LICENSE$

object:
    subcall-api.py
    - 1st load helloworld binding
    - 2nd create a 'demo' api requiring 'helloworld' api
    - 3rd check helloworld/ping is responsing before exposing http service (mainLoopCb)
    - 4rd implement two verbs demo/sync|async those two verb subcall helloworld/testargs in synchronous/asynchronous mode
    demo/synbc|async can be requested from REST|websocket from a browser on http:localhost:1234

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

def asyncRespCB(rqt, status, ctx, *args):
    global count
    libafb.notice  (rqt, "asyncRespCB status=%d ctx:'%s', response:'%s'", status, ctx, args)
    libafb.reply (rqt, status, 'async helloworld/testargs', args)

def syncCB(rqt, *args):
    libafb.notice  (rqt, "syncCB calling helloworld/testargs *args=%s", args)
    status= libafb.callsync(rqt, "helloworld","testargs", args[0])[0]

    if status != 0:
        libafb.reply (rqt, status, 'async helloworld/testargs fail')
    else:
        libafb.reply (rqt, status, 'async helloworld/testargs success')

def asyncCB(rqt, *args):
    userdata= "context-user-data"
    libafb.notice  (rqt, "asyncCB calling helloworld/testargs *args=%s", args)
    libafb.callasync (rqt,"helloworld", "testargs", asyncRespCB, userdata, args[0])
    # response within 'asyncRespCB' callback

# api control function
def startApiCb(api, action):
    apiname= libafb.config(api, "api")
    libafb.notice(api, "api=[%s] action=[%s]", apiname, action)

    if action == 'config':
        libafb.notice(api, "config=%s", libafb.config(api))

    return 0 # ok

# executed when binder and all api/interfaces are ready to serv
def mainLoopCb(binder):
    libafb.notice(binder, "mainLoopCb=[%s]", libafb.config(binder, "uid"))

    # callsync return a tuple (status is [0])
    status= libafb.callsync(binder, "helloworld", "ping")[0]
    if status != 0:
        # force an explicit response
        libafb.notice  (binder, "helloworld/ping fail status=%d", status)

    return status # negative status force mainloop exit

# api verb list
demoVerbs = [
    {'uid':'py-ping'    , 'verb':'ping' , 'callback':pingCB , 'info':'py ping demo function'},
    {'uid':'py-synccall', 'verb':'sync' , 'callback':syncCB , 'info':'synchronous subcall of private api' , 'sample':[{'cezam':'open'}, {'cezam':'close'}]},
    {'uid':'py-asyncall', 'verb':'async', 'callback':asyncCB, 'info':'asynchronous subcall of private api', 'sample':[{'cezam':'open'}, {'cezam':'close'}]},
]

# define and instanciate API
demoApi = {
    'uid'     : 'py-demo',
    'api'     : 'demo',
    'provide' : 'test',
    'info'    : 'py api demo',
    'verbose' : 9,
    'export'  : 'public',
    'require' : 'helloworld',
    'control' : startApiCb,
    'verbs'   : demoVerbs,
}

# helloworld binding sample definition
hellowBinding = {
    'uid'    : 'helloworld',
    'export' : 'private',
    'path'   : 'afb-helloworld-skeleton.so',
    'ldpath' : [os.getenv('HOME') + '/opt/helloworld-binding/lib','/usr/local/helloworld-binding/lib'],
    'alias'  : ['/hello:'+os.getenv("HOME")+'/opt/helloworld-binding/htdocs','/devtools:/usr/share/afb-ui-devtools/binder'],
}

# define and instanciate libafb-binder
demoOpts = {
    'uid'     : 'py-binder',
    'port'    : 1234,
    'verbose' : 9,
    'roothttp': './conf.d/project/htdocs',
    'rootdir' : '.',
}

# create and start binder
binder= libafb.binder(demoOpts)
hello = libafb.binding(hellowBinding)
glue= libafb.apiadd(demoApi)

# should never return
status= libafb.mainloop(mainLoopCb)
if status < 0:
    libafb.error (binder, "OnError MainLoop Exit")
else:
    libafb.notice(binder, "OnSuccess Mainloop Exit")
