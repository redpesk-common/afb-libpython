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
    libafb.notice  (rqt, "From pingCB count=%d", count)
    return (0, {"pong":count}) # implicit response

def argsCB(rqt, *args):
    libafb.notice  (rqt, "actionCB query=%s", args)
    libafb.respond (rqt, 0, {'query': args})

## executed when binder is ready to serv
def loopBinderCb(binder):
    libafb.notice(binder, "loopBinderCb=%s", libafb.config(binder, "uid"))
    return 0 # keep running for ever

## api verb list
demoVerbs = [
    {'uid':'lua-ping', 'verb':'ping', 'callback':pingCB, 'info':'lua ping demo function'},
    {'uid':'lua-args', 'verb':'args', 'callback':argsCB, 'info':'lua check input query', 'sample':[{'arg1':'arg-one', 'arg2':'arg-two'}, {'argA':1, 'argB':2}]},
]

## define and instanciate API
demoApi = {
    'uid'     : 'lua-demo',
    'api'     : 'demo',
    'class'   : 'test',
    'info'    : 'lua api demo',
    'verbose' : 9,
    'export'  : 'public',
    'verbs'   : demoVerbs,
    'alias'  : ['/devtools:/usr/share/afb-ui-devtools/binder'],
}

# define and instantiate libafb-binder
demoOpts = {
    'uid'     : 'lua-binder',
    'port'    : 1234,
    'verbose' : 9,
    'roothttp': './conf.d/project/htdocs',
    'rootdir' : '.',
}

# instantiate binder and API
binder= libafb.binder(demoOpts)
Glue = libafb.apiadd(demoApi)

# enter mainloop
status= libafb.mainloop(loopBinderCb)
if status < 0 :
    libafb.error (binder, "OnError MainLoop Exit")
else:
    libafb.notice(binder, "OnSuccess Mainloop Exit")
