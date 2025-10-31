#!/usr/bin/env python3

from typing import Dict
from typing import List
from typing import Optional

import rclpy
from rclpy.action import ActionClient
from rclpy.action.client import ClientGoalHandle
from rclpy.executors import ExternalShutdownException
from rclpy.executors import SingleThreadedExecutor
from rclpy.lifecycle import Node
from rclpy.lifecycle import Publisher
from rclpy.lifecycle import State
from rclpy.lifecycle import TransitionCallbackReturn
from rclpy.qos import DurabilityPolicy
from rclpy.qos import HistoryPolicy
from rclpy.qos import QoSProfile
from rclpy.service import Service
from rclpy.subscription import Subscription
from rclpy.task import Future

from project11_msgs.msg import BehaviorInformation
from marine_nav_interfaces.msg import TaskInformation, TaskFeedback
from marine_nav_interfaces.action import RunTasks

from mission_manager_interfaces.srv import TaskManagerCmd

from marine_nav_tasks import TaskList


from .camp_interface import CampInterface


class MissionManager(Node):
    """
    Manages the list of tasks sent to the navigator.
    """
    def __init__(self, node_name='mission_manager', **kwargs):
        self.task_service_server: Optional[Service] =  None
        self.goal_future: Optional[Future] = None
        self.goal_handle: Optional[ClientGoalHandle] = None
        self.camp = CampInterface(self)
        super().__init__(node_name, **kwargs)
        self.get_logger().debug('Initializing mission manager node')


    def on_configure(self, state: State) -> TransitionCallbackReturn:
        self.get_logger().debug('Configuring mission manager')
        self.tasks = TaskList(self)

        # A task that may be added, such as hover,
        # to temporarily interrupt current task.
        self.override_task_id: str = None

        self.done_task_information = TaskInformation()
        self.done_task_information.type = "hover"
        self.done_task_information.id = "done_hover"
        self.done_task_information.priority = 100

        self.tasks.addOrUpdate(self.done_task_information)


        # A place to hold the running behavior information publishers.
        self.behavior_library: Dict[str, List[BehaviorInformation]] = {}
        self.behavior_info_publishers: Dict[str, Publisher] = {}
        self.behavior_feedback_subscribers: Dict[str, Subscription] = {}

        self.navigator_client = ActionClient(self, RunTasks, 'run_tasks')
        self.goal_future = None
        self.goal_handle = None
        self.camp.on_configure()
        self.get_logger().debug('Mission manager configured')
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: State) -> TransitionCallbackReturn:        
        self.task_service_server = self.create_service(TaskManagerCmd, 'task_manager', self.taskManagerCallback)
        self.get_logger().debug('Activating mission manager, about to activate CAMP interface')
        self.camp.on_activate()
        self.get_logger().debug('Mission manager activated, about to update navigator')
        try:
            self.updateNavigator()
        except Exception as e:
            self.get_logger().error('Error updating navigator on activation: ' + str(e))
            return TransitionCallbackReturn.FAILURE
        self.get_logger().debug('Navigator updated')
        return super().on_activate(state)

    def on_deactivate(self, state: State) -> TransitionCallbackReturn:
        self.get_logger().info('Deactivating mission manager')
        self.task_service_server.destroy()
        self.task_service_server = None
        # if self.goal_future is not None:
        #     future = self.goal_future.cancel_goal_async()
        #     future.add_done_callback(lambda self: self.get_logger().info('Goal cancelled'))
        #     self.goal_future = None
        self.camp.on_deactivate()
        return super().on_deactivate(state)

    def on_cleanup(self, state):
        self.navigator_client.destroy()
        self.navigator_client = None
        self.camp = None
        return super().on_cleanup(state)

    def updateNavigator(self):
        # if self.goal_future is not None:
        #     self.get_logger().info('Canceling previous pending goal')
        #     self.goal_future.cancel()
        #     self.goal_future = None
        # if self.goal_handle is not None:
        #     self.get_logger().info('Cancelling previous goal')
        #     cancel_future = self.goal_handle.cancel_goal_async()
        #     cancel_future.add_done_callback(self.cancel_goal_callback)
        #     self.goal_handle = None
        #     rclpy.spin_until_future_complete(self, cancel_future, timeout_sec=2.0)
        goal = RunTasks.Goal()
        goal.tasks = self.tasks.listMessages()

        # for t in goal.tasks:

        #     # Populate the library with the behaviors for this task.
        #     self.behavior_library[t.id] = t.behaviors

        #     for bhv in t.behaviors:
        #         # Don't make new pub/sub's if they already exist.
        #         if bhv.id not in self.behavior_info_publishers.keys():
        #             # Create behavior publisher and subscriber
        #             latching_qos = QoSProfile(
        #                 depth=1,
        #                 durability=DurabilityPolicy.TRANSIENT_LOCAL,
        #                 history=HistoryPolicy.KEEP_LAST
        #                 )
        #             self.behavior_info_publishers[bhv.id] = self.create_lifecycle_publisher(
        #                 TaskFeedback,
        #                 'project11/behaviors/' + bhv.type + '/input', qos_profile=latching_qos
        #             )
            
        #             self.behavior_feedback_subscribers[bhv.id] = self.create_subscription(
        #                 BehaviorInformation,
        #                 'project11/behaviors/' + bhv.type + '/feedback',
        #                 self.behaviorFeedbackCallback,
        #                 1
        #             )

        #     # Send the behavior info here for each? or Wait until feedback from the navigator?
            
        if self.navigator_client.wait_for_server(timeout_sec=2.0):
            self.get_logger().info('mission_manager: sending goal:')
            for t in goal.tasks:
                self.get_logger().info(" task: "+ t.id + " type: " + t.type + " status: " + t.status + " done: " + str(t.done))
            self.goal_future = self.navigator_client.send_goal_async(goal, feedback_callback=self.navigator_feedback_callback)
            self.goal_future.add_done_callback(self.navigator_goal_response_callback)

        else:
            self.get_logger().warn('Timeout waiting for navigator action server')

    def navigator_goal_response_callback(self, future: Future):
        self.goal_handle = future.result()
        self.goal_future = None
        if self.goal_handle is None:
            self.get_logger().warn('Goal response is None')
            return
        if not self.goal_handle.accepted:
            self.get_logger().info('Goal rejected :(')
            self.goal_handle = None
            return
        self.get_logger().info('Goal accepted :)')

        self.result_future = self.goal_handle.get_result_async()
        self.result_future.add_done_callback(self.navigator_done_callback)


    def navigator_feedback_callback(self, feedback_msg: RunTasks.Feedback):
        # This block updates our local list of tasks with the status from the navigator.
        needUpdate = False
        if feedback_msg is not None:
            task_feedback: TaskFeedback = feedback_msg.feedback.feedback
            task_ids = self.tasks.addOrUpdateMany(task_feedback.tasks)
            self.tasks.clearExcept(task_ids)

            for updated_task in task_feedback.tasks:
                if updated_task.id == self.override_task_id:
                    if updated_task.done:
                        self.tasks.remove(updated_task.id)
                        self.override_task_id = None
                        needUpdate = True

            # TODO: Verify that the task and its children activate the behavior. 
            if task_feedback.current_navigation_task in self.behavior_library.keys():
                task = self.tasks.task(task_feedback.current_navigation_task)
                while task is not None:
                    task_feedback = TaskFeedback()
                    task_feedback.current_navigation_task = task_feedback.current_navigation_task
                    task_feedback.tasks.append(task.message())

                    for behavior in task.task_information.behaviors:
                        self.behavior_info_publishers[behavior.id].publish(task_feedback)
                    task = task.parent()

        self.camp.navigatorFeedback(feedback_msg)

        if needUpdate:
            self.updateNavigator()

    def navigator_done_callback(self, future):
        self.get_logger().info('navigator done')
        result = future.result()
        self.camp.navigatorDone(None, result.result)
        self.result_future = None
        self.goal_handle = None

    def cancel_goal_callback(self, future):
        self.get_logger().info('Goal cancelled')
        self.cancel_future = None

    
    def behaviorFeedbackCallback(self,feedback):
        self.get_logger().info('behavior feedback:', feedback)

    def replaceTasks(self, new_tasks):
        self.tasks.clear()
        self.tasks.addOrUpdateMany(new_tasks)
        self.updateNavigator()

    def appendTasks(self, new_tasks):
        self.tasks.addOrUpdateMany(new_tasks)
        self.updateNavigator()

    def prependTasks(self, new_tasks):
        self.addOrUpdateMany(new_tasks, prepend=True)
        self.updateNavigator()

    def clearTasks(self):
        self.tasks.clear()
        if self.done_task_information is not None:
            self.appendTasks([self.done_task_information,])
        self.updateNavigator()

    def updateTasks(self, new_or_updated_tasks):
        self.tasks.addOrUpdateMany(new_or_updated_tasks)
        self.updateNavigator()

    def setOverrideTask(self, task = None):
        if self.override_task_id is not None:
            self.tasks.remove(self.override_task_id)
        if task is None:
            self.override_task_id = None
        else:
            self.tasks.addOrUpdate(task, prepend=True)
            self.override_task_id = task.id
        self.updateNavigator()

    def updateLocalTaskList(self, command, new_tasks):
        '''Updates the local task list'''

        if command == 'replace_tasks':
            self.replaceTasks(new_tasks)
        elif command == 'append_tasks':
            self.appendTasks(new_tasks)
        elif command == 'prepend_tasks':
            self.prependTasks(new_tasks)
        elif command == 'clear_tasks':
            self.clearTasks()
        elif command == 'update':
            self.updateTasks(new_tasks)

        self.get_logger().warn('mission_manager: Received unknown command on task_manager service: ' + command)

    def taskManagerCallback(self, request, response):
        '''Receives commands to manipulate the navigator's task list.
        
        Args:
            request:    TaskManagerCmd
            
        '''

        self.updateLocalTaskList(request.command, request.tasks)

        return response


def main(args=None):
    rclpy.init()
    executor = SingleThreadedExecutor()
    mission_manager = MissionManager("mission_manager")
    executor.add_node(mission_manager)
    try:
        executor.spin()
    except (KeyboardInterrupt,ExternalShutdownException):
        pass
    
if __name__ == '__main__':
    main()
