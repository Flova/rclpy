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

#ifndef RCLPY__SUBSCRIPTION_HPP_
#define RCLPY__SUBSCRIPTION_HPP_

#include <pybind11/pybind11.h>

#include <rcl/subscription.h>

#include <memory>
#include <mutex>
#include <string>
#include <iostream>

#include "destroyable.hpp"
#include "node.hpp"
#include "utils.hpp"

namespace py = pybind11;

namespace rclpy
{
/// Create a subscription
/**
 * This class will create a subscription for the given topic name.
 * This subscription will use the typesupport defined in the message module
 * provided as pymsg_type to send messages.
 */
class Subscription : public Destroyable, public std::enable_shared_from_this<Subscription>
{
public:
  /// Create a subscription
  /**
   * Raises RCLError if the subscription could not be created
   *
   * \param[in] node Node to add the subscriber to
   * \param[in] pymsg_type Message module associated with the subscriber
   * \param[in] topic The topic name
   * \param[in] pyqos_profile rmw_qos_profile_t object for this subscription
   */
  Subscription(
    Node & node, py::object pymsg_type, std::string topic,
    py::object pyqos_profile);

  /// Take a message and its metadata from a subscription
  /**
   * Raises MemoryError if there was an error allocating memory
   * Raises RCLError if there was an error within rcl
   *
   * \param[in] pymsg_type Message type to be taken (i.e. a class).
   * \param[in] raw If True, return the message without de-serializing it.
   * \return Tuple of (message, metadata) or None if there was no message to take.
   *   Message is a \p pymsg_type instance if \p raw is True, otherwise a byte string.
   *   Metadata is a plain dictionary.
   */
  py::object
  take_message(py::object pymsg_type, bool raw);

  /// Get the name of the logger associated with the node of the subscription.
  /**
   *
   * \return logger_name, or
   * \return None on failure
   */
  const char *
  get_logger_name() const;

  /// Return the resolved topic name of a subscription.
  /**
   * The returned string is the resolved topic name after remappings have be applied.
   *
   * Raises RCLError if the topic name could not be determined
   *
   * \return a string with the topic name
   */
  std::string
  get_topic_name() const;

  /// Count publishers from a subscriber.
  /**
   * Raises RCLError if the publisher count cannot be determined
   *
   * \return number of publishers
   */
  size_t
  get_publisher_count() const;

  /// Get rcl_subscription_t pointer
  rcl_subscription_t *
  rcl_ptr() const
  {
    return rcl_subscription_.get();
  }

  // Register a callback that is triggered when a new message is received in the middleware
  void
  set_on_new_message_callback(py::function callback);

  /// Clear the callback registered for new messages
  void clear_on_new_message_callback();

  /// Force an early destruction of this object
  void
  destroy() override;

private:
  Node node_;
  // It is important to declare on_new_message_callback_ before
  // subscription_handle_, so on destruction the subscription is
  // destroyed first. Otherwise, the rmw subscription callback
  // would point briefly to a destroyed function.
  std::function<void(size_t)> on_new_message_callback_{nullptr};
  std::shared_ptr<rcl_subscription_t> rcl_subscription_;
};
/// Define a pybind11 wrapper for an rclpy::Service
void define_subscription(py::object module);
}  // namespace rclpy

#endif  // RCLPY__SUBSCRIPTION_HPP_
