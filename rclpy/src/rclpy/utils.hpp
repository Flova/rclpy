// Copyright 2021 Open Source Robotics Foundation, Inc.
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

#ifndef RCLPY__UTILS_HPP_
#define RCLPY__UTILS_HPP_

#include <pybind11/pybind11.h>

#include <rcl/arguments.h>
#include <rcl/graph.h>  // rcl_names_and_types_t
#include <rmw/topic_endpoint_info.h>
#include <rmw/types.h>

#include <memory>

#include "publisher.hpp"

namespace py = pybind11;

namespace rclpy
{

typedef void destroy_ros_message_function (void *);

/// Convert a C rcl_names_and_types_t into a Python list.
/**
 * \param[in] topic_names_and_types The names and types struct to convert.
 * \return List of tuples, where the first element of each tuple is a string
 *   for the name and the second element is a list of strings for the types.
 */
py::list
convert_to_py_names_and_types(const rcl_names_and_types_t * topic_names_and_types);

/// Get the type support structure for a Python ROS message type.
/**
 * \param[in] pymessage The Python ROS message type.
 * \return The type support structure or NULL if an error occurred.
 */
void *
common_get_type_support(py::object pymessage);

/// Create the equivalent ROS message C type instance for a given Python type.
/**
* Raises AttributeError if \p pyclass is missing a required attribute.
*
* \param[in] pyclass ROS message Python type to extract typesupport data from.
* \return a ROS message C type instance.
*/
std::unique_ptr<void, destroy_ros_message_function *>
create_from_py(py::object pyclass);

/// Convert a ROS message from a Python type to a C type.
/**
 * Raises AttributeError if the Python message type is missing a required attribute.
 *
 * \param[in] pyclass ROS message Python type to extract data from.
 * \return unique pointer with the C version of the input ROS message.
 */
std::unique_ptr<void, destroy_ros_message_function *>
convert_from_py(py::object pyclass);

/// Convert a ROS message from a C type to a Python type.
/**
 * Raises AttributeError if \p pyclass is missing a required attribute.
 *
 * \param[in] message ROS message C type instance to be converted.
 * \param[in] pyclass ROS message Python type to convert to.
 * \return an instance of \p pyclass.
 */
py::object
convert_to_py(void * message, py::object pyclass);

/// Return the identifier of the current rmw_implementation
/**
 * \return string containing the identifier of the current rmw_implementation
 */
const char *
get_rmw_implementation_identifier();

/// Manually assert that an entity is alive.
/**
 * When using RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC, the application must
 * call this function at least as often as the qos policy
 * liveliness_lease_duration. The passed entity can be a Publisher.
 *
 * Raises RCLError on failure to assert liveliness
 * Raises TypeError if passed object is not a valid Publisher
 *
 * \param[in] publisher A capsule containing an rcl_publisher_t
 * \return None
 */
void
assert_liveliness(rclpy::Publisher * publisher);

/// Remove ROS specific args from a list of args.
/**
 * Raises RCLInvalidROSArgsError if args contains invalid ROS arguments
 * Raises RCLError on failure of parsing arguments or object cleanup
 * Raises ValueError if number of arguments exceeds limit
 * Raises UnparsedROSArgsError if ROS arguments are not fully parsed
 *
 * \param[in] pycli_args A list of strings
 * \return Parsed list of strings
 */
py::list
remove_ros_args(py::object pycli_args);

/// Throw UnparsedROSArgsError with a message saying which args are unparsed.
void
throw_if_unparsed_ros_args(py::list pyargs, const rcl_arguments_t & rcl_args);

/// Fetch a predefined qos_profile from rcl_action and convert it to a Python QoSProfile object.
/**
 * Raises RuntimeError if the QoS profile is unknown.
 *
 * This function takes a string defining a rmw_qos_profile_t and returns the
 * corresponding Python QoSProfile object.
 * \param[in] rmw_profile String with the name of the profile to load.
 * \return QoSProfile object.
 */
py::dict
rclpy_action_get_rmw_qos_profile(const char * rmw_profile);

/// Convert a C rmw_topic_endpoint_info_array_t into a Python list.
/**
 * Raises RuntimeError if the rmw_profile profile is null.
 *
 * \param[in] info_array a pointer to a rmw_topic_endpoint_info_array_t
 * \return Python list
 */
py::list
convert_to_py_topic_endpoint_info_list(const rmw_topic_endpoint_info_array_t * info_array);

/// Convert a C rmw_qos_profile_t into a Python dictionary with qos profile args.
/**
 * \param[in] qos_profile Pointer to a rmw_qos_profile_t to convert
 * \return Python dictionary
 */
py::dict
convert_to_qos_dict(const rmw_qos_profile_t * qos_profile);

/// Convert a C rosidl_type_hash_t into a Python dictionary.
/**
 * \param[in] type_hash Pointer to a rosidl_type_hash_t to convert
 * \return Python dictionary
 */
py::dict
convert_to_type_hash_dict(const rosidl_type_hash_t * type_hash);

/// Trampoline pattern for wrapping std::function into C-style callbacks.
/**
 * A common pattern in C is for a function to take a function pointer and a
 * void pointer for "user data" which is passed to the function pointer when it
 * is called from within C.
 *
 * It works by using the user data pointer to store a pointer to a
 * std::function instance.
 * So when called from C, this function will cast the user data to the right
 * std::function type and call it.
 *
 * This should allow you to use free functions, lambdas with and without
 * captures, and various kinds of std::bind instances.
 *
 * The interior of this function is likely to be executed within a C runtime,
 * so no exceptions should be thrown at this point, and doing so results in
 * undefined behavior.
 *
 * \tparam UserDataRealT Declared type of the passed function
 * \tparam UserDataT Deduced type based on what is passed for user data,
 *   usually this type is either `void *` or `const void *`.
 * \tparam Args the arguments being passed to the callback
 * \tparam ReturnT the return type of this function and the callback, default void
 * \param user_data the function pointer, possibly type erased
 * \param args the arguments to be forwarded to the callback
 * \returns whatever the callback returns, if anything
 */
template<
  typename UserDataRealT,
  typename UserDataT,
  typename ... Args,
  typename ReturnT = void
>
ReturnT
cpp_callback_trampoline(UserDataT user_data, Args ... args) noexcept
{
  auto & actual_callback = *static_cast<const UserDataRealT *>(user_data);
  return actual_callback(args ...);
}
}  // namespace rclpy

#endif  // RCLPY__UTILS_HPP_
