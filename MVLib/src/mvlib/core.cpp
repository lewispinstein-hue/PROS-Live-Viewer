#include "pros/rtos.hpp"
#include "mvlib/core.hpp"
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <sys/stat.h> 

#ifdef MVLIB_LOGS_REDEFINED
#define LOG_DEBUG MVLIB_LOG_DEBUG
#define LOG_INFO MVLIB_LOG_INFO
#define LOG_WARN MVLIB_LOG_WARN
#define LOG_ERROR MVLIB_LOG_ERROR
#define LOG_FATAL MVLIB_LOG_FATAL
#endif

namespace mvlib {

Logger &Logger::getInstance() {
  static Logger instance;
  return instance;
}

void Logger::setLogToTerminal(bool v) {
  m_config.logToTerminal.store(v);
  LOG_DEBUG("logToTerminal set to: %d", v);
}

void Logger::setLogToSD(bool v) {
  if (m_started || m_sdLocked) {
    LOG_WARN("setLogToSD() called after logger start — ignored. Set value: %d", v);
    return;
  }
  m_config.logToSD.store(v);
  LOG_DEBUG("logToSD set to: %d", v);
}

void Logger::setPrintWatches(bool v) {
  m_config.printWatches.store(v);
  LOG_DEBUG("printWatches set to: %d", v);
}

void Logger::setLoggerMinLevel(LogLevel level) {
  LOG_DEBUG("SetLoggerMinLevel set to: %d", (int)level);
  m_minLogLevel = level;
}

void Logger::setPoseGetter(std::function<std::optional<Pose>()> getter) {
  MutexGuard m(m_mutex, TIMEOUT_MAX);
  if (!m.isLocked()) return;
  m_getPose = std::move(getter);
}

bool Logger::setRobot(Drivetrain drivetrain) {
  if (m_configSet) {
    LOG_WARN("setRobot(Drivetrain) called twice!");
    return false;
  }
  m_configSet = true;

  if (!drivetrain.leftDrivetrain || !drivetrain.rightDrivetrain) {
    LOG_FATAL("setRobot(Drivetrain) called with nullptr drivetrain arguments!");
    return false;
  }

  m_pLeftDrivetrain = drivetrain.leftDrivetrain;
  m_pRightDrivetrain = drivetrain.rightDrivetrain;

  LOG_INFO("setRobot() successfully set variables!");
  return true;
}

const char *Logger::m_levelToString(const LogLevel level) const {
  switch (level) {
  case LogLevel::DEBUG: return "DEBUG";
  case LogLevel::INFO:  return "INFO";
  case LogLevel::WARN:  return "WARN";
  case LogLevel::ERROR: return "ERROR";
  case LogLevel::FATAL: return "FATAL";
  default:              return "UNKNOWN";
  }
  UNREACHABLE();
}

void Logger::logMessage(LogLevel level, const char *fmt, ...) {
  if (level < m_minLogLevel || !(int)m_minLogLevel) return;

  MutexGuard m(m_terminalMutex);
  if (!m.isLocked()) return;

  char buffer[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  double time = pros::millis() / 1000.0;
  if (m_config.logToTerminal.load()) 
    printf("[%.2f] [%s]: %s\n", time, m_levelToString(level), buffer);

  if (m_config.logToSD.load()) 
    logToSD(m_levelToString(level), "%s", buffer);
}

void Logger::m_makeTimestampedFile() {
  time_t now = time(0);
  tm *tstruct = localtime(&now);

  if (tstruct->tm_year < 100) {
    LOG_DEBUG("VEX RTC Inaccurate. Falling back to program duration and last upload date.");
    snprintf(m_currentFilename, sizeof(m_currentFilename), "/usd/MVLIB_%s_%u-%u.log",
             date, pros::millis() / 1000, pros::millis() / 100);
  } else {
    LOG_DEBUG("VEX RTC Plausible. Creating file name with date.");
    strftime(m_currentFilename, sizeof(m_currentFilename),
             "/usd/MVLIB_%Y-%m-%d_%H-%M.log", tstruct);
  }
} 

bool Logger::m_initSDLogger() {
  if (pros::usd::is_installed()) {
    LOG_DEBUG("SD Card installed (On first attempt)");
    pros::delay(500);
  } else {
    LOG_DEBUG("SD Card not installed, rechecking...");
    for (int i = 0; i < 10; i++) {
      if (pros::usd::is_installed()) {
        LOG_DEBUG("SD Card installed! Attempt: %d/10", i);
        break;
      }
      LOG_DEBUG("Rechecking SD card installment... Attempts: %d/10", i);
      pros::delay(200);
    }
  }

  if (!pros::usd::is_installed()) {
    LOG_FATAL("SD Card not installed after 10 attemps. Aborting SD card.");
    return false;
  }

  m_makeTimestampedFile();

  m_sdFile = fopen(m_currentFilename, "w");
  if (!m_sdFile) {
    LOG_FATAL("File: %s could not be opened. Aborting.", m_currentFilename);
    return false;
  }
  LOG_DEBUG("File successfully opened.");
  fprintf(m_sdFile, "=== Logger initialized at %.2fs ===\n", pros::millis() / 1000.0);
  fflush(m_sdFile);
  return true;
}

void Logger::logToSD(const char *levelStr, const char *fmt, ...) {
  if (!m_sdFile || m_sdLocked) return;

  MutexGuard m(m_sdCardMutex);
  if (!m.isLocked()) return;
  uint32_t now = pros::millis();

  fprintf(m_sdFile, "[%.2f] [%s]: ", now / 1000.0, levelStr);

  va_list args;
  va_start(args, fmt);
  vfprintf(m_sdFile, fmt, args);
  va_end(args);

  fprintf(m_sdFile, "\n");

  bool isError = (strcmp(levelStr, "ERROR") == 0 || strcmp(levelStr, "FATAL") == 0);

  if (isError || (now - m_lastFlush >= SD_FLUSH_INTERVAL_MS)) {
    fflush(m_sdFile);
    m_lastFlush = now;
  }
}

bool Logger::m_checkRobotConfig() {
  MutexGuard m(m_mutex, TIMEOUT_MAX);

  bool allValid = true;

  if (!m_pLeftDrivetrain) {
    LOG_FATAL("Left Drivetrain pointer is NULL!");
    allValid = false;
  }
  if (!m_pRightDrivetrain) {
    LOG_FATAL("Right Drivetrain pointer is NULL!");
    allValid = false;
  }

  return allValid;
}

uint32_t Logger::status() const {
  if (!m_task) return pros::E_TASK_STATE_INVALID;
  return m_task->get_state();
}

void Logger::pause() {
  uint32_t st = status();
  if (st != pros::E_TASK_STATE_DELETED && st != pros::E_TASK_STATE_INVALID &&
      st != pros::E_TASK_STATE_SUSPENDED) {
    m_task->suspend();
    LOG_INFO("Logger paused.");
  } else LOG_INFO("Logger cannot be paused as it is not in a running state.");
}

void Logger::resume() {
  uint32_t st = status();
  if (st != pros::E_TASK_STATE_DELETED && st != pros::E_TASK_STATE_INVALID &&
      st == pros::E_TASK_STATE_SUSPENDED) {
    m_task->resume();
    LOG_INFO("Logger resumed.");
  } else LOG_INFO("Logger cannot be resumed as it is not paused.");
}

void Logger::start() {
  if (m_started) {
    LOG_WARN("start() called a second time. Aborted!");
    return;
  }
  m_started = true;

  // SD init happens here.
  if (m_config.logToSD.load() && m_sdFile == nullptr) {
    bool success = m_initSDLogger();
    if (!success) {
      m_config.logToSD.store(false);
      m_sdLocked = true;
      LOG_FATAL("initSDCard failed! Unable to initialize SD card.");
    } else LOG_INFO("Successfully initialized SD card with filename: %s!", m_currentFilename);
  }
    
  // Check config
  m_configValid = m_checkRobotConfig();
  if (!m_configValid) LOG_ERROR("At least one pointer set by setRobot(Drivetrain) is nullptr. "
                                "Using speed estimation instead.");
  else LOG_INFO("All pointers set by setRobot(Drivetrain) seem to be valid.");

  // Create task that runs Update
  m_task = std::make_unique<pros::Task>([this]() mutable {
    pros::delay(200);
    // Wait for controller RX settle
    if (m_config.logToTerminal.load()) pros::delay(1000);
    while (true) {
      // Update loop
      try { this->Update(); }
      catch (std::exception &e) {
        LOG_FATAL("%s\n", e.what());
      }

      // Flush stdout buffer, and wait appropriate time
      if (m_config.logToTerminal.load()) {
        fflush(stdout); // Stdout is only flushed if logging to terminal
        pros::delay(terminalPollingRate);
      } else pros::delay(sdCardPollingRate);
    }
  }, TASK_PRIORITY_DEFAULT, TASK_STACK_DEPTH_DEFAULT, "mvlib Logger");
}

void Logger::printWatches() {
  uint32_t nowMs = pros::millis();
  for (auto &[id, w] : m_watches) {
    // Gate evaluation frequency for not onChange watches
    if (!w.onChange && w.lastPrintMs != 0 &&
       (nowMs - w.lastPrintMs) < w.intervalMs) continue;

    if (!w.eval) continue;

    auto [lvl, valueStr, label] = w.eval();

    if (w.onChange) {
      if (w.lastValue && *w.lastValue == valueStr) continue;
      w.lastValue = valueStr;
    } else if (!w.onChange) w.lastPrintMs = nowMs;
    
    // Add watch tag and add comma separators
    label = std::string("[WATCH],") +
            std::to_string(nowMs) + 
            "," + m_levelToString(lvl) 
            +  "," + label + "," + valueStr;

    switch (lvl) {
      case LogLevel::DEBUG: LOG_DEBUG("%s", label.c_str()); break;
      case LogLevel::INFO:  LOG_INFO("%s",  label.c_str()); break;
      case LogLevel::WARN:  LOG_WARN("%s",  label.c_str()); break;
      case LogLevel::ERROR: LOG_ERROR("%s", label.c_str()); break;
      default:              LOG_INFO("%s",  label.c_str()); break;
    }
  }
}

void Logger::Update() {
  if (m_config.printWatches.load()) printWatches();

  static double leftVelocity, rightVelocity;

  if (m_configValid) {
    static auto getGearsetValue = [&](pros::MotorGears gearset) {
      switch (gearset) {
        case pros::MotorGears::rpm_100: return 100.0; break;
        case pros::MotorGears::rpm_200: return 200.0; break;
        case pros::MotorGears::rpm_600: return 600.0; break;
        default:                        return 127.0; // If unknown, leave unmodified
      }
    };

    static auto norm = [&](double rpm, pros::MotorGears gearset) {
      double v = (rpm / getGearsetValue(gearset)) * 127.0;
      if (v > 127) v = 127;
      if (v < -127) v = -127;
      return v;
    };

    pros::MotorGears leftGearing = m_pLeftDrivetrain 
                     ? m_pLeftDrivetrain->get_gearing()
                     : pros::MotorGears::invalid;

    pros::MotorGears rightGearing = m_pRightDrivetrain
                     ? m_pRightDrivetrain->get_gearing()
                     : pros::MotorGears::invalid;

    // Update drivetrain speed
    leftVelocity = norm(m_pLeftDrivetrain->get_actual_velocity(), leftGearing);
    rightVelocity = norm(m_pRightDrivetrain->get_actual_velocity(), rightGearing);
  } else {
    // Because no drivetrain, we do speed approx with pose
    auto pose = m_getPose ? m_getPose() : std::nullopt;
    if (!pose) pose = {0, 0, 0};

    uint32_t nowMs = pros::millis();
    static Pose prevPose;
    static uint32_t prevMs = 0;
    double avgSpeed = 0.0;

    double dt = (nowMs - prevMs) / 1000.0; // delta time
    double vx = (dt > 0) ? pose->x - prevPose.x / dt : 0.0; // x velocity
    double vy = (dt > 0) ? pose->y - prevPose.y / dt : 0.0; // y velocity

    // Update drivetrain speed
    avgSpeed = (vx + vy) / 2.0;
    leftVelocity = avgSpeed;
    rightVelocity = avgSpeed;

    prevPose = *pose;
    prevMs = nowMs;
  }

  auto pose = m_getPose ? m_getPose() : std::nullopt;
  if (!pose) return;
  
  double normalizedTheta = fmod(pose->theta, 360.0); // Normalize theta 
  if (normalizedTheta < 0) normalizedTheta += 360.0;
  
  // Print main telemetry
  LOG_INFO("[DATA],%d,%.2f,%.2f,%.2f,%.1f,%.1f", 
            pros::millis(), pose->x, pose->y, normalizedTheta,
            leftVelocity, rightVelocity);
}
} // namespace mvlib
