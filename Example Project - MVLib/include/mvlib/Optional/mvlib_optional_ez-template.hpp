#pragma once

/**
 * @file logger_optional_ez-template.hpp
 * @brief Optional Logger adapter for EZ-Template odometry.
 */

#ifdef _MVLIB_OPTIONAL_USED
#error More than one type of Mvlib Optional include used!
#endif

#ifndef _MVLIB_OPTIONAL_USED
#define _MVLIB_OPTIONAL_USED
#include "mvlib/core.hpp" // IWYU pragma: keep
#include "EZ-Template/api.hpp"  // IWYU pragma: keep

#include <optional>

namespace mvlib {

/**
 * @brief Attach EZ-Template odometry to the Logger.
 *
 * Binds a pose getter that queries the EZ-Template drive odom accessors
 * and forwards them into mvlib::Pose. If the drive pointer is null or
 * odometry is disabled, the getter returns std::nullopt.
 *
 * @param logger The Logger instance that will consume the pose getter.
 * @param chassis Pointer to the EZ-Template drive supplying pose data.
 *
 * @warning The caller must ensure the chassis pointer remains valid for
 *          as long as the Logger might invoke the callback.
 *
 * @par Example: EZ-Template odometry
 * @code{.cpp}
 * #include "mvlib/core.hpp"
 * #include "mvlib/Optional/logger_optional_ez-template.hpp"
 *
 * // Provided by your EZ-Template setup.
 * extern ez::Drive chassis;
 *
 * void initialize() {
 *   auto& logger = mvlib::Logger::getInstance();
 *   mvlib::setOdom(logger, &chassis);
 *   logger.start();
 * }
 * @endcode
 */
inline void setOdom(Logger& logger, ez::Drive* chassis) {
  logger.setPoseGetter([chassis]() -> std::optional<Pose> {
    if (!chassis || !chassis->odom_enabled()) return std::nullopt;

    const float xIn   = chassis->odom_x_get();       // inches :contentReference[oaicite:1]{index=1}
    const float yIn   = chassis->odom_y_get();       // inches :contentReference[oaicite:2]{index=2}
    const float thDeg = chassis->odom_theta_get();   // degrees :contentReference[oaicite:3]{index=3}

    return Pose{xIn, yIn, thDeg};
  });
}
} // namespace mvlib
#endif
