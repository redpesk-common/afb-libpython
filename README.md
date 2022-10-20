# afb-libpython

Exposes afb-libafb to the Python scripting language. This module allows to
script in Python to either mock binding apis, test a client, quick prototyping,
... `afb-libpython` runs as a standard Python C module, providing full access to
`afb-libafb` functionality, subcalls, events, ACLs, loopstart control, etc.

## Dependencies

* `afb-libafb` (from jan/2022 version)
* `afb-libglue`
* `python3`
* `afb-cmake-modules`

## Building

```bash
    mkdir build
    cd build
    cmake ..
    make
```

## Testing

Make sure that your dependencies are reachable from the Python scripting engine, before starting your test.

```bash
    export LD_LIBRARY_PATH=/path/to/'afb-libglue.so'
    export PYTHONPATH=/path/to/'_afbpyglue.so'
    python3 sample/simple-api.python
    #http://localhost:1234/devtools
```

## Debug from codium

Codium does not include the GDP profile by default, you should get it from the Ms-Code repository

Go to the VSCode marketplace and download a version compatible with your editor version:

* https://github.com/microsoft/vscode-cpptools/releases
* https://github.com/microsoft/vscode-python/releases

Install your extension

```bash
codium --install-extension cpptools-linux.vsix
codium --install-extension ms-python-release.vsix
```

WARNING: the latest version is probably not compatible with your codium version.

## Import afb-pythonglue

Your Python script should import `afb-pythonglue`. This require returns a table
which contains the C module api.

```python
    #!/usr/bin/python3

    # Note: afbpyglue should point to __init__.py module loader
    from afbpyglue import libafb
    import os
```

## Configure binder services/options

When running mock binding APIs a very simple configuration like the following one
should be enough. For the full options of `libafb.binder` check the libglue API
documentation.


```python
    # define and instantiate libafb-binder
    binderOpts = {
        'uid'     : 'py-binder',
        'port'    : 1234,
        'verbose' : 9,
        'roothttp': './conf.d/project/htdocs',
        'rootdir' : '.'
    }
    binder= libafb.binder(binderOpts)
```

For HTTPS a certificate and a key should be added. Optionally, a list of aliases
and ldpath might also be added.

```python
    # define and instanciate libafb-binder
    binderOpts = {
        'uid'       : 'py-binder',
        'port'      : 1234,
        'verbose'   : 9,
        'roothttp'  : './conf.d/project/htdocs',
        'rootdir'   : '.'
        'https-cert': '/path/to/my/https.cert',
        'https-key' : '/path/to/my/https.key'
    }
    binder= libafb.binder(binderOpts)
```

## Exposing api/verbs

`afb-libpython` allows users to implement api/verbs directly in a scripting
language. When an api is marked `export=public` the corresponding api/verbs are
visible from HTTP. When `export=private` they remain visible only from internal
calls. `export=restricted` allows to expose an API as a unix socket only, with the
`uri=unix:@api-name` tag.

Expose a new api with ```libafb.apiadd(demoApi)``` as in the following example.

Note that the library automatically exports an `info` verb documenting the
binding based on what was provided into each verb data structure. Attempts to
define one will lead to an error at the library startup time like the following:
```
ERROR: { "uid": "cp-test", "verb": "info", "callback":
"UnknownCallbackFuncName", "info": "ping verb, use it to test the binding is alive",
"error": "verb already exists\/registered" }
```

```python
## ping/pong test func
# The global scope declaration is mandatory here
count = 0

def pingCB(rqt, *args):
    global count
    count += 1
    libafb.notice  (rqt, "From pingCB count=%d", count)
    return (0, {"pong":count}) # implicit response

## api verb list
demoVerbs = [
    {'uid':'py-ping', 'verb':'ping', 'callback':pingCB, 'info':'py ping demo function'},
    {'uid':'py-args', 'verb':'args', 'callback':argsCB, 'info':'py check input query', 'sample':[{'arg1':'arg-one', 'arg2':'arg-two'}, {'argA':1, 'argB':2}]},
]

## define and instanciate an API
demoApi = {
    'uid'     : 'py-demo',
    'api'     : 'demo',
    'class'   : 'test',
    'info'    : 'py api demo',
    'verbose' : 9,
    'export'  : 'public',
    'verbs'   : demoVerbs,
    'alias'   : ['/devtools:/usr/share/afb-ui-devtools/binder'],
}

myapi= libafb.apiadd(demoApi)
```

## Importing an API

`afb-libpython` also allows to import an existing API from a different binder
context into the current binder.

In the following example, the `demo-remote` API is imported from a remote binder
running on host `remote_host` on port `21212` over TCP into the current binder.
It is subsequently made public under its original name, `demo-remote`.

```python
imported_demo_api = {
    'uid'    : 'py-demo-import-api',
    'export' : 'public',
    'uri'    : 'tcp:remote_host:21212/demo-remote',
}
```

This example imports the same API from the same location but marks it as
`restricted`. It is thus made available over a Unix socket only, under a newly
defined name, `demo-remote-over-unix`. Note that it is an error to mark an API
as `restricted` and not provide a new URI to define its exported name.

```python
imported_demo_api = {
    'uid'    : 'py-demo-import-api',
    'export' : 'restricted',
    'uri'    : 'unix:@demo-remote-over-unix',
    'uri'    : 'tcp:remote_host:21212/demo-remote',
}
```

Finally, this example imports the same API but marks it as private:

```python
imported_demo_api = {
    'uid'    : 'py-demo-import-api',
    'export' : 'private',
    'uri'    : 'tcp:remote_host:21212/demo-remote',
}
```

## API/RQT Subcalls

Both synchronous and asynchronous call are supported. The fact that the subcall is
done from a request or from an api context is abstracted to the user. When doing
it from a request context the client security context is not propagated and the
removal events are claimed by the Python API.

Explicit response to a request is done with ```
libafb.reply(rqt,status,arg1,..,argn)```. When running a synchronous request an
implicit response may also be done with ```return(status, arg1,...,arg-n)```.

Note that with AFB v4, an application may return zero, one or many data.

```python
def asyncRespCB(rqt, status, ctx, *args):
    libafb.notice  (rqt, "asyncRespCB status=%d ctx:'%s', response:'%s'", status, ctx, args)
    libafb.reply (rqt, status, 'async helloworld/testargs', args)

def syncCB(rqt, *args):
    libafb.notice  (rqt, "syncCB calling helloworld/testargs *args=%s", args)
    response= libafb.callsync(rqt, "helloworld","testargs", args[0])
    if response.status != 0:
        libafb.reply (rqt, response.status, 'async helloworld/testargs fail')
    else:
        libafb.reply (rqt, response.status, 'async helloworld/testargs success', response.args)

def asyncCB(rqt, *args):
    userdata= "context-user-data"
    libafb.notice  (rqt, "asyncCB calling helloworld/testargs *args=%s", args)
    libafb.callasync (rqt,"helloworld", "testargs", asyncRespCB, userdata, args[0])
    # response within 'asyncRespCB' callback
```

## Events

Event should be attached to an API. As binders automatically have an underlying
(unpublished) API, it is nevertheless possible to start a timer directly from a
binder. Under normal circumstances, events should be created from an API control
callback, when API has `state=='ready'`. Note that it is the developer responsibility
to make the pythonEvent handle visible from the function that creates the event
to the function that uses the event.

```python
    def apiControlCb(api, state):
        global evtid

        apiname= libafb.config(api, "api")
        #WARNING: from Python 3.10 use switch-case as elseif replacement
        if state == 'config':
            libafb.notice(api, "api=[%s] 'info':[%s]", apiname, libafb.config(api, 'info'))

        elif state == 'ready':
            tictime= libafb.config(api,'tictime')*1000 # move from second to ms
            libafb.notice(api, "api=[%s] start event tictime=%dms", apiname, tictime)

            evtid= libafb.evtnew (api,{'uid':'py-event', 'info':'py testing event sample'})
            if (evtid is None):
                raise Exception ('fail to create event')

            timer= libafb.timernew (api, {'uid':'py-timer','callback':timerCB, 'period':tictime, 'count':0}, ["my_user-data"])
            if (timer is None):
                raise Exception ('fail to create timer')

        elif state == 'orphan':
            libafb.warning(api, "api=[%s] receive an orphan event", apiname)

        return 0 # 0=ok -1=fatal

        # later event can be push with evtpush
        libafb.evtpush(evtid, {userdata-1},...,{userdata-n})

```

Client event subscription is handled with the `evtsubscribe|unsubcribe` API.
The subscription API should be called from a request context as in the following
example, extracted from [samples/event-api.py](samples/event-api.py):

```python
    def subscribeCB(rqt, *args):
        libafb.notice  (rqt, "subscribing api event")
        libafb.evtsubscribe (rqt, evtid)
        return 0 # implicit respond

    def unsubscribeCB(rqt, *args):
        libafb.notice  (rqt, "subscribing api event")
        libafb.evtunsubscribe (rqt, evtid)
        return 0 # implicit respond
```

## Timers

Timers are typically used to push events or to handle timeouts. A timer is started
with ```libafb.timernew()```. A timer configuration includes a callback, a ticktime
in ms and a number of runs (count). When `count==0` the timer runs indefinitely.
In the following example, a timer runs forever every 'ticktime' and calls
the `timerCB` function.

```python
    def timerCB (timer, count, userdata):
        libafb.notice  (rqt, "evttimerCB name=%s data=%s", name, userdata)
        # return -1 should terminate timer

    timer= libafb.timernew (api,
        {'uid':'py-timer','callback':timerCB, 'period':ticktime, 'count':0}
        , evtid)
    if (timer is None):
        raise Exception ('failed to create timer')
```

The `afb-libafb` timer API is exposed in Python.

## Binder loopstart

Under normal circumstances the binder loopstart never returns. Nevertheless,
during test phases it is very common to wait for asynchronous events before
deciding if the test is successfully or not.

loopstart is started with `libafb.loopstart(binder, ['xxx', handle])`, where `xxx' is
an optional startup function that controls loopstart execution. There are two ways
to control the loopstart: standard and jobstart modes.

### standard mode

If the startup function returns a non-zero status, the binder immediately exits with
the corresponding status. This case is very typical when running pure
synchronous API tests.

```python
# create binder
binder= libafb.binder(binderOpts)

# enter binder main loop and launch startup callback
status= libafb.loopstart(binder, loopBinderCb)
if status < 0 :
    libafb.error (binder, "OnError loopstart Exit")
else:
    libafb.notice(binder, "OnSuccess loopstart Exit")
```

### jobstart mode

This uses a `schedwait` lock to control the main loop from the asynchronous events.
This later case is mandatory when we have to start the loopstart to listen
to events, but still need to exit to run a new set of tests.

In the following example:
* `jobstartCB` callback starts an event handler and passes the lock as an event context
* the event handler counts the number of events and after 5 events releases the lock.

Notes:

* `libafb.jobstart` does not return before the lock is released. As for events, it
  is the developer responsibility to either carry the lock in a context or to
  store it within a shared space, to order the unlock function to access it.

* it is possible to serialize `libafb.jobstart` in order to build an asynchronous
  cascade of asynchronous tests.

```python

    def EventReceiveCB(evt, name, lock, *data):
        global evtCount
        libafb.notice (evt, "event=%s data=%s", name, data)
        evtCount += 1
        if evtCount == 5:
            libafb.notice (evt, "*** EventReceiveCB releasing lock ***");
            libafb.jobkill (evt, lock, evtCount)

    def jobstartCB(api, lock, context):
        libafb.notice (api, "Schedlock timer-event handler register")
        libafb.evthandler(api, {'uid':'timer-event', 'pattern':'helloworld-event/timerCount','callback':EventReceiveCB}, lock)
        return 0

    # executed when binder and all api/interfaces are ready to serve
    def startTestCB(binder, handle):
        status=0
        timeout=4 # seconds
        libafb.notice(binder, "startTestCB=[%s]", libafb.config(binder, "uid"))

        libafb.notice (binder, "waiting (%ds) for test to finish", timeout)
        status= libafb.jobstart(binder, timeout, jobstartCB, None)

        libafb.notice (binder, "test done status=%d", status)
        return(status) # negative status forces loopstart exit

    # start loopstart
    status=libafb.loopstart(binder, startTestCB, handle)
```

## Error management

In general, in case of an error, the infrastructure will retrieve and display
the underlying Python error like the following syntax issue:
```
$ python3 demo.py
File "/home/michel/demo.py", line 24
    print "Hello!"
    ^^^^^^^^^^^^^^
SyntaxError: Missing parentheses in call to 'print'. Did you mean print(...)?
```

It is possible however that for certain specific errors, the library cannot
retrieve the actual underlying Python error (this looks like a limitation with
the `PyErr_Fetch()` routine).

In this case, a more generic message is displayed:
```
$ python3 demo.py
Entering Python module initialization function PyInit__afbpyglue
NOTICE: Entering binder mainloop
NOTICE: Entering main loop for demo binder
WARNING: [REQ/API cloud-pub] verb=[test] python={ "message": "error during verb callback function call", 
"source": "\/home\/michel\/demo.py", "line": 25, "name": "cp_test_cb", 
"info": "unspecified Python error (likely NameError). Check the statement scope." } 
[/buildroot/afb-libpython/src/py-callbacks.c:190,GlueApiVerbCb]
```

This situation can happen for instance in a callback, when referencing a global
variable which was not actually defined in the outermost/global scope.  Even
though the error message is generic, the actual source code file, function name
and line number which are reported do correctly point at the problematic place
in the source code.

It might also be that running the code in the normal Python interpreter can
yield more information (this might not always be possible though).

## Miscellaneous APIs/utilities

* `libafb.clientinfo(rqt)`: returns client session info.
* `libafb.pythonstrict(true)`: prevents Python from creating global variables.
* `libafb.config(handle, "key")`: returns binder/rqt/timer/... config
* `libafb.notice|warning|error|debug()`: print corresponding hookable syslog trace
