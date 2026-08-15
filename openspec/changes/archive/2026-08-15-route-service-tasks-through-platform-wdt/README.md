# route-service-tasks-through-platform-wdt

Move sdf_match_task and sdf_enroll_task off inline esp_task_wdt_* onto the sdf_platform_time_wdt_* wrappers, and make an unregistered reset attempt diagnosable
