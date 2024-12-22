# An alternative events queue based executor for rclpy.
from typing import Optional, TypeAlias
import rclpy
import queue
from rclpy.executors import Executor
from rclpy.node import Node
from rclpy.subscription import Subscription
from rclpy.waitable import Waitable
from rclpy.callback_groups import CallbackGroup
from dataclasses import dataclass

from rclpy.exceptions import InvalidHandle

T_ExecutableEntities: TypeAlias = Waitable | Subscription # TODO add Timer, Service, Client, etc.

@dataclass
class EntityContainer:
    entity: T_ExecutableEntities
    callback_group: CallbackGroup
    node: Optional[Node]

    def __hash__(self):
        return hash(self.entity)

@dataclass
class Event:
    entity: T_ExecutableEntities
    count: int

class EventsExecutor(Executor):
    def __init__(self, *, context=None):
        self._context = context
        self._events: queue.Queue[Event] = queue.Queue()

        self._executable_entities: set[EntityContainer] = set()


    def shutdown(self, timeout_sec = None):
        raise NotImplementedError('shutdown is not supported in EventsExecutor')

    def add_node(self, node: Node):
        # Add the executable entities of the node to the executor
        for sub in node.subscriptions:
            # Check if the subscription is already in the executable entities set
            if not any(entity.entity == sub for entity in self._executable_entities):
                # Add the subscription to the executable entities set
                self._add_to_executor(
                    entity=sub,
                    callback_group=sub.callback_group,
                    node=node
                )

    def _add_to_executor(self, entity: T_ExecutableEntities, callback_group: CallbackGroup, node: Optional[Node]):
        # Add the entity to the executable entities set
        self._executable_entities.add(EntityContainer(
            entity=entity,
            callback_group=callback_group,
            node=node
        ))

        def entity_trigger_callback(count: int):
            self._events.put(Event(
                entity=entity,
                count=count
            ))

        # Register callback for the entity
        if isinstance(entity, Subscription):
            entity.set_on_new_message_callback(entity_trigger_callback)

        # TODO do the other types of entities

    def remove_node(self, node):
        raise NotImplementedError('remove_node is not supported in EventsExecutor')

    def get_nodes(self):
        raise NotImplementedError('get_nodes is not supported in EventsExecutor')

    def spin(self):
        # Process events queue
        while rclpy.ok():
            event = self._events.get()
            match event.entity:
                case Subscription():
                    for _ in range(event.count):
                        self._exec_subscription(event.entity)

            # TODO do the other types of entities

    def _exec_subscription(self, sub: Subscription):
        try:
            with sub.handle:
                msg_info = sub.handle.take_message(sub.msg_type, sub.raw)
                if msg_info is None:
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

    def spin_until_future_complete(self, future, timeout_sec=None):
        raise NotImplementedError('spin_until_future_complete is not supported in EventsExecutor')

    def spin_once(self, timeout_sec=None):
        raise NotImplementedError('spin_once is not supported in EventsExecutor')

    def spin_once_until_future_complete(self, future, timeout_sec = None):
        raise NotImplementedError('spin_once_until_future_complete is not supported in EventsExecutor')

    def can_execute(self, entity):
        raise NotImplementedError('can_execute is not supported in EventsExecutor')




