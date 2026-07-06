from contextlib import contextmanager

import libafb
import os
import sys


@contextmanager
def assert_raises(exception_class):
    raised = False
    try:
        yield
    except exception_class:
        raised = True
    assert raised

@contextmanager
def silence_stderr():
    "Close stderr, then reopen it after exiting the context"
    stderr_fd = sys.stderr.fileno()
    stderr_copy = os.dup(stderr_fd)
    os.close(stderr_fd)

    try:
        yield
    finally:
        os.dup2(stderr_copy, stderr_fd)
        os.close(stderr_copy)

from contextlib import redirect_stderr, redirect_stdout

def test_event_handler():
    def verb_cb(handle, *args):
        assert handle
        assert len(args)
        match args[0]:
            case "ping":
                return 0, *args[1:]
            case "subscribe":
                r = libafb.evtsubscribe(handle, my_event)
                assert r is None
                return 0
            case "emit":
                evt_args = args[1:]
                r = libafb.evtpush(my_event, *evt_args)
                assert r is None
                return 0
            case _:
                assert False

        return 1

    my_api = {
        "uid": "py-binding",
        "api": "py-binding",
        "class": "test",
        "info": "py api test",
        "verbose": 9,
        "export": "public",
        "verbs": [
            {"uid": "py-verb", "verb": "verb", "callback": verb_cb},
        ],
    }
    api_handler = libafb.apiadd(my_api)
    assert api_handler

    my_event = libafb.evtnew(api_handler, "my_event")

    ret = libafb.callsync(_binder, "py-binding", "verb", "ping", None, [42], 43, "toto", 3.14)
    assert (ret.status, ret.args) == (0, (None, [42], 43, "toto", 3.14))

    ret = libafb.callsync(_binder, "py-binding", "verb", "subscribe")
    assert (ret.status, ret.args) == (0, ())

    evt_data = [43]

    def on_evt(handle, event_name, user_data, *wtf):
        assert handle
        assert event_name == "py-binding/my_event"
        assert user_data == evt_data

    r = libafb.evthandler(
        _binder,
        {"api": "py-binding", "pattern": "py-binding/*", "callback": on_evt},
        evt_data,
    )
    assert r is None

    for i in range(3):
        evt_data[0] = i
        r = libafb.callsync(_binder, "py-binding", "verb", "emit", i, i)
        assert (r.status, r.args) == (0, ())

        # event handler already exists
        with silence_stderr(), assert_raises(RuntimeError):
            r = libafb.evthandler(
                _binder,
                {"api": "py-binding", "pattern": "py-binding/*", "callback": on_evt},
                43,
            )

    with silence_stderr(), assert_raises(RuntimeError):
        # event handler not found
        r = libafb.evtdelete(_binder, "toto")

    r = libafb.evtdelete(_binder, "py-binding/*")
    assert r is None

    r = libafb.evthandler(
        _binder,
        {"api": "py-binding", "pattern": "py-binding/*", "callback": on_evt},
        evt_data,
    )
    assert r is None

    for i in range(3):
        evt_data[0] = i + 42
        r = libafb.callsync(_binder, "py-binding", "verb", "emit", i)
        assert (r.status, r.args) == (0, ())

    r = libafb.evtdelete(_binder, "py-binding/*")
    assert r is None

def test_api():
    def my_control(
        handle, state: str
    ):
        print(state)
        return 0

    my_api = {
        "uid": "uid",
        "api": "py-binding2",
        "control": my_control
    }
    r = libafb.apiadd(my_api)


_binder = libafb.binder(
    {
        "uid": "py-binder",
        "verbose": 255,
        "rootdir": ".",
        "set": {},
        "port": 0,
    }
)


def _loop_cb(handle, userdata):
    assert handle
    assert userdata == 42

    test_event_handler()
    #test_api()

    return 1


libafb.loopstart(_binder, _loop_cb, 42)

print()
print("** Tests executed successfully **")
