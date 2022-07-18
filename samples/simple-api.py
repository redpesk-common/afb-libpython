#!/usr/bin/python3

"""
Copyright 2021 Fulup Ar Foll fulup@iot.bzh
Licence: $RP_BEGIN_LICENSE$ SPDX:MIT https://opensource.org/licenses/MIT $RP_END_LICENSE$

object:
    simple-api.python create a single api(demo) with two verbs 'ping' + 'args'
    this api can be requested from REST|websocket from a browser on http:localhost:1234

usage
    - from dev tree: LD_LIBRARY_PATH=../afb-libglue/build/src/ python samples/simple-api.python
    - point your browser at http://localhost:1234/devtools

config: following should match your installation paths
    - devtools alias should point to right path alias= {'/devtools:/usr/share/afb-ui-devtools/binder'},
    - PYTHONPATH='/my-py-module-path' (to _afbpyglue.so)
    - LD_LIBRARY_PATH='/my-glulib-path' (to libafb-glue.so
"""

# import libafb python glue
from afbpyglue import libafb

## static variables
count=0

## ping/pong test func
def pingCB(rqt, *args):
    global count
    count += 1
    return (0, {"pong":count}) # implicit response

def argsCB(rqt, *args):
    libafb.notice  (rqt, "actionCB query=%s", args)
    libafb.reply (rqt, 0, {'query': args})

## executed when binder is ready to serv
def loopBinderCb(binder, nohandle):
    libafb.notice(binder, "loopBinderCb=%s", libafb.config(binder, "uid"))
    return 0 # keep running for ever

## api verb list
demoVerbs = [
    {'uid':'py-ping', 'verb':'ping', 'callback':pingCB, 'info':'py ping demo function'},
    {'uid':'py-args', 'verb':'args', 'callback':argsCB, 'info':'py check input query', 'sample':[{'arg1':'arg-one', 'arg2':'arg-two'}, {'argA':1, 'argB':2}]},
]

## define and instantiate API
demoApi = {
    'uid'     : 'py-demo',
    'api'     : 'demo',
    'class'   : 'test',
    'info'    : 'py api demo',
    'verbose' : 9,
    'export'  : 'public',
    'verbs'   : demoVerbs,
    'alias'  : ['/devtools:/usr/share/afb-ui-devtools/binder'],
}

# define and instantiate libafb-binder
demoOpts = {
    'uid'     : 'py-binder',
    'port'    : 1234,
    'verbose' : 9,
    'roothttp': './conf.d/project/htdocs',
    'rootdir' : '.',
}

# instantiate binder and API
binder= libafb.binder(demoOpts)
myapi = libafb.apiadd(demoApi)

# enter loopstart
status= libafb.loopstart(binder, loopBinderCb)
if status < 0 :
    libafb.error (binder, "OnError loopstart Exit")
else:
    libafb.notice(binder, "OnSuccess loopstart Exit")
