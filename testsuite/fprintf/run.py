#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

import os

if os.path.isfile("data.txt") :
    os.remove ("data.txt")

command = testshade("test")

outputs = [ "data.txt" ]
