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

#ifndef RCLPY__SERVICE_HPP_
#define RCLPY__SERVICE_HPP_

#include <pybind11/pybind11.h>

#include <rcl/service.h>
#include <rcl/service_introspection.h>
#include <rmw/types.h>

#include <memory>
#include <iostream>
#include <string>

#include "clock.hpp"
#include "destroyable.hpp"
#include "node.hpp"
#include "utils.hpp"

namespace py = pybind11;

namespace rclpy
{

class Service : public Destroyable, public std::enable_shared_from_this<Service>
{
public:
  /// Create a service server
  /**
   * This class will create a service server for the given service name.
   * This service will use the typesupport defined in the service module
   * provided as pysrv_type to send messages over the wire.
   *
   * Raises ValueError if the capsules are not the correct types
   * Raises RCLError if the service could not be created
   *
   * \param[in] node Node to add the service to
   * \param[in] pysrv_type Service module associated with the service
   * \param[in] service_name Python object for the service name
   * \param[in] pyqos_profile QoSProfile Python object for this service
   */
  Service(
    Node & node, py::object pysrv_type, const std::string & service_name,
    py::object pyqos_profile);

  Service(
    Node & node, std::shared_ptr<rcl_service_t> rcl_service);

  ~Service() = default;

  /// Publish a response message
  /**
   * Raises ValueError if the capsules are not the correct types
   * Raises RCLError if the response could not be sent
   *
   * \param[in] pyresponse reply message to send
   * \param[in] header Capsule pointing to the rmw_request_id_t header of the request we respond to
   */
  void
  service_send_response(py::object pyresponse, rmw_request_id_t * header);

  /// Take a request from a given service
  /**
   * Raises RCLError if the take failed
   *
   * \param[in] pyrequest_type Instance of the message type to take
   * \return [None, None] if there was nothing to take, or
   * \return List with 2 elements:
   *            first element: a Python request message with all fields populated with received request
   *            second element: a Capsule pointing to the header (rmw_request_id) of the processed request
   */
  py::tuple
  service_take_request(py::object pyrequest_type);

  /// Get rcl_service_t pointer
  rcl_service_t *
  rcl_ptr() const
  {
    return rcl_service_.get();
  }

  /// Get the service name.
  const char *
  get_service_name();

  /// Get the QoS profile for this service.
  py::dict
  get_qos_profile();

  /// Configure introspection.
  /**
   * \param[in] clock clock to use for service event timestamps
   * \param[in] pyqos_service_event_pub QoSProfile python object for the service event publisher
   * \param[in] introspection_state which state to set introspection to
   */
  void
  configure_introspection(
    Clock & clock, py::object pyqos_service_event_pub,
    rcl_service_introspection_state_t introspection_state);

  /// Force an early destruction of this object
  void
  destroy() override;

  /// Set the callback for when a new request is received
  /**
   * Raises RCLError if the callback could not be set
   *
   * \param[in] callback Python function to call when a new request is received
   */
  void
  set_on_new_request_callback(py::function callback);

  /// Unset the callback registered for new events, if any.
  void
  clear_on_new_request_callback();

private:
  Node node_;
  std::function<void(size_t)> on_new_request_callback_{nullptr};
  std::shared_ptr<rcl_service_t> rcl_service_;
  rosidl_service_type_support_t * srv_type_;
};

/// Define a pybind11 wrapper for an rclpy::Service
void
define_service(py::object module);
}  // namespace rclpy

#endif  // RCLPY__SERVICE_HPP_
