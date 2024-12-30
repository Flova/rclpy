# An alternative events queue based executor for rclpy.
from typing import Optional, TypeAlias
import rclpy
import queue
from rclpy.executors import Executor
from rclpy.node import Node
from rclpy.waitable import Waitable
from rclpy.utilities import get_default_context
from rclpy.signals import SignalHandlerGuardCondition
from rclpy.clock import Clock
from rclpy.clock import ClockType
from rclpy.subscription import Subscription
from rclpy.callback_groups import CallbackGroup
from rclpy.guard_condition import GuardCondition
from rclpy.timer import Timer
from rclpy.client import Client
from rclpy.service import Service
from rclpy.exceptions import InvalidHandle

from dataclasses import dataclass

from threading import Lock

from weakref import ReferenceType, ref

T_ExecutableEntities: TypeAlias = GuardCondition | Subscription | Waitable | Timer | Client | Service

@dataclass
class Event:
    entity: T_ExecutableEntities
    count: int

class EventsExecutor(Executor):
    def __init__(self, *, context=None):
        self._context = get_default_context() if context is None else context
        self._events: queue.Queue[Event] = queue.Queue()
        self._blocked_events: list[Event] = []
        self._present_entities: set[ReferenceType[T_ExecutableEntities]] = set()
        self._nodes: set[ReferenceType[Node]] = set()

        # Executor cannot use ROS clock because that requires a node
        self._clock = Clock(clock_type=ClockType.STEADY_TIME)

        # This lock needs to be held when the modify the collections of entities used by the executor
        # This could be when adding or removing entities from the executor, due to adding or removing nodes
        self._entities_collections_lock = Lock()

        # This guard condition is used to wake the executor
        self._wake_guard_condition = GuardCondition(
            callback=None, callback_group=None, context=self._context
        )
        with self._entities_collections_lock:
            self._add_to_executor(self._wake_guard_condition)

        self._shutdown_requested = False # TODO implement shutdown correctly


    def wake(self) -> None:
        """
        Wake the executor because something changed.

        This is used to tell the executor when entities are created or destroyed.
        """
        with self._entities_collections_lock:
            # Rebuild the present entities set, because there might be new entities
            for node in self._nodes:
                # Check if the weak reference is still alive
                if (node := node()) is not None:
                    self._extract_executable_entities_from_node(node)

        # Wake the executor
        self._wake_guard_condition.trigger()


    def shutdown(self, timeout_sec = None) -> bool:  # TODO handle timeout
        raise NotImplementedError('shutdown is not supported in EventsExecutor yet')

    def add_node(self, node: Node):
        # Make sure we are the only ones modifying the collections of entities used by the executor
        with self._entities_collections_lock:
            # Check if the node is already in the executor
            # We only hold weak references to the nodes,
            # so we need to check if the weak reference is in the set
            if ref(node) in self._nodes:
                return
            # Store the weak reference to the node for later reference
            self._nodes.add(ref(node))
            # Add the executable entities of the node to the executor
            self._extract_executable_entities_from_node(node)

    def _extract_executable_entities_from_node(self, node: Node):
        # This needs to be called with the lock held
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
        # Build weak reference so we don't hang on old subscriptions etc.
        w_entity = ref(entity)

        # Check if the entity is already in the executor
        # We only hold weak references to the entities,
        # so we need to check if the weak reference is in the set
        if w_entity in self._present_entities:
            return
        # Store the weak reference to the entity for later reference
        self._present_entities.add(w_entity)

        # This callback will be called when the entity triggers (e.g. message received)
        # It subsequently puts the entity in the events queue
        def entity_trigger_callback(count: int):
            self._events.put(Event(
                entity=w_entity,
                count=count
            ))

        # Register callback for the entity
        match entity:
            case Timer():
                raise NotImplementedError('Timer is not supported in EventsExecutor yet')
            case Subscription():
                with entity.handle:
                    entity.handle.set_on_new_message_callback(entity_trigger_callback)
            case GuardCondition():
                with entity.handle:
                    entity.handle.set_on_trigger_callback(entity_trigger_callback)
            case Client():
                with entity.handle:
                    entity.handle.set_on_new_response_callback(entity_trigger_callback)
            case Service():
                with entity.handle:
                    entity.handle.on_new_request_callback(entity_trigger_callback)
            case Waitable():
                entity.set_on_ready_callback(entity_trigger_callback)

    def remove_node(self, node):
        # Make sure we are the only ones modifying the collections of entities used by the executor
        with self._entities_collections_lock:
            # Remove the weak reference to the node
            self._nodes.remove(ref(node))

            # Remove the executable entities of the node from the executor aka. remove the callbacks in the middleware
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

        # Remove the entity from the present entities
        self._present_entities.remove(ref(entity))

    def get_nodes(self) -> list[Node]:
        # We only hold weak references to the nodes, so we need to cast them to strong references and filter out the None values
        with self._entities_collections_lock:
            return [node() for node in self._nodes if node() is not None]

    def _exec_subscription(self, sub: Subscription):
        try:
            with sub.handle:
                if (msg_info := sub.handle.take_message(sub.msg_type, sub.raw)) is None:
                    return
                if sub._callback_type is Subscription.CallbackType.MessageOnly:
                    msg_tuple = (msg_info[0], )
                else:
                    msg_tuple = msg_info

                sub.callback_group.beginning_execution(sub)
                sub.callback(*msg_tuple)
                sub.callback_group.ending_execution(sub)
                self._wake_guard_condition.trigger()
        except InvalidHandle:
            # Subscription is a Destroyable, which means that on __enter__ it can throw an
            # InvalidHandle exception if the entity has already been destroyed.  Handle that here
            # by just returning an empty argument, which means we will skip doing any real work
            # in _execute_subscription below
            pass

    def _exec_timer(self, timer: Timer):
        try:
            with timer.handle:
                timer.callback_group.beginning_execution(timer)
                timer.handle.call_timer()
                timer.callback_group.ending_execution(timer)
                self._wake_guard_condition.trigger()
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
                    client.callback_group.beginning_execution(client)
                    future.set_result(response)
                    client.callback_group.ending_execution(client)
                    self._wake_guard_condition.trigger()
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
                service.callback_group.beginning_execution(service)
                response = service.callback(request, service.srv_type.Response())
                service.send_response(response, header)
                service.callback_group.ending_execution(service)
                self._wake_guard_condition.trigger()
        except InvalidHandle:
            # Service is a Destroyable, which means that on __enter__ it can throw an
            # InvalidHandle exception if the entity has already been destroyed.  Handle that here
            # by just returning an empty argument, which means we will skip doing any real work
            # in _execute_service below
            pass

    def _exec_waitable(self, waitable: Waitable):
        for future in waitable._futures:
            future._set_executor(self)
        waitable.callback_group.beginning_execution(waitable)
        waitable.execute(waitable.take_data())
        waitable.callback_group.ending_execution(waitable)
        self._wake_guard_condition.trigger()

    def _exec_guard_condition(self, gc: GuardCondition):
        # Don't execute the guard condition if it is the wake guard condition
        # Otherwise we would end up in an infinite loop
        if gc != self._wake_guard_condition:
            gc.callback_group.beginning_execution(gc)
            gc.callback()
            gc.callback_group.ending_execution(gc)
            self._wake_guard_condition.trigger()

    def spin(self):
        # Process events queue
        while rclpy.ok() and not self._shutdown_requested:
            self.spin_once()

    def spin_until_future_complete(self, future, timeout_sec=None):
        # Store the current time to calculate the time elapsed
        start_time = self._clock.now()
        # Run the executor until the future is done, we shutdown or if the timeout is reached (if given)
        while rclpy.ok() and not self._shutdown_requested and not future.done() and \
                (timeout_sec is None or (time_elapsed := (self._clock.now() - start_time).nanoseconds / 1e9) < timeout_sec):
            # If we don't have a timeout, we set the time left to None
            if timeout_sec is None:
                time_left_in_timeout = None
            else:
                # Calculate the time left until the timeout happens
                # The spin once function will therefore only spin for the remaining time at most
                time_left_in_timeout = timeout_sec - time_elapsed
            self.spin_once(time_left_in_timeout)

    def spin_once(self, timeout_sec=None):
        try:
            # First check if any of the blocked events can be executed now
            # They might have been blocked because of their callback group being mutually exclusive
            for event in self._blocked_events:
                # Get regiular event entity (not weakref)
                entity = event.entity()
                # If the entity is None, it means it was destroyed and we can remove it from the blocked events
                if entity is None:
                    self._blocked_events.remove(event)
                    continue
                if self.can_execute(entity):
                    # Remove the event from the blocked events
                    self._blocked_events.remove(event)
                    break
            else: # If no blocked events can be executed
                # Get the next event from the queue
                event = self._events.get(timeout=timeout_sec)
                # Get regiular event entity (not weakref)
                entity = event.entity()
                # If the entity is None, it means it was destroyed
                if entity is None:
                    return
                # Check if the event can be executed
                if not self.can_execute(entity):
                    # Add the event to the blocked events
                    self._blocked_events.append(event)
                    print(f'blocked event: {event}')
                    return

            # Execute the event
            match entity:
                case Timer():
                    for _ in range(event.count):
                        self._exec_timer(entity)
                case Subscription():
                    for _ in range(event.count):
                        self._exec_subscription(entity)
                case GuardCondition():
                    for _ in range(event.count):
                        self._exec_guard_condition(entity)
                case Client():
                    for _ in range(event.count):
                        self._exec_client(entity)
                case Service():
                    for _ in range(event.count):
                        self._exec_service(entity)
                case Waitable():
                    for _ in range(event.count):
                        self._exec_waitable(entity)
                case e:
                    raise ValueError(f'Unknown event entity type: {type(e)}')

        # If the queue is still empty after the given timeout just return
        except queue.Empty:
            return

    def spin_once_until_future_complete(self, future, timeout_sec = None):
        raise NotImplementedError('spin_once_until_future_complete is not supported in EventsExecutor')

    def can_execute(self, entity) -> bool:
        # Some entities have no callback group and can be run at any time
        if entity.callback_group is None:
            return True
        # Direct the call to the callback group
        return entity.callback_group.can_execute(entity)
