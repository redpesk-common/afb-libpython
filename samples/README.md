# Debuging Python with vscode from sources tree

In order to debug Python directly from VScode your should do:

* Set LD_LIBRARY_PATH for python to load libafb-glue.so
* Select Python3 (by default vscode generally select Python2)

## launch.json

Update vscode debug config with adequate LD_LIBRARY_PATH
```json
        {
            "env": {"LD_LIBRARY_PATH": "${workspaceFolder}/../afb-libglue/build/src:/usr/local/lib64"},
            "name":"Python: Current File",
            "type":"python",
            "request":"launch",
            "program":"${file}",
            "console":"integratedTerminal"
        },
```

## select Python3

For this click F1 to enter vscode command mode
type: 'Python:Select Interpretor' and chose Python 3

![vscode python3 debug](./python3-vscode-config.png)

## run debugger

* Open one Python from vscode
* Click on debug icon and select 'Python: Current File' configuration
* Place breakpoint within your source code
* Start debugging session

`Note: Breakpoint should be introduced before debug session start.`


