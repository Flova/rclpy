// Copyright 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <pybind11/pybind11.h>

#include <rcl/error_handling.h>
#include <rcl/event.h>
#include <rmw/incompatible_qos_events_statuses.h>

#include <memory>
#include <stdexcept>
#include <utility>

#include "event_handle.hpp"
#include "exceptions.hpp"

namespace rclpy
{
static
std::shared_ptr<rcl_event_t>
create_zero_initialized_event()
{
  auto event = std::shared_ptr<rcl_event_t>(
    new rcl_event_t,
    [](rcl_event_t * event)
    {
      rcl_ret_t ret = rcl_event_fini(event);
      if (RCL_RET_OK != ret) {
        int stack_level = 1;
        PyErr_WarnFormat(
          PyExc_RuntimeWarning, stack_level,
          "failed to fini event: %s",
          rcl_get_error_string().str);
        rcl_reset_error();
      }
      delete event;
    });

  *event = rcl_get_zero_initialized_event();
  return event;
}

void
EventHandle::destroy()
{
  rcl_event_.reset();
  std::visit([](auto & t) {t.destroy();}, grandparent_);
}

EventHandle::EventHandle(
  rclpy::Subscription & subscription, rcl_subscription_event_type_t event_type)
: event_type_(event_type), grandparent_(subscription)
{
  // Create a subscription event
  rcl_event_ = create_zero_initialized_event();

  rcl_ret_t ret = rcl_subscription_event_init(
    rcl_event_.get(),
    std::get<Subscription>(grandparent_).rcl_ptr(), event_type);
  if (RCL_RET_BAD_ALLOC == ret) {
    rcl_reset_error();
    throw std::bad_alloc();
  }
  if (RCL_RET_UNSUPPORTED == ret) {
    throw UnsupportedEventTypeError("subscription event is unsupported");
  }
  if (RCL_RET_OK != ret) {
    throw RCLError("failed to create subscription event");
  }
}

EventHandle::EventHandle(
  rclpy::Publisher & publisher, rcl_publisher_event_type_t event_type)
: event_type_(event_type), grandparent_(publisher)
{
  // Create a publisher event
  rcl_event_ = create_zero_initialized_event();

  rcl_ret_t ret = rcl_publisher_event_init(
    rcl_event_.get(),
    std::get<Publisher>(grandparent_).rcl_ptr(), event_type);
  if (RCL_RET_BAD_ALLOC == ret) {
    rcl_reset_error();
    throw std::bad_alloc();
  }
  if (RCL_RET_UNSUPPORTED == ret) {
    throw UnsupportedEventTypeError("publisher event is unsupported");
  }
  if (RCL_RET_OK != ret) {
    throw RCLError("failed to create publisher event");
  }
}

typedef union event_callback_data {
  // Subscription events
  rmw_requested_deadline_missed_status_t requested_deadline_missed;
  rmw_liveliness_changed_status_t liveliness_changed;
  rmw_message_lost_status_t message_lost;
  rmw_requested_qos_incompatible_event_status_t requested_incompatible_qos;
  rmw_matched_status_t subscription_matched;
  // Publisher events
  rmw_offered_deadline_missed_status_t offered_deadline_missed;
  rmw_liveliness_lost_status_t liveliness_lost;
  rmw_offered_qos_incompatible_event_status_t offered_incompatible_qos;
  rmw_matched_status_t publisher_matched;

  rmw_incompatible_type_status_t incompatible_type;
} event_callback_data_t;

py::object
EventHandle::take_event()
{
  event_callback_data_t data;
  rcl_ret_t ret = rcl_take_event(rcl_event_.get(), &data);
  if (RCL_RET_BAD_ALLOC == ret) {
    rcl_reset_error();
    throw std::bad_alloc();
  }
  if (RCL_RET_EVENT_TAKE_FAILED == ret) {
    return py::none();
  }
  if (RCL_RET_OK != ret) {
    throw RCLError("failed to take event");
  }

  if (auto sub_type = std::get_if<rcl_subscription_event_type_t>(&event_type_)) {
    switch (*sub_type) {
      case RCL_SUBSCRIPTION_REQUESTED_DEADLINE_MISSED:
        return py::cast(data.requested_deadline_missed);
      case RCL_SUBSCRIPTION_LIVELINESS_CHANGED:
        return py::cast(data.liveliness_changed);
      case RCL_SUBSCRIPTION_MESSAGE_LOST:
        return py::cast(data.message_lost);
      case RCL_SUBSCRIPTION_REQUESTED_INCOMPATIBLE_QOS:
        return py::cast(data.requested_incompatible_qos);
      case RCL_SUBSCRIPTION_INCOMPATIBLE_TYPE:
        return py::cast(data.incompatible_type);
      case RCL_SUBSCRIPTION_MATCHED:
        return py::cast(data.subscription_matched);
      default:
        // suggests a misalignment between C and Python interfaces
        throw py::value_error("event type for subscriptions not understood");
    }
  } else if (auto pub_type = std::get_if<rcl_publisher_event_type_t>(&event_type_)) {
    switch (*pub_type) {
      case RCL_PUBLISHER_OFFERED_DEADLINE_MISSED:
        return py::cast(data.offered_deadline_missed);
      case RCL_PUBLISHER_LIVELINESS_LOST:
        return py::cast(data.liveliness_lost);
      case RCL_PUBLISHER_OFFERED_INCOMPATIBLE_QOS:
        return py::cast(data.offered_incompatible_qos);
      case RCL_PUBLISHER_INCOMPATIBLE_TYPE:
        return py::cast(data.incompatible_type);
      case RCL_PUBLISHER_MATCHED:
        return py::cast(data.publisher_matched);
      default:
        // suggests a misalignment between C and Python interfaces
        throw py::value_error("event type for publishers not understood");
    }
  }
  throw std::runtime_error("cannot take event that is neither a publisher or a subscription event");
}

void EventHandle::set_on_new_event_callback(py::function callback)
{
  on_new_event_callback_ = [callback, this](size_t number_of_events) noexcept {
      try {
        // Acquire GIL before calling Python code
        py::gil_scoped_acquire acquire;
        callback(number_of_events);
      } catch (const std::exception & exception) {
        // TODO proper logging here for now just print to stderr
        std::cerr << "caught exception in user-provided callback for the 'on_event_callback' callback: " <<
          exception.what() << std::endl;
      } catch (...) {
        // TODO proper logging here for now just print to stderr
        std::cerr << "caught unhandled exception in user-provided callback for the 'on_event_callback' callback" << std::endl;
      }
    };

  rcl_ret_t ret = rcl_event_set_callback(
    rcl_event_.get(),
    rclpy::cpp_callback_trampoline<decltype(on_new_event_callback_), const void *, size_t>,
    static_cast<const void *>(&on_new_event_callback_));

  if (RCL_RET_OK != ret) {
    throw RCLError("failed to set the on new event callback for event");
  }
}

void EventHandle::clear_on_new_event_callback()
{
  on_new_event_callback_ = nullptr;
  rcl_ret_t ret = rcl_event_set_callback(rcl_event_.get(), nullptr, nullptr);

  if (RCL_RET_OK != ret) {
    throw RCLError("failed to clear the on new event callback for event");
  }
}

void
define_event_handle(py::module module)
{
  py::class_<EventHandle, Destroyable, std::shared_ptr<EventHandle>>(module, "EventHandle")
  .def(py::init<rclpy::Subscription &, rcl_subscription_event_type_t>())
  .def(py::init<rclpy::Publisher &, rcl_publisher_event_type_t>())
  .def_property_readonly(
    "pointer", [](const EventHandle & event) {
      return reinterpret_cast<size_t>(event.rcl_ptr());
    },
    "Get the address of the entity as an integer")
  .def(
    "take_event", &EventHandle::take_event,
    "Get pending data from a ready event")
  .def(
    "set_on_new_event_callback", &EventHandle::set_on_new_event_callback, py::call_guard<py::gil_scoped_release>(),
    "Register a callback that is triggered when a new event occurs")
  .def(
    "clear_on_new_event_callback", &EventHandle::clear_on_new_event_callback, py::call_guard<py::gil_scoped_release>(),
    "Clear the callback registered for new events");

  py::enum_<rcl_subscription_event_type_t>(module, "rcl_subscription_event_type_t")
  .value("RCL_SUBSCRIPTION_REQUESTED_DEADLINE_MISSED", RCL_SUBSCRIPTION_REQUESTED_DEADLINE_MISSED)
  .value("RCL_SUBSCRIPTION_LIVELINESS_CHANGED", RCL_SUBSCRIPTION_LIVELINESS_CHANGED)
  .value("RCL_SUBSCRIPTION_REQUESTED_INCOMPATIBLE_QOS", RCL_SUBSCRIPTION_REQUESTED_INCOMPATIBLE_QOS)
  .value("RCL_SUBSCRIPTION_MESSAGE_LOST", RCL_SUBSCRIPTION_MESSAGE_LOST)
  .value("RCL_SUBSCRIPTION_INCOMPATIBLE_TYPE", RCL_SUBSCRIPTION_INCOMPATIBLE_TYPE)
  .value("RCL_SUBSCRIPTION_MATCHED", RCL_SUBSCRIPTION_MATCHED);

  py::enum_<rcl_publisher_event_type_t>(module, "rcl_publisher_event_type_t")
  .value("RCL_PUBLISHER_OFFERED_DEADLINE_MISSED", RCL_PUBLISHER_OFFERED_DEADLINE_MISSED)
  .value("RCL_PUBLISHER_LIVELINESS_LOST", RCL_PUBLISHER_LIVELINESS_LOST)
  .value("RCL_PUBLISHER_OFFERED_INCOMPATIBLE_QOS", RCL_PUBLISHER_OFFERED_INCOMPATIBLE_QOS)
  .value("RCL_PUBLISHER_INCOMPATIBLE_TYPE", RCL_PUBLISHER_INCOMPATIBLE_TYPE)
  .value("RCL_PUBLISHER_MATCHED", RCL_PUBLISHER_MATCHED);

  py::class_<rmw_requested_deadline_missed_status_t>(
    module, "rmw_requested_deadline_missed_status_t")
  .def(py::init<>())
  .def_readonly("total_count", &rmw_requested_deadline_missed_status_t::total_count)
  .def_readonly("total_count_change", &rmw_requested_deadline_missed_status_t::total_count_change);

  py::class_<rmw_liveliness_changed_status_t>(module, "rmw_liveliness_changed_status_t")
  .def(py::init<>())
  .def_readonly("alive_count", &rmw_liveliness_changed_status_t::alive_count)
  .def_readonly("not_alive_count", &rmw_liveliness_changed_status_t::not_alive_count)
  .def_readonly("alive_count_change", &rmw_liveliness_changed_status_t::alive_count_change)
  .def_readonly("not_alive_count_change", &rmw_liveliness_changed_status_t::not_alive_count_change);

  py::class_<rmw_message_lost_status_t>(module, "rmw_message_lost_status_t")
  .def(py::init<>())
  .def_readonly("total_count", &rmw_message_lost_status_t::total_count)
  .def_readonly("total_count_change", &rmw_message_lost_status_t::total_count_change);

  py::class_<rmw_requested_qos_incompatible_event_status_t>(
    module, "rmw_requested_qos_incompatible_event_status_t")
  .def(py::init<>())
  .def_readonly("total_count", &rmw_requested_qos_incompatible_event_status_t::total_count)
  .def_readonly(
    "total_count_change", &rmw_requested_qos_incompatible_event_status_t::total_count_change)
  .def_readonly(
    "last_policy_kind", &rmw_requested_qos_incompatible_event_status_t::last_policy_kind);

  py::class_<rmw_offered_deadline_missed_status_t>(module, "rmw_offered_deadline_missed_status_t")
  .def(py::init<>())
  .def_readonly("total_count", &rmw_offered_deadline_missed_status_t::total_count)
  .def_readonly("total_count_change", &rmw_offered_deadline_missed_status_t::total_count_change);

  py::class_<rmw_liveliness_lost_status_t>(module, "rmw_liveliness_lost_status_t")
  .def(py::init<>())
  .def_readonly("total_count", &rmw_liveliness_lost_status_t::total_count)
  .def_readonly("total_count_change", &rmw_liveliness_lost_status_t::total_count_change);

  py::class_<rmw_matched_status_t>(module, "rmw_matched_status_t")
  .def(py::init<>())
  .def_readonly("total_count", &rmw_matched_status_t::total_count)
  .def_readonly("total_count_change", &rmw_matched_status_t::total_count_change)
  .def_readonly("current_count", &rmw_matched_status_t::current_count)
  .def_readonly("current_count_change", &rmw_matched_status_t::current_count_change);

  py::enum_<rmw_qos_policy_kind_t>(module, "rmw_qos_policy_kind_t")
  .value("RMW_QOS_POLICY_INVALID", RMW_QOS_POLICY_INVALID)
  .value("RMW_QOS_POLICY_DURABILITY", RMW_QOS_POLICY_DURABILITY)
  .value("RMW_QOS_POLICY_DEADLINE", RMW_QOS_POLICY_DEADLINE)
  .value("RMW_QOS_POLICY_LIVELINESS", RMW_QOS_POLICY_LIVELINESS)
  .value("RMW_QOS_POLICY_RELIABILITY", RMW_QOS_POLICY_RELIABILITY)
  .value("RMW_QOS_POLICY_HISTORY", RMW_QOS_POLICY_HISTORY)
  .value("RMW_QOS_POLICY_LIFESPAN", RMW_QOS_POLICY_LIFESPAN)
  .value("RMW_QOS_POLICY_DEPTH", RMW_QOS_POLICY_DEPTH)
  .value("RMW_QOS_POLICY_LIVELINESS_LEASE_DURATION", RMW_QOS_POLICY_LIVELINESS_LEASE_DURATION)
  .value(
    "RMW_QOS_POLICY_AVOID_ROS_NAMESPACE_CONVENTIONS",
    RMW_QOS_POLICY_AVOID_ROS_NAMESPACE_CONVENTIONS);

  py::class_<rmw_incompatible_type_status_t>(module, "rmw_incompatible_type_status_t")
  .def(py::init<>())
  .def_readonly("total_count_change", &rmw_incompatible_type_status_t::total_count_change);
}
}  // namespace rclpy
