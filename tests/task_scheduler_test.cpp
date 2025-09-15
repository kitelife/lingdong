#include <gtest/gtest.h>

#include "utils/task_scheduler.hpp"

TEST(TaskSchedulerTest, test_pq) {
  using namespace ling::utils;
  using namespace std::chrono;
  std::priority_queue<TaskTimeEvent, std::vector<TaskTimeEvent>, GreaterComTaskTimeEvent> event_q_ {};
  auto task_func = []() {
    std::cout << "heyhey!" << std::endl;
  };
  auto task2_ptr = std::make_shared<FixedRateTask>(task_func, 500ms, 1000ms);
  auto task2_next_time = task2_ptr->next_time();
  event_q_.emplace(task2_next_time, task2_ptr);
  auto task1_ptr = std::make_shared<FixedRateTask>(task_func, 100ms, 200ms);
  auto task1_next_time = task1_ptr->next_time();
  event_q_.emplace(task1_next_time, task1_ptr);
  //
  std::vector<TaskTimeEvent> sorted_event;
  while (!event_q_.empty()) {
    auto event = event_q_.top();
    sorted_event.emplace_back(event);
    event_q_.pop();
  }
  ASSERT_TRUE(sorted_event[0].first == task1_next_time);
  ASSERT_TRUE(sorted_event[1].first == task2_next_time);
}