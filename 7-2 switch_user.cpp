#include <unistd.h>
#include <sys/types.h>

static bool switch_to_user(uid_t user_id, gid_t gp_id) {

    // First make sure the target user is not root.
    if ((user_id == 0) && (gp_id == 0)) {

        return false;
    }

    // Make sure the current user is a legitimate user: root or target user.
    gid_t gid = getgid();
    uid_t uid = getuid();

    if (((gid != 0) or (uid != 0)) && ((gid != gp_id) or (uid != user_id))) {

        return false;
    }

    // If it is not root, it is already the target user.
    if (uid != 0) {

        return true;
    }

    // Switch to target user.
    if ((setgid(gp_id) < 0) or (setuid(user_id) < 0)) {

        return false;
    }

    return true;
}