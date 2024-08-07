"""

  A3 Core simple gui to start/stop A3 services
  Copyright (C) 2023 Patric Schmitz, Raphael Eismann

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""

import tk
import PySimpleGUI as sg
import os

if os.environ.get('DISPLAY','') == '':
    print('no display found. Using :0')
    os.environ.__setitem__('DISPLAY', ':0')

layout = [
        [sg.Text("A³ Core Interface")],
        [sg.Button("start reaper"), sg.Button("stop reaper")],
        [sg.Button("start supercollider"), sg.Button("stop supercollider")],
        [sg.Button("start mixbus"), sg.Button("stop mixbus")],
        #[sg.Text("cables")],
        #[sg.Button("connect a3 patch")],
        #[sg.Button("disconnect")],
        #[sg.Button("connect userpatch")],
        #[sg.Button("write userpatch")],
        [sg.Text("media")],
        [sg.Button("files"), sg.Button("music"), sg.Button("show")],
        [sg.Text("decoder")],
        [sg.Button("editor")]
        ]

# Create the window
window = sg.Window("A3 Core Interface", layout)

# Create an event loop
while True:
    event, values = window.read()
    if event == "start reaper": 
        os.system("systemctl --user start a3_reaper")
    if event == "stop reaper": 
        os.system("systemctl --user stop a3_reaper")

    if event == "start supercollider": 
        os.system("systemctl --user start a3_vu_meter")
    if event == "stop supercollider": 
        os.system("systemctl --user stop a3_vu_meter")

    if event == "start mixbus": 
        os.system("systemctl --user start a3_mixbus")
    if event == "stop mixbus": 
        os.system("systemctl --user stop a3_mixbus")

#    if event == "connect a3 patch": 
#        os.system("/usr/bin/jmess -Dc /home/aaa/a3-system/a3core/jack_connections/a3_connect.jmess")
#    if event == "disconnect": 
#        os.system("/usr/bin/jmess -D")
#    
#    if event == "connect userpatch": 
#        os.system("/usr/bin/jmess -Dc /home/aaa/userpatches/userpatch.jmess")
#    if event == "write userpatch": 
#        os.system("mv /home/aaa/userpatches/userpatch.jmess /home/aaa/userpatches/userpatch_bck.jmess && /usr/bin/jmess -s /home/aaa/userpatches/userpatch.jmess")
    
    if event == "files": 
        os.system("/usr/bin/thunar &")
    if event == "music": 
        os.system("/usr/bin/vlc &")
    if event == "show": 
        os.system("/usr/bin/linux-show-player -f /home/aaa/linux-show-player/a3-show.lsp &")
    if event == "editor": 
        os.system("/usr/bin/iem-plugin-allradecoder &")

    if event == sg.WIN_CLOSED:
        break

window.close()

