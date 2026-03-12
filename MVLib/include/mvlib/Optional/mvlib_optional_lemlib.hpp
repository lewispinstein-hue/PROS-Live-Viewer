#pragma once

/**
 * @file logger_optional_lemlib.hpp
 * @brief Optional Logger adapter for LemLib odometry.
 */

#ifndef _MVLIB_OPTIONAL_USED
#error More than one type of Mvlib Optional include used!
#endif

#ifndef _MVLIB_OPTIONAL_USED
#define _MVLIB_OPTIONAL_USED
#include "mvlib/core.hpp" // IWYU pragma: keep

/** 
 * @note Depending on your version of LemLib, this include might be outdated.
 * If lemlib/api.hpp is not found, it is likely this instead:
 * lemlib/lemlib.hpp
*/
#include "lemlib/api.hpp"  // IWYU pragma: keep
namespace mvlib {

/**
 * @brief Attach LemLib odometry to the Logger.
 *
 * Binds a pose getter that reads from lemlib::Chassis::getPose() and
 * forwards the values into mvlib::Pose. If the chassis pointer is null,
 * the getter returns std::nullopt. syntactically
 *
 * @param logger The Logger instance that will consume the pose getter.
 * @param chassis Pointer to the LemLib chassis supplying pose data.
 *
 * @warning The caller must ensure the chassis pointer remains valid for
 *          as long as the Logger might invoke the callback.
 *
 * @par Example: LemLib odometry
 * @code{.cpp}
 * #include "mvlib/core.hpp"
 * #include "mvlib/Optional/logger_optional_lemlib.hpp"
 *
 * // Provided by your LemLib setup.
 * extern lemlib::Chassis chassis;
 *
 * void initialize() {
 *   auto& logger = mvlib::Logger::getInstance();
 *   mvlib::setOdom(logger, &chassis);
 *   logger.start();
 * }
 * @endcode
 */
inline void setOdom(Logger &logger, lemlib::Chassis* chassis) {
  logger.setPoseGetter([chassis]() -> std::optional<Pose> {
    if (!chassis) return std::nullopt;
    auto p = chassis->getPose();
    return Pose{p.x, p.y, p.theta};
  });
}
} // namespace mvlib
#endif
