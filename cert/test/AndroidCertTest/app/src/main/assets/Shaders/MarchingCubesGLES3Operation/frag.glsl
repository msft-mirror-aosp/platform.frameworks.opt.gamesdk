/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#version 300 es

in vec3 vColor;
in vec3 vNormal;
in vec3 vWorldNormal;

out vec4 fragColor;

/////////////////////////////////////////////

void main() {
    vec3 color = vColor * ((vNormal + vec3(1.0)) * 0.5);
    fragColor = vec4(color, 1);
}