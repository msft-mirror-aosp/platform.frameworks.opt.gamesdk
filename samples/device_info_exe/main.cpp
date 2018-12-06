/*
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

#include "device_info/device_info.h"

int main(){
  std::cout << "*Proto debug begin:" << std::endl;
  androidgamesdk_deviceinfo::root proto;
  androidgamesdk_deviceinfo::createProto(proto);
  std::cout << proto.cpu_present() << std::endl;
  std::cout << "*Proto debug end." << std::endl;
  std::cout << "fin." << std::endl;
  return 0;
}
