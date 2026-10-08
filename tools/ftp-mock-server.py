#!/usr/bin/env python3
# ProsperoEden - A small stand-in for the console's FTP server, for tools/check-remote.py.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""ftp-mock-server.py <port file>

Takes the files of downloads (headless/remote/ftp.cpp) on 127.0.0.1 and a free port it writes to
<port file>: USER, PASS, TYPE, PASV, REST, STOR and QUIT, with paths as they are on the machine.
STOR writes as ps5-payload-dev's ftpsrv does: the file is opened without emptying it, written from
the REST offset on, and cut where the data ended when the data connection closes. With a file
named ftp-full in the folder it writes to, the drive is full after 1000 bytes: as ftpsrv, it says
"550 No space left on device", closes the data connection and leaves what it wrote.
"""

import os
import socket
import sys
import threading


def serve(control):
    reader = control.makefile("rb")

    def say(line):
        control.sendall((line + "\r\n").encode())

    say("220 mock FTP server")
    offset = 0
    passive = None
    try:
        for raw in reader:
            line = raw.decode(errors="replace").rstrip("\r\n")
            command, _, argument = line.partition(" ")
            command = command.upper()
            if command == "USER":
                say("331 Password, please")
            elif command == "PASS":
                say("230 Signed in")
            elif command == "TYPE":
                say("200 Type set")
            elif command == "PASV":
                if passive:
                    passive.close()
                passive = socket.socket()
                passive.bind(("127.0.0.1", 0))
                passive.listen(1)
                port = passive.getsockname()[1]
                say(f"227 Entering Passive Mode (127,0,0,1,{port // 256},{port % 256})")
            elif command == "REST":
                offset = int(argument)
                say("350 REST OK")
            elif command == "STOR":
                if not passive:
                    say("425 Use PASV first")
                    continue
                say("150 Opening data transfer")
                data, _ = passive.accept()
                passive.close()
                passive = None
                descriptor = os.open(argument, os.O_CREAT | os.O_WRONLY, 0o666)
                os.lseek(descriptor, offset, os.SEEK_SET)
                at = offset
                offset = 0
                room = 1000 if os.path.exists(os.path.join(os.path.dirname(argument), "ftp-full")) else None
                full = False
                while chunk := data.recv(1 << 20):
                    if room is not None and len(chunk) > room:
                        os.write(descriptor, chunk[:room])
                        full = True
                        break
                    os.write(descriptor, chunk)
                    at += len(chunk)
                    if room is not None:
                        room -= len(chunk)
                if full:
                    say("550 No space left on device")
                    data.close()
                    os.close(descriptor)
                    continue
                os.ftruncate(descriptor, at)
                os.close(descriptor)
                data.close()
                say("226 Data transfer complete")
            elif command == "QUIT":
                say("221 Bye")
                break
            else:
                say("502 Not here")
    except (OSError, ValueError):
        pass
    finally:
        if passive:
            passive.close()
        control.close()


def main():
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(16)
    with open(sys.argv[1], "w") as port_file:
        port_file.write(str(server.getsockname()[1]))
    while True:
        control, _ = server.accept()
        threading.Thread(target=serve, args=(control,), daemon=True).start()


if __name__ == "__main__":
    main()
