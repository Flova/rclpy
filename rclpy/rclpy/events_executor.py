# An alternative events queue based executor for rclpy.
from typing import Optional, TypeAlias
import rclpy
import queue
from rclpy.executors import Executor
from rclpy.node import Node
from rclpy.waitable import Waitable
from rclpy.utilities import get_default_context
from rclpy.signals import SignalHandlerGuardCondition
from rclpy.subscription import Subscription
from rclpy.callback_groups import CallbackGroup
from rclpy.guard_condition import GuardCondition
from rclpy.timer import Timer
from rclpy.client import Client
from rclpy.service import Service
from dataclasses import dataclass

from rclpy.exceptions import InvalidHandle
from weakref import ReferenceType, ref

T_ExecutableEntities: TypeAlias = GuardCondition | Subscription | Waitable | Timer | Client | Service
# TODO add Timer, Service, Client, etc.

@dataclass
class Event:
    entity: T_ExecutableEntities
    count: int

class EventsExecutor(Executor):
    def __init__(self, *, context=None):
        self._context = get_default_context() if context is None else context
        self._events: queue.Queue[Event] = queue.Queue()

        # This guard condition is used to wake up the executor
        # This might be necessary if the executor is shutdown and we want stop sleeping
        self._guard = GuardCondition(callback=None, callback_group=None, context=context)
        self._add_to_executor(self._guard)

        self._shutdown_requested = False
        self.context.on_shutdown(self.shutdown)

    def wake(self) -> None:
        """
        Wake the executor because something changed.

        This is used to tell the executor when entities are created or destroyed.
        """
        print('wake')
        if self._guard:
            self._guard.trigger()

    def __del__(self):
        print('EventsExecutor __del__')
        if self._sigint_gc is not None:
            self._sigint_gc.destroy()

    def shutdown(self, timeout_sec = None) -> bool:  # TODO handle timeout
        print('EventsExecutor shutdown')
        self._shutdown_requested = True
        self._guard.trigger()
        return True

    def add_node(self, node: Node):
        # Add the executable entities of the node to the executor
        for timer in node.timers:
            self._add_to_executor(timer)
        for sub in node.subscriptions:
            self._add_to_executor(sub)
        for gc in node.guards:
            self._add_to_executor(gc)
        for client in node.clients:
            self._add_to_executor(client)
        for service in node.services:
            self._add_to_executor(service)
        for waitable in node.waitables:
            self._add_to_executor(waitable)

    def _add_to_executor(self, entity: T_ExecutableEntities):
        def entity_trigger_callback(count: int):
            self._events.put(Event(
                entity=entity,
                count=count
            ))

        # Register callback for the entity
        match entity:
            case Timer():
                print('setting timer callback')
                raise NotImplementedError('Timer is not supported in EventsExecutor yet')
            case Subscription():
                with entity.handle:
                    entity.handle.set_on_new_message_callback(entity_trigger_callback)
            case GuardCondition():
                print('setting guard condition callback')
                with entity.handle:
                    entity.handle.set_on_trigger_callback(entity_trigger_callback)
            case Client():
                with entity.handle:
                    entity.handle.set_on_new_response_callback(entity_trigger_callback)
            case Service():
                with entity.handle:
                    entity.handle.on_new_request_callback(entity_trigger_callback)
            case Waitable():
                print('setting waitable callback', entity)
                entity.set_on_ready_callback(entity_trigger_callback)

        # TODO do the other types of entities

    def remove_node(self, node):
        for timer in node.timers:
            self._remove_from_executor(timer)
        for sub in node.subscriptions:
            self._remove_from_executor(sub)
        for gc in node.guards:
            self._remove_from_executor(gc)
        for client in node.clients:
            self._remove_from_executor(client)
        for service in node.services:
            self._remove_from_executor(service)
        for waitable in node.waitables:
            self._remove_from_executor(waitable)

    def _remove_from_executor(self, entity: T_ExecutableEntities):
        # Remove the entity from the executor
        match entity:
            case Timer():
                raise NotImplementedError('Timer is not supported in EventsExecutor yet')
            case Subscription():
                with entity.handle:
                    entity.handle.clear_on_new_message_callback()
            case GuardCondition():
                with entity.handle:
                    entity.handle.clear_on_trigger_callback()
            case Client():
                with entity.handle:
                    entity.handle.clear_on_new_response_callback()
            case Service():
                with entity.handle:
                    entity.handle.clear_on_new_request_callback()
            case Waitable():
                entity.clear_on_ready_callback()

    def get_nodes(self):
        raise NotImplementedError('get_nodes is not supported in EventsExecutor')

    def _exec_subscription(self, sub: Subscription):
        try:
            with sub.handle:
                if (msg_info := sub.handle.take_message(sub.msg_type, sub.raw)) is None:
                    return
                if sub._callback_type is Subscription.CallbackType.MessageOnly:
                    msg_tuple = (msg_info[0], )
                else:
                    msg_tuple = msg_info

                sub.callback(*msg_tuple)
        except InvalidHandle:
            # Subscription is a Destroyable, which means that on __enter__ it can throw an
            # InvalidHandle exception if the entity has already been destroyed.  Handle that here
            # by just returning an empty argument, which means we will skip doing any real work
            # in _execute_subscription below
            pass

    def _exec_timer(self, timer: Timer):
        try:
            with timer.handle:
                timer.handle.call_timer()
        except InvalidHandle:
            # Timer is a Destroyable, which means that on __enter__ it can throw an
            # InvalidHandle exception if the entity has already been destroyed.  Handle that here
            # by just returning an empty argument, which means we will skip doing any real work
            # in _execute_timer below
            pass

    def _exec_client(self, client: Client):
        try:
            with client.handle:
                header, response = client.handle.take_response(client.srv_type.Response)
                if header is None:
                    return
                try:
                    sequence = header.request_id.sequence_number
                    future = client.get_pending_request(sequence)
                except KeyError:
                    # The request was cancelled
                    pass
                else:
                    future._set_executor(self)
                    future.set_result(response)
        except InvalidHandle:
            # Client is a Destroyable, which means that on __enter__ it can throw an
            # InvalidHandle exception if the entity has already been destroyed.  Handle that here
            # by just returning an empty argument, which means we will skip doing any real work
            # in _execute_client below
            pass

    def _exec_service(self, service: Service):
        try:
            with service.handle:
                request, header = service.handle.service_take_request(service.srv_type.Request)
                if header is None:
                    return
                response = service.callback(request, service.srv_type.Response())
                service.send_response(response, header)
        except InvalidHandle:
            # Service is a Destroyable, which means that on __enter__ it can throw an
            # InvalidHandle exception if the entity has already been destroyed.  Handle that here
            # by just returning an empty argument, which means we will skip doing any real work
            # in _execute_service below
            pass

    def _exec_waitable(self, waitable: Waitable):
        for future in waitable._futures:
            future._set_executor(self)
        waitable.execute(waitable.take_data())

    def spin(self):
        # Process events queue
        while rclpy.ok() and not self._shutdown_requested:
            print('EventsExecutor spin')
            self.spin_once()
        print('EventsExecutor shutdown', self._shutdown_requested)

    def spin_until_future_complete(self, future, timeout_sec=None):
        assert timeout_sec is None, 'timeout_sec is not supported in EventsExecutor yet'
        while rclpy.ok() and not self._shutdown_requested and not future.done():
            self.spin_once()

    def spin_once(self, timeout_sec=None):
        try:
            # Get the next event from the queue
            event = self._events.get(timeout=timeout_sec)

            # Execute the event
            match event.entity:
                case Timer():
                    for _ in range(event.count):
                        print('timer event callback')
                        self._exec_timer(event.entity)
                case Subscription():
                    for _ in range(event.count):
                        print('subscription event callback')
                        print(event.entity.topic)
                        self._exec_subscription(event.entity)
                case GuardCondition():
                    for _ in range(event.count):
                        print('guard condition event callback')
                        event.entity.callback()
                case Client():
                    for _ in range(event.count):
                        print('client event callback')
                        self._exec_client(event.entity)
                case Service():
                    for _ in range(event.count):
                        print('service event callback')
                        self._exec_service(event.entity)
                case Waitable():
                    for _ in range(event.count):
                        print('waitable event callback')
                        self._exec_waitable(event.entity)
                case e:
                    raise ValueError(f'Unknown event entity type: {type(e)}')

        # If the queue is still empty after the given timeout just return
        except queue.Empty:
            return

    def spin_once_until_future_complete(self, future, timeout_sec = None):
        raise NotImplementedError('spin_once_until_future_complete is not supported in EventsExecutor')

    def can_execute(self, entity):
        raise NotImplementedError("not supported in EventsExecutor")




