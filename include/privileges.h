#ifndef PRIVILEGES_H_
#define PRIVILEGES_H_

#include <cerrno>
#include <cstring>
#include <grp.h>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace Privileges {

/**
 * Check if the process is running as root.
 */
inline bool isRoot() { return geteuid() == 0; }

/**
 * Get the current effective username of the process.
 *
 * @return Username of the current effective UID
 * @throws std::runtime_error if the user cannot be determined
 */
inline std::string getCurrentUser() {
  uid_t uid = geteuid();
  struct passwd *pw = getpwuid(uid);
  if (!pw) {
    throw std::runtime_error(std::string("Failed to get username for UID ") +
                             std::to_string(uid) + ": " + std::strerror(errno));
  }
  return std::string(pw->pw_name);
}

/**
 * Get the current effective group name of the process.
 *
 * @return Group name of the current effective GID
 * @throws std::runtime_error if the group cannot be determined
 */
inline std::string getCurrentGroup() {
  gid_t gid = getegid();
  struct group *gr = getgrgid(gid);
  if (!gr) {
    throw std::runtime_error(std::string("Failed to get group name for GID ") +
                             std::to_string(gid) + ": " + std::strerror(errno));
  }
  return std::string(gr->gr_name);
}

/**
 * Drop root privileges to the specified user and group.
 * Must be called after all privileged operations (e.g., binding to port 502).
 *
 * @param user  Username or numeric UID to switch to
 * @param group Group name or numeric GID to switch to (optional, uses user's
 * primary group if empty)
 * @throws std::runtime_error on failure
 */
inline void drop(const std::string &user, const std::string &group = "") {
  if (user.empty()) {
    throw std::runtime_error("User must be specified to drop privileges");
  }

  // Get user info
  struct passwd *pw = getpwnam(user.c_str());
  if (!pw) {
    // Try as numeric UID
    char *endptr;
    long uid = strtol(user.c_str(), &endptr, 10);
    if (*endptr == '\0' && uid >= 0) {
      pw = getpwuid(static_cast<uid_t>(uid));
    }
  }
  if (!pw) {
    throw std::runtime_error("Unknown user: " + user);
  }

  uid_t targetUid = pw->pw_uid;
  gid_t targetGid = pw->pw_gid;

  // Override group if specified
  if (!group.empty()) {
    struct group *gr = getgrnam(group.c_str());
    if (!gr) {
      // Try as numeric GID
      char *endptr;
      long gid = strtol(group.c_str(), &endptr, 10);
      if (*endptr == '\0' && gid >= 0) {
        gr = getgrgid(static_cast<gid_t>(gid));
      }
    }
    if (!gr) {
      throw std::runtime_error("Unknown group: " + group);
    }
    targetGid = gr->gr_gid;
  }

  // Drop supplementary groups
  if (setgroups(0, nullptr) != 0) {
    throw std::runtime_error(
        std::string("Failed to clear supplementary groups: ") +
        std::strerror(errno));
  }

  // Set GID first (must be done before dropping UID)
  if (setgid(targetGid) != 0) {
    throw std::runtime_error(std::string("Failed to set GID:  ") +
                             std::strerror(errno));
  }

  // Set UID last
  if (setuid(targetUid) != 0) {
    throw std::runtime_error(std::string("Failed to set UID: ") +
                             std::strerror(errno));
  }

  // Verify we can't regain root
  if (setuid(0) == 0) {
    throw std::runtime_error("Failed to permanently drop privileges");
  }
}

} // namespace Privileges

#endif /* PRIVILEGES_H_ */