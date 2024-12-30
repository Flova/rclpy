import rclpy
from rclpy.node import Node
from rclpy.executors import SingleThreadedExecutor
from rclpy.events_executor import EventsExecutor

from std_msgs.msg import String
from std_srvs.srv import SetBool

from example_interfaces.srv import AddTwoInts


class MinimalSubscriber(Node):

    def __init__(self):
        super().__init__('minimal_subscriber')
        self.subscription = self.create_subscription(
            String,
            'topic',
            self.listener_callback,
            10)

        self.subscription_array = [self.create_subscription(
            String,
            f'topic_{i}',
            self.listener_callback,
            10) for i in range(10)]

        self.counter = 0
        self.start_time = self.get_clock().now()

        self.service = self.create_service(SetBool, 'service', self.service_callback)

        self.client = self.create_client(AddTwoInts, '/add_two_ints')

        self.guard_condition = self.create_guard_condition(self.guard_condition_callback)

    def listener_callback(self, msg):
        self.counter += 1
        if self.counter % 5000 == 0:
            print(f'I heard: {msg.data}')

    def guard_condition_callback(self):
        print('Guard condition callback')
        return True

    def service_callback(self, request, response):
        print('Service callback')
        response.success = request.data

        self.client.wait_for_service()
        request_1 = AddTwoInts.Request()
        request_1.a = 1
        request_1.b = 2
        resp_future = self.client.call_async(request_1)
        print('Waiting for response')
        self.executor.spin_until_future_complete(resp_future)
        resp = resp_future.result()
        print(resp)

        # Add subscription during runtime

        def tmp_callback(msg):
            print(f'I heard: {msg.data}')

        self.subscription_array.append(self.create_subscription(
            String,
            'new_sub',
            tmp_callback,
            10))

        self.guard_condition.trigger()

        response.success = request.data

        return response


def main(args=None):
    rclpy.init(args=args)

    minimal_subscriber = MinimalSubscriber()

    use_events_executor = True

    try:
        if use_events_executor:
            executor = EventsExecutor()
        else:
            executor = SingleThreadedExecutor()

        minimal_subscriber.executor = executor
        executor.spin()
    except KeyboardInterrupt:
        pass

    print('Recived rate: ', minimal_subscriber.counter / (minimal_subscriber.get_clock().now() - minimal_subscriber.start_time).nanoseconds * 1e9)

    executor.remove_node(minimal_subscriber)

    # https://github.com/eclipse-cyclonedds/cyclonedds/issues/1937


if __name__ == '__main__':
    main()
