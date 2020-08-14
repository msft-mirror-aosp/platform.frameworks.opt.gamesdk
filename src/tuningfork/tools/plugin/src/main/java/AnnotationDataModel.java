/*
 * Copyright (C) 2020 The Android Open Source Project
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
 * limitations under the License
 */

import com.google.protobuf.Descriptors.FieldDescriptor;
import java.util.HashMap;

public class AnnotationDataModel {

  private HashMap<EnumDataModel, FieldDescriptor> fields;

  AnnotationDataModel() {
    fields = new HashMap<>();
  }

  public boolean addEnum(EnumDataModel enumDataModel, FieldDescriptor setValue) {
    if (fields.containsKey(enumDataModel)) {
      return false;
    }
    fields.put(enumDataModel, setValue);
    return true;
  }

  public FieldDescriptor getField(EnumDataModel enumDataModel) {
    return fields.get(enumDataModel);
  }
}