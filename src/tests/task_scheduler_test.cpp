#include <gtest/gtest.h>

#include "utils/task_scheduler.hpp"

TEST(TaskSchedulerTest, test_cron) {
  auto& ts = ling::utils::TaskScheduler::singleton();
  ts.schedule_with_cron([]() {
    std::cout << "hey!" << std::endl;
  }, "0 */1 * * * ?");
  std::this_thread::sleep_for(std::chrono::milliseconds(180 * 1000));
}

TEST(TaskSchedulerTest, test_fixed_rate) {
  auto& ts = ling::utils::TaskScheduler::singleton();
  ts.schedule_at_fixed_rate([]() {
    std::cout << "hey!" << std::endl;
  }, std::chrono::milliseconds(10), std::chrono::milliseconds(100));
  std::this_thread::sleep_for(std::chrono::milliseconds(100 * 1000));
}