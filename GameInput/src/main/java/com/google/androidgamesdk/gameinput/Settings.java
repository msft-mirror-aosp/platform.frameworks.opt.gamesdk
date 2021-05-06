/*
 * Copyright (C) 2021 The Android Open Source Project
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
package com.google.androidgamesdk.gameinput;

import android.view.inputmethod.EditorInfo;

// Settings for InputConnection
public final class Settings {
  EditorInfo mEditorInfo;
  boolean mForwardKeyEvents;

  public Settings(EditorInfo editorInfo, boolean forwardKeyEvents) {
    mEditorInfo = editorInfo;
    mForwardKeyEvents = forwardKeyEvents;
  }
}
