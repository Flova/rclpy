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

#include <pybind11/pybind11.h>

#include <rcl/error_handling.h>
#include <rcl/guard_condition.h>
#include <rcl/types.h>

#include <memory>

#include "context.hpp"
#include "exceptions.hpp"
#include "guard_condition.hpp"

namespace rclpy
{
GuardCondition::GuardCondition(Context & context)
: context_(context)
{
  rcl_guard_condition_ = std::shared_ptr<rcl_guard_condition_t>(
    new rcl_guard_condition_t,
    [](rcl_guard_condition_t * guard_condition)
    {
      rcl_ret_t ret = rcl_guard_condition_fini(guard_condition);
      if (RCL_RET_OK != ret) {
        // Warning should use line number of the current stack frame
        int stack_level = 1;
        PyErr_WarnFormat(
          PyExc_RuntimeWarning, stack_level, "Failed to fini guard condition: %s",
          rcl_get_error_string().str);
        rcl_reset_error();
      }
      delete guard_condition;
    });

  *rcl_guard_condition_ = rcl_get_zero_initialized_guard_condition();
  rcl_guard_condition_options_t gc_options = rcl_guard_condition_get_default_options();

  rcl_ret_t ret = rcl_guard_condition_init(
    rcl_guard_condition_.get(), context.rcl_ptr(), gc_options);
  if (RCL_RET_OK != ret) {
    throw RCLError("failed to create guard_condition");
  }
}

void
GuardCondition::destroy()
{
  rcl_guard_condition_.reset();
  context_.destroy();
}

void
GuardCondition::trigger_guard_condition()
{
  rcl_ret_t ret = rcl_trigger_guard_condition(rcl_guard_condition_.get());

  if (RCL_RET_OK != ret) {
    throw RCLError("failed to trigger guard condition");
  }

  {
    if (on_trigger_callback_) {
      on_trigger_callback_(1);
    } else {
      unread_count_++;
    }
  }
}

void
GuardCondition::set_on_trigger_callback(py::function callback)
{
  on_trigger_callback_ = [callback](size_t unread_count) {
    // Acquire GIL before calling Python code
    py::gil_scoped_acquire acquire;
    callback(unread_count);
  };

  if (unread_count_) {
    on_trigger_callback_(unread_count_);
    unread_count_ = 0;
  }
}

void GuardCondition::clear_on_trigger_callback()
{
  on_trigger_callback_ = nullptr;
}

void define_guard_condition(py::object module)
{
  py::class_<GuardCondition, Destroyable, std::shared_ptr<GuardCondition>>(module, "GuardCondition")
  .def(py::init<Context &>())
  .def_property_readonly(
    "pointer", [](const GuardCondition & guard_condition) {
      return reinterpret_cast<size_t>(guard_condition.rcl_ptr());
    },
    "Get the address of the entity as an integer")
  .def(
    "trigger_guard_condition", &GuardCondition::trigger_guard_condition,
    "Trigger a general purpose guard condition")
  .def(
    "set_on_trigger_callback", &GuardCondition::set_on_trigger_callback,
    "Set a callback to be called whenever the guard condition is triggered.")
  .def(
    "clear_on_trigger_callback", &GuardCondition::clear_on_trigger_callback,
    "Clear the callback registered for the guard condition");
}
}  // namespace rclpy
