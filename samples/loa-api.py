#!/usr/bin/python3

"""
Copyright 2021 Fulup Ar Foll fulup@iot.bzh
Licence: $RP_BEGIN_LICENSE$ SPDX:MIT https://opensource.org/licenses/MIT $RP_END_LICENSE$

object:
    loa-api.lua loanstrate how to use LOA and permission. While LOA can be tested outside of any context,
    permission check requiere a valid Cynagora installation

    - loa/set current LOA level to 1
    - loa/reset current LOA level to 0
    - loa/check is protected by ACLS and requiere a LOA >=1 it display client session uuid

    Api can be requested from REST|websocket from a browser on http:localhost:1234

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

# static variables
count= 0

 # ping/pong event func
def pingCB(rqt, *args):
    global count
    count += 1
    libafb.notice  (rqt, "pingCB count=%d", count)
    return (0, {"pong":count}) # implicit response

def setLoaCB(rqt, *args):
    libafb.notice  (rqt, "setLoaCB LOA=1")
    libafb.setloa (rqt, 1)
    return 0


def resetLoaCB(rqt, *args):
    libafb.notice (rqt, "resetLoaCB LOA=0")
    libafb.setloa (rqt, 0)
    return 0


def checkLoaCB(rqt, *args):
    libafb.notice (rqt, "Protected API session uuid=%s", libafb.clientinfo(rqt, 'uuid'))
    return 0


# executed when binder and all api/interfaces are ready to serv
def mainLoopCB(binder):
    libafb.notice(binder, "mainLoopCB=[%s]", libafb.config(binder, "uid"))
    return 0 # keep running for ever

# api verb list
loaVerbs = [
    {'uid':'lua-ping' , 'verb':'ping'  , 'callback':pingCB    ,'auth':'anonymous', 'info':'lua ping loa def'},
    {'uid':'lua-set'  , 'verb':'set'   , 'callback':setLoaCB  ,'auth':'anonymous', 'info':'set LOA to 1'},
    {'uid':'lua-reset', 'verb':'reset' , 'callback':resetLoaCB,'auth':'anonymous', 'info':'reset LOA to 0'},
    {'uid':'lua-check', 'verb':'check' , 'callback':checkLoaCB,'auth':'autorized', 'info':'protected API requiere LOA>=1'},
]

# define permissions
loaAlcs = [
    ['anonymous'      , 'loa', 0],
    ['autorized'      , 'loa', 1],
    ['perm-1'         , 'key', 'permission-1'],
    ['perm-2'         , 'key', 'permission-2'],
    ['login-and-roles', 'and', ['perm-2', 'perm-1']],
    ['login-or-roles' , 'or' , ['autorized', 'perm-1']],
]

# define and instanciate API
loaApi = {
    'uid'     : 'lua-loa',
    'api'     : 'loa',
    'provide' : 'test',
    'info'    : 'lua api loa',
    'verbose' : 9,
    'export'  : 'public',
    'verbs'   : loaVerbs,
    'alias'   : ['/devtools:/usr/share/afb-ui-devtools/binder'],
}

# define and instanciate libafb-binder
loaOpts = {
    'uid'     : 'lua-binder',
    'port'    : 1234,
    'verbose' : 9,
    'roothttp': './conf.d/project/htdocs',
    'rootdir' : '.',
    'acls'    : loaAlcs,
}


# create and start binder
binder= libafb.binder(loaOpts)
glue= libafb.apiadd(loaApi)

# should never return
status= libafb.mainloop(mainLoopCB)
if status < 0:
    libafb.error (binder, "OnError MainLoop Exit")
else:
    libafb.notice(binder, "OnSuccess Mainloop Exit")

