/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __VIRTUAL_ENV_UTIL_H__
#define __VIRTUAL_ENV_UTIL_H__

#include <string>

namespace rocketmq {

class VirtualEnvUtil {
 public:
  static std::string buildWithProjectGroup(const std::string& origin, const std::string& projectGroup);
  static std::string clearProjectGroup(const std::string& origin, const std::string& projectGroup);

 public:
  static const char* VIRTUAL_APPGROUP_PREFIX;
};

}  // namespace rocketmq

#endif  // __VIRTUAL_ENV_UTIL_H__
