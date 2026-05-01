// Copyright 2026 Shravan Somashekara Rai
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LIVEKIT_ROS2_CLIENT__VISIBILITY_CONTROL_HPP_
#define LIVEKIT_ROS2_CLIENT__VISIBILITY_CONTROL_HPP_

// Provides symbol visibility macros for GCC / Clang / MSVC.
// All public API symbols in this package must be annotated with
// LIVEKIT_ROS2_CLIENT_PUBLIC.  Internal (file-local) symbols should use
// LIVEKIT_ROS2_CLIENT_LOCAL.

#if defined _WIN32 || defined __CYGWIN__
  #ifdef __GNUC__
    #define LIVEKIT_ROS2_CLIENT_EXPORT __attribute__((dllexport))
    #define LIVEKIT_ROS2_CLIENT_IMPORT __attribute__((dllimport))
  #else
    #define LIVEKIT_ROS2_CLIENT_EXPORT __declspec(dllexport)
    #define LIVEKIT_ROS2_CLIENT_IMPORT __declspec(dllimport)
  #endif
  #ifdef LIVEKIT_ROS2_CLIENT_BUILDING_DLL
    #define LIVEKIT_ROS2_CLIENT_PUBLIC LIVEKIT_ROS2_CLIENT_EXPORT
  #else
    #define LIVEKIT_ROS2_CLIENT_PUBLIC LIVEKIT_ROS2_CLIENT_IMPORT
  #endif
  #define LIVEKIT_ROS2_CLIENT_PUBLIC_TYPE LIVEKIT_ROS2_CLIENT_PUBLIC
  #define LIVEKIT_ROS2_CLIENT_LOCAL
#else
  #define LIVEKIT_ROS2_CLIENT_EXPORT __attribute__((visibility("default")))
  #define LIVEKIT_ROS2_CLIENT_IMPORT
  #if __GNUC__ >= 4
    #define LIVEKIT_ROS2_CLIENT_PUBLIC __attribute__((visibility("default")))
    #define LIVEKIT_ROS2_CLIENT_LOCAL  __attribute__((visibility("hidden")))
  #else
    #define LIVEKIT_ROS2_CLIENT_PUBLIC
    #define LIVEKIT_ROS2_CLIENT_LOCAL
  #endif
  #define LIVEKIT_ROS2_CLIENT_PUBLIC_TYPE
#endif

#endif  // LIVEKIT_ROS2_CLIENT__VISIBILITY_CONTROL_HPP_
