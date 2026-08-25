#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

failthresh = 0.01
failpercent = 1

outputs = [ "out.exr", "out_layer_weight.exr" ]
command = testrender("-v -r 98 98 -aa 32 scene.xml out.exr")
command += testrender(
    "-r 160 80 -aa 32 scene_layer_weight.xml out_layer_weight.exr")
